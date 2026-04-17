/*
** LOOP: Loop Optimizations.
** Copyright (C) 2005-2026 Mike Pall. See Copyright Notice in luajit.h
*/

#define lj_opt_loop_c
#define LUA_CORE

#include "lj_obj.h"

#if LJ_HASJIT

#include "lj_err.h"
#include "lj_buf.h"
#include "lj_ir.h"
#include "lj_jit.h"
#include "lj_iropt.h"
#include "lj_trace.h"
#include "lj_snap.h"
#include "lj_vm.h"

/* Loop optimization:
**
** Traditional Loop-Invariant Code Motion (LICM) splits the instructions
** of a loop into invariant and variant instructions. The invariant
** instructions are hoisted out of the loop and only the variant
** instructions remain inside the loop body.
**
** Unfortunately LICM is mostly useless for compiling dynamic languages.
** The IR has many guards and most of the subsequent instructions are
** control-dependent on them. The first non-hoistable guard would
** effectively prevent hoisting of all subsequent instructions.
**
** That's why we use a special form of unrolling using copy-substitution,
** combined with redundancy elimination:
**
** The recorded instruction stream is re-emitted to the compiler pipeline
** with substituted operands. The substitution table is filled with the
** refs returned by re-emitting each instruction. This can be done
** on-the-fly, because the IR is in strict SSA form, where every ref is
** defined before its use.
**
** This aproach generates two code sections, separated by the LOOP
** instruction:
**
** 1. The recorded instructions form a kind of pre-roll for the loop. It
** contains a mix of invariant and variant instructions and performs
** exactly one loop iteration (but not necessarily the 1st iteration).
**
** 2. The loop body contains only the variant instructions and performs
** all remaining loop iterations.
**
** On first sight that looks like a waste of space, because the variant
** instructions are present twice. But the key insight is that the
** pre-roll honors the control-dependencies for *both* the pre-roll itself
** *and* the loop body!
**
** It also means one doesn't have to explicitly model control-dependencies
** (which, BTW, wouldn't help LICM much). And it's much easier to
** integrate sparse snapshotting with this approach.
**
** One of the nicest aspects of this approach is that all of the
** optimizations of the compiler pipeline (FOLD, CSE, FWD, etc.) can be
** reused with only minor restrictions (e.g. one should not fold
** instructions across loop-carried dependencies).
**
** But in general all optimizations can be applied which only need to look
** backwards into the generated instruction stream. At any point in time
** during the copy-substitution process this contains both a static loop
** iteration (the pre-roll) and a dynamic one (from the to-be-copied
** instruction up to the end of the partial loop body).
**
** Since control-dependencies are implicitly kept, CSE also applies to all
** kinds of guards. The major advantage is that all invariant guards can
** be hoisted, too.
**
** Load/store forwarding works across loop iterations, too. This is
** important if loop-carried dependencies are kept in upvalues or tables.
** E.g. 'self.idx = self.idx + 1' deep down in some OO-style method may
** become a forwarded loop-recurrence after inlining.
**
** Since the IR is in SSA form, loop-carried dependencies have to be
** modeled with PHI instructions. The potential candidates for PHIs are
** collected on-the-fly during copy-substitution. After eliminating the
** redundant ones, PHI instructions are emitted *below* the loop body.
**
** Note that this departure from traditional SSA form doesn't change the
** semantics of the PHI instructions themselves. But it greatly simplifies
** on-the-fly generation of the IR and the machine code.
*/

/* Some local macros to save typing. Undef'd at the end. */
#define IR(ref)		(&J->cur.ir[(ref)])

/* Pass IR on to next optimization in chain (FOLD). */
#define emitir(ot, a, b)	(lj_ir_set(J, (ot), (a), (b)), lj_opt_fold(J))

/* Emit raw IR without passing through optimizations. */
#define emitir_raw(ot, a, b)	(lj_ir_set(J, (ot), (a), (b)), lj_ir_emit(J))

/* -- PHI elimination ----------------------------------------------------- */

