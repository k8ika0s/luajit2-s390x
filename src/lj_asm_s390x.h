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
    if (!ra_hashint(r) && !iscrossref(as, ref))
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
  if (mxp - (nexits + 4 + MCLIM_REDZONE) < as->mclim)
    asm_mclimit(as);
  as->mcp = mxp;
  emit_call(as, RID_R14, target);
  emit_loadi(as, RID_TMP, (int32_t)as->T->traceno);
  mxp = as->mcp;
  for (i = nexits; i > 0; i--) {
    emit_jmp(as, mxp);
  }
  as->mctop = as->mcp;
}

static MCode *asm_exitstub_addr(ASMState *as, ExitNo exitno)
{
  return as->mctop + exitno;
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
  if (LJ_UNLIKELY(p == as->invmcp)) {
    as->loopinv = 1;
    *p = S390X_INS_BRC(CC_AL, (int32_t)(((char *)target - (char *)p) >> 1));
    emit_condbranch(as, (S390XCC)asm_guardcc_invert(cc), p);
    return;
  }
  emit_condbranch(as, (S390XCC)cc, target);
}

/* -- Trace setup --------------------------------------------------------- */

static void asm_setup_target(ASMState *as)
{
  asm_exitstub_setup(as, as->T->nsnap + (as->parent ? 1 : 0));
}

static void asm_tail_prep(ASMState *as, TraceNo lnk)
{
  MCode *p = as->mctop - 1;  /* Leave room for loop/exit branch. */
  if (as->loopref) {
    as->invmcp = as->mcp = p;
  } else {
    UNUSED(lnk);
    as->mcp = p;
    as->invmcp = NULL;
  }
  as->mctail = p;
  *p = 0;  /* Keep the reserved slot explicit for later patching. */
}

static void asm_tail_fixup(ASMState *as, TraceNo lnk)
{
  UNUSED(lnk);
  asm_s390x_nyi_tag(as, -101);
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
  if (ra_hasreg(r)) {
    ra_free(as, r);
    if (rset_test(as->modset, r) || irt_ismarked(ir->t))
      ir->r = RID_INIT;  /* No inheritance for a modified BASE register. */
    if (irp->r == r) {
      return r;
    } else if (ra_hasreg(irp->r) && rset_test(as->freeset, irp->r)) {
      emit_movrr(as, ir, r, irp->r);
      return irp->r;
    }
  }
  return RID_NONE;
}

static void asm_stack_check(ASMState *as, BCReg topslot,
			    IRIns *irp, RegSet allow, ExitNo exitno)
{
  UNUSED(topslot); UNUSED(irp); UNUSED(allow); UNUSED(exitno);
  asm_s390x_nyi_tag(as, -106);
}

static void asm_stack_restore(ASMState *as, SnapShot *snap)
{
  UNUSED(snap);
  asm_s390x_nyi_tag(as, -107);
}

/* -- Calls and shared helpers ------------------------------------------- */

static void asm_gencall(ASMState *as, const CCallInfo *ci, IRRef *args)
{
  uint32_t n, nargs = CCI_XNARGS(ci);
  Reg gpr = REGARG_FIRSTGPR;
  if (!ci->func) {
    asm_s390x_nyi_tag(as, -108);
    return;
  }
  if (ci->flags & (CCI_VARARG|CCI_CASTU64)) {
    asm_s390x_nyi_tag(as, -116);
    return;
  }
  if (nargs > REGARG_NUMGPR) {
    asm_s390x_nyi_tag(as, -113);
    return;
  }
  emit_call(as, RID_R14, (void *)ci->func);
  for (n = 0; n < nargs; n++, gpr++) {
    IRRef ref = args[n];
    if (!ref)
      continue;
    if (irt_isfp(IR(ref)->t)) {
      asm_s390x_nyi_tag(as, -115);
      return;
    }
    lj_assertA(rset_test(as->freeset, gpr), "reg %d not free", gpr);
    ra_leftov(as, gpr, ref);
  }
}

static void asm_setupresult(ASMState *as, IRIns *ir, const CCallInfo *ci)
{
  RegSet drop = RSET_SCRATCH;
  int hiop = ((ir+1)->o == IR_HIOP && !irt_isnil((ir+1)->t));
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
      ra_destreg(as, ir, RID_RET);
    }
  }
}

