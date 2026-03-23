/*
** S390X IR assembler scaffolding (SSA IR -> machine code).
** Copyright (C) 2005-2026 Mike Pall. See Copyright Notice in luajit.h
**
** This is a staged bring-up skeleton. It provides the standard backend
** surface so the tree can build with JIT enabled, but it rejects trace
** assembly explicitly until the real s390x emitter and VM/JIT handoff are
** implemented.
*/

/* -- Register allocator extensions --------------------------------------- */

static Reg ra_hintalloc(ASMState *as, IRRef ref, Reg hint, RegSet allow)
{
  Reg r = IR(ref)->r;
  if (ra_noreg(r)) {
    if (!ra_hashint(r) && !iscrossref(as, ref) && hint != RID_SP)
      ra_sethint(IR(ref)->r, hint);
    r = ra_allocref(as, ref, allow);
  }
  ra_noweak(as, r);
  return r;
}

static Reg ra_alloc2(ASMState *as, IRIns *ir, RegSet allow)
{
  IRIns *irl = IR(ir->op1), *irr = IR(ir->op2);
  Reg left = irl->r, right = irr->r;
  if (ra_hasreg(left)) {
    ra_noweak(as, left);
    if (ra_noreg(right))
      right = ra_allocref(as, ir->op2, rset_exclude(allow, left));
    else
      ra_noweak(as, right);
  } else if (ra_hasreg(right)) {
    ra_noweak(as, right);
    left = ra_allocref(as, ir->op1, rset_exclude(allow, right));
  } else if (ra_hashint(right)) {
    right = ra_allocref(as, ir->op2, allow);
    left = ra_alloc1(as, ir->op1, rset_exclude(allow, right));
  } else {
    left = ra_allocref(as, ir->op1, allow);
    right = ra_alloc1(as, ir->op2, rset_exclude(allow, left));
  }
  return left | (right << 8);
}

static uint64_t asm_k64val(ASMState *as, IRRef ref)
{
  IRIns *ir = IR(ref);
  UNUSED(as);
  if (ir->o == IR_KINT64)
    return ir_kint64(ir)->u64;
  if (ir->o == IR_KGC)
    return (uint64_t)(uintptr_t)ir_kgc(ir);
  if (ir->o == IR_KPTR || ir->o == IR_KKPTR)
    return (uint64_t)(uintptr_t)ir_kptr(ir);
  lj_assertA(ir->o == IR_KINT || ir->o == IR_KNULL,
	     "bad 64 bit const IR op %d", ir->o);
  return (uint64_t)(int64_t)ir->i;
}

static intptr_t asm_kintptr(ASMState *as, IRRef ref)
{
  return (intptr_t)asm_k64val(as, ref);
}

static LJ_NORET LJ_NOINLINE void asm_s390x_nyi_tag(ASMState *as, int32_t tag);

static int asm_s390x_ir_log_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_IR_LOG") != NULL);
  return enabled;
}

static int asm_s390x_sload_log_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_SLOAD_LOG") != NULL);
  return enabled;
}

static int asm_s390x_varg_bias_override(void)
{
  static int bias = -1000;
  if (bias == -1000) {
    const char *s = getenv("LUAJIT_S390X_VARG_BIAS");
    bias = s ? atoi(s) : -999;
  }
  return bias;
}

static int asm_s390x_varg_slot_bias_override(void)
{
  static int bias = -1000;
  if (bias == -1000) {
    const char *s = getenv("LUAJIT_S390X_VARG_SLOT_BIAS");
    bias = s ? atoi(s) : 0;
  }
  return bias;
}

static int asm_s390x_varg_slot_bias_root_override(void)
{
  static int bias = -1000;
  if (bias == -1000) {
    const char *s = getenv("LUAJIT_S390X_VARG_SLOT_BIAS_ROOT");
    bias = s ? atoi(s) : -999;
  }
  return bias;
}

static int asm_s390x_varg_slot_bias_loop_override(void)
{
  static int bias = -1000;
  if (bias == -1000) {
    const char *s = getenv("LUAJIT_S390X_VARG_SLOT_BIAS_LOOP");
    bias = s ? atoi(s) : -999;
  }
  return bias;
}

static int asm_s390x_varg_dump_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_VARG_DUMP") != NULL);
  return enabled;
}

static int asm_s390x_is_varg_vload(ASMState *as, IRIns *ir)
{
  if (LJ_BE && ir->o == IR_VLOAD) {
    IRIns *iref = IR(ir->op1);
    if (iref->o == IR_AREF) {
      IRIns *ibase = IR(iref->op1);
      if (ibase->o == IR_ADD && irref_isk(ibase->op2) &&
	  (int32_t)asm_kintptr(as, ibase->op2) == 3)
	return 1;
    }
  }
  return 0;
}

static int asm_s390x_is_loop_varg_vload(ASMState *as, IRIns *ir)
{
  IRIns *aref = IR(ir->op1);
  IRIns *idx;
  if (aref->o != IR_AREF)
    return 0;
  idx = IR(aref->op2);
  if (idx->o == IR_ADD && !irref_isk(idx->op1)) {
    IRIns *base = IR(idx->op1);
    return base->o == IR_PHI;
  }
  return 0;
}

int32_t lj_trace_s390x_varg_probe(const void *effp, int32_t ignored);

static void asm_s390x_ir_log_intcomp(ASMState *as, IRIns *ir, IROp op,
				     IRRef lref, IRRef rref, int cc,
				     Reg left, Reg right, int imm16)
{
  IRIns *lir = IR(lref);
  IRIns *rir = IR(rref);
  if (!asm_s390x_ir_log_enabled())
    return;
  fprintf(stderr,
	  "S390X_IR kind=intcomp curins=%d ir=%d op=%d cc=%d leftref=%d leftop=%d rightref=%d rightop=%d left=%d right=%d imm16=%d k=%lld type=%d\n",
	  (int)(as->curins - REF_BIAS), (int)((ir - as->ir) - REF_BIAS),
	  (int)op, cc, (int)(lref - REF_BIAS), (int)lir->o,
	  (int)(rref - REF_BIAS), (int)rir->o, (int)left, (int)right, imm16,
	  (long long)(irref_isk(rref) ? IR(rref)->i : 0),
	  (int)irt_type(ir->t));
}

static void asm_s390x_ir_log_xstore(ASMState *as, IRIns *ir, IRRef xref,
				    IRRef vref, int32_t ofs,
				    Reg base, Reg src)
{
  IRIns *xir = IR(xref);
  IRIns *vir = IR(vref);
  if (!asm_s390x_ir_log_enabled())
    return;
  fprintf(stderr,
	  "S390X_IR kind=xstore curins=%d ir=%d xref=%d xop=%d vref=%d vop=%d ofs=%d base=%d src=%d type=%d sink=%d\n",
	  (int)(as->curins - REF_BIAS), (int)((ir - as->ir) - REF_BIAS),
	  (int)(xref - REF_BIAS), (int)xir->o, (int)(vref - REF_BIAS),
	  (int)vir->o, (int)ofs,
	  (int)base, (int)src, (int)irt_type(ir->t), ir->r == RID_SINK);
}

static void asm_s390x_ir_log_aref(ASMState *as, IRIns *ir, IRRef bref,
				  IRRef iref, Reg base, Reg idx,
				  Reg dest, int32_t ofs)
{
  if (!asm_s390x_ir_log_enabled())
    return;
  fprintf(stderr,
	  "S390X_IR kind=aref curins=%d ir=%d bref=%d iref=%d base=%d idx=%d dest=%d ofs=%d type=%d\n",
	  (int)(as->curins - REF_BIAS), (int)((ir - as->ir) - REF_BIAS),
	  (int)(bref - REF_BIAS), (int)(iref - REF_BIAS), (int)base,
	  (int)idx, (int)dest, (int)ofs, (int)irt_type(ir->t));
}

static void asm_s390x_ir_log_vload(ASMState *as, IRIns *ir, IRRef bref,
				   Reg fused, Reg fbase, Reg fidx,
				   Reg dest, int32_t ofs)
{
  if (!asm_s390x_ir_log_enabled())
    return;
  fprintf(stderr,
	  "S390X_IR kind=vload curins=%d ir=%d bref=%d fused=%d fbase=%d fidx=%d dest=%d ofs=%d type=%d used=%d\n",
	  (int)(as->curins - REF_BIAS), (int)((ir - as->ir) - REF_BIAS),
	  (int)(bref - REF_BIAS), (int)fused, (int)fbase, (int)fidx,
	  (int)dest, (int)ofs,
	  (int)irt_type(ir->t), ra_used(ir));
}

static void asm_s390x_ir_log_sub(ASMState *as, IRIns *ir, IRRef lref, IRRef rref,
				 Reg dest, Reg left, Reg right)
{
  IRIns *lir = IR(lref);
  IRIns *rir = IR(rref);
  if (!asm_s390x_ir_log_enabled())
    return;
  fprintf(stderr,
	  "S390X_IR kind=sub curins=%d ir=%d lref=%d lop=%d rref=%d rop=%d dest=%d left=%d right=%d type=%d\n",
	  (int)(as->curins - REF_BIAS), (int)((ir - as->ir) - REF_BIAS),
	  (int)(lref - REF_BIAS), (int)lir->o, (int)(rref - REF_BIAS), (int)rir->o,
	  (int)dest, (int)left, (int)right, (int)irt_type(ir->t));
}

static void asm_s390x_ir_log_addk(ASMState *as, IRIns *ir, IRRef lref,
				  IRRef rref, Reg dest, Reg left, int32_t k)
{
  IRIns *rir = IR(rref);
  if (!asm_s390x_ir_log_enabled())
    return;
  fprintf(stderr,
	  "S390X_IR kind=addk curins=%d ir=%d lref=%d rref=%d rop=%d dest=%d left=%d k=%d type=%d\n",
	  (int)(as->curins - REF_BIAS), (int)((ir - as->ir) - REF_BIAS),
	  (int)(lref - REF_BIAS), (int)(rref - REF_BIAS), (int)rir->o,
	  (int)dest, (int)left, (int)k, (int)irt_type(ir->t));
}

static RegSet asm_s390x_dest_gprset(IRType1 t)
{
  if (irt_isp32(t) || irt_type(t) == IRT_P64) {
    RegSet saved = RSET_GPR_SAVED & (RSET_GPR & ~RID2RSET(RID_BASE));
    if (saved != RSET_EMPTY)
      return saved;
  }
  return (RSET_GPR & ~RID2RSET(RID_BASE));
}

static Reg ra_allocbase(ASMState *as, RegSet allow)
{
  RegSet preserved = allow & RSET_GPR_BASE;
  if (preserved)
    allow = preserved;
  else
    preserved = allow & RSET_GPR_SAVED;
  if (preserved)
    allow = preserved;
  return ra_alloc1(as, REF_BASE, allow);
}

static Reg ra_alloc1_nobase(ASMState *as, IRRef ref, RegSet allow, int32_t tag)
{
  Reg r = ra_alloc1(as, ref, allow);
  if (LJ_UNLIKELY(ref != REF_BASE && (r == RID_BASE || r == RID_SP)))
    asm_s390x_nyi_tag(as, tag);
  return r;
}

static Reg ra_hintalloc_nobase(ASMState *as, IRRef ref, Reg hint, RegSet allow,
			       int32_t tag)
{
  Reg r = ra_hintalloc(as, ref, hint, allow);
  if (LJ_UNLIKELY(ref != REF_BASE && (r == RID_BASE || r == RID_SP)))
    asm_s390x_nyi_tag(as, tag);
  return r;
}

static Reg ra_dest_nobase(ASMState *as, IRIns *ir, RegSet allow, int32_t tag)
{
  Reg r = ra_dest(as, ir, allow);
  if (LJ_UNLIKELY(r == RID_BASE || r == RID_SP))
    asm_s390x_nyi_tag(as, tag);
  return r;
}

/* Keep RID_BASE available for explicit REF_BASE materialization, but do not
** hand it out as a generic temp/result register in ordinary lowering.
*/
#define RSET_GPR_NOB		(rset_exclude(RSET_GPR, RID_BASE))
#define RSET_GPR_CALL_NOB	((RSET_GPR & ~RSET_SCRATCH_GPR) & ~RID2RSET(RID_BASE))
/* Reserve the psABI caller save area for traced helper calls. This mirrors
** the s390x FFI call-side requirement of 160 bytes.
*/
#define S390X_CALL_SPS_EXTRA	20