/* Emit or eliminate collected PHIs. */
static void loop_emit_phi(jit_State *J, IRRef1 *subst, IRRef1 *phi, IRRef nphi,
			  SnapNo onsnap)
{
  int passx = 0;
  IRRef i, j, nslots;
  IRRef invar = J->chain[IR_LOOP];
  /* Pass #1: mark redundant and potentially redundant PHIs. */
  for (i = 0, j = 0; i < nphi; i++) {
    IRRef lref = phi[i];
    IRRef rref = subst[lref];
    if (lref == rref || rref == REF_DROP) {  /* Invariants are redundant. */
      irt_clearphi(IR(lref)->t);
    } else {
      phi[j++] = (IRRef1)lref;
      if (!(IR(rref)->op1 == lref || IR(rref)->op2 == lref)) {
	/* Quick check for simple recurrences failed, need pass2. */
	irt_setmark(IR(lref)->t);
	passx = 1;
      }
    }
  }
  nphi = j;
  /* Pass #2: traverse variant part and clear marks of non-redundant PHIs. */
  if (passx) {
    SnapNo s;
    for (i = J->cur.nins-1; i > invar; i--) {
      IRIns *ir = IR(i);
      if (!irref_isk(ir->op2)) irt_clearmark(IR(ir->op2)->t);
      if (!irref_isk(ir->op1)) {
	irt_clearmark(IR(ir->op1)->t);
	if (ir->op1 < invar &&
	    ir->o >= IR_CALLN && ir->o <= IR_CARG) {  /* ORDER IR */
	  ir = IR(ir->op1);
	  while (ir->o == IR_CARG) {
	    if (!irref_isk(ir->op2)) irt_clearmark(IR(ir->op2)->t);
	    if (irref_isk(ir->op1)) break;
	    ir = IR(ir->op1);
	    irt_clearmark(ir->t);
	  }
	}
      }
    }
    for (s = J->cur.nsnap-1; s >= onsnap; s--) {
      SnapShot *snap = &J->cur.snap[s];
      SnapEntry *map = &J->cur.snapmap[snap->mapofs];
      MSize n, nent = snap->nent;
      for (n = 0; n < nent; n++) {
	IRRef ref = snap_ref(map[n]);
	if (!irref_isk(ref)) irt_clearmark(IR(ref)->t);
      }
    }
  }
  /* Pass #3: add PHIs for variant slots without a corresponding SLOAD. */
  nslots = J->baseslot+J->maxslot;
  for (i = 1; i < nslots; i++) {
    IRRef ref = tref_ref(J->slot[i]);
    while (!irref_isk(ref) && ref != subst[ref]) {
      IRIns *ir = IR(ref);
      irt_clearmark(ir->t);  /* Unmark potential uses, too. */
      if (irt_isphi(ir->t) || irt_ispri(ir->t))
	break;
      irt_setphi(ir->t);
      if (nphi >= LJ_MAX_PHI)
	lj_trace_err(J, LJ_TRERR_PHIOV);
      phi[nphi++] = (IRRef1)ref;
      ref = subst[ref];
      if (ref > invar)
	break;
    }
  }
  /* Pass #4: propagate non-redundant PHIs. */
  while (passx) {
    passx = 0;
    for (i = 0; i < nphi; i++) {
      IRRef lref = phi[i];
      IRIns *ir = IR(lref);
      if (!irt_ismarked(ir->t)) {  /* Propagate only from unmarked PHIs. */
	IRIns *irr = IR(subst[lref]);
	if (irt_ismarked(irr->t)) {  /* Right ref points to other PHI? */
	  irt_clearmark(irr->t);  /* Mark that PHI as non-redundant. */
	  passx = 1;  /* Retry. */
	}
      }
    }
  }
  /* Pass #5: emit PHI instructions or eliminate PHIs. */
  for (i = 0; i < nphi; i++) {
    IRRef lref = phi[i];
    IRIns *ir = IR(lref);
    if (!irt_ismarked(ir->t)) {  /* Emit PHI if not marked. */
      IRRef rref = subst[lref];
      if (rref > invar)
	irt_setphi(IR(rref)->t);
      emitir_raw(IRT(IR_PHI, irt_type(ir->t)), lref, rref);
    } else {  /* Otherwise eliminate PHI. */
      irt_clearmark(ir->t);
      irt_clearphi(ir->t);
    }
  }
}

/* -- Loop unrolling using copy-substitution ------------------------------ */

/* Copy-substitute snapshot. */
static void loop_subst_snap(jit_State *J, SnapShot *osnap,
			    SnapEntry *loopmap, IRRef1 *subst)
{
  SnapEntry *nmap, *omap = &J->cur.snapmap[osnap->mapofs];
  SnapEntry *nextmap = &J->cur.snapmap[snap_nextofs(&J->cur, osnap)];
  MSize nmapofs;
  MSize on, ln, nn, onent = osnap->nent;
  BCReg nslots = osnap->nslots;
  SnapShot *snap = &J->cur.snap[J->cur.nsnap];
  if (irt_isguard(J->guardemit)) {  /* Guard inbetween? */
    nmapofs = J->cur.nsnapmap;
    J->cur.nsnap++;  /* Add new snapshot. */
  } else {  /* Otherwise overwrite previous snapshot. */
    snap--;
    nmapofs = snap->mapofs;
  }
  J->guardemit.irt = 0;
  /* Setup new snapshot. */
  snap->mapofs = (uint32_t)nmapofs;
  snap->ref = (IRRef1)J->cur.nins;
  snap->mcofs = 0;
  snap->nslots = nslots;
  snap->topslot = osnap->topslot;
  snap->count = 0;
  nmap = &J->cur.snapmap[nmapofs];
  /* Substitute snapshot slots. */
  on = ln = nn = 0;
  while (on < onent) {
    SnapEntry osn = omap[on], lsn = loopmap[ln];
    if (snap_slot(lsn) < snap_slot(osn)) {  /* Copy slot from loop map. */
      nmap[nn++] = lsn;
      ln++;
    } else {  /* Copy substituted slot from snapshot map. */
      if (snap_slot(lsn) == snap_slot(osn)) ln++;  /* Shadowed loop slot. */
      if (!irref_isk(snap_ref(osn)))
	osn = snap_setref(osn, subst[snap_ref(osn)]);
      nmap[nn++] = osn;
      on++;
    }
  }
  while (snap_slot(loopmap[ln]) < nslots)  /* Copy remaining loop slots. */
    nmap[nn++] = loopmap[ln++];
  snap->nent = (uint8_t)nn;
  omap += onent;
  nmap += nn;
  while (omap < nextmap)  /* Copy PC + frame links. */
    *nmap++ = *omap++;
  J->cur.nsnapmap = (uint32_t)(nmap - J->cur.snapmap);
}

typedef struct LoopState {
  jit_State *J;
  IRRef1 *subst;
  MSize sizesubst;
#if LJ_TARGET_S390X
  uint8_t *stripov;
  MSize sizestripov;
#endif
} LoopState;

