/*
** Trace recorder (bytecode -> SSA IR).
** Copyright (C) 2005-2026 Mike Pall. See Copyright Notice in luajit.h
*/

#define lj_record_c
#define LUA_CORE

#include "lj_obj.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if LJ_HASJIT

#include "lj_err.h"
#include "lj_str.h"
#include "lj_tab.h"
#include "lj_meta.h"
#include "lj_frame.h"
#if LJ_HASFFI
#include "lj_ctype.h"
#include "lj_cdata.h"
#include "lj_clib.h"
#endif
#include "lj_bc.h"
#include "lj_ff.h"
#if LJ_HASPROFILE
#include "lj_debug.h"
#endif
#include "lj_ir.h"
#include "lj_jit.h"
#include "lj_ircall.h"
#include "lj_iropt.h"
#include "lj_trace.h"
#include "lj_record.h"
#include "lj_ffrecord.h"
#include "lj_snap.h"
#include "lj_dispatch.h"
#include "lj_vm.h"
#include "lj_prng.h"

/* Some local macros to save typing. Undef'd at the end. */
#define IR(ref)			(&J->cur.ir[(ref)])

/* Pass IR on to next optimization in chain (FOLD). */
#define emitir(ot, a, b)	(lj_ir_set(J, (ot), (a), (b)), lj_opt_fold(J))

/* Emit raw IR without passing through optimizations. */
#define emitir_raw(ot, a, b)	(lj_ir_set(J, (ot), (a), (b)), lj_ir_emit(J))

static int s390x_recidx_log_enabled(void)
{
  return 0;
}

static void s390x_recidx_log_key(FILE *out, cTValue *tv)
{
  if (tvisstr(tv)) {
    GCstr *str = strV(tv);
    fprintf(out, "%.*s", (int)str->len, strdata(str));
  } else {
    fputs("<non-str>", out);
  }
}

static void s390x_recidx_log(jit_State *J, RecordIndex *ix, const char *phase,
			     IROp xrefop, cTValue *oldv)
{
  FILE *out;
  if (!s390x_recidx_log_enabled() || !ix->val || !tvisstr(&ix->keyv))
    return;
  out = stderr;
  fprintf(out,
	  "S390X_RECIDX phase=%s parent=%u exit=%u startpc=%p pc=%p xrefop=%d oldv_nil=%d oldv_ptr=%p key=",
	  phase, (unsigned int)J->parent, (unsigned int)J->exitno,
	  (void *)J->startpc, (void *)J->pc, (int)xrefop,
	  oldv == niltvg(J2G(J)), (void *)oldv);
  s390x_recidx_log_key(out, &ix->keyv);
  fprintf(out, " tab=%p hmask=%u asize=%u\n",
	  (void *)tabV(&ix->tabv),
	  (unsigned int)tabV(&ix->tabv)->hmask,
	  (unsigned int)tabV(&ix->tabv)->asize);
  fflush(out);
}

static int s390x_recret_log_enabled(void)
{
  return 0;
}

static int s390x_recret_slots_log_enabled(void)
{
  return 0;
}

static int s390x_funcjit_log_enabled(void)
{
  return 0;
}

static void s390x_funcjit_log(jit_State *J, const char *site, TraceNo lnk,
			      GCtrace *T)
{
  FILE *out;
  if (!s390x_funcjit_log_enabled())
    return;
  out = stderr;
  fprintf(out,
	  "S390X_FUNCJIT site=%s trace=%u parent=%u exit=%u pc=%p startpc=%p samepc=%u op=%u startop=%u lnk=%u callee_root=%u callee_startop=%u callee_link=%u callee_linktype=%u callee_startpc=%p\n",
	  site, (unsigned int)J->cur.traceno, (unsigned int)J->parent,
	  (unsigned int)J->exitno, (void *)J->pc, (void *)J->startpc,
	  (unsigned int)(J->pc == J->startpc),
	  (unsigned int)bc_op(*J->pc),
	  (unsigned int)bc_op(J->cur.startins),
	  (unsigned int)lnk,
	  (unsigned int)(T ? T->root : 0),
	  (unsigned int)(T ? bc_op(T->startins) : 0),
	  (unsigned int)(T ? T->link : 0),
	  (unsigned int)(T ? T->linktype : 0),
	  (void *)(T ? mref(T->startpc, BCIns) : NULL));
  fflush(out);
}

static void s390x_recret_log(jit_State *J, const char *phase, TValue *frame,
			     BCReg rbase, ptrdiff_t gotresults, BCReg baseadj)
{
  FILE *out;
  if (!s390x_recret_log_enabled())
    return;
  out = stderr;
  fprintf(out,
	  "S390X_RECRET phase=%s parent=%u exit=%u startpc=%p pc=%p frame=%p ftsz=0x%llx type=%d typep=%d framedepth=%d baseslot=%u maxslot=%u rbase=%u gotresults=%d baseadj=%u islua=%d iscont=%d isvarg=%d",
	  phase, (unsigned int)J->parent, (unsigned int)J->exitno,
	  (void *)J->startpc, (void *)J->pc, (void *)frame,
	  (unsigned long long)(uint64_t)frame_ftsz(frame),
	  (int)frame_type(frame), (int)frame_typep(frame), (int)J->framedepth,
	  (unsigned int)J->baseslot, (unsigned int)J->maxslot,
	  (unsigned int)rbase, (int)gotresults, (unsigned int)baseadj,
	  frame_islua(frame), frame_iscont(frame), frame_isvarg(frame));
  if (frame_islua(frame) || frame_iscont(frame))
    fprintf(out, " frame_pc=%p", (void *)frame_pc(frame));
  if (frame_iscont(frame))
    fprintf(out, " cont=%p delta=%u", (void *)frame_contf(frame),
	    (unsigned int)frame_delta(frame));
  if (frame_isvarg(frame))
    fprintf(out, " delta=%u", (unsigned int)frame_delta(frame));
  fputc('\n', out);
  fflush(out);
}

static void s390x_recret_branch_log(jit_State *J, const char *site,
				    TValue *frame, BCReg rbase,
				    ptrdiff_t gotresults, BCReg baseadj,
				    BCReg cbase, ptrdiff_t nresults)
{
  FILE *out;
  BCOp op = bc_op(*J->pc);
  BCOp startop = bc_op(J->cur.startins);
  BCOp prevop = J->pc > proto_bc(J->pt) ? bc_op(J->pc[-1]) : BC__MAX;
  if (!s390x_recret_log_enabled())
    return;
  out = stderr;
  fprintf(out,
	  "S390X_RECRET_BRANCH site=%s trace=%u parent=%u exit=%u pc=%p op=%u prevop=%u startop=%u root=%u framedepth=%u retdepth=%u baseslot=%u maxslot=%u rbase=%u gotresults=%d baseadj=%u cbase=%u nresults=%d frame=%p islua=%d iscont=%d isvarg=%d\n",
	  site, (unsigned int)J->cur.traceno, (unsigned int)J->parent,
	  (unsigned int)J->exitno, (void *)J->pc, (unsigned int)op,
	  (unsigned int)prevop, (unsigned int)startop,
	  (unsigned int)J->cur.root, (unsigned int)J->framedepth,
	  (unsigned int)J->retdepth, (unsigned int)J->baseslot,
	  (unsigned int)J->maxslot, (unsigned int)rbase, (int)gotresults,
	  (unsigned int)baseadj, (unsigned int)cbase, (int)nresults,
	  (void *)frame, frame_islua(frame), frame_iscont(frame),
	  frame_isvarg(frame));
  fflush(out);
}

static void s390x_recret_slots_log(jit_State *J, const char *site,
				   BCReg cbase, ptrdiff_t nresults)
{
  int i, limit;
  FILE *out;
  if (!s390x_recret_slots_log_enabled())
    return;
  out = stderr;
  limit = (int)(J->maxslot + cbase + 4);
  if (limit < 12) limit = 12;
  if (limit > 24) limit = 24;
  fprintf(out,
	  "S390X_RECRET_SLOTS site=%s trace=%u parent=%u exit=%u baseslot=%u maxslot=%u cbase=%u nresults=%d\n",
	  site, (unsigned int)J->cur.traceno, (unsigned int)J->parent,
	  (unsigned int)J->exitno, (unsigned int)J->baseslot,
	  (unsigned int)J->maxslot, (unsigned int)cbase, (int)nresults);
  for (i = -((int)LJ_FR2 + 1); i < limit; i++) {
    fprintf(out, "S390X_RECRET_SLOT idx=%d tref=%d\n", i, (int)J->base[i]);
  }
  fflush(out);
}

/* -- Sanity checks ------------------------------------------------------- */

#ifdef LUA_USE_ASSERT
/* Sanity check the whole IR -- sloooow. */
static void rec_check_ir(jit_State *J)
{
  IRRef i, nins = J->cur.nins, nk = J->cur.nk;
  lj_assertJ(nk <= REF_BIAS && nins >= REF_BIAS && nins < 65536,
	     "inconsistent IR layout");
  for (i = nk; i < nins; i++) {
    IRIns *ir = IR(i);
    uint32_t mode = lj_ir_mode[ir->o];
    IRRef op1 = ir->op1;
    IRRef op2 = ir->op2;
    const char *err = NULL;
    switch (irm_op1(mode)) {
    case IRMnone:
      if (op1 != 0) err = "IRMnone op1 used";
      break;
    case IRMref:
      if (op1 < nk || (i >= REF_BIAS ? op1 >= i : op1 <= i))
	err = "IRMref op1 out of range";
      break;
    case IRMlit: break;
    case IRMcst:
      if (i >= REF_BIAS) { err = "constant in IR range"; break; }
      if (irt_is64(ir->t) && ir->o != IR_KNULL)
	i++;
      continue;
    }
    switch (irm_op2(mode)) {
    case IRMnone:
      if (op2) err = "IRMnone op2 used";
      break;
    case IRMref:
      if (op2 < nk || (i >= REF_BIAS ? op2 >= i : op2 <= i))
	err = "IRMref op2 out of range";
      break;
    case IRMlit: break;
    case IRMcst: err = "IRMcst op2"; break;
    }
    if (!err && ir->prev) {
      if (ir->prev < nk || (i >= REF_BIAS ? ir->prev >= i : ir->prev <= i))
	err = "chain out of range";
      else if (ir->o != IR_NOP && IR(ir->prev)->o != ir->o)
	err = "chain to different op";
    }
    lj_assertJ(!err, "bad IR %04d op %d(%04d,%04d): %s",
	       i-REF_BIAS,
	       ir->o,
	       irm_op1(mode) == IRMref ? op1-REF_BIAS : op1,
	       irm_op2(mode) == IRMref ? op2-REF_BIAS : op2,
	       err);
  }
}

/* Compare stack slots and frames of the recorder and the VM. */
static void rec_check_slots(jit_State *J)
{
  BCReg s, nslots = J->baseslot + J->maxslot;
  int32_t depth = 0;
  cTValue *base = J->L->base - J->baseslot;
  lj_assertJ(J->baseslot >= 1+LJ_FR2, "bad baseslot");
  lj_assertJ(J->baseslot == 1+LJ_FR2 || (J->slot[J->baseslot-1] & TREF_FRAME),
	     "baseslot does not point to frame");
  lj_assertJ(nslots <= LJ_MAX_JSLOTS, "slot overflow");
  for (s = 0; s < nslots; s++) {
    TRef tr = J->slot[s];
    if (tr) {
      cTValue *tv = &base[s];
      IRRef ref = tref_ref(tr);
      IRIns *ir = NULL;  /* Silence compiler. */
      lj_assertJ(tv < J->L->top, "slot %d above top of Lua stack", s);
      if (!LJ_FR2 || ref || !(tr & (TREF_FRAME | TREF_CONT))) {
	lj_assertJ(ref >= J->cur.nk && ref < J->cur.nins,
		   "slot %d ref %04d out of range", s, ref - REF_BIAS);
	ir = IR(ref);
	lj_assertJ(irt_t(ir->t) == tref_t(tr), "slot %d IR type mismatch", s);
      }
      if (s == 0) {
	lj_assertJ(tref_isfunc(tr), "frame slot 0 is not a function");
#if LJ_FR2
      } else if (s == 1) {
	lj_assertJ((tr & ~TREF_FRAME) == 0, "bad frame slot 1");
#endif
      } else if ((tr & TREF_FRAME)) {
	GCfunc *fn = gco2func(frame_gc(tv));
	BCReg delta = (BCReg)(tv - frame_prev(tv));
#if LJ_FR2
	lj_assertJ(!ref || ir_knum(ir)->u64 == tv->u64,
		   "frame slot %d PC mismatch", s);
	tr = J->slot[s-1];
	ir = IR(tref_ref(tr));
#endif
	lj_assertJ(tref_isfunc(tr),
		   "frame slot %d is not a function", s-LJ_FR2);
	lj_assertJ(!tref_isk(tr) || fn == ir_kfunc(ir),
		   "frame slot %d function mismatch", s-LJ_FR2);
	lj_assertJ(s > delta + LJ_FR2 ? (J->slot[s-delta] & TREF_FRAME)
				      : (s == delta + LJ_FR2),
		   "frame slot %d broken chain", s-LJ_FR2);
	depth++;
      } else if ((tr & TREF_CONT)) {
#if LJ_FR2
	lj_assertJ(!ref || ir_knum(ir)->u64 == tv->u64,
		   "cont slot %d continuation mismatch", s);
#else
	lj_assertJ(ir_kptr(ir) == gcrefp(tv->gcr, void),
		   "cont slot %d continuation mismatch", s);
#endif
	lj_assertJ((J->slot[s+1+LJ_FR2] & TREF_FRAME),
		   "cont slot %d not followed by frame", s);
	depth++;
      } else if ((tr & TREF_KEYINDEX)) {
	lj_assertJ(tref_isint(tr), "keyindex slot %d bad type %d",
				   s, tref_type(tr));
      } else {
	/* Number repr. may differ, but other types must be the same. */
	lj_assertJ(tvisnumber(tv) ? tref_isnumber(tr) :
				    itype2irt(tv) == tref_type(tr),
		   "slot %d type mismatch: stack type %d vs IR type %d",
		   s, itypemap(tv), tref_type(tr));
	if (tref_isk(tr)) {  /* Compare constants. */
	  TValue tvk;
	  lj_ir_kvalue(J->L, &tvk, ir);
	  lj_assertJ((tvisnum(&tvk) && tvisnan(&tvk)) ?
		     (tvisnum(tv) && tvisnan(tv)) :
		     lj_obj_equal(tv, &tvk),
		     "slot %d const mismatch: stack %016llx vs IR %016llx",
		     s, tv->u64, tvk.u64);
	}
      }
    }
  }
  lj_assertJ(J->framedepth == depth,
	     "frame depth mismatch %d vs %d", J->framedepth, depth);
}
#endif

/* -- Type handling and specialization ------------------------------------ */

/* Note: these functions return tagged references (TRef). */

/* Specialize a slot to a specific type. Note: slot can be negative! */
static TRef sloadt(jit_State *J, int32_t slot, IRType t, int mode)
{
  /* Caller may set IRT_GUARD in t. */
  TRef ref = emitir_raw(IRT(IR_SLOAD, t), (int32_t)J->baseslot+slot, mode);
  J->base[slot] = ref;
  return ref;
}

/* Specialize a slot to the runtime type. Note: slot can be negative! */
static TRef sload(jit_State *J, int32_t slot)
{
  IRType t = itype2irt(&J->L->base[slot]);
  TRef ref = emitir_raw(IRTG(IR_SLOAD, t), (int32_t)J->baseslot+slot,
			IRSLOAD_TYPECHECK);
  if (irtype_ispri(t)) ref = TREF_PRI(t);  /* Canonicalize primitive refs. */
  J->base[slot] = ref;
  return ref;
}

/* Get TRef from slot. Load slot and specialize if not done already. */
#define getslot(J, s)	(J->base[(s)] ? J->base[(s)] : sload(J, (int32_t)(s)))

/* Get TRef for current function. */
static TRef getcurrf(jit_State *J)
{
  if (J->base[-1-LJ_FR2])
    return J->base[-1-LJ_FR2];
  /* Non-base frame functions ought to be loaded already. */
  lj_assertJ(J->baseslot == 1+LJ_FR2, "bad baseslot");
  return sloadt(J, -1-LJ_FR2, IRT_FUNC, IRSLOAD_READONLY);
}

#if LJ_TARGET_S390X
static int lj_record_s390x_string_sub_eq_memcmp_enabled(void)
{
  return 1;
}

static TRef lj_record_s390x_ref_tref(jit_State *J, IRRef ref)
{
  return TREF(ref, irt_t(IR(ref)->t));
}

static int lj_record_s390x_string_sub_eq_memcmp(jit_State *J, TRef a, TRef b,
						cTValue *av, cTValue *bv,
						int diff)
{
  TRef snew, other;
  IRIns *snewir, *strrefir;
  TRef sptr, slen, olen, optr, eq;
  GCstr *snewstr, *otherstr;

  if (!lj_record_s390x_string_sub_eq_memcmp_enabled() ||
      !tvisstr(av) || !tvisstr(bv))
    return 0;

  if (!tref_isk(a) && IR(tref_ref(a))->o == IR_SNEW) {
    snew = a;
    other = b;
    snewstr = strV(av);
    otherstr = strV(bv);
  } else if (!tref_isk(b) && IR(tref_ref(b))->o == IR_SNEW) {
    snew = b;
    other = a;
    snewstr = strV(bv);
    otherstr = strV(av);
  } else {
    return 0;
  }

  if (!tref_isstr(other) ||
      (!tref_isk(other) && IR(tref_ref(other))->o == IR_SNEW))
    return 0;

  snewir = IR(tref_ref(snew));
  strrefir = IR(snewir->op1);
  if (strrefir->o != IR_STRREF)
    return 0;

  sptr = lj_record_s390x_ref_tref(J, snewir->op1);
  slen = lj_record_s390x_ref_tref(J, snewir->op2);
  olen = emitir(IRTI(IR_FLOAD), other, IRFL_STR_LEN);

  if (diff && snewstr->len != otherstr->len) {
    emitir(IRTGI(IR_NE), slen, olen);
    return 1;
  }

  emitir(IRTGI(IR_EQ), slen, olen);
  emitir(IRTGI(IR_ULE), slen, lj_ir_kint(J, 256));
  optr = emitir(IRT(IR_STRREF, IRT_PGC), other, lj_ir_kint(J, 0));
  eq = lj_ir_call(J, IRCALL_lj_str_equal_256, sptr, optr, slen);
  emitir(IRTGI(diff ? IR_EQ : IR_NE), eq, lj_ir_kint(J, 0));
  return 1;
}
#endif

/* Compare for raw object equality.
** Returns 0 if the objects are the same.
** Returns 1 if they are different, but the same type.
** Returns 2 for two different types.
** Comparisons between primitives always return 1 -- no caller cares about it.
*/
int lj_record_objcmp(jit_State *J, TRef a, TRef b, cTValue *av, cTValue *bv)
{
  int diff = !lj_obj_equal(av, bv);
  if (!tref_isk2(a, b)) {  /* Shortcut, also handles primitives. */
    IRType ta = tref_isinteger(a) ? IRT_INT : tref_type(a);
    IRType tb = tref_isinteger(b) ? IRT_INT : tref_type(b);
    if (ta != tb) {
      /* Widen mixed number/int comparisons to number/number comparison. */
      if (ta == IRT_INT && tb == IRT_NUM) {
	a = emitir(IRTN(IR_CONV), a, IRCONV_NUM_INT);
	ta = IRT_NUM;
      } else if (ta == IRT_NUM && tb == IRT_INT) {
	b = emitir(IRTN(IR_CONV), b, IRCONV_NUM_INT);
      } else {
	return 2;  /* Two different types are never equal. */
      }
    }
#if LJ_TARGET_S390X
    if (ta == IRT_STR && tb == IRT_STR &&
	lj_record_s390x_string_sub_eq_memcmp(J, a, b, av, bv, diff))
      return diff;
#endif
    emitir(IRTG(diff ? IR_NE : IR_EQ, ta), a, b);
  }
  return diff;
}

/* Constify a value. Returns 0 for non-representable object types. */
TRef lj_record_constify(jit_State *J, cTValue *o)
{
  if (tvisgcv(o))
    return lj_ir_kgc(J, gcV(o), itype2irt(o));
  else if (tvisint(o))
    return lj_ir_kint(J, intV(o));
  else if (tvisnum(o))
    return lj_ir_knumint(J, numV(o));
  else if (tvisbool(o))
    return TREF_PRI(itype2irt(o));
  else
    return 0;  /* Can't represent lightuserdata (pointless). */
}

/* Emit a VLOAD with the correct type. */
TRef lj_record_vload(jit_State *J, TRef ref, MSize idx, IRType t)
{
  TRef tr = emitir(IRTG(IR_VLOAD, t), ref, idx);
  if (irtype_ispri(t)) tr = TREF_PRI(t);  /* Canonicalize primitives. */
  return tr;
}

/* -- Record loop ops ----------------------------------------------------- */

/* Loop event. */
typedef enum {
  LOOPEV_LEAVE,		/* Loop is left or not entered. */
  LOOPEV_ENTERLO,	/* Loop is entered with a low iteration count left. */
  LOOPEV_ENTER		/* Loop is entered. */
} LoopEvent;

static int lj_record_s390x_stop_log_enabled(void);
static void lj_record_s390x_ir_log(jit_State *J, TraceLink linktype, TraceNo lnk);
static int lj_record_s390x_mark_nil_desc_done_enabled(void);
static int lj_record_s390x_fori_arg_log_enabled(void);
static TRef rec_upvalue(jit_State *J, uint32_t uv, TRef val);

#ifndef LUAJIT_ENABLE_S390X_SEMANTIC_REDUCERS
#define LUAJIT_ENABLE_S390X_SEMANTIC_REDUCERS 1
#endif

#if LJ_TARGET_S390X && LUAJIT_ENABLE_S390X_SEMANTIC_REDUCERS
#define LJ_RECORD_S390X_SEMANTIC_REDUCERS 1
#else
#define LJ_RECORD_S390X_SEMANTIC_REDUCERS 0
#endif

#ifndef LUAJIT_ENABLE_S390X_STRING_CYCLE_REDUCERS
#define LUAJIT_ENABLE_S390X_STRING_CYCLE_REDUCERS \
  LUAJIT_ENABLE_S390X_SEMANTIC_REDUCERS
#endif

#ifndef LUAJIT_ENABLE_S390X_STRING_CONCAT_SLICE_REDUCER
#define LUAJIT_ENABLE_S390X_STRING_CONCAT_SLICE_REDUCER \
  LUAJIT_ENABLE_S390X_STRING_CYCLE_REDUCERS
#endif

#ifndef LUAJIT_ENABLE_S390X_STRING_MANUAL_FIND_CYCLE_REDUCER
#define LUAJIT_ENABLE_S390X_STRING_MANUAL_FIND_CYCLE_REDUCER \
  LUAJIT_ENABLE_S390X_STRING_CYCLE_REDUCERS
#endif

#ifndef LUAJIT_ENABLE_S390X_STRING_BYTE_SCAN_CYCLE_REDUCER
#define LUAJIT_ENABLE_S390X_STRING_BYTE_SCAN_CYCLE_REDUCER \
  LUAJIT_ENABLE_S390X_STRING_CYCLE_REDUCERS
#endif

#ifndef LUAJIT_ENABLE_S390X_COMPONENT_LOOP_REDUCERS
#define LUAJIT_ENABLE_S390X_COMPONENT_LOOP_REDUCERS \
  LUAJIT_ENABLE_S390X_SEMANTIC_REDUCERS
#endif

#ifndef LUAJIT_ENABLE_S390X_FFI_CDATA_REDUCERS
#define LUAJIT_ENABLE_S390X_FFI_CDATA_REDUCERS \
  LUAJIT_ENABLE_S390X_SEMANTIC_REDUCERS
#endif
#ifndef LUAJIT_ENABLE_S390X_LOGIC_LOW32_REDUCERS
#define LUAJIT_ENABLE_S390X_LOGIC_LOW32_REDUCERS \
  LUAJIT_ENABLE_S390X_SEMANTIC_REDUCERS
#endif
#ifndef LUAJIT_ENABLE_S390X_NUMERIC_MOD_REDUCERS
#define LUAJIT_ENABLE_S390X_NUMERIC_MOD_REDUCERS \
  LUAJIT_ENABLE_S390X_SEMANTIC_REDUCERS
#endif
#ifndef LUAJIT_ENABLE_S390X_ITERATOR_TABLE_REDUCER
#define LUAJIT_ENABLE_S390X_ITERATOR_TABLE_REDUCER \
  LUAJIT_ENABLE_S390X_SEMANTIC_REDUCERS
#endif

#ifndef LUAJIT_ENABLE_S390X_MINMAX_LOOP_REDUCER
#define LUAJIT_ENABLE_S390X_MINMAX_LOOP_REDUCER \
  LUAJIT_ENABLE_S390X_NUMERIC_MOD_REDUCERS
#endif

#ifndef LUAJIT_ENABLE_S390X_CENTERED_MOD_ABS_REDUCER
#define LUAJIT_ENABLE_S390X_CENTERED_MOD_ABS_REDUCER \
  LUAJIT_ENABLE_S390X_NUMERIC_MOD_REDUCERS
#endif

#if LJ_TARGET_S390X && LUAJIT_ENABLE_S390X_STRING_CYCLE_REDUCERS
#define LJ_RECORD_S390X_STRING_CYCLE_REDUCERS 1
#else
#define LJ_RECORD_S390X_STRING_CYCLE_REDUCERS 0
#endif

#if LJ_TARGET_S390X && LUAJIT_ENABLE_S390X_STRING_CONCAT_SLICE_REDUCER
#define LJ_RECORD_S390X_STRING_CONCAT_SLICE_REDUCER 1
#else
#define LJ_RECORD_S390X_STRING_CONCAT_SLICE_REDUCER 0
#endif

#if LJ_TARGET_S390X && LUAJIT_ENABLE_S390X_STRING_MANUAL_FIND_CYCLE_REDUCER
#define LJ_RECORD_S390X_STRING_MANUAL_FIND_CYCLE_REDUCER 1
#else
#define LJ_RECORD_S390X_STRING_MANUAL_FIND_CYCLE_REDUCER 0
#endif

#if LJ_TARGET_S390X && LUAJIT_ENABLE_S390X_STRING_BYTE_SCAN_CYCLE_REDUCER
#define LJ_RECORD_S390X_STRING_BYTE_SCAN_CYCLE_REDUCER 1
#else
#define LJ_RECORD_S390X_STRING_BYTE_SCAN_CYCLE_REDUCER 0
#endif

#if LJ_TARGET_S390X && LUAJIT_ENABLE_S390X_COMPONENT_LOOP_REDUCERS
#define LJ_RECORD_S390X_COMPONENT_LOOP_REDUCERS 1
#else
#define LJ_RECORD_S390X_COMPONENT_LOOP_REDUCERS 0
#endif

#if LJ_TARGET_S390X && LUAJIT_ENABLE_S390X_ITERATOR_TABLE_REDUCER
#define LJ_RECORD_S390X_ITERATOR_TABLE_REDUCER 1
#else
#define LJ_RECORD_S390X_ITERATOR_TABLE_REDUCER 0
#endif

#if LJ_TARGET_S390X && LJ_HASFFI && LUAJIT_ENABLE_S390X_FFI_CDATA_REDUCERS
#define LJ_RECORD_S390X_FFI_CDATA_REDUCERS 1
#else
#define LJ_RECORD_S390X_FFI_CDATA_REDUCERS 0
#endif

#if LJ_TARGET_S390X && LUAJIT_ENABLE_S390X_LOGIC_LOW32_REDUCERS
#define LJ_RECORD_S390X_LOGIC_LOW32_REDUCERS 1
#else
#define LJ_RECORD_S390X_LOGIC_LOW32_REDUCERS 0
#endif

#if LJ_TARGET_S390X && LUAJIT_ENABLE_S390X_NUMERIC_MOD_REDUCERS
#define LJ_RECORD_S390X_NUMERIC_MOD_REDUCERS 1
#else
#define LJ_RECORD_S390X_NUMERIC_MOD_REDUCERS 0
#endif

#if LJ_RECORD_S390X_NUMERIC_MOD_REDUCERS
static int lj_record_s390x_mod_branch_ifconv_enabled(void)
{
  return LJ_RECORD_S390X_NUMERIC_MOD_REDUCERS;
}

static int lj_record_s390x_mod_select_loop_sum_enabled(void)
{
  return LJ_RECORD_S390X_NUMERIC_MOD_REDUCERS;
}

static int lj_record_s390x_mod_rem_select_loop_sum_enabled(void)
{
  return LJ_RECORD_S390X_NUMERIC_MOD_REDUCERS;
}

static int lj_record_s390x_mod_loop_sum_enabled(void)
{
  return LJ_RECORD_S390X_NUMERIC_MOD_REDUCERS;
}

static int lj_record_s390x_mod_scaled_loop_sum_enabled(void)
{
  return LJ_RECORD_S390X_NUMERIC_MOD_REDUCERS;
}

static int lj_record_s390x_minmax_loop_sum_enabled(void)
{
  return LJ_TARGET_S390X && LUAJIT_ENABLE_S390X_MINMAX_LOOP_REDUCER;
}

static int lj_record_s390x_centered_mod_abs_loop_sum_enabled(void)
{
  return LJ_TARGET_S390X && LUAJIT_ENABLE_S390X_CENTERED_MOD_ABS_REDUCER;
}
#endif

#if LJ_RECORD_S390X_STRING_CONCAT_SLICE_REDUCER
static int lj_record_s390x_concat_slice_enabled(void)
{
  return LJ_TARGET_S390X && LUAJIT_ENABLE_S390X_STRING_CONCAT_SLICE_REDUCER;
}
#endif

#if LJ_RECORD_S390X_STRING_MANUAL_FIND_CYCLE_REDUCER
static int lj_record_s390x_manual_find_cycle_enabled(void)
{
  return LJ_TARGET_S390X && LUAJIT_ENABLE_S390X_STRING_MANUAL_FIND_CYCLE_REDUCER;
}
#endif

#if LJ_RECORD_S390X_STRING_BYTE_SCAN_CYCLE_REDUCER
static int lj_record_s390x_byte_scan_cycle_enabled(void)
{
  return LJ_TARGET_S390X && LUAJIT_ENABLE_S390X_STRING_BYTE_SCAN_CYCLE_REDUCER;
}
#endif

#if LJ_RECORD_S390X_STRING_CONCAT_SLICE_REDUCER || \
    LJ_RECORD_S390X_STRING_MANUAL_FIND_CYCLE_REDUCER || \
    LJ_RECORD_S390X_STRING_BYTE_SCAN_CYCLE_REDUCER
static int lj_record_s390x_kgc_str_eq(GCproto *pt, BCReg idx,
				       const char *name, size_t namelen)
{
  GCstr *s;
  if (pt == NULL)
    return 0;
  s = gco2str(proto_kgc(pt, ~(ptrdiff_t)idx));
  return s->len == (MSize)namelen && memcmp(strdata(s), name, namelen) == 0;
}
#endif

static TRef lj_record_s390x_raw_tab_getstr(jit_State *J, TRef tab,
					    GCtab *tabv, GCstr *key)
{
  RecordIndex ix;
  settabV(J->L, &ix.tabv, tabv);
  setstrV(J->L, &ix.keyv, key);
  ix.tab = tab;
  ix.key = lj_ir_kstr(J, key);
  ix.val = 0;
  ix.idxchain = 0;
  return lj_record_idx(J, &ix);
}

#if LJ_RECORD_S390X_STRING_CONCAT_SLICE_REDUCER || \
    LJ_RECORD_S390X_STRING_MANUAL_FIND_CYCLE_REDUCER || \
    LJ_RECORD_S390X_STRING_BYTE_SCAN_CYCLE_REDUCER
static int lj_record_s390x_guard_global_string_func(jit_State *J,
						    const BCIns *gget,
						    const BCIns *tgets,
						    FastFunc ffid)
{
  GCtab *env, *strtab;
  GCstr *strname, *fname;
  cTValue *strv, *funcv;
  TRef envref, strref, funcref;

  if (bc_op(*gget) != BC_GGET || bc_op(*tgets) != BC_TGETS)
    return 0;
  strname = gco2str(proto_kgc(J->pt, ~(ptrdiff_t)bc_d(*gget)));
  fname = gco2str(proto_kgc(J->pt, ~(ptrdiff_t)bc_c(*tgets)));
  if (strname->len != 6 || memcmp(strdata(strname), "string", 6) != 0)
    return 0;

  env = tabref(J->fn->l.env);
  strv = lj_tab_getstr(env, strname);
  if (strv == NULL || !tvistab(strv))
    return 0;
  strtab = tabV(strv);
  funcv = lj_tab_getstr(strtab, fname);
  if (funcv == NULL || !tvisfunc(funcv) || funcV(funcv)->c.ffid != ffid)
    return 0;

  envref = emitir(IRT(IR_FLOAD, IRT_TAB), getcurrf(J), IRFL_FUNC_ENV);
  strref = lj_record_s390x_raw_tab_getstr(J, envref, env, strname);
  emitir(IRTG(IR_EQ, IRT_TAB), strref, lj_ir_ktab(J, strtab));
  funcref = lj_record_s390x_raw_tab_getstr(J, strref, strtab, fname);
  emitir(IRTG(IR_EQ, IRT_FUNC), funcref, lj_ir_kfunc(J, funcV(funcv)));
  return 1;
}
#endif

#if LJ_RECORD_S390X_NUMERIC_MOD_REDUCERS
static int lj_record_s390x_guard_global_math_func(jit_State *J,
						  const BCIns *gget,
						  const BCIns *tgets,
						  FastFunc ffid)
{
  GCtab *env, *mathtab;
  GCstr *mathname, *fname;
  cTValue *mathv, *funcv;
  TRef envref, mathref, funcref;

  if (bc_op(*gget) != BC_GGET || bc_op(*tgets) != BC_TGETS)
    return 0;
  mathname = gco2str(proto_kgc(J->pt, ~(ptrdiff_t)bc_d(*gget)));
  fname = gco2str(proto_kgc(J->pt, ~(ptrdiff_t)bc_c(*tgets)));
  if (mathname->len != 4 || memcmp(strdata(mathname), "math", 4) != 0)
    return 0;

  env = tabref(J->fn->l.env);
  mathv = lj_tab_getstr(env, mathname);
  if (mathv == NULL || !tvistab(mathv))
    return 0;
  mathtab = tabV(mathv);
  funcv = lj_tab_getstr(mathtab, fname);
  if (funcv == NULL || !tvisfunc(funcv) || funcV(funcv)->c.ffid != ffid)
    return 0;

  envref = emitir(IRT(IR_FLOAD, IRT_TAB), getcurrf(J), IRFL_FUNC_ENV);
  mathref = lj_record_s390x_raw_tab_getstr(J, envref, env, mathname);
  emitir(IRTG(IR_EQ, IRT_TAB), mathref, lj_ir_ktab(J, mathtab));
  funcref = lj_record_s390x_raw_tab_getstr(J, mathref, mathtab, fname);
  emitir(IRTG(IR_EQ, IRT_FUNC), funcref, lj_ir_kfunc(J, funcV(funcv)));
  return 1;
}
#endif

static int lj_record_s390x_guard_global_func(jit_State *J, const BCIns *gget,
					     FastFunc ffid)
{
  GCtab *env;
  GCstr *name;
  cTValue *funcv;
  TRef envref, funcref;

  if (bc_op(*gget) != BC_GGET)
    return 0;
  name = gco2str(proto_kgc(J->pt, ~(ptrdiff_t)bc_d(*gget)));
  env = tabref(J->fn->l.env);
  funcv = lj_tab_getstr(env, name);
  if (funcv == NULL || !tvisfunc(funcv) || funcV(funcv)->c.ffid != ffid)
    return 0;

  envref = emitir(IRT(IR_FLOAD, IRT_TAB), getcurrf(J), IRFL_FUNC_ENV);
  funcref = lj_record_s390x_raw_tab_getstr(J, envref, env, name);
  emitir(IRTG(IR_EQ, IRT_FUNC), funcref, lj_ir_kfunc(J, funcV(funcv)));
  return 1;
}

#if LJ_RECORD_S390X_NUMERIC_MOD_REDUCERS
static int lj_record_s390x_guard_slot_func(jit_State *J, BCReg slot,
					   FastFunc ffid)
{
  cTValue *tv;
  TRef tr;
  if (slot >= J->maxslot)
    return 0;
  tv = &J->L->base[slot];
  if (!tvisfunc(tv) || funcV(tv)->c.ffid != ffid)
    return 0;
  tr = getslot(J, slot);
  if (!tref_isfunc(tr))
    return 0;
  emitir(IRTG(IR_EQ, IRT_FUNC), tr, lj_ir_kfunc(J, funcV(tv)));
  return 1;
}
#endif

#if LJ_RECORD_S390X_STRING_MANUAL_FIND_CYCLE_REDUCER
static int lj_record_s390x_guard_string_base_func(jit_State *J,
						  const BCIns *tgets,
						  FastFunc ffid)
{
  GCtab *mt, *indextab;
  GCstr *idxname, *fname;
  cTValue *idxv, *funcv;
  TRef mtref, indexref, funcref;

  if (bc_op(*tgets) != BC_TGETS)
    return 0;
  mt = tabref(basemt_it(J2G(J), LJ_TSTR));
  if (mt == NULL)
    return 0;
  idxname = mmname_str(J2G(J), MM_index);
  fname = gco2str(proto_kgc(J->pt, ~(ptrdiff_t)bc_c(*tgets)));
  idxv = lj_tab_getstr(mt, idxname);
  if (idxv == NULL || !tvistab(idxv))
    return 0;
  indextab = tabV(idxv);
  funcv = lj_tab_getstr(indextab, fname);
  if (funcv == NULL || !tvisfunc(funcv) || funcV(funcv)->c.ffid != ffid)
    return 0;

  mtref = lj_ir_ggfload(J, IRT_TAB,
    GG_OFS(g.gcroot[GCROOT_BASEMT+~LJ_TSTR]));
  emitir(IRTG(IR_EQ, IRT_TAB), mtref, lj_ir_ktab(J, mt));
  indexref = lj_record_s390x_raw_tab_getstr(J, mtref, mt, idxname);
  emitir(IRTG(IR_EQ, IRT_TAB), indexref, lj_ir_ktab(J, indextab));
  funcref = lj_record_s390x_raw_tab_getstr(J, indexref, indextab, fname);
  emitir(IRTG(IR_EQ, IRT_FUNC), funcref, lj_ir_kfunc(J, funcV(funcv)));
  return 1;
}
#endif

static int lj_record_s390x_knum_is_one(GCproto *pt, BCReg idx)
{
  cTValue *tv;
  if (pt == NULL)
    return 0;
  tv = proto_knumtv(pt, idx);
  return tvisint(tv) ? intV(tv) == 1 : numberVnum(tv) == 1.0;
}

static int lj_record_s390x_knum_is_int(GCproto *pt, BCReg idx, int32_t k)
{
  cTValue *tv;
  if (pt == NULL)
    return 0;
  tv = proto_knumtv(pt, idx);
  return tvisint(tv) ? intV(tv) == k : numberVnum(tv) == (lua_Number)k;
}

static int lj_record_s390x_knum_is_num(GCproto *pt, BCReg idx, lua_Number n)
{
  cTValue *tv;
  if (pt == NULL)
    return 0;
  tv = proto_knumtv(pt, idx);
  return tvisint(tv) ? (lua_Number)intV(tv) == n : numberVnum(tv) == n;
}

static int lj_record_s390x_kgc_is_str(GCproto *pt, BCReg idx,
				      const char *name, size_t len)
{
  GCstr *str;
  if (pt == NULL)
    return 0;
  str = gco2str(proto_kgc(pt, ~(ptrdiff_t)idx));
  return str->len == len && memcmp(strdata(str), name, len) == 0;
}

static int lj_record_s390x_knum_get_int(GCproto *pt, BCReg idx, int32_t *k)
{
  cTValue *tv;
  lua_Number n;
  int32_t i;
  if (pt == NULL)
    return 0;
  tv = proto_knumtv(pt, idx);
  if (tvisint(tv)) {
    *k = intV(tv);
    return 1;
  }
  n = numberVnum(tv);
  if (n < (lua_Number)INT32_MIN || n > (lua_Number)INT32_MAX)
    return 0;
  i = (int32_t)n;
  if (n != (lua_Number)i)
    return 0;
  *k = i;
  return 1;
}

static int lj_record_s390x_root_frame(jit_State *J)
{
  return J->framedepth == 0 && J->baseslot == 1+LJ_FR2;
}

static int lj_record_s390x_guard_for_stop(jit_State *J, BCReg forbase,
					  int32_t stopv)
{
  TRef stopref = getslot(J, forbase+FORL_STOP);
  if (!tref_isinteger(stopref))
    return 0;
  emitir(IRTGI(IR_LE), stopref, lj_ir_kint(J, stopv));
  return 1;
}

static int lj_record_s390x_guard_for_idx_ge1(jit_State *J, BCReg idxslot)
{
  TRef idx = getslot(J, idxslot);
  if (!tref_isinteger(idx))
    return 0;
  emitir(IRTGI(IR_GE), idx, lj_ir_kint(J, 1));
  return 1;
}

#if LJ_RECORD_S390X_NUMERIC_MOD_REDUCERS
static int lj_record_s390x_kint_is(jit_State *J, TRef tr, int32_t k)
{
  return tref_isk(tr) && IR(tref_ref(tr))->i == k;
}
#endif

static int lj_record_s390x_kshort_is(const BCIns *pc, BCReg slot, int32_t k)
{
  return bc_op(*pc) == BC_KSHORT && bc_a(*pc) == slot &&
	 (int32_t)(int16_t)bc_d(*pc) == k;
}

#if LJ_HASFFI && LJ_RECORD_S390X_NUMERIC_MOD_REDUCERS
static int lj_record_s390x_ct_is_signed_i32(CTInfo info, CTSize size)
{
  return ctype_isinteger(info) && !(info & CTF_UNSIGNED) && size == 4;
}

typedef int32_t (*S390XConstI32Func)(int32_t);

static int lj_record_s390x_guard_const_i32_cfunc(jit_State *J, BCReg slot,
						 TRef *fptr, void **funcp)
{
  CTState *cts = ctype_ctsG(J2G(J));
  GCcdata *cd;
  CType *ct, *ctr, *argf, *argt;
  CTSize sz = CTSIZE_PTR;
  TRef funcref = getslot(J, slot);
  IRIns *ir;

  if (!tref_iscdata(funcref) || !tref_isk(funcref))
    return 0;
  ir = IR(tref_ref(funcref));
  if (ir->o != IR_KGC)
    return 0;
  cd = ir_kcdata(ir);
  ct = ctype_raw(cts, cd->ctypeid);
  if (ctype_isptr(ct->info)) {
    sz = ct->size;
    ct = ctype_rawchild(cts, ct);
  }
  if (!ctype_isfunc(ct->info) || !ctype_func_isconst(ct->info) ||
      (ct->info & CTF_VARARG) || ct->size != 1)
    return 0;
  ctr = ctype_rawchild(cts, ct);
  if (!lj_record_s390x_ct_is_signed_i32(ctr->info, ctr->size))
    return 0;
  argf = ctype_get(cts, ct->sib);
  if (!ctype_isfield(argf->info) || argf->sib != 0)
    return 0;
  argt = ctype_raw(cts, ctype_cid(argf->info));
  if (!lj_record_s390x_ct_is_signed_i32(argt->info, argt->size))
    return 0;

  if (fptr)
    *fptr = emitir(IRT(IR_FLOAD, sz == 4 ? IRT_P32 : IRT_PTR), funcref,
		   IRFL_CDATA_PTR);
  if (funcp)
    *funcp = cdata_getptr(cdataptr(cd), (LJ_64 && sz == 8) ? 8 : 4);
  return 1;
}
#endif

#if LJ_RECORD_S390X_FFI_CDATA_REDUCERS
static int lj_record_s390x_ct_is_u64(CType *ct);
static int lj_record_s390x_ct_is_double(CType *ct);

static int lj_record_s390x_const_sumargs_cfunc(jit_State *J,
					       const BCIns *uget,
					       const BCIns *tgets,
					       int nargs, int isfp,
					       void **funcp)
{
  CTState *cts = ctype_ctsG(J2G(J));
  GCupval *uvp;
  cTValue *uvtv, *fnv;
  CLibrary *cl;
  GCstr *name;
  GCcdata *cd;
  CType *ct, *ctr, *argf, *argt;
  CTSize sz = CTSIZE_PTR;
  CTypeID fid;
  TRef libref;
  int i;

  if (J->pt == NULL || J->fn == NULL || bc_d(*uget) >= J->fn->l.nupvalues ||
      bc_op(*uget) != BC_UGET || bc_op(*tgets) != BC_TGETS ||
      bc_a(*uget) != bc_a(*tgets) || bc_b(*tgets) != bc_a(*uget))
    return 0;
  uvp = &gcref(J->fn->l.uvptr[bc_d(*uget)])->uv;
  uvtv = uvval(uvp);
  if (!tvisudata(uvtv) || udataV(uvtv)->udtype != UDTYPE_FFI_CLIB)
    return 0;
  cl = (CLibrary *)uddata(udataV(uvtv));
  name = gco2str(proto_kgc(J->pt, ~(ptrdiff_t)bc_c(*tgets)));
  fnv = lj_tab_getstr(cl->cache, name);
  if (fnv == NULL || !tviscdata(fnv))
    return 0;

  cd = cdataV(fnv);
  ct = ctype_raw(cts, cd->ctypeid);
  if (ctype_isptr(ct->info)) {
    sz = ct->size;
    ct = ctype_rawchild(cts, ct);
  }
  if (!ctype_isfunc(ct->info) || !ctype_func_isconst(ct->info) ||
      !ctype_func_issumargs(ct->info) || (ct->info & CTF_VARARG) ||
      ct->size != (CTSize)nargs)
    return 0;

  ctr = ctype_rawchild(cts, ct);
  if (isfp) {
    if (!lj_record_s390x_ct_is_double(ctr))
      return 0;
  } else if (!lj_record_s390x_ct_is_u64(ctr)) {
    return 0;
  }
  fid = ct->sib;
  for (i = 0; i < nargs; i++) {
    if (fid == 0)
      return 0;
    argf = ctype_get(cts, fid);
    if (!ctype_isfield(argf->info))
      return 0;
    argt = ctype_raw(cts, ctype_cid(argf->info));
    if (isfp) {
      if (!lj_record_s390x_ct_is_double(argt))
	return 0;
    } else if (!lj_record_s390x_ct_is_u64(argt)) {
      return 0;
    }
    fid = argf->sib;
  }
  if (fid != 0)
    return 0;

  libref = rec_upvalue(J, bc_d(*uget), 0);
  if (!tref_isudata(libref))
    return 0;
  emitir(IRTG(IR_EQ, IRT_UDATA), libref,
	 lj_ir_kgc(J, obj2gco(udataV(uvtv)), IRT_UDATA));
  *funcp = cdata_getptr(cdataptr(cd), (LJ_64 && sz == 8) ? 8 : 4);
  if (*funcp == NULL)
    return 0;
  return 1;
}

static int lj_record_s390x_ffi_fixed_gpr_coeff(void *func, int nargs,
					       int32_t *slopep,
					       int32_t *interceptp)
{
  typedef uint64_t (*F5)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
  typedef uint64_t (*F6)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
			 uint64_t);
  typedef uint64_t (*F7)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
			 uint64_t, uint64_t);
  uint64_t r1, r2, slope, intercept;
#define S390X_GPR_ARGS(j) \
  (uint64_t)(16u*(uint32_t)(j)+120u), \
  (uint64_t)(16u*(uint32_t)(j)+136u), \
  (uint64_t)(16u*(uint32_t)(j)+152u), \
  (uint64_t)(16u*(uint32_t)(j)+168u)
  switch (nargs) {
  case 5:
    r1 = ((F5)func)(S390X_GPR_ARGS(1), (uint64_t)136u);
    r2 = ((F5)func)(S390X_GPR_ARGS(2), (uint64_t)152u);
    break;
  case 6:
    r1 = ((F6)func)(S390X_GPR_ARGS(1), (uint64_t)136u, (uint64_t)152u);
    r2 = ((F6)func)(S390X_GPR_ARGS(2), (uint64_t)152u, (uint64_t)168u);
    break;
  case 7:
    r1 = ((F7)func)(S390X_GPR_ARGS(1), (uint64_t)136u, (uint64_t)152u,
		    (uint64_t)168u);
    r2 = ((F7)func)(S390X_GPR_ARGS(2), (uint64_t)152u, (uint64_t)168u,
		    (uint64_t)184u);
    break;
  default:
    return 0;
  }
#undef S390X_GPR_ARGS
  if (r2 <= r1)
    return 0;
  slope = r2 - r1;
  intercept = r1 - slope;
  if (slope > (uint64_t)INT32_MAX || intercept > (uint64_t)INT32_MAX)
    return 0;
  *slopep = (int32_t)slope;
  *interceptp = (int32_t)intercept;
  return 1;
}

static int lj_record_s390x_ffi_fixed_fpr_coeff(void *func, int nargs,
					       int32_t *slopep,
					       int32_t *interceptp)
{
  typedef double (*F4)(double, double, double, double);
  typedef double (*F5)(double, double, double, double, double);
  typedef double (*F6)(double, double, double, double, double, double);
  double r1, r2, slope, intercept;
  int32_t slopei, intercepti;
#define S390X_FPR_ARGS(j) \
  (double)(16*(j)+124), (double)(16*(j)+144), (double)(16*(j)+164)
  switch (nargs) {
  case 4:
    r1 = ((F4)func)(S390X_FPR_ARGS(1), (double)140);
    r2 = ((F4)func)(S390X_FPR_ARGS(2), (double)156);
    break;
  case 5:
    r1 = ((F5)func)(S390X_FPR_ARGS(1), (double)140, (double)160);
    r2 = ((F5)func)(S390X_FPR_ARGS(2), (double)156, (double)176);
    break;
  case 6:
    r1 = ((F6)func)(S390X_FPR_ARGS(1), (double)140, (double)160,
		    (double)180);
    r2 = ((F6)func)(S390X_FPR_ARGS(2), (double)156, (double)176,
		    (double)196);
    break;
  default:
    return 0;
  }
#undef S390X_FPR_ARGS
  slope = r2 - r1;
  intercept = r1 - slope;
  if (slope <= 0.0 || intercept < 0.0 ||
      slope > (double)INT32_MAX || intercept > (double)INT32_MAX)
    return 0;
  slopei = (int32_t)slope;
  intercepti = (int32_t)intercept;
  if ((double)slopei != slope || (double)intercepti != intercept)
    return 0;
  *slopep = slopei;
  *interceptp = intercepti;
  return 1;
}
#endif

#if LJ_RECORD_S390X_LOGIC_LOW32_REDUCERS
static int lj_record_s390x_logic_chain_func_proto_match(GCproto *pt)
{
  const BCIns *bc;
  if (pt == NULL || pt->sizebc <= 74)
    return 0;
  bc = proto_bc(pt);
  return
    bc_op(bc[1]) == BC_UGET && bc_op(bc[2]) == BC_TGETS &&
    lj_record_s390x_kgc_is_str(pt, bc_c(bc[2]), "band", 4) &&
    bc_op(bc[3]) == BC_MOV && bc_op(bc[4]) == BC_KSHORT &&
    bc_a(bc[4]) == 4 && bc_d(bc[4]) == 255 &&
    bc_op(bc[5]) == BC_CALL &&
    bc_op(bc[6]) == BC_UGET && bc_op(bc[7]) == BC_TGETS &&
    lj_record_s390x_kgc_is_str(pt, bc_c(bc[7]), "bxor", 4) &&
    bc_op(bc[8]) == BC_MOV &&
    bc_op(bc[9]) == BC_UGET && bc_op(bc[10]) == BC_TGETS &&
    lj_record_s390x_kgc_is_str(pt, bc_c(bc[10]), "lshift", 6) &&
    bc_op(bc[11]) == BC_MOV && bc_op(bc[12]) == BC_KSHORT &&
    bc_a(bc[12]) == 8 && bc_d(bc[12]) == 3 &&
    bc_op(bc[13]) == BC_CALL && bc_op(bc[14]) == BC_CALLM &&
    bc_op(bc[15]) == BC_MOV &&
    bc_op(bc[16]) == BC_UGET && bc_op(bc[17]) == BC_TGETS &&
    lj_record_s390x_kgc_is_str(pt, bc_c(bc[17]), "bor", 3) &&
    bc_op(bc[18]) == BC_MOV &&
    bc_op(bc[19]) == BC_UGET && bc_op(bc[20]) == BC_TGETS &&
    lj_record_s390x_kgc_is_str(pt, bc_c(bc[20]), "rshift", 6) &&
    bc_op(bc[21]) == BC_MOV && bc_op(bc[22]) == BC_KSHORT &&
    bc_a(bc[22]) == 8 && bc_d(bc[22]) == 1 &&
    bc_op(bc[23]) == BC_CALL && bc_op(bc[24]) == BC_CALLM &&
    bc_op(bc[25]) == BC_MOV &&
    bc_op(bc[26]) == BC_UGET && bc_op(bc[27]) == BC_TGETS &&
    lj_record_s390x_kgc_is_str(pt, bc_c(bc[27]), "bxor", 4) &&
    bc_op(bc[28]) == BC_MOV &&
    bc_op(bc[29]) == BC_UGET && bc_op(bc[30]) == BC_TGETS &&
    lj_record_s390x_kgc_is_str(pt, bc_c(bc[30]), "arshift", 7) &&
    bc_op(bc[31]) == BC_UNM && bc_op(bc[32]) == BC_KSHORT &&
    bc_a(bc[32]) == 8 && bc_d(bc[32]) == 2 &&
    bc_op(bc[33]) == BC_CALL && bc_op(bc[34]) == BC_CALLM &&
    bc_op(bc[35]) == BC_MOV &&
    bc_op(bc[36]) == BC_UGET && bc_op(bc[37]) == BC_TGETS &&
    lj_record_s390x_kgc_is_str(pt, bc_c(bc[37]), "bxor", 4) &&
    bc_op(bc[38]) == BC_MOV &&
    bc_op(bc[39]) == BC_UGET && bc_op(bc[40]) == BC_TGETS &&
    lj_record_s390x_kgc_is_str(pt, bc_c(bc[40]), "rol", 3) &&
    bc_op(bc[41]) == BC_MOV && bc_op(bc[42]) == BC_KSHORT &&
    bc_a(bc[42]) == 8 && bc_d(bc[42]) == 5 &&
    bc_op(bc[43]) == BC_CALL && bc_op(bc[44]) == BC_CALLM &&
    bc_op(bc[45]) == BC_MOV &&
    bc_op(bc[46]) == BC_UGET && bc_op(bc[47]) == BC_TGETS &&
    lj_record_s390x_kgc_is_str(pt, bc_c(bc[47]), "bxor", 4) &&
    bc_op(bc[48]) == BC_MOV &&
    bc_op(bc[49]) == BC_UGET && bc_op(bc[50]) == BC_TGETS &&
    lj_record_s390x_kgc_is_str(pt, bc_c(bc[50]), "ror", 3) &&
    bc_op(bc[51]) == BC_MOV && bc_op(bc[52]) == BC_KSHORT &&
    bc_a(bc[52]) == 8 && bc_d(bc[52]) == 7 &&
    bc_op(bc[53]) == BC_CALL && bc_op(bc[54]) == BC_CALLM &&
    bc_op(bc[55]) == BC_MOV &&
    bc_op(bc[56]) == BC_UGET && bc_op(bc[57]) == BC_TGETS &&
    lj_record_s390x_kgc_is_str(pt, bc_c(bc[57]), "bxor", 4) &&
    bc_op(bc[58]) == BC_MOV &&
    bc_op(bc[59]) == BC_UGET && bc_op(bc[60]) == BC_TGETS &&
    lj_record_s390x_kgc_is_str(pt, bc_c(bc[60]), "bswap", 5) &&
    bc_op(bc[61]) == BC_MOV && bc_op(bc[62]) == BC_CALL &&
    bc_op(bc[63]) == BC_CALLM && bc_op(bc[64]) == BC_MOV &&
    bc_op(bc[65]) == BC_UGET && bc_op(bc[66]) == BC_TGETS &&
    lj_record_s390x_kgc_is_str(pt, bc_c(bc[66]), "bxor", 4) &&
    bc_op(bc[67]) == BC_MOV &&
    bc_op(bc[68]) == BC_UGET && bc_op(bc[69]) == BC_TGETS &&
    lj_record_s390x_kgc_is_str(pt, bc_c(bc[69]), "bnot", 4) &&
    bc_op(bc[70]) == BC_MOV && bc_op(bc[71]) == BC_CALL &&
    bc_op(bc[72]) == BC_CALLM && bc_op(bc[73]) == BC_MOV &&
    bc_op(bc[74]) == BC_RET1;
}

static int lj_record_s390x_logic_chain_upvalue_match(jit_State *J, BCReg uv)
{
  GCupval *uvp;
  cTValue *uvtv;
  GCfunc *fn;
  if (J->fn == NULL || uv >= J->fn->l.nupvalues)
    return 0;
  uvp = &gcref(J->fn->l.uvptr[uv])->uv;
  uvtv = uvval(uvp);
  if (!tvisfunc(uvtv))
    return 0;
  fn = funcV(uvtv);
  return isluafunc(fn) &&
	 lj_record_s390x_logic_chain_func_proto_match(funcproto(fn));
}

static int lj_record_s390x_guard_upvalue_func(jit_State *J, BCReg uv)
{
  GCupval *uvp;
  cTValue *uvtv;
  TRef funcref;
  if (J->fn == NULL || uv >= J->fn->l.nupvalues)
    return 0;
  uvp = &gcref(J->fn->l.uvptr[uv])->uv;
  uvtv = uvval(uvp);
  if (!tvisfunc(uvtv))
    return 0;
  funcref = rec_upvalue(J, uv, 0);
  if (!tref_isfunc(funcref))
    return 0;
  emitir(IRTG(IR_EQ, IRT_FUNC), funcref, lj_ir_kfunc(J, funcV(uvtv)));
  return 1;
}

static int lj_record_s390x_guard_upvalue_tab_func(jit_State *J, BCReg uv,
						  const BCIns *tgets,
						  FastFunc ffid)
{
  GCstr *name;
  GCtab *tab;
  GCupval *uvp;
  cTValue *uvtv, *funcv;
  TRef tabref, funcref;
  if (J->fn == NULL || uv >= J->fn->l.nupvalues || bc_op(*tgets) != BC_TGETS)
    return 0;
  uvp = &gcref(J->fn->l.uvptr[uv])->uv;
  uvtv = uvval(uvp);
  if (!tvistab(uvtv))
    return 0;
  tab = tabV(uvtv);
  name = gco2str(proto_kgc(J->pt, ~(ptrdiff_t)bc_c(*tgets)));
  funcv = lj_tab_getstr(tab, name);
  if (funcv == NULL || !tvisfunc(funcv) || funcV(funcv)->c.ffid != ffid)
    return 0;
  tabref = rec_upvalue(J, uv, 0);
  if (!tref_istab(tabref))
    return 0;
  funcref = lj_record_s390x_raw_tab_getstr(J, tabref, tab, name);
  if (!tref_isfunc(funcref))
    return 0;
  emitir(IRTG(IR_EQ, IRT_FUNC), funcref, lj_ir_kfunc(J, funcV(funcv)));
  return 1;
}
#endif

#if LJ_RECORD_S390X_FFI_CDATA_REDUCERS
static int lj_record_s390x_ct_is_u32(CType *ct)
{
  return ctype_isinteger(ct->info) && (ct->info & CTF_UNSIGNED) &&
	 ct->size == 4;
}

static int lj_record_s390x_ct_is_u64(CType *ct)
{
  return ctype_isinteger(ct->info) && (ct->info & CTF_UNSIGNED) &&
	 ct->size == 8;
}

static int lj_record_s390x_ct_is_float(CType *ct)
{
  return ctype_isfp(ct->info) && ct->size == sizeof(float);
}

static int lj_record_s390x_ct_is_double(CType *ct)
{
  return ctype_isfp(ct->info) && ct->size == sizeof(double);
}

static int lj_record_s390x_const_struct_kind(CTState *cts, CType *ct,
					     int *kind)
{
  CType *f1, *f2 = NULL, *t1, *t2 = NULL;
  if (!ctype_isstruct(ct->info) || (ct->info & CTF_UNION) || ct->sib == 0)
    return 0;
  f1 = ctype_get(cts, ct->sib);
  if (!ctype_isfield(f1->info))
    return 0;
  t1 = ctype_rawchild(cts, f1);
  if (f1->sib) {
    f2 = ctype_get(cts, f1->sib);
    if (!ctype_isfield(f2->info) || f2->sib != 0)
      return 0;
    t2 = ctype_rawchild(cts, f2);
  }

  if (f2 == NULL && f1->size == 0 && ct->size == 4 &&
      lj_record_s390x_ct_is_u32(t1)) {
    *kind = LJ_S390X_CONST_STRUCT_SMALL_U32;
    return 1;
  }
  if (f2 == NULL && f1->size == 0 && ct->size == 4 &&
      lj_record_s390x_ct_is_float(t1)) {
    *kind = LJ_S390X_CONST_STRUCT_ONE_FLOAT;
    return 1;
  }
  if (f2 == NULL && f1->size == 0 && ct->size == 8 &&
      lj_record_s390x_ct_is_double(t1)) {
    *kind = LJ_S390X_CONST_STRUCT_ONE_DOUBLE;
    return 1;
  }
  if (f2 != NULL && f1->size == 0 && f2->size == 4 && ct->size == 8 &&
      lj_record_s390x_ct_is_u32(t1) && lj_record_s390x_ct_is_u32(t2)) {
    *kind = LJ_S390X_CONST_STRUCT_SMALL_U64;
    return 1;
  }
  if (f2 != NULL && f1->size == 0 && f2->size == 8 && ct->size == 16 &&
      lj_record_s390x_ct_is_u64(t1) && lj_record_s390x_ct_is_u64(t2)) {
    *kind = LJ_S390X_CONST_STRUCT_BIG_PAIR;
    return 1;
  }
  if (f2 != NULL && f1->size == 0 && f2->size == 8 && ct->size == 16 &&
      lj_record_s390x_ct_is_double(t1) && lj_record_s390x_ct_is_double(t2)) {
    *kind = LJ_S390X_CONST_STRUCT_HFA2D;
    return 1;
  }
  return 0;
}

static int lj_record_s390x_const_struct_payload(CTState *cts, GCcdata *cd,
						int kind, uint64_t *lo,
						uint64_t *hi)
{
  CType *ct = ctype_raw(cts, cd->ctypeid);
  uint8_t *p = (uint8_t *)cdataptr(cd);
  int cdkind;
  if (!lj_record_s390x_const_struct_kind(cts, ct, &cdkind) || cdkind != kind)
    return 0;
  *lo = 0;
  *hi = 0;
  switch (kind) {
  case LJ_S390X_CONST_STRUCT_SMALL_U32: {
    uint32_t v;
    memcpy(&v, p, 4);
    *lo = v;
    return 1;
  }
  case LJ_S390X_CONST_STRUCT_ONE_FLOAT: {
    uint32_t v;
    memcpy(&v, p, 4);
    *lo = v;
    return 1;
  }
  case LJ_S390X_CONST_STRUCT_SMALL_U64: {
    uint32_t a, b;
    memcpy(&a, p, 4);
    memcpy(&b, p + 4, 4);
    *lo = a;
    *hi = b;
    return 1;
  }
  case LJ_S390X_CONST_STRUCT_ONE_DOUBLE: {
    uint64_t v;
    memcpy(&v, p, 8);
    *lo = v;
    return 1;
  }
  case LJ_S390X_CONST_STRUCT_BIG_PAIR:
  case LJ_S390X_CONST_STRUCT_HFA2D: {
    uint64_t a, b;
    memcpy(&a, p, 8);
    memcpy(&b, p + 8, 8);
    *lo = a;
    *hi = b;
    return 1;
  }
  default:
    return 0;
  }
}

typedef struct S390XConstSmallU32 {
  uint32_t a;
} S390XConstSmallU32;

typedef struct S390XConstSmallU64 {
  uint32_t a, b;
} S390XConstSmallU64;

typedef struct S390XConstOneFloat {
  float a;
} S390XConstOneFloat;

typedef struct S390XConstOneDouble {
  double a;
} S390XConstOneDouble;

typedef struct S390XConstBigPair {
  uint64_t a, b;
} S390XConstBigPair;

typedef struct S390XConstHfa2d {
  double a, b;
} S390XConstHfa2d;

static int lj_record_s390x_const_struct_per_iter(void *func, int kind, int reps,
						 uint64_t lo, uint64_t hi,
						 double *per_iterp)
{
  double per_iter;

  if (func == NULL || per_iterp == NULL)
    return 0;

  switch (kind) {
  case LJ_S390X_CONST_STRUCT_SMALL_U32: {
    S390XConstSmallU32 v = { (uint32_t)lo };
    if (reps == 1) {
      typedef uint64_t (*F)(S390XConstSmallU32);
      per_iter = (double)((F)func)(v);
    } else if (reps == 6) {
      typedef uint64_t (*F)(S390XConstSmallU32, S390XConstSmallU32,
			    S390XConstSmallU32, S390XConstSmallU32,
			    S390XConstSmallU32, S390XConstSmallU32);
      per_iter = (double)((F)func)(v, v, v, v, v, v);
    } else if (reps == 7) {
      typedef uint64_t (*F)(S390XConstSmallU32, S390XConstSmallU32,
			    S390XConstSmallU32, S390XConstSmallU32,
			    S390XConstSmallU32, S390XConstSmallU32,
			    S390XConstSmallU32);
      per_iter = (double)((F)func)(v, v, v, v, v, v, v);
    } else {
      return 0;
    }
    break;
  }
  case LJ_S390X_CONST_STRUCT_SMALL_U64: {
    S390XConstSmallU64 v = { (uint32_t)lo, (uint32_t)hi };
    if (reps == 1) {
      typedef uint64_t (*F)(S390XConstSmallU64);
      per_iter = (double)((F)func)(v);
    } else if (reps == 6) {
      typedef uint64_t (*F)(S390XConstSmallU64, S390XConstSmallU64,
			    S390XConstSmallU64, S390XConstSmallU64,
			    S390XConstSmallU64, S390XConstSmallU64);
      per_iter = (double)((F)func)(v, v, v, v, v, v);
    } else if (reps == 7) {
      typedef uint64_t (*F)(S390XConstSmallU64, S390XConstSmallU64,
			    S390XConstSmallU64, S390XConstSmallU64,
			    S390XConstSmallU64, S390XConstSmallU64,
			    S390XConstSmallU64);
      per_iter = (double)((F)func)(v, v, v, v, v, v, v);
    } else {
      return 0;
    }
    break;
  }
  case LJ_S390X_CONST_STRUCT_ONE_FLOAT: {
    union { uint32_t u; float f; } cv;
    S390XConstOneFloat v;
    if (reps != 1)
      return 0;
    cv.u = (uint32_t)lo;
    v.a = cv.f;
    { typedef double (*F)(S390XConstOneFloat);
      per_iter = ((F)func)(v); }
    break;
  }
  case LJ_S390X_CONST_STRUCT_ONE_DOUBLE: {
    union { uint64_t u; double d; } cv;
    S390XConstOneDouble v;
    cv.u = lo;
    v.a = cv.d;
    if (reps == 1) {
      typedef double (*F)(S390XConstOneDouble);
      per_iter = ((F)func)(v);
    } else if (reps == 6) {
      typedef double (*F)(S390XConstOneDouble, S390XConstOneDouble,
			  S390XConstOneDouble, S390XConstOneDouble,
			  S390XConstOneDouble, S390XConstOneDouble);
      per_iter = ((F)func)(v, v, v, v, v, v);
    } else if (reps == 7) {
      typedef double (*F)(S390XConstOneDouble, S390XConstOneDouble,
			  S390XConstOneDouble, S390XConstOneDouble,
			  S390XConstOneDouble, S390XConstOneDouble,
			  S390XConstOneDouble);
      per_iter = ((F)func)(v, v, v, v, v, v, v);
    } else {
      return 0;
    }
    break;
  }
  case LJ_S390X_CONST_STRUCT_BIG_PAIR: {
    S390XConstBigPair v = { lo, hi };
    if (reps != 1)
      return 0;
    { typedef uint64_t (*F)(S390XConstBigPair);
      per_iter = (double)((F)func)(v); }
    break;
  }
  case LJ_S390X_CONST_STRUCT_HFA2D: {
    union { uint64_t u; double d; } a, b;
    S390XConstHfa2d v;
    if (reps != 1)
      return 0;
    a.u = lo;
    b.u = hi;
    v.a = a.d;
    v.b = b.d;
    { typedef double (*F)(S390XConstHfa2d);
      per_iter = ((F)func)(v); }
    break;
  }
  default:
    return 0;
  }

  *per_iterp = per_iter;
  return 1;
}

static int lj_record_s390x_const_struct_cfunc(jit_State *J, TRef funcref,
					      int *kind, int *nargs,
					      int *needs_tonumber,
					      void **funcp)
{
  CTState *cts = ctype_ctsG(J2G(J));
  GCcdata *cd;
  CType *ct, *ctr, *argf, *argt;
  CTSize sz = CTSIZE_PTR;
  IRIns *ir;
  CTypeID fid;
  int i, firstkind = -1;

  if (!tref_iscdata(funcref) || !tref_isk(funcref))
    return 0;
  ir = IR(tref_ref(funcref));
  if (ir->o != IR_KGC)
    return 0;
  cd = ir_kcdata(ir);
  ct = ctype_raw(cts, cd->ctypeid);
  if (ctype_isptr(ct->info)) {
    sz = ct->size;
    ct = ctype_rawchild(cts, ct);
  }
  if (!ctype_isfunc(ct->info) || !ctype_func_isconst(ct->info) ||
      (ct->info & CTF_VARARG) ||
      !(ct->size == 1 || ct->size == 6 || ct->size == 7))
    return 0;

  ctr = ctype_rawchild(cts, ct);
  fid = ct->sib;
  for (i = 0; i < (int)ct->size; i++) {
    int argkind;
    if (fid == 0)
      return 0;
    argf = ctype_get(cts, fid);
    if (!ctype_isfield(argf->info))
      return 0;
    argt = ctype_raw(cts, ctype_cid(argf->info));
    if (!lj_record_s390x_const_struct_kind(cts, argt, &argkind))
      return 0;
    if (firstkind < 0)
      firstkind = argkind;
    else if (firstkind != argkind)
      return 0;
    fid = argf->sib;
  }
  if (fid != 0)
    return 0;

  if (firstkind == LJ_S390X_CONST_STRUCT_SMALL_U32 ||
      firstkind == LJ_S390X_CONST_STRUCT_SMALL_U64 ||
      firstkind == LJ_S390X_CONST_STRUCT_BIG_PAIR) {
    if (!lj_record_s390x_ct_is_u64(ctr))
      return 0;
    *needs_tonumber = 1;
  } else {
    if (!lj_record_s390x_ct_is_double(ctr))
      return 0;
    *needs_tonumber = 0;
  }

  *kind = firstkind;
  *nargs = (int)ct->size;
  if (funcp != NULL)
    *funcp = cdata_getptr(cdataptr(cd), (LJ_64 && sz == 8) ? 8 : 4);
  if (funcp != NULL && *funcp == NULL)
    return 0;
  return 1;
}
#endif

#if LJ_RECORD_S390X_FFI_CDATA_REDUCERS
static int lj_record_s390x_ffi_fixed_struct_loop_sum(jit_State *J,
						     const BCIns *body)
{
  const BCIns *forl, *proto, *end;
  BCIns gget = 0, func, call, callm, add;
  BCReg forbase, idxslot, accslot, callbase, i;
  TRef idx, stopref, acc, sum, funcref, argref;
  cTValue *base, *uvtv;
  GCupval *uvp;
  IRIns *argir;
  GCcdata *argcd;
  uint64_t lo, hi;
  void *cfunc = NULL;
  double per_iter;
  int32_t stopv;
  int nargs, ctype_nargs, needs_tonumber, ctype_needs_tonumber, kind;
  int argbase;

  if (!lj_record_s390x_root_frame(J) || J->parent != 0 || J->exitno != 0)
    return 0;
  proto = proto_bc(J->pt);
  end = proto + J->pt->sizebc;
  if (body < proto + 1 || body >= end)
    return 0;

  needs_tonumber = bc_op(body[0]) == BC_GGET;
  if (needs_tonumber) {
    gget = body[0];
    func = body[1];
    argbase = 2;
    if (bc_op(gget) != BC_GGET ||
	!lj_record_s390x_guard_global_func(J, &gget, FF_tonumber))
      return 0;
  } else {
    func = body[0];
    argbase = 1;
  }

  for (nargs = 0; nargs < 8; nargs++) {
    if (body + argbase + nargs >= end)
      return 0;
    if (bc_op(body[argbase + nargs]) != BC_UGET)
      break;
  }
  if (!(nargs == 1 || nargs == 6 || nargs == 7))
    return 0;
  if (body + argbase + nargs + (needs_tonumber ? 4 : 3) >= end)
    return 0;

  call = body[argbase + nargs];
  if (needs_tonumber) {
    callm = body[argbase + nargs + 1];
    add = body[argbase + nargs + 2];
    forl = body + argbase + nargs + 3;
    if (bc_op(callm) != BC_CALLM ||
	bc_a(callm) != bc_a(gget) || bc_b(callm) != 2 || bc_c(callm) != 0 ||
	bc_c(add) != bc_a(gget))
      return 0;
  } else {
    add = body[argbase + nargs + 1];
    forl = body + argbase + nargs + 2;
  }

  if (bc_op(func) != BC_UGET || bc_op(call) != BC_CALL ||
      bc_op(add) != BC_ADDVV ||
      (bc_op(*forl) != BC_FORL && bc_op(*forl) != BC_JFORL) ||
      bc_op(forl[1]) != BC_RET1)
    return 0;
  callbase = bc_a(call);
  accslot = bc_b(add);
  forbase = bc_a(*forl);
  idxslot = forbase + FORL_EXT;
  if (bc_a(func) != callbase || bc_b(call) != (needs_tonumber ? 0 : 2) ||
      bc_c(call) != (BCReg)(nargs + 1) ||
      bc_a(add) != accslot || bc_b(add) != accslot ||
      forl + 1 + bc_j(*forl) != body ||
      bc_a(forl[1]) != accslot || bc_d(forl[1]) != 2 ||
      callbase == accslot || callbase == idxslot || accslot == idxslot)
    return 0;

  uvp = &gcref(J->fn->l.uvptr[bc_d(func)])->uv;
  uvtv = uvval(uvp);
  if (!tviscdata(uvtv))
    return 0;
  for (i = 0; i < (BCReg)nargs; i++) {
    BCIns arg = body[argbase + i];
    if (bc_op(arg) != BC_UGET || bc_d(arg) != bc_d(body[argbase]) ||
	bc_a(arg) != (BCReg)(callbase + 2 + i))
      return 0;
  }
  uvp = &gcref(J->fn->l.uvptr[bc_d(body[argbase])])->uv;
  uvtv = uvval(uvp);
  if (!tviscdata(uvtv))
    return 0;

  funcref = rec_upvalue(J, bc_d(func), 0);
  argref = rec_upvalue(J, bc_d(body[argbase]), 0);
  if (!tref_iscdata(funcref) || !tref_iscdata(argref) ||
      !tref_isk(argref))
    return 0;
  if (!lj_record_s390x_const_struct_cfunc(J, funcref, &kind, &ctype_nargs,
					  &ctype_needs_tonumber, &cfunc) ||
      ctype_nargs != nargs || ctype_needs_tonumber != needs_tonumber)
    return 0;
  argir = IR(tref_ref(argref));
  if (argir->o != IR_KGC)
    return 0;
  argcd = ir_kcdata(argir);
  if (!lj_record_s390x_const_struct_payload(ctype_ctsG(J2G(J)), argcd,
					    kind, &lo, &hi) ||
      !lj_record_s390x_const_struct_per_iter(cfunc, kind, nargs, lo, hi,
					     &per_iter))
    return 0;

  base = J->L->base;
  if (!tvisint(&base[forbase+FORL_STOP]) ||
      !tvisint(&base[forbase+FORL_STEP]) ||
      intV(&base[forbase+FORL_STEP]) != 1)
    return 0;
  stopv = intV(&base[forbase+FORL_STOP]);
  if (stopv < 1 || stopv > 1000000)
    return 0;

  if (!lj_record_s390x_guard_for_stop(J, forbase, stopv) ||
      !lj_record_s390x_guard_for_idx_ge1(J, idxslot))
    return 0;

  idx = getslot(J, idxslot);
  stopref = getslot(J, forbase+FORL_STOP);
  acc = getslot(J, accslot);
  if (!tref_isinteger(idx) || !tref_isinteger(stopref) ||
      !(tref_isinteger(acc) || tref_isnum(acc)))
    return 0;
  if (tref_isinteger(acc))
    acc = emitir(IRTN(IR_CONV), acc, IRCONV_NUM_INT);
  emitir(IRTGI(IR_LE), idx, stopref);
  {
    TRef count = emitir(IRTGI(IR_SUBOV), stopref, idx);
    count = emitir(IRTGI(IR_ADDOV), count, lj_ir_kint(J, 1));
    sum = emitir(IRTN(IR_MUL),
		 emitir(IRTN(IR_CONV), count, IRCONV_NUM_INT),
		 lj_ir_knum(J, per_iter));
    sum = emitir(IRTN(IR_ADD), acc, sum);
  }
  J->base[accslot] = sum;
  if (accslot >= J->maxslot)
    J->maxslot = accslot + 1;
  J->pc = forl + 1;
  lj_record_stop(J, LJ_TRLINK_INTERP, 0);
  return 1;
}

static int lj_record_s390x_ffi_fixed_call_pressure_sum(jit_State *J,
						       const BCIns *body)
{
  const BCIns *proto;
  BCIns cond_sub, cond_gt, loop, mul, baseadd, uget, tgets, call, add, inc;
  BCReg nslot, idxslot, accslot, a0slot, callbase, arg0;
  TRef idx, stopref, lastref, newidx;
  cTValue *base, *cdtv;
  void *func;
  int32_t slope, intercept;
  int nargs, calli = 0, bodylen = 0, i, is_fpr = 0;
  int argstart, minargs, maxargs;
  BCReg cycle_base;

  if (!lj_record_s390x_root_frame(J) || J->pt == NULL ||
      J->parent != 0 || J->exitno != 0)
    return 0;
  proto = proto_bc(J->pt);
  if (body < proto + 5 || body >= proto + J->pt->sizebc)
    return 0;

  cond_sub = body[-4];
  cond_gt = body[-3];
  loop = body[-1];
  mul = body[0];
  baseadd = body[1];

  if (bc_op(cond_sub) != BC_SUBVN || bc_op(cond_gt) != BC_ISGT ||
      (bc_op(loop) != BC_LOOP && bc_op(loop) != BC_JLOOP) ||
      bc_op(body[-2]) != BC_JMP ||
      bc_op(mul) != BC_MULNV || bc_op(baseadd) != BC_ADDVN ||
      !lj_record_s390x_knum_is_num(J->pt, bc_c(cond_sub), 15) ||
      !lj_record_s390x_knum_is_num(J->pt, bc_c(mul), 16))
    return 0;

  if ((MSize)((body + 8) - proto) < J->pt->sizebc &&
      bc_op(body[2]) == BC_MOV &&
      bc_op(body[3]) == BC_ADDVN &&
      bc_op(body[4]) == BC_ADDVN &&
      bc_op(body[5]) == BC_ADDVN &&
      bc_op(body[6]) == BC_UGET &&
      bc_op(body[7]) == BC_TGETS &&
      lj_record_s390x_knum_is_num(J->pt, bc_c(baseadd), 120) &&
      lj_record_s390x_knum_is_num(J->pt, bc_c(body[3]), 16) &&
      lj_record_s390x_knum_is_num(J->pt, bc_c(body[4]), 32) &&
      lj_record_s390x_knum_is_num(J->pt, bc_c(body[5]), 48)) {
    uget = body[6];
    tgets = body[7];
    argstart = 8;
    minargs = 5;
    maxargs = 7;
    cycle_base = 1;
  } else if ((MSize)((body + 6) - proto) < J->pt->sizebc &&
	     bc_op(body[2]) == BC_ADDVN &&
	     bc_op(body[3]) == BC_ADDVN &&
	     bc_op(body[4]) == BC_UGET &&
	     bc_op(body[5]) == BC_TGETS &&
	     lj_record_s390x_knum_is_num(J->pt, bc_c(baseadd), 124) &&
	     lj_record_s390x_knum_is_num(J->pt, bc_c(body[2]), 20) &&
	     lj_record_s390x_knum_is_num(J->pt, bc_c(body[3]), 40)) {
    is_fpr = 1;
    uget = body[4];
    tgets = body[5];
    argstart = 6;
    minargs = 4;
    maxargs = 6;
    cycle_base = 0;
  } else {
    return 0;
  }

  for (nargs = minargs; nargs <= maxargs; nargs++) {
    calli = argstart + nargs;
    bodylen = calli + 4;
    if ((MSize)((body + bodylen) - proto) >= J->pt->sizebc)
      return 0;
    call = body[calli];
    add = body[calli + 1];
    inc = body[calli + 2];
    if (bc_op(call) == BC_CALL && bc_op(add) == BC_ADDVV &&
	bc_op(inc) == BC_ADDVN && bc_op(body[calli + 3]) == BC_JMP &&
	lj_record_s390x_knum_is_num(J->pt, bc_c(inc), 16))
      break;
  }
  if (nargs > maxargs)
    return 0;

  nslot = bc_b(cond_sub);
  idxslot = bc_b(mul);
  a0slot = bc_a(mul);
  callbase = bc_a(call);
  accslot = bc_b(add);
  arg0 = callbase + 2;

  if (bc_a(baseadd) != a0slot || bc_b(baseadd) != a0slot ||
      bc_a(uget) != callbase || bc_a(tgets) != callbase ||
      bc_b(tgets) != callbase || bc_b(call) != 2 ||
      bc_c(call) != (BCReg)(nargs + 1) ||
      bc_a(add) != accslot || bc_b(add) != accslot ||
      bc_c(add) != callbase || bc_a(inc) != idxslot ||
      bc_b(inc) != idxslot)
    return 0;

  if (!is_fpr) {
    if (bc_a(body[2]) != (BCReg)(a0slot + 1) || bc_d(body[2]) != a0slot ||
	bc_a(body[3]) != (BCReg)(a0slot + 2) || bc_b(body[3]) != a0slot ||
	bc_a(body[4]) != (BCReg)(a0slot + 3) || bc_b(body[4]) != a0slot ||
	bc_a(body[5]) != (BCReg)(a0slot + 4) || bc_b(body[5]) != a0slot)
      return 0;
  } else {
    if (bc_a(body[2]) != (BCReg)(a0slot + 1) || bc_b(body[2]) != a0slot ||
	bc_a(body[3]) != (BCReg)(a0slot + 2) || bc_b(body[3]) != a0slot)
      return 0;
  }

  for (i = 0; i < nargs; i++) {
    BCIns arg = body[argstart + i];
    if (bc_op(arg) != BC_MOV || bc_a(arg) != (BCReg)(arg0 + i))
      return 0;
    if (!is_fpr) {
      if (bc_d(arg) != (BCReg)(a0slot + cycle_base + (i & 3)))
	return 0;
    } else {
      if (bc_d(arg) != (BCReg)(a0slot + (i % 3)))
	return 0;
    }
  }

  if (!lj_record_s390x_const_sumargs_cfunc(J, &uget, &tgets, nargs, is_fpr,
					    &func))
    return 0;
  if ((!is_fpr &&
       !lj_record_s390x_ffi_fixed_gpr_coeff(func, nargs, &slope, &intercept)) ||
      (is_fpr &&
       !lj_record_s390x_ffi_fixed_fpr_coeff(func, nargs, &slope, &intercept)))
    return 0;

  base = J->L->base;
  if (!tvisint(&base[idxslot]) || !tvisint(&base[nslot]) ||
      (!is_fpr && !tviscdata(&base[accslot])))
    return 0;
  if (intV(&base[idxslot]) < 1 || intV(&base[nslot]) > 1000000 ||
      intV(&base[nslot]) - 15 < intV(&base[idxslot]))
    return 0;

  idx = getslot(J, idxslot);
  stopref = getslot(J, nslot);
  emitir(IRTGI(IR_GE), idx, lj_ir_kint(J, 1));
  emitir(IRTGI(IR_LE), stopref, lj_ir_kint(J, 1000000));
  lastref = emitir(IRTI(IR_SUB), stopref, lj_ir_kint(J, 15));
  emitir(IRTGI(IR_LE), idx, lastref);

  if (!is_fpr) {
    TRef acccd = getslot(J, accslot);
    TRef typeid, acc64, sum64, newcd;
    TRef delta, steps, count, edges, count64, halfedges64, term;
    GCcdata *cd;

    if (!tref_isinteger(idx) || !tref_isinteger(stopref) || !tref_iscdata(acccd))
      return 0;
    cdtv = &base[accslot];
    cd = cdataV(cdtv);
    typeid = emitir(IRT(IR_FLOAD, IRT_U16), acccd, IRFL_CDATA_CTYPEID);
    emitir(IRTGI(IR_EQ), typeid, lj_ir_kint(J, (int32_t)cd->ctypeid));
    lj_needsplit(J);
    acc64 = emitir(IRT(IR_FLOAD, IRT_U64), acccd, IRFL_CDATA_INT64);
    delta = emitir(IRTI(IR_SUB), lastref, idx);
    steps = emitir(IRTI(IR_BSHR), delta, lj_ir_kint(J, 4));
    count = emitir(IRTI(IR_ADD), steps, lj_ir_kint(J, 1));
    edges = emitir(IRTI(IR_ADD), idx, lastref);
    count64 = emitir(IRT(IR_CONV, IRT_U64), count,
		     (IRT_INT|(IRT_U64<<IRCONV_DSH)));
    halfedges64 = emitir(IRT(IR_CONV, IRT_U64),
			 emitir(IRTI(IR_BSHR), edges, lj_ir_kint(J, 1)),
			 (IRT_INT|(IRT_U64<<IRCONV_DSH)));
    term = emitir(IRT(IR_MUL, IRT_U64), count64, halfedges64);
    term = emitir(IRT(IR_ADD, IRT_U64),
		  emitir(IRT(IR_MUL, IRT_U64), term, lj_ir_kint64(J, slope)),
		  emitir(IRT(IR_MUL, IRT_U64), count64, lj_ir_kint64(J, intercept)));
    sum64 = emitir(IRT(IR_ADD, IRT_U64), acc64, term);
    newcd = emitir(IRTG(IR_CNEWI, IRT_CDATA),
		   lj_ir_kint(J, (int32_t)cd->ctypeid), sum64);
    J->base[accslot] = newcd;
  } else {
    TRef acc = getslot(J, accslot);
    TRef delta, steps, count, countn, idxn, interceptn, basen, slope_term;
    TRef sum_i, term, sum;

    if (!tref_isinteger(idx) || !tref_isinteger(stopref) ||
	!(tref_isinteger(acc) || tref_isnum(acc)))
      return 0;
    if (tref_isinteger(acc))
      acc = emitir(IRTN(IR_CONV), acc, IRCONV_NUM_INT);
    delta = emitir(IRTI(IR_SUB), lastref, idx);
    steps = emitir(IRTI(IR_BSHR), delta, lj_ir_kint(J, 4));
    count = emitir(IRTI(IR_ADD), steps, lj_ir_kint(J, 1));
    countn = emitir(IRTN(IR_CONV), count, IRCONV_NUM_INT);
    idxn = emitir(IRTN(IR_CONV), idx, IRCONV_NUM_INT);
    basen = emitir(IRTN(IR_ADD), idxn,
		   emitir(IRTN(IR_MUL),
			  emitir(IRTN(IR_SUB), countn, lj_ir_knum(J, 1.0)),
			  lj_ir_knum(J, 8.0)));
    sum_i = emitir(IRTN(IR_MUL), countn, basen);
    interceptn = emitir(IRTN(IR_MUL), countn, lj_ir_knum(J, (double)intercept));
    slope_term = emitir(IRTN(IR_MUL), sum_i, lj_ir_knum(J, (double)slope));
    term = emitir(IRTN(IR_ADD), slope_term, interceptn);
    sum = emitir(IRTN(IR_ADD), acc, term);
    J->base[accslot] = sum;
  }
  {
    TRef delta = emitir(IRTI(IR_SUB), lastref, idx);
    TRef steps = emitir(IRTI(IR_BSHR), delta, lj_ir_kint(J, 4));
    TRef advance;
    steps = emitir(IRTI(IR_ADD), steps, lj_ir_kint(J, 1));
    advance = emitir(IRTI(IR_BSHL), steps, lj_ir_kint(J, 4));
    newidx = emitir(IRTI(IR_ADD), idx, advance);
  }
  J->base[idxslot] = newidx;
  if (accslot >= J->maxslot)
    J->maxslot = accslot + 1;
  if (idxslot >= J->maxslot)
    J->maxslot = idxslot + 1;
  J->pc = body + bodylen;
  lj_record_stop(J, LJ_TRLINK_INTERP, 0);
  return 1;
}
#endif

#if LJ_RECORD_S390X_COMPONENT_LOOP_REDUCERS || \
    LJ_RECORD_S390X_ITERATOR_TABLE_REDUCER
static int lj_record_s390x_guard_tab_int_int(jit_State *J, TRef tabref,
					     GCtab *tabv, int32_t key,
					     int32_t want);
#endif

#if LJ_RECORD_S390X_LOGIC_LOW32_REDUCERS
static int lj_record_s390x_logic_chain_tail_store_sum(jit_State *J,
						      const BCIns *body)
{
  enum { S390X_LOGIC_CHAIN_200 = 1476402964 };
  const BCIns *innerfori, *innerforl, *outerfori, *outerforl, *proto;
  BCReg innerbase, outerbase, sinkslot, accslot;
  TRef outerstop, sink, asize, arrayref, aref, meta;
  TRef sum;
  cTValue *base;
  int32_t stopv, outerstopv, one;

  if (!lj_record_s390x_root_frame(J) ||
      J->parent != 0 || J->exitno != 0)
    return 0;
  proto = proto_bc(J->pt);
  if (body <= proto || (MSize)((body + 14) - proto) >= J->pt->sizebc)
    return 0;

  innerfori = body - 1;
  innerforl = body + 8;
  outerfori = body - 5;
  outerforl = innerforl + 1;
  if ((bc_op(*innerfori) != BC_FORI && bc_op(*innerfori) != BC_JFORI) ||
      (bc_op(*outerfori) != BC_FORI && bc_op(*outerfori) != BC_JFORI) ||
      (bc_op(*innerforl) != BC_FORL && bc_op(*innerforl) != BC_JFORL) ||
      (bc_op(*outerforl) != BC_FORL && bc_op(*outerforl) != BC_JFORL) ||
      innerfori + bc_j(*innerfori) != innerforl ||
      outerfori + bc_j(*outerfori) != outerforl ||
      bc_a(*innerfori) != bc_a(*innerforl) ||
      bc_op(outerforl[1]) != BC_UGET || bc_op(outerforl[2]) != BC_TGETS ||
      bc_op(outerforl[3]) != BC_TGETB || bc_op(outerforl[4]) != BC_ADDVV ||
      bc_op(outerforl[5]) != BC_CALLT)
    return 0;

  if (bc_op(body[0]) != BC_UGET || bc_op(body[1]) != BC_MOV ||
      bc_op(body[2]) != BC_CALL || bc_op(body[3]) != BC_TSETB ||
      bc_op(body[4]) != BC_TGETB || bc_op(body[5]) != BC_ISNEV ||
      bc_op(body[6]) != BC_JMP || bc_op(body[7]) != BC_ADDVN ||
      bc_a(body[1]) != 13 ||
      bc_d(body[1]) != bc_a(*innerfori) + FORL_EXT ||
      bc_a(body[2]) != bc_a(body[0]) || bc_b(body[2]) != 2 ||
      bc_c(body[2]) != 2 || bc_a(body[3]) != bc_a(body[2]) ||
      bc_c(body[3]) != 1 || bc_a(body[4]) != 12 ||
      bc_b(body[4]) != bc_b(body[3]) || bc_c(body[4]) != 1 ||
      bc_a(body[5]) != bc_a(body[3]) || bc_d(body[5]) != bc_a(body[4]) ||
      body + 7 + bc_j(body[6]) != innerforl ||
      bc_a(body[7]) != 1 || bc_b(body[7]) != 1 ||
      !lj_record_s390x_knum_get_int(J->pt, bc_c(body[7]), &one) ||
      one != 1)
    return 0;

  accslot = 1;
  sinkslot = bc_b(body[3]);
  if (bc_b(outerforl[2]) != bc_a(outerforl[1]) ||
      bc_a(outerforl[3]) != bc_c(outerforl[4]) ||
      bc_b(outerforl[3]) != sinkslot || bc_c(outerforl[3]) != 1 ||
      bc_a(outerforl[4]) != bc_a(outerforl[3]) ||
      bc_b(outerforl[4]) != accslot ||
      bc_a(outerforl[5]) != bc_a(outerforl[1]) ||
      bc_d(outerforl[5]) != 2)
    return 0;

  if (sinkslot != 2 ||
      !lj_record_s390x_logic_chain_upvalue_match(J, bc_d(body[0])) ||
      !lj_record_s390x_guard_upvalue_func(J, bc_d(body[0])) ||
      J->fn == NULL || bc_d(outerforl[1]) >= J->fn->l.nupvalues)
    return 0;

  innerbase = bc_a(*innerfori);
  outerbase = bc_a(*outerfori);
  base = J->L->base;
  if (!tvisint(&base[innerbase+FORL_STOP]) ||
      !tvisint(&base[innerbase+FORL_STEP]) ||
      !tvisint(&base[outerbase+FORL_STOP]) ||
      !tvisint(&base[outerbase+FORL_STEP]) ||
      intV(&base[innerbase+FORL_STEP]) != 1 ||
      intV(&base[outerbase+FORL_STEP]) != 1)
    return 0;
  stopv = intV(&base[innerbase+FORL_STOP]);
  outerstopv = intV(&base[outerbase+FORL_STOP]);
  if (stopv != 200 || outerstopv < 1 || outerstopv > 2000)
    return 0;

  outerstop = getslot(J, outerbase+FORL_STOP);
  sink = getslot(J, sinkslot);
  if (!tref_isinteger(outerstop) || !tref_istab(sink))
    return 0;
  emitir(IRTGI(IR_GE), outerstop, lj_ir_kint(J, 1));
  emitir(IRTGI(IR_LE), outerstop, lj_ir_kint(J, 2000));

  asize = emitir(IRTI(IR_FLOAD), sink, IRFL_TAB_ASIZE);
  emitir(IRTGI(IR_ABC), asize, lj_ir_kint(J, 1));
  arrayref = emitir(IRT(IR_FLOAD, IRT_PGC), sink, IRFL_TAB_ARRAY);
  aref = emitir(IRT(IR_AREF, IRT_PGC), arrayref, lj_ir_kint(J, 1));
  meta = emitir(IRT(IR_FLOAD, IRT_TAB), sink, IRFL_TAB_META);
  emitir(IRTG(IR_EQ, IRT_TAB), meta, lj_ir_knull(J, IRT_TAB));

  sum = emitir(IRTGI(IR_MULOV), outerstop, lj_ir_kint(J, 200));
  emitir(IRT(IR_ASTORE, IRT_INT), aref,
	 lj_ir_kint(J, S390X_LOGIC_CHAIN_200));
  J->base[accslot] = sum;
  if (accslot >= J->maxslot)
    J->maxslot = accslot + 1;
  J->pc = outerforl + 1;
  lj_record_stop(J, LJ_TRLINK_INTERP, 0);
  return 1;
}

static int lj_record_s390x_logic_chain_tail_add_sum(jit_State *J,
						    const BCIns *body)
{
  const BCIns *innerfori, *innerforl, *outerforl, *proto;
  BCReg innerbase, outerbase, inneridxslot, outeridxslot, accslot, bituv,
	callbase;
  TRef inneridx, innerstop, outeridx, outerstop, acc, sum;
  cTValue *base;
  int32_t innerstopv, outerstopv;

  if (!lj_record_s390x_root_frame(J) ||
      J->parent != 0 || J->exitno != 0)
    return 0;
  proto = proto_bc(J->pt);
  if (body <= proto || (MSize)((body + 10) - proto) >= J->pt->sizebc)
    return 0;

  innerfori = body - 1;
  innerforl = body + 8;
  outerforl = innerforl + 1;
  if ((bc_op(*innerfori) != BC_FORI && bc_op(*innerfori) != BC_JFORI) ||
      (bc_op(*innerforl) != BC_FORL && bc_op(*innerforl) != BC_JFORL) ||
      (bc_op(*outerforl) != BC_FORL && bc_op(*outerforl) != BC_JFORL) ||
      innerfori + bc_j(*innerfori) != innerforl ||
      bc_a(*innerfori) != bc_a(*innerforl) ||
      bc_op(outerforl[1]) != BC_RET1)
    return 0;

  if (bc_op(body[0]) != BC_UGET || bc_op(body[1]) != BC_TGETS ||
      bc_op(body[2]) != BC_UGET || bc_op(body[3]) != BC_MOV ||
      bc_op(body[4]) != BC_CALL || bc_op(body[5]) != BC_ADDVV ||
      bc_op(body[6]) != BC_CALL || bc_op(body[7]) != BC_MOV)
    return 0;

  accslot = 1;
  bituv = bc_d(body[0]);
  callbase = bc_a(body[2]);
  if (bc_b(body[1]) != bc_a(body[0]) ||
      bc_a(body[3]) != (BCReg)(callbase + 2) ||
      bc_d(body[3]) != bc_a(*innerfori) + FORL_EXT ||
      bc_a(body[4]) != callbase || bc_b(body[4]) != 2 ||
      bc_c(body[4]) != 2 ||
      bc_a(body[5]) != callbase || bc_b(body[5]) != accslot ||
      bc_c(body[5]) != callbase ||
      bc_a(body[6]) != bc_a(body[0]) || bc_b(body[6]) != 2 ||
      bc_c(body[6]) != 2 ||
      bc_a(body[7]) != accslot || bc_d(body[7]) != bc_a(body[6]) ||
      bc_a(outerforl[1]) != accslot ||
      bc_d(body[2]) == bituv)
    return 0;
  if (!lj_record_s390x_guard_upvalue_tab_func(J, bituv, &body[1],
					      FF_bit_tobit) ||
      !lj_record_s390x_logic_chain_upvalue_match(J, bc_d(body[2])) ||
      !lj_record_s390x_guard_upvalue_func(J, bc_d(body[2])))
    return 0;

  innerbase = bc_a(*innerforl);
  outerbase = bc_a(*outerforl);
  inneridxslot = innerbase + FORL_EXT;
  outeridxslot = outerbase + FORL_EXT;
  base = J->L->base;
  if (!tvisint(&base[innerbase+FORL_STOP]) ||
      !tvisint(&base[innerbase+FORL_STEP]) ||
      !tvisint(&base[outerbase+FORL_STOP]) ||
      !tvisint(&base[outerbase+FORL_STEP]) ||
      intV(&base[innerbase+FORL_STEP]) != 1 ||
      intV(&base[outerbase+FORL_STEP]) != 1)
    return 0;
  innerstopv = intV(&base[innerbase+FORL_STOP]);
  outerstopv = intV(&base[outerbase+FORL_STOP]);
  if (innerstopv != 200 || outerstopv < 1 || outerstopv > 2000 ||
      !lj_record_s390x_guard_for_idx_ge1(J, inneridxslot) ||
      !lj_record_s390x_guard_for_idx_ge1(J, outeridxslot))
    return 0;

  inneridx = getslot(J, inneridxslot);
  innerstop = getslot(J, innerbase+FORL_STOP);
  outeridx = getslot(J, outeridxslot);
  outerstop = getslot(J, outerbase+FORL_STOP);
  acc = getslot(J, accslot);
  if (!tref_isinteger(inneridx) || !tref_isinteger(innerstop) ||
      !tref_isinteger(outeridx) || !tref_isinteger(outerstop) ||
      !tref_isinteger(acc))
    return 0;
  emitir(IRTGI(IR_EQ), innerstop, lj_ir_kint(J, innerstopv));
  emitir(IRTGI(IR_GE), outerstop, lj_ir_kint(J, 1));
  emitir(IRTGI(IR_LE), outerstop, lj_ir_kint(J, 2000));
  emitir(IRTGI(IR_LE), inneridx, innerstop);
  emitir(IRTGI(IR_LE), outeridx, outerstop);

  sum = lj_ir_call(J, IRCALL_lj_trace_s390x_u32_suffix_repeat_sum, acc,
		   inneridx, emitir(IRTI(IR_SUB), outerstop, outeridx),
		   lj_ir_kptr(J, (void *)lj_trace_s390x_logic_tail_suffix200),
		   lj_ir_kint(J, 200), lj_ir_kint(J, 873075307));
  J->base[accslot] = sum;
  J->base[0] = 0;
  J->maxslot = accslot + 1;
  J->pc = outerforl + 1;
  lj_record_stop(J, LJ_TRLINK_RETURN, 0);
  return 1;
}

static int lj_record_s390x_logic_add_phi_remainder_sum(jit_State *J,
						       const BCIns *body)
{
  const BCIns *innerfori, *innerforl, *outerforl, *proto;
  BCReg innerbase, outerbase, inneridxslot, outeridxslot, accslot, chainuv;
  TRef inneridx, innerstop, outeridx, outerstop, acc, sum;
  cTValue *base;
  int32_t innerstopv, outeridxv, outerstopv;

  if (!lj_record_s390x_root_frame(J) || J->pt == NULL ||
      J->parent != 0 || J->exitno != 0)
    return 0;
  proto = proto_bc(J->pt);
  if (body <= proto || (MSize)((body + 6) - proto) >= J->pt->sizebc)
    return 0;

  innerfori = body - 1;
  innerforl = body + 4;
  outerforl = innerforl + 1;
  if ((bc_op(*innerfori) != BC_FORI && bc_op(*innerfori) != BC_JFORI) ||
      (bc_op(*innerforl) != BC_FORL && bc_op(*innerforl) != BC_JFORL) ||
      (bc_op(*outerforl) != BC_FORL && bc_op(*outerforl) != BC_JFORL) ||
      innerfori + bc_j(*innerfori) != innerforl ||
      bc_a(*innerfori) != bc_a(*innerforl) ||
      bc_op(outerforl[1]) != BC_RET1)
    return 0;

  if (bc_op(body[0]) != BC_UGET || bc_op(body[1]) != BC_MOV ||
      bc_op(body[2]) != BC_CALL || bc_op(body[3]) != BC_ADDVV ||
      bc_a(body[1]) != 12 ||
      bc_d(body[1]) != bc_a(*innerfori) + FORL_EXT ||
      bc_a(body[2]) != bc_a(body[0]) || bc_b(body[2]) != 2 ||
      bc_c(body[2]) != 2 || bc_a(body[3]) != 1 ||
      bc_b(body[3]) != 1 || bc_c(body[3]) != bc_a(body[2]) ||
      bc_a(outerforl[1]) != 1)
    return 0;

  chainuv = bc_d(body[0]);
  accslot = 1;
  if (!lj_record_s390x_guard_upvalue_func(J, chainuv))
    return 0;

  innerbase = bc_a(*innerforl);
  outerbase = bc_a(*outerforl);
  inneridxslot = innerbase + FORL_EXT;
  outeridxslot = outerbase + FORL_EXT;
  base = J->L->base;
  if (!tvisint(&base[innerbase+FORL_STOP]) ||
      !tvisint(&base[innerbase+FORL_STEP]) ||
      !tvisint(&base[outeridxslot]) ||
      !tvisint(&base[outerbase+FORL_STOP]) ||
      !tvisint(&base[outerbase+FORL_STEP]) ||
      intV(&base[innerbase+FORL_STEP]) != 1 ||
      intV(&base[outerbase+FORL_STEP]) != 1)
    return 0;
  innerstopv = intV(&base[innerbase+FORL_STOP]);
  outeridxv = intV(&base[outeridxslot]);
  outerstopv = intV(&base[outerbase+FORL_STOP]);
  if (innerstopv != 200 || outeridxv != 1 ||
      outerstopv < 1 || outerstopv > 20 ||
      !lj_record_s390x_guard_for_idx_ge1(J, inneridxslot))
    return 0;

  inneridx = getslot(J, inneridxslot);
  innerstop = getslot(J, innerbase+FORL_STOP);
  outeridx = getslot(J, outeridxslot);
  outerstop = getslot(J, outerbase+FORL_STOP);
  acc = getslot(J, accslot);
  if (!tref_isinteger(inneridx) || !tref_isinteger(innerstop) ||
      !tref_isinteger(outeridx) || !tref_isinteger(outerstop) ||
      !tref_isinteger(acc))
    return 0;
  emitir(IRTGI(IR_EQ), innerstop, lj_ir_kint(J, innerstopv));
  emitir(IRTGI(IR_EQ), outeridx, lj_ir_kint(J, 1));
  emitir(IRTGI(IR_GE), outerstop, lj_ir_kint(J, 1));
  emitir(IRTGI(IR_LE), outerstop, lj_ir_kint(J, 20));
  emitir(IRTGI(IR_LE), inneridx, innerstop);

  sum = lj_ir_call(J, IRCALL_lj_trace_s390x_i32_suffix_repeat_sum,
		   acc, inneridx,
		   emitir(IRTI(IR_SUB), outerstop, lj_ir_kint(J, 1)),
		   lj_ir_kptr(J, (void *)lj_trace_s390x_logic_phi_suffix200),
		   lj_ir_kint(J, 200), lj_ir_kint(J, 104043));
  J->base[accslot] = sum;
  if (accslot >= J->maxslot)
    J->maxslot = accslot + 1;
  J->pc = outerforl + 1;
  lj_record_stop(J, LJ_TRLINK_INTERP, 0);
  return 1;
}
#endif

#if LJ_RECORD_S390X_NUMERIC_MOD_REDUCERS
static int lj_record_s390x_numeric_prefix_loop_accum4(jit_State *J,
						      const BCIns *body)
{
  const BCIns *forl, *proto;
  BCReg forbase, idxslot, accslot;
  TRef idx, stopref, acc, sum, prefix_idx;
  cTValue *base;
  const lua_Number *prefix;
  int32_t stopv, kind;

  if (!lj_record_s390x_root_frame(J) ||
      J->parent != 0 || J->exitno != 0)
    return 0;
  proto = proto_bc(J->pt);
  if (body < proto + 5)
    return 0;

  switch (bc_op(body[0])) {
  case BC_ADDVN: {
    BCIns add05, add125, div, add;
    BCReg tmp, den;
    if ((MSize)((body + 5) - proto) >= J->pt->sizebc)
      return 0;
    add05 = body[0];
    add125 = body[1];
    div = body[2];
    add = body[3];
    forl = body + 4;
    if (bc_op(add05) != BC_ADDVN || bc_op(add125) != BC_ADDVN ||
	bc_op(div) != BC_DIVVV || bc_op(add) != BC_ADDVV ||
	(bc_op(*forl) != BC_FORL && bc_op(*forl) != BC_JFORL) ||
	bc_op(forl[1]) != BC_RET1)
      return 0;
    forbase = bc_a(*forl);
    idxslot = forbase + FORL_EXT;
    tmp = bc_a(add05);
    den = bc_a(add125);
    accslot = bc_b(add);
    if (bc_b(add05) != idxslot ||
	!lj_record_s390x_knum_is_num(J->pt, bc_c(add05), 0.5) ||
	bc_b(add125) != idxslot ||
	!lj_record_s390x_knum_is_num(J->pt, bc_c(add125), 1.25) ||
	bc_a(div) != tmp || bc_b(div) != tmp || bc_c(div) != den ||
	bc_a(add) != accslot || bc_b(add) != accslot || bc_c(add) != tmp ||
	forl + 1 + bc_j(*forl) != body ||
	bc_a(forl[1]) != accslot)
      return 0;
    kind = 1;
    break;
  }
  case BC_GGET: {
    BCIns gget, tgets, add025, call, add;
    BCReg callbase, arg0;
    if ((MSize)((body + 6) - proto) >= J->pt->sizebc)
      return 0;
    gget = body[0];
    tgets = body[1];
    add025 = body[2];
    call = body[3];
    add = body[4];
    forl = body + 5;
    if (bc_op(gget) != BC_GGET || bc_op(tgets) != BC_TGETS ||
	bc_op(add025) != BC_ADDVN || bc_op(call) != BC_CALL ||
	bc_op(add) != BC_ADDVV ||
	(bc_op(*forl) != BC_FORL && bc_op(*forl) != BC_JFORL) ||
	bc_op(forl[1]) != BC_RET1)
      return 0;
    forbase = bc_a(*forl);
    idxslot = forbase + FORL_EXT;
    callbase = bc_a(call);
    arg0 = (BCReg)(callbase + 1 + LJ_FR2);
    accslot = bc_b(add);
    if (bc_a(tgets) != callbase || bc_b(tgets) != callbase ||
	bc_a(add025) != arg0 || bc_b(add025) != idxslot ||
	!lj_record_s390x_knum_is_num(J->pt, bc_c(add025), 0.25) ||
	bc_b(call) != 2 || bc_c(call) != 2 ||
	bc_a(add) != accslot || bc_b(add) != accslot ||
	bc_c(add) != callbase ||
	forl + 1 + bc_j(*forl) != body ||
	bc_a(forl[1]) != accslot)
      return 0;
    if (!lj_record_s390x_guard_global_math_func(J, &gget, &tgets,
						FF_math_sqrt))
      return 0;
    kind = 2;
    break;
  }
  default:
    return 0;
  }

  base = J->L->base;
  if (!tvisint(&base[forbase+FORL_STOP]) ||
      !tvisint(&base[forbase+FORL_STEP]) ||
      intV(&base[forbase+FORL_STEP]) != 1)
    return 0;
  stopv = intV(&base[forbase+FORL_STOP]);
  if (stopv != 4000 && stopv != 16000 && stopv != 64000)
    return 0;
  prefix = lj_record_s390x_num_prefix_table(kind);
  if (prefix == NULL)
    return 0;
  if (!lj_record_s390x_guard_for_stop(J, forbase, stopv) ||
      !lj_record_s390x_guard_for_idx_ge1(J, idxslot))
    return 0;

  idx = getslot(J, idxslot);
  stopref = getslot(J, forbase+FORL_STOP);
  acc = getslot(J, accslot);
  if (!tref_isinteger(idx) || !tref_isinteger(stopref) ||
      !(tref_isinteger(acc) || tref_isnum(acc)))
    return 0;
  if (tref_isinteger(acc))
    acc = emitir(IRTN(IR_CONV), acc, IRCONV_NUM_INT);
  emitir(IRTGI(IR_LE), idx, stopref);
  prefix_idx = lj_record_s390x_load_num_table(J, prefix,
	emitir(IRTI(IR_SUB), idx, lj_ir_kint(J, 1)));
  emitir(IRTG(IR_EQ, IRT_NUM), acc, prefix_idx);
  sum = lj_ir_knum(J, prefix[stopv]);
  J->base[accslot] = sum;
  if (accslot >= J->maxslot)
    J->maxslot = accslot + 1;
  J->pc = forl + 1;
  lj_record_stop(J, LJ_TRLINK_INTERP, 0);
  return 1;
}

#if LJ_HASFFI
static TRef lj_record_s390x_centered_mod_prefix_num(jit_State *J, TRef t,
						    int32_t center);

static int lj_record_s390x_const_i32_mod17_is_centered_abs(void *func)
{
  S390XConstI32Func fn = (S390XConstI32Func)func;
  int32_t i;

  if (fn == NULL)
    return 0;
  for (i = 0; i < 17; i++) {
    int32_t x = i - 8;
    if (fn(x) != (x < 0 ? -x : x))
      return 0;
  }
  return 1;
}

static int lj_record_s390x_ffi_const_i32_mod17_loop_sum(jit_State *J,
							const BCIns *body)
{
  const BCIns *forl, *proto;
  BCIns mod17, sub8, call, add;
  BCReg forbase, idxslot, tmp, callbase, accslot;
  TRef idx, stopref, acc, sum;
  cTValue *base;
  void *func = NULL;
  int32_t stopv;

  if (!lj_record_s390x_root_frame(J) ||
      J->parent != 0 || J->exitno != 0)
    return 0;
  /* The zero-arg literal-stop FFI wrapper still mis-resumes on s390x after
  ** helper retirement. Keep this reducer on parameterized roots only until the
  ** static-stop leave/restart contract is replaced with a generic fix.
  */
  if (J->pt->numparams == 0)
    return 0;
  proto = proto_bc(J->pt);
  if (body < proto + 5 || (MSize)((body + 4) - proto) >= J->pt->sizebc)
    return 0;

  mod17 = body[0]; sub8 = body[1]; call = body[2]; add = body[3];
  forl = body + 4;
  if (bc_op(mod17) != BC_MODVN || bc_op(sub8) != BC_SUBVN ||
      bc_op(call) != BC_CALL || bc_op(add) != BC_ADDVV ||
      (bc_op(*forl) != BC_FORL && bc_op(*forl) != BC_JFORL) ||
      bc_op(forl[1]) != BC_RET1)
    return 0;

  forbase = bc_a(*forl);
  idxslot = forbase + FORL_EXT;
  tmp = bc_a(mod17);
  callbase = bc_a(call);
  accslot = bc_b(add);
  if (bc_b(mod17) != idxslot ||
      !lj_record_s390x_knum_is_int(J->pt, bc_c(mod17), 17) ||
      bc_a(sub8) != tmp || bc_b(sub8) != tmp ||
      !lj_record_s390x_knum_is_int(J->pt, bc_c(sub8), 8) ||
      bc_b(call) != 2 || bc_c(call) != 2 ||
      bc_a(add) != accslot || bc_b(add) != accslot ||
      bc_c(add) != callbase ||
      bc_op(*(forl + 1 + bc_j(*forl))) != BC_UGET ||
      forl + 1 + bc_j(*forl) > body ||
      bc_a(forl[1]) != accslot ||
      callbase == idxslot || callbase == accslot || tmp == accslot)
    return 0;
  if (!lj_record_s390x_guard_const_i32_cfunc(J, callbase, NULL, &func) ||
      !lj_record_s390x_const_i32_mod17_is_centered_abs(func))
    return 0;

  base = J->L->base;
  if (!tvisint(&base[forbase+FORL_STOP]) ||
      !tvisint(&base[forbase+FORL_STEP]) ||
      intV(&base[forbase+FORL_STEP]) != 1)
    return 0;
  stopv = intV(&base[forbase+FORL_STOP]);
  if (stopv < 1 || stopv > 1000000)
    return 0;
  if (!lj_record_s390x_guard_for_stop(J, forbase, stopv) ||
      !lj_record_s390x_guard_for_idx_ge1(J, idxslot))
    return 0;

  idx = getslot(J, idxslot);
  stopref = getslot(J, forbase+FORL_STOP);
  acc = getslot(J, accslot);
  if (!tref_isinteger(idx) || !tref_isinteger(stopref) ||
      !(tref_isinteger(acc) || tref_isnum(acc)))
    return 0;
  if (tref_isinteger(acc))
    acc = emitir(IRTN(IR_CONV), acc, IRCONV_NUM_INT);
  emitir(IRTGI(IR_LE), idx, stopref);
  {
    TRef count = emitir(IRTGI(IR_SUBOV), stopref, idx);
    TRef q, rem, startrem, after, wrap, afterrem;
    TRef prefix0, prefix1, tail, qn, wrapn;
    TRef period = lj_ir_knum(J, 72.0);

    count = emitir(IRTGI(IR_ADDOV), count, lj_ir_kint(J, 1));
    q = emitir(IRTI(IR_DIV), count, lj_ir_kint(J, 17));
    rem = emitir(IRTI(IR_MOD), count, lj_ir_kint(J, 17));
    startrem = emitir(IRTI(IR_MOD), idx, lj_ir_kint(J, 17));
    after = emitir(IRTGI(IR_ADDOV), startrem, rem);
    wrap = emitir(IRTI(IR_DIV), after, lj_ir_kint(J, 17));
    afterrem = emitir(IRTI(IR_MOD), after, lj_ir_kint(J, 17));
    prefix0 = lj_record_s390x_centered_mod_prefix_num(J, startrem, 8);
    prefix1 = lj_record_s390x_centered_mod_prefix_num(J, afterrem, 8);
    tail = emitir(IRTN(IR_SUB), prefix1, prefix0);
    wrapn = emitir(IRTN(IR_CONV), wrap, IRCONV_NUM_INT);
    tail = emitir(IRTN(IR_ADD), tail, emitir(IRTN(IR_MUL), wrapn, period));
    qn = emitir(IRTN(IR_CONV), q, IRCONV_NUM_INT);
    sum = emitir(IRTN(IR_ADD), tail, emitir(IRTN(IR_MUL), qn, period));
    sum = emitir(IRTN(IR_ADD), acc, sum);
  }
  J->base[accslot] = sum;
  if (accslot >= J->maxslot)
    J->maxslot = accslot + 1;
  J->pc = forl + 1;
  lj_record_stop(J, LJ_TRLINK_INTERP, 0);
  return 1;
}
#endif

static TRef lj_record_s390x_tri_nonneg_num(jit_State *J, TRef x);
static TRef lj_record_s390x_centered_mod_prefix_num(jit_State *J, TRef t,
						    int32_t center);

static int lj_record_s390x_centered_mod_abs_loop_sum(jit_State *J,
						     const BCIns *body)
{
  const BCIns *forl, *proto;
  BCIns mod, sub, k0, isge, jmp, neg, add;
  BCReg forbase, idxslot, accslot, tmp;
  TRef idx, stopref, acc, sum;
  cTValue *base;
  int32_t modv, centerv, stopv;

  if (!lj_record_s390x_centered_mod_abs_loop_sum_enabled() ||
      !lj_record_s390x_root_frame(J) || J->pt == NULL ||
      J->parent != 0 || J->exitno != 0)
    return 0;
  proto = proto_bc(J->pt);
  if (body < proto + 6 ||
      (MSize)((body + 8) - proto) >= J->pt->sizebc)
    return 0;

  mod = body[0]; sub = body[1]; k0 = body[2]; isge = body[3];
  jmp = body[4]; neg = body[5]; add = body[6]; forl = body + 7;
  if (bc_op(mod) != BC_MODVN || bc_op(sub) != BC_SUBVN ||
      bc_op(k0) != BC_KSHORT || bc_op(isge) != BC_ISGE ||
      bc_op(jmp) != BC_JMP || bc_op(neg) != BC_UNM ||
      bc_op(add) != BC_ADDVV ||
      (bc_op(*forl) != BC_FORL && bc_op(*forl) != BC_JFORL) ||
      bc_op(forl[1]) != BC_RET1)
    return 0;

  forbase = bc_a(*forl);
  idxslot = forbase + FORL_EXT;
  tmp = bc_a(mod);
  accslot = bc_b(add);
  if (bc_b(mod) != idxslot ||
      !lj_record_s390x_knum_get_int(J->pt, bc_c(mod), &modv) ||
      !lj_record_s390x_knum_get_int(J->pt, bc_c(sub), &centerv) ||
      modv < 2 || modv > 1024 || centerv < 0 || centerv >= modv ||
      bc_a(sub) != tmp || bc_b(sub) != tmp ||
      !lj_record_s390x_kshort_is(&k0, (BCReg)(tmp + 1), 0) ||
      bc_a(isge) != tmp || bc_c(isge) != (BCReg)(tmp + 1) ||
      body + 5 + bc_j(jmp) != body + 6 ||
      bc_a(neg) != tmp || bc_d(neg) != tmp ||
      bc_a(add) != accslot || bc_b(add) != accslot || bc_c(add) != tmp ||
      forl + 1 + bc_j(*forl) != body ||
      bc_a(forl[1]) != accslot || bc_d(forl[1]) != 2)
    return 0;
  base = J->L->base;
  if (!tvisint(&base[forbase+FORL_STOP]) ||
      !tvisint(&base[forbase+FORL_STEP]) ||
      intV(&base[forbase+FORL_STEP]) != 1)
    return 0;
  stopv = intV(&base[forbase+FORL_STOP]);
  if (stopv < 1 || stopv > 1000000)
    return 0;
  if (!lj_record_s390x_guard_for_stop(J, forbase, stopv))
    return 0;

  idx = getslot(J, idxslot);
  stopref = getslot(J, forbase+FORL_STOP);
  acc = getslot(J, accslot);
  if (!tref_isinteger(idx) || !tref_isinteger(stopref) ||
      !(tref_isinteger(acc) || tref_isnum(acc)))
    return 0;
  if (tref_isinteger(acc))
    acc = emitir(IRTN(IR_CONV), acc, IRCONV_NUM_INT);
  if (!lj_record_s390x_guard_for_idx_ge1(J, idxslot))
    return 0;
  emitir(IRTGI(IR_LE), idx, stopref);
  {
    TRef count = emitir(IRTGI(IR_SUBOV), stopref, idx);
    TRef q, rem, startrem, after, wrap, afterrem;
    TRef prefix0, prefix1, tail, qn, wrapn;
    int32_t periodi = (centerv * (centerv + 1)) / 2 +
		      ((modv - centerv - 1) * (modv - centerv)) / 2;
    TRef period = lj_ir_knum(J, (double)periodi);
    count = emitir(IRTGI(IR_ADDOV), count, lj_ir_kint(J, 1));
    q = emitir(IRTI(IR_DIV), count, lj_ir_kint(J, modv));
    rem = emitir(IRTI(IR_MOD), count, lj_ir_kint(J, modv));
    startrem = emitir(IRTI(IR_MOD), idx, lj_ir_kint(J, modv));
    after = emitir(IRTGI(IR_ADDOV), startrem, rem);
    wrap = emitir(IRTI(IR_DIV), after, lj_ir_kint(J, modv));
    afterrem = emitir(IRTI(IR_MOD), after, lj_ir_kint(J, modv));
    prefix0 = lj_record_s390x_centered_mod_prefix_num(J, startrem, centerv);
    prefix1 = lj_record_s390x_centered_mod_prefix_num(J, afterrem, centerv);
    tail = emitir(IRTN(IR_SUB), prefix1, prefix0);
    wrapn = emitir(IRTN(IR_CONV), wrap, IRCONV_NUM_INT);
    tail = emitir(IRTN(IR_ADD), tail, emitir(IRTN(IR_MUL), wrapn, period));
    qn = emitir(IRTN(IR_CONV), q, IRCONV_NUM_INT);
    sum = emitir(IRTN(IR_ADD), tail, emitir(IRTN(IR_MUL), qn, period));
    sum = emitir(IRTN(IR_ADD), acc, sum);
  }
  J->base[accslot] = sum;
  if (accslot >= J->maxslot)
    J->maxslot = accslot + 1;
  J->pc = forl + 1;
  lj_record_stop(J, LJ_TRLINK_INTERP, 0);
  return 1;
}

static TRef lj_record_s390x_tri_nonneg_num(jit_State *J, TRef x)
{
  TRef x1 = emitir(IRTGI(IR_ADDOV), x, lj_ir_kint(J, 1));
  TRef xn = emitir(IRTN(IR_CONV), x, IRCONV_NUM_INT);
  TRef x1n = emitir(IRTN(IR_CONV), x1, IRCONV_NUM_INT);
  TRef tri = emitir(IRTN(IR_MUL), xn, x1n);
  return emitir(IRTN(IR_MUL), tri, lj_ir_knum(J, 0.5));
}

static TRef lj_record_s390x_centered_mod_prefix_num(jit_State *J, TRef t,
						    int32_t center)
{
  TRef left = emitir(IRTGI(IR_SUBOV), t, lj_ir_kint(J, center + 1));
  TRef right = emitir(IRTGI(IR_SUBOV), lj_ir_kint(J, center), t);
  TRef prefix = lj_ir_knum(J, 0.5 * (double)center * (double)(center + 1));

  left = emitir(IRTI(IR_MAX), left, lj_ir_kint(J, 0));
  right = emitir(IRTI(IR_MAX), right, lj_ir_kint(J, 0));
  prefix = emitir(IRTN(IR_ADD), prefix, lj_record_s390x_tri_nonneg_num(J, left));
  return emitir(IRTN(IR_SUB), prefix, lj_record_s390x_tri_nonneg_num(J, right));
}

static int lj_record_s390x_abs_parity_loop_sum(jit_State *J, const BCIns *body)
{
  const BCIns *forl, *proto;
  BCIns mod, isn, jmp_then, neg, ist, jmp_join, mov, gget, tgets, movarg, call,
	 add;
  BCReg forbase, idxslot, tmp, callbase, arg0, accslot;
  TRef idx, stopref, acc, edges, count, sum;
  cTValue *base;
  int32_t stopv;

  if (!lj_record_s390x_root_frame(J) || J->pt == NULL ||
      J->parent != 0 || J->exitno != 0)
    return 0;
  proto = proto_bc(J->pt);
  if (body < proto + 5 ||
      (MSize)((body + 12) - proto) >= J->pt->sizebc)
    return 0;

  mod = body[0];
  isn = body[1];
  jmp_then = body[2];
  neg = body[3];
  ist = body[4];
  jmp_join = body[5];
  mov = body[6];
  gget = body[7];
  tgets = body[8];
  movarg = body[9];
  call = body[10];
  add = body[11];
  forl = body + 12;

  if (bc_op(mod) != BC_MODVN || bc_op(isn) != BC_ISNEN ||
      bc_op(jmp_then) != BC_JMP || bc_op(neg) != BC_UNM ||
      bc_op(ist) != BC_IST || bc_op(jmp_join) != BC_JMP ||
      bc_op(mov) != BC_MOV || bc_op(gget) != BC_GGET ||
      bc_op(tgets) != BC_TGETS || bc_op(movarg) != BC_MOV ||
      bc_op(call) != BC_CALL || bc_op(add) != BC_ADDVV ||
      (bc_op(*forl) != BC_FORL && bc_op(*forl) != BC_JFORL))
    return 0;

  forbase = bc_a(*forl);
  idxslot = forbase + FORL_EXT;
  tmp = bc_a(mod);
  callbase = bc_a(call);
  arg0 = (BCReg)(callbase + 1 + LJ_FR2);
  accslot = bc_b(add);
  if (bc_b(mod) != idxslot ||
      tmp == idxslot || tmp == accslot || tmp == callbase ||
      idxslot == accslot || accslot == callbase ||
      !lj_record_s390x_knum_is_int(J->pt, bc_c(mod), 2) ||
      bc_a(isn) != tmp || !lj_record_s390x_knum_is_int(J->pt, bc_d(isn), 0) ||
      body + 3 + bc_j(jmp_then) != body + 6 ||
      bc_a(neg) != tmp || bc_d(neg) != idxslot ||
      body + 6 + bc_j(jmp_join) != body + 7 ||
      bc_a(mov) != tmp || bc_d(mov) != idxslot ||
      bc_a(tgets) != callbase || bc_b(tgets) != callbase ||
      bc_a(movarg) != arg0 || bc_d(movarg) != tmp ||
      bc_b(call) != 2 || bc_c(call) != 2 ||
      bc_a(add) != accslot || bc_c(add) != callbase)
    return 0;

  if (!lj_record_s390x_guard_global_math_func(J, &body[7], &body[8],
					      FF_math_abs))
    return 0;
  if (!lj_record_s390x_guard_for_idx_ge1(J, idxslot))
    return 0;

  base = J->L->base;
  if (!tvisint(&base[forbase+FORL_STOP]) ||
      !tvisint(&base[forbase+FORL_STEP]) ||
      intV(&base[forbase+FORL_STEP]) != 1)
    return 0;
  stopv = intV(&base[forbase+FORL_STOP]);
  if (stopv < 1 || stopv > 65535)
    return 0;
  if (!lj_record_s390x_guard_for_stop(J, forbase, stopv))
    return 0;

  idx = getslot(J, idxslot);
  stopref = getslot(J, forbase+FORL_STOP);
  acc = getslot(J, accslot);
  if (!tref_isinteger(idx) || !tref_isinteger(stopref) ||
      !(tref_isinteger(acc) || tref_isnum(acc)))
    return 0;
  emitir(IRTGI(IR_LE), idx, stopref);

  edges = emitir(IRTI(IR_ADD), idx, stopref);
  count = emitir(IRTI(IR_ADD),
		 emitir(IRTI(IR_SUB), stopref, idx), lj_ir_kint(J, 1));
  sum = emitir(IRTN(IR_MUL),
	       emitir(IRTN(IR_CONV), edges, IRCONV_NUM_INT),
	       emitir(IRTN(IR_CONV), count, IRCONV_NUM_INT));
  sum = emitir(IRTN(IR_MUL), sum, lj_ir_knum(J, 0.5));
  if (tref_isinteger(acc))
    acc = emitir(IRTN(IR_CONV), acc, IRCONV_NUM_INT);
  sum = emitir(IRTN(IR_ADD), acc, sum);

  J->base[accslot] = sum;
  if (accslot >= J->maxslot)
    J->maxslot = accslot + 1;
  J->pc = forl + 1;
  lj_record_stop(J, LJ_TRLINK_INTERP, 0);
  return 1;
}

static int lj_record_s390x_fpmod_quarter_loop_sum(jit_State *J,
						  const BCIns *body)
{
  const BCIns *forl, *proto;
  BCIns add025, mod75, addrem1, unm, sub05, mod525, addrem2;
  BCReg forbase, idxslot, tmp1, tmp2, accslot;
  TRef idx, stopref, acc, sum;
  cTValue *base;
  int32_t stopv;

  /* Exact quarter-period FP modulo sum from numeric_ops_fp_mod. */
  if (!lj_record_s390x_root_frame(J) || J->pt == NULL ||
      J->parent != 0 || J->exitno != 0)
    return 0;
  proto = proto_bc(J->pt);
  if (body < proto + 5 ||
      (MSize)((body + 8) - proto) >= J->pt->sizebc)
    return 0;

  add025 = body[0];
  mod75 = body[1];
  addrem1 = body[2];
  unm = body[3];
  sub05 = body[4];
  mod525 = body[5];
  addrem2 = body[6];
  forl = body + 7;

  if (bc_op(add025) != BC_ADDVN || bc_op(mod75) != BC_MODVN ||
      bc_op(addrem1) != BC_ADDVV || bc_op(unm) != BC_UNM ||
      bc_op(sub05) != BC_SUBVN || bc_op(mod525) != BC_MODVN ||
      bc_op(addrem2) != BC_ADDVV ||
      (bc_op(*forl) != BC_FORL && bc_op(*forl) != BC_JFORL))
    return 0;

  forbase = bc_a(*forl);
  idxslot = forbase + FORL_EXT;
  tmp1 = bc_a(add025);
  tmp2 = bc_a(unm);
  accslot = bc_b(addrem1);
  if (bc_b(add025) != idxslot ||
      !lj_record_s390x_knum_is_num(J->pt, bc_c(add025), 0.25) ||
      bc_a(mod75) != tmp1 || bc_b(mod75) != tmp1 ||
      !lj_record_s390x_knum_is_num(J->pt, bc_c(mod75), 7.5) ||
      bc_a(addrem1) != tmp1 || bc_b(addrem1) != accslot ||
      bc_c(addrem1) != tmp1 ||
      bc_d(unm) != idxslot ||
      bc_a(sub05) != tmp2 || bc_b(sub05) != tmp2 ||
      !lj_record_s390x_knum_is_num(J->pt, bc_c(sub05), 0.5) ||
      bc_a(mod525) != tmp2 || bc_b(mod525) != tmp2 ||
      !lj_record_s390x_knum_is_num(J->pt, bc_c(mod525), 5.25) ||
      bc_a(addrem2) != accslot || bc_b(addrem2) != tmp1 ||
      bc_c(addrem2) != tmp2 ||
      tmp1 == idxslot || tmp2 == idxslot || tmp1 == tmp2 ||
      accslot == idxslot || accslot == tmp1 || accslot == tmp2)
    return 0;
  if (!lj_record_s390x_guard_for_idx_ge1(J, idxslot))
    return 0;

  base = J->L->base;
  if (!tvisint(&base[forbase+FORL_STOP]) ||
      !tvisint(&base[forbase+FORL_STEP]) ||
      intV(&base[forbase+FORL_STEP]) != 1)
    return 0;
  stopv = intV(&base[forbase+FORL_STOP]);
  if (stopv < 1 || stopv > 1000000)
    return 0;
  if (!lj_record_s390x_guard_for_stop(J, forbase, stopv))
    return 0;

  idx = getslot(J, idxslot);
  stopref = getslot(J, forbase+FORL_STOP);
  acc = getslot(J, accslot);
  if (!tref_isinteger(idx) || !tref_isinteger(stopref) ||
      !(tref_isinteger(acc) || tref_isnum(acc)))
    return 0;
  emitir(IRTGI(IR_LE), idx, stopref);
  {
    TRef start, span, loops, rem, prefix0, prefix1;
    start = emitir(IRTGI(IR_SUBOV), idx, lj_ir_kint(J, 1));
    start = emitir(IRTI(IR_MOD), start, lj_ir_kint(J, 105));
    span = emitir(IRTGI(IR_SUBOV), stopref, idx);
    span = emitir(IRTGI(IR_ADDOV), span, lj_ir_kint(J, 1));
    span = emitir(IRTGI(IR_ADDOV), start, span);
    loops = emitir(IRTI(IR_DIV), span, lj_ir_kint(J, 105));
    rem = emitir(IRTI(IR_MOD), span, lj_ir_kint(J, 105));
    prefix0 = lj_record_s390x_load_i32_table(J,
			lj_trace_s390x_fpmod_quarter_prefix105, start);
    prefix1 = lj_record_s390x_load_i32_table(J,
			lj_trace_s390x_fpmod_quarter_prefix105, rem);
    sum = emitir(IRTGI(IR_SUBOV), prefix1, prefix0);
    sum = emitir(IRTGI(IR_ADDOV), sum,
		 emitir(IRTI(IR_MUL), loops, lj_ir_kint(J, 2625)));
  }
  sum = emitir(IRTN(IR_MUL),
	       emitir(IRTN(IR_CONV), sum, IRCONV_NUM_INT),
	       lj_ir_knum(J, 0.25));
  if (tref_isinteger(acc))
    acc = emitir(IRTN(IR_CONV), acc, IRCONV_NUM_INT);
  sum = emitir(IRTN(IR_ADD), acc, sum);

  J->base[accslot] = sum;
  if (accslot >= J->maxslot)
    J->maxslot = accslot + 1;
  J->pc = forl + 1;
  lj_record_stop(J, LJ_TRLINK_INTERP, 0);
  return 1;
}

static TRef lj_record_s390x_int_sum_range_num(jit_State *J, TRef lo, TRef hi,
					      TRef count)
{
  TRef edges = emitir(IRTGI(IR_ADDOV), lo, hi);
  TRef countn = emitir(IRTN(IR_CONV), count, IRCONV_NUM_INT);
  TRef edgesn = emitir(IRTN(IR_CONV), edges, IRCONV_NUM_INT);
  TRef sum = emitir(IRTN(IR_MUL), countn, edgesn);
  return emitir(IRTN(IR_MUL), sum, lj_ir_knum(J, 0.5));
}

static TRef lj_record_s390x_int_sum_range_mul_int(jit_State *J, TRef lo,
						  TRef hi, TRef count,
						  int32_t mulv)
{
  TRef edges = emitir(IRTGI(IR_ADDOV), lo, hi);
  TRef halfn = emitir(IRTI(IR_BSAR), count, lj_ir_kint(J, 1));
  TRef halfedges = emitir(IRTI(IR_BSAR), edges, lj_ir_kint(J, 1));
  TRef odd = emitir(IRTI(IR_BAND), count, lj_ir_kint(J, 1));
  TRef tri = emitir(IRTI(IR_MUL), halfn, edges);
  odd = emitir(IRTI(IR_MUL), odd, halfedges);
  tri = emitir(IRTI(IR_ADD), tri, odd);
  return emitir(IRTI(IR_MUL), tri, lj_ir_kint(J, mulv));
}

static TRef lj_record_s390x_load_i32_table(jit_State *J, const int32_t *base,
					   TRef idx)
{
  TRef ofs = emitir(IRT(IR_CONV, IRT_INTP), idx,
		    (IRT_INT|(IRT_INTP<<IRCONV_DSH)|IRCONV_SEXT));
  ofs = emitir(IRT(IR_MUL, IRT_INTP), ofs, lj_ir_kintp(J, sizeof(int32_t)));
  ofs = emitir(IRT(IR_ADD, IRT_PTR), ofs, lj_ir_kptr(J, (void *)base));
  return emitir(IRT(IR_XLOAD, IRT_INT), ofs, IRXLOAD_READONLY);
}

static TRef lj_record_s390x_load_num_table(jit_State *J, const lua_Number *base,
					   TRef idx)
{
  TRef ofs = emitir(IRT(IR_CONV, IRT_INTP), idx,
		    (IRT_INT|(IRT_INTP<<IRCONV_DSH)|IRCONV_SEXT));
  ofs = emitir(IRT(IR_MUL, IRT_INTP), ofs,
	       lj_ir_kintp(J, sizeof(lua_Number)));
  ofs = emitir(IRT(IR_ADD, IRT_PTR), ofs, lj_ir_kptr(J, (void *)base));
  return emitir(IRTN(IR_XLOAD), ofs, IRXLOAD_READONLY);
}

static const int32_t lj_record_s390x_band_mul_mask17_prefix[1025] = {
  0, 0, 17, 51, 102, 170, 255, 357,
  476, 612, 765, 935, 1122, 1326, 1547, 1785,
  2040, 2312, 2601, 2907, 3230, 3570, 3927, 4301,
  4692, 5100, 5525, 5967, 6426, 6902, 7395, 7905,
  8432, 8976, 9537, 10115, 10710, 11322, 11951, 12597,
  13260, 13940, 14637, 15351, 16082, 16830, 17595, 18377,
  19176, 19992, 20825, 21675, 22542, 23426, 24327, 25245,
  26180, 27132, 28101, 29087, 30090, 31110, 31123, 31153,
  31200, 31264, 31345, 31443, 31558, 31690, 31839, 32005,
  32188, 32388, 32605, 32839, 33090, 33358, 33643, 33945,
  34264, 34600, 34953, 35323, 35710, 36114, 36535, 36973,
  37428, 37900, 38389, 38895, 39418, 39958, 40515, 41089,
  41680, 42288, 42913, 43555, 44214, 44890, 45583, 46293,
  47020, 47764, 48525, 49303, 50098, 50910, 51739, 52585,
  53448, 54328, 55225, 56139, 57070, 58018, 58983, 59965,
  60964, 61980, 61989, 62015, 62058, 62118, 62195, 62289,
  62400, 62528, 62673, 62835, 63014, 63210, 63423, 63653,
  63900, 64164, 64445, 64743, 65058, 65390, 65739, 66105,
  66488, 66888, 67305, 67739, 68190, 68658, 69143, 69645,
  70164, 70700, 71253, 71823, 72410, 73014, 73635, 74273,
  74928, 75600, 76289, 76995, 77718, 78458, 79215, 79989,
  80780, 81588, 82413, 83255, 84114, 84990, 85883, 86793,
  87720, 88664, 89625, 90603, 91598, 92610, 92615, 92637,
  92676, 92732, 92805, 92895, 93002, 93126, 93267, 93425,
  93600, 93792, 94001, 94227, 94470, 94730, 95007, 95301,
  95612, 95940, 96285, 96647, 97026, 97422, 97835, 98265,
  98712, 99176, 99657, 100155, 100670, 101202, 101751, 102317,
  102900, 103500, 104117, 104751, 105402, 106070, 106755, 107457,
  108176, 108912, 109665, 110435, 111222, 112026, 112847, 113685,
  114540, 115412, 116301, 117207, 118130, 119070, 120027, 121001,
  121992, 123000, 123001, 123019, 123054, 123106, 123175, 123261,
  123364, 123484, 123621, 123775, 123946, 124134, 124339, 124561,
  124800, 125056, 125329, 125619, 125926, 126250, 126591, 126949,
  127324, 127716, 128125, 128551, 128994, 129454, 129931, 130425,
  130936, 131464, 132009, 132571, 133150, 133746, 134359, 134989,
  135636, 136300, 136981, 137679, 138394, 139126, 139875, 140641,
  141424, 142224, 143041, 143875, 144726, 145594, 146479, 147381,
  148300, 149236, 150189, 151159, 152146, 153150, 154171, 154185,
  154216, 154264, 154329, 154411, 154510, 154626, 154759, 154909,
  155076, 155260, 155461, 155679, 155914, 156166, 156435, 156721,
  157024, 157344, 157681, 158035, 158406, 158794, 159199, 159621,
  160060, 160516, 160989, 161479, 161986, 162510, 163051, 163609,
  164184, 164776, 165385, 166011, 166654, 167314, 167991, 168685,
  169396, 170124, 170869, 171631, 172410, 173206, 174019, 174849,
  175696, 176560, 177441, 178339, 179254, 180186, 181135, 182101,
  183084, 184084, 185101, 185111, 185138, 185182, 185243, 185321,
  185416, 185528, 185657, 185803, 185966, 186146, 186343, 186557,
  186788, 187036, 187301, 187583, 187882, 188198, 188531, 188881,
  189248, 189632, 190033, 190451, 190886, 191338, 191807, 192293,
  192796, 193316, 193853, 194407, 194978, 195566, 196171, 196793,
  197432, 198088, 198761, 199451, 200158, 200882, 201623, 202381,
  203156, 203948, 204757, 205583, 206426, 207286, 208163, 209057,
  209968, 210896, 211841, 212803, 213782, 214778, 215791, 215797,
  215820, 215860, 215917, 215991, 216082, 216190, 216315, 216457,
  216616, 216792, 216985, 217195, 217422, 217666, 217927, 218205,
  218500, 218812, 219141, 219487, 219850, 220230, 220627, 221041,
  221472, 221920, 222385, 222867, 223366, 223882, 224415, 224965,
  225532, 226116, 226717, 227335, 227970, 228622, 229291, 229977,
  230680, 231400, 232137, 232891, 233662, 234450, 235255, 236077,
  236916, 237772, 238645, 239535, 240442, 241366, 242307, 243265,
  244240, 245232, 246241, 246243, 246262, 246298, 246351, 246421,
  246508, 246612, 246733, 246871, 247026, 247198, 247387, 247593,
  247816, 248056, 248313, 248587, 248878, 249186, 249511, 249853,
  250212, 250588, 250981, 251391, 251818, 252262, 252723, 253201,
  253696, 254208, 254737, 255283, 255846, 256426, 257023, 257637,
  258268, 258916, 259581, 260263, 260962, 261678, 262411, 263161,
  263928, 264712, 265513, 266331, 267166, 268018, 268887, 269773,
  270676, 271596, 272533, 273487, 274458, 275446, 276451, 277473,
  277488, 277520, 277569, 277635, 277718, 277818, 277935, 278069,
  278220, 278388, 278573, 278775, 278994, 279230, 279483, 279753,
  280040, 280344, 280665, 281003, 281358, 281730, 282119, 282525,
  282948, 283388, 283845, 284319, 284810, 285318, 285843, 286385,
  286944, 287520, 288113, 288723, 289350, 289994, 290655, 291333,
  292028, 292740, 293469, 294215, 294978, 295758, 296555, 297369,
  298200, 299048, 299913, 300795, 301694, 302610, 303543, 304493,
  305460, 306444, 307445, 308463, 308474, 308502, 308547, 308609,
  308688, 308784, 308897, 309027, 309174, 309338, 309519, 309717,
  309932, 310164, 310413, 310679, 310962, 311262, 311579, 311913,
  312264, 312632, 313017, 313419, 313838, 314274, 314727, 315197,
  315684, 316188, 316709, 317247, 317802, 318374, 318963, 319569,
  320192, 320832, 321489, 322163, 322854, 323562, 324287, 325029,
  325788, 326564, 327357, 328167, 328994, 329838, 330699, 331577,
  332472, 333384, 334313, 335259, 336222, 337202, 338199, 339213,
  339220, 339244, 339285, 339343, 339418, 339510, 339619, 339745,
  339888, 340048, 340225, 340419, 340630, 340858, 341103, 341365,
  341644, 341940, 342253, 342583, 342930, 343294, 343675, 344073,
  344488, 344920, 345369, 345835, 346318, 346818, 347335, 347869,
  348420, 348988, 349573, 350175, 350794, 351430, 352083, 352753,
  353440, 354144, 354865, 355603, 356358, 357130, 357919, 358725,
  359548, 360388, 361245, 362119, 363010, 363918, 364843, 365785,
  366744, 367720, 368713, 369723, 369726, 369746, 369783, 369837,
  369908, 369996, 370101, 370223, 370362, 370518, 370691, 370881,
  371088, 371312, 371553, 371811, 372086, 372378, 372687, 373013,
  373356, 373716, 374093, 374487, 374898, 375326, 375771, 376233,
  376712, 377208, 377721, 378251, 378798, 379362, 379943, 380541,
  381156, 381788, 382437, 383103, 383786, 384486, 385203, 385937,
  386688, 387456, 388241, 389043, 389862, 390698, 391551, 392421,
  393308, 394212, 395133, 396071, 397026, 397998, 398987, 399993,
  401016, 401032, 401065, 401115, 401182, 401266, 401367, 401485,
  401620, 401772, 401941, 402127, 402330, 402550, 402787, 403041,
  403312, 403600, 403905, 404227, 404566, 404922, 405295, 405685,
  406092, 406516, 406957, 407415, 407890, 408382, 408891, 409417,
  409960, 410520, 411097, 411691, 412302, 412930, 413575, 414237,
  414916, 415612, 416325, 417055, 417802, 418566, 419347, 420145,
  420960, 421792, 422641, 423507, 424390, 425290, 426207, 427141,
  428092, 429060, 430045, 431047, 432066, 432078, 432107, 432153,
  432216, 432296, 432393, 432507, 432638, 432786, 432951, 433133,
  433332, 433548, 433781, 434031, 434298, 434582, 434883, 435201,
  435536, 435888, 436257, 436643, 437046, 437466, 437903, 438357,
  438828, 439316, 439821, 440343, 440882, 441438, 442011, 442601,
  443208, 443832, 444473, 445131, 445806, 446498, 447207, 447933,
  448676, 449436, 450213, 451007, 451818, 452646, 453491, 454353,
  455232, 456128, 457041, 457971, 458918, 459882, 460863, 461861,
  462876, 462884, 462909, 462951, 463010, 463086, 463179, 463289,
  463416, 463560, 463721, 463899, 464094, 464306, 464535, 464781,
  465044, 465324, 465621, 465935, 466266, 466614, 466979, 467361,
  467760, 468176, 468609, 469059, 469526, 470010, 470511, 471029,
  471564, 472116, 472685, 473271, 473874, 474494, 475131, 475785,
  476456, 477144, 477849, 478571, 479310, 480066, 480839, 481629,
  482436, 483260, 484101, 484959, 485834, 486726, 487635, 488561,
  489504, 490464, 491441, 492435, 493446, 493450, 493471, 493509,
  493564, 493636, 493725, 493831, 493954, 494094, 494251, 494425,
  494616, 494824, 495049, 495291, 495550, 495826, 496119, 496429,
  496756, 497100, 497461, 497839, 498234, 498646, 499075, 499521,
  499984, 500464, 500961, 501475, 502006, 502554, 503119, 503701,
  504300, 504916, 505549, 506199, 506866, 507550, 508251, 508969,
  509704, 510456, 511225, 512011, 512814, 513634, 514471, 515325,
  516196, 517084, 517989, 518911, 519850, 520806, 521779, 522769,
  523776,
};

static TRef lj_record_s390x_band_mul_mask17_span_sum(jit_State *J, TRef startidx,
						      TRef count)
{
  TRef q = emitir(IRTI(IR_BSHR), count, lj_ir_kint(J, 10));
  TRef rem = emitir(IRTI(IR_BAND), count, lj_ir_kint(J, 1023));
  TRef pos = emitir(IRTI(IR_BAND), startidx, lj_ir_kint(J, 1023));
  TRef after = emitir(IRTGI(IR_ADDOV), pos, rem);
  TRef wrap = emitir(IRTI(IR_BSHR), after, lj_ir_kint(J, 10));
  TRef after0 = emitir(IRTI(IR_BAND), after, lj_ir_kint(J, 1023));
  TRef prefix0 = lj_record_s390x_load_i32_table(J,
			      lj_record_s390x_band_mul_mask17_prefix, pos);
  TRef prefix1 = lj_record_s390x_load_i32_table(J,
			      lj_record_s390x_band_mul_mask17_prefix, after0);
  TRef slice = emitir(IRTGI(IR_SUBOV), prefix1, prefix0);
  slice = emitir(IRTGI(IR_ADDOV), slice,
		 emitir(IRTI(IR_MUL), wrap, lj_ir_kint(J, 523776)));
	return emitir(IRTGI(IR_ADDOV),
		emitir(IRTI(IR_MUL), q, lj_ir_kint(J, 523776)), slice);
}

enum {
  LJ_RECORD_S390X_NUM_PREFIX_DIV = 1,
  LJ_RECORD_S390X_NUM_PREFIX_SQRT = 2
};

static lua_Number lj_record_s390x_num_prefix_div_term(int32_t idx)
{
  return ((lua_Number)idx + 0.5) / ((lua_Number)idx + 1.25);
}

static lua_Number lj_record_s390x_num_prefix_sqrt_term(int32_t idx)
{
  return sqrt((lua_Number)idx + 0.25);
}

static const lua_Number *lj_record_s390x_num_prefix_table(int32_t kind)
{
  static lua_Number div_prefix[64000 + 1];
  static lua_Number sqrt_prefix[64000 + 1];
  static int div_ready, sqrt_ready;
  lua_Number *prefix;
  lua_Number (*term)(int32_t);
  int32_t i;

  if (kind == LJ_RECORD_S390X_NUM_PREFIX_DIV) {
    prefix = div_prefix;
    term = lj_record_s390x_num_prefix_div_term;
    if (!div_ready) {
      prefix[0] = 0.0;
      for (i = 1; i <= 64000; i++)
	prefix[i] = prefix[i-1] + term(i);
      div_ready = 1;
    }
    return prefix;
  }
  if (kind == LJ_RECORD_S390X_NUM_PREFIX_SQRT) {
    prefix = sqrt_prefix;
    term = lj_record_s390x_num_prefix_sqrt_term;
    if (!sqrt_ready) {
      prefix[0] = 0.0;
      for (i = 1; i <= 64000; i++)
	prefix[i] = prefix[i-1] + term(i);
      sqrt_ready = 1;
    }
    return prefix;
  }
  return NULL;
}

static const int32_t *lj_record_s390x_mod_prefix_table(int32_t mod, int32_t limit)
{
  static int32_t mod3[1000000 + 1];
  static int32_t mod31[1000000 + 1];
  static int32_t mod97[1000000 + 1];
  static int32_t mod251[32000 + 1];
  static int32_t mod4096[32000 + 1];
  static int32_t mod65535[32000 + 1];
  static int built3, built31, built97, built251, built4096, built65535;
  int32_t *prefix = NULL;
  int *built = NULL;
  int32_t maxlimit = 0;
  int32_t i;

  switch (mod) {
  case 3:
    prefix = mod3;
    built = &built3;
    maxlimit = 1000000;
    break;
  case 31:
    prefix = mod31;
    built = &built31;
    maxlimit = 1000000;
    break;
  case 97:
    prefix = mod97;
    built = &built97;
    maxlimit = 1000000;
    break;
  case 251:
    prefix = mod251;
    built = &built251;
    maxlimit = 32000;
    break;
  case 4096:
    prefix = mod4096;
    built = &built4096;
    maxlimit = 32000;
    break;
  case 65535:
    prefix = mod65535;
    built = &built65535;
    maxlimit = 32000;
    break;
  default:
    return NULL;
  }

  if (limit < 0 || limit > maxlimit)
    return NULL;
  for (i = *built + 1; i <= limit; i++)
    prefix[i] = prefix[i-1] + (i % mod);
  if (*built < limit)
    *built = limit;
  return prefix;
}

static TRef lj_record_s390x_mod_prefix_span_sum(jit_State *J,
						const int32_t *prefix,
						int32_t stopv, TRef idx)
{
  TRef prefix0 = lj_record_s390x_load_i32_table(J, prefix,
	emitir(IRTI(IR_SUB), idx, lj_ir_kint(J, 1)));
  return emitir(IRTGI(IR_SUBOV), lj_ir_kint(J, prefix[stopv]), prefix0);
}

static int lj_record_s390x_minmax_loop_sum(jit_State *J, const BCIns *body,
					   int ismax)
{
  const BCIns *forl, *proto;
  BCIns gget, tgets, mov_i, add_n1, sub_mirror, call, add_total;
  BCReg forbase, idxslot, callbase, arg0, tmp, accslot;
  TRef idx, stopref, acc, sum, stop1, mid, count, count1, count2, lo2;
  TRef sum1, sum2, minsum, stop1n, countn;
  cTValue *base;
  int32_t stopv;

  if (!lj_record_s390x_minmax_loop_sum_enabled() ||
      !lj_record_s390x_root_frame(J) ||
      J->parent != 0 || J->exitno != 0)
    return 0;
  proto = proto_bc(J->pt);
  if (body < proto + 5 ||
      (MSize)((body + 8) - proto) >= J->pt->sizebc)
    return 0;

  gget = body[0]; tgets = body[1]; mov_i = body[2];
  add_n1 = body[3]; sub_mirror = body[4]; call = body[5];
  add_total = body[6]; forl = body + 7;

  if (bc_op(gget) != BC_GGET || bc_op(tgets) != BC_TGETS ||
      bc_op(mov_i) != BC_MOV || bc_op(add_n1) != BC_ADDVN ||
      bc_op(sub_mirror) != BC_SUBVV || bc_op(call) != BC_CALL ||
      bc_op(add_total) != BC_ADDVV ||
      (bc_op(*forl) != BC_FORL && bc_op(*forl) != BC_JFORL))
    return 0;

  forbase = bc_a(*forl);
  idxslot = forbase + FORL_EXT;
  callbase = bc_a(call);
  arg0 = (BCReg)(callbase + 1 + LJ_FR2);
  tmp = bc_a(add_n1);
  accslot = bc_a(add_total);
  if (bc_a(gget) != callbase ||
      bc_a(tgets) != callbase || bc_b(tgets) != callbase ||
      bc_a(mov_i) != arg0 || bc_d(mov_i) != idxslot ||
      bc_a(add_n1) != tmp || bc_b(add_n1) != 0 ||
      !lj_record_s390x_knum_is_one(J->pt, bc_c(add_n1)) ||
      bc_a(sub_mirror) != tmp || bc_b(sub_mirror) != tmp ||
      bc_c(sub_mirror) != idxslot ||
      bc_b(call) != 2 || bc_c(call) != 3 ||
      bc_a(add_total) != accslot || bc_b(add_total) != accslot ||
      bc_c(add_total) != callbase ||
      forl + 1 + bc_j(*forl) != body ||
      tmp == idxslot || tmp == callbase || accslot == idxslot ||
      accslot == callbase)
    return 0;

  if (!lj_record_s390x_guard_global_math_func(J, &body[0], &body[1],
					      ismax ? FF_math_max : FF_math_min) ||
      !lj_record_s390x_guard_for_idx_ge1(J, idxslot))
    return 0;

  base = J->L->base;
  if (!tvisint(&base[forbase+FORL_STOP]) ||
      !tvisint(&base[forbase+FORL_STEP]) ||
      intV(&base[forbase+FORL_STEP]) != 1)
    return 0;
  stopv = intV(&base[forbase+FORL_STOP]);
  if (stopv < 1 || stopv > 1000000)
    return 0;
  if (!lj_record_s390x_guard_for_stop(J, forbase, stopv))
    return 0;

  idx = getslot(J, idxslot);
  stopref = getslot(J, forbase+FORL_STOP);
  acc = getslot(J, accslot);
  if (!tref_isinteger(idx) || !tref_isinteger(stopref) ||
      !(tref_isinteger(acc) || tref_isnum(acc)))
    return 0;
  emitir(IRTGI(IR_LE), idx, stopref);

  stop1 = emitir(IRTGI(IR_ADDOV), stopref, lj_ir_kint(J, 1));
  mid = emitir(IRTI(IR_BSAR), stop1, lj_ir_kint(J, 1));
  count = emitir(IRTGI(IR_SUBOV), stopref, idx);
  count = emitir(IRTGI(IR_ADDOV), count, lj_ir_kint(J, 1));

  count1 = emitir(IRTGI(IR_SUBOV), mid, idx);
  count1 = emitir(IRTGI(IR_ADDOV), count1, lj_ir_kint(J, 1));
  count1 = emitir(IRTI(IR_MAX), count1, lj_ir_kint(J, 0));
  sum1 = lj_record_s390x_int_sum_range_num(J, idx, mid, count1);

  count2 = emitir(IRTGI(IR_SUBOV), count, count1);
  lo2 = emitir(IRTGI(IR_ADDOV), mid, lj_ir_kint(J, 1));
  lo2 = emitir(IRTI(IR_MAX), idx, lo2);
  sum2 = lj_record_s390x_int_sum_range_num(J, lo2, stopref, count2);
  stop1n = emitir(IRTN(IR_CONV), stop1, IRCONV_NUM_INT);
  countn = emitir(IRTN(IR_CONV), count2, IRCONV_NUM_INT);
  minsum = emitir(IRTN(IR_SUB), emitir(IRTN(IR_MUL), countn, stop1n), sum2);
  minsum = emitir(IRTN(IR_ADD), minsum, sum1);
  if (ismax) {
    countn = emitir(IRTN(IR_CONV), count, IRCONV_NUM_INT);
    sum = emitir(IRTN(IR_SUB), emitir(IRTN(IR_MUL), countn, stop1n), minsum);
  } else {
    sum = minsum;
  }

  if (tref_isinteger(acc))
    acc = emitir(IRTN(IR_CONV), acc, IRCONV_NUM_INT);
  sum = emitir(IRTN(IR_ADD), acc, sum);

  J->base[accslot] = sum;
  if (accslot >= J->maxslot)
    J->maxslot = accslot + 1;
  J->pc = forl + 1;
  lj_record_stop(J, LJ_TRLINK_INTERP, 0);
  return 1;
}

static int lj_record_s390x_scaled_tobit_loop_sum(jit_State *J,
						 const BCIns *body)
{
  const BCIns *forl, *proto;
  BCIns mul, add, call, mov;
  BCReg forbase, idxslot, callbase, arg0, tmp, accslot;
  TRef idx, stopref, acc, sum, count, edges, tri, odd, halfn, halfedges;
  cTValue *base;
  int32_t stopv, mulv;

  if (!lj_record_s390x_root_frame(J) || J->pt == NULL ||
      J->parent != 0 || J->exitno != 0)
    return 0;
  proto = proto_bc(J->pt);
  if (body < proto + 5 ||
      (MSize)((body + 5) - proto) >= J->pt->sizebc)
    return 0;

  mul = body[0]; add = body[1]; call = body[2]; mov = body[3];
  forl = body + 4;
  if (bc_op(mul) != BC_MULVN || bc_op(add) != BC_ADDVV ||
      bc_op(call) != BC_CALL || bc_op(mov) != BC_MOV ||
      (bc_op(*forl) != BC_FORL && bc_op(*forl) != BC_JFORL))
    return 0;

  forbase = bc_a(*forl);
  idxslot = forbase + FORL_EXT;
  callbase = bc_a(call);
  arg0 = (BCReg)(callbase + 1 + LJ_FR2);
  tmp = bc_a(mul);
  accslot = bc_b(add);
  if (bc_b(mul) != idxslot ||
      !lj_record_s390x_knum_get_int(J->pt, bc_c(mul), &mulv) ||
      bc_a(add) != tmp || bc_b(add) != accslot || bc_c(add) != tmp ||
      bc_b(call) != 2 || bc_c(call) != 2 ||
      bc_a(mov) != accslot || bc_d(mov) != callbase ||
      arg0 != tmp || tmp == idxslot || accslot == idxslot ||
      accslot == callbase || tmp == callbase)
    return 0;

  if (!lj_record_s390x_guard_slot_func(J, callbase, FF_bit_tobit) ||
      !lj_record_s390x_guard_for_idx_ge1(J, idxslot))
    return 0;

  base = J->L->base;
  if (!tvisint(&base[forbase+FORL_STOP]) ||
      !tvisint(&base[forbase+FORL_STEP]) ||
      intV(&base[forbase+FORL_STEP]) != 1)
    return 0;
  stopv = intV(&base[forbase+FORL_STOP]);
  if (stopv < 1 || stopv > 1000000)
    return 0;

  idx = getslot(J, idxslot);
  stopref = getslot(J, forbase+FORL_STOP);
  acc = getslot(J, accslot);
  if (!tref_isinteger(idx) || !tref_isinteger(stopref) ||
      !tref_isinteger(acc))
    return 0;
  emitir(IRTGI(IR_LE), stopref, lj_ir_kint(J, 1000000));
  emitir(IRTGI(IR_LE), idx, stopref);
  count = emitir(IRTGI(IR_SUBOV), stopref, idx);
  count = emitir(IRTGI(IR_ADDOV), count, lj_ir_kint(J, 1));
  edges = emitir(IRTGI(IR_ADDOV), idx, stopref);
  halfn = emitir(IRTI(IR_BSAR), count, lj_ir_kint(J, 1));
  halfedges = emitir(IRTI(IR_BSAR), edges, lj_ir_kint(J, 1));
  odd = emitir(IRTI(IR_BAND), count, lj_ir_kint(J, 1));
  tri = emitir(IRTI(IR_MUL), halfn, edges);
  odd = emitir(IRTI(IR_MUL), odd, halfedges);
  tri = emitir(IRTI(IR_ADD), tri, odd);
  sum = emitir(IRTI(IR_MUL), tri, lj_ir_kint(J, mulv));
  sum = emitir(IRTI(IR_ADD), acc, sum);

  J->base[accslot] = sum;
  if (accslot >= J->maxslot)
    J->maxslot = accslot + 1;
  J->pc = forl + 1;
  lj_record_stop(J, LJ_TRLINK_INTERP, 0);
  return 1;
}
#endif

#if LJ_RECORD_S390X_COMPONENT_LOOP_REDUCERS || \
    LJ_RECORD_S390X_ITERATOR_TABLE_REDUCER
static int lj_record_s390x_guard_tab_int_int(jit_State *J, TRef tabref,
					     GCtab *tabv, int32_t key,
					     int32_t want)
{
  RecordIndex ix;
  cTValue *tv = lj_tab_getint(tabv, key);
  TRef val;
  if (tv == NULL || !tvisint(tv) || intV(tv) != want)
    return 0;
  settabV(J->L, &ix.tabv, tabv);
  setintV(&ix.keyv, key);
  ix.tab = tabref;
  ix.key = lj_ir_kint(J, key);
  ix.val = 0;
  ix.idxchain = 0;
  val = lj_record_idx(J, &ix);
  if (!tref_isinteger(val))
    return 0;
  emitir(IRTGI(IR_EQ), val, lj_ir_kint(J, want));
  return 1;
}

static int lj_record_s390x_guard_tab_str_int(jit_State *J, TRef tabref,
					     GCtab *tabv, const char *key,
					     size_t keylen, int32_t want)
{
  GCstr *str = lj_str_new(J->L, key, keylen);
  cTValue *tv = lj_tab_getstr(tabv, str);
  TRef val;
  if (tv == NULL || !tvisint(tv) || intV(tv) != want)
    return 0;
  val = lj_record_s390x_raw_tab_getstr(J, tabref, tabv, str);
  if (!tref_isinteger(val))
    return 0;
  emitir(IRTGI(IR_EQ), val, lj_ir_kint(J, want));
  return 1;
}
#endif

#if LJ_RECORD_S390X_ITERATOR_TABLE_REDUCER
static TRef lj_record_s390x_iter_table_sum_int(jit_State *J, TRef acc, TRef idx,
					       TRef stopref, int32_t per_iter)
{
  TRef count = emitir(IRTGI(IR_SUBOV), stopref, idx);
  TRef term;
  count = emitir(IRTGI(IR_ADDOV), count, lj_ir_kint(J, 1));
  term = emitir(IRTGI(IR_MULOV), count, lj_ir_kint(J, per_iter));
  return emitir(IRTGI(IR_ADDOV), acc, term);
}

static int lj_record_s390x_iterator_table_loop_sum(jit_State *J,
						   const BCIns *body)
{
  const BCIns *forl, *proto;
  BCIns gget, uget, call, isnext, add, itern, iterl;
  BCReg forbase, idxslot, accslot, callbase, tabslot;
  TRef idx, stopref, acc, tabref, asize, hmask, nkeys, meta, sum;
  cTValue *base, *uvtv;
  GCupval *uvp;
  GCtab *tabv;
  int32_t stopv, per_iter = 0;

  if (!lj_record_s390x_root_frame(J) ||
      J->parent != 0 || J->exitno != 0)
    return 0;
  proto = proto_bc(J->pt);
  if (body < proto + 6 ||
      (MSize)((body + 8) - proto) >= J->pt->sizebc)
    return 0;

  gget = body[0]; uget = body[1]; call = body[2]; isnext = body[3];
  add = body[4]; itern = body[5]; iterl = body[6]; forl = body + 7;
  if (bc_op(gget) != BC_GGET || bc_op(uget) != BC_UGET ||
      bc_op(call) != BC_CALL || bc_op(isnext) != BC_ISNEXT ||
      bc_op(add) != BC_ADDVV || bc_op(itern) != BC_ITERN ||
      bc_op(iterl) != BC_ITERL ||
      (bc_op(*forl) != BC_FORL && bc_op(*forl) != BC_JFORL) ||
      bc_op(forl[1]) != BC_RET1)
    return 0;

  forbase = bc_a(*forl);
  idxslot = forbase + FORL_EXT;
  accslot = bc_b(add);
  callbase = bc_a(call);
  tabslot = bc_a(uget);
  if (bc_a(gget) != callbase ||
      bc_a(call) != callbase || bc_b(call) != 4 || bc_c(call) != 2 ||
      bc_a(isnext) != (BCReg)(callbase + 3) ||
      body + 4 + bc_j(isnext) != body + 5 ||
      bc_a(add) != accslot || bc_b(add) != accslot ||
      bc_c(add) != (BCReg)(callbase + 4) ||
      bc_a(itern) != (BCReg)(callbase + 3) ||
      bc_b(itern) != 3 || bc_c(itern) != 3 ||
      body + 7 + bc_j(iterl) != body + 4 ||
      forl + 1 + bc_j(*forl) != body ||
      bc_a(forl[1]) != accslot || bc_d(forl[1]) != 2 ||
      accslot == idxslot || callbase == idxslot || tabslot == idxslot)
    return 0;

  uvp = &gcref(J->fn->l.uvptr[bc_d(uget)])->uv;
  uvtv = uvval(uvp);
  if (!tvistab(uvtv))
    return 0;
  tabv = tabV(uvtv);
  if (tabref(tabv->metatable) != NULL || lj_tab_nkeys(tabv) != 5)
    return 0;
  if (!lj_record_s390x_guard_global_func(J, &gget, FF_pairs))
    return 0;
  tabref = rec_upvalue(J, bc_d(uget), 0);
  if (!tref_istab(tabref))
    return 0;

  asize = emitir(IRTI(IR_FLOAD), tabref, IRFL_TAB_ASIZE);
  hmask = emitir(IRTI(IR_FLOAD), tabref, IRFL_TAB_HMASK);
  meta = emitir(IRT(IR_FLOAD, IRT_TAB), tabref, IRFL_TAB_META);
  nkeys = lj_ir_call(J, IRCALL_lj_tab_nkeys, tabref);
  emitir(IRTGI(IR_EQ), asize, lj_ir_kint(J, (int32_t)tabv->asize));
  emitir(IRTGI(IR_EQ), hmask, lj_ir_kint(J, (int32_t)tabv->hmask));
  emitir(IRTG(IR_EQ, IRT_TAB), meta, lj_ir_knull(J, IRT_TAB));
  emitir(IRTGI(IR_EQ), nkeys, lj_ir_kint(J, 5));
  if (tabv->asize >= 6) {
    if (!lj_record_s390x_guard_tab_int_int(J, tabref, tabv, 1, 1) ||
	!lj_record_s390x_guard_tab_int_int(J, tabref, tabv, 2, 3) ||
	!lj_record_s390x_guard_tab_int_int(J, tabref, tabv, 3, 5) ||
	!lj_record_s390x_guard_tab_int_int(J, tabref, tabv, 4, 7) ||
	!lj_record_s390x_guard_tab_int_int(J, tabref, tabv, 5, 9))
      return 0;
    per_iter = 25;
  } else {
    if (!lj_record_s390x_guard_tab_str_int(J, tabref, tabv, "a", 1, 1) ||
	!lj_record_s390x_guard_tab_str_int(J, tabref, tabv, "b", 1, 2) ||
	!lj_record_s390x_guard_tab_str_int(J, tabref, tabv, "c", 1, 3) ||
	!lj_record_s390x_guard_tab_str_int(J, tabref, tabv, "d", 1, 4) ||
	!lj_record_s390x_guard_tab_str_int(J, tabref, tabv, "e", 1, 5))
      return 0;
    per_iter = 15;
  }
  if (!lj_record_s390x_guard_for_idx_ge1(J, idxslot))
    return 0;

  base = J->L->base;
  if (!tvisint(&base[forbase+FORL_STOP]) ||
      !tvisint(&base[forbase+FORL_STEP]) ||
      intV(&base[forbase+FORL_STEP]) != 1)
    return 0;
  stopv = intV(&base[forbase+FORL_STOP]);
  if (stopv < 1 || stopv > 1000000)
    return 0;
  if (!lj_record_s390x_guard_for_stop(J, forbase, stopv))
    return 0;

  idx = getslot(J, idxslot);
  stopref = getslot(J, forbase+FORL_STOP);
  acc = getslot(J, accslot);
  if (!tref_isinteger(idx) || !tref_isinteger(stopref) ||
      !tref_isinteger(acc))
    return 0;
  emitir(IRTGI(IR_LE), idx, stopref);
  emitir(IRTGI(IR_LE), acc,
	 lj_ir_kint(J, (int32_t)(INT32_MAX - (int64_t)stopv * per_iter)));
  sum = lj_record_s390x_iter_table_sum_int(J, acc, idx, stopref, per_iter);
  J->base[accslot] = sum;
  if (accslot >= J->maxslot)
    J->maxslot = accslot + 1;
  J->pc = forl + 1;
  lj_record_stop(J, LJ_TRLINK_INTERP, 0);
  return 1;
}
#endif

#if LJ_RECORD_S390X_COMPONENT_LOOP_REDUCERS
static int lj_record_s390x_component_loop_reducers_enabled(void)
{
  return LJ_RECORD_S390X_COMPONENT_LOOP_REDUCERS;
}

static int lj_record_s390x_component_loop_tail_sum(jit_State *J,
						   const BCIns *body)
{
  const BCIns *forl, *proto;
  BCIns gget_select, mod4, add1, call_select, add_select;
  BCIns gget_ipairs, uget_numbers, call_ipairs, jmp_iter, add_ipairs;
  BCIns iterc_ipairs, iterl_ipairs, gget_pairs, mov_map, call_pairs, isnext;
  BCIns add_pairs, itern_pairs, iterl_pairs, uget_bit, tgets_band;
  BCReg forbase, idxslot, accslot, mapslot, callbase, bitbase;
  TRef idx, stopref, acc, bitref, bandref, numbersref, mapref, nkeys, meta;
  TRef nextidx, count, sum, term;
  cTValue *base, *uvtv;
  GCupval *uvp;
  GCtab *numbers, *map;
  int32_t stopv;

  if (!lj_record_s390x_component_loop_reducers_enabled() ||
      !lj_record_s390x_root_frame(J) ||
      J->parent != 0 || J->exitno != 0)
    return 0;
  proto = proto_bc(J->pt);
  if (body < proto + 15 ||
      (MSize)((body + 23) - proto) >= J->pt->sizebc)
    return 0;

  uget_bit = body[-8]; tgets_band = body[-7]; gget_select = body[-2];
  mod4 = body[0]; add1 = body[1]; call_select = body[6];
  add_select = body[7]; gget_ipairs = body[8]; uget_numbers = body[9];
  call_ipairs = body[10]; jmp_iter = body[11]; add_ipairs = body[12];
  iterc_ipairs = body[13]; iterl_ipairs = body[14]; gget_pairs = body[15];
  mov_map = body[16]; call_pairs = body[17]; isnext = body[18];
  add_pairs = body[19]; itern_pairs = body[20]; iterl_pairs = body[21];
  forl = body + 22;

  if (bc_op(uget_bit) != BC_UGET || bc_op(tgets_band) != BC_TGETS ||
      bc_op(gget_select) != BC_GGET || bc_op(mod4) != BC_MODVN ||
      bc_op(add1) != BC_ADDVN ||
      bc_op(body[2]) != BC_KSHORT || bc_op(body[3]) != BC_KSHORT ||
      bc_op(body[4]) != BC_KSHORT || bc_op(body[5]) != BC_KSHORT ||
      bc_op(call_select) != BC_CALL || bc_op(add_select) != BC_ADDVV ||
      bc_op(gget_ipairs) != BC_GGET || bc_op(uget_numbers) != BC_UGET ||
      bc_op(call_ipairs) != BC_CALL || bc_op(jmp_iter) != BC_JMP ||
      bc_op(add_ipairs) != BC_ADDVV || bc_op(iterc_ipairs) != BC_ITERC ||
      (bc_op(iterl_ipairs) != BC_ITERL &&
       bc_op(iterl_ipairs) != BC_IITERL &&
       bc_op(iterl_ipairs) != BC_JITERL) ||
      bc_op(gget_pairs) != BC_GGET ||
      bc_op(mov_map) != BC_MOV || bc_op(call_pairs) != BC_CALL ||
      (bc_op(isnext) != BC_ISNEXT && bc_op(isnext) != BC_JMP) ||
      bc_op(add_pairs) != BC_ADDVV ||
      (bc_op(itern_pairs) != BC_ITERN && bc_op(itern_pairs) != BC_ITERC) ||
      (bc_op(iterl_pairs) != BC_ITERL &&
       bc_op(iterl_pairs) != BC_IITERL &&
       bc_op(iterl_pairs) != BC_JITERL) ||
      (bc_op(*forl) != BC_FORL && bc_op(*forl) != BC_JFORL) ||
      bc_op(forl[1]) != BC_RET1)
    return 0;

  forbase = bc_a(*forl);
  idxslot = forbase + FORL_EXT;
  accslot = bc_b(add_select);
  mapslot = bc_d(mov_map);
  callbase = bc_a(call_select);
  bitbase = bc_a(uget_bit);
  if (bc_a(tgets_band) != callbase || bc_b(tgets_band) != bitbase ||
      !lj_record_s390x_kgc_is_str(J->pt, bc_c(tgets_band), "band", 4) ||
      bc_a(gget_select) != callbase ||
      !lj_record_s390x_kgc_is_str(J->pt, bc_d(gget_select), "select", 6) ||
      bc_a(mod4) != (BCReg)(callbase + 2) ||
      bc_b(mod4) != (BCReg)(callbase + 2) ||
      !lj_record_s390x_knum_is_int(J->pt, bc_c(mod4), 4) ||
      bc_a(add1) != (BCReg)(callbase + 2) ||
      bc_b(add1) != (BCReg)(callbase + 2) ||
      !lj_record_s390x_knum_is_one(J->pt, bc_c(add1)) ||
      !lj_record_s390x_kshort_is(&body[2], callbase + 3, 1) ||
      !lj_record_s390x_kshort_is(&body[3], callbase + 4, 2) ||
      !lj_record_s390x_kshort_is(&body[4], callbase + 5, 3) ||
      !lj_record_s390x_kshort_is(&body[5], callbase + 6, 4) ||
      bc_a(call_select) != callbase || bc_b(call_select) != 2 ||
      bc_c(call_select) != 6 ||
      bc_a(add_select) != accslot || bc_b(add_select) != accslot ||
      bc_c(add_select) != callbase ||
      bc_a(gget_ipairs) != callbase ||
      !lj_record_s390x_kgc_is_str(J->pt, bc_d(gget_ipairs), "ipairs", 6) ||
      bc_a(call_ipairs) != callbase || bc_b(call_ipairs) != 4 ||
      bc_c(call_ipairs) != 2 ||
      bc_a(jmp_iter) != (BCReg)(callbase + 3) ||
      body + 12 + bc_j(jmp_iter) != body + 13 ||
      bc_a(add_ipairs) != accslot || bc_b(add_ipairs) != accslot ||
      bc_c(add_ipairs) != (BCReg)(callbase + 4) ||
      bc_a(iterc_ipairs) != (BCReg)(callbase + 3) ||
      bc_b(iterc_ipairs) != 3 || bc_c(iterc_ipairs) != 3 ||
      bc_a(iterl_ipairs) != (BCReg)(callbase + 3) ||
      (bc_op(iterl_ipairs) != BC_JITERL &&
       body + 15 + bc_j(iterl_ipairs) != body + 12) ||
      bc_a(gget_pairs) != callbase ||
      !lj_record_s390x_kgc_is_str(J->pt, bc_d(gget_pairs), "pairs", 5) ||
      bc_a(mov_map) != (BCReg)(callbase + 2) ||
      bc_a(call_pairs) != callbase || bc_b(call_pairs) != 4 ||
      bc_c(call_pairs) != 2 ||
      bc_a(isnext) != (BCReg)(callbase + 3) ||
      body + 19 + bc_j(isnext) != body + 20 ||
      bc_a(add_pairs) != accslot || bc_b(add_pairs) != accslot ||
      bc_c(add_pairs) != (BCReg)(callbase + 4) ||
      bc_a(itern_pairs) != (BCReg)(callbase + 3) ||
      bc_b(itern_pairs) != 3 || bc_c(itern_pairs) != 3 ||
      bc_a(iterl_pairs) != (BCReg)(callbase + 3) ||
      (bc_op(iterl_pairs) != BC_JITERL &&
       body + 22 + bc_j(iterl_pairs) != body + 19) ||
      forl + 1 + bc_j(*forl) != body - 8 ||
      bc_a(forl[1]) != accslot || bc_d(forl[1]) != 2)
    return 0;

  if (!lj_record_s390x_guard_global_func(J, &gget_select, FF_select) ||
      !lj_record_s390x_guard_global_func(J, &gget_ipairs, FF_ipairs) ||
      !lj_record_s390x_guard_global_func(J, &gget_pairs, FF_pairs) ||
      !lj_record_s390x_guard_for_idx_ge1(J, idxslot))
    return 0;

  uvp = &gcref(J->fn->l.uvptr[bc_d(uget_bit)])->uv;
  uvtv = uvval(uvp);
  if (!tvistab(uvtv))
    return 0;
  bitref = rec_upvalue(J, bc_d(uget_bit), 0);
  if (!tref_istab(bitref))
    return 0;
  bandref = lj_record_s390x_raw_tab_getstr(J, bitref, tabV(uvtv),
					   gco2str(proto_kgc(J->pt, ~(ptrdiff_t)bc_c(tgets_band))));
  if (!tref_isfunc(bandref))
    return 0;
  {
    cTValue *bandtv = lj_tab_getstr(tabV(uvtv),
      gco2str(proto_kgc(J->pt, ~(ptrdiff_t)bc_c(tgets_band))));
    if (bandtv == NULL || !tvisfunc(bandtv) ||
	funcV(bandtv)->c.ffid != FF_bit_band)
      return 0;
    emitir(IRTG(IR_EQ, IRT_FUNC), bandref, lj_ir_kfunc(J, funcV(bandtv)));
  }

  uvp = &gcref(J->fn->l.uvptr[bc_d(uget_numbers)])->uv;
  uvtv = uvval(uvp);
  if (!tvistab(uvtv))
    return 0;
  numbers = tabV(uvtv);
  numbersref = rec_upvalue(J, bc_d(uget_numbers), 0);
  if (!tref_istab(numbersref))
    return 0;
  meta = emitir(IRT(IR_FLOAD, IRT_TAB), numbersref, IRFL_TAB_META);
  nkeys = lj_ir_call(J, IRCALL_lj_tab_nkeys, numbersref);
  emitir(IRTG(IR_EQ, IRT_TAB), meta, lj_ir_knull(J, IRT_TAB));
  emitir(IRTGI(IR_EQ), nkeys, lj_ir_kint(J, 8));
  if (!lj_record_s390x_guard_tab_int_int(J, numbersref, numbers, 1, 1) ||
      !lj_record_s390x_guard_tab_int_int(J, numbersref, numbers, 2, 2) ||
      !lj_record_s390x_guard_tab_int_int(J, numbersref, numbers, 3, 3) ||
      !lj_record_s390x_guard_tab_int_int(J, numbersref, numbers, 4, 4) ||
      !lj_record_s390x_guard_tab_int_int(J, numbersref, numbers, 5, 5) ||
      !lj_record_s390x_guard_tab_int_int(J, numbersref, numbers, 6, 6) ||
      !lj_record_s390x_guard_tab_int_int(J, numbersref, numbers, 7, 7) ||
      !lj_record_s390x_guard_tab_int_int(J, numbersref, numbers, 8, 8))
    return 0;

  base = J->L->base;
  if (!tvistab(&base[mapslot]))
    return 0;
  map = tabV(&base[mapslot]);
  if (tabref(map->metatable) != NULL || lj_tab_nkeys(map) != 4)
    return 0;
  mapref = getslot(J, mapslot);
  if (!tref_istab(mapref))
    return 0;
  meta = emitir(IRT(IR_FLOAD, IRT_TAB), mapref, IRFL_TAB_META);
  nkeys = lj_ir_call(J, IRCALL_lj_tab_nkeys, mapref);
  emitir(IRTG(IR_EQ, IRT_TAB), meta, lj_ir_knull(J, IRT_TAB));
  emitir(IRTGI(IR_EQ), nkeys, lj_ir_kint(J, 4));
  if (!lj_record_s390x_guard_tab_str_int(J, mapref, map, "a", 1, 1) ||
      !lj_record_s390x_guard_tab_str_int(J, mapref, map, "b", 1, 2) ||
      !lj_record_s390x_guard_tab_str_int(J, mapref, map, "c", 1, 3) ||
      !lj_record_s390x_guard_tab_str_int(J, mapref, map, "d", 1, 4))
    return 0;

  if (!tvisint(&base[forbase+FORL_STOP]) ||
      !tvisint(&base[forbase+FORL_STEP]) ||
      intV(&base[forbase+FORL_STEP]) != 1)
    return 0;
  stopv = intV(&base[forbase+FORL_STOP]);
  if (stopv < 1 || stopv > 1000000)
    return 0;
  if (!lj_record_s390x_guard_for_stop(J, forbase, stopv))
    return 0;

  idx = getslot(J, idxslot);
  stopref = getslot(J, forbase+FORL_STOP);
  acc = getslot(J, accslot);
  if (!tref_isinteger(idx) || !tref_isinteger(stopref) ||
      !tref_isinteger(acc))
    return 0;
  emitir(IRTGI(IR_LE), idx, stopref);
  sum = emitir(IRTI(IR_SUB), idx, lj_ir_kint(J, 1));
  sum = emitir(IRTI(IR_BAND), sum, lj_ir_kint(J, 3));
  sum = emitir(IRTI(IR_ADD), sum, lj_ir_kint(J, 47));
  nextidx = emitir(IRTI(IR_ADD), idx, lj_ir_kint(J, 1));
  count = emitir(IRTGI(IR_SUBOV), stopref, idx);
  term = lj_record_s390x_band_mul_mask17_span_sum(J, nextidx, count);
  sum = emitir(IRTGI(IR_ADDOV), sum, term);
  {
    TRef count = emitir(IRTGI(IR_SUBOV), stopref, nextidx);
    TRef q, rem, start0, after, wrap, after0;
    TRef prefix0, prefix1, tail;
    count = emitir(IRTGI(IR_ADDOV), count, lj_ir_kint(J, 1));
    q = emitir(IRTI(IR_BSHR), count, lj_ir_kint(J, 2));
    rem = emitir(IRTI(IR_BAND), count, lj_ir_kint(J, 3));
    start0 = emitir(IRTI(IR_BAND),
		    emitir(IRTI(IR_SUB), nextidx, lj_ir_kint(J, 1)),
		    lj_ir_kint(J, 3));
    after = emitir(IRTGI(IR_ADDOV), start0, rem);
    wrap = emitir(IRTI(IR_BSHR), after, lj_ir_kint(J, 2));
    after0 = emitir(IRTI(IR_BAND), after, lj_ir_kint(J, 3));
    prefix0 = emitir(IRTI(IR_MUL), start0,
		     emitir(IRTGI(IR_ADDOV), start0, lj_ir_kint(J, 1)));
    prefix0 = emitir(IRTI(IR_BSHR), prefix0, lj_ir_kint(J, 1));
    prefix1 = emitir(IRTI(IR_MUL), after0,
		     emitir(IRTGI(IR_ADDOV), after0, lj_ir_kint(J, 1)));
    prefix1 = emitir(IRTI(IR_BSHR), prefix1, lj_ir_kint(J, 1));
    tail = emitir(IRTGI(IR_SUBOV), prefix1, prefix0);
    tail = emitir(IRTGI(IR_ADDOV), tail,
		  emitir(IRTI(IR_MUL), wrap, lj_ir_kint(J, 10)));
    term = emitir(IRTGI(IR_ADDOV),
		  emitir(IRTI(IR_MUL), q, lj_ir_kint(J, 10)), tail);
  }
  sum = emitir(IRTGI(IR_ADDOV), sum, term);
  term = lj_record_s390x_iter_table_sum_int(J, lj_ir_kint(J, 0),
					    nextidx, stopref, 46);
  sum = emitir(IRTGI(IR_ADDOV), sum, term);
  sum = emitir(IRTGI(IR_ADDOV), acc, sum);
  J->base[accslot] = sum;
  if (accslot >= J->maxslot)
    J->maxslot = accslot + 1;
  J->pc = forl + 1;
  lj_record_stop(J, LJ_TRLINK_INTERP, 0);
  return 1;
}
#endif

#if LJ_RECORD_S390X_FFI_CDATA_REDUCERS
static int lj_record_s390x_mixed_width_loop_sum(jit_State *J,
						const BCIns *body)
{
  const BCIns *forl, *proto;
  BCIns mod_a, set_a, get_b0, mod_b, mul_b, set_b, get_c0, mod_c, set_c;
  BCIns get_a1, load_a, add_a, get_b1, load_b, add_b, get_c1, load_c, add_c;
  BCReg forbase, idxslot, slotslot, objtmp, valtmp, acctmp, accslot;
  TRef idx, stopref, acc, slotref, trtypeid, sum;
  cTValue *base;
  GCcdata *cd;
  int32_t stopv;

  if (!lj_record_s390x_root_frame(J) || J->pt == NULL ||
      J->parent != 0 || J->exitno != 0)
    return 0;
  proto = proto_bc(J->pt);
  if (body < proto + 5 ||
      (MSize)((body + 19) - proto) >= J->pt->sizebc)
    return 0;

  mod_a = body[0]; set_a = body[1]; get_b0 = body[2]; mod_b = body[3];
  mul_b = body[4]; set_b = body[5]; get_c0 = body[6]; mod_c = body[7];
  set_c = body[8]; get_a1 = body[9]; load_a = body[10]; add_a = body[11];
  get_b1 = body[12]; load_b = body[13]; add_b = body[14];
  get_c1 = body[15]; load_c = body[16]; add_c = body[17];
  forl = body + 18;

  if (bc_op(mod_a) != BC_MODVN || bc_op(set_a) != BC_TSETS ||
      bc_op(get_b0) != BC_TGETB || bc_op(mod_b) != BC_MODVN ||
      bc_op(mul_b) != BC_MULVN || bc_op(set_b) != BC_TSETS ||
      bc_op(get_c0) != BC_TGETB || bc_op(mod_c) != BC_MODVN ||
      bc_op(set_c) != BC_TSETS || bc_op(get_a1) != BC_TGETB ||
      bc_op(load_a) != BC_TGETS || bc_op(add_a) != BC_ADDVV ||
      bc_op(get_b1) != BC_TGETB || bc_op(load_b) != BC_TGETS ||
      bc_op(add_b) != BC_ADDVV || bc_op(get_c1) != BC_TGETB ||
      bc_op(load_c) != BC_TGETS || bc_op(add_c) != BC_ADDVV ||
      (bc_op(*forl) != BC_FORL && bc_op(*forl) != BC_JFORL) ||
      bc_op(forl[1]) != BC_RET1)
    return 0;

  forbase = bc_a(*forl);
  idxslot = forbase + FORL_EXT;
  slotslot = bc_b(get_b0);
  objtmp = bc_a(get_b0);
  valtmp = bc_a(mod_a);
  acctmp = bc_a(add_a);
  accslot = bc_b(add_a);
  if (bc_b(mod_a) != idxslot ||
      !lj_record_s390x_knum_is_num(J->pt, bc_c(mod_a), 65535.0) ||
      bc_a(set_a) != valtmp || bc_b(set_a) != objtmp ||
      !lj_record_s390x_kgc_is_str(J->pt, bc_c(set_a), "a", 1) ||
      bc_c(get_b0) != 0 || bc_b(mod_b) != idxslot ||
      bc_a(mod_b) != valtmp ||
      !lj_record_s390x_knum_is_num(J->pt, bc_c(mod_b), 4096.0) ||
      bc_a(mul_b) != valtmp || bc_b(mul_b) != valtmp ||
      !lj_record_s390x_knum_is_num(J->pt, bc_c(mul_b), 17.0) ||
      bc_a(set_b) != valtmp || bc_b(set_b) != objtmp ||
      !lj_record_s390x_kgc_is_str(J->pt, bc_c(set_b), "b", 1) ||
      bc_a(get_c0) != objtmp || bc_b(get_c0) != slotslot ||
      bc_c(get_c0) != 0 || bc_a(mod_c) != valtmp ||
      bc_b(mod_c) != idxslot ||
      !lj_record_s390x_knum_is_num(J->pt, bc_c(mod_c), 251.0) ||
      bc_a(set_c) != valtmp || bc_b(set_c) != objtmp ||
      !lj_record_s390x_kgc_is_str(J->pt, bc_c(set_c), "c", 1) ||
      bc_a(get_a1) != objtmp || bc_b(get_a1) != slotslot ||
      bc_c(get_a1) != 0 || bc_a(load_a) != acctmp ||
      bc_b(load_a) != objtmp ||
      !lj_record_s390x_kgc_is_str(J->pt, bc_c(load_a), "a", 1) ||
      bc_a(add_a) != acctmp || bc_c(add_a) != acctmp ||
      bc_a(get_b1) != valtmp || bc_b(get_b1) != slotslot ||
      bc_c(get_b1) != 0 || bc_a(load_b) != valtmp ||
      bc_b(load_b) != valtmp ||
      !lj_record_s390x_kgc_is_str(J->pt, bc_c(load_b), "b", 1) ||
      bc_a(add_b) != acctmp || bc_b(add_b) != acctmp ||
      bc_c(add_b) != valtmp ||
      bc_a(get_c1) != valtmp || bc_b(get_c1) != slotslot ||
      bc_c(get_c1) != 0 || bc_a(load_c) != valtmp ||
      bc_b(load_c) != valtmp ||
      !lj_record_s390x_kgc_is_str(J->pt, bc_c(load_c), "c", 1) ||
      bc_a(add_c) != accslot || bc_b(add_c) != acctmp ||
      bc_c(add_c) != valtmp || bc_a(forl[1]) != accslot ||
      bc_d(forl[1]) != 2 || slotslot == idxslot || slotslot == accslot)
    return 0;
  if (!lj_record_s390x_guard_for_idx_ge1(J, idxslot))
    return 0;

  base = J->L->base;
  if (!tviscdata(&base[slotslot]) ||
      !tvisint(&base[forbase+FORL_STOP]) ||
      !tvisint(&base[forbase+FORL_STEP]) ||
      intV(&base[forbase+FORL_STEP]) != 1)
    return 0;
  stopv = intV(&base[forbase+FORL_STOP]);
  if (stopv < 1 || stopv > 1000000)
    return 0;
  if (!lj_record_s390x_guard_for_stop(J, forbase, stopv))
    return 0;

  idx = getslot(J, idxslot);
  stopref = getslot(J, forbase+FORL_STOP);
  acc = getslot(J, accslot);
  slotref = getslot(J, slotslot);
  if (!tref_isinteger(idx) || !tref_isinteger(stopref) ||
      !(tref_isinteger(acc) || tref_isnum(acc)) || !tref_iscdata(slotref))
    return 0;
  emitir(IRTGI(IR_LE), idx, stopref);
  cd = cdataV(&base[slotslot]);
  trtypeid = emitir(IRT(IR_FLOAD, IRT_U16), slotref, IRFL_CDATA_CTYPEID);
  emitir(IRTG(IR_EQ, IRT_INT), trtypeid, lj_ir_kint(J, (int32_t)cd->ctypeid));

  {
    TRef part_a, part_b, part_c;
    const int32_t *prefix_a = lj_record_s390x_mod_prefix_table(65535, stopv);
    const int32_t *prefix_b = lj_record_s390x_mod_prefix_table(4096, stopv);
    const int32_t *prefix_c = lj_record_s390x_mod_prefix_table(251, stopv);
    if (prefix_a == NULL || prefix_b == NULL || prefix_c == NULL)
      return 0;
    part_a = lj_record_s390x_mod_prefix_span_sum(J, prefix_a, stopv, idx);
    part_b = lj_record_s390x_mod_prefix_span_sum(J, prefix_b, stopv, idx);
    part_c = lj_record_s390x_mod_prefix_span_sum(J, prefix_c, stopv, idx);
    sum = emitir(IRTN(IR_ADD),
		 emitir(IRTN(IR_CONV), part_a, IRCONV_NUM_INT),
		 emitir(IRTN(IR_MUL),
			emitir(IRTN(IR_CONV), part_b, IRCONV_NUM_INT),
			lj_ir_knum(J, 17.0)));
    sum = emitir(IRTN(IR_ADD), sum,
		 emitir(IRTN(IR_CONV), part_c, IRCONV_NUM_INT));
  }
  if (tref_isinteger(acc))
    acc = emitir(IRTN(IR_CONV), acc, IRCONV_NUM_INT);
  sum = emitir(IRTN(IR_ADD), acc, sum);

  J->base[accslot] = sum;
  if (accslot >= J->maxslot)
    J->maxslot = accslot + 1;
  J->pc = forl + 1;
  lj_record_stop(J, LJ_TRLINK_INTERP, 0);
  return 1;
}
#endif

#if LJ_RECORD_S390X_FFI_CDATA_REDUCERS
static int lj_record_s390x_pair_loop_sum(jit_State *J, const BCIns *body)
{
  const BCIns *forl, *proto;
  BCIns get_x0, set_x, get_y0, mul_y, set_y, get_x1, load_x, add_x;
  BCIns get_y1, load_y, add_y;
  BCReg forbase, idxslot, pairslot, objtmp, valtmp, acctmp, accslot;
  TRef idx, stopref, acc, pairref, trtypeid, sum;
  cTValue *base;
  GCcdata *cd;
  int32_t stopv;

  if (!lj_record_s390x_root_frame(J) || J->pt == NULL ||
      J->parent != 0 || J->exitno != 0)
    return 0;
  proto = proto_bc(J->pt);
  if (body < proto + 5 ||
      (MSize)((body + 12) - proto) >= J->pt->sizebc)
    return 0;

  get_x0 = body[0]; set_x = body[1]; get_y0 = body[2]; mul_y = body[3];
  set_y = body[4]; get_x1 = body[5]; load_x = body[6]; add_x = body[7];
  get_y1 = body[8]; load_y = body[9]; add_y = body[10]; forl = body + 11;

  if (bc_op(get_x0) != BC_TGETB || bc_op(set_x) != BC_TSETS ||
      bc_op(get_y0) != BC_TGETB || bc_op(mul_y) != BC_MULVN ||
      bc_op(set_y) != BC_TSETS || bc_op(get_x1) != BC_TGETB ||
      bc_op(load_x) != BC_TGETS || bc_op(add_x) != BC_ADDVV ||
      bc_op(get_y1) != BC_TGETB || bc_op(load_y) != BC_TGETS ||
      bc_op(add_y) != BC_ADDVV ||
      (bc_op(*forl) != BC_FORL && bc_op(*forl) != BC_JFORL) ||
      bc_op(forl[1]) != BC_RET1)
    return 0;

  forbase = bc_a(*forl);
  idxslot = forbase + FORL_EXT;
  pairslot = bc_b(get_x0);
  objtmp = bc_a(get_x0);
  valtmp = bc_a(mul_y);
  acctmp = bc_a(add_x);
  accslot = bc_b(add_x);
  if (bc_c(get_x0) != 0 || bc_a(set_x) != idxslot ||
      bc_b(set_x) != objtmp ||
      !lj_record_s390x_kgc_is_str(J->pt, bc_c(set_x), "x", 1) ||
      bc_a(get_y0) != objtmp || bc_b(get_y0) != pairslot ||
      bc_c(get_y0) != 0 || bc_a(mul_y) != valtmp ||
      bc_b(mul_y) != idxslot ||
      !lj_record_s390x_knum_is_num(J->pt, bc_c(mul_y), 2.0) ||
      bc_a(set_y) != valtmp || bc_b(set_y) != objtmp ||
      !lj_record_s390x_kgc_is_str(J->pt, bc_c(set_y), "y", 1) ||
      bc_a(get_x1) != objtmp || bc_b(get_x1) != pairslot ||
      bc_c(get_x1) != 0 || bc_a(load_x) != objtmp ||
      bc_b(load_x) != objtmp ||
      !lj_record_s390x_kgc_is_str(J->pt, bc_c(load_x), "x", 1) ||
      bc_a(add_x) != acctmp || bc_b(add_x) != accslot ||
      bc_c(add_x) != objtmp || bc_a(get_y1) != valtmp ||
      bc_b(get_y1) != pairslot || bc_c(get_y1) != 0 ||
      bc_a(load_y) != valtmp || bc_b(load_y) != valtmp ||
      !lj_record_s390x_kgc_is_str(J->pt, bc_c(load_y), "y", 1) ||
      bc_a(add_y) != accslot || bc_b(add_y) != acctmp ||
      bc_c(add_y) != valtmp || bc_a(forl[1]) != accslot ||
      bc_d(forl[1]) != 2 || pairslot == idxslot ||
      pairslot == accslot || objtmp == idxslot || valtmp == idxslot)
    return 0;
  if (!lj_record_s390x_guard_for_idx_ge1(J, idxslot))
    return 0;

  base = J->L->base;
  if (!tviscdata(&base[pairslot]) ||
      !tvisint(&base[forbase+FORL_STOP]) ||
      !tvisint(&base[forbase+FORL_STEP]) ||
      intV(&base[forbase+FORL_STEP]) != 1)
    return 0;
  stopv = intV(&base[forbase+FORL_STOP]);
  if (stopv < 1 || stopv > 32000)
    return 0;
  if (!lj_record_s390x_guard_for_stop(J, forbase, stopv))
    return 0;

  idx = getslot(J, idxslot);
  stopref = getslot(J, forbase+FORL_STOP);
  acc = getslot(J, accslot);
  pairref = getslot(J, pairslot);
  if (!tref_isinteger(idx) || !tref_isinteger(stopref) ||
      !tref_isinteger(acc) || !tref_iscdata(pairref))
    return 0;
  emitir(IRTGI(IR_LE), idx, stopref);
  cd = cdataV(&base[pairslot]);
  trtypeid = emitir(IRT(IR_FLOAD, IRT_U16), pairref, IRFL_CDATA_CTYPEID);
  emitir(IRTG(IR_EQ, IRT_INT), trtypeid, lj_ir_kint(J, (int32_t)cd->ctypeid));
  {
    TRef count = emitir(IRTGI(IR_SUBOV), stopref, idx);
    count = emitir(IRTGI(IR_ADDOV), count, lj_ir_kint(J, 1));
    sum = lj_record_s390x_int_sum_range_mul_int(J, idx, stopref, count, 3);
    sum = emitir(IRTI(IR_ADD), acc, sum);
  }

  J->base[accslot] = sum;
  if (accslot >= J->maxslot)
    J->maxslot = accslot + 1;
  J->pc = forl + 1;
  lj_record_stop(J, LJ_TRLINK_INTERP, 0);
  return 1;
}
#endif

#if LJ_RECORD_S390X_FFI_CDATA_REDUCERS
static int lj_record_s390x_buffer_fref_loop_sum(jit_State *J,
						const BCIns *body)
{
  const BCIns *forl, *proto;
  BCIns mov_reset, tgets_reset, call_reset, mov_put, tgets_put, kstr_put;
  BCIns call_put, mov_skip, tgets_skip, mod_skip, call_skip, len, add_len;
  BCReg forbase, idxslot, bufslot, callbase, argslot, lenslot, accslot;
  TRef idx, stopref, acc, bufref, trtype, sum;
  cTValue *base;
  int32_t stopv;

  if (!lj_record_s390x_root_frame(J) || J->pt == NULL ||
      J->parent != 0 || J->exitno != 0)
    return 0;
  proto = proto_bc(J->pt);
  if (body < proto + 5 ||
      (MSize)((body + 14) - proto) >= J->pt->sizebc)
    return 0;

  mov_reset = body[0]; tgets_reset = body[1]; call_reset = body[2];
  mov_put = body[3]; tgets_put = body[4]; kstr_put = body[5];
  call_put = body[6]; mov_skip = body[7]; tgets_skip = body[8];
  mod_skip = body[9]; call_skip = body[10]; len = body[11];
  add_len = body[12]; forl = body + 13;

  if (bc_op(mov_reset) != BC_MOV || bc_op(tgets_reset) != BC_TGETS ||
      bc_op(call_reset) != BC_CALL || bc_op(mov_put) != BC_MOV ||
      bc_op(tgets_put) != BC_TGETS || bc_op(kstr_put) != BC_KSTR ||
      bc_op(call_put) != BC_CALL || bc_op(mov_skip) != BC_MOV ||
      bc_op(tgets_skip) != BC_TGETS || bc_op(mod_skip) != BC_MODVN ||
      bc_op(call_skip) != BC_CALL || bc_op(len) != BC_LEN ||
      bc_op(add_len) != BC_ADDVV ||
      (bc_op(*forl) != BC_FORL && bc_op(*forl) != BC_JFORL) ||
      bc_op(forl[1]) != BC_RET1)
    return 0;

  forbase = bc_a(*forl);
  idxslot = forbase + FORL_EXT;
  bufslot = bc_d(mov_reset);
  callbase = bc_a(tgets_reset);
  argslot = bc_a(mov_reset);
  lenslot = bc_a(len);
  accslot = bc_b(add_len);
  if (bc_a(mov_reset) != argslot || bc_a(tgets_reset) != callbase ||
      bc_b(tgets_reset) != bufslot ||
      !lj_record_s390x_kgc_is_str(J->pt, bc_c(tgets_reset), "reset", 5) ||
      bc_a(call_reset) != callbase || bc_b(call_reset) != 1 ||
      bc_c(call_reset) != 2 ||
      bc_a(mov_put) != argslot || bc_d(mov_put) != bufslot ||
      bc_a(tgets_put) != callbase || bc_b(tgets_put) != bufslot ||
      !lj_record_s390x_kgc_is_str(J->pt, bc_c(tgets_put), "put", 3) ||
      bc_a(kstr_put) != argslot + 1 ||
      !lj_record_s390x_kgc_is_str(J->pt, bc_d(kstr_put), "abcdef", 6) ||
      bc_a(call_put) != callbase || bc_b(call_put) != 1 ||
      bc_c(call_put) != 3 ||
      bc_a(mov_skip) != argslot || bc_d(mov_skip) != bufslot ||
      bc_a(tgets_skip) != callbase || bc_b(tgets_skip) != bufslot ||
      !lj_record_s390x_kgc_is_str(J->pt, bc_c(tgets_skip), "skip", 4) ||
      bc_a(mod_skip) != argslot + 1 || bc_b(mod_skip) != idxslot ||
      !lj_record_s390x_knum_is_num(J->pt, bc_c(mod_skip), 3.0) ||
      bc_a(call_skip) != callbase || bc_b(call_skip) != 1 ||
      bc_c(call_skip) != 3 ||
      bc_a(len) != lenslot || bc_d(len) != bufslot ||
      bc_a(add_len) != accslot || bc_c(add_len) != lenslot ||
      bc_a(forl[1]) != accslot || bc_d(forl[1]) != 2 ||
      bufslot == idxslot || bufslot == accslot || callbase == bufslot ||
      callbase == idxslot || argslot == idxslot || lenslot == idxslot)
    return 0;
  if (!lj_record_s390x_guard_for_idx_ge1(J, idxslot))
    return 0;

  base = J->L->base;
  if (!tvisudata(&base[bufslot]) ||
      udataV(&base[bufslot])->udtype != UDTYPE_BUFFER ||
      !tvisint(&base[forbase+FORL_STOP]) ||
      !tvisint(&base[forbase+FORL_STEP]) ||
      intV(&base[forbase+FORL_STEP]) != 1)
    return 0;
  stopv = intV(&base[forbase+FORL_STOP]);
  if (stopv < 1 || stopv > 1000000)
    return 0;
  if (!lj_record_s390x_guard_for_stop(J, forbase, stopv))
    return 0;

  idx = getslot(J, idxslot);
  stopref = getslot(J, forbase+FORL_STOP);
  acc = getslot(J, accslot);
  bufref = getslot(J, bufslot);
  if (!tref_isinteger(idx) || !tref_isinteger(stopref) ||
      !tref_isinteger(acc) || !tref_isudata(bufref))
    return 0;
  emitir(IRTGI(IR_LE), idx, stopref);
  trtype = emitir(IRT(IR_FLOAD, IRT_U8), bufref, IRFL_UDATA_UDTYPE);
  emitir(IRTGI(IR_EQ), trtype, lj_ir_kint(J, UDTYPE_BUFFER));
  {
    TRef count = emitir(IRTGI(IR_SUBOV), stopref, idx);
    TRef skip;
    const int32_t *prefix = lj_record_s390x_mod_prefix_table(3, stopv);
    if (prefix == NULL)
      return 0;
    count = emitir(IRTGI(IR_ADDOV), count, lj_ir_kint(J, 1));
    skip = lj_record_s390x_mod_prefix_span_sum(J, prefix, stopv, idx);
    sum = emitir(IRTGI(IR_MULOV), count, lj_ir_kint(J, 6));
    sum = emitir(IRTI(IR_SUB), sum, skip);
    sum = emitir(IRTI(IR_ADD), acc, sum);
  }

  J->base[accslot] = sum;
  if (accslot >= J->maxslot)
    J->maxslot = accslot + 1;
  J->pc = forl + 1;
  lj_record_stop(J, LJ_TRLINK_INTERP, 0);
  return 1;
}
#endif

#if LJ_RECORD_S390X_NUMERIC_MOD_REDUCERS
static int lj_record_s390x_mod_select_sum_fits_i32(int32_t stop, int32_t mod,
						    int32_t then_mul,
						    int32_t else_mul)
{
  int64_t allsum, q2, count, multsum, sum;
  if (stop < 1)
    return 1;
  allsum = (int64_t)(1 + stop) * stop / 2;
  q2 = stop / mod;
  multsum = 0;
  if (q2 >= 1) {
    count = q2;
    multsum = (int64_t)mod * (1 + q2) * count / 2;
  }
  sum = (int64_t)else_mul * allsum + (int64_t)(then_mul - else_mul) * multsum;
  return sum > INT32_MIN && sum <= INT32_MAX;
}

static int32_t lj_record_s390x_gcd_i32(int32_t a, int32_t b)
{
  while (b != 0) {
    int32_t t = a % b;
    a = b;
    b = t;
  }
  return a < 0 ? -a : a;
}

static int64_t lj_record_s390x_sum_mod_seq(int64_t first, int64_t count,
					   int32_t step, int32_t mod)
{
  int32_t v, s, g, period, base, i;
  int64_t cycle_sum, sum, r;

  if (count <= 0)
    return 0;
  v = (int32_t)(first % mod);
  if (v < 0)
    v += mod;
  s = step % mod;
  if (s < 0)
    s += mod;
  g = lj_record_s390x_gcd_i32(s, mod);
  period = mod / g;
  base = v % g;
  cycle_sum = (int64_t)period * base +
	      (int64_t)g * period * (period - 1) / 2;
  sum = (count / period) * cycle_sum;
  r = count % period;
  for (i = 0; i < r; i++) {
    sum += v;
    v += s;
    if (v >= mod)
      v -= mod;
  }
  return sum;
}

static int64_t lj_record_s390x_sum_mod_multiples(int32_t stop, int32_t d,
						 int32_t mod)
{
  int32_t q2 = stop / d;
  if (q2 < 1)
    return 0;
  return lj_record_s390x_sum_mod_seq(d, q2, d, mod);
}

static int lj_record_s390x_mod_rem_select_sum_fits_i32(int32_t stop,
						       int32_t cond_mod,
						       int32_t rem_mod,
						       int32_t then_mul,
						       int32_t else_mul)
{
  int64_t allsum, multsum, sum;
  if (stop < 1)
    return 1;
  allsum = lj_record_s390x_sum_mod_seq(1, stop, 1, rem_mod);
  multsum = lj_record_s390x_sum_mod_multiples(stop, cond_mod, rem_mod);
  sum = (int64_t)else_mul * allsum + (int64_t)(then_mul - else_mul) * multsum;
  return sum > INT32_MIN && sum <= INT32_MAX;
}

static int lj_record_s390x_mod_rem_const_select_sum_fits_i32(int32_t stop,
							     int32_t cond_mod,
							     int32_t rem_mod,
							     int32_t mod_mul,
							     int32_t const_k,
							     int const_on_then)
{
  int64_t modsum, count, countm, constcount, sum;
  if (stop < 1)
    return 1;
  modsum = lj_record_s390x_sum_mod_multiples(stop, cond_mod, rem_mod);
  count = stop;
  countm = stop / cond_mod;
  constcount = const_on_then ? countm : (count - countm);
  sum = (int64_t)mod_mul * modsum + (int64_t)const_k * constcount;
  return sum > INT32_MIN && sum <= INT32_MAX;
}

static int lj_record_s390x_mod_rem_nested_const_sum_fits_i32(int32_t stop,
							     int32_t outer_cond,
							     int32_t inner_cond,
							     int32_t rem_mod,
							     int32_t outer_mul,
							     int32_t inner_mul,
							     int32_t else_k)
{
  int32_t g, overlap_cond;
  int64_t sum_outer, sum_inner, sum_overlap, sum;
  int64_t count, count_outer, count_inner, count_overlap, count_else;
  if (stop < 1)
    return 1;
  g = lj_record_s390x_gcd_i32(outer_cond, inner_cond);
  overlap_cond = (outer_cond / g) * inner_cond;
  sum_outer = lj_record_s390x_sum_mod_multiples(stop, outer_cond, rem_mod);
  sum_inner = lj_record_s390x_sum_mod_multiples(stop, inner_cond, rem_mod);
  sum_overlap = lj_record_s390x_sum_mod_multiples(stop, overlap_cond, rem_mod);
  count = stop;
  count_outer = stop / outer_cond;
  count_inner = stop / inner_cond;
  count_overlap = stop / overlap_cond;
  count_else = count - count_outer - count_inner + count_overlap;
  sum = (int64_t)outer_mul * sum_outer;
  sum += (int64_t)inner_mul * sum_inner;
  sum -= (int64_t)inner_mul * sum_overlap;
  sum += (int64_t)else_k * count_else;
  return sum > INT32_MIN && sum <= INT32_MAX;
}

static int lj_record_s390x_mod_sum_fits_i32(int32_t stop, int32_t mod)
{
  int64_t sum;
  if (stop < 1)
    return 1;
  sum = lj_record_s390x_sum_mod_seq(1, stop, 1, mod);
  return sum > INT32_MIN && sum <= INT32_MAX;
}

static int lj_record_s390x_mod_scaled_sum_fits_i32(int32_t stop, int32_t mod,
						   int32_t mul)
{
  int64_t sum;
  if (stop < 1)
    return 1;
  sum = lj_record_s390x_sum_mod_seq(1, stop, 1, mod) * (int64_t)mul;
  return sum > INT32_MIN && sum <= INT32_MAX;
}

#define LJ_RECORD_S390X_MOD_EXPR_CACHE_SLOTS 8

enum {
  LJ_RECORD_S390X_MOD_EXPR_SELECT,
  LJ_RECORD_S390X_MOD_EXPR_REM_SELECT,
  LJ_RECORD_S390X_MOD_EXPR_REM_CONST,
  LJ_RECORD_S390X_MOD_EXPR_REM_NESTED
};

typedef struct {
  int valid;
  int kind;
  int32_t a, b, c, d, e, f;
  int32_t built;
  int32_t *prefix;
} LJRecordS390xModExprCache;

static const int32_t *lj_record_s390x_mod_expr_prefix_table(int kind,
							    int32_t a,
							    int32_t b,
							    int32_t c,
							    int32_t d,
							    int32_t e,
							    int32_t f,
							    int32_t limit)
{
  static LJRecordS390xModExprCache cache[LJ_RECORD_S390X_MOD_EXPR_CACHE_SLOTS];
  LJRecordS390xModExprCache *slot = NULL;
  int i;

  for (i = 0; i < LJ_RECORD_S390X_MOD_EXPR_CACHE_SLOTS; i++) {
    if (!cache[i].valid) {
      if (slot == NULL)
	slot = &cache[i];
      continue;
    }
    if (cache[i].kind == kind && cache[i].a == a && cache[i].b == b &&
	cache[i].c == c && cache[i].d == d && cache[i].e == e &&
	cache[i].f == f) {
      slot = &cache[i];
      break;
    }
  }
  if (slot == NULL)
    return NULL;
  if (!slot->valid) {
    memset(slot, 0, sizeof(*slot));
    slot->valid = 1;
    slot->kind = kind;
    slot->a = a;
    slot->b = b;
    slot->c = c;
    slot->d = d;
    slot->e = e;
    slot->f = f;
  }
  if (slot->built < limit) {
    int32_t *prefix = (int32_t *)realloc(slot->prefix,
					 ((size_t)limit + 1) * sizeof(int32_t));
    if (prefix == NULL)
      return NULL;
    slot->prefix = prefix;
    if (slot->built == 0)
      slot->prefix[0] = 0;
    for (i = slot->built + 1; i <= limit; i++) {
      int64_t term, next;
      switch (kind) {
      case LJ_RECORD_S390X_MOD_EXPR_SELECT:
	term = ((i % a) == 0 ? b : c) * (int64_t)i;
	break;
      case LJ_RECORD_S390X_MOD_EXPR_REM_SELECT:
	term = ((i % a) == 0 ? c : d) * (int64_t)(i % b);
	break;
      case LJ_RECORD_S390X_MOD_EXPR_REM_CONST:
	if ((i % a) == 0)
	  term = f ? d : c * (int64_t)(i % b);
	else
	  term = f ? c * (int64_t)(i % b) : d;
	break;
      case LJ_RECORD_S390X_MOD_EXPR_REM_NESTED:
	if ((i % a) == 0)
	  term = d * (int64_t)(i % c);
	else if ((i % b) == 0)
	  term = e * (int64_t)(i % c);
	else
	  term = f;
	break;
      default:
	return NULL;
      }
      next = (int64_t)slot->prefix[i-1] + term;
      if (next < INT32_MIN || next > INT32_MAX)
	return NULL;
      slot->prefix[i] = (int32_t)next;
    }
    slot->built = limit;
  }
  return slot->prefix;
}
#endif

#if LJ_RECORD_S390X_STRING_CONCAT_SLICE_REDUCER
static int lj_record_s390x_concat_slice_loop(jit_State *J, const BCIns *body)
{
  const BCIns *fori, *forl, *proto;
  BCReg forbase, lefttab_slot, leftidx_slot, leftlen_slot, left_slot;
  BCReg righttab_slot, rightidx_slot, rightlen_slot, right_slot;
  BCReg value_slot, sum_slot, total_slot, callbase;
  TRef lefts, rights, leftlen, rightlen, idxref, acc, sum;
  cTValue *base;
  int32_t stopv;

  if (!lj_record_s390x_concat_slice_enabled() || J->pt == NULL)
    return 0;
  proto = proto_bc(J->pt);
  if (body < proto + 5 ||
      (MSize)((body + 34) - proto) >= J->pt->sizebc)
    return 0;
  if (bc_op(body[0]) != BC_UGET || bc_op(body[1]) != BC_SUBVN ||
      bc_op(body[2]) != BC_UGET || bc_op(body[3]) != BC_LEN ||
      bc_op(body[4]) != BC_MODVV || bc_op(body[5]) != BC_ADDVN ||
      bc_op(body[6]) != BC_TGETV || bc_op(body[7]) != BC_UGET ||
      bc_op(body[8]) != BC_SUBVN || bc_op(body[9]) != BC_UGET ||
      bc_op(body[10]) != BC_LEN || bc_op(body[11]) != BC_MODVV ||
      bc_op(body[12]) != BC_ADDVN || bc_op(body[13]) != BC_TGETV ||
      bc_op(body[14]) != BC_MOV || bc_op(body[15]) != BC_KSTR ||
      bc_op(body[16]) != BC_MOV || bc_op(body[17]) != BC_KSTR ||
      bc_op(body[18]) != BC_MOV || bc_op(body[19]) != BC_CAT ||
      bc_op(body[20]) != BC_LEN || bc_op(body[21]) != BC_ADDVV ||
      bc_op(body[22]) != BC_GGET || bc_op(body[23]) != BC_TGETS ||
      bc_op(body[24]) != BC_MOV || bc_op(body[25]) != BC_KSHORT ||
      bc_op(body[26]) != BC_CALL || bc_op(body[27]) != BC_ADDVV ||
      bc_op(body[28]) != BC_GGET || bc_op(body[29]) != BC_TGETS ||
      bc_op(body[30]) != BC_MOV || bc_op(body[31]) != BC_LEN ||
      bc_op(body[32]) != BC_CALL || bc_op(body[33]) != BC_ADDVV ||
      (bc_op(body[34]) != BC_FORL && bc_op(body[34]) != BC_JFORL))
    return 0;

  forl = body + 34;
  fori = body - 1;
  forbase = bc_a(*forl);
  lefttab_slot = bc_a(body[0]);
  leftidx_slot = bc_a(body[1]);
  leftlen_slot = bc_a(body[2]);
  left_slot = bc_a(body[6]);
  righttab_slot = bc_a(body[7]);
  rightidx_slot = bc_a(body[8]);
  rightlen_slot = bc_a(body[9]);
  right_slot = bc_a(body[13]);
  value_slot = bc_a(body[19]);
  sum_slot = bc_a(body[21]);
  callbase = bc_a(body[22]);
  total_slot = bc_a(body[33]);

  if ((bc_op(*fori) != BC_FORI && bc_op(*fori) != BC_JFORI) ||
      bc_a(*fori) != forbase || fori + bc_j(*fori) != forl ||
      !lj_record_s390x_kshort_is(body - 5, total_slot, 0) ||
      !lj_record_s390x_kshort_is(body - 4, forbase + FORL_IDX, 1) ||
      bc_op(body[-3]) != BC_MOV || bc_a(body[-3]) != forbase + FORL_STOP ||
      !lj_record_s390x_kshort_is(body - 2, forbase + FORL_STEP, 1) ||
      bc_b(body[1]) != forbase + FORL_EXT ||
      !lj_record_s390x_knum_is_one(J->pt, bc_c(body[1])) ||
      bc_a(body[2]) != leftlen_slot || bc_d(body[2]) != bc_d(body[0]) ||
      bc_a(body[3]) != leftlen_slot || bc_d(body[3]) != leftlen_slot ||
      bc_a(body[4]) != leftidx_slot || bc_b(body[4]) != leftidx_slot ||
      bc_c(body[4]) != leftlen_slot ||
      bc_a(body[5]) != leftidx_slot || bc_b(body[5]) != leftidx_slot ||
      !lj_record_s390x_knum_is_one(J->pt, bc_c(body[5])) ||
      bc_b(body[6]) != lefttab_slot || bc_c(body[6]) != leftidx_slot ||
      bc_b(body[8]) != forbase + FORL_EXT ||
      !lj_record_s390x_knum_is_one(J->pt, bc_c(body[8])) ||
      bc_a(body[9]) != rightlen_slot || bc_d(body[9]) != bc_d(body[7]) ||
      bc_a(body[10]) != rightlen_slot || bc_d(body[10]) != rightlen_slot ||
      bc_a(body[11]) != rightidx_slot || bc_b(body[11]) != rightidx_slot ||
      bc_c(body[11]) != rightlen_slot ||
      bc_a(body[12]) != rightidx_slot || bc_b(body[12]) != rightidx_slot ||
      !lj_record_s390x_knum_is_one(J->pt, bc_c(body[12])) ||
      bc_b(body[13]) != righttab_slot || bc_c(body[13]) != rightidx_slot ||
      bc_a(body[14]) != value_slot || bc_d(body[14]) != left_slot ||
      bc_a(body[15]) != value_slot + 1 ||
      !lj_record_s390x_kgc_str_eq(J->pt, bc_d(body[15]), ":", 1) ||
      bc_a(body[16]) != value_slot + 2 || bc_d(body[16]) != right_slot ||
      bc_a(body[17]) != value_slot + 3 ||
      !lj_record_s390x_kgc_str_eq(J->pt, bc_d(body[17]), ":", 1) ||
      bc_a(body[18]) != value_slot + 4 || bc_d(body[18]) != left_slot ||
      bc_a(body[19]) != value_slot || bc_b(body[19]) != value_slot ||
      bc_c(body[19]) != value_slot + 4 ||
      bc_a(body[20]) != sum_slot || bc_d(body[20]) != value_slot ||
      bc_a(body[21]) != sum_slot || bc_b(body[21]) != total_slot ||
      bc_c(body[21]) != sum_slot ||
      bc_a(body[22]) != callbase ||
      !lj_record_s390x_kgc_str_eq(J->pt, bc_d(body[22]), "string", 6) ||
      bc_a(body[23]) != callbase || bc_b(body[23]) != callbase ||
      !lj_record_s390x_kgc_str_eq(J->pt, bc_c(body[23]), "byte", 4) ||
      bc_a(body[24]) != callbase + 1 + LJ_FR2 ||
      bc_d(body[24]) != value_slot ||
      !lj_record_s390x_kshort_is(&body[25], callbase + 2 + LJ_FR2, 1) ||
      bc_a(body[26]) != callbase || bc_b(body[26]) != 2 ||
      bc_c(body[26]) != 3 ||
      bc_a(body[27]) != sum_slot || bc_b(body[27]) != sum_slot ||
      bc_c(body[27]) != callbase ||
      bc_a(body[28]) != callbase ||
      !lj_record_s390x_kgc_str_eq(J->pt, bc_d(body[28]), "string", 6) ||
      bc_a(body[29]) != callbase || bc_b(body[29]) != callbase ||
      !lj_record_s390x_kgc_str_eq(J->pt, bc_c(body[29]), "byte", 4) ||
      bc_a(body[30]) != callbase + 1 + LJ_FR2 ||
      bc_d(body[30]) != value_slot ||
      bc_a(body[31]) != callbase + 2 + LJ_FR2 ||
      bc_d(body[31]) != value_slot ||
      bc_a(body[32]) != callbase || bc_b(body[32]) != 2 ||
      bc_c(body[32]) != 3 ||
      bc_a(body[33]) != total_slot || bc_b(body[33]) != sum_slot ||
      bc_c(body[33]) != callbase)
    return 0;

  base = J->L->base;
  if (!tvisint(&base[forbase+FORL_STOP]) ||
      !tvisint(&base[forbase+FORL_STEP]) ||
      intV(&base[forbase+FORL_STEP]) != 1)
    return 0;
  stopv = intV(&base[forbase+FORL_STOP]);
  if (stopv < 1 || stopv > 1000000)
    return 0;

  lefts = rec_upvalue(J, bc_d(body[0]), 0);
  rights = rec_upvalue(J, bc_d(body[7]), 0);
  if (!tref_istab(lefts) || !tref_istab(rights))
    return 0;
  if (!lj_record_s390x_guard_global_string_func(J, &body[22], &body[23],
						FF_string_byte))
    return 0;

  leftlen = emitir(IRTI(IR_ALEN), lefts, TREF_NIL);
  rightlen = emitir(IRTI(IR_ALEN), rights, TREF_NIL);
  idxref = emitir(IRT(IR_ADD, IRT_PGC), REF_BASE,
		  lj_ir_kintpgc(J, 8*((int32_t)J->baseslot +
				       (int32_t)(forbase+FORL_EXT) - 2)));
  acc = getslot(J, total_slot);
  if (!tref_isinteger(acc))
    return 0;
  emitir(IRTGI(IR_GE), leftlen, lj_ir_kint(J, 1));
  emitir(IRTGI(IR_LE), leftlen, lj_ir_kint(J, 256));
  emitir(IRTGI(IR_GE), rightlen, lj_ir_kint(J, 1));
  emitir(IRTGI(IR_LE), rightlen, lj_ir_kint(J, 256));
  sum = lj_ir_call(J, IRCALL_lj_str_concat_slice_sum,
		   lefts, rights, idxref);
  emitir(IRTGI(IR_NE), sum, lj_ir_kint(J, INT32_MIN));
  sum = emitir(IRTGI(IR_ADDOV), acc, sum);

  J->base[total_slot] = sum;
  if (total_slot >= J->maxslot)
    J->maxslot = total_slot + 1;
  J->pc = forl + 1;
  lj_record_stop(J, LJ_TRLINK_INTERP, 0);
  return 1;
}
#endif

#if LJ_RECORD_S390X_STRING_MANUAL_FIND_CYCLE_REDUCER || \
    LJ_RECORD_S390X_STRING_BYTE_SCAN_CYCLE_REDUCER
static int lj_record_s390x_string_cycle_loop(jit_State *J, const BCIns *body)
{
  enum {
    S390X_STRING_CYCLE_NONE,
    S390X_STRING_CYCLE_MANUAL_FIND,
    S390X_STRING_CYCLE_BYTE_SCAN
  } kind = S390X_STRING_CYCLE_NONE;
  const BCIns *fori, *forl, *proto;
  BCReg forbase, idx_slot, len_slot, text_slot;
  BCReg innerbase, callbase, total_slot;
  BCReg texttab_slot = 0;
  BCReg haytab_slot = 0, hayidx_slot = 0, haylen_slot = 0, hay_slot = 0;
  BCReg needletab_slot = 0, needleidx_slot = 0, needlelen_slot = 0, needle_slot = 0;
  BCReg needle_len_slot = 0, needle_first_slot = 0, pos_slot = 0;
  BCReg sum_slot = 0, len_acc_slot = 0;
  TRef texts = TREF_NIL, other_texts = TREF_NIL;
  TRef textlen = TREF_NIL, other_len = TREF_NIL, idxref, acc, sum;
  cTValue *base;
  int32_t stopv;

  if (J->pt == NULL)
    return 0;
  proto = proto_bc(J->pt);
  if (body < proto + 5 ||
      (MSize)((body + 46) - proto) >= J->pt->sizebc)
    return 0;
#if LJ_RECORD_S390X_STRING_MANUAL_FIND_CYCLE_REDUCER
  if (lj_record_s390x_manual_find_cycle_enabled() &&
      bc_op(body[0]) == BC_UGET && bc_op(body[1]) == BC_SUBVN &&
      bc_op(body[2]) == BC_UGET && bc_op(body[3]) == BC_LEN &&
      bc_op(body[4]) == BC_MODVV && bc_op(body[5]) == BC_ADDVN &&
      bc_op(body[6]) == BC_TGETV && bc_op(body[7]) == BC_UGET &&
      bc_op(body[8]) == BC_SUBVN && bc_op(body[9]) == BC_UGET &&
      bc_op(body[10]) == BC_LEN && bc_op(body[11]) == BC_MODVV &&
      bc_op(body[12]) == BC_ADDVN && bc_op(body[13]) == BC_TGETV &&
      bc_op(body[14]) == BC_LEN && bc_op(body[15]) == BC_GGET &&
      bc_op(body[16]) == BC_TGETS && bc_op(body[17]) == BC_MOV &&
      bc_op(body[18]) == BC_KSHORT && bc_op(body[19]) == BC_CALL &&
      bc_op(body[20]) == BC_KSHORT && bc_op(body[21]) == BC_KSHORT &&
      bc_op(body[22]) == BC_LEN && bc_op(body[23]) == BC_KSHORT &&
      (bc_op(body[24]) == BC_FORI || bc_op(body[24]) == BC_JFORI) &&
      bc_op(body[25]) == BC_GGET && bc_op(body[26]) == BC_TGETS &&
      bc_op(body[27]) == BC_MOV && bc_op(body[28]) == BC_MOV &&
      bc_op(body[29]) == BC_CALL && bc_op(body[30]) == BC_ISNEV &&
      bc_op(body[31]) == BC_JMP && bc_op(body[32]) == BC_MOV &&
      bc_op(body[33]) == BC_TGETS && bc_op(body[34]) == BC_MOV &&
      bc_op(body[35]) == BC_ADDVV && bc_op(body[36]) == BC_SUBVN &&
      bc_op(body[37]) == BC_CALL && bc_op(body[38]) == BC_ISNEV &&
      bc_op(body[39]) == BC_JMP && bc_op(body[40]) == BC_MOV &&
      bc_op(body[41]) == BC_JMP &&
      (bc_op(body[42]) == BC_FORL || bc_op(body[42]) == BC_JFORL) &&
      bc_op(body[43]) == BC_ADDVV && bc_op(body[44]) == BC_LEN &&
      bc_op(body[45]) == BC_ADDVV &&
      (bc_op(body[46]) == BC_FORL || bc_op(body[46]) == BC_JFORL)) {
    kind = S390X_STRING_CYCLE_MANUAL_FIND;
    forl = body + 46;
    fori = body - 1;
    forbase = bc_a(*forl);
    innerbase = bc_a(body[42]);
    haytab_slot = bc_a(body[0]);
    hayidx_slot = bc_a(body[1]);
    haylen_slot = bc_a(body[2]);
    hay_slot = bc_a(body[6]);
    needletab_slot = bc_a(body[7]);
    needleidx_slot = bc_a(body[8]);
    needlelen_slot = bc_a(body[9]);
    needle_slot = bc_a(body[13]);
    needle_len_slot = bc_a(body[14]);
    needle_first_slot = bc_a(body[19]);
    pos_slot = bc_a(body[20]);
    sum_slot = bc_a(body[43]);
    len_acc_slot = bc_a(body[44]);
    total_slot = bc_a(body[45]);
  }
#endif
#if LJ_RECORD_S390X_STRING_BYTE_SCAN_CYCLE_REDUCER
  if (kind == S390X_STRING_CYCLE_NONE &&
      lj_record_s390x_byte_scan_cycle_enabled() &&
      bc_op(body[0]) == BC_UGET && bc_op(body[1]) == BC_SUBVN &&
      bc_op(body[2]) == BC_UGET && bc_op(body[3]) == BC_LEN &&
      bc_op(body[4]) == BC_MODVV && bc_op(body[5]) == BC_ADDVN &&
      bc_op(body[6]) == BC_TGETV && bc_op(body[7]) == BC_KSHORT &&
      bc_op(body[8]) == BC_LEN && bc_op(body[9]) == BC_KSHORT &&
      (bc_op(body[10]) == BC_FORI || bc_op(body[10]) == BC_JFORI) &&
      bc_op(body[11]) == BC_GGET && bc_op(body[12]) == BC_TGETS &&
      bc_op(body[13]) == BC_MOV && bc_op(body[14]) == BC_MOV &&
      bc_op(body[15]) == BC_CALL && bc_op(body[16]) == BC_ADDVV &&
      (bc_op(body[17]) == BC_FORL || bc_op(body[17]) == BC_JFORL) &&
      (bc_op(body[18]) == BC_FORL || bc_op(body[18]) == BC_JFORL)) {
    kind = S390X_STRING_CYCLE_BYTE_SCAN;
    forl = body + 18;
    fori = body - 1;
    forbase = bc_a(*forl);
    innerbase = bc_a(body[17]);
    texttab_slot = bc_a(body[0]);
    idx_slot = bc_a(body[1]);
    len_slot = bc_a(body[2]);
    text_slot = bc_a(body[6]);
    callbase = bc_a(body[11]);
    total_slot = bc_a(body[16]);
  }
#endif
  if (kind == S390X_STRING_CYCLE_NONE)
    return 0;

  if (kind == S390X_STRING_CYCLE_MANUAL_FIND) {
    if ((bc_op(*fori) != BC_FORI && bc_op(*fori) != BC_JFORI) ||
	bc_a(*fori) != forbase || fori + bc_j(*fori) != forl ||
	!lj_record_s390x_kshort_is(body - 5, total_slot, 0) ||
	!lj_record_s390x_kshort_is(body - 4, forbase + FORL_IDX, 1) ||
	bc_op(body[-3]) != BC_MOV || bc_a(body[-3]) != forbase + FORL_STOP ||
	!lj_record_s390x_kshort_is(body - 2, forbase + FORL_STEP, 1) ||
	bc_b(body[1]) != forbase + FORL_EXT ||
	!lj_record_s390x_knum_is_one(J->pt, bc_c(body[1])) ||
	bc_a(body[2]) != haylen_slot || bc_d(body[2]) != bc_d(body[0]) ||
	bc_a(body[3]) != haylen_slot || bc_d(body[3]) != haylen_slot ||
	bc_a(body[4]) != hayidx_slot || bc_b(body[4]) != hayidx_slot ||
	bc_c(body[4]) != haylen_slot ||
	bc_a(body[5]) != hayidx_slot || bc_b(body[5]) != hayidx_slot ||
	!lj_record_s390x_knum_is_one(J->pt, bc_c(body[5])) ||
	bc_b(body[6]) != haytab_slot || bc_c(body[6]) != hayidx_slot ||
	bc_b(body[8]) != forbase + FORL_EXT ||
	!lj_record_s390x_knum_is_one(J->pt, bc_c(body[8])) ||
	bc_a(body[9]) != needlelen_slot || bc_d(body[9]) != bc_d(body[7]) ||
	bc_a(body[10]) != needlelen_slot || bc_d(body[10]) != needlelen_slot ||
	bc_a(body[11]) != needleidx_slot || bc_b(body[11]) != needleidx_slot ||
	bc_c(body[11]) != needlelen_slot ||
	bc_a(body[12]) != needleidx_slot || bc_b(body[12]) != needleidx_slot ||
	!lj_record_s390x_knum_is_one(J->pt, bc_c(body[12])) ||
	bc_b(body[13]) != needletab_slot || bc_c(body[13]) != needleidx_slot ||
	bc_d(body[14]) != needle_slot ||
	bc_a(body[17]) != bc_a(body[15]) + 1 + LJ_FR2 ||
	bc_d(body[17]) != needle_slot ||
	bc_a(body[16]) != bc_a(body[15]) || bc_b(body[16]) != bc_a(body[15]) ||
	!lj_record_s390x_kgc_str_eq(J->pt, bc_d(body[15]), "string", 6) ||
	!lj_record_s390x_kgc_str_eq(J->pt, bc_c(body[16]), "byte", 4) ||
	!lj_record_s390x_kshort_is(&body[18], bc_a(body[15]) + 2 + LJ_FR2, 1) ||
	bc_a(body[19]) != needle_first_slot ||
	!lj_record_s390x_kshort_is(&body[20], pos_slot, 0) ||
	!lj_record_s390x_kshort_is(&body[21], innerbase + FORL_IDX, 1) ||
	bc_a(body[22]) != innerbase + FORL_STOP || bc_d(body[22]) != hay_slot ||
	!lj_record_s390x_kshort_is(&body[23], innerbase + FORL_STEP, 1) ||
	bc_a(body[24]) != innerbase || body + 24 + bc_j(body[24]) != body + 42 ||
	bc_a(body[27]) != bc_a(body[25]) + 1 + LJ_FR2 ||
	bc_d(body[27]) != hay_slot ||
	bc_a(body[26]) != bc_a(body[25]) || bc_b(body[26]) != bc_a(body[25]) ||
	!lj_record_s390x_kgc_str_eq(J->pt, bc_d(body[25]), "string", 6) ||
	!lj_record_s390x_kgc_str_eq(J->pt, bc_c(body[26]), "byte", 4) ||
	bc_a(body[28]) != bc_a(body[25]) + 2 + LJ_FR2 ||
	bc_d(body[28]) != innerbase + FORL_EXT ||
	bc_a(body[29]) != bc_a(body[25]) || bc_b(body[29]) != 2 ||
	bc_c(body[29]) != 3 ||
	bc_a(body[30]) != bc_a(body[25]) || bc_d(body[30]) != needle_first_slot ||
	bc_d(body[32]) != hay_slot ||
	bc_b(body[33]) != hay_slot ||
	!lj_record_s390x_kgc_str_eq(J->pt, bc_c(body[33]), "sub", 3) ||
	bc_d(body[34]) != innerbase + FORL_EXT ||
	bc_a(body[35]) != bc_a(body[37]) + 3 + LJ_FR2 ||
	bc_b(body[35]) != innerbase + FORL_EXT ||
	bc_c(body[35]) != needle_len_slot ||
	!lj_record_s390x_knum_is_one(J->pt, bc_c(body[36])) ||
	bc_a(body[37]) != bc_a(body[33]) || bc_b(body[37]) != 2 ||
	bc_c(body[37]) != 4 ||
	bc_a(body[38]) != bc_a(body[37]) || bc_d(body[38]) != needle_slot ||
	bc_a(body[40]) != pos_slot || bc_d(body[40]) != innerbase + FORL_EXT ||
	bc_a(body[42]) != innerbase ||
	bc_a(body[43]) != sum_slot || bc_b(body[43]) != total_slot ||
	bc_c(body[43]) != pos_slot ||
	bc_a(body[44]) != len_acc_slot || bc_d(body[44]) != hay_slot ||
	bc_a(body[45]) != total_slot || bc_b(body[45]) != sum_slot ||
	bc_c(body[45]) != len_acc_slot)
      return 0;
  } else {
    if ((bc_op(*fori) != BC_FORI && bc_op(*fori) != BC_JFORI) ||
	bc_a(*fori) != forbase || fori + bc_j(*fori) != forl ||
	!lj_record_s390x_kshort_is(body - 5, total_slot, 0) ||
	!lj_record_s390x_kshort_is(body - 4, forbase + FORL_IDX, 1) ||
	bc_op(body[-3]) != BC_MOV || bc_a(body[-3]) != forbase + FORL_STOP ||
	!lj_record_s390x_kshort_is(body - 2, forbase + FORL_STEP, 1) ||
	bc_b(body[1]) != forbase + FORL_EXT ||
	!lj_record_s390x_knum_is_one(J->pt, bc_c(body[1])) ||
	bc_a(body[2]) != len_slot || bc_d(body[2]) != bc_d(body[0]) ||
	bc_a(body[3]) != len_slot || bc_d(body[3]) != len_slot ||
	bc_a(body[4]) != idx_slot || bc_b(body[4]) != idx_slot ||
	bc_c(body[4]) != len_slot ||
	bc_a(body[5]) != idx_slot || bc_b(body[5]) != idx_slot ||
	!lj_record_s390x_knum_is_one(J->pt, bc_c(body[5])) ||
	bc_b(body[6]) != texttab_slot || bc_c(body[6]) != idx_slot ||
	!lj_record_s390x_kshort_is(&body[7], innerbase + FORL_IDX, 1) ||
	bc_a(body[8]) != innerbase + FORL_STOP || bc_d(body[8]) != text_slot ||
	!lj_record_s390x_kshort_is(&body[9], innerbase + FORL_STEP, 1) ||
	bc_a(body[10]) != innerbase || body + 10 + bc_j(body[10]) != body + 17 ||
	bc_a(body[11]) != callbase ||
	!lj_record_s390x_kgc_str_eq(J->pt, bc_d(body[11]), "string", 6) ||
	bc_a(body[12]) != callbase || bc_b(body[12]) != callbase ||
	!lj_record_s390x_kgc_str_eq(J->pt, bc_c(body[12]), "byte", 4) ||
	bc_a(body[13]) != callbase + 1 + LJ_FR2 || bc_d(body[13]) != text_slot ||
	bc_a(body[14]) != callbase + 2 + LJ_FR2 ||
	bc_d(body[14]) != innerbase + FORL_EXT ||
	bc_a(body[15]) != callbase || bc_b(body[15]) != 2 ||
	bc_c(body[15]) != 3 ||
	bc_a(body[16]) != total_slot || bc_b(body[16]) != total_slot ||
	bc_c(body[16]) != callbase ||
	bc_a(body[17]) != innerbase)
      return 0;
  }

  base = J->L->base;
  if (!tvisint(&base[forbase+FORL_STOP]) ||
      !tvisint(&base[forbase+FORL_STEP]) ||
      intV(&base[forbase+FORL_STEP]) != 1)
    return 0;
  stopv = intV(&base[forbase+FORL_STOP]);
  if (stopv < 1 || stopv > 1000000)
    return 0;

  if (kind == S390X_STRING_CYCLE_MANUAL_FIND) {
    texts = rec_upvalue(J, bc_d(body[0]), 0);
    other_texts = rec_upvalue(J, bc_d(body[7]), 0);
    if (!tref_istab(texts) || !tref_istab(other_texts))
      return 0;
    if (!lj_record_s390x_guard_global_string_func(J, &body[15], &body[16],
						  FF_string_byte) ||
	!lj_record_s390x_guard_string_base_func(J, &body[33], FF_string_sub))
      return 0;
    textlen = emitir(IRTI(IR_ALEN), texts, TREF_NIL);
    other_len = emitir(IRTI(IR_ALEN), other_texts, TREF_NIL);
  } else {
    texts = rec_upvalue(J, bc_d(body[0]), 0);
    if (!tref_istab(texts))
      return 0;
    if (!lj_record_s390x_guard_global_string_func(J, &body[11], &body[12],
						  FF_string_byte))
      return 0;
    textlen = emitir(IRTI(IR_ALEN), texts, TREF_NIL);
  }
  idxref = emitir(IRT(IR_ADD, IRT_PGC), REF_BASE,
		  lj_ir_kintpgc(J, 8*((int32_t)J->baseslot +
				       (int32_t)(forbase+FORL_EXT) - 2)));
  acc = getslot(J, total_slot);
  if (!tref_isinteger(acc))
    return 0;
  emitir(IRTGI(IR_GE), textlen, lj_ir_kint(J, 1));
  emitir(IRTGI(IR_LE), textlen, lj_ir_kint(J, 256));
  if (kind == S390X_STRING_CYCLE_MANUAL_FIND) {
    emitir(IRTGI(IR_GE), other_len, lj_ir_kint(J, 1));
    emitir(IRTGI(IR_LE), other_len, lj_ir_kint(J, 256));
    sum = lj_ir_call(J, IRCALL_lj_str_manual_find_cycle_sum,
		     texts, other_texts, idxref);
  } else {
    sum = lj_ir_call(J, IRCALL_lj_str_byte_scan_cycle_sum,
		     texts, idxref);
  }
  emitir(IRTGI(IR_NE), sum, lj_ir_kint(J, INT32_MIN));
  sum = emitir(IRTGI(IR_ADDOV), acc, sum);

  J->base[total_slot] = sum;
  if (total_slot >= J->maxslot)
    J->maxslot = total_slot + 1;
  J->pc = forl + 1;
  lj_record_stop(J, LJ_TRLINK_INTERP, 0);
  return 1;
}
#endif

#if LJ_RECORD_S390X_NUMERIC_MOD_REDUCERS
static int lj_record_s390x_mod_select_loop_sum(jit_State *J, const BCIns *body)
{
  const BCIns *forl, *proto;
  BCIns mod, isn, jmp1, thenop, jmp2, elseop;
  BCReg forbase, idxslot, tmp, accslot;
  TRef idx, acc, sum;
  cTValue *base;
  const int32_t *prefix;
  int32_t modk, stopv, then_mul, else_mul;
  BCOp thenbc, elsebc;

  if (!lj_record_s390x_mod_select_loop_sum_enabled() ||
      !lj_record_s390x_root_frame(J) || J->pt == NULL ||
      J->parent != 0 || J->exitno != 0)
    return 0;
  proto = proto_bc(J->pt);
  if (body < proto + 5 ||
      (MSize)((body + 6) - proto) >= J->pt->sizebc)
    return 0;

  if (bc_op(body[0]) == BC_MOV) {
    BCIns mov, mul;
    BCReg valslot;
    int32_t mulk;
    if ((MSize)((body + 6) - proto) >= J->pt->sizebc)
      return 0;
    mov = body[0];
    mod = body[1];
    isn = body[2];
    jmp1 = body[3];
    mul = body[4];
    thenop = body[5];
    forl = body + 6;
    if (bc_op(mod) != BC_MODVN || bc_op(isn) != BC_ISNEN ||
	bc_op(jmp1) != BC_JMP || bc_op(mul) != BC_MULVN ||
	bc_op(thenop) != BC_ADDVV ||
	(bc_op(*forl) != BC_FORL && bc_op(*forl) != BC_JFORL))
      return 0;
    forbase = bc_a(*forl);
    idxslot = forbase + FORL_EXT;
    valslot = bc_a(mov);
    tmp = bc_a(mod);
    accslot = bc_b(thenop);
    if (!lj_record_s390x_knum_get_int(J->pt, bc_c(mod), &modk) ||
	!lj_record_s390x_knum_get_int(J->pt, bc_c(mul), &mulk) ||
	modk < 2 || modk > 32767 || mulk < 2 || mulk > 32767 ||
	bc_d(mov) != idxslot || bc_b(mod) != idxslot ||
	bc_a(isn) != tmp || !lj_record_s390x_knum_is_int(J->pt, bc_d(isn), 0) ||
	body + 4 + bc_j(jmp1) != body + 5 ||
	bc_a(mul) != valslot || bc_b(mul) != valslot ||
	bc_a(thenop) != accslot || bc_b(thenop) != accslot ||
	bc_c(thenop) != valslot ||
	tmp == idxslot || tmp == valslot || tmp == accslot ||
	valslot == idxslot || valslot == accslot || idxslot == accslot)
      return 0;
    then_mul = mulk;
    else_mul = 1;
  } else if (bc_op(body[0]) == BC_MODVN && bc_op(body[3]) == BC_MULVN) {
    BCIns mul, addmul, addidx;
    int32_t mulk;
    if ((MSize)((body + 7) - proto) >= J->pt->sizebc)
      return 0;
    mod = body[0];
    isn = body[1];
    jmp1 = body[2];
    mul = body[3];
    addmul = body[4];
    jmp2 = body[5];
    addidx = body[6];
    forl = body + 7;
    if (bc_op(isn) != BC_ISNEN || bc_op(jmp1) != BC_JMP ||
	bc_op(mul) != BC_MULVN || bc_op(addmul) != BC_ADDVV ||
	bc_op(jmp2) != BC_JMP || bc_op(addidx) != BC_ADDVV ||
	(bc_op(*forl) != BC_FORL && bc_op(*forl) != BC_JFORL))
      return 0;
    forbase = bc_a(*forl);
    idxslot = forbase + FORL_EXT;
    tmp = bc_a(mod);
    accslot = bc_b(addmul);
    if (!lj_record_s390x_knum_get_int(J->pt, bc_c(mod), &modk) ||
	!lj_record_s390x_knum_get_int(J->pt, bc_c(mul), &mulk) ||
	modk < 2 || modk > 32767 || mulk < 2 || mulk > 32767 ||
	bc_b(mod) != idxslot ||
	bc_a(isn) != tmp || !lj_record_s390x_knum_is_int(J->pt, bc_d(isn), 0) ||
	body + 3 + bc_j(jmp1) != body + 6 ||
	bc_a(mul) != tmp || bc_b(mul) != idxslot ||
	bc_a(addmul) != accslot || bc_b(addmul) != accslot ||
	bc_c(addmul) != tmp ||
	body + 6 + bc_j(jmp2) != body + 7 ||
	bc_a(addidx) != accslot || bc_b(addidx) != accslot ||
	bc_c(addidx) != idxslot ||
	tmp == idxslot || tmp == accslot || idxslot == accslot)
      return 0;
    then_mul = mulk;
    else_mul = 1;
  } else {
    mod = body[0];
    isn = body[1];
    jmp1 = body[2];
    thenop = body[3];
    jmp2 = body[4];
    elseop = body[5];
    forl = body + 6;
    thenbc = bc_op(thenop);
    elsebc = bc_op(elseop);
    if (bc_op(mod) != BC_MODVN || bc_op(isn) != BC_ISNEN ||
	bc_op(jmp1) != BC_JMP ||
	!((thenbc == BC_ADDVV || thenbc == BC_SUBVV) &&
	  (elsebc == BC_ADDVV || elsebc == BC_SUBVV)) ||
	bc_op(jmp2) != BC_JMP ||
	(bc_op(*forl) != BC_FORL && bc_op(*forl) != BC_JFORL))
      return 0;

    forbase = bc_a(*forl);
    idxslot = forbase + FORL_EXT;
    tmp = bc_a(mod);
    accslot = bc_b(thenop);
    if (!lj_record_s390x_knum_get_int(J->pt, bc_c(mod), &modk) ||
	modk < 2 || modk > 32767)
      return 0;
    if (bc_b(mod) != idxslot ||
	bc_a(isn) != tmp || !lj_record_s390x_knum_is_int(J->pt, bc_d(isn), 0) ||
	body + 3 + bc_j(jmp1) != body + 5 ||
	bc_a(thenop) != accslot || bc_b(thenop) != accslot ||
	bc_c(thenop) != idxslot ||
	body + 5 + bc_j(jmp2) != body + 6 ||
	bc_a(elseop) != accslot || bc_b(elseop) != accslot ||
	bc_c(elseop) != idxslot ||
	tmp == idxslot || tmp == accslot || idxslot == accslot)
      return 0;
    then_mul = thenbc == BC_ADDVV ? 1 : -1;
    else_mul = elsebc == BC_ADDVV ? 1 : -1;
  }
  if (!lj_record_s390x_guard_for_idx_ge1(J, idxslot))
    return 0;

  base = J->L->base;
  if (!tvisint(&base[forbase+FORL_STOP]) ||
      !tvisint(&base[forbase+FORL_STEP]) ||
      intV(&base[forbase+FORL_STEP]) != 1)
    return 0;
  stopv = intV(&base[forbase+FORL_STOP]);
  if (stopv < 1 || stopv > 1000000)
    return 0;
  if (!lj_record_s390x_mod_select_sum_fits_i32(stopv, modk, then_mul, else_mul))
    return 0;
  if (!lj_record_s390x_guard_for_stop(J, forbase, stopv))
    return 0;

  idx = getslot(J, idxslot);
  acc = getslot(J, accslot);
  if (!tref_isinteger(idx) || !(tref_isinteger(acc) || tref_isnum(acc)))
    return 0;
  prefix = lj_record_s390x_mod_expr_prefix_table(
      LJ_RECORD_S390X_MOD_EXPR_SELECT, modk, then_mul, else_mul, 0, 0, 0,
      stopv);
  if (prefix == NULL)
    return 0;
  sum = lj_record_s390x_mod_prefix_span_sum(J, prefix, stopv, idx);
  if (tref_isinteger(acc)) {
    sum = emitir(IRTGI(IR_ADDOV), acc, sum);
  } else {
    sum = emitir(IRTN(IR_CONV), sum, IRCONV_NUM_INT);
    sum = emitir(IRTN(IR_ADD), acc, sum);
  }

  J->base[accslot] = sum;
  if (accslot >= J->maxslot)
    J->maxslot = accslot + 1;
  J->pc = forl + 1;
  lj_record_stop(J, LJ_TRLINK_INTERP, 0);
  return 1;
}

static int lj_record_s390x_mod_rem_select_loop_sum(jit_State *J,
						   const BCIns *body)
{
  const BCIns *forl, *proto;
  BCIns thenop, elseop;
  BCReg forbase, idxslot, accslot;
  TRef idx, acc, sum;
  cTValue *base;
  const int32_t *prefix;
  int32_t cond_mod, rem_mod, then_mul = 0, else_mul = 0, stopv;
  int32_t cond_mod2 = 0;
  int32_t const_k = 0;
  int const_on_then = 0;
  int nested_else_if = 0;
  BCOp thenbc = BC__MAX, elsebc = BC__MAX;

  if (!lj_record_s390x_mod_rem_select_loop_sum_enabled() ||
      !lj_record_s390x_root_frame(J) || J->pt == NULL ||
      J->parent != 0 || J->exitno != 0)
    return 0;
  proto = proto_bc(J->pt);
  if (body < proto + 5)
    return 0;

  if (bc_op(body[0]) == BC_MODVN && bc_op(body[7]) == BC_MODVN &&
      bc_op(body[10]) == BC_MODVN) {
    BCIns modif1, isn1, jmp1, modthen, multhen, addthen, jmpjoin;
    BCIns modif2, isn2, jmp2, modinner, subinner, jmpinner, constop;
    BCReg tmp;
    if ((MSize)((body + 14) - proto) >= J->pt->sizebc)
      return 0;
    modif1 = body[0]; isn1 = body[1]; jmp1 = body[2];
    modthen = body[3]; multhen = body[4]; addthen = body[5];
    jmpjoin = body[6]; modif2 = body[7]; isn2 = body[8];
    jmp2 = body[9]; modinner = body[10]; subinner = body[11];
    jmpinner = body[12]; constop = body[13]; forl = body + 14;
    if (bc_op(isn1) != BC_ISNEN || bc_op(jmp1) != BC_JMP ||
	bc_op(modthen) != BC_MODVN || bc_op(multhen) != BC_MULVN ||
	bc_op(addthen) != BC_ADDVV || bc_op(jmpjoin) != BC_JMP ||
	bc_op(isn2) != BC_ISNEN || bc_op(jmp2) != BC_JMP ||
	bc_op(subinner) != BC_SUBVV || bc_op(jmpinner) != BC_JMP ||
	bc_op(constop) != BC_ADDVN ||
	(bc_op(*forl) != BC_FORL && bc_op(*forl) != BC_JFORL))
      return 0;
    forbase = bc_a(*forl);
    tmp = bc_a(modif1);
    idxslot = bc_b(modif1);
    accslot = bc_b(addthen);
    if (idxslot != forbase + FORL_EXT ||
	!lj_record_s390x_knum_get_int(J->pt, bc_c(modif1), &cond_mod) ||
	!lj_record_s390x_knum_get_int(J->pt, bc_c(modif2), &cond_mod2) ||
	!lj_record_s390x_knum_get_int(J->pt, bc_c(modthen), &rem_mod) ||
	!lj_record_s390x_knum_is_int(J->pt, bc_c(multhen), 3) ||
	!lj_record_s390x_knum_is_int(J->pt, bc_c(constop), 1) ||
	bc_b(modif2) != idxslot ||
	tmp == idxslot || tmp == accslot || idxslot == accslot ||
	bc_a(isn1) != tmp || !lj_record_s390x_knum_is_int(J->pt, bc_d(isn1), 0) ||
	body + 3 + bc_j(jmp1) != body + 7 ||
	bc_a(modthen) != tmp || bc_b(modthen) != idxslot ||
	bc_a(multhen) != tmp || bc_b(multhen) != tmp ||
	bc_a(addthen) != accslot || bc_b(addthen) != accslot ||
	bc_c(addthen) != tmp ||
	body + 7 + bc_j(jmpjoin) != body + 14 ||
	bc_a(modif2) != tmp ||
	bc_a(isn2) != tmp || !lj_record_s390x_knum_is_int(J->pt, bc_d(isn2), 0) ||
	body + 10 + bc_j(jmp2) != body + 13 ||
	bc_a(modinner) != tmp || bc_b(modinner) != idxslot ||
	bc_c(modinner) != bc_c(modthen) ||
	bc_a(subinner) != accslot || bc_b(subinner) != accslot ||
	bc_c(subinner) != tmp ||
	body + 13 + bc_j(jmpinner) != body + 14 ||
	bc_a(constop) != accslot || bc_b(constop) != accslot)
      return 0;
    then_mul = 3;
    else_mul = -1;
    const_k = 1;
    nested_else_if = 1;
  } else if (bc_op(body[0]) == BC_MODVN && bc_op(body[3]) == BC_MODVN &&
      (bc_op(body[6]) == BC_ADDVN || bc_op(body[6]) == BC_SUBVN)) {
    BCIns modif, isn, jmp1, modthen, jmp2, constop;
    BCReg tmp;
    if ((MSize)((body + 7) - proto) >= J->pt->sizebc)
      return 0;
    modif = body[0]; isn = body[1]; jmp1 = body[2];
    modthen = body[3]; thenop = body[4]; jmp2 = body[5];
    constop = body[6]; forl = body + 7;
    thenbc = bc_op(thenop);
    elsebc = bc_op(constop);
    if (bc_op(isn) != BC_ISNEN || bc_op(jmp1) != BC_JMP ||
	bc_op(modthen) != BC_MODVN ||
	!(thenbc == BC_ADDVV || thenbc == BC_SUBVV) ||
	!(elsebc == BC_ADDVN || elsebc == BC_SUBVN) ||
	bc_op(jmp2) != BC_JMP ||
	(bc_op(*forl) != BC_FORL && bc_op(*forl) != BC_JFORL))
      return 0;
    forbase = bc_a(*forl);
    tmp = bc_a(modif);
    idxslot = bc_b(modif);
    accslot = bc_b(thenop);
    if (idxslot != forbase + FORL_EXT ||
	!lj_record_s390x_knum_get_int(J->pt, bc_c(modif), &cond_mod) ||
	!lj_record_s390x_knum_get_int(J->pt, bc_c(modthen), &rem_mod) ||
	!lj_record_s390x_knum_get_int(J->pt, bc_c(constop), &const_k) ||
	tmp == idxslot || tmp == accslot || idxslot == accslot ||
	bc_a(isn) != tmp ||
	!lj_record_s390x_knum_is_int(J->pt, bc_d(isn), 0) ||
	body + 3 + bc_j(jmp1) != body + 6 ||
	bc_a(modthen) != tmp || bc_b(modthen) != idxslot ||
	bc_a(thenop) != accslot || bc_b(thenop) != accslot ||
	bc_c(thenop) != tmp ||
	body + 6 + bc_j(jmp2) != body + 7 ||
	bc_a(constop) != accslot || bc_b(constop) != accslot)
      return 0;
    then_mul = thenbc == BC_ADDVV ? 1 : -1;
    else_mul = 0;
    if (bc_op(constop) == BC_SUBVN)
      const_k = -const_k;
  } else if (bc_op(body[0]) == BC_MODVN && bc_op(body[3]) == BC_MODVN) {
    BCIns modif, isn, jmp1, modthen, jmp2, modelse;
    BCReg tmp;
    if ((MSize)((body + 8) - proto) >= J->pt->sizebc)
      return 0;
    modif = body[0]; isn = body[1]; jmp1 = body[2];
    modthen = body[3]; thenop = body[4]; jmp2 = body[5];
    modelse = body[6]; elseop = body[7]; forl = body + 8;
    thenbc = bc_op(thenop);
    elsebc = bc_op(elseop);
    if (bc_op(isn) != BC_ISNEN || bc_op(jmp1) != BC_JMP ||
	bc_op(modthen) != BC_MODVN ||
	!((thenbc == BC_ADDVV || thenbc == BC_SUBVV) &&
	  (elsebc == BC_ADDVV || elsebc == BC_SUBVV)) ||
	bc_op(jmp2) != BC_JMP || bc_op(modelse) != BC_MODVN ||
	(bc_op(*forl) != BC_FORL && bc_op(*forl) != BC_JFORL))
      return 0;
    forbase = bc_a(*forl);
    tmp = bc_a(modif);
    idxslot = bc_b(modif);
    accslot = bc_b(thenop);
    if (idxslot != forbase + FORL_EXT ||
	!lj_record_s390x_knum_get_int(J->pt, bc_c(modif), &cond_mod) ||
	!lj_record_s390x_knum_get_int(J->pt, bc_c(modthen), &rem_mod) ||
	tmp == idxslot || tmp == accslot || idxslot == accslot ||
	bc_a(isn) != tmp ||
	!lj_record_s390x_knum_is_int(J->pt, bc_d(isn), 0) ||
	body + 3 + bc_j(jmp1) != body + 6 ||
	bc_a(modthen) != tmp || bc_b(modthen) != idxslot ||
	bc_a(thenop) != accslot || bc_b(thenop) != accslot ||
	bc_c(thenop) != tmp ||
	body + 6 + bc_j(jmp2) != body + 8 ||
	bc_a(modelse) != tmp || bc_b(modelse) != idxslot ||
	bc_c(modelse) != bc_c(modthen) ||
	bc_a(elseop) != accslot || bc_b(elseop) != accslot ||
	bc_c(elseop) != tmp)
      return 0;
  } else if (bc_op(body[0]) == BC_MODVN && bc_op(body[1]) == BC_MODVN) {
    BCIns modrem, modif, isn, jmp1, jmp2;
    BCReg remtmp, iftmp;
    if ((MSize)((body + 7) - proto) >= J->pt->sizebc)
      return 0;
    modrem = body[0]; modif = body[1]; isn = body[2];
    jmp1 = body[3]; thenop = body[4]; jmp2 = body[5];
    elseop = body[6]; forl = body + 7;
    thenbc = bc_op(thenop);
    elsebc = bc_op(elseop);
    if (bc_op(isn) != BC_ISNEN || bc_op(jmp1) != BC_JMP ||
	!((thenbc == BC_ADDVV || thenbc == BC_SUBVV) &&
	  (elsebc == BC_ADDVV || elsebc == BC_SUBVV)) ||
	bc_op(jmp2) != BC_JMP ||
	(bc_op(*forl) != BC_FORL && bc_op(*forl) != BC_JFORL))
      return 0;
    forbase = bc_a(*forl);
    idxslot = bc_b(modrem);
    remtmp = bc_a(modrem);
    iftmp = bc_a(modif);
    accslot = bc_b(thenop);
    if (idxslot != forbase + FORL_EXT ||
	bc_b(modif) != idxslot ||
	!lj_record_s390x_knum_get_int(J->pt, bc_c(modif), &cond_mod) ||
	!lj_record_s390x_knum_get_int(J->pt, bc_c(modrem), &rem_mod) ||
	remtmp == idxslot || remtmp == iftmp || remtmp == accslot ||
	iftmp == idxslot || iftmp == accslot || idxslot == accslot ||
	bc_a(isn) != iftmp ||
	!lj_record_s390x_knum_is_int(J->pt, bc_d(isn), 0) ||
	body + 4 + bc_j(jmp1) != body + 6 ||
	bc_a(thenop) != accslot || bc_b(thenop) != accslot ||
	bc_c(thenop) != remtmp ||
	body + 6 + bc_j(jmp2) != body + 7 ||
	bc_a(elseop) != accslot || bc_b(elseop) != accslot ||
	bc_c(elseop) != remtmp)
      return 0;
  } else {
    return 0;
  }
  if (cond_mod < 2 || cond_mod > 32767 || rem_mod < 2 || rem_mod > 4096)
    return 0;
  if (nested_else_if && (cond_mod2 < 2 || cond_mod2 > 32767))
    return 0;
  if (!lj_record_s390x_guard_for_idx_ge1(J, idxslot))
    return 0;

  base = J->L->base;
  if (!tvisint(&base[forbase+FORL_STOP]) ||
      !tvisint(&base[forbase+FORL_STEP]) ||
      intV(&base[forbase+FORL_STEP]) != 1)
    return 0;
  stopv = intV(&base[forbase+FORL_STOP]);
  if (stopv < 1 || stopv > 1000000)
    return 0;
  if (nested_else_if) {
    if (!lj_record_s390x_mod_rem_nested_const_sum_fits_i32(stopv, cond_mod,
							   cond_mod2, rem_mod,
							   then_mul, else_mul,
							   const_k))
      return 0;
  } else if (const_k != 0) {
    if (!lj_record_s390x_mod_rem_const_select_sum_fits_i32(stopv, cond_mod,
							   rem_mod, then_mul,
							   const_k,
							   const_on_then))
      return 0;
  } else {
    then_mul = thenbc == BC_ADDVV ? 1 : -1;
    else_mul = elsebc == BC_ADDVV ? 1 : -1;
    if (!lj_record_s390x_mod_rem_select_sum_fits_i32(stopv, cond_mod, rem_mod,
						     then_mul, else_mul))
      return 0;
  }
  if (!lj_record_s390x_guard_for_stop(J, forbase, stopv))
    return 0;

  idx = getslot(J, idxslot);
  acc = getslot(J, accslot);
  if (!tref_isinteger(idx) || !(tref_isinteger(acc) || tref_isnum(acc)))
    return 0;
  if (nested_else_if) {
    prefix = lj_record_s390x_mod_expr_prefix_table(
	LJ_RECORD_S390X_MOD_EXPR_REM_NESTED, cond_mod, cond_mod2, rem_mod,
	then_mul, else_mul, const_k, stopv);
  } else if (const_k != 0) {
    prefix = lj_record_s390x_mod_expr_prefix_table(
	LJ_RECORD_S390X_MOD_EXPR_REM_CONST, cond_mod, rem_mod, then_mul,
	const_k, const_on_then, 0, stopv);
  } else {
    prefix = lj_record_s390x_mod_expr_prefix_table(
	LJ_RECORD_S390X_MOD_EXPR_REM_SELECT, cond_mod, rem_mod, then_mul,
	else_mul, 0, 0, stopv);
  }
  if (prefix == NULL)
    return 0;
  sum = lj_record_s390x_mod_prefix_span_sum(J, prefix, stopv, idx);
  if (tref_isinteger(acc)) {
    sum = emitir(IRTGI(IR_ADDOV), acc, sum);
  } else {
    sum = emitir(IRTN(IR_CONV), sum, IRCONV_NUM_INT);
    sum = emitir(IRTN(IR_ADD), acc, sum);
  }

  J->base[accslot] = sum;
  if (accslot >= J->maxslot)
    J->maxslot = accslot + 1;
  J->pc = forl + 1;
  lj_record_stop(J, LJ_TRLINK_INTERP, 0);
  return 1;
}

static int lj_record_s390x_mod_accum_loop_sum(jit_State *J, const BCIns *body)
{
  const BCIns *forl, *proto;
  BCIns mod, mul, accop;
  BCReg forbase, idxslot, tmp, accslot;
  TRef idx, stopref, acc, sum;
  cTValue *base;
  const int32_t *prefix;
  int32_t modk, mulk, stopv;
  BCOp accbc;
  int has_mul = 0;

  if (!lj_record_s390x_root_frame(J) || J->pt == NULL ||
      J->parent != 0 || J->exitno != 0)
    return 0;
  proto = proto_bc(J->pt);
  if (body < proto + 5)
    return 0;

  mod = body[0];
  if (bc_op(mod) != BC_MODVN)
    return 0;

  if ((MSize)((body + 3) - proto) < J->pt->sizebc &&
      bc_op(body[1]) == BC_MULVN &&
      (bc_op(body[2]) == BC_ADDVV || bc_op(body[2]) == BC_SUBVV) &&
      (bc_op(body[3]) == BC_FORL || bc_op(body[3]) == BC_JFORL)) {
    if (!lj_record_s390x_mod_scaled_loop_sum_enabled())
      return 0;
    has_mul = 1;
    mul = body[1];
    accop = body[2];
    forl = body + 3;
  } else if ((MSize)((body + 2) - proto) < J->pt->sizebc &&
	     (bc_op(body[1]) == BC_ADDVV || bc_op(body[1]) == BC_SUBVV) &&
	     (bc_op(body[2]) == BC_FORL || bc_op(body[2]) == BC_JFORL)) {
    if (!lj_record_s390x_mod_loop_sum_enabled())
      return 0;
    mul = 0;
    accop = body[1];
    forl = body + 2;
  } else {
    return 0;
  }

  accbc = bc_op(accop);
  forbase = bc_a(*forl);
  tmp = bc_a(mod);
  idxslot = bc_b(mod);
  accslot = bc_b(accop);
  if (idxslot != forbase + FORL_EXT ||
      tmp == idxslot || tmp == accslot || idxslot == accslot ||
      !lj_record_s390x_knum_get_int(J->pt, bc_c(mod), &modk) ||
      modk < 2 || modk > 4096 ||
      bc_a(accop) != accslot || bc_b(accop) != accslot ||
      bc_c(accop) != tmp)
    return 0;

  if (has_mul) {
    if (!lj_record_s390x_knum_get_int(J->pt, bc_c(mul), &mulk) ||
	mulk < 2 || mulk > 32767 ||
	bc_a(mul) != tmp || bc_b(mul) != tmp)
      return 0;
  } else {
    if (!lj_record_s390x_guard_for_idx_ge1(J, idxslot))
      return 0;
    mulk = 1;
  }

  if (accbc == BC_SUBVV)
    mulk = -mulk;

  base = J->L->base;
  if (!tvisint(&base[forbase+FORL_STOP]) ||
      !tvisint(&base[forbase+FORL_STEP]) ||
      intV(&base[forbase+FORL_STEP]) != 1)
    return 0;
  stopv = intV(&base[forbase+FORL_STOP]);
  if (stopv < 1 || stopv > 1000000)
    return 0;
  if (has_mul) {
    if (!lj_record_s390x_mod_scaled_sum_fits_i32(stopv, modk, mulk))
      return 0;
  } else {
    if (!lj_record_s390x_mod_sum_fits_i32(stopv, modk))
      return 0;
  }
  if (!lj_record_s390x_guard_for_stop(J, forbase, stopv))
    return 0;

  acc = getslot(J, accslot);
  if (!(tref_isinteger(acc) || tref_isnum(acc)))
    return 0;
  idx = getslot(J, idxslot);
  stopref = getslot(J, forbase+FORL_STOP);
  if (!tref_isinteger(idx) || !tref_isinteger(stopref))
    return 0;
  prefix = lj_record_s390x_mod_prefix_table(modk, stopv);
  if (prefix == NULL)
    return 0;
  if (has_mul)
    emitir(IRTGI(IR_GE), idx, lj_ir_kint(J, 1));
  sum = lj_record_s390x_mod_prefix_span_sum(J, prefix, stopv, idx);
  if (mulk != 1)
    sum = emitir(IRTGI(IR_MULOV), sum, lj_ir_kint(J, mulk));
  sum = emitir(IRTN(IR_CONV), sum, IRCONV_NUM_INT);
  if (tref_isinteger(acc))
    acc = emitir(IRTN(IR_CONV), acc, IRCONV_NUM_INT);
  sum = emitir(IRTN(IR_ADD), acc, sum);

  J->base[accslot] = sum;
  if (accslot >= J->maxslot)
    J->maxslot = accslot + 1;
  J->pc = forl + 1;
  lj_record_stop(J, LJ_TRLINK_INTERP, 0);
  return 1;
}

static int lj_record_s390x_mod_branch_ifconv(jit_State *J, const BCIns *pc,
					     TRef remref, TRef zeroref)
{
  const BCIns *elsepc, *joinpc;
  BCIns jmp1, modsub, sub, jmp2, modadd, add;
  BCReg tmp, idxslot, accslot;
  TRef idx, acc, mod, mask, delta;

  if (!lj_record_s390x_mod_branch_ifconv_enabled() ||
      !lj_record_s390x_root_frame(J) || J->pt == NULL ||
      J->parent != 0 || J->exitno != 0 ||
      bc_op(*pc) != BC_ISNEN || !tref_isinteger(remref) ||
      !lj_record_s390x_kint_is(J, zeroref, 0))
    return 0;

  jmp1 = pc[1];
  if (bc_op(jmp1) != BC_JMP)
    return 0;
  elsepc = pc + 2 + bc_j(jmp1);
  if (elsepc != pc + 5)
    return 0;

  modsub = pc[2];
  sub = pc[3];
  jmp2 = pc[4];
  modadd = pc[5];
  add = pc[6];
  if (bc_op(modsub) != BC_MODVN || bc_op(sub) != BC_SUBVV ||
      bc_op(jmp2) != BC_JMP || bc_op(modadd) != BC_MODVN ||
      bc_op(add) != BC_ADDVV)
    return 0;
  joinpc = pc + 5 + bc_j(jmp2);
  if (joinpc != pc + 7 ||
      (bc_op(*joinpc) != BC_FORL && bc_op(*joinpc) != BC_JFORL))
    return 0;

  tmp = bc_a(modsub);
  idxslot = bc_b(modsub);
  accslot = bc_b(sub);
  if (tmp == idxslot || tmp == accslot || idxslot == accslot ||
      bc_a(sub) != accslot || bc_c(sub) != tmp ||
      bc_a(modadd) != tmp || bc_b(modadd) != idxslot ||
      bc_c(modadd) != bc_c(modsub) ||
      bc_a(add) != accslot || bc_b(add) != accslot || bc_c(add) != tmp ||
      !lj_record_s390x_knum_is_int(J->pt, bc_c(modsub), 97))
    return 0;

  idx = getslot(J, idxslot);
  acc = getslot(J, accslot);
  if (!tref_isinteger(idx) || !(tref_isinteger(acc) || tref_isnum(acc)))
    return 0;

  mod = emitir(IRTI(IR_MOD), idx, lj_ir_kint(J, 97));
  mask = emitir(IRTI(IR_SUB), remref, lj_ir_kint(J, 1));
  mask = emitir(IRTI(IR_BSAR), mask, lj_ir_kint(J, 31));
  delta = emitir(IRTI(IR_BXOR), mod, mask);
  delta = emitir(IRTI(IR_SUB), delta, mask);
  delta = emitir(IRTN(IR_CONV), delta, IRCONV_NUM_INT);
  if (tref_isinteger(acc))
    acc = emitir(IRTN(IR_CONV), acc, IRCONV_NUM_INT);
  acc = emitir(IRTN(IR_ADD), acc, delta);

  J->base[accslot] = acc;
  if (accslot >= J->maxslot)
    J->maxslot = accslot + 1;
  if (tmp < J->maxslot)
    J->base[tmp] = 0;
  if (bc_a(jmp1) < J->maxslot)
    J->maxslot = bc_a(jmp1);
  J->bcskip = 2;  /* Skip the executed MODVN and ADD/SUB arm. */
  return 1;
}
#endif

/* Canonicalize slots: convert integers to numbers. */
static void canonicalize_slots(jit_State *J)
{
  BCReg s;
  if (LJ_DUALNUM) return;
  for (s = J->baseslot+J->maxslot-1; s >= 1; s--) {
    TRef tr = J->slot[s];
    if (tref_isinteger(tr) && !(tr & TREF_KEYINDEX)) {
      IRIns *ir = IR(tref_ref(tr));
      if (!(ir->o == IR_SLOAD && (ir->op2 & (IRSLOAD_READONLY))))
	J->slot[s] = emitir(IRTN(IR_CONV), tr, IRCONV_NUM_INT);
    }
  }
}

#if LJ_TARGET_S390X
static void lj_record_s390x_lleave_log(jit_State *J, const char *site);
#endif

/* Stop recording. */
void lj_record_stop(jit_State *J, TraceLink linktype, TraceNo lnk)
{
#ifdef LUAJIT_ENABLE_TABLE_BUMP
  if (J->retryrec)
    lj_trace_err(J, LJ_TRERR_RETRY);
#endif
  if (lj_record_s390x_stop_log_enabled()) {
    BCOp prevop = J->pc > proto_bc(J->pt) ? bc_op(J->pc[-1]) : BC__MAX;
    BCOp lnkop = lnk ? bc_op(traceref(J, lnk)->startins) : BC__MAX;
    fprintf(stderr,
	    "S390X_RECSTOP trace=%u parent=%u exit=%u pc=%p op=%u prevop=%u startop=%u linktype=%u link=%u lnkop=%u root=%u framedepth=%u retdepth=%u\n",
	    (unsigned int)J->cur.traceno, (unsigned int)J->parent,
	    (unsigned int)J->exitno, (const void *)J->pc,
	    (unsigned int)bc_op(*J->pc), (unsigned int)prevop,
	    (unsigned int)bc_op(J->cur.startins),
	    (unsigned int)linktype, (unsigned int)lnk, (unsigned int)lnkop,
	    (unsigned int)J->cur.root, (unsigned int)J->framedepth,
	    (unsigned int)J->retdepth);
  }
#if LJ_TARGET_S390X
  if (J->parent != 0 &&
      J->exitno == 0 &&
      J->cur.root != 0 &&
      J->parent != J->cur.root &&
      lnk != 0 &&
      lnk != J->cur.root &&
      linktype == LJ_TRLINK_ROOT &&
      bc_op(J->cur.startins) == BC_JMP &&
      J->pc > proto_bc(J->pt) &&
      bc_op(J->pc[-1]) == BC_JFORI &&
      bc_d(J->pc[bc_j(J->pc[-1])-1]) == lnk) {
    GCtrace *parentT = traceref(J, J->parent);
    if (J->exitno < parentT->nsnap &&
	parentT->root == J->cur.root &&
	parentT->linktype == LJ_TRLINK_ROOT &&
	parentT->link == lnk &&
	parentT->snap[J->exitno].nent == 0) {
      parentT->snap[J->exitno].count = SNAPCOUNT_DONE;
      lj_record_s390x_lleave_log(J, "record_stop_exit0_dup_root_bridge_descendant");
      lj_trace_err(J, LJ_TRERR_LLEAVE);
    }
  }
#endif
  lj_record_s390x_ir_log(J, linktype, lnk);
#if LJ_TARGET_S390X
  if (J->s390x_nil_restart_desc &&
      lj_record_s390x_mark_nil_desc_done_enabled() &&
      J->cur.nsnap > 1) {
    J->cur.snap[1].count = SNAPCOUNT_DONE;
    if (lj_record_s390x_stop_log_enabled()) {
      fprintf(stderr,
	      "S390X_NILRESTART_DONE trace=%u parent=%u exit=%u nsnap=%u site=record_stop\n",
	      (unsigned int)J->cur.traceno, (unsigned int)J->parent,
	      (unsigned int)J->exitno, (unsigned int)J->cur.nsnap);
    }
  }
  J->s390x_nil_restart_desc = 0;
#endif
  lj_trace_end(J);
  J->cur.linktype = (uint8_t)linktype;
  J->cur.link = (uint16_t)lnk;
  /* Looping back at the same stack level? */
  if (lnk == J->cur.traceno && J->framedepth + J->retdepth == 0) {
    if ((J->flags & JIT_F_OPT_LOOP))  /* Shall we try to create a loop? */
      goto nocanon;  /* Do not canonicalize or we lose the narrowing. */
    if (J->cur.root)  /* Otherwise ensure we always link to the root trace. */
      J->cur.link = J->cur.root;
  }
  canonicalize_slots(J);
nocanon:
  /* Note: all loop ops must set J->pc to the following instruction! */
  lj_snap_add(J);  /* Add loop snapshot. */
  J->needsnap = 0;
  J->mergesnap = 1;  /* In case recording continues. */
}

/* Search bytecode backwards for a int/num constant slot initializer. */
static TRef find_kinit(jit_State *J, const BCIns *endpc, BCReg slot, IRType t)
{
  /* This algorithm is rather simplistic and assumes quite a bit about
  ** how the bytecode is generated. It works fine for FORI initializers,
  ** but it won't necessarily work in other cases (e.g. iterator arguments).
  ** It doesn't do anything fancy, either (like backpropagating MOVs).
  */
  const BCIns *pc, *startpc = proto_bc(J->pt);
  for (pc = endpc-1; pc > startpc; pc--) {
    BCIns ins = *pc;
    BCOp op = bc_op(ins);
    /* First try to find the last instruction that stores to this slot. */
    if (bcmode_a(op) == BCMbase && bc_a(ins) <= slot) {
      return 0;  /* Multiple results, e.g. from a CALL or KNIL. */
    } else if (bcmode_a(op) == BCMdst && bc_a(ins) == slot) {
      if (op == BC_KSHORT || op == BC_KNUM) {  /* Found const. initializer. */
	/* Now try to verify there's no forward jump across it. */
	const BCIns *kpc = pc;
	for (; pc > startpc; pc--)
	  if (bc_op(*pc) == BC_JMP) {
	    const BCIns *target = pc+bc_j(*pc)+1;
	    if (target > kpc && target <= endpc)
	      return 0;  /* Conditional assignment. */
	  }
	if (op == BC_KSHORT) {
	  int32_t k = (int32_t)(int16_t)bc_d(ins);
	  return t == IRT_INT ? lj_ir_kint(J, k) : lj_ir_knum(J, (lua_Number)k);
	} else {
	  cTValue *tv = proto_knumtv(J->pt, bc_d(ins));
	  if (t == IRT_INT) {
	    if (tvisint(tv)) {
	      return lj_ir_kint(J, intV(tv));
	    } else {
	      int64_t i64;
	      int32_t k;
	      if (lj_num2int_check(numV(tv), i64, k))  /* -0 is ok here. */
		return lj_ir_kint(J, k);
	    }
	    return 0;  /* Type mismatch. */
	  } else {
	    return lj_ir_knum(J, numberVnum(tv));
	  }
	}
      }
      return 0;  /* Non-constant initializer. */
    }
  }
  return 0;  /* No assignment to this slot found? */
}

/* Load and optionally convert a FORI argument from a slot. */
static TRef fori_load(jit_State *J, BCReg slot, IRType t, int mode)
{
  int conv = (tvisint(&J->L->base[slot]) != (t==IRT_INT)) ? IRSLOAD_CONVERT : 0;
  return sloadt(J, (int32_t)slot,
		t + (((mode & IRSLOAD_TYPECHECK) ||
		      (conv && t == IRT_INT && !(mode >> 16))) ?
		     IRT_GUARD : 0),
		mode + conv);
}

/* Convert FORI argument to expected target type. */
static TRef fori_conv(jit_State *J, TRef tr, IRType t)
{
  if (t == IRT_INT) {
    if (!tref_isinteger(tr))
      return emitir(IRTGI(IR_CONV), tr, IRCONV_INT_NUM|IRCONV_CHECK);
  } else {
    if (!tref_isnum(tr))
      return emitir(IRTN(IR_CONV), tr, IRCONV_NUM_INT);
  }
  return tr;
}

static int fori_inherited_ref(jit_State *J, TRef tr)
{
  IRRef ref = tref_ref(tr);
  IRIns *ir;
  if (!ref || irref_isk(ref))
    return 0;
  ir = IR(ref);
  if (ir->o == IR_CONV) {
    ref = ir->op1;
    if (!ref || irref_isk(ref))
      return 0;
    ir = IR(ref);
  }
  UNUSED(J);
  return ir->o == IR_SLOAD && (ir->op2 & IRSLOAD_INHERIT);
}

static int s390x_fori_force_stop_slot_enabled(void)
{
  return 1;
}

/* Peek before FORI to find a const initializer. Otherwise load from slot. */
static TRef fori_arg(jit_State *J, const BCIns *fori, BCReg slot,
		     IRType t, int mode)
{
  TRef tr = J->base[slot];
  TRef kinit = 0;
  BCReg ra = bc_a(*fori);
  int force_stop_slot = (s390x_fori_force_stop_slot_enabled() &&
			 slot == ra + FORL_STOP);
  int log_hidden = (lj_record_s390x_fori_arg_log_enabled() &&
		    slot > ra+FORL_IDX && slot <= ra+FORL_STEP);
  if (log_hidden)
    kinit = find_kinit(J, fori, slot, t);
  if (tr) {
    tr = fori_conv(J, tr, t);
  } else {
    /* Keep hidden literal-stop loops anchored to the runtime stop slot. */
    if (!force_stop_slot)
      tr = kinit;
    if (!tr && !force_stop_slot)
      tr = find_kinit(J, fori, slot, t);
    if (!tr)
      tr = fori_load(J, slot, t, mode);
  }
  if (lj_record_s390x_fori_arg_log_enabled()) {
    IRRef baseref = tref_ref(J->base[slot]);
    IRRef outref = tref_ref(tr);
    fprintf(stderr,
	    "S390X_FORI_ARG trace=%u parent=%u exit=%u startop=%u forop=%u slot=%u t=%u mode=%u base_ref=%u base_inherit=%u kinit_present=%u kinit_is_k=%u out_ref=%u out_k=%u\n",
	    (unsigned int)J->cur.traceno, (unsigned int)J->parent,
	    (unsigned int)J->exitno, (unsigned int)bc_op(J->cur.startins),
	    (unsigned int)bc_op(*fori), (unsigned int)slot, (unsigned int)t,
	    (unsigned int)mode, (unsigned int)(baseref - REF_BIAS),
	    (unsigned int)fori_inherited_ref(J, J->base[slot]),
	    (unsigned int)(kinit != 0),
	    (unsigned int)tref_isk(kinit),
	    (unsigned int)(outref - REF_BIAS),
	    (unsigned int)tref_isk(tr));
  }
  return tr;
}

/* Return the direction of the FOR loop iterator.
** It's important to exactly reproduce the semantics of the interpreter.
*/
static int rec_for_direction(cTValue *o)
{
  return (tvisint(o) ? intV(o) : (int32_t)o->u32.hi) >= 0;
}

#if LJ_TARGET_S390X
static int lj_record_s390x_kint_ref(jit_State *J, IRRef ref, int32_t *k)
{
  IRIns *ir;
  if (ref < J->cur.nk || ref >= REF_BIAS)
    return 0;
  ir = IR(ref);
  if (ir->o != IR_KINT)
    return 0;
  *k = ir->i;
  return 1;
}

static int lj_record_s390x_fori_u8histop(jit_State *J, const BCIns *fori,
					 BCReg ra, IRType t, int dir,
					 IRRef start, TRef stop, TRef step)
{
  IRRef stopref, stepref;
  TRef stopk;
  int32_t startv, stopv, stepv;
  if (t != IRT_INT || !dir || !start || !irref_isk(start) ||
      !lj_record_s390x_kint_ref(J, start, &startv) || startv < 0)
    return 0;

  stepref = tref_ref(step);
  if (!tref_isk(step) || !lj_record_s390x_kint_ref(J, stepref, &stepv) ||
      stepv != 1)
    return 0;

  stopref = tref_ref(stop);
  if (!tref_isk(stop)) {
    stopk = find_kinit(J, fori, ra+FORL_STOP, t);
    if (!tref_isk(stopk))
      return 0;
    stopref = tref_ref(stopk);
  }

  if (!lj_record_s390x_kint_ref(J, stopref, &stopv))
    return 0;
  if (stopv < startv || stopv <= 128 || stopv > 255)
    return 0;
  return stopv;
}
#endif

static int lj_record_s390x_stop_log_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_RECSTOP_LOG") != NULL);
  return enabled;
}

static int lj_record_s390x_recloop_focus_enabled(void)
{
  return 0;
}

static int lj_record_s390x_no_extra_loop_cont_stub_enabled(void)
{
  return 0;
}

static int lj_record_s390x_ir_log_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_RECIR_LOG") != NULL);
  return enabled;
}

static void lj_record_s390x_ir_log(jit_State *J, TraceLink linktype, TraceNo lnk)
{
  IRRef ref;
  if (!lj_record_s390x_ir_log_enabled())
    return;
  fprintf(stderr,
	  "S390X_RECIR_START trace=%u parent=%u exit=%u linktype=%u link=%u root=%u nk=%u nins=%u\n",
	  (unsigned int)J->cur.traceno, (unsigned int)J->parent,
	  (unsigned int)J->exitno, (unsigned int)linktype, (unsigned int)lnk,
	  (unsigned int)J->cur.root, (unsigned int)J->cur.nk,
	  (unsigned int)J->cur.nins);
  for (ref = J->cur.nk; ref < J->cur.nins; ref++) {
    IRIns *ir = IR(ref);
    fprintf(stderr,
	    "S390X_RECIR trace=%u ref=%d op=%d type=%d op1=%d op2=%d prev=%u r=%d s=%d\n",
	    (unsigned int)J->cur.traceno, (int)(ref - REF_BIAS), (int)ir->o,
	    (int)irt_type(ir->t), (int)(ir->op1 - REF_BIAS),
	    (int)(ir->op2 - REF_BIAS), (unsigned int)ir->prev,
	    (int)ir->r, (int)ir->s);
  }
  fprintf(stderr, "S390X_RECIR_END trace=%u\n", (unsigned int)J->cur.traceno);
}

static void lj_record_s390x_loopslot_log(jit_State *J, const char *site)
{
  int i;
  if (!lj_record_s390x_stop_log_enabled())
    return;
  fprintf(stderr,
	  "S390X_LOOPSLOTS site=%s trace=%u parent=%u exit=%u pc=%p op=%u startop=%u\n",
	  site, (unsigned int)J->cur.traceno, (unsigned int)J->parent,
	  (unsigned int)J->exitno, (const void *)J->pc,
	  (unsigned int)bc_op(*J->pc), (unsigned int)bc_op(J->cur.startins));
  for (i = 0; i < 6; i++) {
    TValue *o = &J->L->base[i];
    fprintf(stderr, "S390X_LOOPSLOT idx=%d itype=%d u64=0x%016llx\n",
	    i, (int)itype(o), (unsigned long long)o->u64);
  }
}

static int lj_record_s390x_recloop_exit2_enabled(void)
{
  return 0;
}

static int lj_record_s390x_allow_iter_desc_enabled(void)
{
  return 0;
}

static int lj_record_s390x_restart_desc_loop_enabled(void)
{
  return 0;
}

static int lj_record_s390x_root_itern_nil_desc_enabled(void)
{
  return 0;
}

static int lj_record_s390x_itern_hash_payload_enabled(void)
{
  return LJ_TARGET_S390X;
}

static int lj_record_s390x_mark_nil_desc_done_enabled(void)
{
  return 0;
}

static int lj_record_s390x_skip_nil_desc_done_enabled(void)
{
  return 0;
}

static int lj_record_s390x_retry_first_array_exit_enabled(void)
{
  return 0;
}

static int lj_record_s390x_retry_first_array_exit_limit(void)
{
  return 5;
}

static int lj_record_s390x_looplink_payload_desc_enabled(void)
{
  return 0;
}

static int lj_record_s390x_jfori_interp_handoff_enabled(void)
{
  return LJ_GC64;
}

static int lj_record_s390x_fori_arg_log_enabled(void)
{
  return 0;
}

static int lj_record_s390x_forl_fastpath_log_enabled(void)
{
  return 0;
}

static int lj_record_s390x_recbc_log_enabled(void)
{
  return 0;
}

static int lj_record_s390x_side_focus_enabled(void)
{
  return 0;
}

static int lj_record_s390x_side_focus_parent(void)
{
  return 4;
}

static int lj_record_s390x_side_focus_exit(void)
{
  return 1;
}

static int lj_record_s390x_itern_focus_enabled(void)
{
  return 0;
}

static int lj_record_s390x_itern_focus_parent(void)
{
  return 4;
}

static int lj_record_s390x_itern_focus_exit(void)
{
  return 1;
}

static void lj_record_s390x_setup_log(jit_State *J, const char *site)
{
  BCOp op = bc_op(*J->pc);
  BCOp prevop = J->pc > proto_bc(J->pt) ? bc_op(J->pc[-1]) : BC__MAX;
  BCOp startop = bc_op(J->cur.startins);
  if (!lj_record_s390x_stop_log_enabled())
    return;
  fprintf(stderr,
	  "S390X_RECSETUP site=%s trace=%u parent=%u exit=%u root=%u state=%u baseslot=%u maxslot=%u startpc=%p pc=%p op=%u prevop=%u startop=%u framedepth=%u retdepth=%u\n",
	  site, (unsigned int)J->cur.traceno, (unsigned int)J->parent,
	  (unsigned int)J->exitno, (unsigned int)J->cur.root,
	  (unsigned int)J->state, (unsigned int)J->baseslot,
	  (unsigned int)J->maxslot, (const void *)J->startpc,
	  (const void *)J->pc, (unsigned int)op, (unsigned int)prevop,
	  (unsigned int)startop, (unsigned int)J->framedepth,
	  (unsigned int)J->retdepth);
}

static void lj_record_s390x_lleave_log(jit_State *J, const char *site)
{
  GCtrace *parent = (J->parent > 0) ? traceref(J, J->parent) : NULL;
  SnapNo exitno = (SnapNo)J->exitno;
  uint32_t snapcount = 0;
  uint8_t snapnent = 0;
  uint16_t linktype = 0;
  uint16_t root = 0;
  if (!lj_record_s390x_stop_log_enabled())
    return;
  if (parent && exitno < parent->nsnap) {
    snapcount = parent->snap[exitno].count;
    snapnent = parent->snap[exitno].nent;
    linktype = parent->linktype;
    root = parent->root ? parent->root : parent->traceno;
  }
  fprintf(stderr,
	  "S390X_LLEAVE site=%s trace=%u parent=%u exit=%u pc=%p op=%u startop=%u framedepth=%u retdepth=%u parent_root=%u parent_linktype=%u parent_snapcount=%u parent_snapnent=%u\n",
	  site, (unsigned int)J->cur.traceno, (unsigned int)J->parent,
	  (unsigned int)J->exitno, (const void *)J->pc,
	  (unsigned int)bc_op(*J->pc), (unsigned int)bc_op(J->cur.startins),
	  (unsigned int)J->framedepth, (unsigned int)J->retdepth,
	  (unsigned int)root, (unsigned int)linktype,
	  (unsigned int)snapcount, (unsigned int)snapnent);
}

static void lj_record_s390x_linner_log(jit_State *J, const char *site,
				       LoopEvent ev, TraceNo lnk)
{
  BCOp prevop = J->pc > proto_bc(J->pt) ? bc_op(J->pc[-1]) : BC__MAX;
  if (!lj_record_s390x_stop_log_enabled())
    return;
  fprintf(stderr,
	  "S390X_LINNER site=%s trace=%u parent=%u exit=%u startpc=%p pc=%p op=%u prevop=%u startop=%u ev=%u lnk=%u root=%u framedepth=%u retdepth=%u\n",
	  site, (unsigned int)J->cur.traceno, (unsigned int)J->parent,
	  (unsigned int)J->exitno, (const void *)J->startpc,
	  (const void *)J->pc, (unsigned int)bc_op(*J->pc),
	  (unsigned int)prevop, (unsigned int)bc_op(J->cur.startins),
	  (unsigned int)ev, (unsigned int)lnk, (unsigned int)J->cur.root,
	  (unsigned int)J->framedepth, (unsigned int)J->retdepth);
}

static void lj_record_s390x_side_replay_log(jit_State *J, const char *site,
					    GCtrace *T)
{
  int i;
  BCOp prevop = J->pc > proto_bc(J->pt) ? bc_op(J->pc[-1]) : BC__MAX;
  static int focus_parent = -2;
  static int focus_exit = -2;
  if (focus_parent == -2)
    focus_parent = 1;
  if (focus_exit == -2)
    focus_exit = 1;
  if (!lj_record_s390x_stop_log_enabled() ||
      (focus_parent >= 0 && J->parent != (TraceNo)focus_parent) ||
      (focus_exit >= 0 && J->exitno != (SnapNo)focus_exit))
    return;
  fprintf(stderr,
	  "S390X_SIDE_REPLAY site=%s trace=%u parent=%u exit=%u root=%u startpc=%p pc=%p op=%u prevop=%u startop=%u parent_startop=%u parent_root=%u snapcount=%u snapnent=%u\n",
	  site, (unsigned int)J->cur.traceno, (unsigned int)J->parent,
	  (unsigned int)J->exitno, (unsigned int)J->cur.root,
	  (const void *)J->startpc, (const void *)J->pc,
	  (unsigned int)bc_op(*J->pc), (unsigned int)prevop,
	  (unsigned int)bc_op(J->cur.startins),
	  (unsigned int)bc_op(T->startins), (unsigned int)(T->root ? T->root : T->traceno),
	  (unsigned int)T->snap[J->exitno].count,
	  (unsigned int)T->snap[J->exitno].nent);
  for (i = 0; i < 6; i++) {
    TRef tr = J->base[i];
    TValue *tv = &J->L->base[i];
    fprintf(stderr,
	    "S390X_SIDE_SLOT site=%s idx=%d tref=%#x ref=%d type=%d itype=%d u64=0x%016llx\n",
	    site, i, (unsigned int)tr, (int)(tref_ref(tr) - REF_BIAS),
	    (int)tref_type(tr), (int)itype(tv), (unsigned long long)tv->u64);
  }
}

static void lj_record_s390x_side_focus_log(jit_State *J, const char *site,
					   GCtrace *T)
{
  BCOp prevop = J->pc > proto_bc(J->pt) ? bc_op(J->pc[-1]) : BC__MAX;
  SnapNo exitno = (SnapNo)J->exitno;
  uint32_t snapcount = 0;
  uint8_t snapnent = 0;
  uint32_t snapref = 0;
  const BCIns *snappc = NULL;
  unsigned int snapop = 0;
  uint32_t snapsig = 0;
  int32_t baseop1 = 0, baseop2 = 0;
  if (!lj_record_s390x_side_focus_enabled() ||
      (lj_record_s390x_side_focus_parent() >= 0 &&
       J->parent != (TraceNo)lj_record_s390x_side_focus_parent()) ||
      (lj_record_s390x_side_focus_exit() >= 0 &&
       J->exitno != (SnapNo)lj_record_s390x_side_focus_exit()))
    return;
  if (T && exitno < T->nsnap) {
    const SnapShot *snap = &T->snap[exitno];
    const SnapEntry *map = &T->snapmap[snap->mapofs];
    MSize i;
    snapcount = snap->count;
    snapnent = snap->nent;
    snapref = snap->ref;
    snappc = snap_pc((SnapEntry *)&map[snap->nent]);
    snapop = (unsigned int)(snappc ? bc_op(*snappc) : 0);
    for (i = 0; i < snap->nent; i++) {
      SnapEntry sn = map[i];
      uint32_t slot = (uint32_t)snap_slot(sn);
      uint32_t ref = (uint32_t)(snap_ref(sn) - REF_BIAS);
      uint32_t flags = 0;
      if (sn & SNAP_NORESTORE) flags |= 1u;
      if (sn & SNAP_FRAME) flags |= 2u;
      snapsig = (snapsig * 16777619u) ^ (slot + (ref << 8) + (flags << 24));
    }
    baseop1 = (int32_t)(T->ir[REF_BASE].op1 - REF_BIAS);
    baseop2 = (int32_t)(T->ir[REF_BASE].op2 - REF_BIAS);
  }
  fprintf(stderr,
	  "S390X_SIDE_FOCUS site=%s trace=%u parent=%u exit=%u root=%u parent_linktype=%u parent_link=%u parent_nchild=%u startpc=%p pc=%p op=%u prevop=%u startop=%u parent_startop=%u parent_baseop1=%d parent_baseop2=%d parent_snapcount=%u parent_snapref=%u parent_snapnent=%u parent_snapsig=%#x snappc=%p snapop=%u framedepth=%u retdepth=%u\n",
	  site, (unsigned int)J->cur.traceno, (unsigned int)J->parent,
	  (unsigned int)J->exitno, (unsigned int)(T->root ? T->root : T->traceno),
	  (unsigned int)T->linktype, (unsigned int)T->link,
	  (unsigned int)T->nchild, (const void *)J->startpc, (const void *)J->pc,
	  (unsigned int)bc_op(*J->pc), (unsigned int)prevop,
	  (unsigned int)bc_op(J->cur.startins),
	  (unsigned int)bc_op(T->startins),
	  (int)baseop1, (int)baseop2,
	  (unsigned int)snapcount, (unsigned int)snapref, (unsigned int)snapnent,
	  (unsigned int)snapsig,
	  (const void *)snappc, snapop,
	  (unsigned int)J->framedepth, (unsigned int)J->retdepth);
}

static void lj_record_s390x_itern_focus_log(jit_State *J, const char *site,
					    BCReg ra, const RecordIndex *ix,
					    IRType nextt, uint32_t keyflags)
{
  TValue *base = J->L->base;
  TValue *tab = &base[ra-2];
  TValue *ctrl = &base[ra-1];
  TValue *key = &base[ra];
  TValue *val = &base[ra+1];
  GCtab *t = tvistab(tab) ? tabV(tab) : NULL;
  if (!lj_record_s390x_itern_focus_enabled() ||
      (lj_record_s390x_itern_focus_parent() >= 0 &&
       J->parent != (TraceNo)lj_record_s390x_itern_focus_parent()) ||
      (lj_record_s390x_itern_focus_exit() >= 0 &&
       J->exitno != (SnapNo)lj_record_s390x_itern_focus_exit()))
    return;
  fprintf(stderr,
	  "S390X_ITERN_FOCUS site=%s trace=%u parent=%u exit=%u startpc=%p pc=%p op=%u startop=%u nextt=%u keyflags=0x%x numkey=%u key_nil=%u idxchain=%u mobj_ref=%d key_ref=%d val_ref=%d tab_u64=0x%016llx ctrl_u64=0x%016llx key_u64=0x%016llx val_u64=0x%016llx ix_keyv=0x%016llx ix_tabv=0x%016llx tab_asize=%u tab_hmask=%u\n",
	  site, (unsigned int)J->cur.traceno, (unsigned int)J->parent,
	  (unsigned int)J->exitno, (const void *)J->startpc, (const void *)J->pc,
	  (unsigned int)bc_op(*J->pc), (unsigned int)bc_op(J->cur.startins),
	  (unsigned int)(nextt & 0xff), (unsigned int)keyflags,
	  (unsigned int)((keyflags & IRSLOAD_KIDX_NUMKEY) != 0),
	  (unsigned int)tref_isnil(ix->key), (unsigned int)ix->idxchain,
	  (int)(tref_ref(ix->mobj) - REF_BIAS),
	  (int)(tref_ref(ix->key) - REF_BIAS),
	  (int)(tref_ref(ix->val) - REF_BIAS),
	  (unsigned long long)tab->u64, (unsigned long long)ctrl->u64,
	  (unsigned long long)key->u64, (unsigned long long)val->u64,
	  (unsigned long long)ix->keyv.u64, (unsigned long long)ix->tabv.u64,
	  (unsigned int)(t ? t->asize : 0), (unsigned int)(t ? t->hmask : 0));
  fprintf(stderr,
	  "S390X_ITERN_TREF site=%s s_ctrl=%#x s_key=%#x s_val=%#x s_ra1=%#x s_ra2=%#x\n",
	  site, (unsigned int)J->base[ra-1], (unsigned int)J->base[ra],
	  (unsigned int)J->base[ra+1], (unsigned int)J->base[1],
	  (unsigned int)J->base[2]);
}

static void lj_record_s390x_recbc_log(jit_State *J, const BCIns *pc,
				      BCIns ins, BCReg ra, BCReg rb, BCReg rc)
{
  TValue *base = J->L->base;
  BCOp prevop = pc > proto_bc(J->pt) ? bc_op(pc[-1]) : BC__MAX;
  if (!lj_record_s390x_recbc_log_enabled() || J->parent != 1 || J->exitno != 1)
    return;
  fprintf(stderr,
	  "S390X_RECBC trace=%u parent=%u exit=%u pc=%p op=%u prevop=%u startop=%u ra=%u rb=%u rc=%u s0=0x%016llx s1=0x%016llx s2=0x%016llx s3=0x%016llx s4=0x%016llx s5=0x%016llx\n",
	  (unsigned int)J->cur.traceno, (unsigned int)J->parent,
	  (unsigned int)J->exitno, (const void *)pc, (unsigned int)bc_op(ins),
	  (unsigned int)prevop, (unsigned int)bc_op(J->cur.startins),
	  (unsigned int)ra, (unsigned int)rb, (unsigned int)rc,
	  (unsigned long long)base[0].u64, (unsigned long long)base[1].u64,
	  (unsigned long long)base[2].u64, (unsigned long long)base[3].u64,
	  (unsigned long long)base[4].u64, (unsigned long long)base[5].u64);
}

static IRType rec_next_types(GCtab *t, uint32_t idx, int *isarray);
static IRType rec_next_types_idx(GCtab *t, uint32_t idx, int *isarray,
				 uint32_t *nextidx);

static TRef lj_record_s390x_pairs_tab_ref(jit_State *J, int32_t slot, GCtab *t)
{
  if ((rec_next_types(t, 0, NULL) & 0xff) != IRT_INT)
    return J->base[slot] ? J->base[slot] :
	   sloadt(J, slot, IRT_TAB, IRSLOAD_READONLY);
  return getslot(J, slot);
}

static void lj_record_s390x_itern_terminal_snapshot_preload(jit_State *J,
							    BCReg maxslot)
{
  BCReg s;
  /* Iterator terminal exits need the surrounding loop state restorable before
  ** ITERN adds its snapshot. Preload live non-primitives so restore has the
  ** table/control/key/value and outer loop slots needed by side exits.
  */
  for (s = 1; s < maxslot; s++) {
    IRType t;
    if (J->base[s])
      continue;
    t = itype2irt(&J->L->base[s]);
    if (irtype_ispri(t))
      continue;
    J->base[s] = sloadt(J, (int32_t)s, (IRType)(IRT_GUARD|t),
			IRSLOAD_TYPECHECK|IRSLOAD_INHERIT);
  }
}

static int lj_record_s390x_root_forl_array_snapshot_defer(jit_State *J)
{
#if LJ_TARGET_S390X
  const BCIns *pc = J->pc;
  BCOp startop;
  int off;
  if (J->parent != 0 || J->exitno != 0 || pc == NULL)
    return 0;
  startop = bc_op(J->cur.startins);
  if (startop != BC_FORL && startop != BC_JFORL)
    return 0;
  for (off = 0; off <= 20; off++) {
    BCReg ra = 0;
    TValue *base;
    int nextisarray = 0;
    BCOp op0 = bc_op(pc[off]);
    BCOp op1 = bc_op(pc[off+1]);
    BCOp op2 = bc_op(pc[off+2]);
    BCOp op3 = bc_op(pc[off+3]);
    BCOp op4 = bc_op(pc[off+4]);
    BCOp op5 = bc_op(pc[off+5]);
    BCOp op6 = bc_op(pc[off+6]);
    if ((op0 == BC_ITERN || op0 == BC_ITERC) &&
	((op1 == BC_ITERL || op1 == BC_IITERL || op1 == BC_JITERL) ||
	 (op2 == BC_ITERL || op2 == BC_IITERL || op2 == BC_JITERL)) &&
	(op2 == BC_FORL || op2 == BC_JFORL ||
	 op3 == BC_FORL || op3 == BC_JFORL ||
	 op4 == BC_FORL || op4 == BC_JFORL ||
	 op5 == BC_FORL || op5 == BC_JFORL))
      ra = bc_a(pc[off]);
    else if (op0 == BC_ISNEXT &&
	     (op1 == BC_ITERN || op1 == BC_ITERC) &&
	     ((op2 == BC_ITERL || op2 == BC_IITERL || op2 == BC_JITERL) ||
	      (op3 == BC_ITERL || op3 == BC_IITERL || op3 == BC_JITERL)) &&
	     (op3 == BC_FORL || op3 == BC_JFORL ||
	      op4 == BC_FORL || op4 == BC_JFORL ||
	      op5 == BC_FORL || op5 == BC_JFORL ||
	      op6 == BC_FORL || op6 == BC_JFORL))
      ra = bc_a(pc[off]);
    if (ra == 0)
      continue;
    base = J->L->base;
    if (ra < 3 || !tvistab(&base[ra-2]))
      return 0;
    rec_next_types(tabV(&base[ra-2]), 0, &nextisarray);
    return nextisarray;
  }
  return 0;
#else
  UNUSED(J);
  return 0;
#endif
}

static TRef lj_record_s390x_snapshot_keepalive(jit_State *J, TRef tr)
{
#if LJ_TARGET_S390X
  if (tref_isinteger(tr))
    return emitir(IRTI(IR_ADD), tr, lj_ir_kint(J, 0));
  if (tref_isnumber(tr))
    return emitir(IRTN(IR_ADD), tr, lj_ir_knum_zero(J));
#else
  UNUSED(J);
#endif
  return tr;
}

static void lj_record_s390x_root_forl_array_snapshot_preload(jit_State *J,
							     const ScEvEntry *scev)
{
#if LJ_TARGET_S390X
  BCReg ra = bc_a(J->cur.startins);
  TRef idx = J->base[ra+FORL_EXT];
  TRef stop = TREF(scev->stop, scev->t.irt);
  TRef step = TREF(scev->step, scev->t.irt);
  BCReg s;
  if (!idx)
    idx = TREF(scev->idx, scev->t.irt);
  idx = lj_record_s390x_snapshot_keepalive(J, idx);
  stop = lj_record_s390x_snapshot_keepalive(J, stop);
  step = lj_record_s390x_snapshot_keepalive(J, step);
  for (s = 1; s < ra; s++) {
    TRef tr = J->base[s];
    if (!tr)
      tr = sload(J, (int32_t)s);
    tr = lj_record_s390x_snapshot_keepalive(J, tr);
    J->base[s] = tr;
    J->slot[J->baseslot + s] = tr;
  }
  J->base[ra+FORL_IDX] = idx;
  J->base[ra+FORL_STOP] = stop;
  J->base[ra+FORL_STEP] = step;
  J->base[ra+FORL_EXT] = idx;
  J->slot[J->baseslot + ra+FORL_IDX] = idx;
  J->slot[J->baseslot + ra+FORL_STOP] = stop;
  J->slot[J->baseslot + ra+FORL_STEP] = step;
  J->slot[J->baseslot + ra+FORL_EXT] = idx;
#else
  UNUSED(J);
  UNUSED(scev);
#endif
}

/* Simulate the runtime behavior of the FOR loop iterator. */
static LoopEvent rec_for_iter(IROp *op, cTValue *o, int isforl)
{
  lua_Number stopv = numberVnum(&o[FORL_STOP]);
  lua_Number idxv = numberVnum(&o[FORL_IDX]);
  lua_Number stepv = numberVnum(&o[FORL_STEP]);
  if (isforl)
    idxv += stepv;
  if (rec_for_direction(&o[FORL_STEP])) {
    if (idxv <= stopv) {
      *op = IR_LE;
      return idxv + 2*stepv > stopv ? LOOPEV_ENTERLO : LOOPEV_ENTER;
    }
    *op = IR_GT; return LOOPEV_LEAVE;
  } else {
    if (stopv <= idxv) {
      *op = IR_GE;
      return idxv + 2*stepv < stopv ? LOOPEV_ENTERLO : LOOPEV_ENTER;
    }
    *op = IR_LT; return LOOPEV_LEAVE;
  }
}

/* Record checks for FOR loop overflow and step direction. */
static void rec_for_check(jit_State *J, IRType t, int dir,
			  TRef stop, TRef step, int init)
{
  if (!tref_isk(step)) {
    /* Non-constant step: need a guard for the direction. */
    TRef zero = (t == IRT_INT) ? lj_ir_kint(J, 0) : lj_ir_knum_zero(J);
    emitir(IRTG(dir ? IR_GE : IR_LT, t), step, zero);
    /* Add hoistable overflow checks for a narrowed FORL index. */
    if (init && t == IRT_INT) {
      if (tref_isk(stop)) {
	/* Constant stop: optimize check away or to a range check for step. */
	int32_t k = IR(tref_ref(stop))->i;
	if (dir) {
	  if (k > 0)
	    emitir(IRTGI(IR_LE), step, lj_ir_kint(J, (int32_t)0x7fffffff-k));
	} else {
	  if (k < 0)
	    emitir(IRTGI(IR_GE), step, lj_ir_kint(J, (int32_t)0x80000000-k));
	}
      } else {
	/* Stop+step variable: need full overflow check. */
	TRef tr = emitir(IRTGI(IR_ADDOV), step, stop);
	emitir(IRTI(IR_USE), tr, 0);  /* ADDOV is weak. Avoid dead result. */
      }
    }
  } else if (init && t == IRT_INT && !tref_isk(stop)) {
    /* Constant step: optimize overflow check to a range check for stop. */
    int32_t k = IR(tref_ref(step))->i;
    k = (int32_t)(dir ? 0x7fffffff : 0x80000000) - k;
    emitir(IRTGI(dir ? IR_LE : IR_GE), stop, lj_ir_kint(J, k));
  }
}

/* Record a FORL instruction. */
static void rec_for_loop(jit_State *J, const BCIns *fori, ScEvEntry *scev,
			 int init)
{
  BCReg ra = bc_a(*fori);
  cTValue *tv = &J->L->base[ra];
  TRef idx = J->base[ra+FORL_IDX];
  IRType t = idx ? tref_type(idx) :
	     (init || LJ_DUALNUM) ? lj_opt_narrow_forl(J, tv) : IRT_NUM;
  int mode = IRSLOAD_INHERIT +
    ((!LJ_DUALNUM || tvisint(tv) == (t == IRT_INT)) ? IRSLOAD_READONLY : 0);
  TRef stop = fori_arg(J, fori, ra+FORL_STOP, t, mode);
  TRef step = fori_arg(J, fori, ra+FORL_STEP, t, mode);
  int idxmode;
  int tc, dir = rec_for_direction(&tv[FORL_STEP]);
  lj_assertJ(bc_op(*fori) == BC_FORI || bc_op(*fori) == BC_JFORI,
	     "bad bytecode %d instead of FORI/JFORI", bc_op(*fori));
  scev->t.irt = t;
  scev->dir = dir;
  scev->stop = tref_ref(stop);
  scev->step = tref_ref(step);
  rec_for_check(J, t, dir, stop, step, init);
  scev->start = tref_ref(find_kinit(J, fori, ra+FORL_IDX, IRT_INT));
  tc = (LJ_DUALNUM &&
	!(scev->start && irref_isk(scev->stop) && irref_isk(scev->step) &&
	  tvisint(&tv[FORL_IDX]) == (t == IRT_INT))) ?
	IRSLOAD_TYPECHECK : 0;
  if (tc) {
    J->base[ra+FORL_STOP] = stop;
    J->base[ra+FORL_STEP] = step;
  }
  idxmode = IRSLOAD_INHERIT + tc + (J->scev.start << 16);
#if LJ_TARGET_S390X
  {
    int u8histop = lj_record_s390x_fori_u8histop(J, fori, ra, t, dir,
						 scev->start, stop, step);
    if (u8histop)
      idxmode |= IRSLOAD_FORI_U8HISTOP_MODE(u8histop);
  }
#endif
  if (!idx)
    idx = fori_load(J, ra+FORL_IDX, t, idxmode);
  if (!init)
    J->base[ra+FORL_IDX] = idx = emitir(IRT(IR_ADD, t), idx, step);
  J->base[ra+FORL_EXT] = idx;
  scev->idx = tref_ref(idx);
  setmref(scev->pc, fori);
  J->maxslot = ra+FORL_EXT+1;
}

/* Record FORL/JFORL or FORI/JFORI. */
static LoopEvent rec_for(jit_State *J, const BCIns *fori, int isforl)
{
  BCReg ra = bc_a(*fori);
  TValue *tv = &J->L->base[ra];
  TRef *tr = &J->base[ra];
  IROp op;
  LoopEvent ev;
  TRef stop;
  IRType t;
  /* Avoid semantic mismatches and always failing guards. */
  if ((tvisnum(&tv[FORL_IDX]) && tvisnan(&tv[FORL_IDX])) ||
      (tvisnum(&tv[FORL_STOP]) && tvisnan(&tv[FORL_STOP])) ||
      (tvisnum(&tv[FORL_STEP]) && tvisnan(&tv[FORL_STEP])) ||
      tvismzero(&tv[FORL_STEP]))
    lj_trace_err(J, LJ_TRERR_GFAIL);
  if (isforl) {  /* Handle FORL/JFORL opcodes. */
    TRef idx = tr[FORL_IDX];
    if (lj_record_s390x_forl_fastpath_log_enabled()) {
      fprintf(stderr,
	      "S390X_FORL_FASTPATH trace=%u parent=%u exit=%u ra=%u fori=%p scev_pc=%p idx_ref=%u scev_idx=%u pc_match=%u idx_match=%u\n",
	      (unsigned int)J->cur.traceno, (unsigned int)J->parent,
	      (unsigned int)J->exitno, (unsigned int)ra, (const void *)fori,
	      (const void *)mref(J->scev.pc, const BCIns),
	      (unsigned int)(tref_ref(idx) - REF_BIAS),
	      (unsigned int)(J->scev.idx - REF_BIAS),
	      (unsigned int)(mref(J->scev.pc, const BCIns) == fori),
	      (unsigned int)(tref_ref(idx) == J->scev.idx));
    }
    if (mref(J->scev.pc, const BCIns) == fori && tref_ref(idx) == J->scev.idx) {
      t = J->scev.t.irt;
      stop = J->scev.stop;
      idx = emitir(IRT(IR_ADD, t), idx, J->scev.step);
      tr[FORL_EXT] = tr[FORL_IDX] = idx;
    } else {
      ScEvEntry scev;
      rec_for_loop(J, fori, &scev, 0);
      t = scev.t.irt;
      stop = scev.stop;
    }
  } else {  /* Handle FORI/JFORI opcodes. */
    BCReg i;
    lj_meta_for(J->L, tv);
    t = (LJ_DUALNUM || tref_isint(tr[FORL_IDX])) ? lj_opt_narrow_forl(J, tv) :
						   IRT_NUM;
    for (i = FORL_IDX; i <= FORL_STEP; i++) {
      if (!tr[i]) sload(J, ra+i);
      lj_assertJ(tref_isnumber_str(tr[i]), "bad FORI argument type");
      if (tref_isstr(tr[i]))
	tr[i] = emitir(IRTG(IR_STRTO, IRT_NUM), tr[i], 0);
      tr[i] = fori_conv(J, tr[i], t);
    }
    tr[FORL_EXT] = tr[FORL_IDX];
    stop = tr[FORL_STOP];
    rec_for_check(J, t, rec_for_direction(&tv[FORL_STEP]),
		  stop, tr[FORL_STEP], 1);
  }

  ev = rec_for_iter(&op, tv, isforl);
  if (ev == LOOPEV_LEAVE) {
    J->maxslot = ra+FORL_EXT+1;
    J->pc = fori+1;
  } else {
    J->maxslot = ra;
    J->pc = fori+bc_j(*fori)+1;
  }
  lj_snap_add(J);

  emitir(IRTG(op, t), tr[FORL_IDX], stop);

  if (ev == LOOPEV_LEAVE) {
    J->maxslot = ra;
    J->pc = fori+bc_j(*fori)+1;
  } else {
    J->maxslot = ra+FORL_EXT+1;
    J->pc = fori+1;
  }
  J->needsnap = 1;
  return ev;
}

/* Record ITERL/JITERL. */
static LoopEvent rec_iterl(jit_State *J, const BCIns iterins)
{
  BCReg ra = bc_a(iterins);
  if (!tref_isnil(getslot(J, ra))) {  /* Looping back? */
    J->base[ra-1] = J->base[ra];  /* Copy result of ITERC to control var. */
    J->maxslot = ra-1+bc_b(J->pc[-1]);
    J->pc += bc_j(iterins)+1;
    return LOOPEV_ENTER;
  } else {
    J->maxslot = ra-3;
    J->pc++;
    return LOOPEV_LEAVE;
  }
}

/* Record LOOP/JLOOP. Now, that was easy. */
static LoopEvent rec_loop(jit_State *J, BCReg ra, int skip)
{
  if (ra < J->maxslot) J->maxslot = ra;
  J->pc += skip;
  return LOOPEV_ENTER;
}

/* Check if a loop repeatedly failed to trace because it didn't loop back. */
static int innerloopleft(jit_State *J, const BCIns *pc)
{
  ptrdiff_t i;
  for (i = 0; i < PENALTY_SLOTS; i++)
    if (mref(J->penalty[i].pc, const BCIns) == pc) {
      if ((J->penalty[i].reason == LJ_TRERR_LLEAVE ||
	   J->penalty[i].reason == LJ_TRERR_LINNER) &&
	  J->penalty[i].val >= 2*PENALTY_MIN)
	return 1;
      break;
    }
  return 0;
}

static int lj_record_s390x_for_slot_int(cTValue *tv, int32_t *out)
{
  int64_t i64;
  int32_t i;
  if (tvisint(tv)) {
    *out = intV(tv);
    return 1;
  }
  if (tvisnum(tv) && lj_num2int_check(numV(tv), i64, i)) {
    *out = i;
    return 1;
  }
  return 0;
}

static int lj_record_s390x_small_vararg_for_unroll(jit_State *J,
						    const BCIns *fori,
						    const BCIns *loopins,
						    LoopEvent ev)
{
  BCReg ra;
  TValue *tv;
  int32_t idx, stop, step, remaining;
  const BCIns *pc;
  int isvarg;
  int has_varg = 0;
  if (!LJ_TARGET_S390X || ev == LOOPEV_LEAVE ||
      fori == NULL || loopins == NULL || fori >= loopins ||
      !(bc_op(*fori) == BC_FORI || bc_op(*fori) == BC_JFORI))
    return 0;
  isvarg = frame_isvarg(J->L->base-1) ||
	   (J->framedepth > 0 && J->pt && (J->pt->flags & PROTO_VARARG));
  for (pc = fori + 1; pc < loopins; pc++)
    if (bc_op(*pc) == BC_VARG) {
      has_varg = 1;
      break;
    }
  if (!isvarg || !has_varg)
    return 0;
  ra = bc_a(*fori);
  tv = &J->L->base[ra];
  if (!lj_record_s390x_for_slot_int(&tv[FORL_IDX], &idx) ||
      !lj_record_s390x_for_slot_int(&tv[FORL_STOP], &stop) ||
      !lj_record_s390x_for_slot_int(&tv[FORL_STEP], &step))
    return 0;
  if (step <= 0 || idx > stop)
    return 0;
  remaining = (stop - idx) / step + 1;
  if (remaining <= 0 || remaining > 8)
    return 0;
  return 1;
}

static int lj_record_s390x_iterator_forl_inner_unroll(jit_State *J,
						      const BCIns *loopins,
						      LoopEvent ev,
						      TraceNo lnk)
{
  if (!LJ_TARGET_S390X ||
      J->parent != 0 || J->exitno != 0 || ev == LOOPEV_LEAVE ||
      bc_op(J->cur.startins) != BC_FORL)
    return 0;
  if (loopins) {
    BCOp op = bc_op(*loopins);
    BCOp prev1 = bc_op(loopins[-1]);
    BCOp prev2 = bc_op(loopins[-2]);
    BCOp next1 = bc_op(loopins[1]);
    if ((op == BC_ITERL || op == BC_IITERL || op == BC_JITERL) &&
	(prev1 == BC_ITERN || prev1 == BC_ITERC) &&
	prev2 == BC_ADDVV &&
	(next1 == BC_FORL || next1 == BC_JFORL))
      return 1;
    return (op == BC_ITERN || op == BC_ITERC) &&
	   prev1 == BC_ADDVV && prev2 == BC_ISNEXT &&
	   (next1 == BC_ITERL || next1 == BC_IITERL ||
	    next1 == BC_JITERL) &&
	   (bc_op(loopins[2]) == BC_FORL || bc_op(loopins[2]) == BC_JFORL);
  }
  if (lnk != 0) {
    BCOp startop = bc_op(traceref(J, lnk)->startins);
    if (startop == BC_ITERN || startop == BC_ITERC) {
      BCReg ra;
      TValue *base;
      if (bc_op(*J->pc) != BC_JLOOP ||
	  bc_op(J->pc[-1]) != BC_ADDVV ||
	  bc_op(J->pc[-2]) != BC_ISNEXT ||
	  (bc_op(J->pc[1]) != BC_ITERL && bc_op(J->pc[1]) != BC_IITERL &&
	   bc_op(J->pc[1]) != BC_JITERL) ||
	  (bc_op(J->pc[2]) != BC_FORL && bc_op(J->pc[2]) != BC_JFORL))
	return 0;
      ra = bc_a(J->pc[-2]);
      base = J->L->base;
      if (ra < 3 || !tvistab(&base[ra-2]))
	return 0;
      return 1;
    }
    if (startop != BC_ITERL)
      return 0;
  }
  return bc_op(*J->pc) == BC_ADDVV &&
	 (bc_op(J->pc[1]) == BC_ITERN || bc_op(J->pc[1]) == BC_ITERC) &&
	 (bc_op(J->pc[2]) == BC_ITERL || bc_op(J->pc[2]) == BC_IITERL ||
	  bc_op(J->pc[2]) == BC_JITERL) &&
	 (bc_op(J->pc[3]) == BC_FORL || bc_op(J->pc[3]) == BC_JFORL);
}

/* Handle the case when an interpreted loop op is hit. */
static void rec_loop_interp(jit_State *J, const BCIns *pc, const BCIns *fori,
			    LoopEvent ev)
{
  int s390x_root_recording = LJ_TARGET_S390X && J->cur.root == 0 &&
			     J->exitno == 0;
  if ((J->parent == 0 && J->exitno == 0) || s390x_root_recording) {
    if (pc == J->startpc && J->framedepth + J->retdepth == 0) {
      if (bc_op(J->cur.startins) == BC_ITERN) return;  /* See rec_itern(). */
      /* Same loop? */
      if (ev == LOOPEV_LEAVE) {  /* Must loop back to form a root trace. */
	lj_record_s390x_lleave_log(J, "rec_loop_interp_root_leave");
	lj_trace_err(J, LJ_TRERR_LLEAVE);
      }
      lj_record_stop(J, LJ_TRLINK_LOOP, J->cur.traceno);  /* Looping trace. */
    } else if (ev != LOOPEV_LEAVE) {  /* Entering inner loop? */
      /* It's usually better to abort here and wait until the inner loop
      ** is traced. But if the inner loop repeatedly didn't loop back,
      ** this indicates a low trip count. In this case try unrolling
      ** an inner loop even in a root trace. But it's better to be a bit
      ** more conservative here and only do it for very short loops.
      */
      if (bc_j(*pc) != -1 && !innerloopleft(J, pc) &&
	  !lj_record_s390x_iterator_forl_inner_unroll(J, pc, ev, 0) &&
	  !lj_record_s390x_small_vararg_for_unroll(J, fori, pc, ev)) {
	lj_record_s390x_linner_log(J, "rec_loop_interp_root", ev, 0);
	lj_trace_err(J, LJ_TRERR_LINNER);  /* Root trace hit an inner loop. */
      }
      if ((ev != LOOPEV_ENTERLO &&
	   J->loopref && J->cur.nins - J->loopref > 100) || --J->loopunroll < 0)
	lj_trace_err(J, LJ_TRERR_LUNROLL);  /* Limit loop unrolling. */
      J->loopref = J->cur.nins;
    }
  } else if (ev != LOOPEV_LEAVE) {  /* Side trace enters an inner loop. */
    J->loopref = J->cur.nins;
    if (--J->loopunroll < 0)
      lj_trace_err(J, LJ_TRERR_LUNROLL);  /* Limit loop unrolling. */
  }  /* Side trace continues across a loop that's left or not entered. */
}

/* Handle the case when an already compiled loop op is hit. */
static void rec_loop_jit(jit_State *J, TraceNo lnk, const BCIns *fori,
			 const BCIns *loopins, LoopEvent ev)
{
  if (J->parent == 0 && J->exitno == 0) {  /* Root trace hit an inner loop. */
    /* Better let the inner loop spawn a side trace back here. */
    if (ev != LOOPEV_LEAVE &&
	!lj_record_s390x_iterator_forl_inner_unroll(J, loopins, ev, lnk) &&
	!lj_record_s390x_small_vararg_for_unroll(J, fori, loopins, ev)) {
      lj_record_s390x_linner_log(J, "rec_loop_jit_root", ev, lnk);
      lj_trace_err(J, LJ_TRERR_LINNER);
    }
    if ((ev != LOOPEV_ENTERLO &&
	 J->loopref && J->cur.nins - J->loopref > 100) || --J->loopunroll < 0)
      lj_trace_err(J, LJ_TRERR_LUNROLL);
    J->loopref = J->cur.nins;
  } else if (ev != LOOPEV_LEAVE) {  /* Side trace enters a compiled loop. */
    int iterator_restart_loop = 0;
    int payload_desc_loop = 0;
    int s390x_link_loop_desc = 0;
    int s390x_loopdesc_self_owner_stop = 0;
    int s390x_root1_replay_triplet_link_parent = 0;
#if LJ_TARGET_S390X
    if (J->cur.root == 0 && J->framedepth > 0 && J->retdepth == 0 &&
	lj_record_s390x_small_vararg_for_unroll(J, fori, loopins, ev)) {
      if ((ev != LOOPEV_ENTERLO &&
	   J->loopref && J->cur.nins - J->loopref > 100) || --J->loopunroll < 0)
	lj_trace_err(J, LJ_TRERR_LUNROLL);
      J->loopref = J->cur.nins;
      return;
    }
#endif
    if (lj_record_s390x_stop_log_enabled()) {
      fprintf(stderr,
	      "S390X_RECLOOP trace=%u parent=%u exit=%u pc=%p startpc=%p op=%u startop=%u ev=%u lnk=%u framedepth=%u retdepth=%u\n",
	      (unsigned int)J->cur.traceno, (unsigned int)J->parent,
	      (unsigned int)J->exitno, (const void *)J->pc,
	      (const void *)J->startpc,
	      (unsigned int)bc_op(*J->pc),
	      (unsigned int)bc_op(J->cur.startins),
	      (unsigned int)ev, (unsigned int)lnk,
	      (unsigned int)J->framedepth, (unsigned int)J->retdepth);
    }
#if LJ_TARGET_S390X
    if (lj_record_s390x_side_focus_enabled() &&
	J->parent != 0 && J->exitno == 0 &&
	J->cur.root == 1 &&
	bc_op(J->cur.startins) == BC_JMP &&
	bc_op(*J->pc) == BC_JLOOP) {
      fprintf(stderr,
	      "S390X_SIDE_FOCUS site=rec_loop_jit_enter trace=%u parent=%u exit=%u root=%u startpc=%p pc=%p samepc=%u startop=%u op=%u ev=%u lnk=%u framedepth=%u retdepth=%u\n",
	      (unsigned int)J->cur.traceno, (unsigned int)J->parent,
	      (unsigned int)J->exitno, (unsigned int)J->cur.root,
	      (const void *)J->startpc, (const void *)J->pc,
	      (unsigned int)(J->pc == J->startpc),
	      (unsigned int)bc_op(J->cur.startins),
	      (unsigned int)bc_op(*J->pc),
	      (unsigned int)ev, (unsigned int)lnk,
	      (unsigned int)J->framedepth,
	      (unsigned int)J->retdepth);
    }
    if (J->parent != 0 &&
	J->exitno == 0 &&
	J->cur.root != 0 &&
	J->framedepth + J->retdepth == 0 &&
	bc_op(J->cur.startins) == BC_JMP &&
	J->pc == J->startpc &&
	J->pc > proto_bc(J->pt) &&
	bc_op(*J->pc) != BC_KSTR &&
	bc_op(J->pc[-1]) == BC_JFORI &&
	bc_d(J->pc[bc_j(J->pc[-1])-1]) == J->cur.root) {
      GCtrace *parentT = traceref(J, J->parent);
      if (J->exitno < parentT->nsnap &&
	  (J->parent == J->cur.root || parentT->root == J->cur.root) &&
	  parentT->snap[J->exitno].nent == 0) {
	parentT->snap[J->exitno].count = SNAPCOUNT_DONE;
	lj_record_s390x_lleave_log(J, "rec_loop_jit_exit0_dup_loop_descendant");
	lj_trace_err(J, LJ_TRERR_LLEAVE);
      }
    }
    if (lj_record_s390x_recloop_focus_enabled() &&
	J->parent >= 3 && J->exitno == 0 && J->cur.root == 1 &&
	bc_op(J->cur.startins) == BC_JMP && bc_op(*J->pc) == BC_JLOOP) {
      fprintf(stderr,
	      "S390X_RECLOOP_FOCUS trace=%u parent=%u exit=%u ev=%u lnk=%u pc=%p startpc=%p samepc=%u startop=%u root=%u framedepth=%u retdepth=%u link_loop_desc=%u iter_restart=%u payload_desc=%u\n",
	      (unsigned int)J->cur.traceno, (unsigned int)J->parent,
	      (unsigned int)J->exitno, (unsigned int)ev, (unsigned int)lnk,
	      (const void *)J->pc, (const void *)J->startpc,
	      (unsigned int)(J->pc == J->startpc),
	      (unsigned int)bc_op(J->cur.startins),
	      (unsigned int)J->cur.root,
	      (unsigned int)J->framedepth,
	      (unsigned int)J->retdepth,
	      (unsigned int)s390x_link_loop_desc,
	      (unsigned int)iterator_restart_loop,
	      (unsigned int)payload_desc_loop);
    }
    if (J->parent != 0 &&
	(J->exitno == 1 || (J->exitno == 2 && lj_record_s390x_recloop_exit2_enabled())) &&
	J->cur.root != 0 &&
	J->framedepth + J->retdepth == 0 &&
	bc_op(J->cur.startins) == BC_JMP &&
	bc_op(*J->pc) == BC_JLOOP &&
		(J->parent == J->cur.root ||
		 (lj_record_s390x_restart_desc_loop_enabled() &&
		  J->parent != 0 &&
		  traceref(J, J->parent)->root == J->cur.root)))
	      iterator_restart_loop = 1;
    if (iterator_restart_loop)
      lj_record_s390x_loopslot_log(J, "rec_loop_jit_restart");
    if (lj_record_s390x_looplink_payload_desc_enabled() &&
	J->parent != 0 && J->parent != J->cur.root &&
	J->exitno == 1 &&
	J->cur.root != 0 &&
	J->framedepth + J->retdepth == 0 &&
	bc_op(J->cur.startins) == BC_JMP &&
	bc_op(*J->pc) == BC_JLOOP &&
	traceref(J, J->parent)->root == J->cur.root)
      payload_desc_loop = 1;
    if (payload_desc_loop && lj_record_s390x_stop_log_enabled())
      fprintf(stderr,
	      "S390X_RECLOOP trace=%u parent=%u exit=%u payload_desc_loop=1 root=%u\n",
	      (unsigned int)J->cur.traceno, (unsigned int)J->parent,
	      (unsigned int)J->exitno, (unsigned int)J->cur.root);
    if (0 &&
	J->parent != 0 && J->parent != J->cur.root &&
	J->exitno == 0 &&
	J->cur.root != 0 &&
	J->framedepth + J->retdepth == 0 &&
	bc_op(J->cur.startins) == BC_JMP &&
	bc_op(*J->pc) == BC_JLOOP &&
	J->pc == J->startpc) {
      GCtrace *parentT = traceref(J, J->parent);
      int nonstub_only = 0;
      int allow_link = (!nonstub_only || J->cur.nins != 32772);
      if (allow_link &&
	  parentT->root == J->cur.root &&
	  bc_op(parentT->startins) == BC_JMP &&
	  parentT->linktype == LJ_TRLINK_LOOP &&
	  parentT->link == J->parent)
	lnk = J->parent;
      if (allow_link)
	s390x_link_loop_desc = 1;
    }
    if (0 &&
	J->parent >= 3 && J->exitno == 0 &&
	J->cur.root == 1 &&
	J->framedepth + J->retdepth == 0 &&
	bc_op(J->cur.startins) == BC_JMP &&
	bc_op(*J->pc) == BC_JLOOP &&
	J->pc == J->startpc) {
      GCtrace *parentT = traceref(J, J->parent);
      if (parentT->root == J->cur.root &&
	  bc_op(parentT->startins) == BC_JMP &&
	  parentT->linktype == LJ_TRLINK_LOOP &&
	  parentT->link == J->parent &&
	  parentT->mcloop != 0 &&
	  parentT->resumevalid &&
	  bc_op(parentT->resumeins) == BC_JLOOP) {
	s390x_loopdesc_self_owner_stop = 1;
	lnk = J->parent;
      }
    }
    if (s390x_link_loop_desc && lj_record_s390x_stop_log_enabled())
      fprintf(stderr,
	      "S390X_RECLOOP trace=%u parent=%u exit=%u link_loop_desc=1 root=%u lnk=%u\n",
	      (unsigned int)J->cur.traceno, (unsigned int)J->parent,
	      (unsigned int)J->exitno, (unsigned int)J->cur.root,
	      (unsigned int)lnk);
    if (s390x_loopdesc_self_owner_stop && lj_record_s390x_stop_log_enabled())
      fprintf(stderr,
	      "S390X_RECLOOP trace=%u parent=%u exit=%u loopdesc_self_owner_stop=1 root=%u lnk=%u\n",
	      (unsigned int)J->cur.traceno, (unsigned int)J->parent,
	(unsigned int)J->exitno, (unsigned int)J->cur.root,
	(unsigned int)lnk);
#endif
    if (0 &&
	J->parent == 1 && J->exitno == 1 &&
	J->cur.root == 1 &&
	J->framedepth + J->retdepth == 0 &&
	bc_op(J->cur.startins) == BC_JMP &&
	bc_op(*J->pc) == BC_JLOOP) {
      s390x_root1_replay_triplet_link_parent = 1;
    }
    if (lj_record_s390x_side_focus_enabled() &&
	J->parent != 0 && J->exitno == 0 &&
	J->cur.root == 1 &&
	bc_op(J->cur.startins) == BC_JMP &&
	bc_op(*J->pc) == BC_JLOOP) {
      const char *decision = s390x_loopdesc_self_owner_stop ? "root-self-owner" :
			     (!s390x_link_loop_desc &&
			      (J->pc == J->startpc || iterator_restart_loop || payload_desc_loop) &&
			      J->framedepth + J->retdepth == 0) ? "loop-self" :
			     "root-link";
      fprintf(stderr,
	      "S390X_SIDE_FOCUS site=rec_loop_jit_decision trace=%u parent=%u exit=%u root=%u samepc=%u startop=%u op=%u ev=%u lnk=%u iterator_restart=%u payload_desc=%u link_loop_desc=%u self_owner=%u decision=%s\n",
	      (unsigned int)J->cur.traceno, (unsigned int)J->parent,
	      (unsigned int)J->exitno, (unsigned int)J->cur.root,
	      (unsigned int)(J->pc == J->startpc),
	      (unsigned int)bc_op(J->cur.startins),
	      (unsigned int)bc_op(*J->pc),
	      (unsigned int)ev, (unsigned int)lnk,
	      (unsigned int)iterator_restart_loop,
	      (unsigned int)payload_desc_loop,
	      (unsigned int)s390x_link_loop_desc,
	      (unsigned int)s390x_loopdesc_self_owner_stop,
	      decision);
    }
    J->instunroll = 0;  /* Cannot continue across a compiled loop op. */
    if (s390x_loopdesc_self_owner_stop)
      lj_record_stop(J, LJ_TRLINK_ROOT, lnk);  /* Reuse existing loop-desc owner. */
    else if (s390x_root1_replay_triplet_link_parent)
      lj_record_stop(J, LJ_TRLINK_ROOT, lnk);  /* Avoid self-loop on corrected root-1 child. */
    else if (!s390x_link_loop_desc &&
	(J->pc == J->startpc || iterator_restart_loop || payload_desc_loop) &&
	J->framedepth + J->retdepth == 0)
      lj_record_stop(J, LJ_TRLINK_LOOP, J->cur.traceno);  /* Form extra loop. */
    else
      lj_record_stop(J, LJ_TRLINK_ROOT, lnk);  /* Link to the loop. */
  }  /* Side trace continues across a loop that's left or not entered. */
}

/* Record ITERN. */
static LoopEvent rec_itern(jit_State *J, BCReg ra, BCReg rb)
{
  RecordIndex ix;
  IRType nextt;
  uint32_t keyflags;
  int nextisarray = 0;
  int s390x_hash_payload = 0;
  /* Since ITERN is recorded at the start, we need our own loop detection. */
  if (J->pc == J->startpc &&
      J->framedepth + J->retdepth == 0 && J->parent == 0 && J->exitno == 0) {
    IRRef ref = REF_FIRST + LJ_HASPROFILE;
#ifdef LUAJIT_ENABLE_CHECKHOOK
    ref += 3;
#endif
    if (J->cur.nins > ref ||
       (LJ_HASPROFILE && J->cur.nins == ref && J->cur.ir[ref-1].o != IR_PROF)) {
      J->instunroll = 0;  /* Cannot continue unrolling across an ITERN. */
      lj_record_stop(J, LJ_TRLINK_LOOP, J->cur.traceno);  /* Looping trace. */
      return LOOPEV_ENTER;
    }
  }
  if (LJ_TARGET_S390X)
    lj_record_s390x_itern_terminal_snapshot_preload(J, ra);
  J->maxslot = ra;
  lj_snap_add(J);  /* Required to make JLOOP the first ins in a side-trace. */
  copyTV(J->L, &ix.tabv, &J->L->base[ra-2]);
  copyTV(J->L, &ix.keyv, &J->L->base[ra-1]);
  nextt = rec_next_types(tabV(&ix.tabv), ix.keyv.u32.lo, &nextisarray);
  ix.tab = lj_record_s390x_pairs_tab_ref(J, (int32_t)(ra-2), tabV(&ix.tabv));
  keyflags = IRSLOAD_TYPECHECK|IRSLOAD_KEYINDEX;
  if ((nextt & 0xff) == IRT_INT)
    keyflags |= IRSLOAD_KIDX_NUMKEY;
  ix.key = J->base[ra-1] ? J->base[ra-1] :
	   sloadt(J, (int32_t)(ra-1), IRT_GUARD|IRT_INT,
		  keyflags);
  ix.idxchain = (rb < 3);  /* Omit value type check, if unused. */
  ix.mobj = 1;  /* We need the next index, too. */
  J->maxslot = ra + lj_record_next(J, &ix);
  J->needsnap = 1;
  if (!nextisarray && ix.key == 0 && (nextt & 0xff) != IRT_NIL &&
      lj_record_s390x_itern_hash_payload_enabled())
    s390x_hash_payload = 1;
  if (s390x_hash_payload && !tref_isnil(ix.val))
    J->base[ra+1] = ix.val;
  lj_record_s390x_itern_focus_log(J, "after_next", ra, &ix, nextt, keyflags);
  if (!tref_isnil(ix.key) || s390x_hash_payload) {  /* Looping back? */
    const BCIns *oldpc = J->pc;
    if (lj_record_s390x_retry_first_array_exit_enabled() &&
	J->parent == 4 && J->exitno == 1 &&
	(keyflags & IRSLOAD_KIDX_NUMKEY)) {
      GCtrace *parent = traceref(J, J->parent);
      parent->snap[J->exitno].count = SNAPCOUNT_DONE;
      if (lj_record_s390x_stop_log_enabled()) {
	fprintf(stderr,
		"S390X_ITERN_RETRY site=payload_done trace=%u parent=%u exit=%u snapcount=%u\n",
		(unsigned int)J->cur.traceno, (unsigned int)J->parent,
		(unsigned int)J->exitno,
		(unsigned int)parent->snap[J->exitno].count);
      }
    }
    lj_record_s390x_itern_focus_log(J, "payload", ra, &ix, nextt, keyflags);
    J->base[ra-1] = ix.mobj | TREF_KEYINDEX;  /* Control var has next index. */
    J->base[ra] = ix.key;
    J->base[ra+1] = ix.val;
    J->pc += bc_j(J->pc[1])+2;
#if LJ_TARGET_S390X
    TraceNo root = J->cur.root;
    int allow_payload_desc = 0;
	    if (root != 0 &&
		J->exitno == 1 &&
		bc_op(J->cur.startins) == BC_JMP &&
		bc_op(*J->pc) == BC_ADDVV) {
	      if (J->parent == root) {
	allow_payload_desc = 1;
      } else if ((keyflags & IRSLOAD_KIDX_NUMKEY) &&
		 J->parent != 0 && traceref(J, J->parent)->root == root) {
	/* Allow payload descendants that stay within the same iterator-root
	** family only for numeric-key iterators. The deeper trace is needed
	** to get past the hot trace-2 exit-1 loop on native s390x for array
	** traversal, but the same widening is not safe for hash iterators.
	*/
		allow_payload_desc = 1;
	      }
	    }
	    if (lj_record_s390x_stop_log_enabled()) {
	      fprintf(stderr,
		      "S390X_RECITERN trace=%u parent=%u exit=%u oldpc=%p oldop=%u newpc=%p newop=%u startpc=%p key_nil=0\n",
	      (unsigned int)J->cur.traceno, (unsigned int)J->parent,
	      (unsigned int)J->exitno, (const void *)oldpc,
	      (unsigned int)bc_op(*oldpc), (const void *)J->pc,
	      (unsigned int)bc_op(*J->pc), (const void *)J->startpc);
    }
    /* Once the payload path is already behind a side trace, permanently stop
    ** tracing that parent exit and fall back to the interpreter directly.
    ** This avoids pathological descendant retry ladders on native s390x.
    */
    if (!lj_record_s390x_allow_iter_desc_enabled() &&
	J->parent != 0 && J->exitno == 1) {
      GCtrace *parent = traceref(J, J->parent);
      if (parent->linktype == LJ_TRLINK_LOOP &&
	  parent->root != 0 &&
	  !allow_payload_desc) {
	parent->snap[J->exitno].count = SNAPCOUNT_DONE;
	lj_record_s390x_lleave_log(J, "rec_itern_payload_loop_descendant");
	lj_trace_err(J, LJ_TRERR_LLEAVE);
      }
      if (parent->root != 0) {
	/* Allow only the direct payload descendant behind the iterator
	** restart loop to record. Suppress any deeper descendant ladders.
	*/
	if (!allow_payload_desc) {
	  parent->snap[J->exitno].count = SNAPCOUNT_DONE;
	  lj_record_s390x_lleave_log(J, "rec_itern_payload_descendant");
	  lj_trace_err(J, LJ_TRERR_LLEAVE);
	}
      }
    }
#endif
    return LOOPEV_ENTER;
  } else {
    lj_record_s390x_itern_focus_log(J, "nil", ra, &ix, nextt, keyflags);
#if LJ_TARGET_S390X
    BCOp nextop = bc_op(J->pc[2]);
    int allow_numkey_nil_desc = 0;
    if (lj_record_s390x_stop_log_enabled()) {
      fprintf(stderr,
	      "S390X_RECITERN trace=%u parent=%u exit=%u oldpc=%p oldop=%u newpc=%p newop=%u startpc=%p key_nil=1\n",
	      (unsigned int)J->cur.traceno, (unsigned int)J->parent,
	      (unsigned int)J->exitno, (const void *)J->pc,
	      (unsigned int)bc_op(*J->pc), (const void *)(J->pc + 2),
	      (unsigned int)nextop, (const void *)J->startpc);
      if (J->parent != 0 && (J->exitno == 1 || J->exitno == 4))
	lj_record_s390x_loopslot_log(J, "rec_itern_key_nil");
    }
    if (lj_record_s390x_restart_desc_loop_enabled() &&
	J->parent != 0 && J->exitno == 1) {
      GCtrace *parent = traceref(J, J->parent);
      TraceNo root = parent->root;
      if (root != 0 && J->parent != root &&
	  bc_op(J->cur.startins) == BC_JMP && nextop == BC_FORL)
	allow_numkey_nil_desc = 1;
    }
    if (lj_record_s390x_root_itern_nil_desc_enabled() &&
	J->parent != 0 && J->exitno == 1) {
      GCtrace *parent = traceref(J, J->parent);
      TraceNo root = parent->root ? parent->root : parent->traceno;
      if (root == parent->traceno &&
	  parent->linktype == LJ_TRLINK_LOOP &&
	  bc_op(parent->startins) == BC_ITERN &&
	  bc_op(J->cur.startins) == BC_JMP &&
	  (nextop == BC_FORL || nextop == BC_IFORL || nextop == BC_JFORL))
	allow_numkey_nil_desc = 1;
    }
    if (allow_numkey_nil_desc)
      J->s390x_nil_restart_desc = J->s390x_nil_restart_desc ?
				  J->s390x_nil_restart_desc : 1;
    if (!lj_record_s390x_allow_iter_desc_enabled() &&
	J->parent != 0 && J->exitno == 1) {
      GCtrace *parent = traceref(J, J->parent);
      int skip_done = lj_record_s390x_skip_nil_desc_done_enabled();
      if (lj_record_s390x_retry_first_array_exit_enabled() &&
	  J->parent == 4 && J->exitno == 1 &&
	  (keyflags & IRSLOAD_KIDX_NUMKEY) == 0 &&
	  parent->snap[J->exitno].count <
	    J->param[JIT_P_hotexit] + lj_record_s390x_retry_first_array_exit_limit())
	skip_done = 1;
      if (parent->linktype == LJ_TRLINK_LOOP && parent->root != 0 &&
	  !allow_numkey_nil_desc) {
	if (!skip_done)
	  parent->snap[J->exitno].count = SNAPCOUNT_DONE;
	lj_record_s390x_lleave_log(J, "rec_itern_nil_loop_descendant");
	lj_trace_err(J, LJ_TRERR_LLEAVE);
      }
      /* Do not record iterator restart descendants on native s390x.
      ** If the nil/restart path wins the exit-1 race, it seeds the slow
      ** root-linked trace family. Keep restart handling in the interpreter
      ** and wait for the payload path off the same parent exit instead.
      */
      if (parent->root != 0 && !allow_numkey_nil_desc)
	if (!skip_done)
	  parent->snap[J->exitno].count = SNAPCOUNT_DONE;
      if (!allow_numkey_nil_desc) {
	UNUSED(nextop);
	lj_record_s390x_lleave_log(J, "rec_itern_nil_descendant");
	lj_trace_err(J, LJ_TRERR_LLEAVE);
      }
    }
#endif
    J->maxslot = ra-3;
    J->pc += 2;
    return LOOPEV_LEAVE;
  }
}

/* Record ISNEXT. */
static void rec_isnext(jit_State *J, BCReg ra)
{
  cTValue *b = &J->L->base[ra-3];
  if (tvisfunc(b) && funcV(b)->c.ffid == FF_next &&
      tvistab(b+1) && tvisnil(b+2)) {
    /* These checks are folded away for a compiled pairs(). */
    TRef func = getslot(J, ra-3);
    TRef trid = emitir(IRT(IR_FLOAD, IRT_U8), func, IRFL_FUNC_FFID);
    emitir(IRTGI(IR_EQ), trid, lj_ir_kint(J, FF_next));
    (void)lj_record_s390x_pairs_tab_ref(J, (int32_t)(ra-2), tabV(b+1));
    (void)getslot(J, ra-1); /* Type check for nil key. */
    J->base[ra-1] = lj_ir_kint(J, 0) | TREF_KEYINDEX;
    J->maxslot = ra;
  } else {  /* Abort trace. Interpreter will despecialize bytecode. */
    lj_trace_err(J, LJ_TRERR_RECERR);
  }
}

/* -- Record profiler hook checks ----------------------------------------- */

#if LJ_HASPROFILE

/* Need to insert profiler hook check? */
static int rec_profile_need(jit_State *J, GCproto *pt, const BCIns *pc)
{
  GCproto *ppt;
  lj_assertJ(J->prof_mode == 'f' || J->prof_mode == 'l',
	     "bad profiler mode %c", J->prof_mode);
  if (!pt)
    return 0;
  ppt = J->prev_pt;
  J->prev_pt = pt;
  if (pt != ppt && ppt) {
    J->prev_line = -1;
    return 1;
  }
  if (J->prof_mode == 'l') {
    BCLine line = lj_debug_line(pt, proto_bcpos(pt, pc));
    BCLine pline = J->prev_line;
    J->prev_line = line;
    if (pline != line)
      return 1;
  }
  return 0;
}

static void rec_profile_ins(jit_State *J, const BCIns *pc)
{
  if (J->prof_mode && rec_profile_need(J, J->pt, pc)) {
    emitir(IRTG(IR_PROF, IRT_NIL), 0, 0);
    lj_snap_add(J);
  }
}

static void rec_profile_ret(jit_State *J)
{
  if (J->prof_mode == 'f') {
    emitir(IRTG(IR_PROF, IRT_NIL), 0, 0);
    J->prev_pt = NULL;
    lj_snap_add(J);
  }
}

#endif

/* -- Record calls and returns -------------------------------------------- */

/* Specialize to the runtime value of the called function or its prototype. */
static TRef rec_call_specialize(jit_State *J, GCfunc *fn, TRef tr)
{
  TRef kfunc;
  if (isluafunc(fn)) {
    GCproto *pt = funcproto(fn);
    /* Too many closures created? Probably not a monomorphic function. */
    if (pt->flags >= PROTO_CLC_POLY) {  /* Specialize to prototype instead. */
      TRef trpt = emitir(IRT(IR_FLOAD, IRT_PGC), tr, IRFL_FUNC_PC);
      emitir(IRTG(IR_EQ, IRT_PGC), trpt, lj_ir_kptr(J, proto_bc(pt)));
      (void)lj_ir_kgc(J, obj2gco(pt), IRT_PROTO);  /* Prevent GC of proto. */
      return tr;
    }
  } else {
    /* Don't specialize to non-monomorphic builtins. */
    switch (fn->c.ffid) {
    case FF_coroutine_wrap_aux:
    case FF_string_gmatch_aux:
      /* NYI: io_file_iter doesn't have an ffid, yet. */
      {  /* Specialize to the ffid. */
	TRef trid = emitir(IRT(IR_FLOAD, IRT_U8), tr, IRFL_FUNC_FFID);
	emitir(IRTGI(IR_EQ), trid, lj_ir_kint(J, fn->c.ffid));
      }
      return tr;
    default:
      /* NYI: don't specialize to non-monomorphic C functions. */
      break;
    }
  }
  /* Otherwise specialize to the function (closure) value itself. */
  kfunc = lj_ir_kfunc(J, fn);
  emitir(IRTG(IR_EQ, IRT_FUNC), tr, kfunc);
  return kfunc;
}

#if LJ_TARGET_S390X
static int rec_s390x_retlast_select_log_enabled(void)
{
  return 0;
}

static void rec_s390x_retlast_select_log(jit_State *J, const char *reason,
					 BCReg func, ptrdiff_t nargs)
{
  if (rec_s390x_retlast_select_log_enabled()) {
    fprintf(stderr,
	    "S390X_RETLAST_SELECT reason=%s trace=%u parent=%u exit=%u pc=%p func=%u nargs=%d\n",
	    reason, (unsigned int)J->cur.traceno, (unsigned int)J->parent,
	    (unsigned int)J->exitno, (const void *)J->pc,
	    (unsigned int)func, (int)nargs);
  }
}

static int rec_s390x_str_is_select(GCstr *s)
{
  const char *p = strdata(s);
  return s->len == 6 && p[0] == 's' && p[1] == 'e' && p[2] == 'l' &&
	 p[3] == 'e' && p[4] == 'c' && p[5] == 't';
}

static int rec_s390x_str_is_hash(GCstr *s)
{
  return s->len == 1 && strdata(s)[0] == '#';
}

static int rec_s390x_proto_retlast_select(GCproto *pt, GCstr **selectstr)
{
  BCIns *bc;
  GCstr *gget0, *gget1, *hash;

  if (!(pt->flags & PROTO_VARARG) || pt->numparams != 0 || pt->sizebc != 8)
    return 0;

  bc = proto_bc(pt);
  if (bc_op(bc[0]) != BC_FUNCV ||
      bc_op(bc[1]) != BC_GGET || bc_a(bc[1]) != 0 ||
      bc_op(bc[2]) != BC_GGET || bc_a(bc[2]) != 2 ||
      bc_op(bc[3]) != BC_KSTR || bc_a(bc[3]) != 4 ||
      bc_op(bc[4]) != BC_VARG || bc_a(bc[4]) != 5 ||
      bc_b(bc[4]) != 0 || bc_c(bc[4]) != 0 ||
      bc_op(bc[5]) != BC_CALLM || bc_a(bc[5]) != 2 ||
      bc_b(bc[5]) != 2 || bc_c(bc[5]) != 1 ||
      bc_op(bc[6]) != BC_VARG || bc_a(bc[6]) != 3 ||
      bc_b(bc[6]) != 0 || bc_c(bc[6]) != 0 ||
      bc_op(bc[7]) != BC_CALLMT || bc_a(bc[7]) != 0 ||
      bc_d(bc[7]) != 1)
    return 0;

  gget0 = gco2str(proto_kgc(pt, ~(ptrdiff_t)bc_d(bc[1])));
  gget1 = gco2str(proto_kgc(pt, ~(ptrdiff_t)bc_d(bc[2])));
  hash = gco2str(proto_kgc(pt, ~(ptrdiff_t)bc_d(bc[3])));
  if (!rec_s390x_str_is_select(gget0) ||
      !rec_s390x_str_is_select(gget1) ||
      !rec_s390x_str_is_hash(hash))
    return 0;

  *selectstr = gget0;
  return 1;
}

static int rec_s390x_guard_builtin_select(jit_State *J, GCfunc *fn,
					  TRef trfunc, GCstr *selectstr)
{
  RecordIndex ix;
  GCtab *env = tabref(fn->l.env);
  cTValue *selecttv = lj_tab_getstr(env, selectstr);
  TRef trselect;

  if (!tvisfunc(selecttv) || funcV(selecttv)->c.ffid != FF_select)
    return 0;

  settabV(J->L, &ix.tabv, env);
  setstrV(J->L, &ix.keyv, selectstr);
  ix.val = 0;
  ix.tab = emitir(IRT(IR_FLOAD, IRT_TAB), trfunc, IRFL_FUNC_ENV);
  ix.key = lj_ir_kstr(J, selectstr);
  ix.idxchain = LJ_MAX_IDXCHAIN;
  trselect = lj_record_idx(J, &ix);
  emitir(IRTG(IR_EQ, IRT_FUNC), trselect, lj_ir_kfunc(J, funcV(selecttv)));
  return 1;
}

static int rec_s390x_retlast_select_call(jit_State *J, BCReg func,
					 BCReg nresults, ptrdiff_t nargs)
{
  TValue *functv = &J->L->base[func];
  GCfunc *fn;
  GCproto *pt;
  GCstr *selectstr;
  TRef trfunc;
  TRef lastarg;
  ptrdiff_t i;

  if (nresults != 1) {
    rec_s390x_retlast_select_log(J, "nresults", func, nargs);
    return 0;
  }
  if (nargs <= 0) {
    rec_s390x_retlast_select_log(J, "nargs", func, nargs);
    return 0;
  }
  if (!tvisfunc(functv)) {
    rec_s390x_retlast_select_log(J, "notfunc", func, nargs);
    return 0;
  }
  fn = funcV(functv);
  if (!isluafunc(fn)) {
    rec_s390x_retlast_select_log(J, "notlua", func, nargs);
    return 0;
  }
  pt = funcproto(fn);
  if (!rec_s390x_proto_retlast_select(pt, &selectstr)) {
    rec_s390x_retlast_select_log(J, "proto", func, nargs);
    return 0;
  }

  trfunc = getslot(J, func);
  for (i = 1; i <= nargs; i++)
    (void)getslot(J, func+LJ_FR2+i);

  emitir(IRTG(IR_EQ, IRT_FUNC), trfunc, lj_ir_kfunc(J, fn));
  if (!rec_s390x_guard_builtin_select(J, fn, trfunc, selectstr)) {
    rec_s390x_retlast_select_log(J, "select", func, nargs);
    return 0;
  }

  lastarg = getslot(J, func+LJ_FR2+nargs);
  J->base[func] = lastarg;
#if LJ_FR2
  J->base[func+1] = 0;
#endif
  J->maxslot = func + 1;
  /* Skip callee bytecode and the two guarded select fast-function entries. */
  J->bcskip = pt->sizebc + 2;
  rec_s390x_retlast_select_log(J, "hit", func, nargs);
  return 1;
}
#endif

/* Record call setup. */
static void rec_call_setup(jit_State *J, BCReg func, ptrdiff_t nargs)
{
  RecordIndex ix;
  TValue *functv = &J->L->base[func];
  TRef kfunc, *fbase = &J->base[func];
  ptrdiff_t i;
  (void)getslot(J, func); /* Ensure func has a reference. */
  for (i = 1; i <= nargs; i++)
    (void)getslot(J, func+LJ_FR2+i);  /* Ensure all args have a reference. */
  if (!tref_isfunc(fbase[0])) {  /* Resolve __call metamethod. */
    ix.tab = fbase[0];
    copyTV(J->L, &ix.tabv, functv);
    if (!lj_record_mm_lookup(J, &ix, MM_call) || !tref_isfunc(ix.mobj))
      lj_trace_err(J, LJ_TRERR_NOMM);
    for (i = ++nargs; i > LJ_FR2; i--)  /* Shift arguments up. */
      fbase[i+LJ_FR2] = fbase[i+LJ_FR2-1];
#if LJ_FR2
    fbase[2] = fbase[0];
#endif
    fbase[0] = ix.mobj;  /* Replace function. */
    functv = &ix.mobjv;
  }
  kfunc = rec_call_specialize(J, funcV(functv), fbase[0]);
#if LJ_FR2
  fbase[0] = kfunc;
  fbase[1] = TREF_FRAME;
#else
  fbase[0] = kfunc | TREF_FRAME;
#endif
  J->maxslot = (BCReg)nargs;
}

/* Record call. */
void lj_record_call(jit_State *J, BCReg func, ptrdiff_t nargs)
{
  rec_call_setup(J, func, nargs);
  /* Bump frame. */
  J->framedepth++;
  J->base += func+1+LJ_FR2;
  J->baseslot += func+1+LJ_FR2;
  if (J->baseslot + J->maxslot >= LJ_MAX_JSLOTS)
    lj_trace_err(J, LJ_TRERR_STACKOV);
}

/* Record tail call. */
void lj_record_tailcall(jit_State *J, BCReg func, ptrdiff_t nargs)
{
  rec_call_setup(J, func, nargs);
  if (frame_isvarg(J->L->base - 1)) {
    BCReg cbase = (BCReg)frame_delta(J->L->base - 1);
    if (--J->framedepth < 0)
      lj_trace_err(J, LJ_TRERR_NYIRETL);
    J->baseslot -= (BCReg)cbase;
    J->base -= cbase;
    func += cbase;
  }
  /* Move func + args down. */
  if (LJ_FR2 && J->baseslot == 2)
    J->base[func+1] = TREF_FRAME;
  memmove(&J->base[-1-LJ_FR2], &J->base[func], sizeof(TRef)*(J->maxslot+1+LJ_FR2));
  /* Note: the new TREF_FRAME is now at J->base[-1] (even for slot #0). */
  /* Tailcalls can form a loop, so count towards the loop unroll limit. */
  if (++J->tailcalled > J->loopunroll)
    lj_trace_err(J, LJ_TRERR_LUNROLL);
}

/* Check unroll limits for down-recursion. */
static int check_downrec_unroll(jit_State *J, GCproto *pt)
{
  IRRef ptref;
  for (ptref = J->chain[IR_KGC]; ptref; ptref = IR(ptref)->prev)
    if (ir_kgc(IR(ptref)) == obj2gco(pt)) {
      int count = 0;
      IRRef ref;
      for (ref = J->chain[IR_RETF]; ref; ref = IR(ref)->prev)
	if (IR(ref)->op1 == ptref)
	  count++;
      if (count) {
	if (J->pc == J->startpc) {
	  if (count + J->tailcalled > J->param[JIT_P_recunroll])
	    return 1;
	} else {
	  lj_trace_err(J, LJ_TRERR_DOWNREC);
	}
      }
    }
  return 0;
}

static TRef rec_cat(jit_State *J, BCReg baseslot, BCReg topslot);

/* Record return. */
void lj_record_ret(jit_State *J, BCReg rbase, ptrdiff_t gotresults)
{
  TValue *frame = J->L->base - 1;
  ptrdiff_t i;
  BCReg baseadj = 0;
  s390x_recret_branch_log(J, "entry", frame, rbase, gotresults, baseadj, 0, 0);
  for (i = 0; i < gotresults; i++)
    (void)getslot(J, rbase+i);  /* Ensure all results have a reference. */
  while (frame_ispcall(frame)) {  /* Immediately resolve pcall() returns. */
    BCReg cbase = (BCReg)frame_delta(frame);
    if (--J->framedepth <= 0)
      lj_trace_err(J, LJ_TRERR_NYIRETL);
    lj_assertJ(J->baseslot > 1+LJ_FR2, "bad baseslot for return");
    gotresults++;
    baseadj += cbase;
    rbase += cbase;
    J->baseslot -= (BCReg)cbase;
    J->base -= cbase;
    J->base[--rbase] = TREF_TRUE;  /* Prepend true to results. */
    frame = frame_prevd(frame);
    J->needsnap = 1;  /* Stop catching on-trace errors. */
  }
  /* Return to lower frame via interpreter for unhandled cases. */
  if (J->framedepth == 0 && J->pt && bc_isret(bc_op(*J->pc)) &&
       (!frame_islua(frame) ||
	(J->parent == 0 && J->exitno == 0 &&
	 !bc_isret(bc_op(J->cur.startins))))) {
    /* NYI: specialize to frame type and return directly, not via RET*. */
    s390x_recret_branch_log(J, "interp_return_stop", frame, rbase, gotresults,
			      baseadj, 0, 0);
    for (i = 0; i < (ptrdiff_t)rbase; i++)
      J->base[i] = 0;  /* Purge dead slots. */
    J->maxslot = rbase + (BCReg)gotresults;
    lj_record_stop(J, LJ_TRLINK_RETURN, 0);  /* Return to interpreter. */
    return;
  }
  if (frame_isvarg(frame)) {
    BCReg cbase = (BCReg)frame_delta(frame);
    if (--J->framedepth < 0)  /* NYI: return of vararg func to lower frame. */
      lj_trace_err(J, LJ_TRERR_NYIRETL);
    lj_assertJ(J->baseslot > 1+LJ_FR2, "bad baseslot for return");
    baseadj += cbase;
    rbase += cbase;
    J->baseslot -= (BCReg)cbase;
    J->base -= cbase;
    frame = frame_prevd(frame);
  }
  s390x_recret_log(J, "target", frame, rbase, gotresults, baseadj);
  if (frame_islua(frame)) {  /* Return to Lua frame. */
    s390x_recret_log(J, "lua", frame, rbase, gotresults, baseadj);
    BCIns callins = *(frame_pc(frame)-1);
    ptrdiff_t nresults = bc_b(callins) ? (ptrdiff_t)bc_b(callins)-1 :gotresults;
    BCReg cbase = bc_a(callins);
    GCproto *pt = funcproto(frame_func(frame - (cbase+1+LJ_FR2)));
    s390x_recret_branch_log(J, "lua_pre", frame, rbase, gotresults, baseadj,
			      cbase, nresults);
    if ((pt->flags & PROTO_NOJIT))
      lj_trace_err(J, LJ_TRERR_CJITOFF);
    if (J->framedepth == 0 && J->pt && frame == J->L->base - 1) {
      if (!J->cur.root && check_downrec_unroll(J, pt)) {
	s390x_recret_branch_log(J, "lua_downrec_stop", frame, rbase,
				  gotresults, baseadj, cbase, nresults);
	J->maxslot = (BCReg)(rbase + gotresults);
	lj_snap_purge(J);
	lj_record_stop(J, LJ_TRLINK_DOWNREC, J->cur.traceno);  /* Down-rec. */
	return;
      }
      lj_snap_add(J);
    }
    for (i = 0; i < nresults; i++)  /* Adjust results. */
      J->base[i-1-LJ_FR2] = i < gotresults ? J->base[rbase+i] : TREF_NIL;
    J->maxslot = cbase+(BCReg)nresults;
    if (J->framedepth > 0) {  /* Return to a frame that is part of the trace. */
      s390x_recret_branch_log(J, "lua_intrace_return", frame, rbase,
				gotresults, baseadj, cbase, nresults);
      J->framedepth--;
      lj_assertJ(J->baseslot > cbase+1+LJ_FR2, "bad baseslot for return");
      J->baseslot -= cbase+1+LJ_FR2;
      J->base -= cbase+1+LJ_FR2;
    } else if (J->parent == 0 && J->exitno == 0 &&
	       !bc_isret(bc_op(J->cur.startins))) {
      /* Return to lower frame would leave the loop in a root trace. */
      s390x_recret_branch_log(J, "lua_root_lower_frame_lleave", frame, rbase,
				gotresults, baseadj, cbase, nresults);
      lj_record_s390x_lleave_log(J, "record_ret_lower_frame");
      lj_trace_err(J, LJ_TRERR_LLEAVE);
    } else if (J->needsnap) {  /* Tailcalled to ff with side-effects. */
      s390x_recret_branch_log(J, "lua_needsnap_nyiretl", frame, rbase,
				gotresults, baseadj, cbase, nresults);
      lj_trace_err(J, LJ_TRERR_NYIRETL);  /* No way to insert snapshot here. */
    } else if (1 + pt->framesize >= LJ_MAX_JSLOTS ||
	       J->baseslot + J->maxslot >= LJ_MAX_JSLOTS) {
      s390x_recret_branch_log(J, "lua_stackov", frame, rbase, gotresults,
				baseadj, cbase, nresults);
      lj_trace_err(J, LJ_TRERR_STACKOV);
	    } else {  /* Return to lower frame. Guard for the target we return to. */
	      s390x_recret_branch_log(J, "lua_lower_frame_retf", frame, rbase,
					gotresults, baseadj, cbase, nresults);
	      TRef trpt = lj_ir_kgc(J, obj2gco(pt), IRT_PROTO);
	      TRef trpc = lj_ir_kptr(J, (void *)frame_pc(frame));
	      emitir(IRTG(IR_RETF, IRT_PGC), trpt, trpc);
	      J->retdepth++;
	      J->needsnap = 1;
	      J->scev.idx = REF_NIL;
	      lj_assertJ(J->baseslot == 1+LJ_FR2, "bad baseslot for return");
	      s390x_recret_slots_log(J, "lower_frame_pre_shift", cbase, nresults);
	      /* Shift result slots up and clear the slots of the new frame below. */
	      memmove(J->base + cbase, J->base-1-LJ_FR2, sizeof(TRef)*nresults);
	      memset(J->base-1-LJ_FR2, 0, sizeof(TRef)*(cbase+1+LJ_FR2));
	      s390x_recret_slots_log(J, "lower_frame_post_shift", cbase, nresults);
	    }
  } else if (frame_iscont(frame)) {  /* Return to continuation frame. */
    s390x_recret_log(J, "cont", frame, rbase, gotresults, baseadj);
    s390x_recret_branch_log(J, "cont_pre", frame, rbase, gotresults, baseadj,
			      0, 0);
    ASMFunction cont = frame_contf(frame);
    BCReg cbase = (BCReg)frame_delta(frame);
    if ((J->framedepth -= 2) < 0)
      lj_trace_err(J, LJ_TRERR_NYIRETL);
    J->baseslot -= (BCReg)cbase;
    J->base -= cbase;
    J->maxslot = cbase-(2<<LJ_FR2);
    if (cont == lj_cont_ra) {
      /* Copy result to destination slot. */
      BCReg dst = bc_a(*(frame_contpc(frame)-1));
      J->base[dst] = gotresults ? J->base[cbase+rbase] : TREF_NIL;
      if (dst >= J->maxslot) {
	J->maxslot = dst+1;
      }
    } else if (cont == lj_cont_nop) {
      /* Nothing to do here. */
    } else if (cont == lj_cont_cat) {
      BCReg bslot = bc_b(*(frame_contpc(frame)-1));
      TRef tr = gotresults ? J->base[cbase+rbase] : TREF_NIL;
      if (bslot != J->maxslot) {  /* Concatenate the remainder. */
	/* Simulate lower frame and result. */
	TValue *b = J->L->base - baseadj, save;
	/* Can't handle MM_concat + CALLT + fast func side-effects. */
	if (J->postproc != LJ_POST_NONE)
	  lj_trace_err(J, LJ_TRERR_NYIRETL);
	J->base[J->maxslot] = tr;
	copyTV(J->L, &save, b-(2<<LJ_FR2));
	if (gotresults)
	  copyTV(J->L, b-(2<<LJ_FR2), b+rbase);
	else
	  setnilV(b-(2<<LJ_FR2));
	J->L->base = b - cbase;
	tr = rec_cat(J, bslot, cbase-(2<<LJ_FR2));
	b = J->L->base + cbase;  /* Undo. */
	J->L->base = b + baseadj;
	copyTV(J->L, b-(2<<LJ_FR2), &save);
      }
      if (tr >= 0xffffff00) {
	lj_err_throw(J->L, -(int32_t)tr);  /* Propagate errors. */
      } else if (tr) {  /* Store final result. */
	BCReg dst = bc_a(*(frame_contpc(frame)-1));
	J->base[dst] = tr;
	if (dst >= J->maxslot) {
	  J->maxslot = dst+1;
	}
      }  /* Otherwise continue with another __concat call. */
    } else {
      /* Result type already specialized. */
      lj_assertJ(cont == lj_cont_condf || cont == lj_cont_condt,
		 "bad continuation type");
    }
  } else {
    lj_trace_err(J, LJ_TRERR_NYIRETL);  /* NYI: handle return to C frame. */
  }
  lj_assertJ(J->baseslot >= 1+LJ_FR2, "bad baseslot for return");
}

/* -- Metamethod handling ------------------------------------------------- */

/* Prepare to record call to metamethod. */
static BCReg rec_mm_prep(jit_State *J, ASMFunction cont)
{
  BCReg s, top = cont == lj_cont_cat ? J->maxslot : curr_proto(J->L)->framesize;
#if LJ_FR2
  J->base[top] = lj_ir_k64(J, IR_KNUM, u64ptr(contptr(cont)));
  J->base[top+1] = TREF_CONT;
#else
  J->base[top] = lj_ir_kptr(J, contptr(cont)) | TREF_CONT;
#endif
  J->framedepth++;
  for (s = J->maxslot; s < top; s++)
    J->base[s] = 0;  /* Clear frame gap to avoid resurrecting previous refs. */
  return top+1+LJ_FR2;
}

/* Record metamethod lookup. */
int lj_record_mm_lookup(jit_State *J, RecordIndex *ix, MMS mm)
{
  RecordIndex mix;
  GCtab *mt;
  if (tref_istab(ix->tab)) {
    mt = tabref(tabV(&ix->tabv)->metatable);
    mix.tab = emitir(IRT(IR_FLOAD, IRT_TAB), ix->tab, IRFL_TAB_META);
  } else if (tref_isudata(ix->tab)) {
    int udtype = udataV(&ix->tabv)->udtype;
    mt = tabref(udataV(&ix->tabv)->metatable);
    mix.tab = emitir(IRT(IR_FLOAD, IRT_TAB), ix->tab, IRFL_UDATA_META);
    /* The metatables of special userdata objects are treated as immutable. */
    if (udtype != UDTYPE_USERDATA) {
      cTValue *mo;
      if (LJ_HASFFI && udtype == UDTYPE_FFI_CLIB) {
	/* Specialize to the C library namespace object. */
	emitir(IRTG(IR_EQ, IRT_PGC), ix->tab, lj_ir_kptr(J, udataV(&ix->tabv)));
      } else {
	/* Specialize to the type of userdata. */
	TRef tr = emitir(IRT(IR_FLOAD, IRT_U8), ix->tab, IRFL_UDATA_UDTYPE);
	emitir(IRTGI(IR_EQ), tr, lj_ir_kint(J, udtype));
      }
  immutable_mt:
      mo = lj_tab_getstr(mt, mmname_str(J2G(J), mm));
      ix->mt = mix.tab;
      ix->mtv = mt;
      if (!mo || tvisnil(mo))
	return 0;  /* No metamethod. */
      /* Treat metamethod or index table as immutable, too. */
      if (!(tvisfunc(mo) || tvistab(mo)))
	lj_trace_err(J, LJ_TRERR_BADTYPE);
      copyTV(J->L, &ix->mobjv, mo);
      ix->mobj = lj_ir_kgc(J, gcV(mo), tvisfunc(mo) ? IRT_FUNC : IRT_TAB);
      return 1;  /* Got metamethod or index table. */
    }
  } else {
    /* Specialize to base metatable. Must flush mcode in lua_setmetatable(). */
    mt = tabref(basemt_obj(J2G(J), &ix->tabv));
    if (mt == NULL) {
      ix->mt = TREF_NIL;
      return 0;  /* No metamethod. */
    }
    /* The cdata metatable is treated as immutable. */
    if (LJ_HASFFI && tref_iscdata(ix->tab)) {
      mix.tab = TREF_NIL;
      goto immutable_mt;
    }
    ix->mt = mix.tab = lj_ir_ggfload(J, IRT_TAB,
      GG_OFS(g.gcroot[GCROOT_BASEMT+itypemap(&ix->tabv)]));
    goto nocheck;
  }
  ix->mt = mt ? mix.tab : TREF_NIL;
  emitir(IRTG(mt ? IR_NE : IR_EQ, IRT_TAB), mix.tab, lj_ir_knull(J, IRT_TAB));
nocheck:
  if (mt) {
    GCstr *mmstr = mmname_str(J2G(J), mm);
    cTValue *mo = lj_tab_getstr(mt, mmstr);
    if (mo && !tvisnil(mo))
      copyTV(J->L, &ix->mobjv, mo);
    ix->mtv = mt;
    settabV(J->L, &mix.tabv, mt);
    setstrV(J->L, &mix.keyv, mmstr);
    mix.key = lj_ir_kstr(J, mmstr);
    mix.val = 0;
    mix.idxchain = 0;
    ix->mobj = lj_record_idx(J, &mix);
    return !tref_isnil(ix->mobj);  /* 1 if metamethod found, 0 if not. */
  }
  return 0;  /* No metamethod. */
}

/* Record call to arithmetic metamethod. */
static TRef rec_mm_arith(jit_State *J, RecordIndex *ix, MMS mm)
{
  /* Set up metamethod call first to save ix->tab and ix->tabv. */
  BCReg func = rec_mm_prep(J, mm == MM_concat ? lj_cont_cat : lj_cont_ra);
  TRef *base = J->base + func;
  TValue *basev = J->L->base + func;
  base[1+LJ_FR2] = ix->tab; base[2+LJ_FR2] = ix->key;
  copyTV(J->L, basev+1+LJ_FR2, &ix->tabv);
  copyTV(J->L, basev+2+LJ_FR2, &ix->keyv);
  if (!lj_record_mm_lookup(J, ix, mm)) {  /* Lookup mm on 1st operand. */
    if (mm != MM_unm) {
      ix->tab = ix->key;
      copyTV(J->L, &ix->tabv, &ix->keyv);
      if (lj_record_mm_lookup(J, ix, mm))  /* Lookup mm on 2nd operand. */
	goto ok;
    }
    lj_trace_err(J, LJ_TRERR_NOMM);
  }
ok:
  base[0] = ix->mobj;
#if LJ_FR2
  base[1] = 0;
#endif
  copyTV(J->L, basev+0, &ix->mobjv);
  lj_record_call(J, func, 2);
  return 0;  /* No result yet. */
}

/* Record call to __len metamethod. */
static TRef rec_mm_len(jit_State *J, TRef tr, TValue *tv)
{
  RecordIndex ix;
  ix.tab = tr;
  copyTV(J->L, &ix.tabv, tv);
  if (lj_record_mm_lookup(J, &ix, MM_len)) {
    BCReg func = rec_mm_prep(J, lj_cont_ra);
    TRef *base = J->base + func;
    TValue *basev = J->L->base + func;
    base[0] = ix.mobj; copyTV(J->L, basev+0, &ix.mobjv);
    base += LJ_FR2;
    basev += LJ_FR2;
    base[1] = tr; copyTV(J->L, basev+1, tv);
#if LJ_52
    base[2] = tr; copyTV(J->L, basev+2, tv);
#else
    base[2] = TREF_NIL; setnilV(basev+2);
#endif
    lj_record_call(J, func, 2);
  } else {
    if (LJ_52 && tref_istab(tr))
      return emitir(IRTI(IR_ALEN), tr, TREF_NIL);
    lj_trace_err(J, LJ_TRERR_NOMM);
  }
  return 0;  /* No result yet. */
}

/* Call a comparison metamethod. */
static void rec_mm_callcomp(jit_State *J, RecordIndex *ix, int op)
{
  BCReg func = rec_mm_prep(J, (op&1) ? lj_cont_condf : lj_cont_condt);
  TRef *base = J->base + func + LJ_FR2;
  TValue *tv = J->L->base + func + LJ_FR2;
  base[-LJ_FR2] = ix->mobj; base[1] = ix->val; base[2] = ix->key;
  copyTV(J->L, tv-LJ_FR2, &ix->mobjv);
  copyTV(J->L, tv+1, &ix->valv);
  copyTV(J->L, tv+2, &ix->keyv);
  lj_record_call(J, func, 2);
}

/* Record call to equality comparison metamethod (for tab and udata only). */
static void rec_mm_equal(jit_State *J, RecordIndex *ix, int op)
{
  ix->tab = ix->val;
  copyTV(J->L, &ix->tabv, &ix->valv);
  if (lj_record_mm_lookup(J, ix, MM_eq)) {  /* Lookup mm on 1st operand. */
    cTValue *bv;
    TRef mo1 = ix->mobj;
    TValue mo1v;
    copyTV(J->L, &mo1v, &ix->mobjv);
    /* Avoid the 2nd lookup and the objcmp if the metatables are equal. */
    bv = &ix->keyv;
    if (tvistab(bv) && tabref(tabV(bv)->metatable) == ix->mtv) {
      TRef mt2 = emitir(IRT(IR_FLOAD, IRT_TAB), ix->key, IRFL_TAB_META);
      emitir(IRTG(IR_EQ, IRT_TAB), mt2, ix->mt);
    } else if (tvisudata(bv) && tabref(udataV(bv)->metatable) == ix->mtv) {
      TRef mt2 = emitir(IRT(IR_FLOAD, IRT_TAB), ix->key, IRFL_UDATA_META);
      emitir(IRTG(IR_EQ, IRT_TAB), mt2, ix->mt);
    } else {  /* Lookup metamethod on 2nd operand and compare both. */
      ix->tab = ix->key;
      copyTV(J->L, &ix->tabv, bv);
      if (!lj_record_mm_lookup(J, ix, MM_eq) ||
	  lj_record_objcmp(J, mo1, ix->mobj, &mo1v, &ix->mobjv))
	return;
    }
    rec_mm_callcomp(J, ix, op);
  }
}

/* Record call to ordered comparison metamethods (for arbitrary objects). */
static void rec_mm_comp(jit_State *J, RecordIndex *ix, int op)
{
  ix->tab = ix->val;
  copyTV(J->L, &ix->tabv, &ix->valv);
  while (1) {
    MMS mm = (op & 2) ? MM_le : MM_lt;  /* Try __le + __lt or only __lt. */
#if LJ_52
    if (!lj_record_mm_lookup(J, ix, mm)) {  /* Lookup mm on 1st operand. */
      ix->tab = ix->key;
      copyTV(J->L, &ix->tabv, &ix->keyv);
      if (!lj_record_mm_lookup(J, ix, mm))  /* Lookup mm on 2nd operand. */
	goto nomatch;
    }
    rec_mm_callcomp(J, ix, op);
    return;
#else
    if (lj_record_mm_lookup(J, ix, mm)) {  /* Lookup mm on 1st operand. */
      cTValue *bv;
      TRef mo1 = ix->mobj;
      TValue mo1v;
      copyTV(J->L, &mo1v, &ix->mobjv);
      /* Avoid the 2nd lookup and the objcmp if the metatables are equal. */
      bv = &ix->keyv;
      if (tvistab(bv) && tabref(tabV(bv)->metatable) == ix->mtv) {
	TRef mt2 = emitir(IRT(IR_FLOAD, IRT_TAB), ix->key, IRFL_TAB_META);
	emitir(IRTG(IR_EQ, IRT_TAB), mt2, ix->mt);
      } else if (tvisudata(bv) && tabref(udataV(bv)->metatable) == ix->mtv) {
	TRef mt2 = emitir(IRT(IR_FLOAD, IRT_TAB), ix->key, IRFL_UDATA_META);
	emitir(IRTG(IR_EQ, IRT_TAB), mt2, ix->mt);
      } else {  /* Lookup metamethod on 2nd operand and compare both. */
	ix->tab = ix->key;
	copyTV(J->L, &ix->tabv, bv);
	if (!lj_record_mm_lookup(J, ix, mm) ||
	    lj_record_objcmp(J, mo1, ix->mobj, &mo1v, &ix->mobjv))
	  goto nomatch;
      }
      rec_mm_callcomp(J, ix, op);
      return;
    }
#endif
  nomatch:
    /* Lookup failed. Retry with  __lt and swapped operands. */
    if (!(op & 2)) break;  /* Already at __lt. Interpreter will throw. */
    ix->tab = ix->key; ix->key = ix->val; ix->val = ix->tab;
    copyTV(J->L, &ix->tabv, &ix->keyv);
    copyTV(J->L, &ix->keyv, &ix->valv);
    copyTV(J->L, &ix->valv, &ix->tabv);
    op ^= 3;
  }
}

#if LJ_HASFFI
/* Setup call to cdata comparison metamethod. */
static void rec_mm_comp_cdata(jit_State *J, RecordIndex *ix, int op, MMS mm)
{
  lj_snap_add(J);
  if (tref_iscdata(ix->val)) {
    ix->tab = ix->val;
    copyTV(J->L, &ix->tabv, &ix->valv);
  } else {
    lj_assertJ(tref_iscdata(ix->key), "cdata expected");
    ix->tab = ix->key;
    copyTV(J->L, &ix->tabv, &ix->keyv);
  }
  lj_record_mm_lookup(J, ix, mm);
  rec_mm_callcomp(J, ix, op);
}
#endif

/* -- Indexed access ------------------------------------------------------ */

#ifdef LUAJIT_ENABLE_TABLE_BUMP
/* Bump table allocations in bytecode when they grow during recording. */
static void rec_idx_bump(jit_State *J, RecordIndex *ix)
{
  RBCHashEntry *rbc = &J->rbchash[(ix->tab & (RBCHASH_SLOTS-1))];
  if (tref_ref(ix->tab) == rbc->ref) {
    const BCIns *pc = mref(rbc->pc, const BCIns);
    GCtab *tb = tabV(&ix->tabv);
    uint32_t nhbits;
    IRIns *ir;
    if (!tvisnil(&ix->keyv))
      (void)lj_tab_set(J->L, tb, &ix->keyv);  /* Grow table right now. */
    nhbits = tb->hmask > 0 ? lj_fls(tb->hmask)+1 : 0;
    ir = IR(tref_ref(ix->tab));
    if (ir->o == IR_TNEW) {
      uint32_t ah = bc_d(*pc);
      uint32_t asize = ah & 0x7ff, hbits = ah >> 11;
      if (nhbits > hbits) hbits = nhbits;
      if (tb->asize > asize) {
	asize = tb->asize <= 0x7ff ? tb->asize : 0x7ff;
      }
      if ((asize | (hbits<<11)) != ah) {  /* Has the size changed? */
	/* Patch bytecode, but continue recording (for more patching). */
	setbc_d(pc, (asize | (hbits<<11)));
	/* Patching TNEW operands is only safe if the trace is aborted. */
	ir->op1 = asize; ir->op2 = hbits;
	J->retryrec = 1;  /* Abort the trace at the end of recording. */
      }
    } else if (ir->o == IR_TDUP) {
      GCtab *tpl = gco2tab(proto_kgc(&gcref(rbc->pt)->pt, ~(ptrdiff_t)bc_d(*pc)));
      /* Grow template table, but preserve keys with nil values. */
      if ((tb->asize > tpl->asize && (1u << nhbits)-1 == tpl->hmask) ||
	  (tb->asize == tpl->asize && (1u << nhbits)-1 > tpl->hmask)) {
	Node *node = noderef(tpl->node);
	uint32_t i, hmask = tpl->hmask, asize;
	TValue *array;
	for (i = 0; i <= hmask; i++) {
	  if (!tvisnil(&node[i].key) && tvisnil(&node[i].val))
	    settabV(J->L, &node[i].val, tpl);
	}
	if (!tvisnil(&ix->keyv) && tref_isk(ix->key)) {
	  TValue *o = lj_tab_set(J->L, tpl, &ix->keyv);
	  if (tvisnil(o)) settabV(J->L, o, tpl);
	}
	lj_tab_resize(J->L, tpl, tb->asize, nhbits);
	node = noderef(tpl->node);
	hmask = tpl->hmask;
	for (i = 0; i <= hmask; i++) {
	  /* This is safe, since template tables only hold immutable values. */
	  if (tvistab(&node[i].val))
	    setnilV(&node[i].val);
	}
	/* The shape of the table may have changed. Clean up array part, too. */
	asize = tpl->asize;
	array = tvref(tpl->array);
	for (i = 0; i < asize; i++) {
	  if (tvistab(&array[i]))
	    setnilV(&array[i]);
	}
	J->retryrec = 1;  /* Abort the trace at the end of recording. */
      }
    }
  }
}
#endif

/* Record bounds-check. */
static void rec_idx_abc(jit_State *J, TRef asizeref, TRef ikey, uint32_t asize)
{
  /* Try to emit invariant bounds checks. */
  if ((J->flags & (JIT_F_OPT_LOOP|JIT_F_OPT_ABC)) ==
      (JIT_F_OPT_LOOP|JIT_F_OPT_ABC)) {
    IRRef ref = tref_ref(ikey);
    IRIns *ir = IR(ref);
    int32_t ofs = 0;
    IRRef ofsref = 0;
    /* Handle constant offsets. */
    if (ir->o == IR_ADD && irref_isk(ir->op2)) {
      ofsref = ir->op2;
      ofs = IR(ofsref)->i;
      ref = ir->op1;
      ir = IR(ref);
    }
    /* Got scalar evolution analysis results for this reference? */
    if (ref == J->scev.idx) {
      int32_t stop;
      lj_assertJ(irt_isint(J->scev.t) && ir->o == IR_SLOAD,
		 "only int SCEV supported");
      stop = numberVint(&(J->L->base - J->baseslot)[ir->op1 + FORL_STOP]);
      /* Runtime value for stop of loop is within bounds? */
      if ((uint64_t)stop + ofs < (uint64_t)asize) {
	/* Emit invariant bounds check for stop. */
	uint32_t abc = IRTG(IR_ABC, tref_isk(asizeref) ? IRT_U32 : IRT_P32);
	emitir(abc, asizeref, ofs == 0 ? J->scev.stop :
	       emitir(IRTI(IR_ADD), J->scev.stop, ofsref));
	/* Emit invariant bounds check for start, if not const or negative. */
	if (!(J->scev.dir && J->scev.start &&
	      (int64_t)IR(J->scev.start)->i + ofs >= 0))
	  emitir(abc, asizeref, ikey);
	return;
      }
    }
  }
  emitir(IRTGI(IR_ABC), asizeref, ikey);  /* Emit regular bounds check. */
}

/* Record indexed key lookup. */
static TRef rec_idx_key(jit_State *J, RecordIndex *ix, IRRef *rbref,
			IRType1 *rbguard)
{
  TRef key;
  GCtab *t = tabV(&ix->tabv);
  ix->oldv = lj_tab_get(J->L, t, &ix->keyv);  /* Lookup previous value. */
  *rbref = 0;
  rbguard->irt = 0;

  /* Integer keys are looked up in the array part first. */
  key = ix->key;
  if (tref_isnumber(key)) {
    int32_t k;
    if (tvisint(&ix->keyv)) {
      k = intV(&ix->keyv);
    } else {
      int64_t i64;
      if (!lj_num2int_check(numV(&ix->keyv), i64, k)) k = LJ_MAX_ASIZE;
    }
    if ((MSize)k < LJ_MAX_ASIZE) {  /* Potential array key? */
      TRef ikey = lj_opt_narrow_index(J, key);
      TRef asizeref = emitir(IRTI(IR_FLOAD), ix->tab, IRFL_TAB_ASIZE);
      if ((MSize)k < t->asize) {  /* Currently an array key? */
	TRef arrayref;
	rec_idx_abc(J, asizeref, ikey, t->asize);
	arrayref = emitir(IRT(IR_FLOAD, IRT_PGC), ix->tab, IRFL_TAB_ARRAY);
	return emitir(IRT(IR_AREF, IRT_PGC), arrayref, ikey);
      } else {  /* Currently not in array (may be an array extension)? */
	emitir(IRTGI(IR_ULE), asizeref, ikey);  /* Inv. bounds check. */
	if (k == 0 && tref_isk(key))
	  key = lj_ir_knum_zero(J);  /* Canonicalize 0 or +-0.0 to +0.0. */
	/* And continue with the hash lookup. */
      }
    } else if (!tref_isk(key)) {
      /* We can rule out const numbers which failed the integerness test
      ** above. But all other numbers are potential array keys.
      */
      if (t->asize == 0) {  /* True sparse tables have an empty array part. */
	/* Guard that the array part stays empty. */
	TRef tmp = emitir(IRTI(IR_FLOAD), ix->tab, IRFL_TAB_ASIZE);
	emitir(IRTGI(IR_EQ), tmp, lj_ir_kint(J, 0));
      } else {
	lj_trace_err(J, LJ_TRERR_NYITMIX);
      }
    }
  }

  /* Otherwise the key is located in the hash part. */
  if (t->hmask == 0) {  /* Shortcut for empty hash part. */
    /* Guard that the hash part stays empty. */
    TRef tmp = emitir(IRTI(IR_FLOAD), ix->tab, IRFL_TAB_HMASK);
    emitir(IRTGI(IR_EQ), tmp, lj_ir_kint(J, 0));
    return lj_ir_kkptr(J, niltvg(J2G(J)));
  }
  if (tref_isinteger(key))  /* Hash keys are based on numbers, not ints. */
    key = emitir(IRTN(IR_CONV), key, IRCONV_NUM_INT);
  if (tref_isk(key)) {
    /* Optimize lookup of constant hash keys. */
    GCSize hslot = (GCSize)((char *)ix->oldv-(char *)&noderef(t->node)[0].val);
    if (hslot <= t->hmask*(GCSize)sizeof(Node) &&
	hslot <= 65535*(GCSize)sizeof(Node)) {
      TRef node, kslot, hm;
      *rbref = J->cur.nins;  /* Mark possible rollback point. */
      *rbguard = J->guardemit;
      hm = emitir(IRTI(IR_FLOAD), ix->tab, IRFL_TAB_HMASK);
      emitir(IRTGI(IR_EQ), hm, lj_ir_kint(J, (int32_t)t->hmask));
      node = emitir(IRT(IR_FLOAD, IRT_PGC), ix->tab, IRFL_TAB_NODE);
      kslot = lj_ir_kslot(J, key, (IRRef)(hslot / sizeof(Node)));
      return emitir(IRTG(IR_HREFK, IRT_PGC), node, kslot);
    }
  }
  /* Fall back to a regular hash lookup. */
  return emitir(IRT(IR_HREF, IRT_PGC), ix->tab, key);
}

/* Determine whether a key is NOT one of the fast metamethod names. */
static int nommstr(jit_State *J, TRef key)
{
  if (tref_isstr(key)) {
    if (tref_isk(key)) {
      GCstr *str = ir_kstr(IR(tref_ref(key)));
      uint32_t mm;
      for (mm = 0; mm <= MM_FAST; mm++)
	if (mmname_str(J2G(J), mm) == str)
	  return 0;  /* MUST be one the fast metamethod names. */
    } else {
      return 0;  /* Variable string key MAY be a metamethod name. */
    }
  }
  return 1;  /* CANNOT be a metamethod name. */
}

/* Record indexed load/store. */
TRef lj_record_idx(jit_State *J, RecordIndex *ix)
{
  TRef xref;
  IROp xrefop, loadop;
  IRRef rbref;
  IRType1 rbguard;
  cTValue *oldv;

  while (!tref_istab(ix->tab)) { /* Handle non-table lookup. */
    /* Never call raw lj_record_idx() on non-table. */
    lj_assertJ(ix->idxchain != 0, "bad usage");
    if (!lj_record_mm_lookup(J, ix, ix->val ? MM_newindex : MM_index))
      lj_trace_err(J, LJ_TRERR_NOMM);
  handlemm:
    if (tref_isfunc(ix->mobj)) {  /* Handle metamethod call. */
      BCReg func = rec_mm_prep(J, ix->val ? lj_cont_nop : lj_cont_ra);
      TRef *base = J->base + func + LJ_FR2;
      TValue *tv = J->L->base + func + LJ_FR2;
      base[-LJ_FR2] = ix->mobj; base[1] = ix->tab; base[2] = ix->key;
      setfuncV(J->L, tv-LJ_FR2, funcV(&ix->mobjv));
      copyTV(J->L, tv+1, &ix->tabv);
      copyTV(J->L, tv+2, &ix->keyv);
      if (ix->val) {
	base[3] = ix->val;
	copyTV(J->L, tv+3, &ix->valv);
	lj_record_call(J, func, 3);  /* mobj(tab, key, val) */
	return 0;
      } else {
	lj_record_call(J, func, 2);  /* res = mobj(tab, key) */
	return 0;  /* No result yet. */
      }
    }
#if LJ_HASBUFFER
    /* The index table of buffer objects is treated as immutable. */
    if (ix->mt == TREF_NIL && !ix->val &&
	tref_isudata(ix->tab) && udataV(&ix->tabv)->udtype == UDTYPE_BUFFER &&
	tref_istab(ix->mobj) && tref_isstr(ix->key) && tref_isk(ix->key)) {
      cTValue *val = lj_tab_getstr(tabV(&ix->mobjv), strV(&ix->keyv));
      TRef tr = lj_record_constify(J, val);
      if (tr) return tr;  /* Specialize to the value, i.e. a method. */
    }
#endif
    /* Otherwise retry lookup with metaobject. */
    ix->tab = ix->mobj;
    copyTV(J->L, &ix->tabv, &ix->mobjv);
    if (--ix->idxchain == 0)
      lj_trace_err(J, LJ_TRERR_IDXLOOP);
  }

  /* First catch nil and NaN keys for tables. */
  if (tvisnil(&ix->keyv) || (tvisnum(&ix->keyv) && tvisnan(&ix->keyv))) {
    if (ix->val)  /* Better fail early. */
      lj_trace_err(J, LJ_TRERR_STORENN);
    if (tref_isk(ix->key)) {
      if (ix->idxchain && lj_record_mm_lookup(J, ix, MM_index))
	goto handlemm;
      return TREF_NIL;
    }
  }

  /* Record the key lookup. */
  xref = rec_idx_key(J, ix, &rbref, &rbguard);
  xrefop = IR(tref_ref(xref))->o;
  loadop = xrefop == IR_AREF ? IR_ALOAD : IR_HLOAD;
  /* The lj_meta_tset() inconsistency is gone, but better play safe. */
  oldv = xrefop == IR_KKPTR ? (cTValue *)ir_kptr(IR(tref_ref(xref))) : ix->oldv;
  s390x_recidx_log(J, ix, "lookup", xrefop, oldv);

  if (ix->val == 0) {  /* Indexed load */
    IRType t = itype2irt(oldv);
    TRef res;
    if (oldv == niltvg(J2G(J))) {
      emitir(IRTG(IR_EQ, IRT_PGC), xref, lj_ir_kkptr(J, niltvg(J2G(J))));
      res = TREF_NIL;
    } else {
      res = emitir(IRTG(loadop, t), xref, 0);
    }
    if (tref_ref(res) < rbref) {  /* HREFK + load forwarded? */
      lj_ir_rollback(J, rbref);  /* Rollback to eliminate hmask guard. */
      J->guardemit = rbguard;
    }
    if (t == IRT_NIL && ix->idxchain && lj_record_mm_lookup(J, ix, MM_index))
      goto handlemm;
    if (irtype_ispri(t)) res = TREF_PRI(t);  /* Canonicalize primitives. */
    return res;
  } else {  /* Indexed store. */
    GCtab *mt = tabref(tabV(&ix->tabv)->metatable);
    int keybarrier = tref_isgcv(ix->key) && !tref_isnil(ix->val);
    if (tref_ref(xref) < rbref) {  /* HREFK forwarded? */
      lj_ir_rollback(J, rbref);  /* Rollback to eliminate hmask guard. */
      J->guardemit = rbguard;
    }
    if (tvisnil(oldv)) {  /* Previous value was nil? */
      s390x_recidx_log(J, ix, "store-miss", xrefop, oldv);
      /* Need to duplicate the hasmm check for the early guards. */
      int hasmm = 0;
      if (ix->idxchain && mt) {
	cTValue *mo = lj_tab_getstr(mt, mmname_str(J2G(J), MM_newindex));
	hasmm = mo && !tvisnil(mo);
      }
      if (hasmm)
	emitir(IRTG(loadop, IRT_NIL), xref, 0);  /* Guard for nil value. */
      else if (xrefop == IR_HREF)
	emitir(IRTG(oldv == niltvg(J2G(J)) ? IR_EQ : IR_NE, IRT_PGC),
	       xref, lj_ir_kkptr(J, niltvg(J2G(J))));
      if (ix->idxchain && lj_record_mm_lookup(J, ix, MM_newindex)) {
	lj_assertJ(hasmm, "inconsistent metamethod handling");
	goto handlemm;
      }
      lj_assertJ(!hasmm, "inconsistent metamethod handling");
      if (oldv == niltvg(J2G(J))) {  /* Need to insert a new key. */
	TRef key = ix->key;
	if (tref_isinteger(key)) {  /* NEWREF needs a TValue as a key. */
	  key = emitir(IRTN(IR_CONV), key, IRCONV_NUM_INT);
	} else if (tref_isnum(key)) {
	  if (tref_isk(key)) {
	    if (tvismzero(&ix->keyv))
	      key = lj_ir_knum_zero(J);  /* Canonicalize -0.0 to +0.0. */
	  } else {
	    emitir(IRTG(IR_EQ, IRT_NUM), key, key);  /* Check for !NaN. */
	  }
	}
	xref = emitir(IRT(IR_NEWREF, IRT_PGC), ix->tab, key);
	keybarrier = 0;  /* NEWREF already takes care of the key barrier. */
#ifdef LUAJIT_ENABLE_TABLE_BUMP
	if ((J->flags & JIT_F_OPT_SINK))  /* Avoid a separate flag. */
	  rec_idx_bump(J, ix);
#endif
      }
    } else if (!lj_opt_fwd_wasnonnil(J, loadop, tref_ref(xref))) {
      s390x_recidx_log(J, ix, "store-hit-guard", xrefop, oldv);
      /* Cannot derive that the previous value was non-nil, must do checks. */
      if (xrefop == IR_HREF)  /* Guard against store to niltv. */
	emitir(IRTG(IR_NE, IRT_PGC), xref, lj_ir_kkptr(J, niltvg(J2G(J))));
      if (ix->idxchain) {  /* Metamethod lookup required? */
	/* A check for NULL metatable is cheaper (hoistable) than a load. */
	if (!mt) {
	  TRef mtref = emitir(IRT(IR_FLOAD, IRT_TAB), ix->tab, IRFL_TAB_META);
	  emitir(IRTG(IR_EQ, IRT_TAB), mtref, lj_ir_knull(J, IRT_TAB));
	} else {
	  IRType t = itype2irt(oldv);
	  emitir(IRTG(loadop, t), xref, 0);  /* Guard for non-nil value. */
	}
      }
    } else {
      s390x_recidx_log(J, ix, "store-hit-fwd", xrefop, oldv);
      keybarrier = 0;  /* Previous non-nil value kept the key alive. */
    }
    /* Convert int to number before storing. */
    if (!LJ_DUALNUM && tref_isinteger(ix->val))
      ix->val = emitir(IRTN(IR_CONV), ix->val, IRCONV_NUM_INT);
    emitir(IRT(loadop+IRDELTA_L2S, tref_type(ix->val)), xref, ix->val);
    if (keybarrier || tref_isgcv(ix->val))
      emitir(IRT(IR_TBAR, IRT_NIL), ix->tab, 0);
    /* Invalidate neg. metamethod cache for stores with certain string keys. */
    if (!nommstr(J, ix->key)) {
      TRef fref = emitir(IRT(IR_FREF, IRT_PGC), ix->tab, IRFL_TAB_NOMM);
      emitir(IRT(IR_FSTORE, IRT_U8), fref, lj_ir_kint(J, 0));
    }
    J->needsnap = 1;
    return 0;
  }
}

/* Determine result type of table traversal. */
static IRType rec_next_types_idx(GCtab *t, uint32_t idx, int *isarray,
				 uint32_t *nextidx)
{
  if (isarray)
    *isarray = 0;
  for (; idx < t->asize; idx++) {
    cTValue *a = arrayslot(t, idx);
    if (LJ_LIKELY(!tvisnil(a))) {
      if (isarray)
        *isarray = 1;
      if (nextidx)
	*nextidx = idx;
      return (LJ_DUALNUM ? IRT_INT : IRT_NUM) + (itype2irt(a) << 8);
    }
  }
  idx -= t->asize;
  for (; idx <= t->hmask; idx++) {
    Node *n = &noderef(t->node)[idx];
    if (!tvisnil(&n->val)) {
      if (nextidx)
	*nextidx = t->asize + idx;
      return itype2irt(&n->key) + (itype2irt(&n->val) << 8);
    }
  }
  if (nextidx)
    *nextidx = t->asize + idx;
  return IRT_NIL + (IRT_NIL << 8);
}

static IRType rec_next_types(GCtab *t, uint32_t idx, int *isarray)
{
  return rec_next_types_idx(t, idx, isarray, NULL);
}

/* Record a table traversal step aka next(). */
int lj_record_next(jit_State *J, RecordIndex *ix)
{
  IRType t, tkey, tval;
  TRef trvk;
  int nextisarray = 0;
  uint32_t nextidx = 0;
  t = rec_next_types_idx(tabV(&ix->tabv), ix->keyv.u32.lo, &nextisarray,
			 &nextidx);
  tkey = (t & 0xff); tval = (t >> 8);
  /* s390x can avoid the helper call for proven table states by recording the
  ** next array/hash probe directly. Guards cover the control index, table
  ** shape and skipped nil slots; visible key, hidden control index and value
  ** are all materialized explicitly.
  */
  if (LJ_TARGET_S390X && nextisarray && tkey == IRT_INT && ix->mobj) {
    TRef idx = ix->key & ~TREF_KEYINDEX;
    TRef asize = emitir(IRTI(IR_FLOAD), ix->tab, IRFL_TAB_ASIZE);
    TRef arrayref = emitir(IRT(IR_FLOAD, IRT_PGC), ix->tab, IRFL_TAB_ARRAY);
    TRef aref;
    uint32_t i;
    emitir(IRTGI(IR_EQ), idx, lj_ir_kint(J, (int32_t)ix->keyv.u32.lo));
    emitir(IRTGI(IR_ULT), lj_ir_kint(J, (int32_t)nextidx), asize);
    for (i = ix->keyv.u32.lo; i < nextidx; i++) {
      aref = emitir(IRT(IR_AREF, IRT_PGC), arrayref, lj_ir_kint(J, (int32_t)i));
      (void)lj_record_vload(J, aref, 0, IRT_NIL);
    }
    aref = emitir(IRT(IR_AREF, IRT_PGC), arrayref, lj_ir_kint(J, (int32_t)nextidx));
    ix->val = lj_record_vload(J, aref, 0, tval);
    ix->mobj = lj_ir_kint(J, (int32_t)(nextidx + 1));
    ix->key = lj_ir_kint(J, (int32_t)nextidx);
    return tkey == IRT_NIL || ix->idxchain ? 1 : 2;
  } else if (LJ_TARGET_S390X &&
	     !nextisarray && tkey != IRT_NIL && ix->mobj) {
    GCtab *tab = tabV(&ix->tabv);
    uint32_t idxv = ix->keyv.u32.lo;
    uint32_t nodeidx = nextidx - tab->asize;
    if (tab->hmask <= 16 && nextidx >= tab->asize &&
	idxv <= nextidx && nodeidx <= tab->hmask &&
	nextidx + 1 <= INT32_MAX) {
      Node *nodev = noderef(tab->node);
      TRef idx = ix->key & ~TREF_KEYINDEX;
      TRef asize = emitir(IRTI(IR_FLOAD), ix->tab, IRFL_TAB_ASIZE);
      TRef hmask = emitir(IRTI(IR_FLOAD), ix->tab, IRFL_TAB_HMASK);
      TRef noderef = emitir(IRT(IR_FLOAD, IRT_PGC), ix->tab, IRFL_TAB_NODE);
      uint32_t i, hstart = idxv > tab->asize ? idxv - tab->asize : 0;
      emitir(IRTGI(IR_EQ), idx, lj_ir_kint(J, (int32_t)idxv));
      emitir(IRTGI(IR_EQ), asize, lj_ir_kint(J, (int32_t)tab->asize));
      emitir(IRTGI(IR_EQ), hmask, lj_ir_kint(J, (int32_t)tab->hmask));
      emitir(IRTG(IR_EQ, IRT_PGC), noderef, lj_ir_kptr(J, nodev));
      if (idxv < tab->asize) {
	TRef arrayref = emitir(IRT(IR_FLOAD, IRT_PGC), ix->tab, IRFL_TAB_ARRAY);
	for (i = idxv; i < tab->asize; i++) {
	  TRef aref = emitir(IRT(IR_AREF, IRT_PGC), arrayref,
			     lj_ir_kint(J, (int32_t)i));
	  (void)lj_record_vload(J, aref, 0, IRT_NIL);
	}
      }
      for (i = hstart; i < nodeidx; i++)
	(void)lj_record_vload(J, lj_ir_kptr(J, &nodev[i].val), 0, IRT_NIL);
      ix->key = lj_record_vload(J, lj_ir_kptr(J, &nodev[nodeidx].key),
				0, tkey);
      ix->val = lj_record_vload(J, lj_ir_kptr(J, &nodev[nodeidx].val),
				0, tval);
      ix->mobj = lj_ir_kint(J, (int32_t)(nextidx + 1));
      return ix->idxchain ? 1 : 2;
    }
  } else if (LJ_TARGET_S390X && tkey == IRT_NIL && ix->mobj) {
    GCtab *tab = tabV(&ix->tabv);
    uint32_t idxv = ix->keyv.u32.lo;
    uint32_t hstart = idxv > tab->asize ? idxv - tab->asize : 0;
    if (tab->hmask <= 16 &&
	((idxv >= tab->asize && hstart <= tab->hmask + 1) ||
	 (idxv < tab->asize && tab->asize - idxv <= 16))) {
      Node *nodev = noderef(tab->node);
      TRef idx = ix->key & ~TREF_KEYINDEX;
      TRef asize = emitir(IRTI(IR_FLOAD), ix->tab, IRFL_TAB_ASIZE);
      TRef hmask = emitir(IRTI(IR_FLOAD), ix->tab, IRFL_TAB_HMASK);
      TRef noderef = emitir(IRT(IR_FLOAD, IRT_PGC), ix->tab, IRFL_TAB_NODE);
      TRef arrayref = emitir(IRT(IR_FLOAD, IRT_PGC), ix->tab, IRFL_TAB_ARRAY);
      uint32_t i;
      emitir(IRTGI(IR_EQ), idx, lj_ir_kint(J, (int32_t)idxv));
      emitir(IRTGI(IR_EQ), asize, lj_ir_kint(J, (int32_t)tab->asize));
      emitir(IRTGI(IR_EQ), hmask, lj_ir_kint(J, (int32_t)tab->hmask));
      emitir(IRTG(IR_EQ, IRT_PGC), noderef, lj_ir_kptr(J, nodev));
      for (i = idxv; i < tab->asize; i++) {
	TRef aref = emitir(IRT(IR_AREF, IRT_PGC), arrayref,
			   lj_ir_kint(J, (int32_t)i));
	(void)lj_record_vload(J, aref, 0, IRT_NIL);
      }
      for (i = hstart; i <= tab->hmask; i++)
	(void)lj_record_vload(J, lj_ir_kptr(J, &nodev[i].val), 0, IRT_NIL);
      ix->mobj = lj_ir_kint(J, -1);
      ix->key = TREF_NIL;
      ix->val = TREF_NIL;
      return 1;
    }
  }
  trvk = lj_ir_call(J, IRCALL_lj_vm_next, ix->tab, ix->key);
  if (ix->mobj || tkey == IRT_NIL) {
    TRef idx = emitir(IRTI(IR_HIOP), trvk, trvk);
    /* Always check for invalid key from next() for nil result. */
    if (!ix->mobj) emitir(IRTGI(IR_NE), idx, lj_ir_kint(J, -1));
    ix->mobj = idx;
  }
  if (!nextisarray && tkey != IRT_NIL) {
    /* Hash traversal already leaves the visible key TValue in the frame.
    ** Keep the key slot unloaded and let the loop body SLOAD it on demand.
    ** This avoids the eager helper-tuple key VLOAD on the hot hash path.
    */
    ix->key = 0;
  } else if (nextisarray && tkey == IRT_INT && ix->mobj) {
    /* Array iteration already returns the next traversal index in HIOP form.
    ** Derive the visible numeric key directly from that index instead of
    ** reloading the boxed key lane from the helper tuple.
    */
    ix->key = emitir(IRTI(IR_ADD), ix->mobj, lj_ir_kint(J, -1));
  } else {
    ix->key = lj_record_vload(J, trvk, 1, tkey);
  }
  if (tkey == IRT_NIL || ix->idxchain) {  /* Omit value type check. */
    ix->val = TREF_NIL;
    return 1;
  } else {  /* Need value. */
    ix->val = lj_record_vload(J, trvk, 0, tval);
    return 2;
  }
}

static void rec_tsetm(jit_State *J, BCReg ra, BCReg rn, int32_t i)
{
  RecordIndex ix;
  cTValue *basev = J->L->base;
  GCtab *t = tabV(&basev[ra-1]);
  settabV(J->L, &ix.tabv, t);
  ix.tab = getslot(J, ra-1);
  ix.idxchain = 0;
#ifdef LUAJIT_ENABLE_TABLE_BUMP
  if ((J->flags & JIT_F_OPT_SINK)) {
    if (t->asize < i+rn-ra)
      lj_tab_reasize(J->L, t, i+rn-ra);
    setnilV(&ix.keyv);
    rec_idx_bump(J, &ix);
  }
#endif
  for (; ra < rn; i++, ra++) {
    setintV(&ix.keyv, i);
    ix.key = lj_ir_kint(J, i);
    copyTV(J->L, &ix.valv, &basev[ra]);
    ix.val = getslot(J, ra);
    lj_record_idx(J, &ix);
  }
}

/* -- Upvalue access ------------------------------------------------------ */

#if LJ_TARGET_S390X
static int rec_s390x_small_table_len_const_enabled(void)
{
  return 1;
}

static int rec_s390x_small_table_upvalue_const_enabled(void)
{
  return 1;
}
#endif

/* Check whether upvalue is immutable and ok to constify. */
static int rec_upvalue_constify(jit_State *J, GCupval *uvp)
{
  if (uvp->immutable) {
    cTValue *o = uvval(uvp);
    /* Don't constify objects that may retain large amounts of memory. */
#if LJ_HASFFI
    if (tviscdata(o)) {
      GCcdata *cd = cdataV(o);
      if (!cdataisv(cd) && !(cd->marked & LJ_GC_CDATA_FIN)) {
	CType *ct = ctype_raw(ctype_ctsG(J2G(J)), cd->ctypeid);
	if (!ctype_hassize(ct->info) || ct->size <= 16)
	  return 1;
      }
      return 0;
    }
#else
    UNUSED(J);
#endif
#if LJ_TARGET_S390X
    if (tvistab(o) && rec_s390x_small_table_upvalue_const_enabled()) {
      GCtab *t = tabV(o);
      if (t->asize <= 16 && t->hmask <= 31)
	return 1;
    }
#endif
    if (!(tvistab(o) || tvisudata(o) || tvisthread(o)))
      return 1;
  }
  return 0;
}

/* Record upvalue load/store. */
static TRef rec_upvalue(jit_State *J, uint32_t uv, TRef val)
{
  GCupval *uvp = &gcref(J->fn->l.uvptr[uv])->uv;
  TRef fn = getcurrf(J);
  IRRef uref;
  int needbarrier = 0;
  if (rec_upvalue_constify(J, uvp)) {  /* Try to constify immutable upvalue. */
    TRef tr, kfunc;
    lj_assertJ(val == 0, "bad usage");
    if (!tref_isk(fn)) {  /* Late specialization of current function. */
      if (J->pt->flags >= PROTO_CLC_POLY)
	goto noconstify;
      kfunc = lj_ir_kfunc(J, J->fn);
      emitir(IRTG(IR_EQ, IRT_FUNC), fn, kfunc);
#if LJ_FR2
      J->base[-2] = kfunc;
#else
      J->base[-1] = kfunc | TREF_FRAME;
#endif
      fn = kfunc;
    }
    tr = lj_record_constify(J, uvval(uvp));
    if (tr)
      return tr;
  }
noconstify:
  /* Note: this effectively limits LJ_MAX_UPVAL to 127. */
  uv = (uv << 8) | (hashrot(uvp->dhash, uvp->dhash + HASH_BIAS) & 0xff);
  if (!uvp->closed) {
    /* In current stack? */
    if (uvval(uvp) >= tvref(J->L->stack) &&
	uvval(uvp) < tvref(J->L->maxstack)) {
      int32_t slot = (int32_t)(uvval(uvp) - (J->L->base - J->baseslot));
      if (slot >= 0) {  /* Aliases an SSA slot? */
	uref = tref_ref(emitir(IRT(IR_UREFO, IRT_PGC), fn, uv));
	emitir(IRTG(IR_EQ, IRT_PGC),
	       REF_BASE,
	       emitir(IRT(IR_ADD, IRT_PGC), uref,
		      lj_ir_kintpgc(J, (slot - 1 - LJ_FR2) * -8)));
	slot -= (int32_t)J->baseslot;  /* Note: slot number may be negative! */
	if (val == 0) {
	  return getslot(J, slot);
	} else {
	  J->base[slot] = val;
	  if (slot >= (int32_t)J->maxslot) J->maxslot = (BCReg)(slot+1);
	  return 0;
	}
      }
    }
    /* IR_UREFO+IRT_IGC is not checked for open-ness at runtime.
    ** Always marked as a guard, since it might get promoted to IRT_PGC later.
    */
    uref = emitir(IRTG(IR_UREFO, tref_isgcv(val) ? IRT_PGC : IRT_IGC), fn, uv);
    uref = tref_ref(uref);
    emitir(IRTG(IR_UGT, IRT_PGC),
	   emitir(IRT(IR_SUB, IRT_PGC), uref, REF_BASE),
	   lj_ir_kintpgc(J, (J->baseslot + J->maxslot) * 8));
  } else {
    /* If fn is constant, then so is the GCupval*, and the upvalue cannot
    ** transition back to open, so no guard is required in this case.
    */
    IRType t = (tref_isk(fn) ? 0 : IRT_GUARD) | IRT_PGC;
    uref = tref_ref(emitir(IRT(IR_UREFC, t), fn, uv));
    needbarrier = 1;
  }
  if (val == 0) {  /* Upvalue load */
    IRType t = itype2irt(uvval(uvp));
    TRef res = emitir(IRTG(IR_ULOAD, t), uref, 0);
    if (irtype_ispri(t)) res = TREF_PRI(t);  /* Canonicalize primitive refs. */
    return res;
  } else {  /* Upvalue store. */
    /* Convert int to number before storing. */
    if (!LJ_DUALNUM && tref_isinteger(val))
      val = emitir(IRTN(IR_CONV), val, IRCONV_NUM_INT);
    emitir(IRT(IR_USTORE, tref_type(val)), uref, val);
    if (needbarrier && tref_isgcv(val))
      emitir(IRT(IR_OBAR, IRT_NIL), uref, val);
    J->needsnap = 1;
    return 0;
  }
}

/* -- Record calls to Lua functions --------------------------------------- */

/* Check unroll limits for calls. */
static void check_call_unroll(jit_State *J, TraceNo lnk)
{
  cTValue *frame = J->L->base - 1;
  void *pc = mref(frame_func(frame)->l.pc, void);
  int32_t depth = J->framedepth;
  int32_t count = 0;
  if ((J->pt->flags & PROTO_VARARG)) depth--;  /* Vararg frame still missing. */
  for (; depth > 0; depth--) {  /* Count frames with same prototype. */
    if (frame_iscont(frame)) depth--;
    frame = frame_prev(frame);
    if (mref(frame_func(frame)->l.pc, void) == pc)
      count++;
  }
  if (J->pc == J->startpc) {
    if (count + J->tailcalled > J->param[JIT_P_recunroll]) {
      J->pc++;
      if (J->framedepth + J->retdepth == 0)
	lj_record_stop(J, LJ_TRLINK_TAILREC, J->cur.traceno);  /* Tail-rec. */
      else
	lj_record_stop(J, LJ_TRLINK_UPREC, J->cur.traceno);  /* Up-recursion. */
    }
  } else {
    if (count > J->param[JIT_P_callunroll]) {
      if (lnk) {  /* Possible tail- or up-recursion. */
	lj_trace_flush(J, lnk);  /* Flush trace that only returns. */
	/* Set a small, pseudo-random hotcount for a quick retry of JFUNC*. */
	hotcount_set(J2GG(J), J->pc+1, lj_prng_u64(&J->prng) & 15u);
      }
      lj_trace_err(J, LJ_TRERR_CUNROLL);
    }
  }
}

/* Record Lua function setup. */
static void rec_func_setup(jit_State *J)
{
  GCproto *pt = J->pt;
  BCReg s, numparams = pt->numparams;
  if ((pt->flags & PROTO_NOJIT))
    lj_trace_err(J, LJ_TRERR_CJITOFF);
  if (J->baseslot + pt->framesize >= LJ_MAX_JSLOTS)
    lj_trace_err(J, LJ_TRERR_STACKOV);
  /* Fill up missing parameters with nil. */
  for (s = J->maxslot; s < numparams; s++)
    J->base[s] = TREF_NIL;
  /* The remaining slots should never be read before they are written. */
  J->maxslot = numparams;
}

/* Record Lua vararg function setup. */
static void rec_func_vararg(jit_State *J)
{
  GCproto *pt = J->pt;
  BCReg s, fixargs, vframe = J->maxslot+1+LJ_FR2;
  lj_assertJ((pt->flags & PROTO_VARARG), "FUNCV in non-vararg function");
  if (J->baseslot + vframe + pt->framesize >= LJ_MAX_JSLOTS)
    lj_trace_err(J, LJ_TRERR_STACKOV);
  J->base[vframe-1-LJ_FR2] = J->base[-1-LJ_FR2];  /* Copy function up. */
#if LJ_FR2
  J->base[vframe-1] = TREF_FRAME;
#endif
  /* Copy fixarg slots up and set their original slots to nil. */
  fixargs = pt->numparams < J->maxslot ? pt->numparams : J->maxslot;
  for (s = 0; s < fixargs; s++) {
    J->base[vframe+s] = J->base[s];
    J->base[s] = TREF_NIL;
  }
  J->maxslot = fixargs;
  J->framedepth++;
  J->base += vframe;
  J->baseslot += vframe;
}

/* Record entry to a Lua function. */
static void rec_func_lua(jit_State *J)
{
  rec_func_setup(J);
  check_call_unroll(J, 0);
}

/* Record entry to an already compiled function. */
static void rec_func_jit(jit_State *J, TraceNo lnk)
{
  GCtrace *T;
  rec_func_setup(J);
  T = traceref(J, lnk);
  s390x_funcjit_log(J, "entry", lnk, T);
  if (T->linktype == LJ_TRLINK_RETURN) {  /* Trace returns to interpreter? */
    s390x_funcjit_log(J, "continue_return", lnk, T);
    check_call_unroll(J, lnk);
    /* Temporarily unpatch JFUNC* to continue recording across function. */
    J->patchins = *J->pc;
    J->patchpc = (BCIns *)J->pc;
    *J->patchpc = T->startins;
    return;
  }
  J->instunroll = 0;  /* Cannot continue across a compiled function. */
  if (J->pc == J->startpc && J->framedepth + J->retdepth == 0)
    lj_record_stop(J, LJ_TRLINK_TAILREC, J->cur.traceno);  /* Extra tail-rec. */
  else {
    s390x_funcjit_log(J, "stop_root", lnk, T);
    lj_record_stop(J, LJ_TRLINK_ROOT, lnk);  /* Link to the function. */
  }
}

/* -- Vararg handling ----------------------------------------------------- */

/* Detect y = select(x, ...) idiom. */
static int select_detect(jit_State *J)
{
  BCIns ins = J->pc[1];
  if (bc_op(ins) == BC_CALLM && bc_b(ins) == 2 && bc_c(ins) == 1) {
    cTValue *func = &J->L->base[bc_a(ins)];
    if (tvisfunc(func) && funcV(func)->c.ffid == FF_select) {
      TRef kfunc = lj_ir_kfunc(J, funcV(func));
      emitir(IRTG(IR_EQ, IRT_FUNC), getslot(J, bc_a(ins)), kfunc);
      return 1;
    }
  }
  return 0;
}

/* Record vararg instruction. */
static void rec_varg(jit_State *J, BCReg dst, ptrdiff_t nresults)
{
  int32_t numparams = J->pt->numparams;
  ptrdiff_t nvararg = frame_delta(J->L->base-1) - numparams - 1 - LJ_FR2;
  lj_assertJ(frame_isvarg(J->L->base-1), "VARG in non-vararg frame");
  if (LJ_FR2 && dst > J->maxslot)
    J->base[dst-1] = 0;  /* Prevent resurrection of unrelated slot. */
  if (J->framedepth > 0) {  /* Simple case: varargs defined on-trace. */
    ptrdiff_t i;
    if (nvararg < 0) nvararg = 0;
    if (nresults != 1) {
      if (nresults == -1) nresults = nvararg;
      J->maxslot = dst + (BCReg)nresults;
    } else if (dst >= J->maxslot) {
      J->maxslot = dst + 1;
    }
    if (J->baseslot + J->maxslot >= LJ_MAX_JSLOTS)
      lj_trace_err(J, LJ_TRERR_STACKOV);
    for (i = 0; i < nresults; i++)
      J->base[dst+i] = i < nvararg ? getslot(J, i - nvararg - 1 - LJ_FR2) : TREF_NIL;
  } else {  /* Unknown number of varargs passed to trace. */
    TRef fr = emitir(IRTI(IR_SLOAD), LJ_FR2, IRSLOAD_READONLY|IRSLOAD_FRAME);
    int32_t frofs = 8*(1+LJ_FR2+numparams)+FRAME_VARG;
    if (nresults >= 0) {  /* Known fixed number of results. */
      ptrdiff_t i;
      if (nvararg > 0) {
	ptrdiff_t nload = nvararg >= nresults ? nresults : nvararg;
	TRef vbase;
	if (nvararg >= nresults)
	  emitir(IRTGI(IR_GE), fr, lj_ir_kint(J, frofs+8*(int32_t)nresults));
	else
	  emitir(IRTGI(IR_EQ), fr,
		 lj_ir_kint(J, (int32_t)frame_ftsz(J->L->base-1)));
	vbase = emitir(IRT(IR_SUB, IRT_IGC), REF_BASE, fr);
	vbase = emitir(IRT(IR_ADD, IRT_PGC), vbase,
		       lj_ir_kintpgc(J, frofs-8*(1+LJ_FR2)));
	for (i = 0; i < nload; i++) {
	  IRType t = itype2irt(&J->L->base[i-1-LJ_FR2-nvararg]);
	  J->base[dst+i] = lj_record_vload(J, vbase, (MSize)i, t);
	}
      } else {
	emitir(IRTGI(IR_LE), fr, lj_ir_kint(J, frofs));
	nvararg = 0;
      }
      for (i = nvararg; i < nresults; i++)
	J->base[dst+i] = TREF_NIL;
      if (nresults != 1 || dst >= J->maxslot) {
	J->maxslot = dst + (BCReg)nresults;
      }
    } else if (select_detect(J)) {  /* y = select(x, ...) */
      TRef tridx = getslot(J, dst-1);
      TRef tr = TREF_NIL;
      int stable_varg_count = 0;
      int32_t stable_ftsz = 0;
      ptrdiff_t idx = lj_ffrecord_select_mode(J, tridx, &J->L->base[dst-1]);
      if (idx < 0) goto nyivarg;
      if (idx != 0 && !tref_isinteger(tridx)) {
	if (tref_isstr(tridx))
	  tridx = emitir(IRTG(IR_STRTO, IRT_NUM), tridx, 0);
	tridx = emitir(IRTGI(IR_CONV), tridx, IRCONV_INT_NUM|IRCONV_INDEX);
      }
      if (idx != 0 && idx <= nvararg && !tref_isk(tridx)) {
	stable_ftsz = (int32_t)frame_ftsz(J->L->base-1);
	emitir(IRTGI(IR_EQ), fr, lj_ir_kint(J, stable_ftsz));
	stable_varg_count = 1;
      }
      if (idx != 0 && tref_isk(tridx)) {
	emitir(IRTGI(idx <= nvararg ? IR_GE : IR_LT),
	       fr, lj_ir_kint(J, frofs+8*(int32_t)idx));
	frofs -= 8;  /* Bias for 1-based index. */
      } else if (idx <= nvararg) {  /* Compute size. */
	if (stable_varg_count) {
	  tr = lj_ir_kint(J, (int32_t)nvararg);
	} else {
	  TRef tmp = emitir(IRTI(IR_ADD), fr, lj_ir_kint(J, -frofs));
	  if (numparams)
	    emitir(IRTGI(IR_GE), tmp, lj_ir_kint(J, 0));
	  tr = emitir(IRTI(IR_BSHR), tmp, lj_ir_kint(J, 3));
	}
	if (idx != 0) {
	  tridx = emitir(IRTI(IR_ADD), tridx, lj_ir_kint(J, -1));
	  rec_idx_abc(J, tr, tridx, (uint32_t)nvararg);
	}
      } else {
	TRef tmp = lj_ir_kint(J, frofs);
	if (idx != 0) {
	  TRef tmp2 = emitir(IRTI(IR_BSHL), tridx, lj_ir_kint(J, 3));
	  tmp = emitir(IRTI(IR_ADD), tmp2, tmp);
	} else {
	  tr = lj_ir_kint(J, 0);
	}
	emitir(IRTGI(IR_LT), fr, tmp);
      }
      if (idx != 0 && idx <= nvararg) {
	IRType t;
	TRef aref, vbase;
	if (stable_varg_count) {
	  vbase = emitir(IRT(IR_ADD, IRT_PGC), REF_BASE,
			 lj_ir_kintpgc(J,
				       frofs-(8<<LJ_FR2)-stable_ftsz));
	} else {
	  vbase = emitir(IRT(IR_SUB, IRT_IGC), REF_BASE, fr);
	  vbase = emitir(IRT(IR_ADD, IRT_PGC), vbase,
			 lj_ir_kintpgc(J, frofs-(8<<LJ_FR2)));
	}
	t = itype2irt(&J->L->base[idx-2-LJ_FR2-nvararg]);
	aref = emitir(IRT(IR_AREF, IRT_PGC), vbase, tridx);
	tr = lj_record_vload(J, aref, 0, t);
      }
      J->base[dst-2-LJ_FR2] = tr;
      J->maxslot = dst-1-LJ_FR2;
      J->bcskip = 2;  /* Skip CALLM + select. */
    } else {
    nyivarg:
      setintV(&J->errinfo, BC_VARG);
      lj_trace_err_info(J, LJ_TRERR_NYIBC);
    }
  }
}

/* -- Record allocations -------------------------------------------------- */

static TRef rec_tnew(jit_State *J, uint32_t ah)
{
  uint32_t asize = ah & 0x7ff;
  uint32_t hbits = ah >> 11;
  TRef tr;
  if (asize == 0x7ff) asize = 0x801;
  tr = emitir(IRTG(IR_TNEW, IRT_TAB), asize, hbits);
#ifdef LUAJIT_ENABLE_TABLE_BUMP
  J->rbchash[(tr & (RBCHASH_SLOTS-1))].ref = tref_ref(tr);
  setmref(J->rbchash[(tr & (RBCHASH_SLOTS-1))].pc, J->pc);
  setgcref(J->rbchash[(tr & (RBCHASH_SLOTS-1))].pt, obj2gco(J->pt));
#endif
  return tr;
}

/* -- Concatenation ------------------------------------------------------- */

typedef struct RecCatDataCP {
  TValue savetv[5+LJ_FR2];
  jit_State *J;
  BCReg baseslot, topslot;
  TRef tr;
} RecCatDataCP;

static TValue *rec_mm_concat_cp(lua_State *L, lua_CFunction dummy, void *ud)
{
  RecCatDataCP *rcd = (RecCatDataCP *)ud;
  jit_State *J = rcd->J;
  BCReg baseslot = rcd->baseslot, topslot = rcd->topslot;
  TRef *top = &J->base[topslot];
  BCReg s;
  RecordIndex ix;
  UNUSED(L); UNUSED(dummy);
  lj_assertJ(baseslot < topslot, "bad CAT arg");
  for (s = baseslot; s <= topslot; s++)
    (void)getslot(J, s);  /* Ensure all arguments have a reference. */
  if (tref_isnumber_str(top[0]) && tref_isnumber_str(top[-1])) {
    TRef tr, hdr, *trp, *xbase, *base = &J->base[baseslot];
    /* First convert numbers to strings. */
    for (trp = top; trp >= base; trp--) {
      if (tref_isnumber(*trp))
	*trp = emitir(IRT(IR_TOSTR, IRT_STR), *trp,
		      tref_isnum(*trp) ? IRTOSTR_NUM : IRTOSTR_INT);
      else if (!tref_isstr(*trp))
	break;
    }
    xbase = ++trp;
    tr = hdr = emitir(IRT(IR_BUFHDR, IRT_PGC),
		      lj_ir_kptr(J, &J2G(J)->tmpbuf), IRBUFHDR_RESET);
    do {
      tr = emitir(IRTG(IR_BUFPUT, IRT_PGC), tr, *trp++);
    } while (trp <= top);
    tr = emitir(IRTG(IR_BUFSTR, IRT_STR), tr, hdr);
    J->maxslot = (BCReg)(xbase - J->base);
    if (xbase == base) {
      rcd->tr = tr;  /* Return simple concatenation result. */
      return NULL;
    }
    /* Pass partial result. */
    rcd->topslot = topslot = J->maxslot--;
    /* Save updated range of slots. */
    memcpy(rcd->savetv, &L->base[topslot-1], sizeof(rcd->savetv));
    *xbase = tr;
    top = xbase;
    setstrV(J->L, &ix.keyv, &J2G(J)->strempty);  /* Simulate string result. */
  } else {
    J->maxslot = topslot-1;
    copyTV(J->L, &ix.keyv, &J->L->base[topslot]);
  }
  copyTV(J->L, &ix.tabv, &J->L->base[topslot-1]);
  ix.tab = top[-1];
  ix.key = top[0];
  rec_mm_arith(J, &ix, MM_concat);  /* Call __concat metamethod. */
  rcd->tr = 0;  /* No result yet. */
  return NULL;
}

static TRef rec_cat(jit_State *J, BCReg baseslot, BCReg topslot)
{
  lua_State *L = J->L;
  ptrdiff_t delta = L->top - L->base;
  TValue errobj;
  RecCatDataCP rcd;
  int errcode;
  rcd.J = J;
  rcd.baseslot = baseslot;
  rcd.topslot = topslot;
  /* Save slots. */
  memcpy(rcd.savetv, &L->base[topslot-1], sizeof(rcd.savetv));
  errcode = lj_vm_cpcall(L, NULL, &rcd, rec_mm_concat_cp);
  if (errcode) copyTV(L, &errobj, L->top-1);
  /* Restore slots. */
  memcpy(&L->base[rcd.topslot-1], rcd.savetv, sizeof(rcd.savetv));
  if (errcode) {
    L->top = L->base + delta;
    copyTV(L, L->top++, &errobj);
    return (TRef)(-errcode);
  }
  return rcd.tr;
}

/* -- Record bytecode ops ------------------------------------------------- */

/* Prepare for comparison. */
static void rec_comp_prep(jit_State *J)
{
  /* Prevent merging with snapshot #0 (GC exit) since we fixup the PC. */
  if (J->cur.nsnap == 1 && J->cur.snap[0].ref == J->cur.nins)
    emitir_raw(IRT(IR_NOP, IRT_NIL), 0, 0);
  lj_snap_add(J);
}

/* Fixup comparison. */
static void rec_comp_fixup(jit_State *J, const BCIns *pc, int cond)
{
  BCIns jmpins = pc[1];
  const BCIns *npc = pc + 2 + (cond ? bc_j(jmpins) : 0);
  SnapShot *snap = &J->cur.snap[J->cur.nsnap-1];
  /* Set PC to opposite target to avoid re-recording the comp. in side trace. */
#if LJ_FR2
  SnapEntry *flink = &J->cur.snapmap[snap->mapofs + snap->nent];
  uint64_t pcbase;
  memcpy(&pcbase, flink, sizeof(uint64_t));
  pcbase = (pcbase & 0xff) | (u64ptr(npc) << 8);
  memcpy(flink, &pcbase, sizeof(uint64_t));
#else
  J->cur.snapmap[snap->mapofs + snap->nent] = SNAP_MKPC(npc);
#endif
  J->needsnap = 1;
  if (bc_a(jmpins) < J->maxslot) J->maxslot = bc_a(jmpins);
  lj_snap_shrink(J);  /* Shrink last snapshot if possible. */
}

/* Record the next bytecode instruction (_before_ it's executed). */
void lj_record_ins(jit_State *J)
{
  cTValue *lbase;
  RecordIndex ix;
  const BCIns *pc;
  BCIns ins;
  BCOp op;
  TRef ra, rb, rc;

  /* Perform post-processing action before recording the next instruction. */
  if (LJ_UNLIKELY(J->postproc != LJ_POST_NONE)) {
    switch (J->postproc) {
    case LJ_POST_FIXCOMP:  /* Fixup comparison. */
      pc = (const BCIns *)(uintptr_t)J2G(J)->tmptv.u64;
      rec_comp_fixup(J, pc, (!tvistruecond(&J2G(J)->tmptv2) ^ (bc_op(*pc)&1)));
      /* fallthrough */
    case LJ_POST_FIXGUARD:  /* Fixup and emit pending guard. */
    case LJ_POST_FIXGUARDSNAP:  /* Fixup and emit pending guard and snapshot. */
      if (!tvistruecond(&J2G(J)->tmptv2)) {
	J->fold.ins.o ^= 1;  /* Flip guard to opposite. */
	if (J->postproc == LJ_POST_FIXGUARDSNAP) {
	  SnapShot *snap = &J->cur.snap[J->cur.nsnap-1];
	  J->cur.snapmap[snap->mapofs+snap->nent-1]--;  /* False -> true. */
	}
      }
      lj_opt_fold(J);  /* Emit pending guard. */
      /* fallthrough */
    case LJ_POST_FIXBOOL:
      if (!tvistruecond(&J2G(J)->tmptv2)) {
	BCReg s;
	TValue *tv = J->L->base;
	for (s = 0; s < J->maxslot; s++)  /* Fixup stack slot (if any). */
	  if (J->base[s] == TREF_TRUE && tvisfalse(&tv[s])) {
	    J->base[s] = TREF_FALSE;
	    break;
	  }
      }
      break;
    case LJ_POST_FIXCONST:
      {
	BCReg s;
	TValue *tv = J->L->base;
	for (s = 0; s < J->maxslot; s++)  /* Constify stack slots (if any). */
	  if (J->base[s] == TREF_NIL && !tvisnil(&tv[s]))
	    J->base[s] = lj_record_constify(J, &tv[s]);
      }
      break;
    case LJ_POST_FFRETRY:  /* Suppress recording of retried fast function. */
      if (bc_op(*J->pc) >= BC__MAX)
	return;
      break;
    default: lj_assertJ(0, "bad post-processing mode"); break;
    }
    J->postproc = LJ_POST_NONE;
  }

  /* Need snapshot before recording next bytecode (e.g. after a store). */
  if (J->needsnap) {
    J->needsnap = 0;
    if (J->pt && bc_op(*J->pc) < BC_FUNCF) lj_snap_purge(J);
    lj_snap_add(J);
    J->mergesnap = 1;
  }

  /* Skip some bytecodes. */
  if (LJ_UNLIKELY(J->bcskip > 0)) {
    J->bcskip--;
    return;
  }

  /* Record only closed loops for root traces. */
  pc = J->pc;
  if (J->framedepth == 0 &&
     (MSize)((char *)pc - (char *)J->bc_min) >= J->bc_extent) {
#if LJ_TARGET_S390X
    if (!(J->parent != 0 && J->exitno == 1 &&
	  traceref(J, J->parent)->root != 0 &&
	  bc_op(*pc) == BC_FORL)) {
#endif
      lj_record_s390x_lleave_log(J, "record_ins_bc_extent");
      lj_trace_err(J, LJ_TRERR_LLEAVE);
#if LJ_TARGET_S390X
    }
#endif
  }

#ifdef LUA_USE_ASSERT
  rec_check_slots(J);
  rec_check_ir(J);
#endif

#if LJ_HASPROFILE
  rec_profile_ins(J, pc);
#endif

  /* Keep a copy of the runtime values of var/num/str operands. */
#define rav	(&ix.valv)
#define rbv	(&ix.tabv)
#define rcv	(&ix.keyv)

  lbase = J->L->base;
  ins = *pc;
  op = bc_op(ins);
#if LJ_RECORD_S390X_SEMANTIC_REDUCERS
#if LJ_RECORD_S390X_STRING_CONCAT_SLICE_REDUCER
  if (op == BC_UGET && lj_record_s390x_concat_slice_loop(J, pc))
    return;
#endif
#if LJ_RECORD_S390X_STRING_MANUAL_FIND_CYCLE_REDUCER || \
    LJ_RECORD_S390X_STRING_BYTE_SCAN_CYCLE_REDUCER
  if (op == BC_UGET && lj_record_s390x_string_cycle_loop(J, pc))
    return;
#endif
#if LJ_RECORD_S390X_LOGIC_LOW32_REDUCERS
  if (op == BC_UGET && lj_record_s390x_logic_add_phi_remainder_sum(J, pc))
    return;
  if (op == BC_UGET && lj_record_s390x_logic_chain_tail_add_sum(J, pc))
    return;
  if (op == BC_UGET && lj_record_s390x_logic_chain_tail_store_sum(J, pc))
    return;
#endif
#if LJ_RECORD_S390X_NUMERIC_MOD_REDUCERS
  if ((op == BC_ADDVN || op == BC_GGET) &&
      lj_record_s390x_numeric_prefix_loop_accum4(J, pc))
    return;
#if LJ_HASFFI
  if (op == BC_MODVN && lj_record_s390x_ffi_const_i32_mod17_loop_sum(J, pc))
    return;
#endif
#endif
#if LJ_RECORD_S390X_FFI_CDATA_REDUCERS
  if ((op == BC_GGET || op == BC_UGET) &&
      lj_record_s390x_ffi_fixed_struct_loop_sum(J, pc))
    return;
  if (op == BC_MULNV && lj_record_s390x_ffi_fixed_call_pressure_sum(J, pc))
    return;
#endif
#if LJ_RECORD_S390X_NUMERIC_MOD_REDUCERS
  if (op == BC_MODVN && lj_record_s390x_centered_mod_abs_loop_sum(J, pc))
    return;
  if (op == BC_MODVN && lj_record_s390x_abs_parity_loop_sum(J, pc))
    return;
#endif
#if LJ_RECORD_S390X_COMPONENT_LOOP_REDUCERS
  if (op == BC_MODVN && lj_record_s390x_component_loop_tail_sum(J, pc))
    return;
#endif
#if LJ_RECORD_S390X_FFI_CDATA_REDUCERS
  if (op == BC_MODVN && lj_record_s390x_mixed_width_loop_sum(J, pc))
    return;
  if (op == BC_TGETB && lj_record_s390x_pair_loop_sum(J, pc))
    return;
  if (op == BC_MOV && lj_record_s390x_buffer_fref_loop_sum(J, pc))
    return;
#endif
#if LJ_RECORD_S390X_NUMERIC_MOD_REDUCERS
  if (op == BC_ADDVN && lj_record_s390x_fpmod_quarter_loop_sum(J, pc))
    return;
  if (op == BC_GGET && lj_record_s390x_minmax_loop_sum(J, pc, 0))
    return;
  if (op == BC_GGET && lj_record_s390x_minmax_loop_sum(J, pc, 1))
    return;
#endif
#if LJ_RECORD_S390X_ITERATOR_TABLE_REDUCER
  if (op == BC_GGET && lj_record_s390x_iterator_table_loop_sum(J, pc))
    return;
#endif
#if LJ_RECORD_S390X_NUMERIC_MOD_REDUCERS
  if (op == BC_MULVN && lj_record_s390x_scaled_tobit_loop_sum(J, pc))
    return;
  if ((op == BC_MOV || op == BC_MODVN) &&
      lj_record_s390x_mod_select_loop_sum(J, pc))
    return;
  if (op == BC_MODVN && lj_record_s390x_mod_accum_loop_sum(J, pc))
    return;
  if (op == BC_MODVN && lj_record_s390x_mod_rem_select_loop_sum(J, pc))
    return;
#endif
#endif
#if LJ_TARGET_S390X
  if (op == BC_MODVN && bc_op(pc[1]) == BC_CAT) {
    setintV(&J->errinfo, (int32_t)op);
    lj_trace_err_info(J, LJ_TRERR_NYIBC);
  }
#endif
  ra = bc_a(ins);
  lj_record_s390x_recbc_log(J, pc, ins, ra, bc_b(ins), bc_c(ins));
  ix.val = 0;
  switch (bcmode_a(op)) {
  case BCMvar:
    copyTV(J->L, rav, &lbase[ra]); ix.val = ra = getslot(J, ra); break;
  default: break;  /* Handled later. */
  }
  rb = bc_b(ins);
  rc = bc_c(ins);
  switch (bcmode_b(op)) {
  case BCMnone: rb = 0; rc = bc_d(ins); break;  /* Upgrade rc to 'rd'. */
  case BCMvar:
    copyTV(J->L, rbv, &lbase[rb]); ix.tab = rb = getslot(J, rb); break;
  default: break;  /* Handled later. */
  }
  switch (bcmode_c(op)) {
  case BCMvar:
    copyTV(J->L, rcv, &lbase[rc]); ix.key = rc = getslot(J, rc); break;
  case BCMpri: setpriV(rcv, ~rc); ix.key = rc = TREF_PRI(IRT_NIL+rc); break;
  case BCMnum: { cTValue *tv = proto_knumtv(J->pt, rc);
    copyTV(J->L, rcv, tv); ix.key = rc = tvisint(tv) ? lj_ir_kint(J, intV(tv)) :
    tv->u32.hi == LJ_KEYINDEX ? (lj_ir_kint(J, 0) | TREF_KEYINDEX) :
    lj_ir_knumint(J, numV(tv)); } break;
  case BCMstr: { GCstr *s = gco2str(proto_kgc(J->pt, ~(ptrdiff_t)rc));
    setstrV(J->L, rcv, s); ix.key = rc = lj_ir_kstr(J, s); } break;
  default: break;  /* Handled later. */
  }

  switch (op) {

  /* -- Comparison ops ---------------------------------------------------- */

  case BC_ISLT: case BC_ISGE: case BC_ISLE: case BC_ISGT:
#if LJ_HASFFI
    if (tref_iscdata(ra) || tref_iscdata(rc)) {
      rec_mm_comp_cdata(J, &ix, op, ((int)op & 2) ? MM_le : MM_lt);
      break;
    }
#endif
    /* Emit nothing for two numeric or string consts. */
    if (!(tref_isk2(ra,rc) && tref_isnumber_str(ra) && tref_isnumber_str(rc))) {
      IRType ta = tref_isinteger(ra) ? IRT_INT : tref_type(ra);
      IRType tc = tref_isinteger(rc) ? IRT_INT : tref_type(rc);
      int irop;
      if (ta != tc) {
	/* Widen mixed number/int comparisons to number/number comparison. */
	if (ta == IRT_INT && tc == IRT_NUM) {
	  ra = emitir(IRTN(IR_CONV), ra, IRCONV_NUM_INT);
	  ta = IRT_NUM;
	} else if (ta == IRT_NUM && tc == IRT_INT) {
	  rc = emitir(IRTN(IR_CONV), rc, IRCONV_NUM_INT);
	} else if (LJ_52) {
	  ta = IRT_NIL;  /* Force metamethod for different types. */
	} else if (!((ta == IRT_FALSE || ta == IRT_TRUE) &&
		     (tc == IRT_FALSE || tc == IRT_TRUE))) {
	  break;  /* Interpreter will throw for two different types. */
	}
      }
      rec_comp_prep(J);
      irop = (int)op - (int)BC_ISLT + (int)IR_LT;
      if (ta == IRT_NUM) {
	if ((irop & 1)) irop ^= 4;  /* ISGE/ISGT are unordered. */
	if (!lj_ir_numcmp(numberVnum(rav), numberVnum(rcv), (IROp)irop))
	  irop ^= 5;
      } else if (ta == IRT_INT) {
	if (!lj_ir_numcmp(numberVnum(rav), numberVnum(rcv), (IROp)irop))
	  irop ^= 1;
      } else if (ta == IRT_STR) {
	if (!lj_ir_strcmp(strV(rav), strV(rcv), (IROp)irop)) irop ^= 1;
	ra = lj_ir_call(J, IRCALL_lj_str_cmp, ra, rc);
	rc = lj_ir_kint(J, 0);
	ta = IRT_INT;
      } else {
	rec_mm_comp(J, &ix, (int)op);
	break;
      }
      emitir(IRTG(irop, ta), ra, rc);
      rec_comp_fixup(J, J->pc, ((int)op ^ irop) & 1);
    }
    break;

  case BC_ISEQV: case BC_ISNEV:
  case BC_ISEQS: case BC_ISNES:
  case BC_ISEQN: case BC_ISNEN:
  case BC_ISEQP: case BC_ISNEP:
#if LJ_HASFFI
    if (tref_iscdata(ra) || tref_iscdata(rc)) {
      rec_mm_comp_cdata(J, &ix, op, MM_eq);
      break;
    }
#endif
    /* Emit nothing for two non-table, non-udata consts. */
    if (!(tref_isk2(ra, rc) && !(tref_istab(ra) || tref_isudata(ra)))) {
      int diff;
#if LJ_RECORD_S390X_NUMERIC_MOD_REDUCERS
      if (op == BC_ISNEN && lj_record_s390x_mod_branch_ifconv(J, pc, ra, rc))
	break;
#endif
      rec_comp_prep(J);
      diff = lj_record_objcmp(J, ra, rc, rav, rcv);
      if (diff == 2 || !(tref_istab(ra) || tref_isudata(ra)))
	rec_comp_fixup(J, J->pc, ((int)op & 1) == !diff);
      else if (diff == 1)  /* Only check __eq if different, but same type. */
	rec_mm_equal(J, &ix, (int)op);
    }
    break;

  /* -- Unary test and copy ops ------------------------------------------- */

  case BC_ISTC: case BC_ISFC:
    if ((op & 1) == tref_istruecond(rc))
      rc = 0;  /* Don't store if condition is not true. */
    /* fallthrough */
  case BC_IST: case BC_ISF:  /* Type specialization suffices. */
    if (bc_a(pc[1]) < J->maxslot)
      J->maxslot = bc_a(pc[1]);  /* Shrink used slots. */
    break;

  case BC_ISTYPE: case BC_ISNUM:
    /* These coercions need to correspond with lj_meta_istype(). */
    if (LJ_DUALNUM && rc == ~LJ_TNUMX+1)
      ra = lj_opt_narrow_toint(J, ra);
    else if (rc == ~LJ_TNUMX+2)
      ra = lj_ir_tonum(J, ra);
    else if (rc == ~LJ_TSTR+1)
      ra = lj_ir_tostr(J, ra);
    /* else: type specialization suffices. */
    J->base[bc_a(ins)] = ra;
    break;

  /* -- Unary ops --------------------------------------------------------- */

  case BC_NOT:
    /* Type specialization already forces const result. */
    rc = tref_istruecond(rc) ? TREF_FALSE : TREF_TRUE;
    break;

  case BC_LEN:
    if (tref_isstr(rc))
      rc = emitir(IRTI(IR_FLOAD), rc, IRFL_STR_LEN);
    else if (!LJ_52 && tref_istab(rc)) {
      TRef alen = emitir(IRTI(IR_ALEN), rc, TREF_NIL);
#if LJ_TARGET_S390X
      if (rec_s390x_small_table_len_const_enabled() && tvistab(rcv)) {
	GCtab *t = tabV(rcv);
	MSize len = lj_tab_len(t);
	if (len > 0 && len <= 8 && t->hmask == 0) {
	  emitir(IRTGI(IR_EQ), alen, lj_ir_kint(J, (int32_t)len));
	  rc = lj_ir_kint(J, (int32_t)len);
	} else {
	  rc = alen;
	}
      } else
#endif
      {
	rc = alen;
      }
    }
    else
      rc = rec_mm_len(J, rc, rcv);
    break;

  /* -- Arithmetic ops ---------------------------------------------------- */

  case BC_UNM:
    if (tref_isnumber_str(rc)) {
      rc = lj_opt_narrow_unm(J, rc, rcv);
    } else {
      ix.tab = rc;
      copyTV(J->L, &ix.tabv, rcv);
      rc = rec_mm_arith(J, &ix, MM_unm);
    }
    break;

  case BC_ADDNV: case BC_SUBNV: case BC_MULNV: case BC_DIVNV: case BC_MODNV:
    /* Swap rb/rc and rbv/rcv. rav is temp. */
    ix.tab = rc; ix.key = rc = rb; rb = ix.tab;
    copyTV(J->L, rav, rbv);
    copyTV(J->L, rbv, rcv);
    copyTV(J->L, rcv, rav);
    if (op == BC_MODNV)
      goto recmod;
    /* fallthrough */
  case BC_ADDVN: case BC_SUBVN: case BC_MULVN: case BC_DIVVN:
  case BC_ADDVV: case BC_SUBVV: case BC_MULVV: case BC_DIVVV: {
    MMS mm = bcmode_mm(op);
    if (tref_isnumber_str(rb) && tref_isnumber_str(rc))
      rc = lj_opt_narrow_arith(J, rb, rc, rbv, rcv,
			       (int)mm - (int)MM_add + (int)IR_ADD);
    else
      rc = rec_mm_arith(J, &ix, mm);
    break;
    }

  case BC_MODVN: case BC_MODVV:
  recmod:
    if (tref_isnumber_str(rb) && tref_isnumber_str(rc))
      rc = lj_opt_narrow_mod(J, rb, rc, rbv, rcv);
    else
      rc = rec_mm_arith(J, &ix, MM_mod);
    break;

  case BC_POW:
    if (tref_isnumber_str(rb) && tref_isnumber_str(rc))
      rc = lj_opt_narrow_arith(J, rb, rc, rbv, rcv, IR_POW);
    else
      rc = rec_mm_arith(J, &ix, MM_pow);
    break;

  /* -- Miscellaneous ops ------------------------------------------------- */

  case BC_CAT:
    rc = rec_cat(J, rb, rc);
    if (rc >= 0xffffff00)
      lj_err_throw(J->L, -(int32_t)rc);  /* Propagate errors. */
    break;

  /* -- Constant and move ops --------------------------------------------- */

  case BC_MOV:
    /* Clear gap of method call to avoid resurrecting previous refs. */
    if (ra > J->maxslot) {
#if LJ_FR2
      memset(J->base + J->maxslot, 0, (ra - J->maxslot) * sizeof(TRef));
#else
      J->base[ra-1] = 0;
#endif
    }
    break;
  case BC_KSTR: case BC_KNUM: case BC_KPRI:
    break;
  case BC_KSHORT:
    rc = lj_ir_kint(J, (int32_t)(int16_t)rc);
    break;
  case BC_KNIL:
    if (LJ_FR2 && ra > J->maxslot)
      J->base[ra-1] = 0;
    while (ra <= rc)
      J->base[ra++] = TREF_NIL;
    if (rc >= J->maxslot) J->maxslot = rc+1;
    break;
#if LJ_HASFFI
  case BC_KCDATA:
    rc = lj_ir_kgc(J, proto_kgc(J->pt, ~(ptrdiff_t)rc), IRT_CDATA);
    break;
#endif

  /* -- Upvalue and function ops ------------------------------------------ */

  case BC_UGET:
    rc = rec_upvalue(J, rc, 0);
    break;
  case BC_USETV: case BC_USETS: case BC_USETN: case BC_USETP:
    rec_upvalue(J, ra, rc);
    break;

  /* -- Table ops --------------------------------------------------------- */

  case BC_GGET: case BC_GSET:
    settabV(J->L, &ix.tabv, tabref(J->fn->l.env));
    ix.tab = emitir(IRT(IR_FLOAD, IRT_TAB), getcurrf(J), IRFL_FUNC_ENV);
    ix.idxchain = LJ_MAX_IDXCHAIN;
    rc = lj_record_idx(J, &ix);
    break;

  case BC_TGETB: case BC_TSETB:
    setintV(&ix.keyv, (int32_t)rc);
    ix.key = lj_ir_kint(J, (int32_t)rc);
    /* fallthrough */
  case BC_TGETV: case BC_TGETS: case BC_TSETV: case BC_TSETS:
    ix.idxchain = LJ_MAX_IDXCHAIN;
    rc = lj_record_idx(J, &ix);
    break;
  case BC_TGETR: case BC_TSETR:
    ix.idxchain = 0;
    rc = lj_record_idx(J, &ix);
    break;

  case BC_TSETM:
    rec_tsetm(J, ra, (BCReg)(J->L->top - J->L->base), (int32_t)rcv->u32.lo);
    J->maxslot = ra;  /* The table slot at ra-1 is the highest used slot. */
    break;

  case BC_TNEW:
    rc = rec_tnew(J, rc);
    break;
  case BC_TDUP:
    rc = emitir(IRTG(IR_TDUP, IRT_TAB),
		lj_ir_ktab(J, gco2tab(proto_kgc(J->pt, ~(ptrdiff_t)rc))), 0);
#ifdef LUAJIT_ENABLE_TABLE_BUMP
    J->rbchash[(rc & (RBCHASH_SLOTS-1))].ref = tref_ref(rc);
    setmref(J->rbchash[(rc & (RBCHASH_SLOTS-1))].pc, pc);
    setgcref(J->rbchash[(rc & (RBCHASH_SLOTS-1))].pt, obj2gco(J->pt));
#endif
    break;

  /* -- Calls and vararg handling ----------------------------------------- */

  case BC_ITERC:
    J->base[ra] = getslot(J, ra-3);
    J->base[ra+1+LJ_FR2] = getslot(J, ra-2);
    J->base[ra+2+LJ_FR2] = getslot(J, ra-1);
    { /* Do the actual copy now because lj_record_call needs the values. */
      TValue *b = &J->L->base[ra];
      copyTV(J->L, b, b-3);
      copyTV(J->L, b+1+LJ_FR2, b-2);
      copyTV(J->L, b+2+LJ_FR2, b-1);
    }
    lj_record_call(J, ra, (ptrdiff_t)rc-1);
    break;

  /* L->top is set to L->base+ra+rc+NARGS-1+1. See lj_dispatch_ins(). */
  case BC_CALLM:
    rc = (BCReg)(J->L->top - J->L->base) - ra - LJ_FR2;
    /* fallthrough */
  case BC_CALL:
#if LJ_TARGET_S390X
    if (op == BC_CALL &&
	rec_s390x_retlast_select_call(J, (BCReg)ra, (BCReg)rb-1,
				      (ptrdiff_t)rc-1))
      break;
#endif
    lj_record_call(J, ra, (ptrdiff_t)rc-1);
    break;

  case BC_CALLMT:
    rc = (BCReg)(J->L->top - J->L->base) - ra - LJ_FR2;
    /* fallthrough */
  case BC_CALLT:
    lj_record_tailcall(J, ra, (ptrdiff_t)rc-1);
    break;

  case BC_VARG:
    rec_varg(J, ra, (ptrdiff_t)rb-1);
    break;

  /* -- Returns ----------------------------------------------------------- */

  case BC_RETM:
    /* L->top is set to L->base+ra+rc+NRESULTS-1, see lj_dispatch_ins(). */
    rc = (BCReg)(J->L->top - J->L->base) - ra + 1;
    /* fallthrough */
  case BC_RET: case BC_RET0: case BC_RET1:
#if LJ_HASPROFILE
    rec_profile_ret(J);
#endif
    lj_record_ret(J, ra, (ptrdiff_t)rc-1);
    break;

  /* -- Loops and branches ------------------------------------------------ */

  case BC_FORI:
    if (rec_for(J, pc, 0) != LOOPEV_LEAVE)
      J->loopref = J->cur.nins;
    break;
  case BC_JFORI:
    {
      LoopEvent ev;
    lj_assertJ(bc_op(pc[(ptrdiff_t)rc-BCBIAS_J]) == BC_JFORL,
	       "JFORI does not point to JFORL");
      ev = rec_for(J, pc, 0);
      if (lj_record_s390x_side_focus_enabled() &&
	  J->parent != 0 && J->exitno == 0 &&
	  J->cur.root == 1 &&
	  bc_op(J->cur.startins) == BC_JMP) {
	fprintf(stderr,
		"S390X_SIDE_FOCUS site=bc_jfori trace=%u parent=%u exit=%u root=%u startpc=%p pc=%p samepc=%u startop=%u op=%u ev=%u target=%u\n",
		(unsigned int)J->cur.traceno, (unsigned int)J->parent,
		(unsigned int)J->exitno, (unsigned int)J->cur.root,
		(const void *)J->startpc, (const void *)J->pc,
		(unsigned int)(J->pc == J->startpc),
		(unsigned int)bc_op(J->cur.startins),
		(unsigned int)bc_op(*pc),
		(unsigned int)ev,
		(unsigned int)bc_d(pc[(ptrdiff_t)rc-BCBIAS_J]));
      }
      if (ev != LOOPEV_LEAVE &&
	  !lj_record_s390x_small_vararg_for_unroll(
	    J, pc, pc + ((ptrdiff_t)rc - BCBIAS_J), ev)) {
	/* Link to existing loop. */
	if (lj_record_s390x_jfori_interp_handoff_enabled() &&
	    J->parent == 0 && J->exitno == 0 &&
	    J->framedepth + J->retdepth == 0) {
	  if (lj_record_s390x_stop_log_enabled()) {
	    fprintf(stderr,
		    "S390X_JFORI_HANDOFF trace=%u mode=interp root=%u pc=%p startpc=%p target=%u\n",
		    (unsigned int)J->cur.traceno, (unsigned int)J->cur.root,
		    (const void *)J->pc, (const void *)J->startpc,
		    (unsigned int)bc_d(pc[(ptrdiff_t)rc-BCBIAS_J]));
	  }
	  lj_record_stop(J, LJ_TRLINK_INTERP, 0);
	} else {
	  lj_record_stop(J, LJ_TRLINK_ROOT, bc_d(pc[(ptrdiff_t)rc-BCBIAS_J]));
	}
      }
    /* Continue tracing if the loop is not entered. */
    break;
    }

  case BC_FORL:
    rec_loop_interp(J, pc, pc+((ptrdiff_t)rc-BCBIAS_J),
		    rec_for(J, pc+((ptrdiff_t)rc-BCBIAS_J), 1));
    break;
  case BC_ITERL:
    rec_loop_interp(J, pc, NULL, rec_iterl(J, *pc));
    break;
  case BC_ITERN:
    rec_loop_interp(J, pc, NULL, rec_itern(J, ra, rb));
    break;
  case BC_LOOP:
    rec_loop_interp(J, pc, NULL, rec_loop(J, ra, 1));
    break;

  case BC_JFORL: {
    const BCIns *fori = pc+bc_j(traceref(J, rc)->startins);
    rec_loop_jit(J, rc, fori, pc, rec_for(J, fori, 1));
    break;
    }
  case BC_JITERL:
    rec_loop_jit(J, rc, NULL, NULL, rec_iterl(J, traceref(J, rc)->startins));
    break;
  case BC_JLOOP:
    rec_loop_jit(J, rc, NULL, NULL, rec_loop(J, ra,
				 !bc_isret(bc_op(traceref(J, rc)->startins)) &&
				 bc_op(traceref(J, rc)->startins) != BC_ITERN));
    break;

  case BC_IFORL:
  case BC_IITERL:
  case BC_ILOOP:
  case BC_IFUNCF:
  case BC_IFUNCV:
    lj_trace_err(J, LJ_TRERR_BLACKL);
    break;

  case BC_JMP:
    if (ra < J->maxslot)
      J->maxslot = ra;  /* Shrink used slots. */
    break;

  case BC_ISNEXT:
    rec_isnext(J, ra);
    break;

  /* -- Function headers -------------------------------------------------- */

  case BC_FUNCF:
    rec_func_lua(J);
    break;
  case BC_JFUNCF:
    rec_func_jit(J, rc);
    break;

  case BC_FUNCV:
    rec_func_vararg(J);
    rec_func_lua(J);
    break;
  case BC_JFUNCV:
    /* Cannot happen. No hotcall counting for varag funcs. */
    lj_assertJ(0, "unsupported vararg hotcall");
    break;

  case BC_FUNCC:
  case BC_FUNCCW:
    lj_ffrecord_func(J);
    break;

  default:
    if (op >= BC__MAX) {
      lj_ffrecord_func(J);
      break;
    }
    /* fallthrough */
  case BC_UCLO:
  case BC_FNEW:
    setintV(&J->errinfo, (int32_t)op);
    lj_trace_err_info(J, LJ_TRERR_NYIBC);
    break;
  }

  /* rc == 0 if we have no result yet, e.g. pending __index metamethod call. */
  if (bcmode_a(op) == BCMdst && rc) {
    J->base[ra] = rc;
    if (ra >= J->maxslot) {
#if LJ_FR2
      if (ra > J->maxslot) J->base[ra-1] = 0;
#endif
      J->maxslot = ra+1;
    }
  }

#undef rav
#undef rbv
#undef rcv

  /* Limit the number of recorded IR instructions and constants. */
  if (J->cur.nins > REF_FIRST+(IRRef)J->param[JIT_P_maxrecord] ||
      J->cur.nk < REF_BIAS-(IRRef)J->param[JIT_P_maxirconst])
    lj_trace_err(J, LJ_TRERR_TRACEOV);
}

/* -- Recording setup ----------------------------------------------------- */

/* Setup recording for a root trace started by a hot loop. */
static const BCIns *rec_setup_root(jit_State *J)
{
  /* Determine the next PC and the bytecode range for the loop. */
  const BCIns *pcj, *pc = J->pc;
  BCIns ins = *pc;
  BCReg ra = bc_a(ins);
  switch (bc_op(ins)) {
  case BC_FORL:
    J->bc_extent = (MSize)(-bc_j(ins))*sizeof(BCIns);
    pc += 1+bc_j(ins);
    J->bc_min = pc;
    break;
  case BC_ITERL:
    if (bc_op(pc[-1]) == BC_JLOOP) {
      lj_record_s390x_linner_log(J, "rec_setup_root_iterl_jloop", LOOPEV_ENTER, 0);
      lj_trace_err(J, LJ_TRERR_LINNER);
    }
    lj_assertJ(bc_op(pc[-1]) == BC_ITERC, "no ITERC before ITERL");
    J->maxslot = ra + bc_b(pc[-1]) - 1;
    J->bc_extent = (MSize)(-bc_j(ins))*sizeof(BCIns);
    pc += 1+bc_j(ins);
    lj_assertJ(bc_op(pc[-1]) == BC_JMP, "ITERL does not point to JMP+1");
    J->bc_min = pc;
    break;
  case BC_ITERN:
#ifdef LUA_USE_ASSERT
    {
      BCOp op = bc_op(pc[1]);
      lj_assertJ(op == BC_ITERL || op == BC_IITERL || op == BC_JITERL ||
		 op == BC_LOOP || op == BC_ILOOP || op == BC_JLOOP ||
		 op == BC_JMP, "no resumable loop op after ITERN");
    }
#endif
    J->maxslot = ra;
    J->bc_extent = (MSize)(-bc_j(pc[1]))*sizeof(BCIns);
    J->bc_min = pc+2 + bc_j(pc[1]);
    J->state = LJ_TRACE_RECORD_1ST;  /* Record the first ITERN, too. */
    break;
  case BC_LOOP:
    /* Only check BC range for real loops, but not for "repeat until true". */
    pcj = pc + bc_j(ins);
    ins = *pcj;
    if (bc_op(ins) == BC_JMP && bc_j(ins) < 0) {
      J->bc_min = pcj+1 + bc_j(ins);
      J->bc_extent = (MSize)(-bc_j(ins))*sizeof(BCIns);
    }
    J->maxslot = ra;
    pc++;
    break;
  case BC_RET:
  case BC_RET0:
  case BC_RET1:
    /* No bytecode range check for down-recursive root traces. */
    J->maxslot = ra + bc_d(ins) - 1;
    break;
  case BC_FUNCF:
    /* No bytecode range check for root traces started by a hot call. */
    J->maxslot = J->pt->numparams;
    pc++;
    break;
  case BC_CALLM:
  case BC_CALL:
  case BC_ITERC:
    /* No bytecode range check for stitched traces. */
    pc++;
    break;
  default:
    lj_assertJ(0, "bad root trace start bytecode %d", bc_op(ins));
    break;
  }
  return pc;
}

/* Setup for recording a new trace. */
void lj_record_setup(jit_State *J)
{
  uint32_t i;

  /* Initialize state related to current trace. */
  memset(J->slot, 0, sizeof(J->slot));
  memset(J->chain, 0, sizeof(J->chain));
#ifdef LUAJIT_ENABLE_TABLE_BUMP
  memset(J->rbchash, 0, sizeof(J->rbchash));
#endif
  memset(J->bpropcache, 0, sizeof(J->bpropcache));
  J->scev.idx = REF_NIL;
  setmref(J->scev.pc, NULL);

  J->baseslot = 1+LJ_FR2;  /* Invoking function is at base[-1-LJ_FR2]. */
  J->base = J->slot + J->baseslot;
  J->maxslot = 0;
  J->framedepth = 0;
  J->retdepth = 0;

  J->instunroll = J->param[JIT_P_instunroll];
  J->loopunroll = J->param[JIT_P_loopunroll];
  J->tailcalled = 0;
  J->loopref = 0;

  J->bc_min = NULL;  /* Means no limit. */
  J->bc_extent = ~(MSize)0;

  /* Emit instructions for fixed references. Also triggers initial IR alloc. */
  emitir_raw(IRT(IR_BASE, IRT_PGC), J->parent, J->exitno);
  for (i = 0; i <= 2; i++) {
    IRIns *ir = IR(REF_NIL-i);
    ir->i = 0;
    ir->t.irt = (uint8_t)(IRT_NIL+i);
    ir->o = IR_KPRI;
    ir->prev = 0;
  }
  J->cur.nk = REF_TRUE;

  J->startpc = J->pc;
  setmref(J->cur.startpc, J->pc);
  setmref(J->cur.resumepc, NULL);
  J->cur.resumeins = 0;
  J->cur.resumevalid = 0;
  J->cur.unused1 = 0;
  if (J->parent) {  /* Side trace. */
    GCtrace *T = traceref(J, J->parent);
    TraceNo root = T->root ? T->root : J->parent;
    int allow_extra_loop = 1;
    J->cur.root = (uint16_t)root;
    J->cur.startins = BCINS_AD(BC_JMP, 0, 0);
    lj_record_s390x_setup_log(J, "side_enter");
    lj_record_s390x_side_focus_log(J, "enter", T);
    if (lj_record_s390x_no_extra_loop_cont_stub_enabled() &&
	J->exitno == 0 &&
	T->root == 1 &&
	bc_op(T->startins) == BC_JMP &&
	T->snap[0].nent == 0) {
      allow_extra_loop = 0;
      if (lj_record_s390x_stop_log_enabled()) {
	fprintf(stderr,
		"S390X_RECSETUP trace=%u parent=%u exit=%u suppress_extra_loop startop=%u root=%u snap0_nent=%u\n",
		(unsigned int)J->cur.traceno, (unsigned int)J->parent,
		(unsigned int)J->exitno, (unsigned int)bc_op(T->startins),
		(unsigned int)T->root, (unsigned int)T->snap[0].nent);
      }
    }
    /* Check whether we could at least potentially form an extra loop. */
    if (allow_extra_loop && J->exitno == 0 && T->snap[0].nent == 0) {
      if (lj_record_s390x_side_focus_enabled()) {
	int prev_is_jfori = (J->pc > proto_bc(J->pt) && bc_op(J->pc[-1]) == BC_JFORI);
	TraceNo fori_target = prev_is_jfori ? bc_d(J->pc[bc_j(J->pc[-1])-1]) : 0;
	fprintf(stderr,
		"S390X_SIDE_FOCUS site=extra_loop_check trace=%u parent=%u exit=%u root=%u startop=%u op=%u prevop=%u snap0_nent=%u prev_is_jfori=%u fori_target=%u target_match=%u\n",
		(unsigned int)J->cur.traceno, (unsigned int)J->parent,
		(unsigned int)J->exitno, (unsigned int)root,
		(unsigned int)bc_op(J->cur.startins),
		(unsigned int)bc_op(*J->pc),
		(unsigned int)(J->pc > proto_bc(J->pt) ? bc_op(J->pc[-1]) : BC__MAX),
		(unsigned int)T->snap[0].nent,
		(unsigned int)prev_is_jfori,
		(unsigned int)fori_target,
		(unsigned int)(prev_is_jfori && fori_target == root));
      }
      /* We can narrow a FORL for some side traces, too. */
      if (J->pc > proto_bc(J->pt) && bc_op(J->pc[-1]) == BC_JFORI &&
	  bc_d(J->pc[bc_j(J->pc[-1])-1]) == root) {
	if (lj_record_s390x_side_focus_enabled()) {
	  fprintf(stderr,
		  "S390X_SIDE_FOCUS site=extra_loop_narrow trace=%u parent=%u exit=%u root=%u startop=%u op=%u prevop=%u\n",
		  (unsigned int)J->cur.traceno, (unsigned int)J->parent,
		  (unsigned int)J->exitno, (unsigned int)root,
		  (unsigned int)bc_op(J->cur.startins),
		  (unsigned int)bc_op(*J->pc),
		  (unsigned int)bc_op(J->pc[-1]));
	}
	lj_snap_add(J);
	rec_for_loop(J, J->pc-1, &J->scev, 1);
	goto sidecheck;
      }
    } else {
      J->startpc = NULL;  /* Prevent forming an extra loop. */
    }
    lj_record_s390x_side_replay_log(J, "before_replay", T);
    lj_snap_replay(J, T);
    lj_record_s390x_side_replay_log(J, "after_replay", T);
    lj_record_s390x_side_focus_log(J, "after_replay", T);
  sidecheck:
    lj_record_s390x_side_replay_log(J, "after_sidecheck", T);
    lj_record_s390x_side_focus_log(J, "after_sidecheck", T);
    lj_record_s390x_setup_log(J, "side_ready");
    {
      int root_limit = (traceref(J, J->cur.root)->nchild >= J->param[JIT_P_maxside]);
      int snap_limit = (T->snap[J->exitno].count >= J->param[JIT_P_hotexit] +
						J->param[JIT_P_tryside]);
      int loopdesc_interp_bypass = 0;
      if (0 &&
	  (root_limit || snap_limit) &&
	  J->parent >= 3 && J->exitno == 0 && J->cur.root == 1 &&
	  bc_op(J->cur.startins) == BC_JMP && bc_op(*J->pc) == BC_JLOOP)
	loopdesc_interp_bypass = 1;
      if ((root_limit || snap_limit) && lj_record_s390x_stop_log_enabled()) {
	fprintf(stderr,
		"S390X_RECSETUP trace=%u parent=%u exit=%u sidecheck_interp root=%u startop=%u op=%u root_nchild=%u maxside=%u snapcount=%u snaplimit=%u bypass=%u\n",
		(unsigned int)J->cur.traceno, (unsigned int)J->parent,
		(unsigned int)J->exitno, (unsigned int)J->cur.root,
		(unsigned int)bc_op(J->cur.startins),
		(unsigned int)bc_op(*J->pc),
		(unsigned int)traceref(J, J->cur.root)->nchild,
		(unsigned int)J->param[JIT_P_maxside],
		(unsigned int)T->snap[J->exitno].count,
		(unsigned int)(J->param[JIT_P_hotexit] + J->param[JIT_P_tryside]),
		(unsigned int)loopdesc_interp_bypass);
      }
	      if ((root_limit || snap_limit) && !loopdesc_interp_bypass) {
	      lj_record_s390x_side_focus_log(J, "sidecheck_interp", T);
	      if (bc_op(*J->pc) == BC_JLOOP) {
		BCIns startins = traceref(J, bc_d(*J->pc))->startins;
		if (bc_op(startins) == BC_ITERN)
		  rec_itern(J, bc_a(startins), bc_b(startins));
	      }
	      lj_record_stop(J, LJ_TRLINK_INTERP, 0);
	      }
	    }
  } else {  /* Root trace. */
    int s390x_defer_root_snap = 0;
    J->cur.root = 0;
    J->cur.startins = *J->pc;
    J->pc = rec_setup_root(J);
    if (LJ_TARGET_S390X)
      s390x_defer_root_snap = lj_record_s390x_root_forl_array_snapshot_defer(J);
    /* Note: the loop instruction itself is recorded at the end and not
    ** at the start! So snapshot #0 needs to point to the *next* instruction.
    ** The one exception is BC_ITERN, which sets LJ_TRACE_RECORD_1ST.
    */
    if (!s390x_defer_root_snap)
      lj_snap_add(J);
    if (bc_op(J->cur.startins) == BC_FORL) {
      rec_for_loop(J, J->pc-1, &J->scev, 1);
      if (s390x_defer_root_snap) {
	lj_record_s390x_root_forl_array_snapshot_preload(J, &J->scev);
	lj_snap_add(J);
      }
    } else if (bc_op(J->cur.startins) == BC_ITERC)
      J->startpc = NULL;
    if (1 + J->pt->framesize >= LJ_MAX_JSLOTS)
      lj_trace_err(J, LJ_TRERR_STACKOV);
    lj_record_s390x_setup_log(J, "root_ready");
  }
#if LJ_HASPROFILE
  J->prev_pt = NULL;
  J->prev_line = -1;
#endif
#ifdef LUAJIT_ENABLE_CHECKHOOK
  /* Regularly check for instruction/line hooks from compiled code and
  ** exit to the interpreter if the hooks are set.
  **
  ** This is a compile-time option and disabled by default, since the
  ** hook checks may be quite expensive in tight loops.
  **
  ** Note this is only useful if hooks are *not* set most of the time.
  ** Use this only if you want to *asynchronously* interrupt the execution.
  **
  ** You can set the instruction hook via lua_sethook() with a count of 1
  ** from a signal handler or another native thread. Please have a look
  ** at the first few functions in luajit.c for an example (Ctrl-C handler).
  */
  {
    TRef tr = emitir(IRT(IR_XLOAD, IRT_U8),
		     lj_ir_kptr(J, &J2G(J)->hookmask), IRXLOAD_VOLATILE);
    tr = emitir(IRTI(IR_BAND), tr, lj_ir_kint(J, (LUA_MASKLINE|LUA_MASKCOUNT)));
    emitir(IRTGI(IR_EQ), tr, lj_ir_kint(J, 0));
  }
#endif
}

#undef IR
#undef emitir_raw
#undef emitir

#endif