static void asm_gencall_preserve(ASMState *as, IRRef ref, Reg gpr)
{
  Reg save;
  RegSet allow = RSET_GPR_CALL_NOB & ~RID2RSET(gpr);
  IRIns *ir = IR(ref);
  lj_assertA(!irref_isk(ref), "bad preserved call arg K%03d", REF_BIAS - ref);
  lj_assertA(ir->r == gpr, "call arg %04d not in reg %d", ref - REF_BIAS, gpr);
  /* asm_gencall() clears arg-register costs before preserving live args.
  ** Restore the allocator identity here so ra_rename() moves the right ref.
  */
  as->cost[gpr] = REGCOST_REF_T(ref, irt_t(ir->t));
  if (rset_test(as->freeset, gpr)) {
    rset_clear(as->freeset, gpr);
    ra_noweak(as, gpr);
  }
  lj_assertA(allow != RSET_EMPTY, "no preserved reg for call arg %04d",
	     ref - REF_BIAS);
  save = ra_pick(as, allow);
  ra_rename(as, gpr, save);
}

static RegSet asm_gencall_nonarg_gpr(Reg gpr)
{
  RegSet allow = RSET_GPR_CALL_NOB & ~RID2RSET(gpr);
  if (allow == RSET_EMPTY)
    allow = rset_exclude(RSET_GPR_CALL_NOB, gpr);
  return allow;
}

static void asm_guardcc(ASMState *as, int cc);
static void asm_tvstore64(ASMState *as, Reg base, int32_t ofs, IRRef ref);

static int asm_gencall_sload(ASMState *as, Reg gpr, IRRef ref)
{
  IRIns *ir = IR(ref);
  int32_t ofs;
  IRType1 t;
  Reg base;
  RegSet allow;

  if (ir->o != IR_SLOAD)
    return 0;
  t = ir->t;
  if (!(irt_isint(t) || irt_isu32(t) || irt_isaddr(t)))
    return 0;

  ofs = 8 * ((int32_t)ir->op1 - 2);
  allow = rset_exclude(RSET_GPR_NOB, gpr);
  base = ra_scratch(as, allow);
  rset_clear(allow, base);

  if (ir->op2 & IRSLOAD_TYPECHECK) {
    Reg tmp = ra_scratch(as, allow);
    asm_guardcc(as, CC_NE);
    emit_u32(as, S390X_INS_RI(S390XI_CGHI, tmp, (int32_t)irt_toitype(t)));
    emit_shiftimm(as, S390XI_SRAG, tmp, tmp, 47);
    emit_load64ofs(as, tmp, base, ofs);
  }
  if (irt_isaddr(t)) {
    emit_shiftimm(as, S390XI_SRLG, gpr, gpr, 17);
    emit_shiftimm(as, S390XI_SLLG, gpr, gpr, 17);
    emit_load64ofs(as, gpr, base, ofs);
  } else {
    if (irt_isint(t))
      emit_u32(as, S390X_INS_RXE(S390XI_LGFR, gpr, gpr));
    emit_loadu32ofs(as, gpr, base, ofs + (LJ_BE ? 4 : 0));
  }
  emit_getgl(as, base, jit_base);
  return 1;
}

/* -- Shared NYI helpers -------------------------------------------------- */

static LJ_NORET LJ_NOINLINE void asm_s390x_nyi(ASMState *as)
{
  setintV(&as->J->errinfo, -1);
  lj_trace_err_info(as->J, LJ_TRERR_NYIIR);
}

static LJ_NORET LJ_NOINLINE void asm_s390x_nyi_tag(ASMState *as, int32_t tag)
{
  setintV(&as->J->errinfo, tag);
  lj_trace_err_info(as->J, LJ_TRERR_NYIIR);
}

static LJ_NORET LJ_NOINLINE void asm_s390x_nyi_ir(ASMState *as, IRIns *ir)
{
  setintV(&as->J->errinfo, ir->o);
  lj_trace_err_info(as->J, LJ_TRERR_NYIIR);
}

#define ASM_S390X_STUB_IR(name) \
  static void name(ASMState *as, IRIns *ir) { asm_s390x_nyi_ir(as, ir); }

/* -- Guard handling ------------------------------------------------------ */

static void asm_exitstub_setup(ASMState *as, ExitNo nexits)
{
  ExitNo i;
  MCode *target = (MCode *)(void *)lj_vm_exit_handler;
  MCode *mxp = as->mctop;
  if (mxp - (nexits * EXITSTUB_SPACING + MCLIM_REDZONE) < as->mclim)
    asm_mclimit(as);
  as->mcp = mxp;
  for (i = nexits; i > 0; i--) {
    ExitNo exitno = i - 1;
    emit_u32(as, (uint32_t)as->T->traceno);
    emit_u32(as, (uint32_t)exitno);
    emit_call(as, RID_R14, target);
  }
  as->mcexit = as->mcp;
  as->mctop = as->mcp;
}

static MCode *asm_exitstub_addr(ASMState *as, ExitNo exitno)
{
  return as->mcexit + exitno * EXITSTUB_SPACING;
}

static int asm_guardcc_invert(int cc)
{
  if (cc == CC_EQ) return CC_NE;
  if (cc == CC_NE) return CC_EQ;
  if (cc == CC_LT) return CC_GE;
  if (cc == CC_GE) return CC_LT;
  if (cc == CC_GT) return CC_LE;
  if (cc == CC_LE) return CC_GT;
  if (cc == CC_AL) return CC_AL;
  return cc ^ 14;
}

static void asm_guardcc(ASMState *as, int cc)
{
  MCode *target = asm_exitstub_addr(as, as->snapno);
  MCode *p = as->mcp;
  lj_asm_s390x_guard_log(as, cc, target, p, 0);
  if (LJ_UNLIKELY(p == as->invmcp)) {
    as->loopinv = 1;
    lj_asm_s390x_guard_log(as, cc, target, p, 1);
    *p = S390X_INS_BRC(CC_AL, (int32_t)(((char *)target - (char *)p) >> 1));
    emit_condbranch(as, (S390XCC)asm_guardcc_invert(cc), p);
    return;
  }
  emit_condbranch(as, (S390XCC)cc, target);
}

/* -- Trace setup --------------------------------------------------------- */

/* Must match SAVE_L in vm_s390x.dasc. */
#define S390X_OFS_SAVE_L 256

static void asm_setup_target(ASMState *as)
{
  asm_exitstub_setup(as, as->T->nsnap + (as->parent ? 1 : 0));
}

static void asm_tail_prep(ASMState *as, TraceNo lnk)
{
  MCode *p = as->mctop - (as->loopref ? 1 : (lnk ? 3 : 7));
  if (as->loopref) {
    as->invmcp = as->mcp = p;
  } else {
    UNUSED(lnk);
    as->mcp = p;
    as->invmcp = NULL;
  }
  as->mctail = p;
  *p = 0;  /* Keep the reserved slot explicit for later patching. */
  if (!as->loopref) {
    p[1] = 0;
    p[2] = 0;
  }
}

static void asm_tail_fixup(ASMState *as, TraceNo lnk)
{
  MCode *mcp = as->mctail;
  MCode *target = lnk ? traceref(as->J, lnk)->mcode :
			(MCode *)(void *)lj_vm_exit_interp;
  int32_t spadj = as->T->spadjust;
  ptrdiff_t delta;

  if (spadj) {
    lj_assertA(checki16(spadj), "s390x stack adjustment out of range");
    *mcp++ = S390X_INS_RI(S390XI_AGHI, RID_SP, spadj);
  }
  if (!lnk) {
    emit_u48_at(mcp, S390X_INS_RXY(S390XI_LG, RID_TMP, 0, RID_DISPATCH,
				   emit_gl_ofs(cur_L)));
    mcp += 2;
    emit_u48_at(mcp, S390X_INS_RXY(S390XI_STG, RID_TMP, 0, RID_SP,
				   S390X_OFS_SAVE_L));
    mcp += 2;
  }

  delta = (char *)target - (char *)mcp;
  lj_assertA((delta & 1) == 0, "unaligned tail branch target");
  if (checki16((int32_t)(delta >> 1))) {
    *mcp++ = S390X_INS_BRC(CC_AL, (int32_t)(delta >> 1));
  } else {
    lj_assertA(checki32((int64_t)(delta >> 1)),
	       "s390x tail branch target out of range");
    emit_u48_at(mcp, S390X_INS_BRCL(CC_AL, (int32_t)(delta >> 1)));
    mcp += 2;
  }

  while (as->mctop > mcp)
    *mcp++ = 0x07070707u;  /* Four 2-byte NOPR instructions. */
}

static void asm_loop_fixup(ASMState *as)
{
  MCode *p = as->mctop;
  MCode *target = as->mcp;
  ptrdiff_t delta;
  if (as->loopinv) {  /* Guard inversion already consumed the tail slot. */
    delta = (char *)target - (char *)(p - 2);
    lj_assertA((delta & 1) == 0, "unaligned inverted loop branch target");
    lj_assertA(checki16((int32_t)(delta >> 1)),
	       "s390x inverted loop branch target out of range");
    p[-2] = (p[-2] & 0xffff0000u) | (uint16_t)(delta >> 1);
  } else {
    delta = (char *)target - (char *)(p - 1);
    lj_assertA((delta & 1) == 0, "unaligned loop branch target");
    lj_assertA(checki16((int32_t)(delta >> 1)),
	       "s390x loop branch target out of range");
    p[-1] = S390X_INS_BRC(CC_AL, (int32_t)(delta >> 1));
  }
}

static void asm_loop_tail_fixup(ASMState *as)
{
  UNUSED(as);  /* Fixed-width branch patching needs no extra tail cleanup. */
}

static void asm_head_root_base(ASMState *as)
{
  IRIns *ir = IR(REF_BASE);
  Reg r = ir->r;
#if LJ_TARGET_S390X
  emit_getgl(as, RID_BASE, jit_base);
#endif
  if (ra_hasreg(r)) {
    ra_free(as, r);
    if (rset_test(as->modset, r) || irt_ismarked(ir->t))
      ir->r = RID_INIT;  /* No inheritance for a modified BASE register. */
    if (r != RID_BASE)
      emit_movrr(as, ir, r, RID_BASE);
  }
}

static Reg asm_head_side_base(ASMState *as, IRIns *irp)
{
  IRIns *ir = IR(REF_BASE);
  Reg r = ir->r;
#if LJ_TARGET_S390X
  emit_getgl(as, RID_BASE, jit_base);
#endif
  if (ra_hasreg(r)) {
    ra_free(as, r);
    if (rset_test(as->modset, r) || irt_ismarked(ir->t))
      ir->r = RID_INIT;  /* No inheritance for a modified BASE register. */
    if (irp->r == r) {
      return r;
    } else if (ra_hasreg(irp->r) && rset_test(as->freeset, irp->r)) {
      emit_movrr(as, ir, r, irp->r);
      return irp->r;
    } else {
      emit_getgl(as, r, jit_base);
    }
  }
  return RID_NONE;
}

static void asm_stack_check(ASMState *as, BCReg topslot,
			    IRIns *irp, RegSet allow, ExitNo exitno)
{
  Reg pbase = RID_BASE;
  ExitNo oldsnap = as->snapno;
  Reg tmp, tmpload;
  int remat_pbase = 0;
  RegSet atmp;

  lj_assertA(checki16(8 * (int32_t)topslot), "stack check slot offset out of range");
  if (irp) {
    if (ra_hasreg(irp->r)) {
      pbase = irp->r;
    } else {
      remat_pbase = 1;
      if (allow != RSET_EMPTY) {
	Reg picked = rset_pickbot(allow);
	pbase = picked;
	rset_clear(allow, picked);
      } else {
	pbase = RID_R2;
      }
    }
  }

  atmp = rset_exclude(allow, pbase);
  if (atmp != RSET_EMPTY) {
    tmp = rset_pickbot(atmp);
    ra_modified(as, tmp);
  } else {
    tmp = (pbase != RID_R1) ? RID_R1 : RID_R2;
  }
  if (tmp == pbase)
    tmp = RID_R3;
  tmpload = tmp;
  if (tmpload == RID_TMP)
    tmpload = RID_R3;

  as->snapno = exitno;
  asm_guardcc(as, CC_LT);
  as->snapno = oldsnap;
  emit_u32(as, S390X_INS_RI(S390XI_CGHI, tmp, 8 * (int32_t)topslot));
  emit_u32(as, S390X_INS_RXE(S390XI_SGR, tmp, pbase));
  emit_load64ofs(as, tmp, tmpload, (int32_t)offsetof(lua_State, maxstack));
  emit_getgl(as, tmpload, cur_L);
  if (remat_pbase) {
    emit_getgl(as, pbase, jit_base);
    if (allow != RSET_EMPTY)
      ra_modified(as, pbase);
  }
}