#if LJ_TARGET_S390X
static int loop_s390x_has_call(jit_State *J, IRRef invar);

static int loop_s390x_count_lt_clip_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1) {
    const char *opt_out = getenv("LUAJIT_S390X_DISABLE_COUNT_LT_CLIP");
    enabled = (opt_out == NULL);
  }
  return enabled;
}

static int loop_s390x_scev_ref_offset(jit_State *J, IRRef ref, int64_t *ofsp)
{
  int64_t ofs = 0;

  for (;;) {
    IRIns *ir;
    if (ref == J->scev.idx) {
      *ofsp = ofs;
      return 1;
    }
    if (irref_isk(ref))
      return 0;
    ir = IR(ref);
    if ((ir->o == IR_ADD || ir->o == IR_ADDOV) &&
	irref_isk(ir->op2) && IR(ir->op2)->o == IR_KINT) {
      ofs += IR(ir->op2)->i;
      ref = ir->op1;
      continue;
    }
    if ((ir->o == IR_SUB || ir->o == IR_SUBOV) &&
	irref_isk(ir->op2) && IR(ir->op2)->o == IR_KINT) {
      ofs -= IR(ir->op2)->i;
      ref = ir->op1;
      continue;
    }
    return 0;
  }
}

static int loop_s390x_pow2plus1_shift(int32_t k)
{
  uint32_t p;
  int shift = 1;

  if (k <= 1)
    return 0;
  p = (uint32_t)k - 1u;
  if ((p & (p - 1u)) != 0)
    return 0;
  while (p > 1u) {
    p >>= 1;
    shift++;
  }
  return shift;
}

static int loop_s390x_mod_scev_inc(jit_State *J, IRIns *ir, int32_t *kp,
				   int32_t *shiftp)
{
  int64_t ofs;
  int32_t k;
  int32_t shift;

  if (ir->o != IR_MOD || !irref_isk(ir->op2) ||
      IR(ir->op2)->o != IR_KINT)
    return 0;
  k = IR(ir->op2)->i;
  if (k > 32767)
    return 0;
  shift = loop_s390x_pow2plus1_shift(k);
  if (shift == 0)
    return 0;
  if (J->scev.idx == REF_NIL || !J->scev.dir ||
      J->scev.start == REF_NIL || !irref_isk(J->scev.start) ||
      J->scev.step == REF_NIL || !irref_isk(J->scev.step) ||
      IR(J->scev.step)->i != 1)
    return 0;
  if (!loop_s390x_scev_ref_offset(J, ir->op1, &ofs))
    return 0;
  if ((int64_t)IR(J->scev.start)->i + ofs < 0)
    return 0;
  *kp = k;
  *shiftp = shift;
  return 1;
}

static IRRef loop_s390x_emit_mod_step(jit_State *J, IRRef remref,
				      int32_t k, int32_t shift)
{
  IRRef rem_plus_1 = tref_ref(emitir_raw(IRTI(IR_ADD), remref, lj_ir_kint(J, 1)));
  IRRef biased = tref_ref(emitir_raw(IRTI(IR_ADD), remref, lj_ir_kint(J, k - 1)));
  IRRef wrap = tref_ref(emitir_raw(IRTI(IR_BSHR), biased, lj_ir_kint(J, shift)));
  IRRef wrapk = tref_ref(emitir_raw(IRTI(IR_MUL), wrap, lj_ir_kint(J, k)));
  return tref_ref(emitir_raw(IRTI(IR_SUB), rem_plus_1, wrapk));
}

static int loop_s390x_mod_value_scev_inc(jit_State *J, IRIns *ir,
					 int32_t *kp, int32_t *shiftp)
{
  IRIns *mod;

  if (ir->o != IR_ADD || !irref_isk(ir->op2) ||
      IR(ir->op2)->o != IR_KINT || IR(ir->op2)->i != 1 ||
      irref_isk(ir->op1))
    return 0;
  mod = IR(ir->op1);
  return loop_s390x_mod_scev_inc(J, mod, kp, shiftp);
}

static int loop_s390x_snap_uses_ref(jit_State *J, IRRef ref)
{
  SnapNo s;

  for (s = 1; s < J->cur.nsnap; s++) {
    SnapShot *snap = &J->cur.snap[s];
    SnapEntry *map = &J->cur.snapmap[snap->mapofs];
    MSize n, nent = snap->nent;
    for (n = 0; n < nent; n++)
      if (snap_ref(map[n]) == ref)
	return 1;
  }
  return 0;
}

static int loop_s390x_mod_value_only_use(jit_State *J, IRRef ref, IRRef invar)
{
  IRRef i;
  int seen = 0;

  if (loop_s390x_snap_uses_ref(J, ref))
    return 0;
  for (i = ref + 1; i < invar; i++) {
    IRIns *ir = IR(i);
    if (ir->op1 != ref && ir->op2 != ref)
      continue;
    if (ir->o == IR_ADD && ir->op1 == ref && irref_isk(ir->op2) &&
	IR(ir->op2)->o == IR_KINT && IR(ir->op2)->i == 1) {
      seen = 1;
      continue;
    }
    return 0;
  }
  return seen;
}

static IRRef loop_s390x_emit_mod_value_step(jit_State *J, IRRef valueref,
					    int32_t k, int32_t shift)
{
  IRRef value_plus_1 = tref_ref(emitir_raw(IRTI(IR_ADD), valueref, lj_ir_kint(J, 1)));
  IRRef biased = tref_ref(emitir_raw(IRTI(IR_ADD), valueref, lj_ir_kint(J, k - 2)));
  IRRef wrap = tref_ref(emitir_raw(IRTI(IR_BSHR), biased, lj_ir_kint(J, shift)));
  IRRef wrapk = tref_ref(emitir_raw(IRTI(IR_MUL), wrap, lj_ir_kint(J, k)));
  return tref_ref(emitir_raw(IRTI(IR_SUB), value_plus_1, wrapk));
}