static void asm_gc_check(ASMState *as)
{
  asm_s390x_nyi_tag(as, -109);
}

static void asm_tvptr(ASMState *as, Reg dest, IRRef ref, MSize mode)
{
  UNUSED(dest); UNUSED(ref); UNUSED(mode);
  asm_s390x_nyi_tag(as, -110);
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
  int nslots = SPS_FIRST;
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
  Reg left, right;
  int cc;
  lj_assertA(irt_isint(ir->t) || irt_isu32(ir->t) || irt_isaddr(ir->t) ||
	     irt_isu8(ir->t), "bad comparison data type %d", irt_type(ir->t));
  if (irref_isk(lref) && !irref_isk(rref)) {
    IRRef tmp = lref; lref = rref; rref = tmp;
    op = asm_comp_swapop(op);
  }
  cc = asm_compmap[op];
  left = ra_alloc1(as, lref, RSET_GPR);
  asm_guardcc(as, cc & 15);
  if (irref_isk(rref)) {
    int32_t k = IR(rref)->i;
    if ((cc & CC_UNSIGNED) == 0 && checki16(k)) {
      emit_u32(as, S390X_INS_RI(S390XI_CGHI, left, k));
      return;
    }
    if (checki16(k)) {
      right = ra_allock(as, k, rset_exclude(RSET_GPR, left));
    } else {
      asm_s390x_nyi_ir(as, ir);
      return;
    }
  } else {
    right = ra_alloc1(as, rref, rset_exclude(RSET_GPR, left));
  }
  emit_u32(as, S390X_INS_RXE((cc & CC_UNSIGNED) ? S390XI_CLGR : S390XI_CGR,
			     left, right));
}

static void asm_add(ASMState *as, IRIns *ir)
{
  Reg dest = ra_dest(as, ir, RSET_GPR);
  Reg left, right;
  if (!irt_isinteger(ir->t) && !irt_is64(ir->t) && !irt_isaddr(ir->t)) {
    asm_s390x_nyi_ir(as, ir);
    return;
  }
  left = ra_hintalloc(as, ir->op1, dest, RSET_GPR);
  if (irref_isk(ir->op2)) {
    int32_t k = IR(ir->op2)->i;
    if (!checki16(k)) {
      asm_s390x_nyi_ir(as, ir);
      return;
    }
    if (dest != left)
      emit_movrr(as, ir, dest, left);
    emit_u32(as, S390X_INS_RI(S390XI_AGHI, dest, k));
    return;
  }
  right = ra_alloc1(as, ir->op2, rset_exclude(RSET_GPR, left));
  if (dest == right && dest != left) {
    Reg tmp = left;
    left = right;
    right = tmp;
  } else if (dest != left) {
    emit_movrr(as, ir, dest, left);
  }
  emit_u32(as, S390X_INS_RXE(S390XI_AGR, dest, right));
}

ASM_S390X_STUB_IR(asm_hiop)
ASM_S390X_STUB_IR(asm_prof)
static void asm_comp(ASMState *as, IRIns *ir)
{
  if (irt_isfp(ir->t))
    asm_s390x_nyi_ir(as, ir);
  else
    asm_intcomp(as, ir);
}
#define asm_equal(as, ir)	asm_comp(as, ir)
ASM_S390X_STUB_IR(asm_retf)
ASM_S390X_STUB_IR(asm_bnot)
ASM_S390X_STUB_IR(asm_bswap)
ASM_S390X_STUB_IR(asm_band)
ASM_S390X_STUB_IR(asm_bor)
ASM_S390X_STUB_IR(asm_bxor)
ASM_S390X_STUB_IR(asm_bshl)
ASM_S390X_STUB_IR(asm_bshr)
ASM_S390X_STUB_IR(asm_bsar)
ASM_S390X_STUB_IR(asm_brol)
ASM_S390X_STUB_IR(asm_bror)
ASM_S390X_STUB_IR(asm_sub)
ASM_S390X_STUB_IR(asm_mul)
ASM_S390X_STUB_IR(asm_neg)
ASM_S390X_STUB_IR(asm_abs)
ASM_S390X_STUB_IR(asm_fpdiv)
ASM_S390X_STUB_IR(asm_fpmath)
ASM_S390X_STUB_IR(asm_tobit)
ASM_S390X_STUB_IR(asm_min)
ASM_S390X_STUB_IR(asm_max)
#define asm_addov(as, ir)	asm_add(as, ir)
#define asm_subov(as, ir)	asm_sub(as, ir)
#define asm_mulov(as, ir)	asm_mul(as, ir)
ASM_S390X_STUB_IR(asm_aref)