static void asm_stack_restore(ASMState *as, SnapShot *snap)
{
  SnapEntry *map = &as->T->snapmap[snap->mapofs];
  MSize n, nent = snap->nent;
  for (n = 0; n < nent; n++) {
    SnapEntry sn = map[n];
    BCReg s = snap_slot(sn);
    int32_t ofs = 8 * ((int32_t)s - 1 - LJ_FR2);
    IRRef ref = snap_ref(sn);
    IRIns *ir = IR(ref);
    RegSet allow;
    Reg src;

    if ((sn & SNAP_NORESTORE))
      continue;

    allow = rset_exclude(RSET_GPR, RID_BASE);
    if ((sn & SNAP_KEYINDEX)) {
      src = irref_isk(ref) ? ra_allock(as, ir->i, allow) :
			     ra_alloc1(as, ref, allow);
      lj_assertA(src != RID_SP, "snap keyindex restore picked RID_SP");
      rset_clear(allow, src);
      emit_store32ofs(as, src, RID_BASE, ofs + (LJ_BE ? 4 : 0));
      emit_store32ofs(as, ra_allock(as, LJ_KEYINDEX, allow), RID_BASE,
		      ofs + (LJ_BE ? 0 : 4));
      checkmclim(as);
      continue;
    }

    if (irt_isint(ir->t) || irt_isu32(ir->t)) {
      src = irref_isk(ref) ? ra_allock(as, ir->i, allow) :
			     ra_alloc1(as, ref, allow);
      lj_assertA(src != RID_SP, "snap int restore picked RID_SP");
      rset_clear(allow, src);
      emit_store32ofs(as, src, RID_BASE, ofs + (LJ_BE ? 4 : 0));
      emit_store32ofs(as,
		      ra_allock(as, (int32_t)((uint32_t)LJ_TISNUM << 15), allow),
		      RID_BASE, ofs + (LJ_BE ? 0 : 4));
    } else if (irt_isnum(ir->t)) {
      src = ra_alloc1(as, ref, RSET_FPR);
      emit_u48_pad8(as, S390X_INS_RXY(S390XI_STDY, src, 0, RID_BASE, ofs));
    } else {
      asm_tvstore64(as, RID_BASE, ofs, ref);
    }
    checkmclim(as);
  }
}

/* -- Calls and shared helpers ------------------------------------------- */

static void asm_gencall(ASMState *as, const CCallInfo *ci, IRRef *args)
{
  uint32_t n, nargs = CCI_XNARGS(ci);
  Reg gpr = REGARG_FIRSTGPR;
  if (ci->flags & (CCI_VARARG|CCI_CASTU64)) {
    asm_s390x_nyi_tag(as, -116);
    return;
  }
  if (nargs > REGARG_NUMGPR) {
    asm_s390x_nyi_tag(as, -113);
    return;
  }
  if (ci->func)
    emit_call(as, RID_R14, (void *)ci->func);
  for (gpr = REGARG_FIRSTGPR; gpr <= REGARG_LASTGPR; gpr++) {
    IRRef ref = regcost_ref(as->cost[gpr]);
    if (!ra_iskref(ref) && ref <= as->T->nins && IR(ref)->r == gpr)
      ra_sethint(IR(ref)->r, gpr);
    as->cost[gpr] = REGCOST(~0u, ASMREF_L);
  }
  gpr = REGARG_FIRSTGPR;
  for (n = 0; n < nargs; n++, gpr++) {
    IRRef ref = args[n];
    if (!ref)
      continue;
    if (irt_isfp(IR(ref)->t)) {
      asm_s390x_nyi_tag(as, -115);
      return;
    }
    if (asm_gencall_sload(as, gpr, ref))
      continue;
    if (!irref_isk(ref)) {
      Reg src = IR(ref)->r;
      if (ra_hasreg(src) &&
	  src >= REGARG_FIRSTGPR && src <= REGARG_LASTGPR)
	asm_gencall_preserve(as, ref, src);
      ra_alloc1(as, ref, asm_gencall_nonarg_gpr(gpr));
    }
    lj_assertA(rset_test(as->freeset, gpr), "reg %d not free", gpr);
    ra_leftov(as, gpr, ref);
  }
}

static void asm_setupresult(ASMState *as, IRIns *ir, const CCallInfo *ci)
{
  RegSet drop = RSET_SCRATCH;
  Reg retreg = RID_RET;
  int hiop = ((ir+1)->o == IR_HIOP && !irt_isnil((ir+1)->t));
  if (ir->o == IR_CALLL && ir->op2 == IRCALL_lj_vm_next)
    retreg = RID_RETLO;
  if ((ci->flags & CCI_NOFPRCLOBBER))
    drop &= ~RSET_FPR;
  if (ra_hasreg(ir->r))
    rset_clear(drop, ir->r);
  if (hiop && ra_hasreg((ir+1)->r))
    rset_clear(drop, (ir+1)->r);
  ra_evictset(as, drop);
  if (ra_used(ir)) {
    lj_assertA(!irt_ispri(ir->t), "PRI dest");
    if (irt_isfp(ir->t)) {
      ra_destreg(as, ir, RID_FPRET);
    } else if (hiop) {
      ra_destpair(as, ir);
    } else {
      ra_destreg(as, ir, retreg);
      if (irt_isint(ir->t))
        emit_u32(as, S390X_INS_RXE(S390XI_LGFR, retreg, retreg));
      else if (irt_isu32(ir->t))
        emit_u32(as, S390X_INS_RXE(S390XI_LLGFR, retreg, retreg));
    }
  }
}

static void asm_gc_check(ASMState *as)
{
  const CCallInfo *ci = &lj_ir_callinfo[IRCALL_lj_gc_step_jit];
  IRRef args[2];
  MCode *l_end;
  Reg tmp1, tmp2;

  ra_evictset(as, RSET_SCRATCH);
  l_end = as->mcp;

  /* Exit trace if in GCSatomic or GCSfinalize. Assumes asm_snap_prep() ran. */
  asm_guardcc(as, CC_NE);
  emit_u32(as, S390X_INS_RI(S390XI_CGHI, RID_RET, 0));

  args[0] = ASMREF_TMP1;  /* global_State *g */
  args[1] = ASMREF_TMP2;  /* MSize steps     */
  asm_gencall(as, ci, args);

  tmp1 = ra_releasetmp(as, ASMREF_TMP1);
  tmp2 = ra_releasetmp(as, ASMREF_TMP2);
  lj_assertA(tmp1 != RID_SP && tmp2 != RID_SP, "gc tmp uses RID_SP");
  emit_loadi(as, tmp2, as->gcsteps);
  /* Jump around GC step if GC total < GC threshold. */
  emit_condbranch(as, CC_LO, l_end);
  emit_u32(as, S390X_INS_RXE(S390XI_CLGR, RID_TMP, tmp2));
  emit_getgl(as, tmp2, gc.threshold);
  emit_getgl(as, RID_TMP, gc.total);
  emit_addptr(as, tmp1, GG_DISP2G);
  if (tmp1 != RID_DISPATCH)
    emit_u32(as, S390X_INS_RXE(S390XI_LGR, tmp1, RID_DISPATCH));

  as->gcsteps = 0;
  checkmclim(as);
}

static void asm_tvstore64(ASMState *as, Reg base, int32_t ofs, IRRef ref)
{
  RegSet allow = rset_exclude(RSET_GPR, base);
  IRIns *ir = IR(ref);
  lj_assertA(irt_ispri(ir->t) || irt_isaddr(ir->t) || irt_isinteger(ir->t),
	     "store of IR type %d", irt_type(ir->t));
  if (irref_isk(ref)) {
    TValue k;
    Reg tmp = ra_scratch(as, allow);
    lj_ir_kvalue(as->J->L, &k, ir);
    emit_store64ofs(as, tmp, base, ofs);
    emit_loadu64(as, tmp, k.u64);
  } else if (irt_ispri(ir->t)) {
    Reg tmp = ra_scratch(as, allow);
    emit_store64ofs(as, tmp, base, ofs);
    emit_loadu64(as, tmp, (uint64_t)(~((int64_t)~irt_toitype(ir->t) << 47)));
  } else {
    Reg src = ra_alloc1(as, ref, allow);
    Reg tmp, type;
    allow = rset_exclude(allow, src);
    tmp = ra_scratch(as, allow);
    allow = rset_exclude(allow, tmp);
    type = ra_scratch(as, allow);
    emit_store64ofs(as, tmp, base, ofs);
    emit_u32(as, S390X_INS_RXE(S390XI_AGR, tmp, type));
    emit_loadu64(as, type, irt_isinteger(ir->t) ?
      ((uint64_t)(uint32_t)LJ_TISNUM << 47) :
      ((uint64_t)irt_toitype(ir->t) << 47));
    if (irt_isinteger(ir->t))
      emit_u32(as, S390X_INS_RXE(S390XI_LLGFR, tmp, src));
    else if (tmp != src)
      emit_movrr(as, ir, tmp, src);
  }
}

static void asm_tvptr(ASMState *as, Reg dest, IRRef ref, MSize mode)
{
  if ((mode & IRTMPREF_IN1)) {
    IRIns *ir = IR(ref);
    if (irt_isnum(ir->t)) {
      asm_s390x_nyi_tag(as, -110);
      return;
    }
    asm_tvstore64(as, dest, 0, ref);
  }
  emit_addptr(as, dest, emit_gl_ofs(tmptv));
  if (dest != RID_DISPATCH)
    emit_u32(as, S390X_INS_RXE(S390XI_LGR, dest, RID_DISPATCH));
}

static void asm_bufhdr_write(ASMState *as, Reg sb)
{
  UNUSED(sb);
  asm_s390x_nyi_tag(as, -111);
}

static Reg asm_setup_call_slots(ASMState *as, IRIns *ir, const CCallInfo *ci)
{
  IRRef args[CCI_NARGS_MAX*2];
  uint32_t i, nargs = CCI_XNARGS(ci);
  /* Traced helper calls need the s390x ABI caller save area at the current
  ** stack pointer in addition to the normal TValue spill area.
  */
  int nslots = SPS_FIRST + S390X_CALL_SPS_EXTRA * 2;
  int ngpr = REGARG_NUMGPR, nfpr = REGARG_NUMFPR;
  asm_collectargs(as, ir, ci, args);
  for (i = 0; i < nargs; i++) {
    IRRef ref = args[i];
    if (!ref)
      continue;
    if (irt_isfp(IR(ref)->t)) {
      if (nfpr > 0)
	nfpr--;
      else
	nslots += 2;
    } else {
      if (ngpr > 0)
	ngpr--;
      else
	nslots += 2;
    }
  }
  if (nslots > as->evenspill)
    as->evenspill = nslots;
  return REGSP_HINT(irt_isfp(ir->t) ? RID_FPRET : RID_RET);
}