static int loop_s390x_scev_stop_value(jit_State *J, int32_t *stopp)
{
  if (J->scev.stop == REF_NIL)
    return 0;
  if (irref_isk(J->scev.stop) && IR(J->scev.stop)->o == IR_KINT) {
    *stopp = IR(J->scev.stop)->i;
    return 1;
  }
  if (J->scev.idx != REF_NIL) {
    IRIns *idx = IR(J->scev.idx);
    if (idx->o == IR_SLOAD) {
      TValue *base = J->L->base - J->baseslot;
      *stopp = numberVint(&base[idx->op1 + FORL_STOP]);
      return 1;
    }
  }
  return 0;
}

static int loop_s390x_unit_scev_bounds(jit_State *J, int32_t *startp,
				       int32_t *stopp, int64_t *tripsp)
{
  int32_t start, stop;
  int64_t trips;

  if ((J->pt && (J->pt->flags & PROTO_VARARG)) ||
      loop_s390x_has_call(J, J->cur.nins) ||
      J->scev.idx == REF_NIL || !J->scev.dir ||
      J->scev.start == REF_NIL || !irref_isk(J->scev.start) ||
      J->scev.step == REF_NIL || !irref_isk(J->scev.step) ||
      IR(J->scev.step)->i != 1 || !loop_s390x_scev_stop_value(J, &stop))
    return 0;

  start = IR(J->scev.start)->i;
  trips = (int64_t)stop - (int64_t)start + 1;
  if (trips <= 0 || trips > 65535)
    return 0;

  if (startp) *startp = start;
  if (stopp) *stopp = stop;
  if (tripsp) *tripsp = trips;
  return 1;
}

static int loop_s390x_lowmask(int32_t k)
{
  uint32_t uk = (uint32_t)k;
  return k >= 0 && (uk & (uk + 1u)) == 0;
}

static int loop_s390x_ref_nonneg_max(jit_State *J, IRRef ref, int32_t stop,
				     int64_t *maxp)
{
  IRIns *ir;
  int64_t ofs;
  int64_t m1, m2;

  if (irref_isk(ref)) {
    if (IR(ref)->o == IR_KINT && IR(ref)->i >= 0) {
      *maxp = IR(ref)->i;
      return 1;
    }
    return 0;
  }

  if (loop_s390x_scev_ref_offset(J, ref, &ofs)) {
    int64_t minv;
    if (J->scev.start == REF_NIL || !irref_isk(J->scev.start))
      return 0;
    minv = (int64_t)IR(J->scev.start)->i + ofs;
    if (minv < 0)
      return 0;
    *maxp = (int64_t)stop + ofs;
    return *maxp >= 0 && *maxp <= INT32_MAX;
  }

  ir = IR(ref);
  if (ir->o == IR_BAND && irref_isk(ir->op2) && IR(ir->op2)->o == IR_KINT &&
      IR(ir->op2)->i >= 0) {
    *maxp = IR(ir->op2)->i;
    return 1;
  }

  if (ir->o == IR_ADD &&
      loop_s390x_ref_nonneg_max(J, ir->op1, stop, &m1) &&
      loop_s390x_ref_nonneg_max(J, ir->op2, stop, &m2) &&
      m1 + m2 <= INT32_MAX) {
    *maxp = m1 + m2;
    return 1;
  }

  return 0;
}

static int loop_s390x_bswap_zero_under_mask(jit_State *J, IRIns *ir,
					    int32_t stop, uint32_t mask)
{
  uint32_t bit;
  int64_t maxv;

  if (ir->o != IR_BSWAP || !loop_s390x_ref_nonneg_max(J, ir->op1, stop, &maxv))
    return 0;

  for (bit = 0; bit < 32; bit++) {
    uint32_t srcbit;
    if ((mask & (1u << bit)) == 0)
      continue;
    srcbit = bit < 8 ? bit + 24 : bit < 16 ? bit + 8 :
	     bit < 24 ? bit - 8 : bit - 24;
    if (srcbit < 31 && maxv >= (1u << srcbit))
      return 0;
  }
  return 1;
}

static int loop_s390x_ref_zero_under_mask(jit_State *J, IRRef ref,
					  int32_t stop, uint32_t mask)
{
  if (mask == 0)
    return 1;
  if (irref_isk(ref)) {
    if (IR(ref)->o == IR_KINT)
      return (((uint32_t)IR(ref)->i) & mask) == 0;
    return 0;
  }
  return loop_s390x_bswap_zero_under_mask(J, IR(ref), stop, mask);
}