static void asm_href(ASMState *as, IRIns *ir, IROp merge)
{
  UNUSED(merge);
  asm_s390x_nyi_ir(as, ir);
}

ASM_S390X_STUB_IR(asm_hrefk)
ASM_S390X_STUB_IR(asm_uref)
ASM_S390X_STUB_IR(asm_fref)
ASM_S390X_STUB_IR(asm_strref)
ASM_S390X_STUB_IR(asm_ahuvload)
ASM_S390X_STUB_IR(asm_fload)
ASM_S390X_STUB_IR(asm_xload)

static void asm_sload(ASMState *as, IRIns *ir)
{
  int32_t ofs = 8 * ((int32_t)ir->op1 - 2);
  IRType1 t = ir->t;
  Reg dest = RID_NONE, base;
  RegSet allow = RSET_GPR;

  lj_assertA(!(ir->op2 & IRSLOAD_PARENT), "bad parent SLOAD");
  lj_assertA(irt_isguard(t) || !(ir->op2 & IRSLOAD_TYPECHECK),
	     "inconsistent SLOAD variant");

  if (ir->op2 & IRSLOAD_CONVERT) {
    asm_s390x_nyi_tag(as, -117);
    return;
  }
  if (ra_used(ir)) {
    if (!(irt_isint(t) || irt_isu32(t) || irt_isaddr(t))) {
      asm_s390x_nyi_tag(as, -118);
      return;
    }
    dest = ra_dest(as, ir, allow);
    base = ra_alloc1(as, REF_BASE, rset_clear(allow, dest));
    if (irt_isaddr(t)) {
      emit_shiftimm(as, S390XI_SRLG, dest, dest, 17);
      emit_shiftimm(as, S390XI_SLLG, dest, dest, 17);
    } else if (irt_isint(t)) {
      emit_u32(as, S390X_INS_RXE(S390XI_LGFR, dest, dest));
    }
    goto dotypecheck;
  }
  base = ra_alloc1(as, REF_BASE, allow);

dotypecheck:
  rset_clear(allow, base);
  if (ir->op2 & IRSLOAD_TYPECHECK) {
    Reg tmp = ra_scratch(as, allow);
    asm_guardcc(as, CC_NE);
    emit_u32(as, S390X_INS_RI(S390XI_CGHI, tmp, (int32_t)irt_toitype(t)));
    emit_shiftimm(as, S390XI_SRAG, tmp, tmp, 47);
    emit_load64ofs(as, tmp, base, ofs);
  }
  if (ra_hasreg(dest)) {
    if (irt_isaddr(t)) {
      emit_load64ofs(as, dest, base, ofs);
    } else {
      emit_loadu32ofs(as, dest, base, ofs + (LJ_BE ? 4 : 0));
    }
  }
}

ASM_S390X_STUB_IR(asm_ahustore)
ASM_S390X_STUB_IR(asm_fstore)
ASM_S390X_STUB_IR(asm_xstore)
ASM_S390X_STUB_IR(asm_cnew)
ASM_S390X_STUB_IR(asm_tbar)
ASM_S390X_STUB_IR(asm_obar)
ASM_S390X_STUB_IR(asm_conv)
ASM_S390X_STUB_IR(asm_strto)
ASM_S390X_STUB_IR(asm_callx)

#undef ASM_S390X_STUB_IR

/* -- Trace patching ------------------------------------------------------ */

void lj_asm_patchexit(jit_State *J, GCtrace *T, ExitNo exitno, MCode *target)
{
  UNUSED(J); UNUSED(T); UNUSED(exitno); UNUSED(target);
}