static void asm_callx(ASMState *as, IRIns *ir)
{
  IRRef args[CCI_NARGS_MAX*2];
  CCallInfo ci;
  IRRef func;
  IRIns *irf;

  ci.flags = asm_callx_flags(as, ir);
  asm_collectargs(as, ir, &ci, args);
  asm_setupresult(as, ir, &ci);

  func = ir->op2;
  irf = IR(func);
  if (irf->o == IR_CARG) {
    func = irf->op1;
    irf = IR(func);
  }

  if (irref_isk(func)) {
    intptr_t addr = asm_kintptr(as, func);
    uint8_t *p = (uint8_t *)as->mcp - 8;
    ptrdiff_t delta = (char *)(void *)(uintptr_t)addr - (char *)p;
    if ((delta & 1) == 0 && checki32((int64_t)(delta >> 1))) {
      ci.func = (ASMFunction)(void *)(uintptr_t)addr;
    } else {
      RegSet allow = (RSET_GPR & ~RSET_SCRATCH_GPR) &
		     ~RSET_RANGE(REGARG_FIRSTGPR, REGARG_LASTGPR+1);
      Reg freg = ra_allock(as, addr, allow);
      emit_callr(as, RID_R14, freg);
      ci.func = (ASMFunction)(void *)0;
    }
  } else {
    RegSet allow = (RSET_GPR & ~RSET_SCRATCH_GPR) &
		   ~RSET_RANGE(REGARG_FIRSTGPR, REGARG_LASTGPR+1);
    Reg freg = ra_alloc1(as, func, allow);
    emit_callr(as, RID_R14, freg);
    ci.func = (ASMFunction)(void *)0;
  }

  asm_gencall(as, &ci, args);
}

/* -- IR lowerers --------------------------------------------------------- */

#define CC_UNSIGNED	0x10

static const uint8_t asm_compmap[IR_ABC+1] = {
  /* LT  */ CC_GE,
  /* GE  */ CC_LT,
  /* LE  */ CC_GT,
  /* GT  */ CC_LE,
  /* ULT */ CC_HS | CC_UNSIGNED,
  /* UGE */ CC_LO | CC_UNSIGNED,
  /* ULE */ CC_HI | CC_UNSIGNED,
  /* UGT */ CC_LS | CC_UNSIGNED,
  /* EQ  */ CC_NE,
  /* NE  */ CC_EQ,
  /* ABC */ CC_LS | CC_UNSIGNED
};

static IROp asm_comp_swapop(IROp op)
{
  switch (op) {
  case IR_LT: return IR_GT;
  case IR_GE: return IR_LE;
  case IR_LE: return IR_GE;
  case IR_GT: return IR_LT;
  case IR_ULT: return IR_UGT;
  case IR_UGE: return IR_ULE;
  case IR_ULE: return IR_UGE;
  case IR_UGT: return IR_ULT;
  default: return op;
  }
}

static void asm_intcomp(ASMState *as, IRIns *ir)
{
  IROp op = ir->o;
  IRRef lref = ir->op1, rref = ir->op2;
  Reg left, right, cmp_left, cmp_right;
  int cc;
  int cmp32u;
  lj_assertA(irt_isint(ir->t) || irt_isu32(ir->t) || irt_isaddr(ir->t) ||
	     irt_isu8(ir->t) || irt_isp32(ir->t),
	     "bad comparison data type %d", irt_type(ir->t));
  if (irref_isk(lref) && !irref_isk(rref)) {
    IRRef tmp = lref; lref = rref; rref = tmp;
    op = asm_comp_swapop(op);
  }
  cc = asm_compmap[op];
  left = ra_alloc1_nobase(as, lref, RSET_GPR_NOB, -201);
  asm_guardcc(as, cc & 15);
  if (irref_isk(rref) && !(cc & CC_UNSIGNED) && !irt_isaddr(ir->t) &&
      checki16(IR(rref)->i)) {
    asm_s390x_ir_log_intcomp(as, ir, op, lref, rref, cc, left, RID_NONE, 1);
    emit_u32(as, S390X_INS_RI(S390XI_CGHI, left, IR(rref)->i));
    return;
  }
  if (irref_isk(rref)) {
    right = ra_allock(as, asm_kintptr(as, rref),
		      rset_exclude(RSET_GPR_NOB, left));
  } else {
    right = ra_alloc1_nobase(as, rref, rset_exclude(RSET_GPR_NOB, left), -202);
  }
  cmp32u = (cc & CC_UNSIGNED) &&
	   (irt_isu32(ir->t) || irt_isp32(ir->t) || irt_isu8(ir->t) ||
	    irt_isu16(ir->t));
  cmp_left = left;
  cmp_right = right;
  if (cmp32u) {
    RegSet allow = rset_exclude(RSET_GPR_NOB, left);
    if (!irref_isk(rref))
      allow = rset_exclude(allow, right);
    cmp_left = ra_scratch(as, allow);
    if (!irref_isk(rref)) {
      allow = rset_exclude(allow, cmp_left);
      cmp_right = ra_scratch(as, allow);
    }
  }
  asm_s390x_ir_log_intcomp(as, ir, op, lref, rref, cc, cmp_left, cmp_right, 0);
  emit_u32(as, S390X_INS_RXE((cc & CC_UNSIGNED) ? S390XI_CLGR : S390XI_CGR,
			     cmp_left, cmp_right));
  if (cmp32u) {
    if (!irref_isk(rref))
      emit_u32(as, S390X_INS_RXE(S390XI_LLGFR, cmp_right, right));
    emit_u32(as, S390X_INS_RXE(S390XI_LLGFR, cmp_left, left));
  }
}

static void asm_bnorm32(ASMState *as, IRIns *ir, Reg dest);

static void asm_s390x_fpleft(ASMState *as, IRIns *ir, Reg dest, Reg left)
{
  if (dest != left)
    emit_movrr(as, ir, dest, left);
}

static void asm_add(ASMState *as, IRIns *ir)
{
  if (irt_isnum(ir->t)) {
    Reg dest = ra_dest(as, ir, RSET_FPR);
    Reg left = ra_hintalloc(as, ir->op1, dest, RSET_FPR);
    Reg right = ra_alloc1(as, ir->op2, rset_exclude(RSET_FPR, left));
    if (asm_s390x_ir_log_enabled()) {
      fprintf(stderr,
	      "S390X_IR kind=fpadd curins=%d ir=%d leftref=%d rightref=%d dest=%d left=%d right=%d\n",
	      (int)(as->curins - REF_BIAS), (int)((ir - as->ir) - REF_BIAS),
	      (int)(ir->op1 - REF_BIAS), (int)(ir->op2 - REF_BIAS),
	      (int)dest, (int)left, (int)right);
    }
    if (dest == right && dest != left) {
      Reg tmp = left;
      left = right;
      right = tmp;
    }
    emit_u32(as, S390X_INS_RXE(S390XI_ADBR, dest, right));
    asm_s390x_fpleft(as, ir, dest, left);
    return;
  }

  Reg dest = ra_dest_nobase(as, ir, asm_s390x_dest_gprset(ir->t), -260);
  Reg left, right;
  int bnorm = irt_isinteger(ir->t) || irt_isu32(ir->t);
  if (!irt_isinteger(ir->t) && !irt_is64(ir->t) && !irt_isaddr(ir->t)) {
    asm_s390x_nyi_ir(as, ir);
    return;
  }
  left = ra_hintalloc(as, ir->op1, dest, RSET_GPR_NOB);
  if (irref_isk(ir->op2)) {
    int32_t k = (int32_t)asm_kintptr(as, ir->op2);
    if (!checki16(k)) {
      asm_s390x_nyi_ir(as, ir);
      return;
    }
    asm_s390x_ir_log_addk(as, ir, ir->op1, ir->op2, dest, left, k);
    if (irt_isguard(ir->t) && irt_isinteger(ir->t)) {
      RegSet allow = rset_exclude(RSET_GPR_NOB, dest);
      Reg tmp = ra_scratch(as, allow);
      asm_guardcc(as, CC_NE);
      emit_u32(as, S390X_INS_RXE(S390XI_CGR, dest, tmp));
      emit_u32(as, S390X_INS_RXE(S390XI_LGFR, tmp, dest));
      emit_u32(as, S390X_INS_RI(S390XI_AGHI, dest, k));
      emit_u32(as, S390X_INS_RXE(S390XI_LGFR, dest, dest));
    } else {
      if (irt_isguard(ir->t))
	asm_guardcc(as, CC_OF);
      if (bnorm)
	asm_bnorm32(as, ir, dest);
      emit_u32(as, S390X_INS_RI(S390XI_AGHI, dest, k));
    }
    if (dest != left)
      emit_movrr(as, ir, dest, left);
    return;
  }
  right = ra_alloc1(as, ir->op2, rset_exclude(RSET_GPR_NOB, left));
  if (dest == right && dest != left) {
    Reg tmp = left;
    left = right;
    right = tmp;
  }
  if (irt_isguard(ir->t) && irt_isinteger(ir->t)) {
    RegSet allow = RSET_GPR_NOB & ~RID2RSET(dest) & ~RID2RSET(right);
    Reg tmp = ra_scratch(as, allow);
    asm_guardcc(as, CC_NE);
    emit_u32(as, S390X_INS_RXE(S390XI_CGR, dest, tmp));
    emit_u32(as, S390X_INS_RXE(S390XI_LGFR, tmp, dest));
    emit_u32(as, S390X_INS_RXE(S390XI_AGR, dest, right));
    emit_u32(as, S390X_INS_RXE(S390XI_LGFR, dest, dest));
  } else {
    if (irt_isguard(ir->t))
      asm_guardcc(as, CC_OF);
    if (bnorm)
      asm_bnorm32(as, ir, dest);
    emit_u32(as, S390X_INS_RXE(S390XI_AGR, dest, right));
  }
  if (dest != left)
    emit_movrr(as, ir, dest, left);
}

/* Hiword op of a split 64/64 bit op. Previous op is the loword op. */
static void asm_hiop(ASMState *as, IRIns *ir)
{
  /* HIOP is marked as a store because it needs its own DCE logic. */
  int uselo = ra_used(ir-1), usehi = ra_used(ir);  /* Loword/hiword used? */
  if (LJ_UNLIKELY(!(as->flags & JIT_F_OPT_DCE))) uselo = usehi = 1;
  if (!usehi) return;  /* Skip unused hiword op for all remaining ops. */
  switch ((ir-1)->o) {
  case IR_CALLN:
  case IR_CALLL:
  case IR_CALLS:
  case IR_CALLXS:
    if (!uselo)
      ra_allocref(as, ir->op1, RID2RSET(RID_RETLO));  /* Mark lo op as used. */
    break;
  default:
    lj_assertA(0, "bad HIOP for op %d", (ir-1)->o);
    break;
  }
}
ASM_S390X_STUB_IR(asm_prof)
static void asm_comp(ASMState *as, IRIns *ir)
{
  if (irt_isfp(ir->t))
    asm_s390x_nyi_ir(as, ir);
  else
    asm_intcomp(as, ir);
}

static void asm_retf(ASMState *as, IRIns *ir)
{
  Reg base = ra_alloc1(as, REF_BASE, RSET_GPR_NOB);
  Reg tmp = ra_scratch(as, rset_exclude(RSET_GPR_NOB, base));
  Reg expected = ra_allock(as, (intptr_t)ir_kptr(IR(ir->op2)),
			   rset_exclude(rset_exclude(RSET_GPR_NOB, tmp), base));
  void *pc = ir_kptr(IR(ir->op2));
  int32_t delta = 1+LJ_FR2+bc_a(*((const BCIns *)pc - 1));
  if (getenv("LUAJIT_S390X_RETF_LOG") != NULL) {
    fprintf(stderr,
	    "S390X_RETF trace=%u curins=%d delta=%d pcop=%u base_r=%d base_s=%d topslot=%u\n",
	    (unsigned int)as->T->traceno, (int)(as->curins - REF_BIAS), delta,
	    (unsigned int)bc_op(*((const BCIns *)pc - 1)),
	    (int)IR(REF_BASE)->r, (int)IR(REF_BASE)->s,
	    (unsigned int)as->topslot);
  }
  as->topslot -= (BCReg)delta;
  if ((int32_t)as->topslot < 0) as->topslot = 0;
  irt_setmark(IR(REF_BASE)->t);  /* Children must not coalesce with BASE reg. */
  emit_store64ofs(as, base, RID_SP, ra_spill(as, IR(REF_BASE)));
  emit_setgl(as, base, jit_base);
  emit_addptr(as, base, -8*delta);
  asm_guardcc(as, CC_NE);
  emit_u32(as, S390X_INS_RXE(S390XI_CGR, tmp, expected));
  emit_load64ofs(as, tmp, base, -8);
}