static IRRef loop_s390x_emit_demanded_lowbits(jit_State *J, IRRef ref,
					      uint32_t mask, IRRef1 *subst,
					      int32_t stop, int depth)
{
  IRIns *ir;
  IRRef op1, op2;

  if (mask == 0)
    return lj_ir_kint(J, 0);
  if (irref_isk(ref) || depth <= 0)
    return irref_isk(ref) ? ref : subst[ref];
  if (loop_s390x_ref_zero_under_mask(J, ref, stop, mask))
    return lj_ir_kint(J, 0);

  ir = IR(ref);
  switch (ir->o) {
  case IR_BAND:
    if (irref_isk(ir->op2) && IR(ir->op2)->o == IR_KINT) {
      uint32_t kmask = (uint32_t)IR(ir->op2)->i;
      if ((kmask & mask) == 0)
	return lj_ir_kint(J, 0);
      if ((kmask & mask) == mask)
	return loop_s390x_emit_demanded_lowbits(J, ir->op1, mask, subst,
						stop, depth-1);
    }
    break;
  case IR_BOR:
  case IR_BXOR:
    if (loop_s390x_ref_zero_under_mask(J, ir->op1, stop, mask))
      return loop_s390x_emit_demanded_lowbits(J, ir->op2, mask, subst,
					      stop, depth-1);
    if (loop_s390x_ref_zero_under_mask(J, ir->op2, stop, mask))
      return loop_s390x_emit_demanded_lowbits(J, ir->op1, mask, subst,
					      stop, depth-1);
    op1 = loop_s390x_emit_demanded_lowbits(J, ir->op1, mask, subst,
					   stop, depth-1);
    op2 = loop_s390x_emit_demanded_lowbits(J, ir->op2, mask, subst,
					   stop, depth-1);
    if (op1 != subst[ir->op1] || op2 != subst[ir->op2])
      return tref_ref(emitir(IRT(ir->o, irt_type(ir->t)), op1, op2));
    break;
  default:
    break;
  }

  return subst[ref];
}

static int loop_s390x_copy_fold_band(jit_State *J, IRIns *ir, IRRef op1,
				     IRRef op2, IRRef1 *subst,
				     int32_t stop, IRRef *refp)
{
  int64_t maxv;
  int32_t k;
  IRRef simplified;

  if (ir->o != IR_BAND || !irref_isk(ir->op2) || IR(ir->op2)->o != IR_KINT)
    return 0;
  k = IR(ir->op2)->i;
  if (k < 0)
    return 0;

  if (loop_s390x_lowmask(k) &&
      loop_s390x_ref_nonneg_max(J, ir->op1, stop, &maxv) && maxv <= k) {
    *refp = op1;
    UNUSED(op2);
    return 1;
  }

  simplified = loop_s390x_emit_demanded_lowbits(J, ir->op1, (uint32_t)k,
						subst, stop, 8);
  if (simplified != op1) {
    *refp = tref_ref(emitir(IRTI(IR_BAND), simplified, op2));
    return 1;
  }

  return 0;
}

static int loop_s390x_addov_chain_from_acc(jit_State *J, IRRef ref,
					   int32_t stop, IRRef *rootp,
					   int64_t *maxp, uint8_t *stripov)
{
  IRIns *ir;
  IRRef root;
  int64_t chainmax, addmax;

  if (irref_isk(ref))
    return 0;
  ir = IR(ref);
  if (ir->o == IR_SLOAD && irt_isint(ir->t)) {
    *rootp = ref;
    *maxp = 0;
    return 1;
  }
  if (ir->o != IR_ADDOV || !irt_isint(ir->t))
    return 0;

  if (loop_s390x_addov_chain_from_acc(J, ir->op1, stop, &root, &chainmax,
				      stripov) &&
      loop_s390x_ref_nonneg_max(J, ir->op2, stop, &addmax) &&
      chainmax + addmax <= INT32_MAX) {
    *rootp = root;
    *maxp = chainmax + addmax;
    if (stripov)
      stripov[ref - REF_BIAS] = 1;
    return 1;
  }

  if (loop_s390x_addov_chain_from_acc(J, ir->op2, stop, &root, &chainmax,
				      stripov) &&
      loop_s390x_ref_nonneg_max(J, ir->op1, stop, &addmax) &&
      chainmax + addmax <= INT32_MAX) {
    *rootp = root;
    *maxp = chainmax + addmax;
    if (stripov)
      stripov[ref - REF_BIAS] = 1;
    return 1;
  }

  return 0;
}

static int loop_s390x_ref_has_ir_use(jit_State *J, IRRef ref, IRRef invar)
{
  IRRef i;
  for (i = ref + 1; i < invar; i++) {
    IRIns *ir = IR(i);
    if (ir->op1 == ref || ir->op2 == ref)
      return 1;
  }
  return 0;
}

static int loop_s390x_has_call(jit_State *J, IRRef invar)
{
  IRRef i;
  for (i = REF_FIRST; i < invar; i++) {
    IROp op = IR(i)->o;
    if (op >= IR_CALLN && op <= IR_CARG)
      return 1;
  }
  return 0;
}

static int loop_s390x_guard_stripov_recurrence(jit_State *J, IRRef invar,
					       uint8_t *stripov)
{
  IRRef ins, best = 0, bestroot = 0;
  int32_t stop;
  int64_t trips, bestmaxinc = 0;

  if (!loop_s390x_unit_scev_bounds(J, NULL, &stop, &trips) ||
      loop_s390x_has_call(J, invar))
    return 0;

  for (ins = REF_FIRST; ins < invar; ins++) {
    IRIns *ir = IR(ins);
    IRRef root = 0;
    int64_t maxinc = 0;
    if (ir->o != IR_ADDOV || !irt_isint(ir->t))
      continue;
    if (loop_s390x_ref_has_ir_use(J, ins, invar))
      continue;
    if (!loop_s390x_addov_chain_from_acc(J, ins, stop, &root, &maxinc, NULL))
      continue;
    if (maxinc < 200 || maxinc <= bestmaxinc)
      continue;
    best = ins;
    bestroot = root;
    bestmaxinc = maxinc;
  }

  if (best) {
    int64_t maxsum = bestmaxinc * trips;
    int64_t threshold;
    if (maxsum <= 0 || maxsum > INT32_MAX)
      return 0;
    threshold = (int64_t)INT32_MAX - maxsum;
    if (!irref_isk(J->scev.stop))
      emitir(IRTGI(IR_EQ), J->scev.stop, lj_ir_kint(J, stop));
    emitir(IRTGI(IR_LE), best, lj_ir_kint(J, (int32_t)threshold));
    loop_s390x_addov_chain_from_acc(J, best, stop, &bestroot, &bestmaxinc,
				    stripov);
    return 1;
  }

  return 0;
}