static void asm_equal(ASMState *as, IRIns *ir)
{
  Reg left, right;
  if (irt_isfp(ir->t)) {
    asm_s390x_nyi_ir(as, ir);
    return;
  }
  left = ra_alloc1_nobase(as, ir->op1, RSET_GPR_NOB, -203);
  if (irref_isk(ir->op2))
    right = ra_allock(as, asm_kintptr(as, ir->op2),
		      rset_exclude(RSET_GPR_NOB, left));
  else
    right = ra_alloc1_nobase(as, ir->op2, rset_exclude(RSET_GPR_NOB, left), -204);
  asm_guardcc(as, ir->o == IR_EQ ? CC_NE : CC_EQ);
  emit_u32(as, S390X_INS_RXE(S390XI_CGR, left, right));
}
static void asm_bnorm32(ASMState *as, IRIns *ir, Reg dest)
{
  if (irt_isu32(ir->t))
    emit_u32(as, S390X_INS_RXE(S390XI_LLGFR, dest, dest));
  else
    emit_u32(as, S390X_INS_RXE(S390XI_LGFR, dest, dest));
}

static void asm_bitop_logic(ASMState *as, IRIns *ir, uint32_t op)
{
  Reg left = ra_alloc1_nobase(as, ir->op1, RSET_GPR_NOB, -231);
  Reg right = irref_isk(ir->op2) ?
    ra_allock(as, asm_kintptr(as, ir->op2), rset_exclude(RSET_GPR_NOB, left)) :
    ra_alloc1_nobase(as, ir->op2, rset_exclude(RSET_GPR_NOB, left), -232);
  Reg dest = ra_dest_nobase(as, ir, rset_exclude(RSET_GPR_NOB, right), -230);
  asm_bnorm32(as, ir, dest);
  emit_u32(as, S390X_INS_RXE(op, dest, right));
  if (dest != left)
    emit_movrr(as, ir, dest, left);
}

static void asm_bnot(ASMState *as, IRIns *ir)
{
  Reg left = ra_alloc1_nobase(as, ir->op1, RSET_GPR_NOB, -234);
  Reg right = ra_scratch(as, rset_exclude(RSET_GPR_NOB, left));
  Reg dest = ra_dest_nobase(as, ir, rset_exclude(RSET_GPR_NOB, right), -233);
  asm_bnorm32(as, ir, dest);
  emit_u32(as, S390X_INS_RXE(S390XI_XGR, dest, right));
  if (dest != left)
    emit_movrr(as, ir, dest, left);
  emit_loadu64(as, right, ~(uint64_t)0);
}

static void asm_bswap(ASMState *as, IRIns *ir)
{
  Reg dest = ra_dest_nobase(as, ir, RSET_GPR_NOB, -235);
  Reg left = ra_alloc1_nobase(as, ir->op1, RSET_GPR_NOB, -236);
  if (!irt_is64(ir->t))
    asm_bnorm32(as, ir, dest);
  emit_u32(as, S390X_INS_RXE(irt_is64(ir->t) ? S390XI_LRVGR : S390XI_LRVR,
			     dest, left));
}

static void asm_band(ASMState *as, IRIns *ir)
{
  asm_bitop_logic(as, ir, S390XI_NGR);
}

static void asm_bor(ASMState *as, IRIns *ir)
{
  asm_bitop_logic(as, ir, S390XI_OGR);
}

static void asm_bxor(ASMState *as, IRIns *ir)
{
  asm_bitop_logic(as, ir, S390XI_XGR);
}

static void asm_bitshift(ASMState *as, IRIns *ir, uint64_t op)
{
  if (irref_isk(ir->op2)) {
    Reg dest = ra_dest_nobase(as, ir, RSET_GPR_NOB, -237);
    Reg left = ra_alloc1_nobase(as, ir->op1, RSET_GPR_NOB, -239);
    uint64_t immop;
    int32_t sh = IR(ir->op2)->i & 31;
    if (op == S390XI_SRLK) {
      emit_shiftimm(as, S390XI_SRLG, dest, dest, sh);
      emit_u32(as, S390X_INS_RXE(S390XI_LLGFR, dest, left));
      return;
    }
    immop = (op == S390XI_SLLK) ? S390XI_SLLG : S390XI_SRAG;
    emit_shiftimm(as, immop, dest, left, sh);
    if (dest != left)
      emit_movrr(as, ir, dest, left);
    asm_bnorm32(as, ir, dest);
    return;
  } else {
    Reg left = ra_alloc1_nobase(as, ir->op1, RSET_GPR_NOB, -239);
    Reg right = ra_alloc1_nobase(as, ir->op2, rset_exclude(RSET_GPR_NOB, left), -238);
    Reg dest = ra_dest_nobase(as, ir, rset_exclude(RSET_GPR_NOB, right), -237);
    asm_bnorm32(as, ir, dest);
    emit_u48_pad8(as, S390X_INS_RSYB(op, dest, left, right, 0));
    if (op == S390XI_SRLK) {
      emit_u32(as, S390X_INS_RXE(S390XI_LLGFR, dest, left));
    } else if (dest != left) {
      emit_movrr(as, ir, dest, left);
    }
  }
}

static void asm_bshl(ASMState *as, IRIns *ir)
{
  asm_bitshift(as, ir, S390XI_SLLK);
}

static void asm_bshr(ASMState *as, IRIns *ir)
{
  asm_bitshift(as, ir, S390XI_SRLK);
}

static void asm_bsar(ASMState *as, IRIns *ir)
{
  asm_bitshift(as, ir, S390XI_SRAK);
}

static void asm_brot(ASMState *as, IRIns *ir, int rightrot)
{
  Reg left = ra_alloc1_nobase(as, ir->op1, RSET_GPR_NOB, -242);
  Reg right;
  if (irref_isk(ir->op2)) {
    int32_t rot = IR(ir->op2)->i & 31;
    Reg dest = ra_dest_nobase(as, ir, RSET_GPR_NOB, -240);
    if (rightrot)
      rot = (32 - rot) & 31;
    asm_bnorm32(as, ir, dest);
    emit_u48_pad8(as, S390X_INS_RSYB(S390XI_RLL, dest, left, 0, rot));
    return;
  } else {
    right = ra_alloc1_nobase(as, ir->op2, RSET_GPR_NOB, -241);
    if (rightrot) {
      Reg tmp = ra_releasetmp(as, ASMREF_TMP2);
      right = tmp;
    }
  }
  Reg dest = ra_dest_nobase(as, ir, rset_exclude(RSET_GPR_NOB, right), -240);
  asm_bnorm32(as, ir, dest);
  emit_u48_pad8(as, S390X_INS_RSYB(S390XI_RLL, dest, left, right, 0));
  if (rightrot) {
    Reg orig = IR(ir->op2)->r;
    lj_assertA(ra_hasreg(orig), "right rotate count not in register");
    emit_u32(as, S390X_INS_RXE(S390XI_SGR, right, orig));
    emit_loadi(as, right, 32);
  }
}

static void asm_brol(ASMState *as, IRIns *ir)
{
  asm_brot(as, ir, 0);
}

static void asm_bror(ASMState *as, IRIns *ir)
{
  asm_brot(as, ir, 1);
}
static void asm_sub(ASMState *as, IRIns *ir)
{
  if (irt_isnum(ir->t)) {
    Reg dest = ra_dest(as, ir, RSET_FPR);
    Reg lr = ra_alloc2(as, ir, RSET_FPR);
    Reg left = lr & 255;
    Reg right = lr >> 8;
    emit_u32(as, S390X_INS_RXE(S390XI_SDBR, dest, right));
    asm_s390x_fpleft(as, ir, dest, left);
    return;
  }

  Reg dest, left, right;
  int bnorm = irt_isinteger(ir->t) || irt_isu32(ir->t);

  if (!irt_isinteger(ir->t) && !irt_is64(ir->t) && !irt_isaddr(ir->t)) {
    asm_s390x_nyi_ir(as, ir);
    return;
  }

  if (irref_isk(ir->op2)) {
    int32_t k = (int32_t)asm_kintptr(as, ir->op2);
    if (k != INT32_MIN && checki16(-k)) {
  dest = ra_dest_nobase(as, ir, asm_s390x_dest_gprset(ir->t), -261);
      left = ra_hintalloc(as, ir->op1, dest, RSET_GPR_NOB);
      if (irt_isguard(ir->t) && irt_isinteger(ir->t)) {
	RegSet allow = rset_exclude(RSET_GPR_NOB, dest);
	Reg tmp = ra_scratch(as, allow);
	asm_guardcc(as, CC_NE);
	emit_u32(as, S390X_INS_RXE(S390XI_CGR, dest, tmp));
	emit_u32(as, S390X_INS_RXE(S390XI_LGFR, tmp, dest));
	emit_u32(as, S390X_INS_RI(S390XI_AGHI, dest, -k));
	emit_u32(as, S390X_INS_RXE(S390XI_LGFR, dest, dest));
      } else {
	if (irt_isguard(ir->t))
	  asm_guardcc(as, CC_OF);
	if (bnorm)
	  asm_bnorm32(as, ir, dest);
	emit_u32(as, S390X_INS_RI(S390XI_AGHI, dest, -k));
      }
      if (dest != left)
        emit_movrr(as, ir, dest, left);
      return;
    }
  }

  right = irref_isk(ir->op2) ? ra_allock(as, IR(ir->op2)->i, RSET_GPR_NOB) :
			       ra_alloc1(as, ir->op2, RSET_GPR_NOB);
  dest = ra_dest_nobase(as, ir, rset_exclude(RSET_GPR_NOB, right), -205);
  left = ra_hintalloc_nobase(as, ir->op1, dest,
			     rset_exclude(RSET_GPR_NOB, right), -206);
  asm_s390x_ir_log_sub(as, ir, ir->op1, ir->op2, dest, left, right);
  if (irt_isguard(ir->t) && irt_isinteger(ir->t)) {
    RegSet allow = RSET_GPR_NOB & ~RID2RSET(dest) & ~RID2RSET(right);
    Reg tmp = ra_scratch(as, allow);
    asm_guardcc(as, CC_NE);
    emit_u32(as, S390X_INS_RXE(S390XI_CGR, dest, tmp));
    emit_u32(as, S390X_INS_RXE(S390XI_LGFR, tmp, dest));
    emit_u32(as, S390X_INS_RXE(S390XI_SGR, dest, right));
    emit_u32(as, S390X_INS_RXE(S390XI_LGFR, dest, dest));
  } else {
    if (irt_isguard(ir->t))
      asm_guardcc(as, CC_OF);
    if (bnorm)
      asm_bnorm32(as, ir, dest);
    emit_u32(as, S390X_INS_RXE(S390XI_SGR, dest, right));
  }
  if (dest != left)
    emit_movrr(as, ir, dest, left);
}
static void asm_mul(ASMState *as, IRIns *ir)
{
  Reg dest = ra_dest_nobase(as, ir, RSET_GPR_NOB, -207);
  Reg left, right;

  if (!irt_isinteger(ir->t)) {
    asm_s390x_nyi_ir(as, ir);
    return;
  }

  left = ra_hintalloc_nobase(as, ir->op1, dest, RSET_GPR_NOB, -208);
  if (irref_isk(ir->op2))
    right = ra_allock(as, IR(ir->op2)->i, rset_exclude(RSET_GPR_NOB, left));
  else
    right = ra_alloc1_nobase(as, ir->op2, rset_exclude(RSET_GPR_NOB, left), -209);

  if (dest == right && dest != left) {
    Reg tmp = left;
    left = right;
    right = tmp;
  }

  if (irt_isguard(ir->t)) {
    RegSet allow = RSET_GPR_NOB & ~RID2RSET(dest) & ~RID2RSET(right);
    Reg tmp = ra_scratch(as, allow);
    asm_guardcc(as, CC_NE);
    emit_u32(as, S390X_INS_RXE(S390XI_CGR, dest, tmp));
    emit_u32(as, S390X_INS_RXE(S390XI_LGFR, tmp, dest));
    emit_u32(as, S390X_INS_RXE(S390XI_MSGFR, dest, right));
    emit_u32(as, S390X_INS_RXE(S390XI_LGFR, dest, dest));
  } else {
    emit_u32(as, S390X_INS_RXE(S390XI_LGFR, dest, dest));
    emit_u32(as, S390X_INS_RXE(S390XI_MSGFR, dest, right));
    emit_u32(as, S390X_INS_RXE(S390XI_LGFR, dest, dest));
  }

  if (dest != left)
    emit_movrr(as, ir, dest, left);
}
static void asm_neg(ASMState *as, IRIns *ir)
{
  Reg dest, left;

  if (irt_isnum(ir->t)) {
    Reg zero = ra_scratch(as, RSET_FPR);
    dest = ra_dest(as, ir, rset_exclude(RSET_FPR, zero));
    left = ra_alloc1(as, ir->op1, rset_exclude(RSET_FPR, zero));
    emit_u32(as, S390X_INS_RXE(S390XI_SDBR, dest, left));
    if (dest != zero)
      emit_movrr(as, ir, dest, zero);
    emit_u32(as, S390X_INS_RXE(S390XI_SDBR, zero, zero));
    return;
  }

  if (!irt_isinteger(ir->t) && !irt_is64(ir->t) && !irt_isaddr(ir->t)) {
    asm_s390x_nyi_ir(as, ir);
    return;
  }

  dest = ra_dest_nobase(as, ir, RSET_GPR_NOB, -243);
  left = ra_alloc1_nobase(as, ir->op1, rset_exclude(RSET_GPR_NOB, dest), -244);

  if (!irt_is64(ir->t))
    asm_bnorm32(as, ir, dest);
  if (irt_isguard(ir->t))
    asm_guardcc(as, CC_OF);
  emit_u32(as, S390X_INS_RXE(S390XI_SGR, dest, left));
  emit_u32(as, S390X_INS_RXE(S390XI_XGR, dest, dest));
}
ASM_S390X_STUB_IR(asm_abs)
ASM_S390X_STUB_IR(asm_fpdiv)
ASM_S390X_STUB_IR(asm_fpmath)
ASM_S390X_STUB_IR(asm_tobit)
ASM_S390X_STUB_IR(asm_min)
ASM_S390X_STUB_IR(asm_max)
#define asm_addov(as, ir)	asm_add(as, ir)
#define asm_subov(as, ir)	asm_sub(as, ir)
#define asm_mulov(as, ir)	asm_mul(as, ir)

static void asm_aref(ASMState *as, IRIns *ir)
{
  Reg base, dest;
  if (irref_isk(ir->op2)) {
    int32_t ofs = 8 * IR(ir->op2)->i;
    if (!checki16(ofs)) {
      asm_s390x_nyi_ir(as, ir);
      return;
    }
    base = ra_alloc1_nobase(as, ir->op1, RSET_GPR_NOB, -210);
    dest = ra_dest_nobase(as, ir, rset_exclude(RSET_GPR_NOB, base), -211);
    asm_s390x_ir_log_aref(as, ir, ir->op1, ir->op2, base, RID_NONE, dest, ofs);
    emit_u32(as, S390X_INS_RI(S390XI_AGHI, dest, ofs));
    if (dest != base)
      emit_movrr(as, ir, dest, base);
    return;
  }

  base = ra_alloc1_nobase(as, ir->op1, RSET_GPR_NOB, -212);
  {
    RegSet allow = rset_exclude(RSET_GPR_NOB, base);
    Reg idx = ra_alloc1_nobase(as, ir->op2, allow, -214);
    dest = ra_dest_nobase(as, ir, rset_exclude(allow, idx), -213);
    asm_s390x_ir_log_aref(as, ir, ir->op1, ir->op2, base, idx, dest, 0);
    emit_u32(as, S390X_INS_RXE(S390XI_AGR, dest, base));
    emit_shiftimm(as, S390XI_SLLG, dest, idx, 3);
    if (dest != idx)
      emit_movrr(as, ir, dest, idx);
  }
}

typedef struct S390XFusedRef {
  Reg reg;
  Reg base;
  Reg idx;
  int32_t ofs;
} S390XFusedRef;

/* Fuse array/hash/upvalue reference into register+offset operand. */
static S390XFusedRef asm_fuseahuref(ASMState *as, IRRef ref, RegSet allow)
{
  IRIns *ir = IR(ref);
  S390XFusedRef fr;
  fr.reg = RID_NONE;
  fr.base = RID_NONE;
  fr.idx = RID_NONE;
  fr.ofs = 0;
  if (ra_noreg(ir->r)) {
    if (ir->o == IR_AREF) {
      if (mayfuse(as, ref)) {
	if (irref_isk(ir->op2)) {
	  int32_t ofs = 8 * IR(ir->op2)->i;
	  if (checki20(ofs)) {
	    fr.reg = ra_alloc1_nobase(as, ir->op1, allow, -251);
	    fr.ofs = ofs;
	    return fr;
	  }
	} else {
	  fr.base = ra_alloc1_nobase(as, ir->op1, allow, -252);
	  allow = rset_exclude(allow, fr.base);
	  fr.idx = ra_alloc1_nobase(as, ir->op2, allow, -253);
	  fr.reg = ra_scratch(as, rset_exclude(allow, fr.idx));
	  return fr;
	}
      }
    } else if (ir->o == IR_HREFK) {
      if (mayfuse(as, ref)) {
	int32_t ofs = (int32_t)(IR(ir->op2)->op2 * sizeof(Node));
	if (checki20(ofs)) {
	  fr.reg = ra_alloc1_nobase(as, ir->op1, allow, -254);
	  fr.ofs = ofs;
	  return fr;
	}
      }
    } else if (ir->o == IR_UREFC) {
      if (irref_isk(ir->op1)) {
	GCfunc *fn = ir_kfunc(IR(ir->op1));
	GCupval *uv = &gcref(fn->l.uvptr[(ir->op2 >> 8)])->uv;
	intptr_t ofs = (intptr_t)&uv->tv - (intptr_t)J2G(as->J);
	if (checki20(ofs)) {
	  fr.reg = RID_DISPATCH;
	  fr.ofs = (int32_t)ofs;
	  return fr;
	}
      }
    } else if (ir->o == IR_TMPREF) {
      fr.reg = RID_DISPATCH;
      fr.ofs = emit_gl_ofs(tmptv);
      return fr;
    }
  }
  fr.reg = ra_alloc1_nobase(as, ref, allow, -255);
  return fr;
}

static void asm_emitfuseahuref(ASMState *as, IRIns *ir,
			       const S390XFusedRef *fr)
{
  if (fr->idx == RID_NONE)
    return;
  emit_u32(as, S390X_INS_RXE(S390XI_AGR, fr->reg, fr->base));
  emit_shiftimm(as, S390XI_SLLG, fr->reg, fr->idx, 3);
  if (fr->reg != fr->idx)
    emit_movrr(as, ir, fr->reg, fr->idx);
}

static int32_t asm_s390x_vload_intofs(ASMState *as, IRIns *ir, int32_t ofs)
{
  if (asm_s390x_is_varg_vload(as, ir)) {
    int bias = asm_s390x_varg_bias_override();
    int slotbias = asm_s390x_varg_slot_bias_override();
    int rootbias = asm_s390x_varg_slot_bias_root_override();
    int loopbias = asm_s390x_varg_slot_bias_loop_override();
    if (asm_s390x_is_loop_varg_vload(as, ir)) {
      if (loopbias != -999)
	slotbias += loopbias;
    } else if (rootbias != -999) {
      slotbias += rootbias;
    }
    return ofs + slotbias +
	   (bias != -999 ? bias : (LJ_BE ? 4 : 0));
  }
  return ofs + (LJ_BE ? 4 : 0);
}

static void asm_ahuvload(ASMState *as, IRIns *ir)
{
  int32_t ofs = 0;
  IRType1 t = ir->t;
  Reg dest = RID_NONE;
  S390XFusedRef fr;
  RegSet allow = RSET_GPR_NOB;

  lj_assertA(!(ir->o == IR_VLOAD && 8 * ir->op2 < 0), "bad VLOAD offset");

  if (ra_used(ir)) {
    if (!(irt_isint(t) || irt_isu32(t) || irt_isaddr(t) || irt_ispri(t))) {
      asm_s390x_nyi_ir(as, ir);
      return;
    }
    dest = ra_dest_nobase(as, ir, allow, -215);
    fr = asm_fuseahuref(as, ir->op1, rset_clear(allow, dest));
    ofs = fr.ofs;
    if (ir->o == IR_VLOAD)
      ofs += 8 * ir->op2;
    asm_s390x_ir_log_vload(as, ir, ir->op1, fr.reg, fr.base, fr.idx, dest, ofs);
    if (irt_isaddr(t)) {
      emit_shiftimm(as, S390XI_SRLG, dest, dest, 17);
      emit_shiftimm(as, S390XI_SLLG, dest, dest, 17);
    } else if (irt_isint(t)) {
      emit_u32(as, S390X_INS_RXE(S390XI_LGFR, dest, dest));
    } else if (irt_isu32(t)) {
      emit_u32(as, S390X_INS_RXE(S390XI_LLGFR, dest, dest));
    }
    goto dotypecheck;
  }
  fr = asm_fuseahuref(as, ir->op1, allow);
  ofs = fr.ofs;
  if (ir->o == IR_VLOAD)
    ofs += 8 * ir->op2;
  asm_s390x_ir_log_vload(as, ir, ir->op1, fr.reg, fr.base, fr.idx, dest, ofs);

dotypecheck:
  rset_clear(allow, fr.reg);
  if (!irt_isint(t) && !irt_isu32(t) && !irt_isaddr(t) && !irt_ispri(t)) {
    asm_s390x_nyi_ir(as, ir);
    return;
  }
  if (irt_isaddr(t)) {
    Reg tmp = ra_scratch(as, allow);
    asm_guardcc(as, CC_NE);
    emit_u32(as, S390X_INS_RI(S390XI_CGHI, tmp, (int32_t)irt_toitype(t)));
    emit_shiftimm(as, S390XI_SRAG, tmp, tmp, 47);
    emit_load64ofs(as, tmp, fr.reg, ofs);
  } else if (irt_ispri(t)) {
    Reg tmp = ra_scratch(as, allow);
    Reg expected = ra_scratch(as, rset_exclude(allow, tmp));
    asm_guardcc(as, CC_NE);
    emit_u32(as, S390X_INS_RXE(S390XI_CGR, tmp, expected));
    emit_loadu64(as, expected, irt_isnil(t) ? ~(uint64_t)0 :
      (uint64_t)(~((int64_t)~irt_toitype(t) << 47)));
    emit_load64ofs(as, tmp, fr.reg, ofs);
  }
  if (ra_hasreg(dest)) {
	  if (irt_isaddr(t) || irt_ispri(t)) {
	    emit_load64ofs(as, dest, fr.reg, ofs);
	  } else if (irt_isint(t) || irt_isu32(t)) {
	    if (asm_s390x_varg_dump_enabled() && asm_s390x_is_varg_vload(as, ir)) {
	      CCallInfo ci;
	      ci.func = (ASMFunction)lj_trace_s390x_varg_probe;
	      ci.flags = CCI_NOFPRCLOBBER;
	      asm_setupresult(as, ir, &ci);
	      emit_call(as, RID_R14, (void *)ci.func);
	      emit_loadi(as, REGARG_FIRSTGPR+1, ofs);
	      if (REGARG_FIRSTGPR != fr.reg)
		emit_movrr(as, ir, REGARG_FIRSTGPR, fr.reg);
	    } else {
	      emit_loadu32ofs(as, dest, fr.reg, asm_s390x_vload_intofs(as, ir, ofs));
	    }
	  }
	}
	asm_emitfuseahuref(as, ir, &fr);
}