static int loop_s390x_kint_value(jit_State *J, IRRef ref, int32_t *kp)
{
  if (!irref_isk(ref) || IR(ref)->o != IR_KINT)
    return 0;
  *kp = IR(ref)->i;
  return 1;
}

static const BCIns *loop_s390x_guard_snap_pc(jit_State *J, IRRef ref)
{
  SnapNo s;
  for (s = 1; s < J->cur.nsnap; s++) {
    SnapShot *snap = &J->cur.snap[s];
    if (snap->ref == ref || snap->ref == ref + 1) {
      SnapEntry *map = &J->cur.snapmap[snap->mapofs];
      return snap_pc(&map[snap->nent]);
    }
  }
  return NULL;
}

static int loop_s390x_count_lt_false_to_forl(jit_State *J, IRRef guard)
{
  const BCIns *pc = loop_s390x_guard_snap_pc(J, guard);
  BCOp op;
  if (pc == NULL)
    return 0;
  op = bc_op(*pc);
  return op == BC_FORL || op == BC_JFORL;
}

static int loop_s390x_count_lt_body_ok(jit_State *J, IRRef guard,
				       IRRef bottom, IRRef inc)
{
  IRRef ref;
  for (ref = guard + 1; ref < bottom; ref++) {
    IRIns *ir = IR(ref);
    if (ref == inc)
      continue;
    switch (ir->o) {
    case IR_SLOAD:
    case IR_ADD:
    case IR_ADDOV:
      break;
    default:
      return 0;
    }
  }
  return 1;
}

static int loop_s390x_find_count_lt_clip(jit_State *J, IRRef invar,
					 IRRef *guardp, IRRef *bottomp,
					 IRRef *limitp)
{
  IRRef bottom;
  int32_t start;

  if (!loop_s390x_count_lt_clip_enabled() ||
      !loop_s390x_unit_scev_bounds(J, &start, NULL, NULL) ||
      start != 1 || J->scev.stop == REF_NIL)
    return 0;

  for (bottom = REF_FIRST; bottom < invar; bottom++) {
    IRIns *bir = IR(bottom), *incir;
    IRRef idx, guard;
    int32_t one;

    if (bir->o != IR_LE || !irt_isguard(bir->t) ||
	bir->op2 != J->scev.stop || irref_isk(bir->op1))
      continue;
    incir = IR(bir->op1);
    if (incir->o != IR_ADD || !loop_s390x_kint_value(J, incir->op2, &one) ||
	one != 1)
      continue;
    idx = incir->op1;

    for (guard = REF_FIRST; guard < bottom; guard++) {
      IRIns *gir = IR(guard);
      int32_t threshold;
      IRRef limit;
      if (gir->o != IR_LT || !irt_isguard(gir->t) || gir->op1 != idx ||
	  !loop_s390x_kint_value(J, gir->op2, &threshold) ||
	  threshold <= 1)
	continue;
      if (!loop_s390x_count_lt_false_to_forl(J, guard) ||
	  !loop_s390x_count_lt_body_ok(J, guard, bottom, bir->op1))
	continue;

      limit = tref_ref(emitir(IRTI(IR_MIN), J->scev.stop,
			      lj_ir_kint(J, threshold - 1)));
      *guardp = guard;
      *bottomp = bottom;
      *limitp = limit;
      return 1;
    }
  }
  return 0;
}
#endif

/* Unroll loop. */
static void loop_unroll(LoopState *lps)
{
  jit_State *J = lps->J;
  IRRef1 phi[LJ_MAX_PHI];
  uint32_t nphi = 0;
  IRRef1 *subst;
  SnapNo onsnap;
  SnapShot *osnap, *loopsnap;
  SnapEntry *loopmap, *psentinel;
  IRRef ins, invar;
#if LJ_TARGET_S390X
  int s390x_have_bounds = 0;
  int32_t s390x_stop = 0;
  IRRef s390x_clip_guard = 0;
  IRRef s390x_clip_bottom = 0;
  IRRef s390x_clip_limit = 0;
#endif

  /* Allocate substitution table.
  ** Only non-constant refs in [REF_BIAS,invar) are valid indexes.
  */
  invar = J->cur.nins;
  lps->sizesubst = invar - REF_BIAS;
  lps->subst = lj_mem_newvec(J->L, lps->sizesubst, IRRef1);
  subst = lps->subst - REF_BIAS;
  subst[REF_BASE] = REF_BASE;
#if LJ_TARGET_S390X
  lps->sizestripov = lps->sizesubst;
  lps->stripov = lj_mem_newvec(J->L, lps->sizestripov, uint8_t);
  {
    MSize i;
    for (i = 0; i < lps->sizestripov; i++)
      lps->stripov[i] = 0;
  }
  loop_s390x_guard_stripov_recurrence(J, invar, lps->stripov);
  loop_s390x_find_count_lt_clip(J, invar, &s390x_clip_guard,
				&s390x_clip_bottom, &s390x_clip_limit);
  s390x_have_bounds = loop_s390x_unit_scev_bounds(J, NULL, &s390x_stop, NULL);
#endif

  /* LOOP separates the pre-roll from the loop body. */
  emitir_raw(IRTG(IR_LOOP, IRT_NIL), 0, 0);

  /* Grow snapshot buffer and map for copy-substituted snapshots.
  ** Need up to twice the number of snapshots minus #0 and loop snapshot.
  ** Need up to twice the number of entries plus fallback substitutions
  ** from the loop snapshot entries for each new snapshot.
  ** Caveat: both calls may reallocate J->cur.snap and J->cur.snapmap!
  */
  onsnap = J->cur.nsnap;
  lj_snap_grow_buf(J, 2*onsnap-2);
  lj_snap_grow_map(J, J->cur.nsnapmap*2+(onsnap-2)*J->cur.snap[onsnap-1].nent);

  /* The loop snapshot is used for fallback substitutions. */
  loopsnap = &J->cur.snap[onsnap-1];
  loopmap = &J->cur.snapmap[loopsnap->mapofs];
  /* The PC of snapshot #0 and the loop snapshot must match. */
  psentinel = &loopmap[loopsnap->nent];
  lj_assertJ(*psentinel == J->cur.snapmap[J->cur.snap[0].nent],
	     "mismatched PC for loop snapshot");
  *psentinel = SNAP(255, 0, 0);  /* Replace PC with temporary sentinel. */

  /* Start substitution with snapshot #1 (#0 is empty for root traces). */
  osnap = &J->cur.snap[1];

  /* Copy and substitute all recorded instructions and snapshots. */
  for (ins = REF_FIRST; ins < invar; ins++) {
    IRIns *ir;
    IRRef op1, op2;

#if LJ_TARGET_S390X
    if (ins == s390x_clip_guard && osnap->ref == ins) {
      osnap++;
    }
#endif
    if (ins >= osnap->ref)  /* Instruction belongs to next snapshot? */
      loop_subst_snap(J, osnap++, loopmap, subst);  /* Copy-substitute it. */

    /* Substitute instruction operands. */
    ir = IR(ins);
#if LJ_TARGET_S390X
    if (ins == s390x_clip_guard) {
      subst[ins] = REF_DROP;
      continue;
    }
#endif
    op1 = ir->op1;
    if (!irref_isk(op1)) op1 = subst[op1];
    op2 = ir->op2;
    if (!irref_isk(op2)) op2 = subst[op2];
#if LJ_TARGET_S390X
    if (ins == s390x_clip_bottom)
      op2 = s390x_clip_limit;
#endif
#if LJ_TARGET_S390X
    {
      int32_t modk, modshift;
      if (loop_s390x_mod_value_scev_inc(J, ir, &modk, &modshift)) {
	IRRef ref = loop_s390x_emit_mod_value_step(J, ins, modk, modshift);
	subst[ins] = (IRRef1)ref;
	if (!irt_isphi(ir->t) && !irt_ispri(ir->t)) {
	  irt_setphi(ir->t);
	  if (nphi >= LJ_MAX_PHI)
	    lj_trace_err(J, LJ_TRERR_PHIOV);
	  phi[nphi++] = (IRRef1)ins;
	}
	continue;
      } else if (loop_s390x_mod_scev_inc(J, ir, &modk, &modshift)) {
	if (loop_s390x_mod_value_only_use(J, ins, invar)) {
	  subst[ins] = (IRRef1)ins;
	  continue;
	}
	IRRef ref = loop_s390x_emit_mod_step(J, ins, modk, modshift);
	subst[ins] = (IRRef1)ref;
	if (!irt_isphi(ir->t) && !irt_ispri(ir->t)) {
	  irt_setphi(ir->t);
	  if (nphi >= LJ_MAX_PHI)
	    lj_trace_err(J, LJ_TRERR_PHIOV);
	  phi[nphi++] = (IRRef1)ins;
	}
	continue;
      }
    }
#endif
#if LJ_TARGET_S390X
    if (s390x_have_bounds) {
      IRRef ref;
      if (loop_s390x_copy_fold_band(J, ir, op1, op2, subst, s390x_stop,
				    &ref)) {
	subst[ins] = (IRRef1)ref;
	continue;
      }
    }
#endif
    if (
#if LJ_TARGET_S390X
	lps->stripov && ins >= REF_BIAS &&
	lps->stripov[ins - REF_BIAS] && ir->o == IR_ADDOV
#else
	0
#endif
    ) {
      IRRef ref = tref_ref(emitir(IRT(IR_ADD, irt_type(ir->t)), op1, op2));
      subst[ins] = (IRRef1)ref;
      continue;
    }
    if (irm_kind(lj_ir_mode[ir->o]) == IRM_N &&
	op1 == ir->op1 && op2 == ir->op2) {  /* Regular invariant ins? */
      subst[ins] = (IRRef1)ins;  /* Shortcut. */
    } else {
      /* Re-emit substituted instruction to the FOLD/CSE/etc. pipeline. */
      IRType1 t = ir->t;  /* Get this first, since emitir may invalidate ir. */
      IRRef ref = tref_ref(emitir(ir->ot & ~IRT_ISPHI, op1, op2));
      subst[ins] = (IRRef1)ref;
      if (ref != ins) {
	IRIns *irr = IR(ref);
	if (ref < invar) {  /* Loop-carried dependency? */
	  /* Potential PHI? */
	  if (!irref_isk(ref) && !irt_isphi(irr->t) && !irt_ispri(irr->t)) {
	    irt_setphi(irr->t);
	    if (nphi >= LJ_MAX_PHI)
	      lj_trace_err(J, LJ_TRERR_PHIOV);
	    phi[nphi++] = (IRRef1)ref;
	  }
	  /* Check all loop-carried dependencies for type instability. */
	  if (!irt_sametype(t, irr->t)) {
	    if (irt_isinteger(t) && irt_isinteger(irr->t))
	      continue;
	    else if (irt_isnum(t) && irt_isinteger(irr->t))  /* Fix int->num. */
	      ref = tref_ref(emitir(IRTN(IR_CONV), ref, IRCONV_NUM_INT));
	    else if (irt_isnum(irr->t) && irt_isinteger(t))  /* Fix num->int. */
	      ref = tref_ref(emitir(IRTGI(IR_CONV), ref,
				    IRCONV_INT_NUM|IRCONV_CHECK));
	    else
	      lj_trace_err(J, LJ_TRERR_TYPEINS);
	    subst[ins] = (IRRef1)ref;
	    irr = IR(ref);
	    goto phiconv;
	  }
	} else if (ref != REF_DROP && ref > invar &&
		   ((irr->o == IR_CONV && irr->op1 < invar) ||
		    (irr->o == IR_ALEN && irr->op2 < invar &&
					  irr->op2 != REF_NIL))) {
	  /* May need an extra PHI for a CONV or ALEN hint. */
	  ref = irr->o == IR_CONV ? irr->op1 : irr->op2;
	  irr = IR(ref);
	phiconv:
	  if (ref < invar && !irref_isk(ref) && !irt_isphi(irr->t)) {
	    irt_setphi(irr->t);
	    if (nphi >= LJ_MAX_PHI)
	      lj_trace_err(J, LJ_TRERR_PHIOV);
	    phi[nphi++] = (IRRef1)ref;
	  }
	}
      }
    }
  }
  if (!irt_isguard(J->guardemit))  /* Drop redundant snapshot. */
    J->cur.nsnapmap = (uint32_t)J->cur.snap[--J->cur.nsnap].mapofs;
  lj_assertJ(J->cur.nsnapmap <= J->sizesnapmap, "bad snapshot map index");
  *psentinel = J->cur.snapmap[J->cur.snap[0].nent];  /* Restore PC. */

  loop_emit_phi(J, subst, phi, nphi, onsnap);
}