static void asm_href(ASMState *as, IRIns *ir, IROp merge)
{
  const CCallInfo *ci = &lj_ir_callinfo[IRCALL_lj_tab_get];
  IRRef args[3];
  IRIns *gir = NULL;
  Reg expected = RID_NONE;
  RegSet allow;
  args[0] = ASMREF_L;     /* lua_State * */
  args[1] = ir->op1;      /* GCtab * */
  args[2] = ASMREF_TMP1;  /* cTValue *key */
  if (merge == IR_EQ || merge == IR_NE) {
    gir = IR(as->curins+1);
    lj_assertA(gir->o == merge && gir->op1 == as->curins,
	       "bad fused HREF compare %d", gir->o);
    allow = rset_exclude(RSET_GPR_NOB, RID_RET);
    asm_guardcc(as, merge == IR_EQ ? CC_NE : CC_EQ);
    if (irref_isk(gir->op2)) {
      expected = ra_scratch(as, allow);
      emit_u32(as, S390X_INS_RXE(S390XI_CGR, RID_RET, expected));
      emit_loadu64(as, expected, (uint64_t)(uintptr_t)asm_kintptr(as, gir->op2));
    } else {
      expected = ra_alloc1(as, gir->op2, allow);
      emit_u32(as, S390X_INS_RXE(S390XI_CGR, RID_RET, expected));
    }
  } else if (merge != 0) {
    asm_s390x_nyi_ir(as, ir);
  }
  asm_setupresult(as, ir, ci);  /* cTValue * */
  asm_gencall(as, ci, args);
  asm_tvptr(as, ra_releasetmp(as, ASMREF_TMP1), ir->op2, IRTMPREF_IN1);
}

static void asm_hrefk(ASMState *as, IRIns *ir)
{
  IRIns *kslot = IR(ir->op2);
  IRIns *irkey = IR(kslot->op1);
  int32_t ofs = (int32_t)(kslot->op2 * sizeof(Node));
  int32_t kofs = ofs + (int32_t)offsetof(Node, key);
  int bigofs = !checki20(kofs);
  Reg node = ra_alloc1_nobase(as, ir->op1, RSET_GPR_NOB, -218);
  Reg dest = (ra_used(ir) || bigofs) ?
	     ra_dest_nobase(as, ir, rset_exclude(RSET_GPR_NOB, node), -219) : RID_NONE;
  Reg idx = node;
  RegSet allow = rset_exclude(RSET_GPR_NOB, node);
  Reg key, expected;
  uint64_t k;

  lj_assertA(ofs % sizeof(Node) == 0, "unaligned HREFK slot");
  if (ra_hasreg(dest))
    rset_clear(allow, dest);

  if (bigofs) {
    idx = dest;
    kofs = (int32_t)offsetof(Node, key);
  } else if (ra_hasreg(dest)) {
    emit_addptr(as, dest, ofs);
    if (dest != node)
      emit_movrr(as, ir, dest, node);
  }

  key = ra_scratch(as, allow);
  allow = rset_exclude(allow, key);
  expected = ra_scratch(as, allow);

  asm_guardcc(as, CC_NE);
  if (irt_ispri(irkey->t)) {
    lj_assertA(!irt_isnil(irkey->t), "bad HREFK key type");
    k = (uint64_t)(~((int64_t)~irt_toitype(irkey->t) << 47));
  } else if (irt_isnum(irkey->t)) {
    k = ir_knum(irkey)->u64;  /* -0.0 is canonicalized to +0.0 by recorder. */
  } else {
    lj_assertA(irt_isgcv(irkey->t), "bad HREFK key type");
    k = ((uint64_t)irt_toitype(irkey->t) << 47) |
	(uint64_t)(uintptr_t)ir_kgc(irkey);
  }
  emit_u32(as, S390X_INS_RXE(S390XI_CGR, key, expected));
  emit_loadu64(as, expected, k);
  emit_load64ofs(as, key, idx, kofs);

  if (bigofs) {
    emit_addptr(as, dest, ofs);
    if (dest != node)
      emit_movrr(as, ir, dest, node);
  }
}
static void asm_uref(ASMState *as, IRIns *ir)
{
  Reg dest = ra_dest_nobase(as, ir, RSET_GPR_NOB, -245);
  int32_t uvofs = (int32_t)offsetof(GCfuncL, uvptr) +
		  (int32_t)sizeof(GCRef) * (int32_t)(ir->op2 >> 8);
  int guarded = (irt_t(ir->t) & (IRT_GUARD|IRT_TYPE)) == (IRT_GUARD|IRT_PGC);

  if (irref_isk(ir->op1) && !guarded) {
    GCfunc *fn = ir_kfunc(IR(ir->op1));
    MRef *v = &gcref(fn->l.uvptr[(ir->op2 >> 8)])->uv.v;
    emit_loadu64(as, dest, (uintptr_t)v);
    emit_load64ofs(as, dest, dest, 0);
    return;
  }

  if (ir->o == IR_UREFC) {
    emit_u32(as, S390X_INS_RI(S390XI_AGHI, dest, (int32_t)offsetof(GCupval, tv)));
  } else {
    emit_load64ofs(as, dest, dest, (int32_t)offsetof(GCupval, v));
  }

  if (guarded) {
    Reg tmp = ra_releasetmp(as, ASMREF_TMP1);
    asm_guardcc(as, ir->o == IR_UREFC ? CC_EQ : CC_NE);
    emit_u32(as, S390X_INS_RI(S390XI_CGHI, tmp, 0));
    emit_u48_pad8(as, S390X_INS_RXY(S390XI_LLGC, tmp, 0, dest,
				    (int32_t)offsetof(GCupval, closed)));
  }

  if (irref_isk(ir->op1)) {
    GCfunc *fn = ir_kfunc(IR(ir->op1));
    emit_loadu64(as, dest, gcrefu(fn->l.uvptr[(ir->op2 >> 8)]));
  } else {
    Reg fn = ra_alloc1_nobase(as, ir->op1, rset_exclude(RSET_GPR_NOB, dest), -246);
    emit_load64ofs(as, dest, fn, uvofs);
  }
}
ASM_S390X_STUB_IR(asm_fref)
ASM_S390X_STUB_IR(asm_strref)

static IRRef asm_fusexref_kbase(ASMState *as, IRRef ref, int32_t *ofsp)
{
  IRIns *ir = IR(ref);
  IRRef base = ref;
  int32_t ofs = 0;

  if (ir->o == IR_ADD) {
    if (irref_isk(ir->op2)) {
      intptr_t k = asm_kintptr(as, ir->op2);
      if (checki20(k)) {
	ofs = (int32_t)k;
	base = ir->op1;
      }
    } else if (irref_isk(ir->op1)) {
      intptr_t k = asm_kintptr(as, ir->op1);
      if (checki20(k)) {
	ofs = (int32_t)k;
	base = ir->op2;
      }
    }
    if (base != ref && !checki20(ofs))
      base = ref;
  }

  if (base == ref)
    ofs = 0;
  *ofsp = ofs;
  UNUSED(as);
  return base;
}

static void asm_xload(ASMState *as, IRIns *ir)
{
  RegSet allow;
  Reg dest, base;
  int32_t ofs = 0;
  IRRef xref = asm_fusexref_kbase(as, ir->op1, &ofs);

  lj_assertA(LJ_TARGET_UNALIGNED || !(ir->op2 & IRXLOAD_UNALIGNED),
	     "unaligned XLOAD");
  if (!(irt_isint(ir->t) || irt_isu32(ir->t) || irt_isaddr(ir->t) ||
	irt_is64(ir->t) || irt_isgcv(ir->t) || irt_isu8(ir->t) ||
	irt_isu16(ir->t) || irt_isi8(ir->t) || irt_isi16(ir->t))) {
    asm_s390x_nyi_ir(as, ir);
    return;
  }

  allow = irt_isfp(ir->t) ? RSET_FPR : RSET_GPR_NOB;
  dest = ra_dest_nobase(as, ir, allow, -247);
  if (irt_isfp(ir->t))
    base = ra_alloc1(as, xref, RSET_GPR);
  else
    base = ra_alloc1_nobase(as, xref,
			    rset_exclude(RSET_GPR_NOB, dest), -248);
  if (irt_isu8(ir->t))
    emit_loadu8ofs(as, dest, base, ofs);
  else if (irt_isu16(ir->t))
    emit_loadu16ofs(as, dest, base, ofs);
  else if (irt_isi8(ir->t))
    emit_loadi8ofs(as, dest, base, ofs);
  else if (irt_isi16(ir->t))
    emit_loadi16ofs(as, dest, base, ofs);
  else
    emit_loadofs(as, ir, dest, base, ofs);
}

static void asm_fload(ASMState *as, IRIns *ir)
{
  int32_t ofs;
  IRType1 t = ir->t;
  Reg dest, base;

  if (ir->op1 == REF_NIL) {
    asm_s390x_nyi_ir(as, ir);
    return;
  }
  if (!(irt_isint(t) || irt_isu32(t) || irt_isaddr(t) ||
	irt_isu8(t) || irt_isu16(t) || irt_isi8(t) || irt_isi16(t) ||
	irt_isgcv(t))) {
    asm_s390x_nyi_ir(as, ir);
    return;
  }

  dest = ra_dest_nobase(as, ir, RSET_GPR_NOB, -220);
  base = ra_alloc1_nobase(as, ir->op1, rset_exclude(RSET_GPR_NOB, dest), -221);
  ofs = field_ofs[ir->op2];

  if (irt_isaddr(t) || irt_isgcv(t)) {
    emit_load64ofs(as, dest, base, ofs);
  } else if (irt_isu8(t)) {
    emit_shiftimm(as, S390XI_SRLG, dest, dest, 56);
    emit_load64ofs(as, dest, base, ofs);
  } else if (irt_isu16(t)) {
    emit_shiftimm(as, S390XI_SRLG, dest, dest, 48);
    emit_load64ofs(as, dest, base, ofs);
  } else if (irt_isi8(t)) {
    emit_shiftimm(as, S390XI_SRAG, dest, dest, 56);
    emit_shiftimm(as, S390XI_SLLG, dest, dest, 56);
    emit_load64ofs(as, dest, base, ofs);
  } else if (irt_isi16(t)) {
    emit_shiftimm(as, S390XI_SRAG, dest, dest, 48);
    emit_shiftimm(as, S390XI_SLLG, dest, dest, 48);
    emit_load64ofs(as, dest, base, ofs);
  } else {
    if (irt_isint(t))
      emit_u32(as, S390X_INS_RXE(S390XI_LGFR, dest, dest));
    emit_loadu32ofs(as, dest, base, ofs);
  }
}

static void asm_sload(ASMState *as, IRIns *ir)
{
  int32_t ofs = 8 * ((int32_t)ir->op1 - 2);
  IRType1 t = ir->t;
  int32_t vofs = ofs + ((LJ_BE && !irt_isaddr(t)) ? 4 : 0);
  Reg dest = RID_NONE, base;
  RegSet allow = RSET_GPR_NOB;

  lj_assertA(!(ir->op2 & IRSLOAD_PARENT), "bad parent SLOAD");
  lj_assertA(irt_isguard(t) || !(ir->op2 & IRSLOAD_TYPECHECK),
	     "inconsistent SLOAD variant");

  if (ir->op2 & IRSLOAD_CONVERT) {
    asm_s390x_nyi_tag(as, -117);
    return;
  }
  if (ra_used(ir)) {
    if (!(irt_isint(t) || irt_isu32(t) || irt_isaddr(t) || irt_isnum(t))) {
      asm_s390x_nyi_tag(as, -118);
      return;
    }
    if (irt_isnum(t)) {
      dest = ra_dest(as, ir, RSET_FPR);
      base = ra_allocbase(as, allow);
    } else {
      dest = ra_dest_nobase(as, ir, allow, -222);
      base = ra_allocbase(as, rset_clear(allow, dest));
    }
    if (irt_isaddr(t)) {
      emit_shiftimm(as, S390XI_SRLG, dest, dest, 17);
      emit_shiftimm(as, S390XI_SLLG, dest, dest, 17);
    } else if (irt_isint(t) && !(ir->op2 & IRSLOAD_FRAME)) {
      emit_u32(as, S390X_INS_RXE(S390XI_LGFR, dest, dest));
    }
    goto dotypecheck;
  }
  base = ra_allocbase(as, allow);

dotypecheck:
  if (asm_s390x_sload_log_enabled()) {
    fprintf(stderr,
	    "S390X_SLOAD curins=%d ref=%d op1=%d ofs=%d vofs=%d type=%d op2=0x%x used=%d dest=%d base=%d\n",
	    (int)(as->curins - REF_BIAS), (int)((ir - as->ir) - REF_BIAS),
	    (int)ir->op1, (int)ofs, (int)vofs, (int)irt_type(t),
	    (unsigned int)ir->op2, (int)ra_used(ir), (int)dest, (int)base);
  }
  rset_clear(allow, base);
  if (ir->op2 & IRSLOAD_TYPECHECK) {
    RegSet tallow = allow;
    if (ra_hasreg(dest))
      rset_clear(tallow, dest);
    Reg tmp = ra_scratch(as, tallow);
    asm_guardcc(as, CC_NE);
    emit_u32(as, S390X_INS_RI(S390XI_CGHI, tmp, (int32_t)irt_toitype(t)));
    emit_shiftimm(as, S390XI_SRAG, tmp, tmp, 47);
    emit_load64ofs(as, tmp, base, ofs);
  }
  if (ra_hasreg(dest)) {
    if (irt_isnum(t)) {
      emit_loadofs(as, ir, dest, base, ofs);
    } else if (irt_isaddr(t)) {
      emit_load64ofs(as, dest, base, ofs);
    } else {
      emit_loadu32ofs(as, dest, base, vofs);
    }
  }
  if (base == RID_BASE)
    emit_getgl(as, base, jit_base);
}