/* Undo any partial changes made by the loop optimization. */
static void loop_undo(jit_State *J, IRRef ins, SnapNo nsnap, MSize nsnapmap)
{
  ptrdiff_t i;
  SnapShot *snap = &J->cur.snap[nsnap-1];
  SnapEntry *map = J->cur.snapmap;
  map[snap->mapofs + snap->nent] = map[J->cur.snap[0].nent];  /* Restore PC. */
  J->cur.nsnapmap = (uint32_t)nsnapmap;
  J->cur.nsnap = nsnap;
  J->guardemit.irt = 0;
  lj_ir_rollback(J, ins);
  for (i = 0; i < BPROP_SLOTS; i++) {  /* Remove backprop. cache entries. */
    BPropEntry *bp = &J->bpropcache[i];
    if (bp->val >= ins)
      bp->key = 0;
  }
  for (ins--; ins >= REF_FIRST; ins--) {  /* Remove flags. */
    IRIns *ir = IR(ins);
    irt_clearphi(ir->t);
    irt_clearmark(ir->t);
  }
}

/* Protected callback for loop optimization. */
static TValue *cploop_opt(lua_State *L, lua_CFunction dummy, void *ud)
{
  UNUSED(L); UNUSED(dummy);
  loop_unroll((LoopState *)ud);
  return NULL;
}

/* Loop optimization. */
int lj_opt_loop(jit_State *J)
{
  IRRef nins = J->cur.nins;
  SnapNo nsnap = J->cur.nsnap;
  MSize nsnapmap = J->cur.nsnapmap;
  LoopState lps;
  int errcode;
  lps.J = J;
  lps.subst = NULL;
  lps.sizesubst = 0;
#if LJ_TARGET_S390X
  lps.stripov = NULL;
  lps.sizestripov = 0;
#endif
  errcode = lj_vm_cpcall(J->L, NULL, &lps, cploop_opt);
  lj_mem_freevec(J2G(J), lps.subst, lps.sizesubst, IRRef1);
#if LJ_TARGET_S390X
  lj_mem_freevec(J2G(J), lps.stripov, lps.sizestripov, uint8_t);
#endif
  if (LJ_UNLIKELY(errcode)) {
    lua_State *L = J->L;
    if (errcode == LUA_ERRRUN && tvisnumber(L->top-1)) {  /* Trace error? */
      int32_t e = numberVint(L->top-1);
      switch ((TraceError)e) {
      case LJ_TRERR_TYPEINS:  /* Type instability. */
      case LJ_TRERR_GFAIL:  /* Guard would always fail. */
	/* Unrolling via recording fixes many cases, e.g. a flipped boolean. */
	if (--J->instunroll < 0)  /* But do not unroll forever. */
	  break;
	L->top--;  /* Remove error object. */
	loop_undo(J, nins, nsnap, nsnapmap);
	return 1;  /* Loop optimization failed, continue recording. */
      default:
	break;
      }
    }
    lj_err_throw(L, errcode);  /* Propagate all other errors. */
  }
  return 0;  /* Loop optimization is ok. */
}

#undef IR
#undef emitir
#undef emitir_raw

#endif