static void asm_ahustore(ASMState *as, IRIns *ir)
{
  S390XFusedRef fr;

  if (ir->r == RID_SINK)
    return;
  if (irt_isnum(ir->t)) {
    asm_s390x_nyi_ir(as, ir);
    return;
  }
  fr = asm_fuseahuref(as, ir->op1, RSET_GPR_NOB);
  asm_tvstore64(as, fr.reg, fr.ofs, ir->op2);
  asm_emitfuseahuref(as, ir, &fr);
}

static void asm_fstore(ASMState *as, IRIns *ir)
{
  IRIns *irf;
  Reg base, src;
  int32_t ofs;
  RegSet allow = RSET_GPR;

  if (ir->r == RID_SINK)
    return;

  irf = IR(ir->op1);
  lj_assertA(irf->o == IR_FREF, "bad FSTORE reference op %d", irf->o);
  base = ra_alloc1(as, irf->op1, allow);
  allow = rset_exclude(allow, base);
  src = ra_alloc1(as, ir->op2, allow);
  ofs = field_ofs[irf->op2];

  if (irt_isi8(ir->t) || irt_isu8(ir->t)) {
    emit_store8ofs(as, src, base, ofs);
  } else if (irt_isi16(ir->t) || irt_isu16(ir->t)) {
    emit_store16ofs(as, src, base, ofs);
  } else if (irt_isint(ir->t) || irt_isu32(ir->t)) {
    emit_store32ofs(as, src, base, ofs);
  } else if (irt_isaddr(ir->t) || irt_is64(ir->t) || irt_isgcv(ir->t)) {
    emit_store64ofs(as, src, base, ofs);
  } else {
    asm_s390x_nyi_ir(as, ir);
  }
}

static void asm_tbar(ASMState *as, IRIns *ir)
{
  Reg tab = ra_alloc1(as, ir->op1, RSET_GPR);
  Reg link = ra_scratch(as, rset_exclude(RSET_GPR, tab));
  MCode *l_end = as->mcp;
  int32_t marked_ofs = (int32_t)offsetof(GCtab, marked);

  lj_assertA(marked_ofs >= 0 && marked_ofs < 4096, "GCtab.marked offset out of range");
  emit_store64ofs(as, link, tab, (int32_t)offsetof(GCtab, gclist));
  emit_setgl(as, tab, gc.grayagain);
  emit_u32(as, S390X_INS_SI(S390XI_NI, tab, marked_ofs, (uint8_t)~LJ_GC_BLACK));
  emit_getgl(as, link, gc.grayagain);
  emit_condbranch(as, CC_EQ, l_end);
  emit_u32(as, S390X_INS_SI(S390XI_TM, tab, marked_ofs, LJ_GC_BLACK));
}

static void asm_xstore(ASMState *as, IRIns *ir)
{
  RegSet allow;
  Reg src, base;
  int32_t ofs = 0;
  IRRef xref = asm_fusexref_kbase(as, ir->op1, &ofs);

  if (ir->r == RID_SINK)
    return;
  if (!(irt_isint(ir->t) || irt_isu32(ir->t) || irt_isaddr(ir->t) ||
	irt_is64(ir->t) || irt_isgcv(ir->t))) {
    asm_s390x_nyi_ir(as, ir);
    return;
  }

  allow = irt_isfp(ir->t) ? RSET_FPR : RSET_GPR_NOB;
  src = irt_isfp(ir->t) ? ra_alloc1(as, ir->op2, allow) :
			  ra_alloc1_nobase(as, ir->op2, allow, -249);
  if (irt_isfp(ir->t))
    base = ra_alloc1(as, xref, RSET_GPR);
  else
    base = ra_alloc1_nobase(as, xref,
			    rset_exclude(RSET_GPR_NOB, src), -250);
  asm_s390x_ir_log_xstore(as, ir, xref, ir->op2, ofs, base, src);
  emit_storeofs(as, ir, src, base, ofs);
}

static void asm_tointg(ASMState *as, IRIns *ir, Reg left)
{
  Reg tmp = ra_scratch(as, rset_exclude(RSET_FPR, left));
  Reg dest = ra_dest_nobase(as, ir, RSET_GPR_NOB, -273);
  asm_guardcc(as, CC_NE);
  emit_u32(as, S390X_INS_RXE(S390XI_CDBR, tmp, left));
  emit_u32(as, S390X_INS_RXE(S390XI_CDFBR, tmp, dest));
  emit_u32(as, S390X_INS_RXE(S390XI_LGFR, dest, dest));
  emit_u32(as, S390X_INS_RRF_M(S390XI_CFDBR, dest, 5, left));
}
#if LJ_HASFFI
static void asm_cnew(ASMState *as, IRIns *ir)
{
  CTState *cts = ctype_ctsG(J2G(as->J));
  CTypeID id = (CTypeID)IR(ir->op1)->i;
  CTSize sz;
  CTInfo info = lj_ctype_info(cts, id, &sz);
  const CCallInfo *ci = &lj_ir_callinfo[IRCALL_lj_mem_newgco];
  IRRef args[4];

  lj_assertA(sz != CTSIZE_INVALID || (ir->o == IR_CNEW && ir->op2 != REF_NIL),
	     "bad CNEW/CNEWI operands");

  as->gcsteps++;
  asm_setupresult(as, ir, ci);  /* GCcdata * */

  if (ir->o == IR_CNEWI) {
    RegSet allow = rset_exclude(
	rset_exclude(rset_exclude(RSET_GPR, RID_RET), RID_TMP), RID_R1);
    Reg src = irref_isk(ir->op2) ? ra_allock(as, ir->op2, allow) :
				   ra_alloc1(as, ir->op2, allow);
    lj_assertA(sz == 4 || sz == 8, "bad CNEWI size %d", sz);
    if (sz == 8)
      emit_store64ofs(as, src, RID_RET, sizeof(GCcdata));
    else
      emit_store32ofs(as, src, RID_RET, sizeof(GCcdata));
  } else if (ir->op2 != REF_NIL) {  /* Create VLA/VLS/aligned cdata. */
    ci = &lj_ir_callinfo[IRCALL_lj_cdata_newv];
    args[0] = ASMREF_L;     /* lua_State *L */
    args[1] = ir->op1;      /* CTypeID id   */
    args[2] = ir->op2;      /* CTSize sz    */
    args[3] = ASMREF_TMP1;  /* CTSize align */
    asm_gencall(as, ci, args);
    emit_loadi(as, ra_releasetmp(as, ASMREF_TMP1), (int32_t)ctype_align(info));
    return;
  }

  emit_store8ofs(as, RID_TMP, RID_RET, offsetof(GCcdata, gct));
  emit_loadi(as, RID_TMP, (int32_t)~LJ_TCDATA);
  emit_store16ofs(as, RID_R1, RID_RET, offsetof(GCcdata, ctypeid));
  emit_loadi(as, RID_R1, (int32_t)id);

  args[0] = ASMREF_L;     /* lua_State *L */
  args[1] = ASMREF_TMP1;  /* MSize size   */
  asm_gencall(as, ci, args);
  emit_loadi(as, ra_releasetmp(as, ASMREF_TMP1), (int32_t)(sz+sizeof(GCcdata)));
}
#endif
ASM_S390X_STUB_IR(asm_obar)

static void asm_conv(ASMState *as, IRIns *ir)
{
  IRType st = (IRType)(ir->op2 & IRCONV_SRCMASK);
  IRRef lref = ir->op1;

  lj_assertA(irt_type(ir->t) != st, "inconsistent types for CONV");

  if (irt_isnum(ir->t)) {
    Reg dest = ra_dest(as, ir, RSET_FPR);
    if (st == IRT_NUM) {
      ra_leftov(as, dest, lref);
      return;
    }
    if (st == IRT_INT || st == IRT_U32 || st == IRT_U16 || st == IRT_U8 ||
	st == IRT_I16 || st == IRT_I8) {
      Reg left = ra_alloc1_nobase(as, lref, RSET_GPR_NOB, -271);
      if (st == IRT_U32 || st == IRT_U16 || st == IRT_U8)
	emit_u32(as, S390X_INS_RXE(S390XI_LLGFR, left, left));
      else
	emit_u32(as, S390X_INS_RXE(S390XI_LGFR, left, left));
      emit_u32(as, S390X_INS_RXE(S390XI_CDFBR, dest, left));
      return;
    }
    if (st == IRT_I64 || st == IRT_U64 || st == IRT_P64) {
      Reg left = ra_alloc1_nobase(as, lref, RSET_GPR_NOB, -271);
      emit_u32(as, S390X_INS_RXE(S390XI_CDGBR, dest, left));
      return;
    }
    asm_s390x_nyi_ir(as, ir);
    return;
  }

  if (st == IRT_NUM) {
    Reg left = ra_alloc1(as, lref, RSET_FPR);
    if (irt_isguard(ir->t)) {
      lj_assertA(irt_isint(ir->t), "bad type for checked CONV");
      asm_tointg(as, ir, left);
      return;
    }
    if (irt_isint(ir->t)) {
      Reg dest = ra_dest_nobase(as, ir, RSET_GPR_NOB, -274);
      emit_u32(as, S390X_INS_RXE(S390XI_LGFR, dest, dest));
      emit_u32(as, S390X_INS_RRF_M(S390XI_CFDBR, dest, 5, left));
      return;
    }
    asm_s390x_nyi_ir(as, ir);
    return;
  }

  if (st >= IRT_I8 && st <= IRT_U16) {
    Reg dest = ra_dest_nobase(as, ir, RSET_GPR_NOB, -275);
    Reg left = ra_alloc1_nobase(as, lref, RSET_GPR_NOB, -272);
    lj_assertA(irt_isint(ir->t) || irt_isu32(ir->t), "bad type for CONV EXT");
    if ((ir->op2 & IRCONV_SEXT) || st == IRT_I8 || st == IRT_I16)
      emit_u32(as, S390X_INS_RXE(S390XI_LGFR, dest, left));
    else
      emit_u32(as, S390X_INS_RXE(S390XI_LLGFR, dest, left));
    return;
  }

  {
    Reg dest = ra_dest(as, ir, RSET_GPR);
    ra_leftov(as, dest, lref);
  }
}

ASM_S390X_STUB_IR(asm_strto)

#undef ASM_S390X_STUB_IR

/* -- Trace patching ------------------------------------------------------ */

void lj_asm_patchexit(jit_State *J, GCtrace *T, ExitNo exitno, MCode *target)
{
  MCode *mcarea = lj_mcode_patch(J, T->mcode, 0);
  MCode *px = exitstub_trace_addr(T, exitno);
  ptrdiff_t delta = (char *)target - (char *)px;
  lj_assertJ((delta & 1) == 0, "unaligned patched exit target");
  lj_assertJ(checki32((int64_t)(delta >> 1)),
	     "s390x patched exit target out of range");
  emit_u48_at(px, S390X_INS_BRCL(CC_AL, (int32_t)(delta >> 1)));
  lj_mcode_sync(px, px + 2);
  lj_mcode_patch(J, mcarea, 1);
}
