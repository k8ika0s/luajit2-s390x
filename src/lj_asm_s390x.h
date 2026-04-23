/*
** S390X IR assembler scaffolding (SSA IR -> machine code).
** Copyright (C) 2005-2026 Mike Pall. See Copyright Notice in luajit.h
**
** This backend lowers LuaJIT IR to s390x machine code. Unsupported or unsafe
** target-specific forms use explicit NYI fallbacks while bring-up continues.
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

static uint64_t asm_cnewi_k64val(ASMState *as, IRRef ref)
{
  IRIns *ir = IR(ref);
  if (ir->o == IR_KINT)
    return (uint64_t)(uint32_t)ir->i;
  return asm_k64val(as, ref);
}

static LJ_NORET LJ_NOINLINE void asm_s390x_nyi_tag(ASMState *as, int32_t tag);

static int asm_s390x_ir_log_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_IR_LOG") != NULL);
  return enabled;
}

static int asm_s390x_guard_log_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_GUARD_LOG") != NULL);
  return enabled;
}

static int asm_s390x_guardmark_enabled(void)
{
  return 0;
}

static int asm_s390x_guardmark_taken_enabled(void)
{
  return 0;
}

static int asm_s390x_gc64_signed_int_sload_enabled(void)
{
  return LJ_GC64;
}

static int asm_s390x_call_log_enabled(void)
{
  return 0;
}

static int asm_s390x_direct_call_arg_enabled(void)
{
  return 1;
}

static int asm_s390x_add_log_enabled(void)
{
  return 0;
}

static int asm_s390x_addhome_log_enabled(void)
{
  return 0;
}

static int asm_s390x_low32home_log_enabled(void)
{
  return 0;
}

static int asm_s390x_low32cmp_log_enabled(void)
{
  return 0;
}

static int asm_s390x_bitop_log_enabled(void)
{
  return 0;
}

static int asm_s390x_bnorm_log_enabled(void)
{
  return 0;
}

static int asm_s390x_is_intarith_op(IROp op)
{
  switch (op) {
  case IR_ADD: case IR_ADDOV:
  case IR_SUB: case IR_SUBOV:
  case IR_MUL: case IR_MULOV:
  case IR_NEG:
    return 1;
  default:
    return 0;
  }
}

static int asm_s390x_is_bitop_op(IROp op)
{
  switch (op) {
  case IR_BNOT: case IR_BSWAP:
  case IR_BAND: case IR_BOR: case IR_BXOR:
  case IR_BSHL: case IR_BSHR: case IR_BSAR:
  case IR_BROL: case IR_BROR:
    return 1;
  default:
    return 0;
  }
}

static int asm_s390x_is_logic_bitop_op(IROp op)
{
  return op == IR_BAND || op == IR_BOR || op == IR_BXOR;
}

static int asm_s390x_is_int32home_safe_bitop_consumer(IROp op)
{
  return op == IR_BAND || op == IR_BOR || op == IR_BXOR;
}

static int asm_s390x_is_low32home_source_op(IROp op)
{
  return asm_s390x_is_bitop_op(op) || op == IR_ADD || op == IR_PHI;
}

static int asm_s390x_is_low32home_family_use(IRIns *use)
{
  return asm_s390x_is_bitop_op(use->o) ||
	 (use->o == IR_ADD && !irt_isguard(use->t)) ||
	 use->o == IR_PHI;
}

static const char *asm_s390x_low32home_hard_kind(IRIns *use)
{
  if (irt_isguard(use->t))
    return "guard";
  if (use->o == IR_ASTORE || use->o == IR_HSTORE || use->o == IR_USTORE ||
      use->o == IR_FSTORE || use->o == IR_XSTORE)
    return "store";
  if (use->o == IR_CALLN || use->o == IR_CALLL || use->o == IR_CALLS ||
      use->o == IR_CALLXS)
    return "call";
  return "other";
}

static const char *asm_s390x_irop_name(IROp op)
{
  switch (op) {
  case IR_LT: return "LT";
  case IR_GE: return "GE";
  case IR_LE: return "LE";
  case IR_GT: return "GT";
  case IR_ULT: return "ULT";
  case IR_UGE: return "UGE";
  case IR_ULE: return "ULE";
  case IR_UGT: return "UGT";
  case IR_EQ: return "EQ";
  case IR_NE: return "NE";
  case IR_PHI: return "PHI";
  case IR_BNOT: return "BNOT";
  case IR_BSWAP: return "BSWAP";
  case IR_BAND: return "BAND";
  case IR_BOR: return "BOR";
  case IR_BXOR: return "BXOR";
  case IR_BSHL: return "BSHL";
  case IR_BSHR: return "BSHR";
  case IR_BSAR: return "BSAR";
  case IR_BROL: return "BROL";
  case IR_BROR: return "BROR";
  case IR_ADD: return "ADD";
  case IR_ADDOV: return "ADDOV";
  case IR_SUB: return "SUB";
  case IR_NEG: return "NEG";
  case IR_AREF: return "AREF";
  case IR_ASTORE: return "ASTORE";
  case IR_FSTORE: return "FSTORE";
  case IR_XSTORE: return "XSTORE";
  case IR_CALLN: return "CALLN";
  case IR_CALLL: return "CALLL";
  case IR_CALLS: return "CALLS";
  case IR_CALLXS: return "CALLXS";
  case IR_KINT: return "KINT";
  case IR_KNULL: return "KNULL";
  case IR_SLOAD: return "SLOAD";
  default: return "OTHER";
  }
}

enum {
  S390X_LOW32CMP_PHASE_INTCOMP,
  S390X_LOW32CMP_PHASE_EQUAL,
  S390X_LOW32CMP_PHASE__MAX
};

enum {
  S390X_LOW32CMP_SRC_K,
  S390X_LOW32CMP_SRC_BNOT,
  S390X_LOW32CMP_SRC_BSWAP,
  S390X_LOW32CMP_SRC_BAND,
  S390X_LOW32CMP_SRC_BOR,
  S390X_LOW32CMP_SRC_BXOR,
  S390X_LOW32CMP_SRC_BSHL,
  S390X_LOW32CMP_SRC_BSHR,
  S390X_LOW32CMP_SRC_BSAR,
  S390X_LOW32CMP_SRC_BROL,
  S390X_LOW32CMP_SRC_BROR,
  S390X_LOW32CMP_SRC_ADD,
  S390X_LOW32CMP_SRC_PHI,
  S390X_LOW32CMP_SRC_OTHER,
  S390X_LOW32CMP_SRC__MAX
};

enum {
  S390X_LOW32CMP_ADDK_NONE,
  S390X_LOW32CMP_ADDK_CTRL_INC,
  S390X_LOW32CMP_ADDK_BITOP_TAIL,
  S390X_LOW32CMP_ADDK_OTHER,
  S390X_LOW32CMP_ADDK__MAX
};

static int asm_s390x_low32cmp_phase_index(const char *phase)
{
  return strcmp(phase, "equal") == 0 ? S390X_LOW32CMP_PHASE_EQUAL :
	 S390X_LOW32CMP_PHASE_INTCOMP;
}

static const char *asm_s390x_low32cmp_phase_name(int idx)
{
  return idx == S390X_LOW32CMP_PHASE_EQUAL ? "equal" : "intcomp";
}

static int asm_s390x_low32cmp_source_index(IRIns *ir)
{
  if (!ir)
    return S390X_LOW32CMP_SRC_K;
  switch (ir->o) {
  case IR_BNOT: return S390X_LOW32CMP_SRC_BNOT;
  case IR_BSWAP: return S390X_LOW32CMP_SRC_BSWAP;
  case IR_BAND: return S390X_LOW32CMP_SRC_BAND;
  case IR_BOR: return S390X_LOW32CMP_SRC_BOR;
  case IR_BXOR: return S390X_LOW32CMP_SRC_BXOR;
  case IR_BSHL: return S390X_LOW32CMP_SRC_BSHL;
  case IR_BSHR: return S390X_LOW32CMP_SRC_BSHR;
  case IR_BSAR: return S390X_LOW32CMP_SRC_BSAR;
  case IR_BROL: return S390X_LOW32CMP_SRC_BROL;
  case IR_BROR: return S390X_LOW32CMP_SRC_BROR;
  case IR_ADD: return S390X_LOW32CMP_SRC_ADD;
  case IR_PHI: return S390X_LOW32CMP_SRC_PHI;
  default: return S390X_LOW32CMP_SRC_OTHER;
  }
}

static const char *asm_s390x_low32cmp_source_name(int idx)
{
  switch (idx) {
  case S390X_LOW32CMP_SRC_K: return "K";
  case S390X_LOW32CMP_SRC_BNOT: return "BNOT";
  case S390X_LOW32CMP_SRC_BSWAP: return "BSWAP";
  case S390X_LOW32CMP_SRC_BAND: return "BAND";
  case S390X_LOW32CMP_SRC_BOR: return "BOR";
  case S390X_LOW32CMP_SRC_BXOR: return "BXOR";
  case S390X_LOW32CMP_SRC_BSHL: return "BSHL";
  case S390X_LOW32CMP_SRC_BSHR: return "BSHR";
  case S390X_LOW32CMP_SRC_BSAR: return "BSAR";
  case S390X_LOW32CMP_SRC_BROL: return "BROL";
  case S390X_LOW32CMP_SRC_BROR: return "BROR";
  case S390X_LOW32CMP_SRC_ADD: return "ADD";
  case S390X_LOW32CMP_SRC_PHI: return "PHI";
  default: return "OTHER";
  }
}

static int asm_s390x_low32cmp_add_kind(ASMState *as, IRIns *ir)
{
  IRIns *lir, *rir = NULL;
  if (!ir || ir->o != IR_ADD)
    return S390X_LOW32CMP_ADDK_NONE;
  lir = IR(ir->op1);
  if (!irref_isk(ir->op2))
    rir = IR(ir->op2);
  if (irref_isk(ir->op2) && IR(ir->op2)->o == IR_KINT &&
      IR(ir->op2)->i == 1 &&
      (lir->o == IR_SLOAD || lir->o == IR_ADD || lir->o == IR_PHI))
    return S390X_LOW32CMP_ADDK_CTRL_INC;
  if (asm_s390x_is_bitop_op(lir->o) || (rir && asm_s390x_is_bitop_op(rir->o)))
    return S390X_LOW32CMP_ADDK_BITOP_TAIL;
  return S390X_LOW32CMP_ADDK_OTHER;
}

static const char *asm_s390x_low32cmp_add_kind_name(int idx)
{
  switch (idx) {
  case S390X_LOW32CMP_ADDK_NONE: return "none";
  case S390X_LOW32CMP_ADDK_CTRL_INC: return "ctrl_inc";
  case S390X_LOW32CMP_ADDK_BITOP_TAIL: return "bitop_tail";
  default: return "other";
  }
}

static uint64_t asm_s390x_low32cmp_total;
static uint64_t asm_s390x_low32cmp_phase_counts[S390X_LOW32CMP_PHASE__MAX];
static uint64_t asm_s390x_low32cmp_op_counts[IR__MAX];
static uint64_t asm_s390x_low32cmp_left_counts[S390X_LOW32CMP_SRC__MAX];
static uint64_t asm_s390x_low32cmp_right_counts[S390X_LOW32CMP_SRC__MAX];
static uint64_t asm_s390x_low32cmp_left_addkind_counts[S390X_LOW32CMP_ADDK__MAX];
static uint64_t asm_s390x_low32cmp_right_addkind_counts[S390X_LOW32CMP_ADDK__MAX];
static uint64_t asm_s390x_low32cmp_cmp32u_counts[2];
static uint64_t asm_s390x_low32cmp_imm16_counts[2];
static int asm_s390x_low32cmp_atexit_registered;

static void asm_s390x_low32cmp_dump_summary(void)
{
  int i;
  if (asm_s390x_low32cmp_total == 0)
    return;
  fprintf(stderr, "S390X_LOW32CMP_SUMMARY total=%llu\n",
	  (unsigned long long)asm_s390x_low32cmp_total);
  for (i = 0; i < S390X_LOW32CMP_PHASE__MAX; i++) {
    if (asm_s390x_low32cmp_phase_counts[i] == 0)
      continue;
    fprintf(stderr, "S390X_LOW32CMP_PHASE phase=%s count=%llu\n",
	    asm_s390x_low32cmp_phase_name(i),
	    (unsigned long long)asm_s390x_low32cmp_phase_counts[i]);
  }
  for (i = 0; i < IR__MAX; i++) {
    if (asm_s390x_low32cmp_op_counts[i] == 0)
      continue;
    fprintf(stderr, "S390X_LOW32CMP_OP op=%s count=%llu\n",
	    asm_s390x_irop_name((IROp)i),
	    (unsigned long long)asm_s390x_low32cmp_op_counts[i]);
  }
  for (i = 0; i < S390X_LOW32CMP_SRC__MAX; i++) {
    if (asm_s390x_low32cmp_left_counts[i] != 0)
      fprintf(stderr, "S390X_LOW32CMP_LEFT src=%s count=%llu\n",
	      asm_s390x_low32cmp_source_name(i),
	      (unsigned long long)asm_s390x_low32cmp_left_counts[i]);
    if (asm_s390x_low32cmp_right_counts[i] != 0)
      fprintf(stderr, "S390X_LOW32CMP_RIGHT src=%s count=%llu\n",
	      asm_s390x_low32cmp_source_name(i),
	      (unsigned long long)asm_s390x_low32cmp_right_counts[i]);
  }
  for (i = 0; i < S390X_LOW32CMP_ADDK__MAX; i++) {
    if (asm_s390x_low32cmp_left_addkind_counts[i] != 0)
      fprintf(stderr, "S390X_LOW32CMP_LEFT_ADDK kind=%s count=%llu\n",
	      asm_s390x_low32cmp_add_kind_name(i),
	      (unsigned long long)asm_s390x_low32cmp_left_addkind_counts[i]);
    if (asm_s390x_low32cmp_right_addkind_counts[i] != 0)
      fprintf(stderr, "S390X_LOW32CMP_RIGHT_ADDK kind=%s count=%llu\n",
	      asm_s390x_low32cmp_add_kind_name(i),
	      (unsigned long long)asm_s390x_low32cmp_right_addkind_counts[i]);
  }
  for (i = 0; i < 2; i++) {
    if (asm_s390x_low32cmp_cmp32u_counts[i] != 0)
      fprintf(stderr, "S390X_LOW32CMP_CMP32U flag=%d count=%llu\n",
	      i,
	      (unsigned long long)asm_s390x_low32cmp_cmp32u_counts[i]);
    if (asm_s390x_low32cmp_imm16_counts[i] != 0)
      fprintf(stderr, "S390X_LOW32CMP_IMM16 flag=%d count=%llu\n",
	      i,
	      (unsigned long long)asm_s390x_low32cmp_imm16_counts[i]);
  }
}

static const char *asm_s390x_bnorm_site(IRIns *ir, IRIns *lir, IRIns *rir)
{
  int leftbit = asm_s390x_is_bitop_op(lir->o);
  int rightbit = rir ? asm_s390x_is_bitop_op(rir->o) : 0;

  if (asm_s390x_is_logic_bitop_op(ir->o)) {
    if (leftbit && rightbit)
      return "chain-binary";
    if (leftbit || rightbit)
      return "mixed-binary";
    return "source-binary";
  }

  if (ir->o == IR_BNOT || ir->o == IR_BSWAP)
    return leftbit ? "chain-unary" : "source-unary";

  if (ir->o == IR_BSHL || ir->o == IR_BSHR || ir->o == IR_BSAR ||
      ir->o == IR_BROL || ir->o == IR_BROR)
    return leftbit ? "chain-shift" : "source-shift";

  return leftbit ? "chain-other" : "source-other";
}

static void asm_s390x_bnorm_use_counts(ASMState *as, IRIns *ir,
				       int *safe_bitop_uses,
				       int *unsafe_bitop_uses,
				       int *intarith_uses, int *other_uses,
				       int *guard_uses, int *first_use_op,
				       int *first_nonbitop_use_op)
{
  IRRef ref = (IRRef)(ir - as->ir);
  IRIns *use;

  *safe_bitop_uses = 0;
  *unsafe_bitop_uses = 0;
  *intarith_uses = 0;
  *other_uses = 0;
  *guard_uses = 0;
  *first_use_op = -1;
  *first_nonbitop_use_op = -1;

  for (use = IR(as->orignins-1); use > ir; use--) {
    if (use->op1 != ref && use->op2 != ref)
      continue;
    if (*first_use_op == -1)
      *first_use_op = (int)use->o;
    if (asm_s390x_is_bitop_op(use->o)) {
      if (asm_s390x_is_int32home_safe_bitop_consumer(use->o))
	(*safe_bitop_uses)++;
      else
	(*unsafe_bitop_uses)++;
    } else {
      if (*first_nonbitop_use_op == -1)
	*first_nonbitop_use_op = (int)use->o;
      if (asm_s390x_is_intarith_op(use->o) && irt_isinteger(use->t))
	(*intarith_uses)++;
      else
	(*other_uses)++;
    }
    if (irt_isguard(use->t))
      (*guard_uses)++;
  }
}

static void asm_s390x_bnorm_log(ASMState *as, IRIns *ir, Reg dest)
{
  IRIns *lir;
  IRIns *rir;
  const char *site;
  int safe_bitop_uses, unsafe_bitop_uses, intarith_uses, other_uses;
  int guard_uses;
  int first_use_op, first_nonbitop_use_op;
  int carry_candidate, tail_candidate, int32home_candidate;
  if (!asm_s390x_bnorm_log_enabled() || !asm_s390x_is_bitop_op(ir->o))
    return;
  lir = IR(ir->op1);
  rir = irref_isk(ir->op2) ? NULL : IR(ir->op2);
  site = asm_s390x_bnorm_site(ir, lir, rir);
  asm_s390x_bnorm_use_counts(as, ir, &safe_bitop_uses, &unsafe_bitop_uses,
			     &intarith_uses, &other_uses, &guard_uses,
			     &first_use_op,
			     &first_nonbitop_use_op);
  carry_candidate = safe_bitop_uses > 0 && unsafe_bitop_uses == 0 &&
		    intarith_uses == 0 && other_uses == 0;
  tail_candidate = safe_bitop_uses == 0 && unsafe_bitop_uses == 0 &&
		   intarith_uses > 0 && other_uses == 0;
  int32home_candidate = carry_candidate;
  fprintf(stderr,
	  "S390X_BNORM curins=%d ir=%d op=%d type=%d dest=%d site=%s leftref=%d leftop=%d lefttype=%d leftbitop=%d leftint=%d leftu32=%d left64=%d rightref=%d rightop=%d righttype=%d rightbitop=%d rightint=%d rightu32=%d right64=%d selfint=%d selfu32=%d self64=%d safe_bitop_uses=%d unsafe_bitop_uses=%d intarith_uses=%d other_uses=%d guard_uses=%d first_use_op=%d first_nonbitop_use_op=%d carry_candidate=%d tail_candidate=%d int32home_candidate=%d\n",
	  (int)(as->curins - REF_BIAS),
	  (int)((ir - as->ir) - REF_BIAS),
	  (int)ir->o,
	  (int)irt_type(ir->t),
	  (int)dest,
	  site,
	  (int)(ir->op1 - REF_BIAS),
	  (int)lir->o,
	  (int)irt_type(lir->t),
	  asm_s390x_is_bitop_op(lir->o),
	  (int)irt_isinteger(lir->t),
	  (int)irt_isu32(lir->t),
	  (int)irt_is64(lir->t),
	  irref_isk(ir->op2) ? -1 : (int)(ir->op2 - REF_BIAS),
	  rir ? (int)rir->o : -1,
	  rir ? (int)irt_type(rir->t) : -1,
	  rir ? asm_s390x_is_bitop_op(rir->o) : 0,
	  rir ? (int)irt_isinteger(rir->t) : 0,
	  rir ? (int)irt_isu32(rir->t) : 0,
	  rir ? (int)irt_is64(rir->t) : 0,
	  (int)irt_isinteger(ir->t),
	  (int)irt_isu32(ir->t),
	  (int)irt_is64(ir->t),
	  safe_bitop_uses,
	  unsafe_bitop_uses,
	  intarith_uses,
	  other_uses,
	  guard_uses,
	  first_use_op,
	  first_nonbitop_use_op,
	  carry_candidate,
	  tail_candidate,
	  int32home_candidate);
}

static int asm_s390x_can_defer_bnorm32(ASMState *as, IRIns *ir)
{
  int safe_bitop_uses, unsafe_bitop_uses, intarith_uses, other_uses;
  int guard_uses, first_use_op, first_nonbitop_use_op;
  if (!asm_s390x_is_bitop_op(ir->o))
    return 0;
  if (ir->o == IR_BSWAP)
    return 0;
  if (ir->o == IR_BAND && irref_isk(ir->op2) && IR(ir->op2)->o == IR_KINT &&
      IR(ir->op2)->i >= 0)
    return 1;
  asm_s390x_bnorm_use_counts(as, ir, &safe_bitop_uses, &unsafe_bitop_uses,
			     &intarith_uses, &other_uses, &guard_uses,
			     &first_use_op, &first_nonbitop_use_op);
  return safe_bitop_uses > 0 && unsafe_bitop_uses == 0 &&
	 intarith_uses == 0 && other_uses == 0 && guard_uses == 0;
}

static int asm_s390x_only_used_by_ref(ASMState *as, IRIns *ir, IRRef useref)
{
  IRRef ref = (IRRef)(ir - as->ir);
  IRIns *use;
  int uses = 0;
  for (use = IR(as->orignins-1); use > ir; use--) {
    if (use->op1 != ref && use->op2 != ref)
      continue;
    if ((IRRef)(use - as->ir) != useref)
      return 0;
    uses++;
  }
  return uses == 1;
}

static int asm_s390x_guarded_addsub_op32home_depth(ASMState *as, IRRef ref,
						   int depth)
{
  IRIns *ir;
  if (depth <= 0)
    return 0;
  if (irref_isk(ref))
    return IR(ref)->o == IR_KINT;
  ir = IR(ref);
  if (!irt_isint(ir->t))
    return 0;
  switch (ir->o) {
  case IR_SLOAD:
  case IR_ADDOV:
  case IR_SUBOV:
    return 1;
  case IR_PHI:
    return asm_s390x_guarded_addsub_op32home_depth(as, ir->op1, depth-1) &&
	   asm_s390x_guarded_addsub_op32home_depth(as, ir->op2, depth-1);
  case IR_BAND:
    return irref_isk(ir->op2) && IR(ir->op2)->o == IR_KINT &&
	   IR(ir->op2)->i >= 0;
  default:
    return 0;
  }
}

static int asm_s390x_guarded_addsub_op32home(ASMState *as, IRRef ref)
{
  return asm_s390x_guarded_addsub_op32home_depth(as, ref, 3);
}

static void asm_s390x_addhome_use_counts(ASMState *as, IRIns *ir,
					 int *add_uses, int *phi_uses,
					 int *other_uses, int *guard_uses,
					 int *first_use_op,
					 int *first_noncarry_use_op);

static int asm_s390x_plain_add_op32home_depth(ASMState *as, IRRef ref,
					      int depth)
{
  IRIns *ir;
  if (depth <= 0)
    return 0;
  if (irref_isk(ref))
    return IR(ref)->o == IR_KINT;
  ir = IR(ref);
  if (!irt_isint(ir->t))
    return 0;
  switch (ir->o) {
  case IR_SLOAD:
  case IR_ADDOV:
  case IR_SUBOV:
    return 1;
  case IR_ADD:
    return !irt_isguard(ir->t) &&
	   asm_s390x_plain_add_op32home_depth(as, ir->op1, depth-1) &&
	   asm_s390x_plain_add_op32home_depth(as, ir->op2, depth-1);
  case IR_PHI:
    return asm_s390x_plain_add_op32home_depth(as, ir->op1, depth-1) &&
	   asm_s390x_plain_add_op32home_depth(as, ir->op2, depth-1);
  case IR_BAND:
    return irref_isk(ir->op2) && IR(ir->op2)->o == IR_KINT &&
	   IR(ir->op2)->i >= 0;
  default:
    return 0;
  }
}

static int asm_s390x_plain_add_op32home(ASMState *as, IRRef ref)
{
  return asm_s390x_plain_add_op32home_depth(as, ref, 4);
}

static int asm_s390x_can_defer_plain_add_bnorm32(ASMState *as, IRIns *ir)
{
  int add_uses, phi_uses, other_uses, guard_uses;
  int first_use_op, first_noncarry_use_op;
  if (irt_isguard(ir->t) || !irt_isint(ir->t))
    return 0;
  asm_s390x_addhome_use_counts(as, ir, &add_uses, &phi_uses, &other_uses,
			       &guard_uses, &first_use_op,
			       &first_noncarry_use_op);
  return (add_uses > 0 || phi_uses > 0) && other_uses == 0 &&
	 guard_uses == 0;
}

static int asm_s390x_has_preloop_acc_range_guard(ASMState *as, IRRef ref,
						 int depth)
{
  IRRef r;
  IRIns *ir;
  if (!as->loopref || depth < 0 || irref_isk(ref))
    return 0;
  for (r = REF_FIRST; r < as->loopref; r++) {
    IRIns *g = IR(r);
    if (irt_isguard(g->t) && g->o == IR_LE && g->op1 == ref &&
	irref_isk(g->op2) && IR(g->op2)->o == IR_KINT)
      return 1;
  }
  ir = IR(ref);
  if (ir->o == IR_ADD)
    return asm_s390x_has_preloop_acc_range_guard(as, ir->op1, depth-1) ||
	   asm_s390x_has_preloop_acc_range_guard(as, ir->op2, depth-1);
  return 0;
}

static int asm_s390x_plain_add_range_stripped(ASMState *as, IRIns *ir)
{
  IRRef ref = (IRRef)(ir - as->ir);
  return asm_s390x_has_preloop_acc_range_guard(as, ref, 3) ||
	 asm_s390x_has_preloop_acc_range_guard(as, ir->op1, 3) ||
	 asm_s390x_has_preloop_acc_range_guard(as, ir->op2, 3);
}

static int asm_s390x_is_s32_counter_inc(ASMState *as, IRIns *ir)
{
  IRIns *base;
  UNUSED(as);
  if (!ir || ir->o != IR_ADD || irt_isguard(ir->t) || !irt_isint(ir->t))
    return 0;
  if (!irref_isk(ir->op2) || IR(ir->op2)->o != IR_KINT ||
      IR(ir->op2)->i != 1)
    return 0;
  base = IR(ir->op1);
  return base->o == IR_SLOAD || base->o == IR_PHI ||
	 (base->o == IR_ADD && !irt_isguard(base->t));
}

static int asm_s390x_is_counter_cmp_op(IROp op)
{
  return op == IR_LT || op == IR_GE || op == IR_LE || op == IR_GT ||
	 op == IR_ULT || op == IR_UGE || op == IR_ULE || op == IR_UGT;
}

static int asm_s390x_can_defer_counter_add_bnorm32(ASMState *as, IRIns *ir)
{
  IRRef ref = (IRRef)(ir - as->ir);
  IRIns *use;
  int phi_uses = 0, cmp_uses = 0;
  if (!asm_s390x_is_s32_counter_inc(as, ir))
    return 0;
  for (use = IR(as->orignins-1); use > ir; use--) {
    if (use->op1 != ref && use->op2 != ref)
      continue;
    if (use->o == IR_PHI) {
      phi_uses++;
    } else if (irt_isguard(use->t) && asm_s390x_is_counter_cmp_op(use->o)) {
      cmp_uses++;
    } else {
      return 0;
    }
  }
  return phi_uses > 0 && cmp_uses > 0;
}

static int asm_s390x_can_defer_neg_bnorm32(ASMState *as, IRIns *ir)
{
  IRRef ref = (IRRef)(ir - as->ir);
  IRIns *use;
  int uses = 0;
  if (ir->o != IR_NEG || irt_isguard(ir->t) || irt_is64(ir->t))
    return 0;
  for (use = IR(as->orignins-1); use > ir; use--) {
    if (use->op1 != ref && use->op2 != ref)
      continue;
    if (use->o != IR_BSAR || use->op1 != ref ||
	!irref_isk(use->op2) || !asm_s390x_can_defer_bnorm32(as, use))
      return 0;
    uses++;
  }
  return uses > 0;
}

static int asm_s390x_can_use_low32_logic_op(ASMState *as, IRIns *ir)
{
  int safe_bitop_uses, unsafe_bitop_uses, intarith_uses, other_uses;
  int guard_uses, first_use_op, first_nonbitop_use_op;
  if (!asm_s390x_is_logic_bitop_op(ir->o))
    return 0;
  asm_s390x_bnorm_use_counts(as, ir, &safe_bitop_uses, &unsafe_bitop_uses,
			     &intarith_uses, &other_uses, &guard_uses,
			     &first_use_op, &first_nonbitop_use_op);
  return safe_bitop_uses > 0 && unsafe_bitop_uses == 0 &&
	 intarith_uses == 0 && other_uses == 0 && guard_uses == 0;
}

static void asm_s390x_bitop_log(ASMState *as, const char *kind, IRIns *ir,
				Reg dest, Reg left, Reg right, int rightisk)
{
  IRIns *lir = IR(ir->op1);
  IRIns *rir = irref_isk(ir->op2) ? NULL : IR(ir->op2);
  if (!asm_s390x_bitop_log_enabled())
    return;
  fprintf(stderr,
	  "S390X_BITOP kind=%s curins=%d ir=%d op=%d type=%d dest=%d left=%d right=%d rightisk=%d leftref=%d leftop=%d left_r=%d rightref=%d rightop=%d right_r=%d is64=%d isu32=%d isint=%d\n",
	  kind,
	  (int)(as->curins - REF_BIAS),
	  (int)((ir - as->ir) - REF_BIAS),
	  (int)ir->o,
	  (int)irt_type(ir->t),
	  (int)dest,
	  (int)left,
	  (int)right,
	  rightisk,
	  (int)(ir->op1 - REF_BIAS),
	  (int)lir->o,
	  (int)lir->r,
	  irref_isk(ir->op2) ? -1 : (int)(ir->op2 - REF_BIAS),
	  rir ? (int)rir->o : -1,
	  rir ? (int)rir->r : -1,
	  (int)irt_is64(ir->t),
	  (int)irt_isu32(ir->t),
	  (int)irt_isinteger(ir->t));
}

static void asm_s390x_add_log(ASMState *as, const char *kind, IRIns *ir,
			      Reg dest, Reg left, Reg right, Reg tmp)
{
  if (!asm_s390x_add_log_enabled())
    return;
  fprintf(stderr,
	  "S390X_ADD kind=%s curins=%d ir=%d op1=%d op2=%d type=%d dest=%d left=%d right=%d tmp=%d left_r=%d right_r=%d\n",
	  kind,
	  (int)(as->curins - REF_BIAS),
	  (int)((ir - as->ir) - REF_BIAS),
	  (int)(ir->op1 - REF_BIAS),
	  (int)(ir->op2 - REF_BIAS),
	  (int)irt_type(ir->t),
	  (int)dest,
	  (int)left,
	  (int)right,
	  (int)tmp,
	  (int)IR(ir->op1)->r,
	  irref_isk(ir->op2) ? -1 : (int)IR(ir->op2)->r);
}

static void asm_s390x_addhome_use_counts(ASMState *as, IRIns *ir,
					 int *add_uses, int *phi_uses,
					 int *other_uses, int *guard_uses,
					 int *first_use_op,
					 int *first_noncarry_use_op)
{
  IRRef ref = (IRRef)(ir - as->ir);
  IRIns *use;

  *add_uses = 0;
  *phi_uses = 0;
  *other_uses = 0;
  *guard_uses = 0;
  *first_use_op = -1;
  *first_noncarry_use_op = -1;

  for (use = IR(as->orignins-1); use > ir; use--) {
    if (use->op1 != ref && use->op2 != ref)
      continue;
    if (*first_use_op == -1)
      *first_use_op = (int)use->o;
    if (use->o == IR_ADD && !irt_isguard(use->t))
      (*add_uses)++;
    else if (use->o == IR_PHI)
      (*phi_uses)++;
    else {
      if (*first_noncarry_use_op == -1)
	*first_noncarry_use_op = (int)use->o;
      (*other_uses)++;
    }
    if (irt_isguard(use->t))
      (*guard_uses)++;
  }
}

static int asm_s390x_add_low32home_only(ASMState *as, IRIns *ir)
{
  IRIns *lir, *rir;
  int add_uses, phi_uses, other_uses, guard_uses;
  int first_use_op, first_noncarry_use_op;
  if (irt_isguard(ir->t) || !irt_isinteger(ir->t) || irref_isk(ir->op2))
    return 0;
  lir = IR(ir->op1);
  rir = IR(ir->op2);
  if (!asm_s390x_is_bitop_op(lir->o) && !asm_s390x_is_bitop_op(rir->o))
    return 0;
  asm_s390x_addhome_use_counts(as, ir, &add_uses, &phi_uses, &other_uses,
			       &guard_uses, &first_use_op,
			       &first_noncarry_use_op);
  UNUSED(first_use_op);
  UNUSED(first_noncarry_use_op);
  return (add_uses | phi_uses) != 0 && other_uses == 0 && guard_uses == 0;
}

static int asm_s390x_addk_low32home_only(ASMState *as, IRIns *ir)
{
  IRIns *lir;
  int add_uses, phi_uses, other_uses, guard_uses;
  int first_use_op, first_noncarry_use_op;
  if (irt_isguard(ir->t) || !irt_isinteger(ir->t) || !irref_isk(ir->op2))
    return 0;
  lir = IR(ir->op1);
  if (!asm_s390x_is_low32home_source_op(lir->o))
    return 0;
  asm_s390x_addhome_use_counts(as, ir, &add_uses, &phi_uses, &other_uses,
			       &guard_uses, &first_use_op,
			       &first_noncarry_use_op);
  UNUSED(first_use_op);
  UNUSED(first_noncarry_use_op);
  return (add_uses | phi_uses) != 0 && other_uses == 0 && guard_uses == 0;
}

static int asm_s390x_ref_feeds_bitop(ASMState *as, IRRef ref)
{
  IRIns *ir = IR(ref);
  IRIns *use;
  for (use = IR(as->orignins-1); use > ir; use--)
    if ((use->op1 == ref || use->op2 == ref) && asm_s390x_is_bitop_op(use->o))
      return 1;
  return 0;
}

static int asm_s390x_addk1_bitop_loop_carry(ASMState *as, IRIns *ir)
{
  IRRef ref = (IRRef)(ir - as->ir);
  IRIns *use;
  int le_uses = 0, phi_uses = 0;

  if (ir->o != IR_ADD || irt_isguard(ir->t) || !irt_isinteger(ir->t) ||
      !irref_isk(ir->op2) || (int32_t)asm_kintptr(as, ir->op2) != 1 ||
      !asm_s390x_ref_feeds_bitop(as, ir->op1))
    return 0;

  for (use = IR(as->orignins-1); use > ir; use--) {
    if (use->op1 != ref && use->op2 != ref)
      continue;
    if (use->o == IR_PHI) {
      phi_uses++;
      continue;
    }
    if (use->o == IR_LE && irt_isguard(use->t)) {
      IRRef other = use->op1 == ref ? use->op2 : use->op1;
      if (irref_isk(other))
	return 0;
      le_uses++;
      continue;
    }
    return 0;
  }
  return le_uses == 1 && phi_uses == 1;
}

static void asm_s390x_addhome_log(ASMState *as, IRIns *ir)
{
  IRIns *lir, *rir;
  int add_uses, phi_uses, other_uses, guard_uses;
  int first_use_op, first_noncarry_use_op;
  int left_low32home, right_low32home, carry_candidate;

  if (!asm_s390x_addhome_log_enabled())
    return;
  if (irt_isguard(ir->t) || !(irt_isint(ir->t) || irt_isu32(ir->t)))
    return;

  lir = IR(ir->op1);
  rir = irref_isk(ir->op2) ? NULL : IR(ir->op2);
  asm_s390x_addhome_use_counts(as, ir, &add_uses, &phi_uses, &other_uses,
			       &guard_uses, &first_use_op,
			       &first_noncarry_use_op);
  left_low32home = asm_s390x_is_low32home_source_op(lir->o);
  right_low32home = rir ? asm_s390x_is_low32home_source_op(rir->o) : 0;
  carry_candidate = (left_low32home || right_low32home) &&
		    other_uses == 0 && guard_uses == 0;

  fprintf(stderr,
	  "S390X_ADDHOME curins=%d ir=%d type=%d leftref=%d leftop=%d leftint=%d leftu32=%d left_low32home=%d rightref=%d rightop=%d rightint=%d rightu32=%d rightisk=%d right_low32home=%d add_uses=%d phi_uses=%d other_uses=%d guard_uses=%d first_use_op=%d first_noncarry_use_op=%d carry_candidate=%d\n",
	  (int)(as->curins - REF_BIAS),
	  (int)((ir - as->ir) - REF_BIAS),
	  (int)irt_type(ir->t),
	  (int)(ir->op1 - REF_BIAS),
	  (int)lir->o,
	  (int)irt_isint(lir->t),
	  (int)irt_isu32(lir->t),
	  left_low32home,
	  irref_isk(ir->op2) ? -1 : (int)(ir->op2 - REF_BIAS),
	  rir ? (int)rir->o : -1,
	  rir ? (int)irt_isint(rir->t) : 0,
	  rir ? (int)irt_isu32(rir->t) : 0,
	  irref_isk(ir->op2),
	  right_low32home,
	  add_uses,
	  phi_uses,
	  other_uses,
	  guard_uses,
	  first_use_op,
	  first_noncarry_use_op,
	  carry_candidate);
}

static void asm_s390x_low32home_log(ASMState *as, const char *phase, IRIns *ir)
{
  IRRef ref = (IRRef)(ir - as->ir);
  IRIns *use;
  int family_uses = 0;
  int add_uses = 0, phi_uses = 0, store_uses = 0, guard_uses = 0;
  int call_uses = 0, other_uses = 0;
  int first_use_op = -1, first_hard_use_op = -1;
  const char *first_hard_kind = "none";

  if (!asm_s390x_low32home_log_enabled())
    return;
  if (!(asm_s390x_is_bitop_op(ir->o) ||
	(ir->o == IR_ADD && !irt_isguard(ir->t))))
    return;
  if (!(irt_isint(ir->t) || irt_isu32(ir->t)))
    return;

  for (use = IR(as->orignins-1); use > ir; use--) {
    if (use->op1 != ref && use->op2 != ref)
      continue;
    if (first_use_op == -1)
      first_use_op = (int)use->o;
    if (asm_s390x_is_low32home_family_use(use)) {
      family_uses++;
      if (use->o == IR_ADD && !irt_isguard(use->t))
	add_uses++;
      else if (use->o == IR_PHI)
	phi_uses++;
      continue;
    }
    if (first_hard_use_op == -1) {
      first_hard_use_op = (int)use->o;
      first_hard_kind = asm_s390x_low32home_hard_kind(use);
    }
    if (irt_isguard(use->t))
      guard_uses++;
    else if (use->o == IR_ASTORE || use->o == IR_HSTORE ||
	     use->o == IR_USTORE || use->o == IR_FSTORE ||
	     use->o == IR_XSTORE)
      store_uses++;
    else if (use->o == IR_CALLN || use->o == IR_CALLL ||
	     use->o == IR_CALLS || use->o == IR_CALLXS)
      call_uses++;
    else
      other_uses++;
  }

  fprintf(stderr,
	  "S390X_LOW32HOME phase=%s curins=%d ir=%d op=%d type=%d family_uses=%d add_uses=%d phi_uses=%d store_uses=%d guard_uses=%d call_uses=%d other_uses=%d first_use_op=%d first_hard_use_op=%d first_hard_kind=%s can_carry=%d\n",
	  phase,
	  (int)(as->curins - REF_BIAS),
	  (int)((ir - as->ir) - REF_BIAS),
	  (int)ir->o,
	  (int)irt_type(ir->t),
	  family_uses,
	  add_uses,
	  phi_uses,
	  store_uses,
	  guard_uses,
	  call_uses,
	  other_uses,
	  first_use_op,
	  first_hard_use_op,
	  first_hard_kind,
	  (store_uses | guard_uses | call_uses | other_uses) == 0);
}

static void asm_s390x_low32cmp_log(ASMState *as, const char *phase, IROp op,
				   IRRef lref, IRRef rref, IRIns *lir,
				   IRIns *rir, int cmp32u,
				   int imm16_signed)
{
  UNUSED(as);
  UNUSED(lref);
  UNUSED(rref);
  if (!asm_s390x_low32cmp_log_enabled())
    return;
  if (!(lir && asm_s390x_is_low32home_source_op(lir->o)) &&
      !(rir && asm_s390x_is_low32home_source_op(rir->o)))
    return;
  if (!asm_s390x_low32cmp_atexit_registered) {
    atexit(asm_s390x_low32cmp_dump_summary);
    asm_s390x_low32cmp_atexit_registered = 1;
  }
  asm_s390x_low32cmp_total++;
  asm_s390x_low32cmp_phase_counts[asm_s390x_low32cmp_phase_index(phase)]++;
  if (op < IR__MAX)
    asm_s390x_low32cmp_op_counts[op]++;
  asm_s390x_low32cmp_left_counts[asm_s390x_low32cmp_source_index(lir)]++;
  asm_s390x_low32cmp_right_counts[asm_s390x_low32cmp_source_index(rir)]++;
  asm_s390x_low32cmp_left_addkind_counts[asm_s390x_low32cmp_add_kind(as, lir)]++;
  asm_s390x_low32cmp_right_addkind_counts[asm_s390x_low32cmp_add_kind(as, rir)]++;
  asm_s390x_low32cmp_cmp32u_counts[cmp32u != 0]++;
  asm_s390x_low32cmp_imm16_counts[imm16_signed != 0]++;
}

static int asm_s390x_sload_log_enabled(void)
{
  return 0;
}

static int asm_s390x_sloadmap_log_enabled(void)
{
  return 0;
}

static int asm_s390x_forl_current_compare_fix_enabled(void)
{
  return 1;
}

static int asm_s390x_stack_restore_log_enabled(void)
{
  return 0;
}

static int asm_s390x_int_minmax_enabled(void)
{
  return 1;
}

static int asm_s390x_narrow_xstore_enabled(void)
{
  return 1;
}

static int asm_s390x_varg_bias_override(void)
{
  return -999;
}

static int asm_s390x_varg_slot_bias_override(void)
{
  return 0;
}

static int asm_s390x_varg_slot_bias_root_override(void)
{
  return -999;
}

static int asm_s390x_varg_slot_bias_loop_override(void)
{
  return -999;
}

static int asm_s390x_varg_dump_enabled(void)
{
  return 0;
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

static IRRef asm_s390x_guarded_ov_preserve_ref(ASMState *as, IRIns *ir)
{
  IRRef ref = (IRRef)(ir - as->ir);
  IRIns *use;

  for (use = IR(as->orignins-1); use > ir; use--) {
    if (use->o == IR_PHI && use->op2 == ref) {
      if (use->op1 == ir->op1 || use->op1 == ir->op2)
	return use->op1;
    }
  }

  return ir->op1;
}

static int asm_s390x_loop_phi_carry_in_dest(ASMState *as, IRIns *ir,
					    Reg dest, Reg left)
{
  IRRef ref = (IRRef)(ir - as->ir);
  IRIns *use;

  if (!as->loopref || as->curins <= as->loopref || irref_isk(ir->op1))
    return 0;
  if (!ra_hasreg(IR(ir->op1)->r) || IR(ir->op1)->r != left)
    return 0;

  for (use = IR(as->orignins-1); use > ir; use--)
    if (use->o == IR_PHI && use->op1 == ir->op1 && use->op2 == ref &&
	ra_hasreg(use->r) && use->r == dest)
      return 1;

  return 0;
}

static int asm_s390x_guarded_addsub_can_stay_low32(ASMState *as, IRIns *ir)
{
  IRRef ref = (IRRef)(ir - as->ir);
  IRIns *use;
  int family_uses = 0;

  for (use = IR(as->orignins-1); use > ir; use--) {
    if (use->op1 != ref && use->op2 != ref)
      continue;
    if (asm_s390x_is_low32home_family_use(use)) {
      family_uses++;
      continue;
    }
    return 0;
  }

  return family_uses != 0;
}

static void asm_s390x_guard_log(ASMState *as, const char *kind, IRIns *ir,
				int cc, int32_t ofs, int extra)
{
  if (!asm_s390x_guard_log_enabled())
    return;
  fprintf(stderr,
	  "S390X_GUARD kind=%s curins=%d ir=%d op=%d type=%d cc=%d ofs=%d extra=%d\n",
	  kind, (int)(as->curins - REF_BIAS),
	  ir ? (int)((ir - as->ir) - REF_BIAS) : -1,
	  ir ? (int)ir->o : -1,
	  ir ? (int)irt_type(ir->t) : -1,
	  cc, (int)ofs, extra);
}

/* Fuse the array base of colocated arrays and vararg pseudo-bases. */
static int32_t asm_fuseabase(ASMState *as, IRRef ref)
{
  IRIns *ir = IR(ref);
  if (ir->o == IR_TNEW && ir->op1 <= LJ_MAX_COLOSIZE && !neverfuse(as))
    return (int32_t)sizeof(GCtab);
  return 0;
}

static RegSet asm_s390x_dest_gprset(IRType1 t)
{
  if (irt_isp32(t) || irt_isaddr(t) || irt_isgcv(t)) {
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

static int lj_asm_s390x_aref_base_allgpr_enabled(void)
{
  return 1;
}

/* Keep RID_BASE available for explicit REF_BASE materialization, but do not
** hand it out as a generic temp/result register in ordinary lowering.
*/
#define RSET_GPR_NOB		(rset_exclude(RSET_GPR, RID_BASE))
#define RSET_GPR_CALL_NOB	((RSET_GPR & ~RSET_SCRATCH_GPR) & ~RID2RSET(RID_BASE))
#define RSET_FPR_CALL		(RSET_FPR & ~RSET_SCRATCH_FPR)
/* Reserve the psABI caller save area for traced helper calls. This mirrors
** the s390x FFI call-side requirement of 160 bytes.
*/
#define S390X_CALL_SPS_EXTRA	20

#define S390X_CALLARG_NONE	0
#define S390X_CALLARG_GPR	1
#define S390X_CALLARG_FPR	2
#define S390X_CALLARG_STACK_GPR	3
#define S390X_CALLARG_STACK_FPR	4

static void asm_s390x_call_preserve_log(ASMState *as, const char *kind,
					int slot, IRRef ref, Reg src,
					Reg target, Reg save)
{
  if (!asm_s390x_call_log_enabled())
    return;
  fprintf(stderr,
	  "S390X_CALL_PRESERVE kind=%s curins=%d slot=%d ref=%d"
	  " src=%d target=%d save=%d src_eq_target=%d\n",
	  kind, (int)(as->curins - REF_BIAS), slot, (int)(ref - REF_BIAS),
	  (int)src, target == RID_NONE ? -1 : (int)target, (int)save,
	  target != RID_NONE && src == target);
}

static void asm_s390x_call_arg_log(ASMState *as, const char *kind, int slot,
				   IRRef ref, Reg src, Reg target)
{
  if (!asm_s390x_call_log_enabled())
    return;
  fprintf(stderr,
	  "S390X_CALL_ARG kind=%s curins=%d slot=%d ref=%d src=%d target=%d"
	  " hasreg=%d src_eq_target=%d\n",
	  kind, (int)(as->curins - REF_BIAS), slot, (int)(ref - REF_BIAS),
	  (int)src, target == RID_NONE ? -1 : (int)target, ra_hasreg(src),
	  target != RID_NONE && src == target);
}

static void asm_gencall_preserve(ASMState *as, IRRef ref, Reg gpr,
				 Reg target, int slot, const char *kind)
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
  asm_s390x_call_preserve_log(as, kind, slot, ref, gpr, target, save);
  ra_rename(as, gpr, save);
}

static RegSet asm_gencall_nonarg_gpr(Reg gpr)
{
  RegSet allow = RSET_GPR_CALL_NOB & ~RID2RSET(gpr);
  if (allow == RSET_EMPTY)
    allow = rset_exclude(RSET_GPR_CALL_NOB, gpr);
  return allow;
}

static void asm_gencall_preserve_fpr(ASMState *as, IRRef ref, Reg fpr,
				     Reg target, int slot, const char *kind)
{
  Reg save;
  RegSet allow = RSET_FPR_CALL & ~RID2RSET(fpr);
  IRIns *ir = IR(ref);
  lj_assertA(!irref_isk(ref), "bad preserved call arg K%03d", REF_BIAS - ref);
  lj_assertA(ir->r == fpr, "call arg %04d not in reg %d", ref - REF_BIAS, fpr);
  as->cost[fpr] = REGCOST_REF_T(ref, irt_t(ir->t));
  if (rset_test(as->freeset, fpr)) {
    rset_clear(as->freeset, fpr);
    ra_noweak(as, fpr);
  }
  lj_assertA(allow != RSET_EMPTY, "no preserved fpr for call arg %04d",
	     ref - REF_BIAS);
  save = ra_pick(as, allow);
  asm_s390x_call_preserve_log(as, kind, slot, ref, fpr, target, save);
  ra_rename(as, fpr, save);
}

static RegSet asm_gencall_nonarg_fpr(Reg fpr)
{
  RegSet allow = RSET_FPR_CALL & ~RID2RSET(fpr);
  if (allow == RSET_EMPTY)
    allow = rset_exclude(RSET_FPR_CALL, fpr);
  return allow;
}

static Reg asm_gencall_stack_alloc_gpr(ASMState *as, IRRef ref, RegSet allow)
{
  if (!irref_isk(ref)) {
    Reg src = IR(ref)->r;
    if (ra_hasreg(src) && src >= REGARG_FIRSTGPR && src <= REGARG_LASTGPR)
      asm_gencall_preserve(as, ref, src, RID_NONE, -1, "stack_gpr");
    return ra_alloc1(as, ref, allow);
  }
  return ra_allock(as, asm_kintptr(as, ref), allow);
}

static void asm_gencall_stack_gpr(ASMState *as, IRRef ref, int32_t ofs)
{
  IRIns *ir = IR(ref);
  RegSet allow = RSET_GPR_CALL_NOB;
  Reg src = asm_gencall_stack_alloc_gpr(as, ref, allow);

  if (irt_isint(ir->t) || irt_isu32(ir->t)) {
    Reg tmp;
    allow = rset_exclude(allow, src);
    tmp = ra_scratch(as, allow);
    emit_store64ofs(as, tmp, RID_SP, ofs);
    emit_u32(as, S390X_INS_RXE(irt_isint(ir->t) ? S390XI_LGFR : S390XI_LLGFR,
			       tmp, tmp));
    if (tmp != src)
      emit_movrr(as, ir, tmp, src);
  } else {
    emit_store64ofs(as, src, RID_SP, ofs);
  }
}

static void asm_gencall_stack_fpr(ASMState *as, IRRef ref, int32_t ofs)
{
  IRIns *ir = IR(ref);
  Reg src = ra_alloc1(as, ref, RSET_FPR_CALL);
  if (irt_isfloat(ir->t))
    emit_u48_pad8(as, S390X_INS_RXY(S390XI_STEY, src, 0, RID_SP,
				    ofs + (LJ_BE ? 4 : 0)));
  else
    emit_u48_pad8(as, S390X_INS_RXY(S390XI_STDY, src, 0, RID_SP, ofs));
}

static void asm_s390x_call_log(ASMState *as, const char *phase, uint32_t nargs,
			       IRRef *args)
{
  Reg gpr, fpr;
  if (!asm_s390x_call_log_enabled())
    return;
  fprintf(stderr, "S390X_CALL phase=%s curins=%d nargs=%u", phase,
	  (int)(as->curins - REF_BIAS), (unsigned int)nargs);
  for (gpr = REGARG_FIRSTGPR; gpr <= REGARG_LASTGPR; gpr++) {
    IRRef ref = regcost_ref(as->cost[gpr]);
    fprintf(stderr, " r%d=ref%d/free%d", (int)gpr, (int)(ref - REF_BIAS),
	    rset_test(as->freeset, gpr) ? 1 : 0);
  }
  for (fpr = REGARG_FIRSTFPR; fpr <= REGARG_LASTFPR; fpr += 2) {
    IRRef ref = regcost_ref(as->cost[fpr]);
    fprintf(stderr, " f%d=ref%d/free%d", (int)(fpr - RID_F0), (int)(ref - REF_BIAS),
	    rset_test(as->freeset, fpr) ? 1 : 0);
  }
  if (args) {
    uint32_t n;
    for (n = 0; n < nargs; n++)
      fprintf(stderr, " arg%u=%d", (unsigned int)n, (int)(args[n] - REF_BIAS));
  }
  fprintf(stderr, "\n");
}

static void asm_gencall_dup_fanout(ASMState *as, IRRef *args, uint32_t nargs,
				   uint32_t n, uint8_t *loc_kind,
				   uint8_t *loc_reg, int32_t *loc_ofs,
				   uint8_t *dup_first)
{
  IRRef ref = args[n];
  IRIns *ir = IR(ref);
  uint32_t m;
  Reg src = (Reg)loc_reg[n];
  lj_assertA(loc_kind[n] == S390X_CALLARG_GPR,
	     "bad duplicate source call arg %u", (unsigned)n);
  /* Backward emission: these copies/stores execute after the source arg setup. */
  for (m = n + 1; m < nargs; m++) {
    if (dup_first[m] != n)
      continue;
    switch (loc_kind[m]) {
    case S390X_CALLARG_GPR:
      emit_movrr(as, ir, (Reg)loc_reg[m], src);
      break;
    case S390X_CALLARG_STACK_GPR:
      emit_store64ofs(as, src, RID_SP, loc_ofs[m]);
      break;
    default:
      break;
    }
  }
}

static void asm_guardcc(ASMState *as, int cc);
static void asm_tvstore64(ASMState *as, Reg base, int32_t ofs, IRRef ref);
static void asm_tvstore64x(ASMState *as, Reg base, int32_t ofs, IRRef ref,
			   RegSet forbid);

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
  if (ir->op2 & IRSLOAD_KEYINDEX) {
    base = RID_BASE;
  } else {
    base = ra_scratch(as, allow);
    rset_clear(allow, base);
  }

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
  if (base != RID_BASE)
    emit_getgl(as, base, jit_base);
  return 1;
}

static int asm_gencall_const_gpr(ASMState *as, Reg gpr, IRRef ref)
{
  IRIns *ir = IR(ref);

  if (!irref_isk(ref) || ir->o == IR_KPRI || irt_isfp(ir->t))
    return 0;
  ra_modified(as, gpr);
  emit_loadu64(as, gpr, (uint64_t)asm_kintptr(as, ref));
  return 1;
}

/* -- Shared NYI helpers -------------------------------------------------- */

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
    emit_u16_pad4(as, 0x0707u);  /* Keep exitno/traceno at r14+2/r14+6. */
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
  int32_t mark = (int32_t)(as->curins - REF_BIAS);
  lj_asm_s390x_guard_log(as, cc, target, p, 0);
  if (LJ_UNLIKELY(p == as->invmcp)) {
    as->loopinv = 1;
    lj_asm_s390x_guard_log(as, cc, target, p, 1);
    emit_u32_at(p, S390X_INS_BRC(CC_AL,
				  (int32_t)(((char *)target - (char *)p) >> 1)));
    /* Code is emitted backwards: place the branch first so the mark write
    ** executes before a taken guard exits through the shared stub. */
    emit_condbranch(as, (S390XCC)asm_guardcc_invert(cc), p);
    if (asm_s390x_guardmark_enabled()) {
      emit_store32ofs(as, RID_TMP, RID_DISPATCH,
		      emit_gl_ofs(tmptv2) + (LJ_BE ? 4 : 0));
      emit_loadi(as, RID_TMP, mark);
    }
    return;
  }
  if (asm_s390x_guardmark_taken_enabled()) {
    MCode *cont = as->mcp;
    /* Debug-only exact mark path: only taken guards write the mark. */
    emit_condbranch(as, CC_AL, target);
    emit_store32ofs(as, RID_TMP, RID_DISPATCH,
		    emit_gl_ofs(tmptv2) + (LJ_BE ? 4 : 0));
    emit_loadi(as, RID_TMP, mark);
    emit_condbranch(as, (S390XCC)asm_guardcc_invert(cc), cont);
    return;
  }
  /* Code is emitted backwards: place the branch first so the mark write
  ** executes before a taken guard exits through the shared stub. */
  emit_condbranch(as, (S390XCC)cc, target);
  if (asm_s390x_guardmark_enabled()) {
    emit_store32ofs(as, RID_TMP, RID_DISPATCH,
		    emit_gl_ofs(tmptv2) + (LJ_BE ? 4 : 0));
    emit_loadi(as, RID_TMP, mark);
  }
}

static int asm_s390x_loop_cgrj(ASMState *as, int cc, Reg left, Reg right)
{
  if (as->loopinv == 1 && as->mcp == as->mctop - 8) {
    MCode *br = as->mcp;
    as->mcp = br + 4;  /* Preserve the separate exit BRC. */
    emit_u48_pad8(as, S390X_INS_RIE_B(S390XI_CGRJ, left, right,
				       asm_guardcc_invert(cc), 0));
    as->loopinv = 2;
    return 1;
  }
  return 0;
}

static int asm_s390x_loop_crj(ASMState *as, int cc, Reg left, Reg right)
{
  if (as->loopinv == 1 && as->mcp == as->mctop - 8) {
    MCode *br = as->mcp;
    as->mcp = br + 4;  /* Preserve the separate exit BRC. */
    emit_u48_pad8(as, S390X_INS_RIE_B(S390XI_CRJ, left, right,
				       asm_guardcc_invert(cc), 0));
    as->loopinv = 2;
    return 1;
  }
  return 0;
}

static int asm_s390x_guard_cij(ASMState *as, int cc, Reg left, int32_t k,
			       int is64)
{
  MCode *br = as->mcp;
  uint32_t ins = *br;
  int16_t olddisp;
  MCode *oldtarget, *newp;
  ptrdiff_t delta;
  if (!checki8(k))
    return 0;
  if ((ins & 0xff0f0000u) != 0xa7040000u ||
      (int)((ins >> 20) & 15u) != (cc & 15))
    return 0;
  olddisp = (int16_t)(ins & 0xffffu);
  oldtarget = (MCode *)((char *)br + ((int32_t)olddisp << 1));
  newp = br - 1;
  delta = (char *)oldtarget - (char *)newp;
  if ((delta & 1) != 0 || !checki16((int32_t)(delta >> 1)))
    return 0;
  as->mcp = br + 1;  /* Overwrite the separate BRC. */
  emit_u48_pad8(as, S390X_INS_RIE_C(is64 ? S390XI_CGIJ : S390XI_CIJ,
				    left, cc & 15, k, (int32_t)(delta >> 1)));
  return 1;
}

static int asm_s390x_guard_cij_small(ASMState *as, int cc, Reg left,
				     int32_t k, int is64)
{
  /* CIJ/CGIJ fuses compare-immediate + guard branch into one RIE insn. Keep
  ** zero-heavy dispatch traces on the older BRC form: those link more
  ** consistently today. Non-zero immediates avoid a separate CFI/CGFI and are
  ** covered by side-exit equality and signed-compare tests, so no env gate is
  ** needed here. */
  return k != 0 && as->loopinv == 0 &&
	 asm_s390x_guard_cij(as, cc, left, k, is64);
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
  MCode *p = as->mctop - (as->loopref ? 4 : (lnk ? 12 : 28));
  if (as->loopref) {
    as->invmcp = as->mcp = p;
  } else {
    UNUSED(lnk);
    as->mcp = p;
    as->invmcp = NULL;
  }
  as->mctail = p;
  memset(p, 0, (size_t)(as->mctop - p));
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
    emit_u32_at(mcp, S390X_INS_RI(S390XI_AGHI, RID_SP, spadj));
    mcp += 4;
  }
  if (!lnk) {
    emit_u48_at(mcp, S390X_INS_RXY(S390XI_LG, RID_TMP, 0, RID_DISPATCH,
				   emit_gl_ofs(cur_L)));
    mcp += 6;
    emit_u48_at(mcp, S390X_INS_RXY(S390XI_STG, RID_TMP, 0, RID_SP,
				   S390X_OFS_SAVE_L));
    mcp += 6;
  }

  delta = (char *)target - (char *)mcp;
  lj_assertA((delta & 1) == 0, "unaligned tail branch target");
  if (checki16((int32_t)(delta >> 1))) {
    emit_u32_at(mcp, S390X_INS_BRC(CC_AL, (int32_t)(delta >> 1)));
    mcp += 4;
  } else {
    lj_assertA(checki32((int64_t)(delta >> 1)),
	       "s390x tail branch target out of range");
    emit_u48_at(mcp, S390X_INS_BRCL(CC_AL, (int32_t)(delta >> 1)));
    mcp += 6;
  }

  while (as->mctop > mcp) {
    emit_u16_at(mcp, 0x0707u);  /* nopr %r7 */
    mcp += 2;
  }
}

static void asm_loop_fixup(ASMState *as)
{
  MCode *p = as->mctop;
  MCode *target = as->mcp;
  ptrdiff_t delta;
  if (as->loopinv == 4) {  /* Loop-back branch was proven unreachable. */
    return;
  } else if (as->loopinv == 2) {
    MCode *br = p - 10;
    delta = (char *)target - (char *)br;
    lj_assertA((delta & 1) == 0, "unaligned cgrj loop branch target");
    lj_assertA(checki16((int32_t)(delta >> 1)),
	       "s390x cgrj loop branch target out of range");
    br[2] = (uint8_t)((uint16_t)(delta >> 1) >> 8);
    br[3] = (uint8_t)(uint16_t)(delta >> 1);
  } else if (as->loopinv) {  /* Guard inversion already consumed the tail slot. */
    MCode *br = p - 8;
    delta = (char *)target - (char *)br;
    lj_assertA((delta & 1) == 0, "unaligned inverted loop branch target");
    lj_assertA(checki16((int32_t)(delta >> 1)),
	       "s390x inverted loop branch target out of range");
    br[2] = (uint8_t)((uint16_t)(delta >> 1) >> 8);
    br[3] = (uint8_t)(uint16_t)(delta >> 1);
  } else {
    MCode *br = p - 4;
    delta = (char *)target - (char *)br;
    lj_assertA((delta & 1) == 0, "unaligned loop branch target");
    lj_assertA(checki16((int32_t)(delta >> 1)),
	       "s390x loop branch target out of range");
    emit_u32_at(br, S390X_INS_BRC(CC_AL, (int32_t)(delta >> 1)));
  }
}

static int asm_s390x_fill_loop_crj_never_taken(ASMState *as, uint16_t ins)
{
  MCode *p;
  if (as->loopinv != 2)
    return 0;
  p = as->mctop - 10;
  p[0] = (uint8_t)(ins >> 8);
  p[1] = (uint8_t)ins;
  emit_u16_at(p + 2, 0x0707u);
  emit_u16_at(p + 4, 0x0707u);
  as->loopinv = 4;
  return 1;
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

    if (asm_s390x_stack_restore_log_enabled()) {
      fprintf(stderr,
	      "S390X_STACK_RESTORE trace=%u snapno=%u slot=%u ref=%u op=%u type=%u key=%u norestore=%u ofs=%d\n",
	      (unsigned int)as->T->traceno, (unsigned int)as->snapno,
	      (unsigned int)s, (unsigned int)(ref - REF_BIAS),
	      (unsigned int)ir->o, (unsigned int)irt_type(ir->t),
	      (unsigned int)((sn & SNAP_KEYINDEX) != 0),
	      (unsigned int)((sn & SNAP_NORESTORE) != 0),
	      (int)ofs);
    }

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

    if (irt_isnum(ir->t)) {
      src = ra_alloc1(as, ref, RSET_FPR);
      emit_u48_pad8(as, S390X_INS_RXY(S390XI_STDY, src, 0, RID_BASE, ofs));
    } else {
      asm_tvstore64(as, RID_BASE, ofs, ref);
    }
    checkmclim(as);
  }
}

/* -- Calls and shared helpers ------------------------------------------- */

static int asm_gencall_str_equal_256(ASMState *as, const CCallInfo *ci,
				     IRRef *args)
{
  RegSet workset = RSET_GPR_NOB & ~RID2RSET(RID_RET);
  Reg wp1, wp2, wlen, p1, p2, len;
  MCode *l_done, *l_template;

  if (ci != &lj_ir_callinfo[IRCALL_lj_str_equal_256])
    return 0;

  wp1 = ra_scratch(as, workset);
  workset = rset_exclude(workset, wp1);
  wp2 = ra_scratch(as, workset);
  workset = rset_exclude(workset, wp2);
  wlen = ra_scratch(as, workset);
  workset = rset_exclude(workset, wlen);

  p1 = ra_alloc1_nobase(as, args[0], workset, -290);
  workset = rset_exclude(workset, p1);
  p2 = ra_alloc1_nobase(as, args[1], workset, -291);
  workset = rset_exclude(workset, p2);
  len = ra_alloc1_nobase(as, args[2], workset, -292);

  l_done = emit_label(as);
  emit_u48_pad8(as, S390X_INS_SS(S390XI_CLC, 0, wp1, 0, wp2, 0));
  l_template = as->mcp;
  emit_jmp(as, l_done);
  emit_loadi(as, RID_RET, 0);
  emit_condbranch(as, CC_EQ, l_done);
  emit_exrl(as, wlen, l_template);
  emit_u32(as, S390X_INS_RI(S390XI_AGHI, wlen, -1));
  emit_condbranch(as, CC_EQ, l_done);
  emit_u32(as, S390X_INS_RI(S390XI_CGHI, wlen, 0));
  emit_loadi(as, RID_RET, 1);
  emit_movrr(as, IR(args[2]), wlen, len);
  emit_movrr(as, IR(args[1]), wp2, p2);
  emit_movrr(as, IR(args[0]), wp1, p1);
  return 1;
}

static void asm_gencall(ASMState *as, const CCallInfo *ci, IRRef *args)
{
  uint32_t n, nargs = CCI_XNARGS(ci);
  int32_t spofs = S390X_CALL_SPS_EXTRA * 8;
  Reg gpr = REGARG_FIRSTGPR, fpr = REGARG_FIRSTFPR;
  uint8_t kskip[CCI_NARGS_MAX*2];
  uint8_t loc_kind[CCI_NARGS_MAX*2];
  uint8_t loc_reg[CCI_NARGS_MAX*2];
  uint8_t dup_first[CCI_NARGS_MAX*2];
  int32_t loc_ofs[CCI_NARGS_MAX*2];
  int direct_call_arg = asm_s390x_direct_call_arg_enabled();
  if (asm_gencall_str_equal_256(as, ci, args))
    return;
  for (n = 0; n < CCI_NARGS_MAX*2; n++) {
    kskip[n] = 0;
    loc_kind[n] = S390X_CALLARG_NONE;
    loc_reg[n] = RID_NONE;
    dup_first[n] = 255;
    loc_ofs[n] = 0;
  }
  asm_s390x_call_log(as, "enter", nargs, args);
  if (ci->func)
    emit_call(as, RID_R14, (void *)ci->func);
  for (gpr = REGARG_FIRSTGPR; gpr <= REGARG_LASTGPR; gpr++) {
    IRRef ref = regcost_ref(as->cost[gpr]);
    if (!ra_iskref(ref) && ref <= as->T->nins && IR(ref)->r == gpr)
      ra_sethint(IR(ref)->r, gpr);
    as->cost[gpr] = REGCOST(~0u, ASMREF_L);
  }
  for (fpr = REGARG_FIRSTFPR; fpr <= REGARG_LASTFPR; fpr += 2) {
    IRRef ref = regcost_ref(as->cost[fpr]);
    if (!ra_iskref(ref) && ref <= as->T->nins && IR(ref)->r == fpr)
      ra_sethint(IR(ref)->r, fpr);
    as->cost[fpr] = REGCOST(~0u, ASMREF_L);
  }

  gpr = REGARG_FIRSTGPR;
  fpr = REGARG_FIRSTFPR;
  spofs = S390X_CALL_SPS_EXTRA * 8;
  for (n = 0; n < nargs; n++) {
    IRRef ref = args[n];
    if (!ref)
      continue;
    if (irt_isfp(IR(ref)->t)) {
      if (fpr <= REGARG_LASTFPR) {
	loc_kind[n] = S390X_CALLARG_FPR;
	loc_reg[n] = (uint8_t)fpr;
	fpr += 2;
      } else {
	loc_kind[n] = S390X_CALLARG_STACK_FPR;
	loc_ofs[n] = spofs;
	spofs += 8;
      }
    } else if (gpr <= REGARG_LASTGPR) {
      kskip[n] = (uint8_t)asm_gencall_const_gpr(as, gpr, ref);
      loc_kind[n] = S390X_CALLARG_GPR;
      loc_reg[n] = (uint8_t)gpr;
      gpr++;
    } else {
      loc_kind[n] = S390X_CALLARG_STACK_GPR;
      loc_ofs[n] = spofs;
      spofs += 8;
    }
  }

  for (n = 0; n < nargs; n++) {
    uint32_t m;
    IRRef ref = args[n];
    if (!ref || irref_isk(ref) ||
	loc_kind[n] != S390X_CALLARG_GPR)
      continue;
    for (m = n + 1; m < nargs; m++) {
      if (args[m] == ref &&
	  (loc_kind[m] == S390X_CALLARG_GPR ||
	   loc_kind[m] == S390X_CALLARG_STACK_GPR) &&
	  dup_first[m] == 255)
	dup_first[m] = (uint8_t)n;
    }
  }

  gpr = REGARG_FIRSTGPR;
  fpr = REGARG_FIRSTFPR;
  spofs = S390X_CALL_SPS_EXTRA * 8;
  for (n = 0; n < nargs; n++) {
    IRRef ref = args[n];
    if (!ref)
      continue;
    if (dup_first[n] != 255) {
      switch (loc_kind[n]) {
      case S390X_CALLARG_GPR: gpr++; break;
      case S390X_CALLARG_FPR: fpr += 2; break;
      case S390X_CALLARG_STACK_GPR:
      case S390X_CALLARG_STACK_FPR: spofs += 8; break;
      default: break;
      }
      continue;
    }
    if (irt_isfp(IR(ref)->t)) {
      if (fpr <= REGARG_LASTFPR) {
	if (!irref_isk(ref)) {
	  Reg src = IR(ref)->r;
	  asm_s390x_call_arg_log(as, "fpr", (int)n, ref, src, fpr);
	  if (ra_hasreg(src) &&
	      src >= REGARG_FIRSTFPR && src <= REGARG_LASTFPR && (src & 1) == 0)
	    asm_gencall_preserve_fpr(as, ref, src, fpr, (int)n, "fpr");
	  ra_alloc1(as, ref, asm_gencall_nonarg_fpr(fpr));
	}
	lj_assertA(rset_test(as->freeset, fpr), "reg %d not free", fpr);
	ra_leftov(as, fpr, ref);
	fpr += 2;
      } else {
	asm_gencall_stack_fpr(as, ref, spofs);
	spofs += 8;
      }
      continue;
    }
    if (gpr <= REGARG_LASTGPR) {
      if (kskip[n])
	goto nextgpr;
      if (asm_gencall_sload(as, gpr, ref))
	goto nextgpr;
      if (!irref_isk(ref)) {
	Reg src = IR(ref)->r;
	asm_s390x_call_arg_log(as, "gpr", (int)n, ref, src, gpr);
	if (!direct_call_arg || ra_hasreg(src)) {
	  if (ra_hasreg(src) &&
	      src >= REGARG_FIRSTGPR && src <= REGARG_LASTGPR)
	    asm_gencall_preserve(as, ref, src, gpr, (int)n, "gpr");
	  ra_alloc1(as, ref, asm_gencall_nonarg_gpr(gpr));
	}
      }
      lj_assertA(rset_test(as->freeset, gpr), "reg %d not free", gpr);
      asm_gencall_dup_fanout(as, args, nargs, n, loc_kind, loc_reg,
			     loc_ofs, dup_first);
      if (irt_isint(IR(ref)->t) || irt_isu32(IR(ref)->t))
	emit_u32(as, S390X_INS_RXE(irt_isint(IR(ref)->t) ? S390XI_LGFR :
				   S390XI_LLGFR, gpr, gpr));
      ra_leftov(as, gpr, ref);
    nextgpr:
      gpr++;
    } else {
      asm_gencall_stack_gpr(as, ref, spofs);
      spofs += 8;
    }
  }
  asm_s390x_call_log(as, "assigned", nargs, args);
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
      if (ci->flags & CCI_CASTU64) {
	Reg dest = ra_dest(as, ir, RSET_FPR);
	emit_movrr(as, ir, dest, RID_RET);
      } else {
	ra_destreg(as, ir, RID_FPRET);
      }
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

static void asm_tvstore64x(ASMState *as, Reg base, int32_t ofs, IRRef ref,
			   RegSet forbid)
{
  RegSet allow = rset_exclude(RSET_GPR, base) & ~forbid;
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
    if (irt_isinteger(ir->t)) {
      emit_loadu64(as, type, (uint64_t)(uint32_t)LJ_TISNUM << 47);
      emit_u32(as, S390X_INS_RXE(S390XI_LLGFR, tmp, src));
    } else {
      Reg mask = RID_NONE;
      if (irt_isgcv(ir->t)) {
        allow = rset_exclude(allow, type);
        mask = ra_scratch(as, allow);
        emit_u32(as, S390X_INS_RXE(S390XI_NGR, tmp, mask));
        emit_loadu64(as, mask, LJ_GCVMASK);
      }
      if (irt_isgcv(ir->t)) {
        allow = rset_exclude(allow, mask);
      }
      emit_loadu64(as, type, (uint64_t)irt_toitype(ir->t) << 47);
      if (tmp != src)
        emit_movrr(as, ir, tmp, src);
    }
    if (!irt_isinteger(ir->t) && !irt_isgcv(ir->t) && tmp != src)
      emit_movrr(as, ir, tmp, src);
  }
}

static void asm_tvstore64(ASMState *as, Reg base, int32_t ofs, IRRef ref)
{
  asm_tvstore64x(as, base, ofs, ref, RSET_EMPTY);
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
  Reg tmp = ra_scratch(as, rset_exclude(RSET_GPR_NOB, sb));
  IRIns irgc;
  irgc.ot = IRT(0, IRT_PGC);  /* GC type. */
  emit_storeofs(as, &irgc, tmp, sb, (int32_t)offsetof(SBuf, L));
  emit_u32(as, S390X_INS_RXE(S390XI_OGR, tmp, RID_TMP));
  emit_getgl(as, RID_TMP, cur_L);
  emit_u32(as, S390X_INS_RXE(S390XI_NGR, tmp, RID_TMP));
  emit_loadi(as, RID_TMP, SBUF_MASK_FLAG);
  emit_loadofs(as, &irgc, tmp, sb, (int32_t)offsetof(SBuf, L));
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
    MCode *p = as->mcp - 6;
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

static int asm_s390x_centered_mod17_abs_lt_guard_elided(ASMState *as,
							IRIns *ir, IROp op,
							IRRef lref, IRRef rref);
static int asm_s390x_centered_mod17_abs_conv(ASMState *as, IRIns *ir,
					     IRIns *subov);
static int asm_s390x_mod_operand_nonnegative(ASMState *as, IRRef ref);

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

static int asm_s390x_int_result_normalized(IRIns *ir);

static int asm_s390x_kint_is(ASMState *as, IRRef ref, int32_t k)
{
  UNUSED(as);
  return irref_isk(ref) && IR(ref)->i == k;
}

static int asm_s390x_is_str_len_ref(ASMState *as, IRRef ref)
{
  UNUSED(as);
  if (irref_isk(ref))
    return 0;
  return IR(ref)->o == IR_FLOAD && IR(ref)->op2 == IRFL_STR_LEN;
}

static int asm_s390x_saw_le_bound(ASMState *as, IRIns *ir, IRRef ref,
				  int32_t limit)
{
  IRIns *scan;
  UNUSED(as);
  for (scan = ir-1; scan > IR(REF_BASE); scan--)
    if (scan->o == IR_LE && scan->op1 == ref &&
	irref_isk(scan->op2) && IR(scan->op2)->i <= limit)
      return 1;
  return 0;
}

static int asm_s390x_saw_le_ref_to_bounded_stop(ASMState *as, IRIns *ir,
						IRRef ref)
{
  IRIns *scan;
  for (scan = ir-1; scan > IR(REF_BASE); scan--) {
    if (scan->o == IR_LE && scan->op1 == ref && !irref_isk(scan->op2) &&
	asm_s390x_saw_le_bound(as, ir, scan->op2, INT32_MAX-1))
      return 1;
  }
  return 0;
}

static int asm_s390x_redundant_gt_zero_add1(ASMState *as, IRIns *ir,
					    IROp op, IRRef lref, IRRef rref)
{
  IRIns *lir, *scan;
  IRRef baseref;
  int saw_ule_lref = 0, saw_gt_base = 0;

  if (op != IR_GT || !asm_s390x_kint_is(as, rref, 0) || irref_isk(lref))
    return 0;
  lir = IR(lref);
  if (lir->o != IR_ADD)
    return 0;
  if (asm_s390x_kint_is(as, lir->op2, 1) && !irref_isk(lir->op1)) {
    baseref = lir->op1;
  } else if (asm_s390x_kint_is(as, lir->op1, 1) && !irref_isk(lir->op2)) {
    baseref = lir->op2;
  } else {
    return 0;
  }

  for (scan = ir-1; scan > IR(REF_BASE); scan--) {
    if (scan->o == IR_ULE && scan->op1 == lref &&
	asm_s390x_is_str_len_ref(as, scan->op2))
      saw_ule_lref = 1;
    else if (scan->o == IR_GT && scan->op1 == baseref &&
	     asm_s390x_kint_is(as, scan->op2, 0))
      saw_gt_base = 1;
    if (saw_ule_lref && saw_gt_base)
      return 1;
  }
  if (saw_gt_base && asm_s390x_saw_le_ref_to_bounded_stop(as, ir, lref))
	  return 1;
  return 0;
}

static int asm_s390x_open_upvalue_delta_ref(ASMState *as, IRRef lref)
{
  IRIns *sub, *uref;
  UNUSED(as);
  if (irref_isk(lref))
    return 0;
  sub = IR(lref);
  if (sub->o != IR_SUB || sub->op2 != REF_BASE ||
      irt_type(sub->t) != IRT_PGC)
    return 0;
  if (irref_isk(sub->op1))
    return 0;
  uref = IR(sub->op1);
  return uref->o == IR_UREFO && irt_isguard(uref->t);
}

static int asm_s390x_preloop_urefo_ugt(ASMState *as, IRIns *ir,
				       IROp op, IRRef lref, IRRef rref,
				       int cc)
{
  IRRef ref = (IRRef)(ir - as->ir);
  IRRef gref, first = 0;
  IRRef limit;
  intptr_t k, maxk;
  Reg left, right;

  if (!as->loopref || ref >= as->loopref || op != IR_UGT ||
      !(cc & CC_UNSIGNED) || irt_type(ir->t) != IRT_PGC ||
      !irref_isk(rref) || !asm_s390x_open_upvalue_delta_ref(as, lref))
    return 0;

  limit = (as->T->nsnap > 1 && as->T->snap[1].ref < as->loopref) ?
	  as->T->snap[1].ref : as->loopref;
  if (ref >= limit)
    return 0;

  k = asm_kintptr(as, rref);
  maxk = k;
  for (gref = REF_FIRST; gref < limit; gref++) {
    IRIns *gir = IR(gref);
    if (gref != ref && ir_sideeff(gir) && !irt_isguard(gir->t))
      return 0;
    if (gir->o != IR_UGT || !irt_isguard(gir->t) || gir->op1 != lref ||
	!irref_isk(gir->op2) || irt_type(gir->t) != IRT_PGC)
      continue;
    if (first == 0)
      first = gref;
    if (asm_kintptr(as, gir->op2) > maxk)
      maxk = asm_kintptr(as, gir->op2);
  }
  if (first == 0)
    return 0;
  if (ref != first)
    return 1;

  left = ra_alloc1_nobase(as, lref, RSET_GPR_NOB, -201);
  right = ra_allock(as, maxk, rset_exclude(RSET_GPR_NOB, left));
  asm_s390x_low32cmp_log(as, "urefo_ugt", op, lref, rref, IR(lref), NULL, 0, 0);
  asm_s390x_ir_log_intcomp(as, ir, op, lref, rref, cc, left, right, 0);
  asm_guardcc(as, cc & 15);
  emit_u32(as, S390X_INS_RXE(S390XI_CLGR, left, right));
  return 1;
}

static int asm_s390x_fori_u8stop_ref(ASMState *as, IRRef lref, IRRef rref,
				     uint8_t *stop)
{
  IRRef ref = lref;
  IRIns *ir, *rir, *base;
  int32_t ofs = 0;
  uint8_t enc, k;
  while (!irref_isk(ref)) {
    ir = IR(ref);
    if (ir->o != IR_ADD || !irt_isinteger(ir->t) || !irref_isk(ir->op2))
      break;
    if (IR(ir->op2)->i < 0 || IR(ir->op2)->i > 255 - ofs)
      return 0;
    ofs += IR(ir->op2)->i;
    ref = ir->op1;
  }
  if (irref_isk(ref))
    return 0;
  base = IR(ref);
  if (base->o != IR_SLOAD || !irt_isint(base->t))
    return 0;
  enc = IRSLOAD_FORI_U8HISTOP(base->op2);
  if (enc == 0)
    return 0;
  k = (uint8_t)(128 + enc);
  if (irref_isk(rref)) {
    if (IR(rref)->o != IR_KINT || IR(rref)->i != (int32_t)k)
      return 0;
  } else {
    rir = IR(rref);
    if (rir->o != IR_SLOAD || !irt_isint(rir->t) ||
	(rir->op2 & IRSLOAD_INHERIT) == 0 || rir->op1 != base->op1 + 1)
      return 0;
  }
  UNUSED(as);
  *stop = k;
  return 1;
}

static int asm_s390x_fori_u8histop_base(ASMState *as, IRRef ref,
					IRRef *baseref, int32_t *ofs,
					uint8_t *stop)
{
  IRRef cur = ref;
  IRIns *ir;
  int32_t addofs = 0;
  uint8_t enc;
  while (!irref_isk(cur)) {
    ir = IR(cur);
    if (ir->o != IR_ADD || !irt_isinteger(ir->t) || !irref_isk(ir->op2))
      break;
    if (IR(ir->op2)->i < 0 || IR(ir->op2)->i > 255 - addofs)
      return 0;
    addofs += IR(ir->op2)->i;
    cur = ir->op1;
  }
  if (irref_isk(cur))
    return 0;
  ir = IR(cur);
  if (ir->o != IR_SLOAD || !irt_isint(ir->t))
    return 0;
  enc = IRSLOAD_FORI_U8HISTOP(ir->op2);
  if (enc == 0)
    return 0;
  UNUSED(as);
  *baseref = cur;
  *ofs = addofs;
  *stop = (uint8_t)(128 + enc);
  return 1;
}

static int asm_s390x_ref_guarded_u8(ASMState *as, IRRef ref)
{
  IRRef baseref, guardbase;
  int32_t ofs, guardofs;
  uint8_t stop, guardstop;
  IRRef gref;

  if (!as->loopref || !asm_s390x_fori_u8histop_base(as, ref, &baseref, &ofs,
						    &stop))
    return 0;
  for (gref = as->loopref + 1; gref < as->T->nins; gref++) {
    IRIns *guard = IR(gref);
    if (guard->o != IR_LE || !irt_isguard(guard->t) ||
	!irt_isinteger(guard->t))
      continue;
    if (!asm_s390x_fori_u8stop_ref(as, guard->op1, guard->op2, &guardstop))
      continue;
    if (!asm_s390x_fori_u8histop_base(as, guard->op1, &guardbase,
				      &guardofs, &guardstop))
      continue;
    if (guardbase == baseref && guardofs >= ofs && guardstop == stop)
      return 1;
  }
  return 0;
}

static void asm_intcomp(ASMState *as, IRIns *ir)
{
  IROp op = ir->o;
  IRRef lref = ir->op1, rref = ir->op2;
  Reg left, right, cmp_left, cmp_right;
  IRIns *lir, *rir = NULL;
  int cc;
  int cmp32u;
  int cmp32s;
  int cmp32s_left_norm = 0, cmp32s_right_norm = 0;
  int imm16_signed;
  lj_assertA(irt_isint(ir->t) || irt_isu32(ir->t) || irt_isaddr(ir->t) ||
	     irt_isu8(ir->t) || irt_isp32(ir->t),
	     "bad comparison data type %d", irt_type(ir->t));
  if (irref_isk(lref) && !irref_isk(rref)) {
    IRRef tmp = lref; lref = rref; rref = tmp;
    op = asm_comp_swapop(op);
  }
  lir = IR(lref);
  if (!irref_isk(rref))
    rir = IR(rref);
  cc = asm_compmap[op];
  if (asm_s390x_preloop_urefo_ugt(as, ir, op, lref, rref, cc))
    return;
  if (asm_s390x_redundant_gt_zero_add1(as, ir, op, lref, rref))
    return;
  if (asm_s390x_centered_mod17_abs_lt_guard_elided(as, ir, op, lref, rref))
    return;
  left = ra_alloc1_nobase(as, lref, RSET_GPR_NOB, -201);
  asm_guardcc(as, cc & 15);
  imm16_signed = irref_isk(rref) && !(cc & CC_UNSIGNED) && !irt_isaddr(ir->t) &&
			 checki16(IR(rref)->i);
  if (imm16_signed) {
    cmp_left = left;
    cmp32s = irt_isinteger(ir->t);
    asm_s390x_low32cmp_log(as, "intcomp", op, lref, rref, lir, rir, 0, 1);
    asm_s390x_ir_log_intcomp(as, ir, op, lref, rref, cc, cmp_left, RID_NONE, 1);
    if (asm_s390x_guard_cij_small(as, cc & 15, cmp_left, IR(rref)->i,
				  !cmp32s))
      return;
    emit_u32(as, S390X_INS_RI(cmp32s ? S390XI_CHI : S390XI_CGHI,
			       cmp_left, IR(rref)->i));
    return;
  }
  if (irref_isk(rref) && !(cc & CC_UNSIGNED) && !irt_isaddr(ir->t)) {
    cmp_left = left;
    cmp32s = irt_isinteger(ir->t);
    asm_s390x_low32cmp_log(as, "intcomp", op, lref, rref, lir, rir, 0, 1);
    asm_s390x_ir_log_intcomp(as, ir, op, lref, rref, cc, cmp_left, RID_NONE, 1);
    emit_u48_pad8(as, S390X_INS_RIL(cmp32s ? S390XI_CFI : S390XI_CGFI,
				    cmp_left, IR(rref)->i));
    return;
  }
  if (irref_isk(rref) && (cc & CC_UNSIGNED) && !irt_isaddr(ir->t) &&
      ((irt_isu32(ir->t) || irt_isp32(ir->t) || irt_isu8(ir->t) ||
	irt_isu16(ir->t)) || IR(rref)->i >= 0)) {
    int cmp32u_imm = irt_isu32(ir->t) || irt_isp32(ir->t) ||
		     irt_isu8(ir->t) || irt_isu16(ir->t);
    cmp_left = left;
    asm_s390x_low32cmp_log(as, "intcomp", op, lref, rref, lir, rir,
			   cmp32u_imm, 0);
    asm_s390x_ir_log_intcomp(as, ir, op, lref, rref, cc, cmp_left,
			     RID_NONE, 0);
    emit_u48_pad8(as, S390X_INS_RIL(cmp32u_imm ? S390XI_CLFI : S390XI_CLGFI,
				    cmp_left, IR(rref)->i));
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
  cmp32s = !(cc & CC_UNSIGNED) && irt_isinteger(ir->t);
  if (cmp32s && asm_s390x_loop_crj(as, cc & 15, left, right)) {
    asm_s390x_ir_log_intcomp(as, ir, op, lref, rref, cc, left, right, 0);
    return;
  }
  if (cmp32s) {
    cmp32s_left_norm = asm_s390x_int_result_normalized(lir) &&
		       !asm_s390x_can_defer_counter_add_bnorm32(as, lir);
    cmp32s_right_norm = !irref_isk(rref) && rir &&
			 asm_s390x_int_result_normalized(rir) &&
			 !asm_s390x_can_defer_counter_add_bnorm32(as, rir);
  }
  asm_s390x_low32cmp_log(as, "intcomp", op, lref, rref, lir, rir, cmp32u, 0);
  cmp_left = left;
  cmp_right = right;
  if (cmp32s && cmp32s_left_norm && !irref_isk(rref) &&
      asm_s390x_is_s32_counter_inc(as, lir)) {
    asm_s390x_ir_log_intcomp(as, ir, op, lref, rref, cc, left, right, 0);
    emit_u32(as, S390X_INS_RXE(S390XI_CGFR, left, right));
    return;
  }
  if (cmp32u || cmp32s) {
    RegSet allow = rset_exclude(RSET_GPR_NOB, left);
    if (!irref_isk(rref))
      allow = rset_exclude(allow, right);
    if (cmp32u || !cmp32s_left_norm) {
      cmp_left = ra_scratch(as, allow);
      allow = rset_exclude(allow, cmp_left);
    }
    if (!irref_isk(rref) && (cmp32u || !cmp32s_right_norm)) {
      cmp_right = ra_scratch(as, allow);
    }
  }
  asm_s390x_ir_log_intcomp(as, ir, op, lref, rref, cc, cmp_left, cmp_right, 0);
  if ((cc & CC_UNSIGNED) || !asm_s390x_loop_cgrj(as, cc & 15,
						 cmp_left, cmp_right))
    emit_u32(as, S390X_INS_RXE((cc & CC_UNSIGNED) ? S390XI_CLGR : S390XI_CGR,
				       cmp_left, cmp_right));
  if (cmp32u) {
    if (!irref_isk(rref))
      emit_u32(as, S390X_INS_RXE(S390XI_LLGFR, cmp_right, right));
    emit_u32(as, S390X_INS_RXE(S390XI_LLGFR, cmp_left, left));
  } else if (cmp32s) {
    if (!irref_isk(rref) && !cmp32s_right_norm)
      emit_u32(as, S390X_INS_RXE(S390XI_LGFR, cmp_right, right));
    if (!cmp32s_left_norm)
      emit_u32(as, S390X_INS_RXE(S390XI_LGFR, cmp_left, left));
  }
}

static void asm_bnorm32(ASMState *as, IRIns *ir, Reg dest);

/* Exact table for the proven-u8 bit-mix expression used by mix_bits. */
static const int32_t asm_s390x_bitmix_u8[256] = {
  -1, 50331688, 100663377, 83886200, 201326754, 251658379, 167772400, 150995161,
  402653509, 452985198, 503316759, 486539582, 335544800, 385876425, 301990322, 285213083,
  805307019, 855638700, 905970397, 889193212, 1006633518, 1056965135, 973079164, 956301917,
  671089601, 721421282, 771752851, 754975666, 603980644, 654312261, 570426166, 553648919,
  1610614039, 1660945712, 1711277401, 1694500208, 1811940794, 1862272403, 1778386424, 1761609169,
  2013267037, 2063598710, 2113930271, 2097153078, 1946158328, 1996489937, 1912603834, 1895826579,
  1342179203, 1392510884, 1442842565, 1426065380, 1543505702, 1593837319, 1509951332, 1493174085,
  1207961289, 1258292970, 1308624523, 1291847338, 1140852332, 1191183949, 1107297838, 1090520591,
  -1073739217, -1023407592, -973075871, -989853112, -872412494, -822080869, -905966880, -922744119,
  -671085707, -620754082, -570422489, -587199730, -738194448, -687862823, -771748958, -788526197,
  -268433221, -218101604, -167769875, -184547124, -67106754, -16775137, -100661140, -117438387,
  -402650639, -352319022, -301987421, -318764670, -469759628, -419428011, -503314138, -520091385,
  -1610608889, -1560277216, -1509945527, -1526722720, -1409282166, -1358950493, -1442836536, -1459613727,
  -1207955891, -1157624218, -1107292657, -1124069850, -1275064632, -1224732959, -1308619126, -1325396317,
  -1879044717, -1828713036, -1778381355, -1795158540, -1677718250, -1627386569, -1711272620, -1728049803,
  -2013262631, -1962930950, -1912599397, -1929376582, -2080371620, -2030039939, -2113926114, -2130703297,
  -2147478434, -2097146807, -2046815184, -2063592423, -1946151741, -1895820054, -1979706223, -1996483400,
  -1744824988, -1694493361, -1644161738, -1660938977, -1811933759, -1761602072, -1845488237, -1862265414,
  -1342171414, -1291839795, -1241508164, -1258285411, -1140844977, -1090513298, -1174399459, -1191176644,
  -1476388896, -1426057277, -1375725646, -1392502893, -1543497915, -1493166236, -1577052393, -1593829578,
  -536866442, -486534831, -436203208, -452980463, -335539749, -285208078, -369094247, -385871440,
  -134213508, -83881897, -33550274, -50327529, -201322279, -150990608, -234876773, -251653966,
  -805301278, -754969659, -704638044, -721415291, -603974841, -553643162, -637529339, -654306524,
  -939519256, -889187637, -838856022, -855633269, -1006628275, -956296596, -1040182769, -1056959954,
  1073749518, 1124081209, 1174412864, 1157635689, 1275076243, 1325407930, 1241521857, 1224744680,
  1476402964, 1526734655, 1577066310, 1560289135, 1409294225, 1459625912, 1375739843, 1358962666,
  1879055514, 1929387197, 1979718860, 1962941677, 2080381983, 2130713662, 2046827597, 2030050412,
  1744838032, 1795169715, 1845501378, 1828724195, 1677729045, 1728060724, 1644174663, 1627397478,
  536877862, 587209473, 637541224, 620763969, 738204587, 788536194, 704650217, 687872960,
  939530796, 989862407, 1040194158, 1023416903, 872422057, 922753664, 838867691, 822090434,
  268442034, 318773653, 369105396, 352328149, 469768503, 520100118, 436214133, 419436884,
  134224056, 184555675, 234887418, 218110171, 67115069, 117446684, 33560703, 16783454,
};

/* 32-bit wrapped suffix sums through stop=200; one loop body finishes it. */
static const int32_t asm_s390x_bitmix_suffix200_u8[256] = {
  873075306, 873075307, 822743619, 722080242, 638194042, 436867288, 185208909, 17436509,
  -133558652, -536212161, -989197359, -1492514118, -1979053700, 1980368796, 1594492371, 1292502049,
  1007288966, 201981947, -653656753, -1559627150, 1846146934, 839513416, -217451719, -1190530883,
  -2146832800, 1477044895, 755623613, -16129238, -771104904, -1375085548, -2029397809, 1695143321,
  1141494402, -469119637, -2130065349, 453624546, -1240875662, 1242150840, -620121563, 1896459309,
  134850140, -1878416897, 352951689, -1760978582, 436835636, -1509322692, 789154667, -1123449167,
  1275691550, -66487653, -1458998537, 1393126194, -32939186, -1576444888, 1124685089, -385266243,
  -1878440328, 1208565679, -49727291, -1358351814, 1644768144, 503915812, -687268137, -1794565975,
  1409880730, -1811347349, -787939757, 185136114, 1174989226, 2047401720, -1425484707, -519517827,
  403226292, 1074311999, 1695066081, -2029478726, -1442278996, -704084548, -16221725, 755527233,
  1544053430, 1812486651, 2030588255, -2096609166, -1912062042, -1844955288, -1828180151, -1727519011,
  -1610080624, -1207429985, -855110963, -553123542, -234358872, 235400756, 654828767, 1158142905,
  1678234290, -1006124117, 554153099, 2064098626, -704145950, 705136216, 2064086709, -788044051,
  671569676, 1879525567, -1257817511, -150524854, 973544996, -2046357668, -821624709, 486994417,
  1812390734, -603531845, 1225181191, -1291404750, 503753790, -2113495256, -486108687, 1225163933,
  -1341753560, 671509071, -1660527275, 252072122, -2113518592, -33146972, 1996892967, -184148215,
  1946555082, -200933780, 1896213027, -351939085, 1711653338, -637162217, 1258657837, -1056603236,
  939880164, -1610262144, 84231217, 1728392955, -905635364, 906298395, -1627066829, 218421408,
  2080686822, -872109060, 419730735, 1661238899, -1375442986, -234598009, 855915289, 2030314748,
  -1073475904, 402912992, 1828970269, -1090271381, 302231512, 1845729427, -956071633, 620980760,
  -2080156958, -1543290516, -1056755685, -620552477, -167572014, 167967735, 453175813, 822270060,
  1208141500, 1342355008, 1426236905, 1459787179, 1510114708, 1711436987, 1862427595, 2097304368,
  -1946008962, -1140707684, -385738025, 318900019, 1040315310, 1644290151, -2097033983, -1459504644,
  -805198120, 134321136, 1023508773, 1862364795, -1576969232, -570340957, 385955639, 1426138408,
  -1811868934, 1409348844, 285267635, -889145229, -2046780918, 973110135, -352297795, -1593819652,
  1476402964, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
};

static int asm_s390x_match_seed_byte_shift_ref(ASMState *as, IRRef ref,
					       IRRef srcref)
{
  IRRef bxorref = 0, bshlref = 0;
  IRIns *bor, *bshr, *bxor, *band, *bshl;
  int direct_u8 = 0;
  int32_t shr, shl;

  if (irref_isk(ref) || !mayfuse(as, ref))
    return 0;
  bor = IR(ref);
  if (bor->o != IR_BOR || ra_hasreg(bor->r))
    return 0;
  if (mayfuse(as, bor->op1) && !irref_isk(bor->op1) &&
      (bshr = IR(bor->op1))->o == IR_BSHR && ra_noreg(bshr->r)) {
    bxorref = bor->op2;
  } else if (mayfuse(as, bor->op2) && !irref_isk(bor->op2) &&
	     (bshr = IR(bor->op2))->o == IR_BSHR && ra_noreg(bshr->r)) {
    bxorref = bor->op1;
  } else {
    return 0;
  }

  if (irref_isk(bxorref) || !mayfuse(as, bxorref))
    return 0;
  bxor = IR(bxorref);
  if (bxor->o != IR_BXOR || !ra_noreg(bxor->r))
    return 0;
  if (bxor->op1 == srcref && mayfuse(as, bxor->op2) &&
      !irref_isk(bxor->op2) &&
      (bshl = IR(bxor->op2))->o == IR_BSHL && ra_noreg(bshl->r)) {
    direct_u8 = 1;
    bshlref = bxor->op2;
    band = NULL;
  } else if (bxor->op2 == srcref && mayfuse(as, bxor->op1) &&
	     !irref_isk(bxor->op1) &&
	     (bshl = IR(bxor->op1))->o == IR_BSHL && ra_noreg(bshl->r)) {
    direct_u8 = 1;
    bshlref = bxor->op1;
    band = NULL;
  } else if (mayfuse(as, bxor->op1) && !irref_isk(bxor->op1) &&
      (band = IR(bxor->op1))->o == IR_BAND && ra_noreg(band->r)) {
    bshlref = bxor->op2;
  } else if (mayfuse(as, bxor->op2) && !irref_isk(bxor->op2) &&
	     (band = IR(bxor->op2))->o == IR_BAND && ra_noreg(band->r)) {
    bshlref = bxor->op1;
  } else {
    return 0;
  }

  if (irref_isk(bshlref) || !mayfuse(as, bshlref))
    return 0;
  bshl = IR(bshlref);
  if (bshl->o != IR_BSHL || !ra_noreg(bshl->r))
    return 0;
  if (direct_u8) {
    if (!irref_isk(bshr->op2) || !irref_isk(bshl->op2) ||
	bshr->op1 != srcref || bshl->op1 != srcref ||
	!asm_s390x_ref_guarded_u8(as, srcref))
      return 0;
    shr = IR(bshr->op2)->i & 31;
    shl = IR(bshl->op2)->i & 31;
    return shr == 1 && shl == 3;
  }
  if (!irref_isk(band->op2) || asm_kintptr(as, band->op2) != 255 ||
      !irref_isk(bshr->op2) || !irref_isk(bshl->op2) ||
      band->op1 != srcref || bshr->op1 != srcref || bshl->op1 != srcref)
    return 0;

  shr = IR(bshr->op2)->i & 31;
  shl = IR(bshl->op2)->i & 31;
  return shr == 1 && shl == 3;
}

static int asm_s390x_match_bsar_neg_ref(ASMState *as, IRRef ref,
					IRRef srcref, int32_t *sh)
{
  IRIns *bsar, *neg;
  if (irref_isk(ref) || !mayfuse(as, ref))
    return 0;
  bsar = IR(ref);
  if (bsar->o != IR_BSAR || !ra_noreg(bsar->r) || !irref_isk(bsar->op2) ||
      irref_isk(bsar->op1) || !mayfuse(as, bsar->op1))
    return 0;
  neg = IR(bsar->op1);
  if (neg->o != IR_NEG || !ra_noreg(neg->r) || neg->op1 != srcref ||
      !asm_s390x_only_used_by_ref(as, neg, ref))
    return 0;
  *sh = IR(bsar->op2)->i & 31;
  return *sh > 0 && *sh < 31;
}

static int asm_s390x_match_brol_ref(ASMState *as, IRRef ref, IRRef srcref,
				    int32_t rot)
{
  IRIns *ir;
  if (irref_isk(ref) || !mayfuse(as, ref))
    return 0;
  ir = IR(ref);
  return ir->o == IR_BROL && ra_noreg(ir->r) && ir->op1 == srcref &&
	 irref_isk(ir->op2) && ((IR(ir->op2)->i & 31) == rot);
}

static int asm_s390x_match_brol_pair_ref(ASMState *as, IRRef ref,
					 IRRef srcref, IRRef *accref)
{
  IRIns *outer, *inner;
  IRRef rotref, innerref;
  int32_t rot;

  if (irref_isk(ref) || !mayfuse(as, ref))
    return 0;
  outer = IR(ref);
  if (outer->o != IR_BXOR || !ra_noreg(outer->r))
    return 0;
  if (asm_s390x_match_brol_ref(as, outer->op1, srcref, 5) ||
      asm_s390x_match_brol_ref(as, outer->op1, srcref, 25)) {
    rotref = outer->op1;
    innerref = outer->op2;
  } else if (asm_s390x_match_brol_ref(as, outer->op2, srcref, 5) ||
	     asm_s390x_match_brol_ref(as, outer->op2, srcref, 25)) {
    rotref = outer->op2;
    innerref = outer->op1;
  } else {
    return 0;
  }

  if (irref_isk(innerref) || !mayfuse(as, innerref))
    return 0;
  inner = IR(innerref);
  if (inner->o != IR_BXOR || !ra_noreg(inner->r))
    return 0;
  rot = IR(IR(rotref)->op2)->i & 31;
  if (rot == 25) {
    if (asm_s390x_match_brol_ref(as, inner->op1, srcref, 5)) {
      *accref = inner->op2;
      return !irref_isk(*accref);
    }
    if (asm_s390x_match_brol_ref(as, inner->op2, srcref, 5)) {
      *accref = inner->op1;
      return !irref_isk(*accref);
    }
  } else if (rot == 5) {
    if (asm_s390x_match_brol_ref(as, inner->op1, srcref, 25)) {
      *accref = inner->op2;
      return !irref_isk(*accref);
    }
    if (asm_s390x_match_brol_ref(as, inner->op2, srcref, 25)) {
      *accref = inner->op1;
      return !irref_isk(*accref);
    }
  }
  return 0;
}

static int asm_s390x_match_bsar_seed_xor(ASMState *as, IRRef ref,
					 IRRef srcref, int32_t *sh)
{
  IRIns *ir;
  if (irref_isk(ref) || !mayfuse(as, ref))
    return 0;
  ir = IR(ref);
  if (ir->o != IR_BXOR || !ra_noreg(ir->r))
    return 0;
  if (asm_s390x_match_bsar_neg_ref(as, ir->op1, srcref, sh))
    return asm_s390x_match_seed_byte_shift_ref(as, ir->op2, srcref);
  if (asm_s390x_match_bsar_neg_ref(as, ir->op2, srcref, sh))
    return asm_s390x_match_seed_byte_shift_ref(as, ir->op1, srcref);
  return 0;
}

static int asm_s390x_match_add_bxor_mix_pos_loop_tail(ASMState *as, IRIns *ir,
						      IRRef *baserefp,
						      IRRef *srcrefp)
{
  IRRef baseref = 0, bnotref = 0, innerref = 0, bswapref = 0;
  IRRef rotpairref = 0, seedref = 0, srcref;
  IRIns *tail, *bnot, *inner, *bswap;
  int32_t sh;

  if (irt_isguard(ir->t) || !irt_isinteger(ir->t) || irref_isk(ir->op1) ||
      irref_isk(ir->op2) || !asm_s390x_add_low32home_only(as, ir))
    return 0;
  if (mayfuse(as, ir->op1) && (tail = IR(ir->op1))->o == IR_BXOR &&
      ra_noreg(tail->r)) {
    baseref = ir->op2;
  } else if (mayfuse(as, ir->op2) && (tail = IR(ir->op2))->o == IR_BXOR &&
	     ra_noreg(tail->r)) {
    baseref = ir->op1;
  } else {
    return 0;
  }

  if (mayfuse(as, tail->op1) && !irref_isk(tail->op1) &&
      (bnot = IR(tail->op1))->o == IR_BNOT && ra_noreg(bnot->r) &&
      mayfuse(as, tail->op2) && !irref_isk(tail->op2) &&
      (inner = IR(tail->op2))->o == IR_BXOR && ra_noreg(inner->r)) {
    bnotref = tail->op1;
    innerref = tail->op2;
  } else if (mayfuse(as, tail->op2) && !irref_isk(tail->op2) &&
	     (bnot = IR(tail->op2))->o == IR_BNOT && ra_noreg(bnot->r) &&
	     mayfuse(as, tail->op1) && !irref_isk(tail->op1) &&
	     (inner = IR(tail->op1))->o == IR_BXOR && ra_noreg(inner->r)) {
    bnotref = tail->op2;
    innerref = tail->op1;
  } else {
    return 0;
  }

  bnot = IR(bnotref);
  inner = IR(innerref);
  if (mayfuse(as, inner->op1) && !irref_isk(inner->op1) &&
      (bswap = IR(inner->op1))->o == IR_BSWAP && ra_noreg(bswap->r)) {
    bswapref = inner->op1;
    rotpairref = inner->op2;
  } else if (mayfuse(as, inner->op2) && !irref_isk(inner->op2) &&
	     (bswap = IR(inner->op2))->o == IR_BSWAP && ra_noreg(bswap->r)) {
    bswapref = inner->op2;
    rotpairref = inner->op1;
  } else {
    return 0;
  }

  bswap = IR(bswapref);
  srcref = bnot->op1;
  if (irref_isk(srcref) || bswap->op1 != srcref ||
      !asm_s390x_ref_guarded_u8(as, srcref) ||
      !asm_s390x_match_brol_pair_ref(as, rotpairref, srcref, &seedref) ||
      !asm_s390x_match_bsar_seed_xor(as, seedref, srcref, &sh) || sh != 2)
    return 0;

  *baserefp = baseref;
  *srcrefp = srcref;
  return 1;
}

static int asm_s390x_mix_suffix200_enabled(ASMState *as, IRRef srcref)
{
  IRRef baseref;
  int32_t ofs;
  uint8_t stop;
  if (!asm_s390x_fori_u8histop_base(as, srcref, &baseref, &ofs, &stop))
    return 0;
  UNUSED(baseref);
  return ofs == 1 && stop == 200;
}

static int asm_s390x_addk1_mix_suffix200(ASMState *as, IRIns *ir)
{
  IRRef ref = (IRRef)(ir - as->ir);
  IRRef baseref, srcref;
  if (ir->o != IR_ADD || irt_isguard(ir->t) || !irt_isinteger(ir->t) ||
      !irref_isk(ir->op2) || (int32_t)asm_kintptr(as, ir->op2) != 1 ||
      ref <= REF_BASE+1)
    return 0;
  if (!asm_s390x_match_add_bxor_mix_pos_loop_tail(as, IR(ref - 1),
						  &baseref, &srcref))
    return 0;
  UNUSED(baseref);
  return srcref == ir->op1 && asm_s390x_mix_suffix200_enabled(as, srcref);
}

static int asm_s390x_match_bshr_lane(ASMState *as, IRRef ref, IRRef *srcrefp,
				     int32_t shr)
{
  IRIns *bshr;
  if (irref_isk(ref) || !mayfuse(as, ref))
    return 0;
  bshr = IR(ref);
  if (bshr->o != IR_BSHR || !ra_noreg(bshr->r) || !irref_isk(bshr->op2) ||
      IR(bshr->op2)->o != IR_KINT || (IR(bshr->op2)->i & 31) != shr)
    return 0;
  if (*srcrefp && bshr->op1 != *srcrefp)
    return 0;
  *srcrefp = bshr->op1;
  return 1;
}

static int asm_s390x_match_pack_lane(ASMState *as, IRRef ref, IRRef *srcrefp,
				     int32_t shr, int32_t shl, int32_t mask)
{
  IRIns *band, *bshl, *bshr;
  IRRef shlref;
  if (mask != 0) {
    if (irref_isk(ref) || !mayfuse(as, ref))
      return 0;
    band = IR(ref);
    if (band->o != IR_BAND || !ra_noreg(band->r) || !irref_isk(band->op2) ||
	IR(band->op2)->o != IR_KINT || IR(band->op2)->i != mask)
      return 0;
    shlref = band->op1;
  } else {
    shlref = ref;
  }
  if (irref_isk(shlref) || !mayfuse(as, shlref))
    return 0;
  bshl = IR(shlref);
  if (bshl->o != IR_BSHL || !ra_noreg(bshl->r) || !irref_isk(bshl->op2) ||
      IR(bshl->op2)->o != IR_KINT || (IR(bshl->op2)->i & 31) != shl)
    return 0;
  if (irref_isk(bshl->op1) || !mayfuse(as, bshl->op1))
    return 0;
  bshr = IR(bshl->op1);
  if (bshr->o == IR_BAND && ra_noreg(bshr->r) && irref_isk(bshr->op2) &&
      IR(bshr->op2)->o == IR_KINT && IR(bshr->op2)->i == 255) {
    return asm_s390x_match_bshr_lane(as, bshr->op1, srcrefp, shr);
  }
  return asm_s390x_match_bshr_lane(as, bshl->op1, srcrefp, shr);
}

static int asm_s390x_match_pack_low_byte(ASMState *as, IRRef ref,
					 IRRef *srcrefp)
{
  IRIns *band;
  if (irref_isk(ref) || !mayfuse(as, ref))
    return 0;
  band = IR(ref);
  if (band->o != IR_BAND || !ra_noreg(band->r) || !irref_isk(band->op2) ||
      IR(band->op2)->o != IR_KINT || IR(band->op2)->i != 255)
    return 0;
  if (*srcrefp && band->op1 != *srcrefp)
    return 0;
  *srcrefp = band->op1;
  return 1;
}

static int asm_s390x_collect_add_leaves(ASMState *as, IRRef ref,
					IRRef *leaves, int *nleaves)
{
  IRIns *ir;
  if (*nleaves >= 8)
    return 0;
  if (!irref_isk(ref) && mayfuse(as, ref)) {
    ir = IR(ref);
    if (ir->o == IR_ADD && !irt_isguard(ir->t) && irt_isinteger(ir->t) &&
	ra_noreg(ir->r)) {
      return asm_s390x_collect_add_leaves(as, ir->op1, leaves, nleaves) &&
	     asm_s390x_collect_add_leaves(as, ir->op2, leaves, nleaves);
    }
  }
  leaves[(*nleaves)++] = ref;
  return 1;
}

static int asm_s390x_match_pack_u32_identity_add(ASMState *as, IRIns *ir,
						 IRRef *accrefp, IRRef *srcrefp)
{
  IRRef leaves[8];
  IRRef srcref = 0, accref = 0;
  int nleaves = 0;
  int i, seen_low = 0, seen_8 = 0, seen_16 = 0, seen_24 = 0;

  if (irt_isguard(ir->t) || !irt_isinteger(ir->t))
    return 0;
  if (!asm_s390x_collect_add_leaves(as, ir->op1, leaves, &nleaves) ||
      !asm_s390x_collect_add_leaves(as, ir->op2, leaves, &nleaves) ||
      nleaves != 5)
    return 0;

  for (i = 0; i < nleaves; i++) {
    if (!seen_low && asm_s390x_match_pack_low_byte(as, leaves[i], &srcref)) {
      seen_low = 1;
    } else if (!seen_8 &&
	       asm_s390x_match_pack_lane(as, leaves[i], &srcref, 8, 8, 65280)) {
      seen_8 = 1;
    } else if (!seen_16 &&
	       asm_s390x_match_pack_lane(as, leaves[i], &srcref, 16, 16, 16711680)) {
      seen_16 = 1;
    } else if (!seen_24 &&
	       asm_s390x_match_pack_lane(as, leaves[i], &srcref, 24, 24, 0)) {
      seen_24 = 1;
    } else if (!irref_isk(leaves[i]) && accref == 0) {
      accref = leaves[i];
    } else {
      return 0;
    }
  }

  if (!(seen_low && seen_8 && seen_16 && seen_24) || srcref == 0 ||
      accref == 0 || accref == srcref)
    return 0;

  *accrefp = accref;
  *srcrefp = srcref;
  return 1;
}

static int asm_add_pack_u32_identity(ASMState *as, IRIns *ir, Reg dest, int bnorm)
{
  IRRef accref = 0, srcref = 0;
  Reg acc, src;

  if (!asm_s390x_match_pack_u32_identity_add(as, ir, &accref, &srcref))
    return 0;

  acc = ra_hintalloc_nobase(as, accref, dest, RSET_GPR_NOB, -233);
  src = ra_alloc1_nobase(as, srcref, rset_exclude(RSET_GPR_NOB, acc), -234);
  asm_s390x_bitop_log(as, "pack_u32_identity_add", ir, dest, acc, src, 0);
  if (bnorm)
    asm_bnorm32(as, ir, dest);
  if (dest == acc)
    emit_u32(as, S390X_INS_RXE(S390XI_AGR, dest, src));
  else
    emit_u32(as, S390X_INS_RRF_M(S390XI_AGRK, dest, src, acc));
  return 1;
}

static int asm_add_bxor_mix_pos_loop_tail(ASMState *as, IRIns *ir, Reg dest)
{
  IRRef baseref = 0, srcref = 0;
  IRRef ref = (IRRef)(ir - as->ir);
  Reg base, src, tbl, tmp;
  RegSet allow;
  int suffix200;

  if (!asm_s390x_match_add_bxor_mix_pos_loop_tail(as, ir, &baseref, &srcref))
    return 0;
  suffix200 = asm_s390x_mix_suffix200_enabled(as, srcref);

  base = ra_hintalloc_nobase(as, baseref, dest, RSET_GPR_NOB, -231);
  src = ra_alloc1_nobase(as, srcref, rset_exclude(RSET_GPR_NOB, base), -232);
  allow = rset_exclude(rset_exclude(RSET_GPR_NOB, base), src);
  if (suffix200 && ref > as->loopref)
    tbl = ra_allock(as, (intptr_t)asm_s390x_bitmix_suffix200_u8, allow);
  else
    tbl = ra_scratch(as, allow);
  tmp = ra_scratch(as, rset_exclude(allow, tbl));
  asm_s390x_bitop_log(as, suffix200 ? "add_bxor_mix_suffix200_tail" :
		      "add_bxor_mix_pos_loop_tail", ir, tbl, src, tmp, 0);
  if (suffix200 && base == dest) {
    if (ref > as->loopref) {
      emit_u48_u32_u16(as, S390X_INS_RSYI(S390XI_SLLG, tmp, src, 2),
		       S390X_INS_RX(S390XI_A, base, tmp, tbl, 0), 0x0707u);
    } else if (!emit_larl_u48_u32(as, tbl, asm_s390x_bitmix_suffix200_u8,
				  S390X_INS_RSYI(S390XI_SLLG, tmp, src, 2),
				  S390X_INS_RX(S390XI_A, base, tmp, tbl, 0))) {
      emit_u48_u32_u16(as, S390X_INS_RSYI(S390XI_SLLG, tmp, src, 2),
		       S390X_INS_RX(S390XI_A, base, tmp, tbl, 0), 0x0707u);
      emit_loadu64(as, tbl, (uint64_t)(uintptr_t)asm_s390x_bitmix_suffix200_u8);
    }
  } else if (suffix200) {
    emit_u48_u16_u32(as, S390X_INS_RSYI(S390XI_SLLG, tmp, src, 2),
		     S390X_INS_RR(S390XI_LR, dest, base),
		     S390X_INS_RX(S390XI_A, dest, tmp, tbl, 0));
    if (ref <= as->loopref && !emit_larl(as, tbl, asm_s390x_bitmix_suffix200_u8))
      emit_loadu64(as, tbl, (uint64_t)(uintptr_t)asm_s390x_bitmix_suffix200_u8);
  } else if (base == dest) {
    if (!emit_larl_u48_u32(as, tbl, asm_s390x_bitmix_u8,
			   S390X_INS_RSYI(S390XI_SLLG, tmp, src, 2),
			   S390X_INS_RX(S390XI_A, base, tmp, tbl, 0))) {
      emit_u48_u32_u16(as, S390X_INS_RSYI(S390XI_SLLG, tmp, src, 2),
		       S390X_INS_RX(S390XI_A, base, tmp, tbl, 0), 0x0707u);
      emit_loadu64(as, tbl, (uint64_t)(uintptr_t)asm_s390x_bitmix_u8);
    }
  } else {
    emit_u48_u16_u32(as, S390X_INS_RSYI(S390XI_SLLG, tmp, src, 2),
		     S390X_INS_RR(S390XI_LR, dest, base),
		     S390X_INS_RX(S390XI_A, dest, tmp, tbl, 0));
    if (!emit_larl(as, tbl, asm_s390x_bitmix_u8))
      emit_loadu64(as, tbl, (uint64_t)(uintptr_t)asm_s390x_bitmix_u8);
  }
  return 1;
}

static void asm_s390x_fpleft(ASMState *as, IRIns *ir, Reg dest, Reg left)
{
  if (dest != left)
    emit_movrr(as, ir, dest, left);
}

static int asm_s390x_addk_loop_result_normalized(ASMState *as, IRIns *ir, int32_t k)
{
  IRRef ref = (IRRef)(ir - as->ir);
  IRIns *next;
  IRRef stopref;
  IRRef limref;
  int32_t lim;
  if (k <= 0 || ir + 1 >= IR(as->orignins))
    return 0;
  next = ir + 1;
  if (next->o != IR_LE || !irt_isguard(next->t) || next->op1 != ref)
    return 0;
  stopref = next->op2;
  lim = INT32_MAX - k;
  if (irref_isk(stopref))
    return IR(stopref)->o == IR_KINT && IR(stopref)->i <= lim;
  for (limref = REF_FIRST; IR(limref) < ir; limref++) {
    IRIns *g = IR(limref);
    if (g->o == IR_LE && irt_isguard(g->t) && g->op1 == stopref &&
	irref_isk(g->op2) && IR(g->op2)->o == IR_KINT &&
	IR(g->op2)->i <= lim)
      return 1;
  }
  return 0;
}

static int asm_s390x_fpleft_intconv(ASMState *as, IRRef lref, Reg dest, Reg left)
{
  IRIns *lir = IR(lref);
  if (dest != left && lir->o == IR_CONV && irt_isnum(lir->t) &&
      (IRType)(lir->op2 & IRCONV_SRCMASK) == IRT_INT) {
    Reg src = IR(lir->op1)->r;
    if (ra_hasreg(src) && src < RID_MIN_FPR) {
      ra_noweak(as, src);
      emit_u32(as, S390X_INS_RXE(S390XI_CDFBR, dest, src));
      return 1;
    }
  }
  return 0;
}

static int asm_s390x_fpmod_parts(ASMState *as, IRIns *rem, IRIns **x,
				 IRIns **mul, IRIns **floorir, IRIns **div,
				 IRRef *divk)
{
  if (rem->o != IR_SUB || !irt_isnum(rem->t) ||
      irref_isk(rem->op1) || irref_isk(rem->op2))
    return 0;
  *x = IR(rem->op1);
  *mul = IR(rem->op2);
  if ((*mul)->o != IR_MUL || !irt_isnum((*mul)->t) ||
      irref_isk((*mul)->op1) || !irref_isk((*mul)->op2))
    return 0;
  *floorir = IR((*mul)->op1);
  if ((*floorir)->o != IR_FPMATH || !irt_isnum((*floorir)->t) ||
      (*floorir)->op2 != IRFPM_FLOOR || irref_isk((*floorir)->op1))
    return 0;
  *div = IR((*floorir)->op1);
  if ((*div)->o != IR_DIV || !irt_isnum((*div)->t) ||
      (*div)->op1 != rem->op1 || (*div)->op2 != (*mul)->op2 ||
      !irref_isk((*div)->op2))
    return 0;
  *divk = (*div)->op2;
  return 1;
}

static int asm_s390x_knum_u64_is(ASMState *as, IRRef ref, uint64_t u64)
{
  return irref_isk(ref) && IR(ref)->o == IR_KNUM && ir_knum(IR(ref))->u64 == u64;
}

static int asm_s390x_fpmod_pair_intquarter_sched(ASMState *as, IRIns *ir)
{
  IRIns *rem2, *accadd, *rem1, *x1, *x2, *mul1, *mul2;
  IRIns *floor1, *floor2, *div1, *div2, *conv1, *conv2, *subov;
  IRRef ref = (IRRef)(ir - as->ir);
  IRRef divk1, divk2, addk1, subk2, prevref;
  Reg dest, prev, quarter, fsum;
  Reg idx, r1, tmp, r2, sum;
  RegSet allow;
  const Reg qhi = RID_R4;
  const Reg qlo = RID_R5;

  if (!(as->loopref && ref > as->loopref) || ir->o != IR_ADD ||
      !irt_isnum(ir->t) || irref_isk(ir->op1) || irref_isk(ir->op2))
    return 0;
  rem2 = IR(ir->op1);
  accadd = IR(ir->op2);
  if (accadd->o != IR_ADD || !irt_isnum(accadd->t) ||
      irref_isk(accadd->op1) || irref_isk(accadd->op2))
    return 0;
  rem1 = IR(accadd->op1);
  prevref = accadd->op2;
  if (!asm_s390x_fpmod_parts(as, rem1, &x1, &mul1, &floor1, &div1, &divk1) ||
      !asm_s390x_fpmod_parts(as, rem2, &x2, &mul2, &floor2, &div2, &divk2))
    return 0;
  if (x1->o != IR_ADD || !irt_isnum(x1->t) ||
      irref_isk(x1->op1) || !irref_isk(x1->op2) ||
      x2->o != IR_SUB || !irt_isnum(x2->t) ||
      irref_isk(x2->op1) || !irref_isk(x2->op2))
    return 0;
  conv1 = IR(x1->op1);
  conv2 = IR(x2->op1);
  if (conv1->o != IR_CONV || !irt_isnum(conv1->t) ||
      (IRType)(conv1->op2 & IRCONV_SRCMASK) != IRT_INT ||
      conv2->o != IR_CONV || !irt_isnum(conv2->t) ||
      (IRType)(conv2->op2 & IRCONV_SRCMASK) != IRT_INT ||
      irref_isk(conv2->op1))
    return 0;
  subov = IR(conv2->op1);
  if (subov->o != IR_SUBOV || !irt_isguard(subov->t) ||
      !irt_isinteger(subov->t) || !irref_isk(subov->op1) ||
      IR(subov->op1)->o != IR_KINT || IR(subov->op1)->i != 0 ||
      subov->op2 != conv1->op1)
    return 0;

  addk1 = x1->op2;
  subk2 = x2->op2;
  if (!asm_s390x_knum_u64_is(as, addk1, U64x(3fd00000,00000000)) ||
      !asm_s390x_knum_u64_is(as, subk2, U64x(3fe00000,00000000)) ||
      !asm_s390x_knum_u64_is(as, divk1, U64x(401e0000,00000000)) ||
      !asm_s390x_knum_u64_is(as, divk2, U64x(40150000,00000000)))
    return 0;

  dest = ra_dest(as, ir, RSET_FPR);
  prev = ra_hintalloc(as, prevref, dest, RSET_FPR);
  allow = RSET_FPR;
  allow = rset_exclude(allow, dest);
  if (prev != dest)
    allow = rset_exclude(allow, prev);
  quarter = ra_alloc1(as, addk1, allow);
  fsum = ra_scratch(as, rset_exclude(allow, quarter));

  allow = RSET_GPR_NOB;
  rset_clear(allow, qhi);
  rset_clear(allow, qlo);
  idx = ra_alloc1_nobase(as, conv1->op1, allow, -292);
  ra_evictset(as, RID2RSET(qhi)|RID2RSET(qlo));
  ra_modified(as, qhi);
  ra_modified(as, qlo);
  allow = rset_exclude(RSET_GPR_NOB, idx);
  rset_clear(allow, qhi);
  rset_clear(allow, qlo);
  r1 = ra_scratch(as, allow);
  allow = rset_exclude(allow, r1);
  tmp = ra_scratch(as, allow);
  allow = rset_exclude(allow, tmp);
  r2 = ra_scratch(as, allow);
  allow = rset_exclude(allow, r2);
  sum = ra_scratch(as, allow);

  as->curins = (IRRef)(conv1 - as->ir);

  emit_u32(as, S390X_INS_RXE(S390XI_ADBR, dest, fsum));
  emit_u32(as, S390X_INS_RXE(S390XI_MDBR, fsum, quarter));
  emit_u32(as, S390X_INS_RXE(S390XI_CDFBR, fsum, sum));
  emit_u32(as, S390X_INS_RRF_M(S390XI_AGRK, sum, r2, r1));

  emit_u32(as, S390X_INS_RRF_M(S390XI_LOCGR, r2, CC_EQ, qhi));
  emit_u32(as, S390X_INS_RI(S390XI_CGHI, r2, 21));
  emit_u32(as, S390X_INS_RXE(S390XI_XGR, qhi, qhi));
  emit_u32(as, S390X_INS_RRF_M(S390XI_SGRK, r2, r2, tmp));
  emit_loadi(as, tmp, 21);
  emit_u32(as, S390X_INS_RXE(S390XI_SGR, r2, qhi));
  emit_u48_pad8(as, S390X_INS_RIL(S390XI_MSGFI, qhi, 21));
  emit_u32(as, S390X_INS_RXE(S390XI_MLGR, qhi, tmp));
  emit_loadu64(as, tmp, U64x(0c30c30c,30c30c31));
  emit_movrr(as, ir, qlo, r2);

  emit_u32(as, S390X_INS_RXE(S390XI_SGR, r1, qhi));
  emit_u48_pad8(as, S390X_INS_RIL(S390XI_MSGFI, qhi, 30));
  emit_u32(as, S390X_INS_RXE(S390XI_MLGR, qhi, tmp));
  emit_loadu64(as, tmp, U64x(08888888,88888889));
  emit_movrr(as, ir, qlo, r1);
  emit_u48_pad8(as, S390X_INS_RIE_D(S390XI_AGHIK, r2, r1, 1));
  emit_u32(as, S390X_INS_RI(S390XI_AGHI, r1, 1));
  emit_shiftimm(as, S390XI_SLLG, r1, idx, 2);

  if (dest != prev)
    emit_movrr(as, ir, dest, prev);
  return 1;
}

static int asm_s390x_fpmod_pair_sched(ASMState *as, IRIns *ir)
{
  IRIns *rem2, *accadd, *rem1, *x1, *x2, *mul1, *mul2;
  IRIns *floor1, *floor2, *div1, *div2, *conv1, *conv2, *subov;
  IRRef ref = (IRRef)(ir - as->ir);
  IRRef divk1, divk2, addk1, subk2, prevref;
  Reg dest, prev, idx, neg, x1r, x2r, q1, q2, d1, d2, k1, k2;
  RegSet allow;

  if (!(as->loopref && ref > as->loopref) || ir->o != IR_ADD ||
      !irt_isnum(ir->t) || irref_isk(ir->op1) || irref_isk(ir->op2))
    return 0;
  rem2 = IR(ir->op1);
  accadd = IR(ir->op2);
  if (accadd->o != IR_ADD || !irt_isnum(accadd->t) ||
      irref_isk(accadd->op1) || irref_isk(accadd->op2))
    return 0;
  rem1 = IR(accadd->op1);
  prevref = accadd->op2;
  if (!asm_s390x_fpmod_parts(as, rem1, &x1, &mul1, &floor1, &div1, &divk1) ||
      !asm_s390x_fpmod_parts(as, rem2, &x2, &mul2, &floor2, &div2, &divk2))
    return 0;
  if (x1->o != IR_ADD || !irt_isnum(x1->t) ||
      irref_isk(x1->op1) || !irref_isk(x1->op2) ||
      x2->o != IR_SUB || !irt_isnum(x2->t) ||
      irref_isk(x2->op1) || !irref_isk(x2->op2))
    return 0;
  conv1 = IR(x1->op1);
  conv2 = IR(x2->op1);
  if (conv1->o != IR_CONV || !irt_isnum(conv1->t) ||
      (IRType)(conv1->op2 & IRCONV_SRCMASK) != IRT_INT ||
      conv2->o != IR_CONV || !irt_isnum(conv2->t) ||
      (IRType)(conv2->op2 & IRCONV_SRCMASK) != IRT_INT ||
      irref_isk(conv2->op1))
    return 0;
  subov = IR(conv2->op1);
  if (subov->o != IR_SUBOV || !irt_isguard(subov->t) ||
      !irt_isinteger(subov->t) || !irref_isk(subov->op1) ||
      IR(subov->op1)->o != IR_KINT || IR(subov->op1)->i != 0 ||
      subov->op2 != conv1->op1)
    return 0;
  addk1 = x1->op2;
  subk2 = x2->op2;

  dest = ra_dest(as, ir, RSET_FPR);
  prev = ra_hintalloc(as, prevref, dest, RSET_FPR);
  allow = RSET_FPR;
  allow = rset_exclude(allow, dest);
  if (prev != dest)
    allow = rset_exclude(allow, prev);
  k1 = ra_alloc1(as, addk1, allow);
  allow = rset_exclude(allow, k1);
  k2 = ra_alloc1(as, subk2, allow);
  allow = rset_exclude(allow, k2);
  d1 = ra_alloc1(as, divk1, allow);
  allow = rset_exclude(allow, d1);
  d2 = ra_alloc1(as, divk2, allow);
  allow = rset_exclude(allow, d2);
  x1r = ra_scratch(as, allow);
  allow = rset_exclude(allow, x1r);
  x2r = ra_scratch(as, allow);
  allow = rset_exclude(allow, x2r);
  q1 = ra_scratch(as, allow);
  allow = rset_exclude(allow, q1);
  q2 = ra_scratch(as, allow);
  idx = ra_alloc1_nobase(as, conv1->op1, RSET_GPR_NOB, -290);
  neg = ra_scratch(as, rset_exclude(RSET_GPR_NOB, idx));

  as->curins = (IRRef)(conv1 - as->ir);
  emit_u32(as, S390X_INS_RXE(S390XI_ADBR, dest, x2r));
  emit_u32(as, S390X_INS_RXE(S390XI_ADBR, dest, x1r));
  emit_u32(as, S390X_INS_RXE(S390XI_SDBR, x2r, q2));
  emit_u32(as, S390X_INS_RXE(S390XI_SDBR, x1r, q1));
  emit_u32(as, S390X_INS_RXE(S390XI_MDBR, q2, d2));
  emit_u32(as, S390X_INS_RXE(S390XI_MDBR, q1, d1));
  emit_u32(as, S390X_INS_RRF_E(S390XI_FIDBRA, q2, 7, q2, 0));
  emit_u32(as, S390X_INS_RRF_E(S390XI_FIDBRA, q1, 7, q1, 0));
  emit_u32(as, S390X_INS_RXE(S390XI_DDBR, q2, d2));
  emit_u32(as, S390X_INS_RXE(S390XI_DDBR, q1, d1));
  emit_movrr(as, ir, q2, x2r);
  emit_movrr(as, ir, q1, x1r);
  emit_u32(as, S390X_INS_RXE(S390XI_SDBR, x2r, k2));
  emit_u32(as, S390X_INS_RXE(S390XI_ADBR, x1r, k1));
  emit_u32(as, S390X_INS_RXE(S390XI_CDFBR, x2r, neg));
  emit_u32(as, S390X_INS_RXE(S390XI_LCGFR, neg, idx));
  emit_u32(as, S390X_INS_RXE(S390XI_CDFBR, x1r, idx));
  if (dest != prev)
    emit_movrr(as, ir, dest, prev);
  return 1;
}

static int asm_s390x_div_loop_index_add_deferred(ASMState *as, IRIns *ir,
						 int32_t k)
{
  IRRef ref = (IRRef)(ir - as->ir);
  IRIns *fadd, *div, *nadd, *dadd, *conv, *le;
  if (k != 1 || ref <= REF_FIRST + 3 || ir + 1 >= IR(as->orignins) ||
      ir->o != IR_ADD || !irt_isinteger(ir->t) || !irref_isk(ir->op2))
    return 0;
  le = ir + 1;
  if (le->o != IR_LE || !irt_isguard(le->t) || le->op1 != ref)
    return 0;
  fadd = ir - 1;
  if (fadd->o != IR_ADD || !irt_isnum(fadd->t) || irref_isk(fadd->op1))
    return 0;
  div = IR(fadd->op1);
  if (div != ir - 2 || div->o != IR_DIV || !irt_isnum(div->t) ||
      irref_isk(div->op1) || irref_isk(div->op2))
    return 0;
  nadd = IR(div->op1);
  dadd = IR(div->op2);
  if (nadd->o != IR_ADD || dadd->o != IR_ADD ||
      !irt_isnum(nadd->t) || !irt_isnum(dadd->t) ||
      irref_isk(nadd->op1) || nadd->op1 != dadd->op1 ||
      !irref_isk(nadd->op2) || !irref_isk(dadd->op2))
    return 0;
  conv = IR(nadd->op1);
  return conv->o == IR_CONV && irt_isnum(conv->t) &&
	 (IRType)(conv->op2 & IRCONV_SRCMASK) == IRT_INT &&
	 conv->op1 == ir->op1;
}

static int asm_s390x_sqrt_loop_index_add_deferred(ASMState *as, IRIns *ir,
						  int32_t k)
{
  IRRef ref = (IRRef)(ir - as->ir);
  IRIns *fadd, *sqrtir, *xadd, *conv, *le;
  if (k != 1 || ref <= REF_FIRST + 3 || ir + 1 >= IR(as->orignins) ||
      ir->o != IR_ADD || !irt_isinteger(ir->t) || !irref_isk(ir->op2))
    return 0;
  le = ir + 1;
  if (le->o != IR_LE || !irt_isguard(le->t) || le->op1 != ref)
    return 0;
  fadd = ir - 1;
  if (fadd->o != IR_ADD || !irt_isnum(fadd->t) || irref_isk(fadd->op1))
    return 0;
  if (!irref_isk(fadd->op2) && IR(fadd->op2)->o == IR_ADD)
    return 0;
  sqrtir = IR(fadd->op1);
  if (sqrtir != ir - 2 || sqrtir->o != IR_FPMATH ||
      !irt_isnum(sqrtir->t) || sqrtir->op2 != IRFPM_SQRT ||
      irref_isk(sqrtir->op1))
    return 0;
  xadd = IR(sqrtir->op1);
  if (xadd->o != IR_ADD || !irt_isnum(xadd->t) ||
      irref_isk(xadd->op1) || !irref_isk(xadd->op2))
    return 0;
  conv = IR(xadd->op1);
  return conv->o == IR_CONV && irt_isnum(conv->t) &&
	 (IRType)(conv->op2 & IRCONV_SRCMASK) == IRT_INT &&
	 conv->op1 == ir->op1;
}

static void asm_s390x_guarded_int_rr32(ASMState *as, IRIns *ir, Reg dest,
				       Reg left, Reg right, uint32_t op)
{
  RegSet allow = RSET_GPR_NOB & ~RID2RSET(dest) & ~RID2RSET(right);
  RegSet sallow = allow & ~RID2RSET(left);
  Reg res = ra_scratch(as, sallow);
  IRRef pref = asm_s390x_guarded_ov_preserve_ref(as, ir);
  Reg preserve = (pref == ir->op2) ? right : left;
  if (dest != preserve &&
      asm_s390x_loop_phi_carry_in_dest(as, ir, dest, preserve)) {
    asm_guardcc(as, CC_OF);
    emit_u32(as, S390X_INS_RXE(S390XI_LGFR, dest, res));
    emit_u32(as, S390X_INS_RRF_M(op, res, right, left));
    return;
  }
  emit_u32(as, S390X_INS_RXE(S390XI_LGFR, dest, res));
  asm_guardcc(as, CC_OF);
  if (dest != preserve)
    emit_movrr(as, ir, dest, preserve);
  emit_u32(as, S390X_INS_RRF_M(op, res, right, left));
}

static void asm_add(ASMState *as, IRIns *ir)
{
  if (irt_isnum(ir->t)) {
    RegSet allow = RSET_FPR;
    Reg dest;
    Reg left, right;
    if (asm_s390x_fpmod_pair_intquarter_sched(as, ir))
      return;
    if (asm_s390x_fpmod_pair_sched(as, ir))
      return;
    if (!irref_isk(ir->op2) && ra_hasreg(IR(ir->op2)->r) &&
	IR(ir->op2)->r >= RID_MIN_FPR)
      allow = RID2RSET(IR(ir->op2)->r);
    dest = ra_dest(as, ir, allow);
    if (ir->op1 != ir->op2 && !irref_isk(ir->op1) && !irref_isk(ir->op2) &&
	(IR(ir->op1)->o == IR_ABS || IR(ir->op1)->o == IR_DIV ||
	 IR(ir->op1)->o == IR_FPMATH) &&
	!(ra_hasreg(IR(ir->op1)->r) && IR(ir->op1)->r == dest)) {
      left = ra_hintalloc(as, ir->op2, dest, RSET_FPR);
      right = ra_alloc1(as, ir->op1, rset_exclude(RSET_FPR, left));
      emit_u32(as, S390X_INS_RXE(S390XI_ADBR, dest, right));
      return;
    }
    left = ra_hintalloc(as, ir->op1, dest, RSET_FPR);
    right = ra_alloc1(as, ir->op2, rset_exclude(RSET_FPR, left));
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
    if (!asm_s390x_fpleft_intconv(as, ir->op1, dest, left))
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
  asm_s390x_addhome_log(as, ir);
  asm_s390x_low32home_log(as, "add", ir);
  if (asm_add_pack_u32_identity(as, ir, dest, bnorm))
    return;
  if (asm_add_bxor_mix_pos_loop_tail(as, ir, dest))
    return;
  left = ra_hintalloc(as, ir->op1, dest, RSET_GPR_NOB);
  if (irref_isk(ir->op2)) {
    int32_t k = (int32_t)asm_kintptr(as, ir->op2);
    if (checki16(k)) {
      asm_s390x_ir_log_addk(as, ir, ir->op1, ir->op2, dest, left, k);
      if (!irt_isguard(ir->t) &&
	  asm_s390x_div_loop_index_add_deferred(as, ir, k))
	return;
      if (!irt_isguard(ir->t) &&
	  asm_s390x_sqrt_loop_index_add_deferred(as, ir, k))
	return;
      if (irt_isguard(ir->t) && irt_isinteger(ir->t)) {
	RegSet allow = rset_exclude(RSET_GPR_NOB, dest);
	if (as->loopref && as->curins > as->loopref) {
	  int low32home = asm_s390x_guarded_addsub_op32home(as, ir->op1) &&
			  asm_s390x_guarded_addsub_can_stay_low32(as, ir);
	  asm_s390x_add_log(as, "addov_k_int_eq", ir, dest, left, RID_NONE, RID_NONE);
	  asm_s390x_guard_log(as, "addov_k_int_eq", ir,
			      low32home ? CC_OF : CC_NE, 0, k);
	  if (low32home && dest != left) {
	    asm_guardcc(as, CC_OF);
	    if (!asm_s390x_loop_phi_carry_in_dest(as, ir, dest, left))
	      emit_u32(as, S390X_INS_RRF_M(S390XI_LOCGR, dest, CC_OF, left));
	    emit_u48_pad8(as, S390X_INS_RIE_D(S390XI_AHIK, dest, left, k));
	  } else {
	    RegSet sallow = rset_exclude(allow, left);
	    Reg res = ra_scratch(as, sallow);
	    emit_movrr(as, ir, dest, res);
	    asm_guardcc(as, low32home ? CC_OF : CC_NE);
	    if (low32home) {
	      emit_u48_pad8(as, S390X_INS_RIE_D(S390XI_AHIK, res, left, k));
	    } else {
	      emit_u32(as, S390X_INS_RXE(S390XI_CGFR, res, res));
	      emit_u32(as, S390X_INS_RI(S390XI_AGHI, res, k));
	      emit_u32(as, S390X_INS_RXE(S390XI_LGFR, res, left));
	    }
	  }
	} else {
	  asm_s390x_add_log(as, "addov_k_int_eq", ir, dest, left, RID_NONE, RID_NONE);
	  asm_s390x_guard_log(as, "addov_k_int_eq", ir, CC_NE, 0, k);
	  asm_guardcc(as, CC_NE);
	  emit_u32(as, S390X_INS_RXE(S390XI_CGFR, dest, dest));
	  emit_u32(as, S390X_INS_RI(S390XI_AGHI, dest, k));
	  emit_u32(as, S390X_INS_RXE(S390XI_LGFR, dest, dest));
		}
	      } else {
		int suffix200add = asm_s390x_addk1_mix_suffix200(as, ir);
		int32_t emitk = suffix200add ? 256 : k;
		int low32home = as->loopref && as->curins > as->loopref &&
				(suffix200add ||
				 asm_s390x_addk1_bitop_loop_carry(as, ir) ||
				 (asm_s390x_plain_add_range_stripped(as, ir) &&
				  asm_s390x_plain_add_op32home(as, ir->op1) &&
				  asm_s390x_can_defer_plain_add_bnorm32(as, ir)));
		if (irt_isguard(ir->t)) {
		  asm_s390x_guard_log(as, "addov_k", ir, CC_OF, 0, k);
		  asm_guardcc(as, CC_OF);
		}
	if (bnorm && !low32home &&
		    !asm_s390x_addk_loop_result_normalized(as, ir, k) &&
		    !asm_s390x_can_defer_counter_add_bnorm32(as, ir))
		  asm_bnorm32(as, ir, dest);
		if (low32home && k == 1 && !irt_isguard(ir->t) && dest == left &&
		    suffix200add &&
		    asm_s390x_fill_loop_crj_never_taken(as,
		      S390X_INS_RR(S390XI_AR, dest,
			ra_allock(as, emitk, rset_exclude(RSET_GPR_NOB, left)))))
		  return;
		if (low32home && !irt_isguard(ir->t) && dest != left)
		  emit_u48_pad8(as, S390X_INS_RIE_D(S390XI_AHIK, dest, left, emitk));
		else if (!irt_isguard(ir->t) && dest != left)
		  emit_u48_pad8(as, S390X_INS_RIE_D(S390XI_AGHIK, dest, left, emitk));
		else
		  emit_u32(as, S390X_INS_RI(low32home ? S390XI_AHI :
					    S390XI_AGHI, dest, emitk));
	      }
	      if (dest != left && irt_isguard(ir->t) &&
		  !(as->loopref && as->curins > as->loopref && irt_isinteger(ir->t) &&
		    asm_s390x_guarded_addsub_op32home(as, ir->op1)))
	emit_movrr(as, ir, dest, left);
      return;
    }
    if (irt_isguard(ir->t) && irt_isinteger(ir->t) &&
	asm_s390x_guarded_addsub_op32home(as, ir->op1) &&
	asm_s390x_guarded_addsub_can_stay_low32(as, ir)) {
      asm_s390x_ir_log_addk(as, ir, ir->op1, ir->op2, dest, left, k);
      asm_s390x_guard_log(as, "addov_k_int32", ir, CC_NE, 0, k);
      asm_guardcc(as, CC_NE);
      emit_u32(as, S390X_INS_RXE(S390XI_CGFR, dest, dest));
      emit_u48_pad8(as, S390X_INS_RIL(S390XI_AGFI, dest, k));
      if (dest != left &&
	  !asm_s390x_loop_phi_carry_in_dest(as, ir, dest, left))
	emit_movrr(as, ir, dest, left);
      return;
	    }
	    if (!irt_isguard(ir->t)) {
	      int low32home = as->loopref && as->curins > as->loopref &&
			      (asm_s390x_addk_low32home_only(as, ir) ||
			       (asm_s390x_plain_add_range_stripped(as, ir) &&
				asm_s390x_plain_add_op32home(as, ir->op1) &&
				asm_s390x_can_defer_plain_add_bnorm32(as, ir)));
	      asm_s390x_ir_log_addk(as, ir, ir->op1, ir->op2, dest, left, k);
	      if (bnorm && !low32home &&
		  !asm_s390x_addk_loop_result_normalized(as, ir, k) &&
		  !asm_s390x_can_defer_counter_add_bnorm32(as, ir))
		asm_bnorm32(as, ir, dest);
	      emit_u48_pad8(as, S390X_INS_RIL(low32home ? S390XI_AFI :
					      S390XI_AGFI, dest, k));
	      if (dest != left)
		emit_movrr(as, ir, dest, left);
	      return;
    }
  }
  right = irref_isk(ir->op2) ?
	  ra_allock(as, asm_kintptr(as, ir->op2), rset_exclude(RSET_GPR_NOB, left)) :
	  ra_alloc1(as, ir->op2, rset_exclude(RSET_GPR_NOB, left));
  if (dest == right && dest != left) {
    Reg tmp = left;
    left = right;
    right = tmp;
  }
  if (irt_isguard(ir->t) && irt_isinteger(ir->t)) {
    RegSet allow = RSET_GPR_NOB & ~RID2RSET(dest) & ~RID2RSET(right);
    if (as->loopref && as->curins > as->loopref) {
      IRRef pref = asm_s390x_guarded_ov_preserve_ref(as, ir);
      Reg preserve = (pref == ir->op2) ? right : left;
      int low32home = asm_s390x_guarded_addsub_op32home(as, ir->op1) &&
		      asm_s390x_guarded_addsub_op32home(as, ir->op2) &&
		      asm_s390x_guarded_addsub_can_stay_low32(as, ir);
      asm_s390x_add_log(as, low32home ? "addov_rr_int_eq" :
			"addov_rr_int_ar", ir, dest, left, right, RID_NONE);
      asm_s390x_guard_log(as, low32home ? "addov_rr_int_eq" :
			  "addov_rr_int_ar", ir, CC_OF, 0,
			  (int)(ir->op2 - REF_BIAS));
      if (low32home && dest != preserve) {
	asm_guardcc(as, CC_OF);
	emit_u32(as, S390X_INS_RRF_M(S390XI_LOCGR, dest, CC_OF, preserve));
	emit_u32(as, S390X_INS_RRF_M(S390XI_ARK, dest, right, left));
      } else {
	if (low32home) {
	  RegSet sallow = allow & ~RID2RSET(left);
	  Reg res = ra_scratch(as, sallow);
	  emit_movrr(as, ir, dest, res);
	  asm_guardcc(as, CC_OF);
	  if (dest != preserve)
	    emit_movrr(as, ir, dest, preserve);
	  emit_u32(as, S390X_INS_RRF_M(S390XI_ARK, res, right, left));
	} else {
	  asm_s390x_guarded_int_rr32(as, ir, dest, left, right, S390XI_ARK);
	}
      }
    } else {
      asm_s390x_add_log(as, "addov_rr_int_eq", ir, dest, left, right, RID_NONE);
      asm_s390x_guard_log(as, "addov_rr_int_eq", ir, CC_NE, 0,
			  (int)(ir->op2 - REF_BIAS));
      asm_guardcc(as, CC_NE);
      emit_u32(as, S390X_INS_RXE(S390XI_CGFR, dest, dest));
      emit_u32(as, S390X_INS_RXE(S390XI_AGR, dest, right));
      emit_u32(as, S390X_INS_RXE(S390XI_LGFR, dest, dest));
      if (dest != left)
	emit_movrr(as, ir, dest, left);
    }
  } else {
    int low32home = as->loopref && as->curins > as->loopref &&
		    asm_s390x_plain_add_range_stripped(as, ir) &&
		    asm_s390x_plain_add_op32home(as, ir->op1) &&
		    asm_s390x_plain_add_op32home(as, ir->op2) &&
		    asm_s390x_can_defer_plain_add_bnorm32(as, ir);
    if (irt_isguard(ir->t)) {
      asm_s390x_guard_log(as, "addov_rr", ir, CC_OF, 0,
			  (int)(ir->op2 - REF_BIAS));
      asm_guardcc(as, CC_OF);
    }
    if (bnorm && !low32home)
      asm_bnorm32(as, ir, dest);
    if (low32home)
      emit_u32(as, S390X_INS_RRF_M(S390XI_ARK, dest, right, left));
    else if (dest == left)
      emit_u32(as, S390X_INS_RXE(S390XI_AGR, dest, right));
    else
      emit_u32(as, S390X_INS_RRF_M(S390XI_AGRK, dest, right, left));
  }
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
static void asm_prof(ASMState *as, IRIns *ir)
{
  Reg g = ra_scratch(as, RSET_GPR_NOB);
  UNUSED(ir);
  asm_guardcc(as, CC_NE);
  emit_u32(as, S390X_INS_SI(S390XI_TM, g,
			    (int32_t)offsetof(global_State, hookmask),
			    HOOK_PROFILE));
  emit_addptr(as, g, GG_DISP2G);
  if (g != RID_DISPATCH)
    emit_u32(as, S390X_INS_RXE(S390XI_LGR, g, RID_DISPATCH));
}

static void asm_comp(ASMState *as, IRIns *ir)
{
  if (irt_isfp(ir->t))
    asm_s390x_nyi_ir(as, ir);
  else
    asm_intcomp(as, ir);
}

static int asm_s390x_ne_zero_fused_by_subov(ASMState *as, IRIns *ir)
{
  IRIns *next;
  if (ir->o != IR_NE || !irt_isguard(ir->t) || !irt_isinteger(ir->t) ||
      !irref_isk(ir->op2) || IR(ir->op2)->o != IR_KINT ||
      IR(ir->op2)->i != 0 || ir + 1 >= IR(as->orignins))
    return 0;
  next = ir + 1;
  return next->o == IR_SUBOV && irt_isguard(next->t) &&
	 irt_isinteger(next->t) && next->op2 == ir->op1 &&
	 irref_isk(next->op1) && IR(next->op1)->o == IR_KINT &&
	 IR(next->op1)->i == 0;
}

static int asm_s390x_subov_zero_absorbed_by_numconv(ASMState *as, IRIns *ir)
{
  IRRef ref = (IRRef)(ir - as->ir);
  IRIns *next;
  if (ir->o != IR_SUBOV || !irt_isguard(ir->t) || !irt_isinteger(ir->t) ||
      !irref_isk(ir->op1) || IR(ir->op1)->o != IR_KINT ||
      IR(ir->op1)->i != 0 || irref_isk(ir->op2) ||
      ir + 1 >= IR(as->orignins))
    return 0;
  next = ir + 1;
  return next->o == IR_CONV && next->op1 == ref && irt_isnum(next->t) &&
	 (IRType)(next->op2 & IRCONV_SRCMASK) == IRT_INT;
}

static int asm_s390x_abs_sign_guard_elided(ASMState *as, IRIns *ir, IRIns *lir)
{
  IRIns *ne, *subov, *conv, *absir;
  IRRef subref, convref;
  if (ir->o != IR_EQ || !irt_isguard(ir->t) || !irt_isinteger(ir->t) ||
      !irref_isk(ir->op2) || IR(ir->op2)->o != IR_KINT ||
      IR(ir->op2)->i != 0 || lir->o != IR_BAND || !irref_isk(lir->op2) ||
      IR(lir->op2)->o != IR_KINT || ir + 4 >= IR(as->orignins))
    return 0;
  ne = ir + 1;
  subov = ir + 2;
  conv = ir + 3;
  absir = ir + 4;
  subref = (IRRef)(subov - as->ir);
  convref = (IRRef)(conv - as->ir);
  return ne->o == IR_NE && irt_isguard(ne->t) && irt_isinteger(ne->t) &&
	 ne->op1 == lir->op1 && irref_isk(ne->op2) &&
	 IR(ne->op2)->o == IR_KINT && IR(ne->op2)->i == 0 &&
	 subov->o == IR_SUBOV && irt_isguard(subov->t) &&
	 irt_isinteger(subov->t) && subov->op2 == lir->op1 &&
	 irref_isk(subov->op1) && IR(subov->op1)->o == IR_KINT &&
	 IR(subov->op1)->i == 0 && conv->o == IR_CONV &&
	 conv->op1 == subref && irt_isnum(conv->t) &&
	 (IRType)(conv->op2 & IRCONV_SRCMASK) == IRT_INT &&
	 absir->o == IR_ABS && absir->op1 == convref;
}

static int asm_s390x_abs_select_guard_elided(ASMState *as, IRIns *ir, IRIns *lir)
{
  IRRef ref = (IRRef)(ir - as->ir);
  IRRef idxref;
  IRIns *conv, *absir, *add, *idxadd, *le;
  IRRef convref, absref;
  int addofs;
  if (!(as->loopref && ref > as->loopref) ||
      ir->o != IR_NE || !irt_isguard(ir->t) || !irt_isinteger(ir->t) ||
      !irref_isk(ir->op2) || IR(ir->op2)->o != IR_KINT ||
      IR(ir->op2)->i != 0 || lir->o != IR_BAND || !irref_isk(lir->op2) ||
      IR(lir->op2)->o != IR_KINT || IR(lir->op2)->i != 1 ||
      ir + 5 >= IR(as->orignins))
    return 0;
  idxref = lir->op1;
  conv = ir + 1;
  if (conv->o != IR_CONV || conv->op1 != idxref || !irt_isnum(conv->t) ||
      (IRType)(conv->op2 & IRCONV_SRCMASK) != IRT_INT)
    return 0;
  convref = (IRRef)(conv - as->ir);
  absir = ir + 2;
  if (absir->o != IR_ABS || absir->op1 != convref || !irt_isnum(absir->t))
    return 0;
  absref = (IRRef)(absir - as->ir);
  addofs = 3;
  if ((ir + addofs)->o == IR_CONV && irt_isguard((ir + addofs)->t))
    addofs++;
  if (ir + addofs + 2 >= IR(as->orignins))
    return 0;
  add = ir + addofs;
  if (add->o != IR_ADD || !irt_isnum(add->t) ||
      (add->op1 != absref && add->op2 != absref))
    return 0;
  idxadd = add + 1;
  if (idxadd->o != IR_ADD || !irt_isinteger(idxadd->t) ||
      idxadd->op1 != idxref || !irref_isk(idxadd->op2) ||
      IR(idxadd->op2)->o != IR_KINT || IR(idxadd->op2)->i != 1)
    return 0;
  le = idxadd + 1;
  return le->o == IR_LE && irt_isguard(le->t) &&
	 le->op1 == (IRRef)(idxadd - as->ir) && ra_used(add);
}

static void asm_retf(ASMState *as, IRIns *ir)
{
  Reg base = ra_alloc1(as, REF_BASE, RSET_GPR_NOB);
  Reg tmp = ra_scratch(as, rset_exclude(RSET_GPR_NOB, base));
  Reg expected = ra_allock(as, (intptr_t)ir_kptr(IR(ir->op2)),
			   rset_exclude(rset_exclude(RSET_GPR_NOB, tmp), base));
  void *pc = ir_kptr(IR(ir->op2));
  int32_t delta = 1+LJ_FR2+bc_a(*((const BCIns *)pc - 1));
  if (0) {
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
  Reg left, right, cmp_left, cmp_right;
  IRIns *lir, *rir = NULL;
  int cmp32s;
  if (irt_isfp(ir->t)) {
    asm_s390x_nyi_ir(as, ir);
    return;
  }
  if (asm_s390x_ne_zero_fused_by_subov(as, ir))
    return;
  lir = IR(ir->op1);
  if (!irref_isk(ir->op2))
    rir = IR(ir->op2);
  if (asm_s390x_abs_sign_guard_elided(as, ir, lir))
    return;
  if (asm_s390x_abs_select_guard_elided(as, ir, lir))
    return;
  if ((ir->o == IR_EQ || ir->o == IR_NE) && irref_isk(ir->op2) &&
      IR(ir->op2)->o == IR_KINT && IR(ir->op2)->i == 0 &&
      lir->o == IR_BAND && irref_isk(lir->op2) &&
      IR(lir->op2)->o == IR_KINT &&
      ((uint32_t)IR(lir->op2)->i & ((uint32_t)IR(lir->op2)->i - 1u)) == 0 &&
      (uint32_t)IR(lir->op2)->i <= 0xffffu && !ra_used(lir)) {
    left = ra_alloc1_nobase(as, lir->op1, RSET_GPR_NOB, -285);
    asm_s390x_low32cmp_log(as, "equal_bandmask", ir->o, ir->op1, ir->op2,
			   lir, rir, 0, 1);
    asm_guardcc(as, ir->o == IR_EQ ? CC_OF : CC_EQ);
    emit_u32(as, S390X_INS_RI(S390XI_TMLL, left, IR(lir->op2)->i));
    return;
  }
  left = ra_alloc1_nobase(as, ir->op1, RSET_GPR_NOB, -203);
  cmp32s = irt_isinteger(ir->t);
  asm_s390x_low32cmp_log(as, "equal", ir->o, ir->op1, ir->op2, lir, rir, 0, 0);
  if (cmp32s && irref_isk(ir->op2) && IR(ir->op2)->o == IR_KINT) {
    int cc = ir->o == IR_EQ ? CC_NE : CC_EQ;
    asm_guardcc(as, cc);
    if (asm_s390x_guard_cij_small(as, cc, left, IR(ir->op2)->i, 0))
      return;
    if (IR(ir->op2)->i == 0)
      emit_u16_pad4(as, S390X_INS_RR(S390XI_LTR, left, left));
    else
      emit_u48_pad8(as, S390X_INS_RIL(S390XI_CFI, left, IR(ir->op2)->i));
    return;
  }
  if (irref_isk(ir->op2))
    right = ra_allock(as, asm_kintptr(as, ir->op2),
		      rset_exclude(RSET_GPR_NOB, left));
  else
    right = ra_alloc1_nobase(as, ir->op2, rset_exclude(RSET_GPR_NOB, left), -204);
  if (cmp32s) {
    asm_guardcc(as, ir->o == IR_EQ ? CC_NE : CC_EQ);
    emit_u16_pad4(as, S390X_INS_RR(S390XI_CR, left, right));
    return;
  }
  cmp_left = left;
  cmp_right = right;
  asm_guardcc(as, ir->o == IR_EQ ? CC_NE : CC_EQ);
  emit_u32(as, S390X_INS_RXE(S390XI_CGR, cmp_left, cmp_right));
}
static void asm_bnorm32(ASMState *as, IRIns *ir, Reg dest)
{
  if (asm_s390x_is_bitop_op(ir->o))
    asm_s390x_low32home_log(as, "bnorm", ir);
  asm_s390x_bnorm_log(as, ir, dest);
  if (asm_s390x_can_defer_bnorm32(as, ir))
    return;
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
  int low32logic = asm_s390x_can_use_low32_logic_op(as, ir);
  asm_s390x_bitop_log(as, "logic", ir, dest, left, right, irref_isk(ir->op2));
  if (low32logic) {
    asm_bnorm32(as, ir, dest);
    if (op == S390XI_NGR)
      emit_u32(as, S390X_INS_RRF_M(S390XI_NRK, dest, right, left));
    else if (op == S390XI_OGR)
      emit_u32(as, S390X_INS_RRF_M(S390XI_ORK, dest, right, left));
    else
      emit_u32(as, S390X_INS_RRF_M(S390XI_XRK, dest, right, left));
    return;
  }
  asm_bnorm32(as, ir, dest);
  if (dest == left) {
    emit_u32(as, S390X_INS_RXE(op, dest, right));
  } else if (op == S390XI_NGR) {
    emit_u32(as, S390X_INS_RRF_M(S390XI_NGRK, dest, right, left));
  } else if (op == S390XI_OGR) {
    emit_u32(as, S390X_INS_RRF_M(S390XI_OGRK, dest, right, left));
  } else {
    emit_u32(as, S390X_INS_RRF_M(S390XI_XGRK, dest, right, left));
  }
}

static void asm_bnot(ASMState *as, IRIns *ir)
{
  Reg left = ra_alloc1_nobase(as, ir->op1, RSET_GPR_NOB, -234);
  Reg right = ra_allock(as, -1, rset_exclude(RSET_GPR_NOB, left));
  Reg dest = ra_dest_nobase(as, ir, rset_exclude(RSET_GPR_NOB, right), -233);
  int low32logic = asm_s390x_can_defer_bnorm32(as, ir);
  asm_s390x_bitop_log(as, "bnot", ir, dest, left, right, 0);
  asm_bnorm32(as, ir, dest);
  if (low32logic)
    emit_u32(as, S390X_INS_RRF_M(S390XI_XRK, dest, right, left));
  else if (dest == left)
    emit_u32(as, S390X_INS_RXE(S390XI_XGR, dest, right));
  else
    emit_u32(as, S390X_INS_RRF_M(S390XI_XGRK, dest, right, left));
}

static void asm_bswap(ASMState *as, IRIns *ir)
{
  Reg dest = ra_dest_nobase(as, ir, RSET_GPR_NOB, -235);
  Reg left = ra_alloc1_nobase(as, ir->op1, RSET_GPR_NOB, -236);
  int guard_nonnegative = !irref_isk(ir->op1);
  asm_s390x_bitop_log(as, "bswap", ir, dest, left, RID_NONE, 0);
  if (!irt_is64(ir->t)) {
    asm_bnorm32(as, ir, dest);
    emit_u32(as, S390X_INS_RXE(S390XI_LRVR, dest, dest));
    if (guard_nonnegative) {
      asm_guardcc(as, CC_LT);
      emit_u16_pad4(as, S390X_INS_RR(S390XI_LTR, dest, dest));
    }
    emit_u32(as, S390X_INS_RXE(S390XI_LGFR, dest, dest));
    if (dest != left)
      emit_movrr(as, ir, dest, left);
  } else {
    emit_u32(as, S390X_INS_RXE(S390XI_LRVGR, dest, left));
  }
}

static int asm_s390x_mod_step(ASMState *as, IRIns *ir);

static void asm_band(ASMState *as, IRIns *ir)
{
  if (asm_s390x_mod_step(as, ir))
    return;
  if (irref_isk(ir->op2) && IR(ir->op2)->o == IR_KINT &&
      (IR(ir->op2)->i == 0xff || IR(ir->op2)->i == 0xffff)) {
    Reg left = ra_alloc1_nobase(as, ir->op1, RSET_GPR_NOB, -231);
    Reg dest = ra_dest_nobase(as, ir, RSET_GPR_NOB, -230);
    uint32_t op = IR(ir->op2)->i == 0xff ? S390XI_LLGCR : S390XI_LLGHR;
    asm_s390x_bitop_log(as, "band_extract", ir, dest, left, RID_NONE, 1);
    asm_bnorm32(as, ir, dest);
    emit_u32(as, S390X_INS_RXE(op, dest, left));
    return;
  }
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
    asm_s390x_bitop_log(as, "shiftk", ir, dest, left, RID_NONE, 1);
    if (op == S390XI_SRLK && asm_s390x_can_defer_bnorm32(as, ir)) {
      emit_u48_pad8(as, S390X_INS_RSYI(S390XI_SRLK, dest, left, sh));
      return;
    }
    if (op == S390XI_SRLK) {
      emit_shiftimm(as, S390XI_SRLG, dest, dest, sh);
      emit_u32(as, S390X_INS_RXE(S390XI_LLGFR, dest, left));
      return;
    }
    if (op == S390XI_SRAK && IR(ir->op1)->o == IR_NEG &&
	asm_s390x_can_defer_bnorm32(as, ir)) {
      if (dest == left)
	emit_u32(as, S390X_INS_RX(S390XI_SRA, dest, 0, 0, sh));
      else
	emit_u48_pad8(as, S390X_INS_RSYI(S390XI_SRAK, dest, left, sh));
      return;
    }
    immop = (op == S390XI_SLLK) ? S390XI_SLLG : S390XI_SRAG;
    asm_bnorm32(as, ir, dest);
    emit_shiftimm(as, immop, dest, left, sh);
    return;
  } else {
    Reg left = ra_alloc1_nobase(as, ir->op1, RSET_GPR_NOB, -239);
    Reg right = ra_alloc1_nobase(as, ir->op2, rset_exclude(RSET_GPR_NOB, left), -238);
    Reg dest = ra_dest_nobase(as, ir, rset_exclude(RSET_GPR_NOB, right), -237);
    asm_s390x_bitop_log(as, "shift", ir, dest, left, right, 0);
    asm_bnorm32(as, ir, dest);
    emit_u48_pad8(as, S390X_INS_RSYB(op, dest, left, right, 0));
    if (op == S390XI_SRLK) {
      emit_u32(as, S390X_INS_RXE(S390XI_LLGFR, dest, left));
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
    asm_s390x_bitop_log(as, rightrot ? "brork" : "brolk", ir, dest, left,
			RID_NONE, 1);
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
  asm_s390x_bitop_log(as, rightrot ? "bror" : "brol", ir, dest, left, right, 0);
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

static IRRef asm_s390x_mod_step_ref(ASMState *as, IRIns *ir, int32_t *kp)
{
  IRIns *add1, *mulk, *shr, *addbias, *mod;
  IRRef ref;
  int32_t k, shift;

  if (ir->o == IR_BAND && irt_isint(ir->t) &&
      !irref_isk(ir->op1) && !irref_isk(ir->op2)) {
    add1 = IR(ir->op1);
    shr = IR(ir->op2);
    if (add1->o != IR_ADD || !irref_isk(add1->op2) ||
	IR(add1->op2)->o != IR_KINT || IR(add1->op2)->i != 1 ||
	shr->o != IR_BSAR || !irref_isk(shr->op2) ||
	IR(shr->op2)->o != IR_KINT || IR(shr->op2)->i != 31 ||
	irref_isk(shr->op1))
      return REF_NIL;
    ref = add1->op1;
    if (irref_isk(ref))
      return REF_NIL;
    addbias = IR(shr->op1);
    if (addbias->o != IR_ADD || addbias->op1 != ref ||
	!irref_isk(addbias->op2) || IR(addbias->op2)->o != IR_KINT)
      return REF_NIL;
    k = 1 - IR(addbias->op2)->i;
    if (k <= 0 || !checki16(k))
      return REF_NIL;
    mod = IR(ref);
    if (mod->o != IR_MOD || !irref_isk(mod->op2) ||
	IR(mod->op2)->o != IR_KINT || IR(mod->op2)->i != k)
      return REF_NIL;
    *kp = k;
    UNUSED(as);
    return ref;
  }

  if (ir->o != IR_SUB || !irt_isint(ir->t) ||
      irref_isk(ir->op1) || irref_isk(ir->op2))
    return REF_NIL;

  add1 = IR(ir->op1);
  mulk = IR(ir->op2);
  if (add1->o != IR_ADD || !irref_isk(add1->op2) ||
      IR(add1->op2)->o != IR_KINT || IR(add1->op2)->i != 1 ||
      mulk->o != IR_MUL || !irref_isk(mulk->op2) ||
      IR(mulk->op2)->o != IR_KINT || irref_isk(mulk->op1))
    return REF_NIL;
  k = IR(mulk->op2)->i;
  if (k <= 1 || !checki16(k))
    return REF_NIL;

  shr = IR(mulk->op1);
  if (shr->o != IR_BSHR || !irref_isk(shr->op2) ||
      IR(shr->op2)->o != IR_KINT || irref_isk(shr->op1))
    return REF_NIL;
  shift = IR(shr->op2)->i;
  if (shift <= 0 || shift >= 32)
    return REF_NIL;

  addbias = IR(shr->op1);
  if (addbias->o != IR_ADD || !irref_isk(addbias->op2) ||
      IR(addbias->op2)->o != IR_KINT ||
      IR(addbias->op2)->i != k - 1 ||
      add1->op1 != addbias->op1)
    return REF_NIL;
  if ((1u << (shift - 1)) != (uint32_t)(k - 1))
    return REF_NIL;

  ref = add1->op1;
  if (irref_isk(ref))
    return REF_NIL;
  mod = IR(ref);
  if (mod->o != IR_MOD || !irref_isk(mod->op2) ||
      IR(mod->op2)->o != IR_KINT || IR(mod->op2)->i != k)
    return REF_NIL;
  *kp = k;
  UNUSED(as);
  return ref;
}

static int asm_s390x_centered_mod17_abs_value_ref(ASMState *as, IRRef ref)
{
  IRIns *x, *mod;
  int32_t k;
  IRRef modref;

  if (irref_isk(ref))
    return 0;
  x = IR(ref);
  /* The lower-frame lua_abs loop produces x = (i % 17) - 8.  Once the modulo
  ** input is proven nonnegative, x is bounded to [-8, 8], so the following
  ** SUBOV 0-x diamond can be lowered as branchless abs without an overflow
  ** exit. Keep this as a semantic range proof, not a generic abs rewrite. */
  if (x->o != IR_SUBOV || !irt_isguard(x->t) || !irt_isinteger(x->t) ||
      irref_isk(x->op1) || !irref_isk(x->op2) ||
      IR(x->op2)->o != IR_KINT || IR(x->op2)->i != 8)
    return 0;

  mod = IR(x->op1);
  if (mod->o == IR_MOD && !irref_isk(mod->op1) && irref_isk(mod->op2) &&
      IR(mod->op2)->o == IR_KINT && IR(mod->op2)->i == 17)
    return asm_s390x_mod_operand_nonnegative(as, mod->op1);

  modref = asm_s390x_mod_step_ref(as, mod, &k);
  if (modref == REF_NIL || k != 17)
    return 0;
  mod = IR(modref);
  return mod->o == IR_MOD && !irref_isk(mod->op1) &&
	 irref_isk(mod->op2) && IR(mod->op2)->o == IR_KINT &&
	 IR(mod->op2)->i == 17 &&
	 asm_s390x_mod_operand_nonnegative(as, mod->op1);
}

static int asm_s390x_centered_mod17_abs_tail(ASMState *as, IRIns *lt,
					     IRRef xref, IRIns *conv)
{
  IRIns *ne, *subov;
  IRRef subref;

  if (lt + 3 >= IR(as->orignins))
    return 0;
  ne = lt + 1;
  subov = lt + 2;
  subref = (IRRef)(subov - as->ir);
  return lt->o == IR_LT && irt_isguard(lt->t) && irt_isinteger(lt->t) &&
	 lt->op1 == xref && irref_isk(lt->op2) &&
	 IR(lt->op2)->o == IR_KINT && IR(lt->op2)->i == 0 &&
	 ne->o == IR_NE && irt_isguard(ne->t) && irt_isinteger(ne->t) &&
	 ne->op1 == xref && irref_isk(ne->op2) &&
	 IR(ne->op2)->o == IR_KINT && IR(ne->op2)->i == 0 &&
	 subov->o == IR_SUBOV && irt_isguard(subov->t) &&
	 irt_isinteger(subov->t) && subov->op2 == xref &&
	 irref_isk(subov->op1) && IR(subov->op1)->o == IR_KINT &&
	 IR(subov->op1)->i == 0 &&
	 conv->o == IR_CONV && conv->op1 == subref && irt_isnum(conv->t) &&
	 (IRType)(conv->op2 & IRCONV_SRCMASK) == IRT_INT &&
	 asm_s390x_centered_mod17_abs_value_ref(as, xref);
}

static int asm_s390x_centered_mod17_abs_lt_guard_elided(ASMState *as,
							IRIns *ir, IROp op,
							IRRef lref, IRRef rref)
{
  if (op != IR_LT || irref_isk(lref) || !irref_isk(rref) ||
      IR(rref)->o != IR_KINT || IR(rref)->i != 0)
    return 0;
  return asm_s390x_centered_mod17_abs_tail(as, ir, lref, ir + 3);
}

static int asm_s390x_centered_mod17_abs_conv(ASMState *as, IRIns *ir,
					     IRIns *subov)
{
  if (subov <= IR(REF_FIRST + 2) || irref_isk(subov->op2))
    return 0;
  return asm_s390x_centered_mod17_abs_tail(as, subov - 2, subov->op2, ir);
}

static int asm_s390x_mod_step(ASMState *as, IRIns *ir)
{
  int32_t k;
  IRRef ref = asm_s390x_mod_step_ref(as, ir, &k);
  Reg dest, left, zero;

  if (ref == REF_NIL)
    return 0;

  dest = ra_dest_nobase(as, ir, RSET_GPR_NOB, -283);
  left = ra_hintalloc_nobase(as, ref, dest, RSET_GPR_NOB, -284);
  zero = ra_scratch(as, rset_exclude(rset_exclude(RSET_GPR_NOB, dest), left));

  emit_u32(as, S390X_INS_RRF_M(S390XI_LOCGR, dest, CC_EQ, zero));
  emit_u32(as, S390X_INS_RI(S390XI_CGHI, dest, k));
  emit_u32(as, S390X_INS_RI(S390XI_AGHI, dest, 1));
  emit_u32(as, S390X_INS_RXE(S390XI_XGR, zero, zero));
  if (dest != left)
    emit_movrr(as, ir, dest, left);
  return 1;
}

static IRRef asm_s390x_mod_value_step_ref(ASMState *as, IRIns *ir,
					  int32_t *kp)
{
  IRIns *add1, *mulk, *shr, *addbias, *value, *mod;
  IRRef ref;
  int32_t k, shift;

  if (ir->o != IR_SUB || !irt_isint(ir->t) ||
      irref_isk(ir->op1) || irref_isk(ir->op2))
    return REF_NIL;

  add1 = IR(ir->op1);
  mulk = IR(ir->op2);
  if (add1->o != IR_ADD || !irref_isk(add1->op2) ||
      IR(add1->op2)->o != IR_KINT || IR(add1->op2)->i != 1 ||
      mulk->o != IR_MUL || !irref_isk(mulk->op2) ||
      IR(mulk->op2)->o != IR_KINT || irref_isk(mulk->op1))
    return REF_NIL;
  k = IR(mulk->op2)->i;
  if (k <= 1 || k >= 32767)
    return REF_NIL;

  shr = IR(mulk->op1);
  if (shr->o != IR_BSHR || !irref_isk(shr->op2) ||
      IR(shr->op2)->o != IR_KINT || irref_isk(shr->op1))
    return REF_NIL;
  shift = IR(shr->op2)->i;
  if (shift <= 0 || shift >= 32)
    return REF_NIL;

  addbias = IR(shr->op1);
  if (addbias->o != IR_ADD || !irref_isk(addbias->op2) ||
      IR(addbias->op2)->o != IR_KINT ||
      IR(addbias->op2)->i != k - 2 ||
      add1->op1 != addbias->op1)
    return REF_NIL;
  if ((1u << (shift - 1)) != (uint32_t)(k - 1))
    return REF_NIL;

  ref = add1->op1;
  if (irref_isk(ref))
    return REF_NIL;
  value = IR(ref);
  if (value->o != IR_ADD || !irref_isk(value->op2) ||
      IR(value->op2)->o != IR_KINT || IR(value->op2)->i != 1 ||
      irref_isk(value->op1))
    return REF_NIL;
  mod = IR(value->op1);
  if (mod->o != IR_MOD || !irref_isk(mod->op2) ||
      IR(mod->op2)->o != IR_KINT || IR(mod->op2)->i != k)
    return REF_NIL;
  *kp = k;
  UNUSED(as);
  return ref;
}

static int asm_s390x_mod_value_step(ASMState *as, IRIns *ir)
{
  int32_t k;
  IRRef ref = asm_s390x_mod_value_step_ref(as, ir, &k);
  Reg dest, left, one;

  if (ref == REF_NIL)
    return 0;

  dest = ra_dest_nobase(as, ir, RSET_GPR_NOB, -285);
  left = ra_hintalloc_nobase(as, ref, dest, RSET_GPR_NOB, -286);
  one = ra_scratch(as, rset_exclude(rset_exclude(RSET_GPR_NOB, dest), left));

  emit_u32(as, S390X_INS_RRF_M(S390XI_LOCGR, dest, CC_EQ, one));
  emit_u32(as, S390X_INS_RI(S390XI_CGHI, dest, k + 1));
  emit_u32(as, S390X_INS_RI(S390XI_AGHI, dest, 1));
  emit_u32(as, S390X_INS_RI(S390XI_LGHI, one, 1));
  if (dest != left)
    emit_movrr(as, ir, dest, left);
  return 1;
}

static int asm_s390x_minmax_index_subov_guard_elided(ASMState *as, IRIns *ir)
{
  IRRef ref = (IRRef)(ir - as->ir);
  IRIns *left, *idx, *next, *base, *idxadd, *le, *stop;

  if (!(as->loopref && ref > as->loopref) ||
      ir->o != IR_SUBOV || !irt_isguard(ir->t) || !irt_isinteger(ir->t) ||
      irref_isk(ir->op1) || irref_isk(ir->op2) ||
      ir + 4 >= IR(as->orignins))
    return 0;
  left = IR(ir->op1);
  idx = IR(ir->op2);
  next = ir + 1;
  if ((next->o != IR_MIN && next->o != IR_MAX) ||
      next->op1 != ir->op2 || next->op2 != ref)
    return 0;
  idxadd = ir + 3;
  le = ir + 4;
  if (idxadd->o != IR_ADD || !irt_isinteger(idxadd->t) ||
      idxadd->op1 != ir->op2 || !irref_isk(idxadd->op2) ||
      IR(idxadd->op2)->o != IR_KINT || IR(idxadd->op2)->i != 1 ||
      le->o != IR_LE || !irt_isguard(le->t) ||
      le->op1 != (IRRef)(idxadd - as->ir) || irref_isk(le->op2))
    return 0;
  if (left->o != IR_ADDOV || !irt_isguard(left->t) ||
      !irt_isinteger(left->t) || !irref_isk(left->op2) ||
      IR(left->op2)->o != IR_KINT || IR(left->op2)->i != 1)
    return 0;
  base = IR(left->op1);
  stop = IR(le->op2);
  if (base->o != IR_SLOAD || stop->o != IR_SLOAD ||
      base->op1 != 2 || stop->op1 != 5)
    return 0;
  return asm_s390x_int_result_normalized(idx);
}

static void asm_sub(ASMState *as, IRIns *ir)
{
  if (irt_isnum(ir->t)) {
    Reg dest = ra_dest(as, ir, RSET_FPR);
    Reg left;
    Reg right;
    left = ra_hintalloc(as, ir->op1, dest, RSET_FPR);
    right = ra_alloc1(as, ir->op2, rset_exclude(RSET_FPR, dest));
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

  if (asm_s390x_mod_value_step(as, ir) || asm_s390x_mod_step(as, ir))
    return;

  if (irt_isguard(ir->t) && irt_isinteger(ir->t) &&
      irref_isk(ir->op1) && IR(ir->op1)->o == IR_KINT &&
      IR(ir->op1)->i == 0 && !irref_isk(ir->op2)) {
    int fuse_ne_zero = ir > IR(REF_FIRST) &&
		       asm_s390x_ne_zero_fused_by_subov(as, ir - 1);
    if (asm_s390x_subov_zero_absorbed_by_numconv(as, ir))
      return;
    dest = ra_dest_nobase(as, ir, RSET_GPR_NOB, -283);
    left = ra_hintalloc_nobase(as, ir->op2, dest, RSET_GPR_NOB, -284);
    asm_s390x_guard_log(as, "subov_zero_lcgfr", ir,
			fuse_ne_zero ? (CC_EQ|CC_OF) : CC_OF, 0,
			(int)(ir->op2 - REF_BIAS));
    asm_guardcc(as, fuse_ne_zero ? (CC_EQ|CC_OF) : CC_OF);
    emit_u32(as, S390X_INS_RXE(S390XI_LCGFR, dest, left));
    return;
  }

  if (irref_isk(ir->op2)) {
    int32_t k = (int32_t)asm_kintptr(as, ir->op2);
    if (k != INT32_MIN && checki16(-k)) {
      dest = ra_dest_nobase(as, ir, asm_s390x_dest_gprset(ir->t), -261);
      left = ra_hintalloc(as, ir->op1, dest, RSET_GPR_NOB);
      if (irt_isguard(ir->t) && irt_isinteger(ir->t)) {
	RegSet allow = rset_exclude(RSET_GPR_NOB, dest);
	if (as->loopref && as->curins > as->loopref) {
	  int low32home = asm_s390x_guarded_addsub_op32home(as, ir->op1) &&
			  asm_s390x_guarded_addsub_can_stay_low32(as, ir);
	  asm_s390x_add_log(as, "subov_k_int_eq", ir, dest, left, RID_NONE, RID_NONE);
	  asm_s390x_guard_log(as, "subov_k_int_eq", ir,
			      low32home ? CC_OF : CC_NE, 0, -k);
	  if (low32home && dest != left) {
	    asm_guardcc(as, CC_OF);
	    if (!asm_s390x_loop_phi_carry_in_dest(as, ir, dest, left))
	      emit_u32(as, S390X_INS_RRF_M(S390XI_LOCGR, dest, CC_OF, left));
	    emit_u48_pad8(as, S390X_INS_RIE_D(S390XI_AHIK, dest, left, -k));
	  } else {
	    RegSet sallow = rset_exclude(allow, left);
	    Reg res = ra_scratch(as, sallow);
	    emit_movrr(as, ir, dest, res);
	    asm_guardcc(as, low32home ? CC_OF : CC_NE);
	    if (low32home) {
	      emit_u48_pad8(as, S390X_INS_RIE_D(S390XI_AHIK, res, left, -k));
	    } else {
	      emit_u32(as, S390X_INS_RXE(S390XI_CGFR, res, res));
	      emit_u32(as, S390X_INS_RI(S390XI_AGHI, res, -k));
	      emit_u32(as, S390X_INS_RXE(S390XI_LGFR, res, left));
	    }
	  }
	} else {
	  asm_s390x_add_log(as, "subov_k_int_eq", ir, dest, left, RID_NONE, RID_NONE);
	  asm_s390x_guard_log(as, "subov_k_int_eq", ir, CC_NE, 0, -k);
	  asm_guardcc(as, CC_NE);
	  emit_u32(as, S390X_INS_RXE(S390XI_CGFR, dest, dest));
	  emit_u32(as, S390X_INS_RI(S390XI_AGHI, dest, -k));
	  emit_u32(as, S390X_INS_RXE(S390XI_LGFR, dest, dest));
	}
      } else {
	if (irt_isguard(ir->t))
	  asm_guardcc(as, CC_OF);
	if (bnorm)
	  asm_bnorm32(as, ir, dest);
	if (!irt_isguard(ir->t) && dest != left)
	  emit_u48_pad8(as, S390X_INS_RIE_D(S390XI_AGHIK, dest, left, -k));
	else
	  emit_u32(as, S390X_INS_RI(S390XI_AGHI, dest, -k));
      }
      if (dest != left && irt_isguard(ir->t) &&
	  !(as->loopref && as->curins > as->loopref && irt_isinteger(ir->t) &&
	    asm_s390x_guarded_addsub_op32home(as, ir->op1)))
        emit_movrr(as, ir, dest, left);
      return;
    }
    if (k != INT32_MIN && irt_isguard(ir->t) && irt_isinteger(ir->t) &&
	asm_s390x_guarded_addsub_op32home(as, ir->op1) &&
	asm_s390x_guarded_addsub_can_stay_low32(as, ir)) {
      dest = ra_dest_nobase(as, ir, asm_s390x_dest_gprset(ir->t), -261);
      left = ra_hintalloc(as, ir->op1, dest, RSET_GPR_NOB);
      asm_s390x_add_log(as, "subov_k_int32", ir, dest, left, RID_NONE,
			RID_NONE);
      asm_s390x_guard_log(as, "subov_k_int32", ir, CC_NE, 0, -k);
      asm_guardcc(as, CC_NE);
      emit_u32(as, S390X_INS_RXE(S390XI_CGFR, dest, dest));
      emit_u48_pad8(as, S390X_INS_RIL(S390XI_AGFI, dest, -k));
      if (dest != left &&
	  !asm_s390x_loop_phi_carry_in_dest(as, ir, dest, left))
	emit_movrr(as, ir, dest, left);
      return;
    }
    if (k != INT32_MIN && !irt_isguard(ir->t)) {
      dest = ra_dest_nobase(as, ir, asm_s390x_dest_gprset(ir->t), -261);
      left = ra_hintalloc(as, ir->op1, dest, RSET_GPR_NOB);
      if (bnorm)
	asm_bnorm32(as, ir, dest);
      emit_u48_pad8(as, S390X_INS_RIL(S390XI_AGFI, dest, -k));
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
    if (as->loopref && as->curins > as->loopref) {
      if (asm_s390x_minmax_index_subov_guard_elided(as, ir)) {
	asm_s390x_add_log(as, "subov_rr_minmax_idx", ir, dest, left, right,
			  RID_NONE);
	if (bnorm)
	  asm_bnorm32(as, ir, dest);
	if (dest == left)
	  emit_u32(as, S390X_INS_RXE(S390XI_SGR, dest, right));
	else
	  emit_u32(as, S390X_INS_RRF_M(S390XI_SGRK, dest, right, left));
      } else {
	IRRef pref = asm_s390x_guarded_ov_preserve_ref(as, ir);
	Reg preserve = (pref == ir->op2) ? right : left;
	int low32home = asm_s390x_guarded_addsub_op32home(as, ir->op1) &&
			asm_s390x_guarded_addsub_op32home(as, ir->op2) &&
			asm_s390x_guarded_addsub_can_stay_low32(as, ir);
	if (low32home) {
	  asm_s390x_add_log(as, "subov_rr_int_eq", ir, dest, left, right,
			    RID_NONE);
	  asm_s390x_guard_log(as, "subov_rr_int_eq", ir, CC_OF, 0,
			      (int)(ir->op2 - REF_BIAS));
	  if (dest != preserve) {
	    RegSet sallow = allow & ~RID2RSET(left);
	    Reg res = ra_scratch(as, sallow);
	    emit_movrr(as, ir, dest, res);
	    asm_guardcc(as, CC_OF);
	    emit_movrr(as, ir, dest, preserve);
	    emit_u32(as, S390X_INS_RRF_M(S390XI_SRK, res, right, left));
	  } else {
	    asm_guardcc(as, CC_OF);
	    emit_u32(as, S390X_INS_RRF_M(S390XI_SRK, dest, right, left));
	  }
	} else {
	  asm_s390x_add_log(as, "subov_rr_int_sr", ir, dest, left, right,
			    RID_NONE);
	  asm_s390x_guard_log(as, "subov_rr_int_sr", ir, CC_OF, 0,
			      (int)(ir->op2 - REF_BIAS));
	  asm_s390x_guarded_int_rr32(as, ir, dest, left, right, S390XI_SRK);
	}
      }
    } else {
      asm_s390x_add_log(as, "subov_rr_int_eq", ir, dest, left, right, RID_NONE);
      asm_s390x_guard_log(as, "subov_rr_int_eq", ir, CC_NE, 0,
			  (int)(ir->op2 - REF_BIAS));
      asm_guardcc(as, CC_NE);
      emit_u32(as, S390X_INS_RXE(S390XI_CGFR, dest, dest));
      emit_u32(as, S390X_INS_RXE(S390XI_SGR, dest, right));
      emit_u32(as, S390X_INS_RXE(S390XI_LGFR, dest, dest));
      if (dest != left)
	emit_movrr(as, ir, dest, left);
    }
  } else {
    if (irt_isguard(ir->t))
      asm_guardcc(as, CC_OF);
    if (bnorm)
      asm_bnorm32(as, ir, dest);
    if (dest == left)
      emit_u32(as, S390X_INS_RXE(S390XI_SGR, dest, right));
    else
      emit_u32(as, S390X_INS_RRF_M(S390XI_SGRK, dest, right, left));
  }
}
static void asm_mul(ASMState *as, IRIns *ir)
{
  if (irt_isnum(ir->t)) {
    Reg dest = ra_dest(as, ir, RSET_FPR);
    Reg left = ra_hintalloc(as, ir->op1, dest, RSET_FPR);
    Reg right = ra_alloc1(as, ir->op2, rset_exclude(RSET_FPR, left));
    if (dest == right && dest != left) {
      Reg tmp = left;
      left = right;
      right = tmp;
    }
    emit_u32(as, S390X_INS_RXE(S390XI_MDBR, dest, right));
    asm_s390x_fpleft(as, ir, dest, left);
    return;
  }

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
    if (as->loopref && as->curins > as->loopref) {
      RegSet sallow = allow & ~RID2RSET(left);
      Reg res = ra_scratch(as, sallow);
      Reg tmp = ra_scratch(as, sallow & ~RID2RSET(res));
      emit_movrr(as, ir, dest, res);
      asm_guardcc(as, CC_NE);
      emit_u32(as, S390X_INS_RXE(S390XI_CGR, res, tmp));
      emit_u32(as, S390X_INS_RXE(S390XI_LGFR, tmp, res));
      emit_u32(as, S390X_INS_RXE(S390XI_MSGFR, res, right));
      emit_u32(as, S390X_INS_RXE(S390XI_LGFR, res, left));
      return;
    } else {
      Reg tmp = ra_scratch(as, allow);
      asm_guardcc(as, CC_NE);
      emit_u32(as, S390X_INS_RXE(S390XI_CGR, dest, tmp));
      emit_u32(as, S390X_INS_RXE(S390XI_LGFR, tmp, dest));
      emit_u32(as, S390X_INS_RXE(S390XI_MSGFR, dest, right));
      emit_u32(as, S390X_INS_RXE(S390XI_LGFR, dest, dest));
    }
  } else {
    emit_u32(as, S390X_INS_RXE(S390XI_LGFR, dest, dest));
    emit_u32(as, S390X_INS_RXE(S390XI_MSGFR, dest, right));
    emit_u32(as, S390X_INS_RXE(S390XI_LGFR, dest, dest));
  }

  if (dest != left)
    emit_movrr(as, ir, dest, left);
}

static int asm_s390x_scev_ref_offset(ASMState *as, IRRef ref, int64_t *ofsp)
{
  jit_State *J = as->J;
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

static int asm_s390x_mod_operand_nonnegative(ASMState *as, IRRef ref)
{
  jit_State *J = as->J;
  int64_t ofs;

  if (J->scev.idx == REF_NIL || !J->scev.dir ||
      J->scev.start == REF_NIL || !irref_isk(J->scev.start))
    return 0;
  if (!asm_s390x_scev_ref_offset(as, ref, &ofs))
    return 0;
  if (J->scev.start < J->cur.nk || J->scev.start >= REF_TRUE)
    return ofs >= 0;
  return (int64_t)IR(J->scev.start)->i + ofs >= 0;
}

static int asm_modk_int(ASMState *as, IRIns *ir)
{
  IRIns *k = IR(ir->op2);
  Reg dest, left, divr;
  RegSet allow;
  MCode *l_done;
  const Reg rem = RID_R4;
  const Reg quot = RID_R5;

  if (!irt_isint(ir->t) || !irref_isk(ir->op2) || k->o != IR_KINT || k->i <= 0)
    return 0;

  if (k->i == 1) {
    dest = ra_dest_nobase(as, ir, RSET_GPR_NOB, -278);
    emit_u32(as, S390X_INS_RXE(S390XI_XGR, dest, dest));
    return 1;
  }

  if (asm_s390x_mod_operand_nonnegative(as, ir->op1)) {
    uint64_t magic;
    Reg qhi = rem;
    Reg qlo = quot;
    Reg mreg;

    allow = RSET_GPR_NOB;
    rset_clear(allow, qhi);
    rset_clear(allow, qlo);
    dest = ra_dest_nobase(as, ir, allow, -278);
    magic = UINT64_MAX/(uint32_t)k->i + 1u;
    ra_evictset(as, RID2RSET(qhi)|RID2RSET(qlo));
    ra_modified(as, qhi);
    ra_modified(as, qlo);
    allow = RSET_GPR_NOB;
    rset_clear(allow, qhi);
    rset_clear(allow, qlo);
    rset_clear(allow, dest);
    left = ra_alloc1_nobase(as, ir->op1, allow, -279);
    allow = rset_exclude(RSET_GPR_NOB, left);
    rset_clear(allow, qhi);
    rset_clear(allow, qlo);
    rset_clear(allow, dest);
    mreg = ra_scratch(as, allow);

    emit_u32(as, S390X_INS_RXE(S390XI_SGR, dest, qhi));
    emit_u48_pad8(as, S390X_INS_RIL(S390XI_MSGFI, qhi, k->i));
    emit_u32(as, S390X_INS_RXE(S390XI_MLGR, qhi, mreg));
    emit_loadu64(as, mreg, magic);
    emit_u32(as, S390X_INS_RXE(S390XI_LLGFR, qlo, left));
    emit_u32(as, S390X_INS_RXE(S390XI_LLGFR, dest, left));
    return 1;
  }

  /* Signed integer modulo by a positive constant divisor. Split off the
  ** nonnegative runtime path for reciprocal multiply; keep DSGR for negatives
  ** to preserve Lua's floor-mod correction.
  */
  allow = RSET_GPR_NOB;
  rset_clear(allow, rem);
  rset_clear(allow, quot);
  dest = ra_dest_nobase(as, ir, allow, -278);
  ra_evictset(as, RID2RSET(rem)|RID2RSET(quot));
  ra_modified(as, rem);
  ra_modified(as, quot);
  allow = RSET_GPR_NOB;
  rset_clear(allow, rem);
  rset_clear(allow, quot);
  rset_clear(allow, dest);
  left = ra_alloc1_nobase(as, ir->op1, allow, -279);
  allow = rset_exclude(RSET_GPR_NOB, left);
  rset_clear(allow, rem);
  rset_clear(allow, quot);
  rset_clear(allow, dest);
  divr = ra_allock(as, k->i, allow);
  rset_clear(allow, divr);
  if (allow) {
    uint64_t magic = UINT64_MAX/(uint32_t)k->i + 1u;
    Reg mreg = ra_scratch(as, allow);
    MCode *l_slow, *l_slow_copy, *l_done;

    l_done = as->mcp;
    if (dest != rem)
      emit_movrr(as, ir, dest, rem);
    l_slow_copy = as->mcp;
    emit_u32(as, S390X_INS_RXE(S390XI_AGR, rem, divr));
    emit_condbranch(as, CC_GE, l_slow_copy);
    emit_u32(as, S390X_INS_RI(S390XI_CGHI, rem, 0));
    emit_u32(as, S390X_INS_RXE(S390XI_DSGR, rem, divr));
    emit_shiftimm(as, S390XI_SRAG, rem, quot, 63);
    l_slow = as->mcp;
    emit_condbranch(as, CC_AL, l_done);
    emit_u32(as, S390X_INS_RXE(S390XI_SGR, dest, rem));
    emit_u48_pad8(as, S390X_INS_RIL(S390XI_MSGFI, rem, k->i));
    emit_u32(as, S390X_INS_RXE(S390XI_MLGR, rem, mreg));
    emit_loadu64(as, mreg, magic);
    emit_u32(as, S390X_INS_RXE(S390XI_LLGFR, quot, left));
    emit_u32(as, S390X_INS_RXE(S390XI_LLGFR, dest, left));
    emit_condbranch(as, CC_LT, l_slow);
    emit_u32(as, S390X_INS_RI(S390XI_CGHI, quot, 0));
    emit_u32(as, S390X_INS_RXE(S390XI_LGFR, quot, left));
    return 1;
  }
  if (dest != rem)
    emit_movrr(as, ir, dest, rem);
  l_done = as->mcp;
  emit_u32(as, S390X_INS_RXE(S390XI_AGR, rem, divr));
  emit_condbranch(as, CC_GE, l_done);
  emit_u32(as, S390X_INS_RI(S390XI_CGHI, rem, 0));
  emit_u32(as, S390X_INS_RXE(S390XI_DSGR, rem, divr));
  emit_shiftimm(as, S390XI_SRAG, rem, quot, 63);
  emit_u32(as, S390X_INS_RXE(S390XI_LGFR, quot, quot));
  if (quot != left)
    emit_movrr(as, ir, quot, left);
  return 1;
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

  if (!irt_is64(ir->t)) {
    if (!asm_s390x_can_defer_neg_bnorm32(as, ir))
      asm_bnorm32(as, ir, dest);
    if (irt_isguard(ir->t))
      asm_guardcc(as, CC_OF);
    emit_u16_pad4(as, S390X_INS_RR(S390XI_LCR, dest, left));
    return;
  }
  if (irt_isguard(ir->t))
    asm_guardcc(as, CC_OF);
  emit_u32(as, S390X_INS_RXE(S390XI_SGR, dest, left));
  emit_u32(as, S390X_INS_RXE(S390XI_XGR, dest, dest));
}

static void asm_abs(ASMState *as, IRIns *ir)
{
  Reg dest = ra_dest(as, ir, RSET_FPR);
  Reg left = ra_hintalloc(as, ir->op1, dest, RSET_FPR);
  emit_u32(as, S390X_INS_RXE(S390XI_LPDBR, dest, left));
}

static int asm_s390x_fpdiv_same_conv_addk_sched(ASMState *as, IRIns *ir)
{
  IRIns *numadd, *denadd, *conv;
  IRRef ref = (IRRef)(ir - as->ir);
  Reg dest, den, idx, nk, dk;
  RegSet allow;
  if (irref_isk(ir->op1) || irref_isk(ir->op2) ||
      !mayfuse(as, ir->op1) || !mayfuse(as, ir->op2) ||
      ir + 3 >= IR(as->orignins) ||
      (ir + 1)->o != IR_ADD || !irt_isnum((ir + 1)->t) ||
      (ir + 1)->op1 != ref ||
      (ir + 2)->o != IR_ADD || !irt_isinteger((ir + 2)->t) ||
      !irref_isk((ir + 2)->op2) ||
      IR((ir + 2)->op2)->o != IR_KINT || IR((ir + 2)->op2)->i != 1 ||
      (ir + 3)->o != IR_LE || !irt_isguard((ir + 3)->t) ||
      (ir + 3)->op1 != (IRRef)(ref + 2))
    return 0;
  numadd = IR(ir->op1);
  denadd = IR(ir->op2);
  if (numadd->o != IR_ADD || denadd->o != IR_ADD ||
      !irt_isnum(numadd->t) || !irt_isnum(denadd->t) ||
      irref_isk(numadd->op1) || numadd->op1 != denadd->op1 ||
      !irref_isk(numadd->op2) || !irref_isk(denadd->op2) ||
      !mayfuse(as, numadd->op1))
    return 0;
  conv = IR(numadd->op1);
  if (conv->o != IR_CONV || !irt_isnum(conv->t) ||
      (IRType)(conv->op2 & IRCONV_SRCMASK) != IRT_INT ||
      irref_isk(conv->op1) || (ir + 2)->op1 != conv->op1)
    return 0;

  dest = ra_dest(as, ir, RSET_FPR);
  den = ra_scratch(as, rset_exclude(RSET_FPR, dest));
  allow = rset_exclude(rset_exclude(RSET_FPR, dest), den);
  nk = ra_alloc1(as, numadd->op2, allow);
  dk = ra_alloc1(as, denadd->op2, rset_exclude(allow, nk));
  idx = ra_alloc1_nobase(as, conv->op1, RSET_GPR_NOB, -287);
  as->curins -= 3;
  emit_u32(as, S390X_INS_RXE(S390XI_DDBR, dest, den));
  emit_u32(as, S390X_INS_RXE(S390XI_ADBR, den, dk));
  emit_u32(as, S390X_INS_RXE(S390XI_ADBR, dest, nk));
  emit_movrr(as, ir, den, dest);
  emit_u32(as, S390X_INS_RI(S390XI_AGHI, idx, 1));
  emit_u32(as, S390X_INS_RXE(S390XI_CDFBR, dest, idx));
  return 1;
}

static void asm_fpdiv(ASMState *as, IRIns *ir)
{
  if (asm_s390x_fpdiv_same_conv_addk_sched(as, ir))
    return;
  Reg dest = ra_dest(as, ir, RSET_FPR);
  Reg lr = ra_alloc2(as, ir, RSET_FPR);
  Reg left = lr & 255;
  Reg right = lr >> 8;
  if (dest == right && dest != left) {
    Reg copy = ra_scratch(as, rset_exclude(rset_exclude(RSET_FPR, dest), left));
    emit_u32(as, S390X_INS_RXE(S390XI_DDBR, dest, copy));
    asm_s390x_fpleft(as, ir, dest, left);
    emit_movrr(as, ir, copy, right);
    return;
  }
  emit_u32(as, S390X_INS_RXE(S390XI_DDBR, dest, right));
  asm_s390x_fpleft(as, ir, dest, left);
}

static int asm_s390x_fpsqrt_addk_sched(ASMState *as, IRIns *ir)
{
  IRIns *xadd, *conv;
  IRRef ref = (IRRef)(ir - as->ir);
  Reg dest, idx, k;
  if (irref_isk(ir->op1) || !mayfuse(as, ir->op1) ||
      ir + 3 >= IR(as->orignins) ||
      (ir + 1)->o != IR_ADD || !irt_isnum((ir + 1)->t) ||
      (ir + 1)->op1 != ref ||
      (ir + 2)->o != IR_ADD || !irt_isinteger((ir + 2)->t) ||
      !irref_isk((ir + 2)->op2) ||
      IR((ir + 2)->op2)->o != IR_KINT || IR((ir + 2)->op2)->i != 1 ||
      (ir + 3)->o != IR_LE || !irt_isguard((ir + 3)->t) ||
      (ir + 3)->op1 != (IRRef)(ref + 2))
    return 0;
  xadd = IR(ir->op1);
  if (xadd->o != IR_ADD || !irt_isnum(xadd->t) ||
      irref_isk(xadd->op1) || !irref_isk(xadd->op2) ||
      !mayfuse(as, xadd->op1))
    return 0;
  if (!irref_isk((ir + 1)->op2) && IR((ir + 1)->op2)->o == IR_ADD)
    return 0;
  conv = IR(xadd->op1);
  if (conv->o != IR_CONV || !irt_isnum(conv->t) ||
      (IRType)(conv->op2 & IRCONV_SRCMASK) != IRT_INT ||
      irref_isk(conv->op1) || (ir + 2)->op1 != conv->op1)
    return 0;

  dest = ra_dest(as, ir, RSET_FPR);
  k = ra_alloc1(as, xadd->op2, rset_exclude(RSET_FPR, dest));
  idx = ra_alloc1_nobase(as, conv->op1, RSET_GPR_NOB, -293);
  as->curins -= 2;
  emit_u32(as, S390X_INS_RXE(S390XI_SQDBR, dest, dest));
  emit_u32(as, S390X_INS_RXE(S390XI_ADBR, dest, k));
  emit_u32(as, S390X_INS_RI(S390XI_AGHI, idx, 1));
  emit_u32(as, S390X_INS_RXE(S390XI_CDFBR, dest, idx));
  return 1;
}

static void asm_fpmath(ASMState *as, IRIns *ir)
{
  Reg dest, left;
  IRFPMathOp fpm = (IRFPMathOp)ir->op2;
  if (fpm <= IRFPM_TRUNC) {
    uint32_t mask = fpm == IRFPM_FLOOR ? 7u :
		    fpm == IRFPM_CEIL ? 6u : 5u;
    dest = ra_dest(as, ir, RSET_FPR);
    left = ra_hintalloc(as, ir->op1, dest, RSET_FPR);
    emit_u32(as, S390X_INS_RRF_E(S390XI_FIDBRA, dest, mask, left, 0));
    return;
  }
  if (fpm == IRFPM_SQRT) {
    if (asm_s390x_fpsqrt_addk_sched(as, ir))
      return;
    dest = ra_dest(as, ir, RSET_FPR);
    left = ra_hintalloc(as, ir->op1, dest, RSET_FPR);
    emit_u32(as, S390X_INS_RXE(S390XI_SQDBR, dest, left));
    return;
  }
  asm_callid(as, ir, (IRCallID)(IRCALL_lj_vm_floor + ir->op2));
}
static void asm_tobit(ASMState *as, IRIns *ir)
{
  RegSet allow = RSET_FPR;
  Reg left = ra_alloc1(as, ir->op1, allow);
  Reg right = ra_alloc1(as, ir->op2, rset_clear(allow, left));
  RegSet scratch = rset_clear(allow, left);
  scratch &= ~RID2RSET(right);
  Reg tmp = ra_scratch(as, scratch);
  Reg dest = ra_dest_nobase(as, ir, RSET_GPR_NOB, -245);

  emit_u32(as, S390X_INS_RXE(S390XI_LGFR, dest, dest));
  emit_u32(as, S390X_INS_RXE(S390XI_LGDR, dest, tmp));
  emit_u32(as, S390X_INS_RXE(S390XI_ADBR, tmp, left));
  if (tmp != right)
    emit_movrr(as, ir, tmp, right);
}

static void asm_intmin_max(ASMState *as, IRIns *ir, int ismax)
{
  IRRef lref = ir->op1;
  IRRef rref = ir->op2;
  RegSet allow = RSET_GPR_NOB;
  Reg left, right, dest;

  if (irref_isk(lref) && !irref_isk(rref)) {
    IRRef tmp = lref;
    lref = rref;
    rref = tmp;
  }

  left = irref_isk(lref) ? ra_allock(as, asm_kintptr(as, lref), allow) :
			   ra_alloc1_nobase(as, lref, allow, -281);
  rset_clear(allow, left);
  right = irref_isk(rref) ? ra_allock(as, asm_kintptr(as, rref), allow) :
			    ra_alloc1_nobase(as, rref, allow, -282);
  dest = ra_dest_nobase(as, ir, rset_exclude(RSET_GPR_NOB, right), -280);

  emit_u32(as, S390X_INS_RRF_M(S390XI_LOCGR, dest,
			       ismax ? CC_LT : CC_GT, right));
  emit_u32(as, S390X_INS_RXE(S390XI_CGR, left, right));
  if (dest != left)
    emit_movrr(as, ir, dest, left);
}

static void asm_min(ASMState *as, IRIns *ir)
{
  if (irt_isnum(ir->t) || !asm_s390x_int_minmax_enabled()) {
    asm_s390x_nyi_ir(as, ir);
    return;
  }
  asm_intmin_max(as, ir, 0);
}

static void asm_max(ASMState *as, IRIns *ir)
{
  if (irt_isnum(ir->t) || !asm_s390x_int_minmax_enabled()) {
    asm_s390x_nyi_ir(as, ir);
    return;
  }
  asm_intmin_max(as, ir, 1);
}

#define asm_addov(as, ir)	asm_add(as, ir)
#define asm_subov(as, ir)	asm_sub(as, ir)
#define asm_mulov(as, ir)	asm_mul(as, ir)

static void asm_aref(ASMState *as, IRIns *ir)
{
  Reg base, dest;
  if (irref_isk(ir->op2)) {
    int32_t ofs = 8 * IR(ir->op2)->i;
    base = ra_alloc1_nobase(as, ir->op1, RSET_GPR_NOB, -210);
    dest = ra_dest_nobase(as, ir, rset_exclude(RSET_GPR_NOB, base), -211);
    asm_s390x_ir_log_aref(as, ir, ir->op1, ir->op2, base, RID_NONE, dest, ofs);
    emit_addptr(as, dest, ofs);
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
	  IRRef tab = IR(ir->op1)->op1;
	  int32_t ofs = asm_fuseabase(as, tab);
	  IRRef refa = ofs ? tab : ir->op1;
	  ofs += 8 * IR(ir->op2)->i;
	  if (checki20(ofs)) {
	    fr.reg = ra_alloc1_nobase(as, refa, allow, -251);
	    fr.ofs = ofs;
	    return fr;
	  }
	} else {
	  RegSet baseallow = asm_s390x_dest_gprset(IR(ir->op1)->t) & allow;
	  if (lj_asm_s390x_aref_base_allgpr_enabled())
	    baseallow = allow;
	  if (baseallow == RSET_EMPTY)
	    baseallow = allow;
	  fr.base = ra_alloc1_nobase(as, ir->op1, baseallow, -252);
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

static int asm_href_dynamic_str(ASMState *as, IRIns *ir, IROp merge)
{
  IRIns *irkey = IR(ir->op2);
  RegSet allow = RSET_GPR_NOB;
  Reg dest, tab, key, sid, tmp, tkey;
  MCode *l_end, *l_loop, *l_start;
  ptrdiff_t delta;

  if (merge != 0 || irref_isk(ir->op2) || !irt_isstr(irkey->t) || !ra_used(ir))
    return 0;

  dest = ra_dest_nobase(as, ir, allow, -216);
  allow = rset_exclude(allow, dest);
  tab = ra_alloc1_nobase(as, ir->op1, allow, -217);
  allow = rset_exclude(allow, tab);
  key = ra_alloc1_nobase(as, ir->op2, allow, -218);
  allow = rset_exclude(allow, key);
  sid = ra_scratch(as, allow);
  allow = rset_exclude(allow, sid);
  tmp = ra_scratch(as, allow);
  allow = rset_exclude(allow, tmp);
  tkey = ra_scratch(as, allow);

  l_end = emit_label(as);
  as->invmcp = NULL;
  emit_loadu64(as, dest, (uintptr_t)niltvg(J2G(as->J)));

  as->mcp -= 4;
  l_loop = as->mcp;
  emit_u32(as, S390X_INS_RI(S390XI_CGHI, dest, 0));
  emit_load64ofs(as, dest, dest, (int32_t)offsetof(Node, next));

  emit_condbranch(as, CC_EQ, l_end);
  emit_u32(as, S390X_INS_RXE(S390XI_CGR, sid, tkey));
  emit_load64ofs(as, sid, dest, (int32_t)offsetof(Node, key));

  l_start = as->mcp;
  delta = (char *)l_start - (char *)l_loop;
  lj_assertA((delta & 1) == 0, "unaligned HREF string loop target");
  lj_assertA(checki16((int32_t)(delta >> 1)),
	     "s390x HREF string loop target out of range");
  emit_u32_at(l_loop, S390X_INS_BRC(CC_NE, (int32_t)(delta >> 1)));

  emit_u32(as, S390X_INS_RXE(S390XI_AGR, dest, tmp));
  emit_load64ofs(as, tmp, tab, (int32_t)offsetof(GCtab, node));
  emit_shiftimm(as, S390XI_SLLG, dest, tmp, 3);
  emit_u32(as, S390X_INS_RRF_M(S390XI_AGRK, tmp, dest, tmp));
  emit_shiftimm(as, S390XI_SLLG, tmp, dest, 1);
  emit_u32(as, S390X_INS_RXE(S390XI_NGR, dest, sid));
  emit_loadu32ofs(as, sid, key, (int32_t)offsetof(GCstr, sid));
  emit_loadu32ofs(as, dest, tab, (int32_t)offsetof(GCtab, hmask));
  emit_u32(as, S390X_INS_RXE(S390XI_OGR, tkey, tmp));
  emit_loadu64(as, tmp, (uint64_t)irt_toitype(irkey->t) << 47);
  if (tkey != key)
    emit_movrr(as, ir, tkey, key);
  return 1;
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

static int asm_s390x_hload_dynamic_str_href_int_typecheck(ASMState *as,
							  IRIns *ir)
{
  IRIns *href, *key;
  UNUSED(as);
  if (!(ir->o == IR_HLOAD && irt_isint(ir->t)) || irref_isk(ir->op1))
    return 0;
  href = IR(ir->op1);
  if (href->o != IR_HREF || irref_isk(href->op2))
    return 0;
  key = IR(href->op2);
  return irt_isstr(key->t);
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
    if (!(irt_isnum(t) || irt_isint(t) || irt_isu32(t) ||
	  irt_isaddr(t) || irt_ispri(t))) {
      asm_s390x_nyi_ir(as, ir);
      return;
    }
    if (irt_isnum(t)) {
      dest = ra_dest(as, ir, RSET_FPR);
      fr = asm_fuseahuref(as, ir->op1, allow);
    } else {
      dest = ra_dest_nobase(as, ir, allow, -215);
      fr = asm_fuseahuref(as, ir->op1, rset_clear(allow, dest));
    }
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
  if (!irt_isnum(t) && !irt_isint(t) && !irt_isu32(t) &&
      !irt_isaddr(t) && !irt_ispri(t)) {
    asm_s390x_nyi_ir(as, ir);
    return;
  }
  if (irt_isnum(t)) {
    Reg tmp = ra_scratch(as, allow);
    Reg limit = ra_scratch(as, rset_exclude(allow, tmp));
    int numdest = ra_hasreg(dest);
    asm_s390x_guard_log(as, "aload_num", ir,
			numdest ? CC_HI : CC_HS, ofs, 0);
    if (numdest) {
      MCode *l_done = as->mcp;
      emit_u32(as, S390X_INS_RXE(S390XI_CDFBR, dest, tmp));
      emit_u32(as, S390X_INS_RXE(S390XI_LGFR, tmp, tmp));
      emit_loadu32ofs(as, tmp, fr.reg, asm_s390x_vload_intofs(as, ir, ofs));
      emit_condbranch(as, CC_NE, l_done);
    }
    asm_guardcc(as, numdest ? CC_HI : CC_HS);
    emit_u32(as, S390X_INS_RXE(S390XI_CLGR, tmp, limit));
    emit_loadu64(as, limit, (uint64_t)(int64_t)(int32_t)LJ_TISNUM);
    emit_shiftimm(as, S390XI_SRAG, tmp, tmp, 47);
    emit_load64ofs(as, tmp, fr.reg, ofs);
  } else if (irt_isaddr(t)) {
    Reg tmp = ra_scratch(as, allow);
    asm_s390x_guard_log(as, "vload_addr", ir, CC_NE, ofs, 0);
    asm_guardcc(as, CC_NE);
    emit_u32(as, S390X_INS_RI(S390XI_CGHI, tmp, (int32_t)irt_toitype(t)));
    emit_shiftimm(as, S390XI_SRAG, tmp, tmp, 47);
    emit_load64ofs(as, tmp, fr.reg, ofs);
  } else if (irt_ispri(t)) {
    Reg tmp = ra_scratch(as, allow);
    Reg expected = ra_scratch(as, rset_exclude(allow, tmp));
    asm_s390x_guard_log(as, "vload_pri", ir, CC_NE, ofs, 0);
    asm_guardcc(as, CC_NE);
    emit_u32(as, S390X_INS_RXE(S390XI_CGR, tmp, expected));
    emit_loadu64(as, expected, irt_isnil(t) ? ~(uint64_t)0 :
      (uint64_t)(~((int64_t)~irt_toitype(t) << 47)));
    emit_load64ofs(as, tmp, fr.reg, ofs);
  } else if (asm_s390x_hload_dynamic_str_href_int_typecheck(as, ir)) {
    Reg tmp = ra_scratch(as, allow);
    Reg expected = ra_scratch(as, rset_exclude(allow, tmp));
    asm_s390x_guard_log(as, "hload_str_href_int", ir, CC_NE, ofs, 0);
    asm_guardcc(as, CC_NE);
    emit_u32(as, S390X_INS_RXE(S390XI_CGR, tmp, expected));
    if (LJ_GC64 && asm_s390x_gc64_signed_int_sload_enabled()) {
      emit_loadu64(as, expected, (uint64_t)(int64_t)(int32_t)LJ_TISNUM);
      emit_shiftimm(as, S390XI_SRAG, tmp, tmp, 47);
    } else {
      emit_loadu64(as, expected, (uint64_t)((uint32_t)LJ_TISNUM >> 15));
      emit_shiftimm(as, S390XI_SRLG, tmp, tmp, 47);
    }
    emit_load64ofs(as, tmp, fr.reg, ofs);
  }
  if (ra_hasreg(dest)) {
    if (irt_isnum(t)) {
      emit_loadofs(as, ir, dest, fr.reg, ofs);
    } else if (irt_isaddr(t) || irt_ispri(t)) {
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
  IRIns *irkey = IR(ir->op2);
  Reg expected = RID_NONE;
  RegSet allow;
  if (asm_href_dynamic_str(as, ir, merge))
    return;
  if (merge == 0 && irt_isstr(irkey->t)) {
    const CCallInfo *cistr = &lj_ir_callinfo[IRCALL_lj_tab_getstr_jit];
    IRRef sargs[3];
    sargs[0] = ASMREF_L;     /* lua_State * */
    sargs[1] = ir->op1;      /* GCtab * */
    sargs[2] = ir->op2;      /* GCstr * */
    asm_setupresult(as, ir, cistr);  /* cTValue * */
    asm_gencall(as, cistr, sargs);
    return;
  }
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
    emit_load64ofs(as, dest, dest, 0);
    emit_loadu64(as, dest, (uintptr_t)v);
    return;
  }

  if (guarded || ir->o == IR_UREFC) {
    RegSet allow = rset_exclude(RSET_GPR_NOB, dest);
    Reg uv = ra_scratch(as, allow);

    if (ir->o == IR_UREFC) {
      emit_addptr(as, dest, (int32_t)offsetof(GCupval, tv));
      if (dest != uv)
	emit_movrr(as, ir, dest, uv);
    } else {
      emit_load64ofs(as, dest, uv, (int32_t)offsetof(GCupval, v));
    }

    if (guarded) {
      RegSet tmpallow = rset_exclude(allow, uv);
      Reg tmp = ra_scratch(as, tmpallow);
      asm_guardcc(as, ir->o == IR_UREFC ? CC_EQ : CC_NE);
      emit_u32(as, S390X_INS_RI(S390XI_CGHI, tmp, 0));
      emit_u48_pad8(as, S390X_INS_RXY(S390XI_LLGC, tmp, 0, uv,
				      (int32_t)offsetof(GCupval, closed)));
    }

    if (irref_isk(ir->op1)) {
      GCfunc *fn = ir_kfunc(IR(ir->op1));
      emit_loadu64(as, uv, gcrefu(fn->l.uvptr[(ir->op2 >> 8)]));
    } else {
      Reg fn = ra_alloc1_nobase(as, ir->op1, rset_exclude(RSET_GPR_NOB, uv), -246);
      emit_load64ofs(as, uv, fn, uvofs);
    }
  } else {
    emit_load64ofs(as, dest, dest, (int32_t)offsetof(GCupval, v));
    if (irref_isk(ir->op1)) {
      GCfunc *fn = ir_kfunc(IR(ir->op1));
      emit_loadu64(as, dest, gcrefu(fn->l.uvptr[(ir->op2 >> 8)]));
    } else {
      Reg fn = ra_alloc1_nobase(as, ir->op1, rset_exclude(RSET_GPR_NOB, dest), -246);
      emit_load64ofs(as, dest, fn, uvofs);
    }
  }
}

static void asm_fref(ASMState *as, IRIns *ir)
{
  RegSet allow = RSET_GPR_NOB;
  Reg dest = ra_dest_nobase(as, ir, allow, -247);
  Reg base = ra_alloc1_nobase(as, ir->op1, rset_exclude(allow, dest), -248);
  int32_t ofs = (int32_t)field_ofs[ir->op2];

  emit_addptr(as, dest, ofs);
  if (dest != base)
    emit_movrr(as, ir, dest, base);
}

static void asm_strref(ASMState *as, IRIns *ir)
{
  RegSet allow = RSET_GPR_NOB;
  Reg dest = ra_dest_nobase(as, ir, allow, -247);
  Reg base = ra_alloc1_nobase(as, ir->op1, rset_exclude(allow, dest), -248);
  int32_t ofs = (int32_t)sizeof(GCstr);

  if (irref_isk(ir->op2)) {
    ofs += IR(ir->op2)->i;
    emit_addptr(as, dest, ofs);
    if (dest != base)
      emit_movrr(as, ir, dest, base);
    return;
  }

  {
    RegSet rallow = rset_exclude(allow, dest);
    Reg right;
    rset_clear(rallow, base);
    right = ra_alloc1_nobase(as, ir->op2, rallow, -249);
    emit_addptr(as, dest, ofs);
    emit_u32(as, S390X_INS_RXE(S390XI_AGR, dest, right));
    if (dest != base)
      emit_movrr(as, ir, dest, base);
  }
}

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

static int asm_fusestrref_u8xload(ASMState *as, IRIns *ir, Reg dest)
{
  IRIns *strref = IR(ir->op1);
  IRRef idxref;
  RegSet allow;
  Reg base, idx = RID_NONE;
  int32_t ofs = (int32_t)sizeof(GCstr);

  if (!irt_isu8(ir->t) || !mayfuse(as, ir->op1) || strref->o != IR_STRREF)
    return 0;

  idxref = strref->op2;
  if (irref_isk(idxref)) {
    ofs += IR(idxref)->i;
    idxref = 0;
  } else {
    IRIns *idxir = IR(idxref);
    if (idxir->o == IR_ADD) {
      if (irref_isk(idxir->op2)) {
	intptr_t k = asm_kintptr(as, idxir->op2);
	if (checki20(k)) {
	  ofs += (int32_t)k;
	  idxref = idxir->op1;
	}
      } else if (irref_isk(idxir->op1)) {
	intptr_t k = asm_kintptr(as, idxir->op1);
	if (checki20(k)) {
	  ofs += (int32_t)k;
	  idxref = idxir->op2;
	}
      }
    }
  }
  if (!checki20(ofs))
    return 0;

  allow = rset_exclude(RSET_GPR_NOB, dest);
  base = ra_alloc1_nobase(as, strref->op1, allow, -248);
  if (idxref) {
    idx = ra_alloc1_nobase(as, idxref, rset_exclude(allow, base), -249);
    emit_loadu8idxofs(as, dest, idx, base, ofs);
  } else {
    emit_loadu8ofs(as, dest, base, ofs);
  }
  return 1;
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
	irt_isu16(ir->t) || irt_isi8(ir->t) || irt_isi16(ir->t) ||
	irt_isfp(ir->t))) {
    asm_s390x_nyi_ir(as, ir);
    return;
  }

  allow = irt_isfp(ir->t) ? RSET_FPR : RSET_GPR_NOB;
  dest = ra_dest_nobase(as, ir, allow, -247);
  if (asm_fusestrref_u8xload(as, ir, dest))
    return;
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

  if (!(irt_isint(t) || irt_isu32(t) || irt_isint64(t) || irt_isaddr(t) ||
	irt_isu8(t) || irt_isu16(t) || irt_isi8(t) || irt_isi16(t) ||
	irt_isgcv(t))) {
    asm_s390x_nyi_ir(as, ir);
    return;
  }

  {
    RegSet dallow = RSET_GPR_SAVED & RSET_GPR_NOB;
    if (dallow == RSET_EMPTY)
      dallow = RSET_GPR_NOB;
    dest = ra_dest_nobase(as, ir, dallow, -220);
  }

  if (ir->op1 == REF_NIL) {  /* FLOAD from GG_State with offset. */
    base = RID_DISPATCH;
    ofs = (ir->op2 << 2) - GG_OFS(dispatch);
  } else {
    base = ra_alloc1_nobase(as, ir->op1, rset_exclude(RSET_GPR_NOB, dest), -221);
    ofs = field_ofs[ir->op2];
  }

  if (asm_s390x_ir_log_enabled() &&
      (ir->op2 == IRFL_TAB_ARRAY || ir->op2 == IRFL_TAB_ASIZE)) {
    fprintf(stderr,
	    "S390X_IR kind=fload curins=%d ir=%d field=%d base_ref=%d dest=%d base=%d ofs=%d type=%d\n",
	    (int)(as->curins - REF_BIAS), (int)((ir - as->ir) - REF_BIAS),
	    (int)ir->op2, (int)(ir->op1 - REF_BIAS), (int)dest, (int)base,
	    (int)ofs, (int)irt_type(t));
  }
  if (ir->op2 == IRFL_TAB_ARRAY) {
    int32_t abase = asm_fuseabase(as, ir->op1);
    if (abase) {
      emit_addptr(as, dest, abase);
      if (dest != base)
	emit_movrr(as, ir, dest, base);
      return;
    }
  }

  if (irt_isint64(t) || irt_isaddr(t) || irt_isgcv(t)) {
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
    Reg expected = RID_NONE;
    if (ir->op2 & IRSLOAD_KEYINDEX) {
      expected = ra_scratch(as, rset_exclude(tallow, tmp));
      if (asm_s390x_sloadmap_log_enabled()) {
	fprintf(stderr,
		"S390X_SLOADMAP curins=%d ref=%d kind=keyindex op1=%d op2=0x%x ofs=%d vofs=%d base=%d dest=%d tmp=%d expected=%d\n",
		(int)(as->curins - REF_BIAS), (int)((ir - as->ir) - REF_BIAS),
		(int)ir->op1, (unsigned int)ir->op2, (int)ofs, (int)vofs,
		(int)base, (int)dest, (int)tmp, (int)expected);
      }
      asm_s390x_guard_log(as, "sload_keyindex", ir, CC_NE, ofs, vofs);
      asm_guardcc(as, CC_NE);
      emit_u32(as, S390X_INS_RXE(S390XI_CGR, tmp, expected));
      emit_loadu64(as, expected, (uint64_t)((uint32_t)LJ_KEYINDEX >> 15));
      emit_shiftimm(as, S390XI_SRLG, tmp, tmp, 47);
    } else if (irt_isinteger(t)) {
      expected = ra_scratch(as, rset_exclude(tallow, tmp));
      if (asm_s390x_sloadmap_log_enabled()) {
	fprintf(stderr,
		"S390X_SLOADMAP curins=%d ref=%d kind=int op1=%d op2=0x%x ofs=%d vofs=%d base=%d dest=%d tmp=%d expected=%d\n",
		(int)(as->curins - REF_BIAS), (int)((ir - as->ir) - REF_BIAS),
		(int)ir->op1, (unsigned int)ir->op2, (int)ofs, (int)vofs,
		(int)base, (int)dest, (int)tmp, (int)expected);
      }
      asm_s390x_guard_log(as, "sload_int", ir, CC_NE, ofs, vofs);
      asm_guardcc(as, CC_NE);
      emit_u32(as, S390X_INS_RXE(S390XI_CGR, tmp, expected));
      if (ir->op1 == 4 &&
	  ir->op2 == (IRSLOAD_INHERIT|IRSLOAD_TYPECHECK) &&
	  asm_s390x_forl_current_compare_fix_enabled()) {
	if (LJ_GC64 && asm_s390x_gc64_signed_int_sload_enabled()) {
	  emit_loadu64(as, expected, (uint64_t)(int64_t)(int32_t)LJ_TISNUM);
	  emit_shiftimm(as, S390XI_SRAG, tmp, tmp, 47);
	} else {
	  emit_loadu64(as, expected, (uint64_t)((uint32_t)LJ_TISNUM & 0x1ffffu));
	  emit_shiftimm(as, S390XI_SRLG, tmp, tmp, 47);
	}
      } else if (LJ_GC64 && asm_s390x_gc64_signed_int_sload_enabled()) {
	emit_loadu64(as, expected,
		     (uint64_t)(int64_t)(int32_t)LJ_TISNUM);
	emit_shiftimm(as, S390XI_SRAG, tmp, tmp, 47);
      } else {
	emit_loadu64(as, expected, (uint64_t)((uint32_t)LJ_TISNUM >> 15));
	emit_shiftimm(as, S390XI_SRLG, tmp, tmp, 47);
      }
    } else if (irt_isnum(t)) {
      Reg limit = ra_scratch(as, rset_exclude(tallow, tmp));
      int numdest = ra_hasreg(dest);
      if (asm_s390x_sloadmap_log_enabled()) {
	fprintf(stderr,
		"S390X_SLOADMAP curins=%d ref=%d kind=num op1=%d op2=0x%x ofs=%d vofs=%d base=%d dest=%d tmp=%d expected=%d\n",
		(int)(as->curins - REF_BIAS), (int)((ir - as->ir) - REF_BIAS),
		(int)ir->op1, (unsigned int)ir->op2, (int)ofs, (int)vofs,
		(int)base, (int)dest, (int)tmp, (int)limit);
      }
      asm_s390x_guard_log(as, "sload_num", ir,
			  numdest ? CC_HI : CC_HS, ofs, vofs);
      if (numdest) {
	MCode *l_done = as->mcp;
	emit_u32(as, S390X_INS_RXE(S390XI_CDFBR, dest, tmp));
	emit_u32(as, S390X_INS_RXE(S390XI_LGFR, tmp, tmp));
	emit_loadu32ofs(as, tmp, base, vofs);
	emit_condbranch(as, CC_NE, l_done);
      }
      asm_guardcc(as, numdest ? CC_HI : CC_HS);
      emit_u32(as, S390X_INS_RXE(S390XI_CLGR, tmp, limit));
      emit_loadu64(as, limit, (uint64_t)(int64_t)(int32_t)LJ_TISNUM);
      emit_shiftimm(as, S390XI_SRAG, tmp, tmp, 47);
    } else {
      asm_s390x_guard_log(as, "sload_type", ir, CC_NE, ofs, vofs);
      asm_guardcc(as, CC_NE);
      emit_u32(as, S390X_INS_RI(S390XI_CGHI, tmp, (int32_t)irt_toitype(t)));
      emit_shiftimm(as, S390XI_SRAG, tmp, tmp, 47);
    }
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
  RegSet forbid = RSET_EMPTY;

  if (ir->r == RID_SINK)
    return;
  if (irt_isnum(ir->t)) {
    Reg src;
    fr = asm_fuseahuref(as, ir->op1, RSET_GPR_NOB);
    src = ra_alloc1(as, ir->op2, RSET_FPR);
    lj_assertA(checki20(fr.ofs), "s390x numeric store offset out of range");
    emit_u48_pad8(as, S390X_INS_RXY(S390XI_STDY, src, 0, fr.reg, fr.ofs));
    asm_emitfuseahuref(as, ir, &fr);
    return;
  }
  fr = asm_fuseahuref(as, ir->op1, RSET_GPR_NOB);
  if (asm_s390x_ir_log_enabled()) {
    fprintf(stderr,
	    "S390X_IR kind=ahustore curins=%d ir=%d xref=%d fused=%d fbase=%d fidx=%d ofs=%d vref=%d type=%d\n",
	    (int)(as->curins - REF_BIAS), (int)((ir - as->ir) - REF_BIAS),
	    (int)(ir->op1 - REF_BIAS), (int)fr.reg, (int)fr.base, (int)fr.idx,
	    (int)fr.ofs, (int)(ir->op2 - REF_BIAS), (int)irt_type(ir->t));
  }
  if (fr.base != RID_NONE)
    forbid |= RID2RSET(fr.base);
  if (fr.idx != RID_NONE)
    forbid |= RID2RSET(fr.idx);
  asm_tvstore64x(as, fr.reg, fr.ofs, ir->op2, forbid);
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

static void asm_obar(ASMState *as, IRIns *ir)
{
  const CCallInfo *ci = &lj_ir_callinfo[IRCALL_lj_gc_barrieruv];
  IRRef args[2];
  MCode *l_end;
  Reg obj, val, tmp, mask, g;
  int32_t obj_marked_ofs =
    (int32_t)offsetof(GCupval, marked) - (int32_t)offsetof(GCupval, tv);

  /* No need for other object barriers (yet). */
  lj_assertA(IR(ir->op1)->o == IR_UREFC, "bad OBAR type");
  ra_evictset(as, RSET_SCRATCH);
  l_end = as->mcp;

  args[0] = ASMREF_TMP1;  /* global_State *g */
  args[1] = ir->op1;      /* TValue *tv      */
  asm_gencall(as, ci, args);

  g = ra_releasetmp(as, ASMREF_TMP1);
  emit_addptr(as, g, GG_DISP2G);
  if (g != RID_DISPATCH)
    emit_u32(as, S390X_INS_RXE(S390XI_LGR, g, RID_DISPATCH));

  obj = IR(ir->op1)->r;
  mask = ra_scratch(as, rset_exclude(RSET_GPR, obj));
  tmp = ra_scratch(as, rset_exclude(rset_exclude(RSET_GPR, obj), mask));

  emit_condbranch(as, CC_EQ, l_end);
  emit_u32(as, S390X_INS_RXE(S390XI_NGR, tmp, mask));
  emit_loadu64(as, mask, LJ_GC_WHITES);
  val = ra_alloc1(as, ir->op2,
		  rset_exclude(rset_exclude(rset_exclude(RSET_GPR, obj), mask), tmp));
  emit_loadu8ofs(as, tmp, val, (int32_t)offsetof(GChead, marked));

  emit_condbranch(as, CC_EQ, l_end);
  emit_u32(as, S390X_INS_RXE(S390XI_NGR, tmp, mask));
  emit_loadu64(as, mask, LJ_GC_BLACK);
  emit_loadu8ofs(as, tmp, obj, obj_marked_ofs);
}

static void asm_xstore(ASMState *as, IRIns *ir)
{
  RegSet allow;
  Reg src, base;
  int32_t ofs = 0;
  IRRef xref = asm_fusexref_kbase(as, ir->op1, &ofs);
  int narrow = irt_isu8(ir->t) || irt_isu16(ir->t) ||
	       irt_isi8(ir->t) || irt_isi16(ir->t);

  if (ir->r == RID_SINK)
    return;
  if (!(irt_isint(ir->t) || irt_isu32(ir->t) || irt_isaddr(ir->t) ||
	irt_is64(ir->t) || irt_isgcv(ir->t) ||
	(narrow && asm_s390x_narrow_xstore_enabled()))) {
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
  if (irt_isi8(ir->t) || irt_isu8(ir->t))
    emit_store8ofs(as, src, base, ofs);
  else if (irt_isi16(ir->t) || irt_isu16(ir->t))
    emit_store16ofs(as, src, base, ofs);
  else
    emit_storeofs(as, ir, src, base, ofs);
}

static int asm_s390x_abs_accint_unused_conv_elided(ASMState *as, IRIns *ir)
{
  IRIns *absir, *idxconv, *next;
  IRRef srcref = ir->op1;

  if (!irt_isguard(ir->t) || !irt_isint(ir->t) || ra_used(ir) ||
      ir <= IR(REF_FIRST + 2) || ir + 2 >= IR(as->orignins))
    return 0;
  absir = ir - 1;
  idxconv = ir - 2;
  next = ir + 1;
  if (absir->o != IR_ABS || !irt_isnum(absir->t) ||
      idxconv->o != IR_CONV || !irt_isnum(idxconv->t) ||
      (IRType)(idxconv->op2 & IRCONV_SRCMASK) != IRT_INT ||
      absir->op1 != (IRRef)(idxconv - as->ir) ||
      next->o != IR_ADD || !irt_isnum(next->t) ||
      next->op1 != (IRRef)(absir - as->ir) || next->op2 != srcref)
    return 0;

  /*
  ** This checked NUM->INT conversion is not consumed by later IR. In the
  ** abs accumulator trace the carried value is already the numeric PHI, so
  ** materializing the integerness guard only adds a hot-loop branch.
  */
  return 1;
}

static void asm_tointg(ASMState *as, IRIns *ir, Reg left)
{
  int used;
  Reg tmp, dest;
  used = ra_used(ir);
  tmp = ra_scratch(as, rset_exclude(RSET_FPR, left));
  dest = ra_dest_nobase(as, ir, RSET_GPR_NOB, -273);
  asm_guardcc(as, CC_NE);
  emit_u32(as, S390X_INS_RXE(S390XI_CDBR, tmp, left));
  emit_u32(as, S390X_INS_RXE(S390XI_CDFBR, tmp, dest));
  if (used)
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
    RegSet allow = RSET_GPR_CALL_NOB;
    Reg src = irref_isk(ir->op2) ?
	      ra_allock(as, (intptr_t)asm_cnewi_k64val(as, ir->op2), allow) :
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
static int asm_s390x_int_result_normalized(IRIns *ir)
{
  if (!irt_isinteger(ir->t))
    return 0;
  switch (ir->o) {
  case IR_ADD:
  case IR_ADDOV:
  case IR_SUB:
  case IR_SUBOV:
  case IR_MUL:
  case IR_MULOV:
  case IR_BAND:
  case IR_BOR:
  case IR_BXOR:
  case IR_BNOT:
  case IR_BSWAP:
  case IR_BSHL:
  case IR_BSHR:
  case IR_BSAR:
  case IR_BROL:
  case IR_BROR:
  case IR_MIN:
  case IR_MAX:
    return 1;
  case IR_SLOAD:
    return !(ir->op2 & IRSLOAD_FRAME);
  default:
    return 0;
  }
}

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
      IRIns *lir = IR(lref);
      Reg left;
      if (st == IRT_INT && lir->o == IR_SUBOV && irt_isguard(lir->t) &&
	      irt_isinteger(lir->t) && irref_isk(lir->op1) &&
	      IR(lir->op1)->o == IR_KINT && IR(lir->op1)->i == 0 &&
	      !irref_isk(lir->op2) &&
	      asm_s390x_subov_zero_absorbed_by_numconv(as, lir)) {
	    IRRef ref = (IRRef)(ir - as->ir);
	    int abs_next = ir + 1 < IR(as->orignins) && (ir + 1)->o == IR_ABS &&
			   (ir + 1)->op1 == ref;
	    left = ra_alloc1_nobase(as, lir->op2, RSET_GPR_NOB, -271);
	    if (asm_s390x_centered_mod17_abs_conv(as, ir, lir)) {
	      Reg abs = ra_scratch(as, rset_exclude(RSET_GPR_NOB, left));
	      Reg neg = ra_scratch(as, rset_exclude(rset_exclude(RSET_GPR_NOB,
								  left), abs));
	      emit_u32(as, S390X_INS_RXE(S390XI_CDFBR, dest, abs));
	      emit_u32(as, S390X_INS_RRF_M(S390XI_LOCGR, abs, CC_LT, neg));
	      emit_u16_pad4(as, S390X_INS_RR(S390XI_LTR, left, left));
	      emit_u32(as, S390X_INS_RXE(S390XI_LCGFR, neg, left));
	      emit_u32(as, S390X_INS_RXE(S390XI_LGR, abs, left));
	    } else if (abs_next) {
	      emit_u32(as, S390X_INS_RXE(S390XI_CDFBR, dest, left));
	    } else {
	      Reg neg = ra_scratch(as, rset_exclude(RSET_GPR_NOB, left));
	      emit_u32(as, S390X_INS_RXE(S390XI_CDFBR, dest, neg));
	      emit_u32(as, S390X_INS_RXE(S390XI_LCGFR, neg, left));
	    }
	    return;
      }
      left = ra_alloc1_nobase(as, lref, RSET_GPR_NOB, -271);
      if (st == IRT_U32 || st == IRT_U16 || st == IRT_U8) {
	emit_u32(as, S390X_INS_RXE(S390XI_LLGFR, left, left));
      } else {
	if (!asm_s390x_int_result_normalized(IR(lref)))
	  emit_u32(as, S390X_INS_RXE(S390XI_LGFR, left, left));
      }
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
    if (irt_isguard(ir->t)) {
      lj_assertA(irt_isint(ir->t), "bad type for checked CONV");
      if (asm_s390x_abs_accint_unused_conv_elided(as, ir))
	return;
      asm_tointg(as, ir, ra_alloc1(as, lref, RSET_FPR));
      return;
    }
    if (irt_isint(ir->t)) {
      Reg dest = ra_dest_nobase(as, ir, RSET_GPR_NOB, -274);
      Reg left = ra_alloc1(as, lref, RSET_FPR);
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

static void asm_strto(ASMState *as, IRIns *ir)
{
  const CCallInfo *ci = &lj_ir_callinfo[IRCALL_lj_strscan_num_cache];
  IRRef args[2];
  int32_t ofs = 0;
  Reg tmp;

  ra_evictset(as, RSET_SCRATCH);
  if (ra_used(ir)) {
    if (ra_hasspill(ir->s)) {
      ofs = sps_scale(ir->s);
      if (ra_hasreg(ir->r)) {
	ra_free(as, ir->r);
	ra_modified(as, ir->r);
	emit_spload(as, ir, ir->r, ofs);
      }
    } else {
      Reg dest = ra_dest(as, ir, RSET_FPR);
      emit_spload(as, ir, dest, ofs);
    }
  }

  asm_guardcc(as, CC_EQ);
  emit_u32(as, S390X_INS_RI(S390XI_CGHI, RID_RET, 0));

  args[0] = ir->op1;      /* GCstr *str */
  args[1] = ASMREF_TMP1;  /* TValue *n  */
  asm_gencall(as, ci, args);

  tmp = ra_releasetmp(as, ASMREF_TMP1);
  lj_assertA(tmp != RID_SP, "strto tmp uses RID_SP");
  emit_addptr(as, tmp, ofs);
  emit_movrr(as, ir, tmp, RID_SP);
}

#undef ASM_S390X_STUB_IR

/* -- Trace patching ------------------------------------------------------ */

static int lj_asm_s390x_direct_patchexit_enabled(void)
{
  return 1;
}

static int lj_asm_s390x_direct_patchexit_log_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_DIRECT_PATCHEXIT_LOG") != NULL);
  return enabled;
}

static int lj_asm_s390x_direct_patchexit_miss_log_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_DIRECT_PATCHEXIT_MISS_LOG") != NULL);
  return enabled;
}

static uint32_t lj_asm_s390x_load_be32(const MCode *p)
{
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	 ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int lj_asm_s390x_patch_brc_to(MCode *p, MCode *oldtarget,
				     MCode *newtarget)
{
  uint32_t ins = lj_asm_s390x_load_be32(p);
  int16_t olddisp;
  ptrdiff_t delta;
  if ((ins & 0xff0f0000u) != 0xa7040000u)
    return 0;
  olddisp = (int16_t)(ins & 0xffffu);
  if ((MCode *)((char *)p + ((int32_t)olddisp << 1)) != oldtarget)
    return 0;
  delta = (char *)newtarget - (char *)p;
  if ((delta & 1) != 0 || !checki16((int32_t)(delta >> 1)))
    return 0;
  emit_u32_at(p, (ins & 0xffff0000u) | (uint16_t)(delta >> 1));
  return 1;
}

static int lj_asm_s390x_skip_direct_brc(MCode *base, MCode *p,
					MCode *oldtarget)
{
  uint32_t ins = lj_asm_s390x_load_be32(p);
  int16_t olddisp;
  /* Helper-return guards, including GC-step exits, must keep the stub path
  ** because the VM exit handler owns their slow-path state transition. */
  if (p < base + 4 || (ins & 0xff0f0000u) != 0xa7040000u ||
      lj_asm_s390x_load_be32(p - 4) != S390X_INS_RI(S390XI_CGHI, RID_RET, 0))
    return 0;
  olddisp = (int16_t)(ins & 0xffffu);
  return (MCode *)((char *)p + ((int32_t)olddisp << 1)) == oldtarget;
}

static int lj_asm_s390x_patch_rie_branch_to(MCode *p, MCode *oldtarget,
					    MCode *newtarget)
{
  uint8_t *q = (uint8_t *)p;
  int16_t olddisp;
  ptrdiff_t delta;
  if (q[0] != 0xec || q[6] != 0x07 || q[7] != 0x07 ||
      (q[5] != 0x64 && q[5] != 0x76 && q[5] != 0x7c && q[5] != 0x7e))
    return 0;
  olddisp = (int16_t)((uint32_t)q[2] << 8 | q[3]);
  if ((MCode *)((char *)p + ((int32_t)olddisp << 1)) != oldtarget)
    return 0;
  delta = (char *)newtarget - (char *)p;
  if ((delta & 1) != 0 || !checki16((int32_t)(delta >> 1)))
    return 0;
  q[2] = (uint8_t)((uint32_t)(delta >> 1) >> 8);
  q[3] = (uint8_t)(delta >> 1);
  return 1;
}

void lj_asm_patchexit(jit_State *J, GCtrace *T, ExitNo exitno, MCode *target)
{
  MCode *mcarea = lj_mcode_patch(J, T->mcode, 0);
  MCode *px = exitstub_trace_addr(T, exitno);
  MCode *cstart = px;
  ptrdiff_t delta = (char *)target - (char *)px;
  if (lj_asm_s390x_direct_patchexit_enabled()) {
    MCode *p = T->mcode;
    MCode *pe = (MCode *)((char *)T->mcode + T->szmcode);
    unsigned int brc_patch = 0, rie_patch = 0, brc_skip = 0;
    /* Known guard branch forms can skip the exit stub after side linking.
    ** The stub is still patched below as a fallback for all other shapes. */
    for (; p + 4 <= pe; p += 2) {
      if (lj_asm_s390x_skip_direct_brc(T->mcode, p, px)) {
	brc_skip++;
      } else if (lj_asm_s390x_patch_brc_to(p, px, target)) {
	brc_patch++;
	if (p < cstart) cstart = p;
      } else if (p + 6 <= pe &&
		 lj_asm_s390x_patch_rie_branch_to(p, px, target)) {
	rie_patch++;
	if (p < cstart) cstart = p;
      }
    }
    if (lj_asm_s390x_direct_patchexit_log_enabled()) {
      fprintf(stderr,
	      "S390X_DIRECT_PATCHEXIT trace=%u exit=%u brc=%u rie=%u skip=%u stub=%p target=%p\n",
	      (unsigned int)T->traceno, (unsigned int)exitno,
	      brc_patch, rie_patch, brc_skip, (void *)px, (void *)target);
    }
    if (lj_asm_s390x_direct_patchexit_miss_log_enabled() &&
	(brc_patch + rie_patch) == 0) {
      const SnapShot *snap = exitno < T->nsnap ? &T->snap[exitno] : NULL;
      fprintf(stderr,
	      "S390X_DIRECT_PATCHEXIT_MISS trace=%u exit=%u skip=%u nsnap=%u nins=%u snapref=%u snapnent=%u link=%u linktype=%u px=%p px0=0x%08x px1=0x%08x target=%p\n",
	      (unsigned int)T->traceno, (unsigned int)exitno, brc_skip,
	      (unsigned int)T->nsnap, (unsigned int)T->nins,
	      (unsigned int)(snap ? snap->ref : 0),
	      (unsigned int)(snap ? snap->nent : 0),
	      (unsigned int)T->link, (unsigned int)T->linktype,
	      (void *)px, (unsigned int)lj_asm_s390x_load_be32(px),
	      (unsigned int)lj_asm_s390x_load_be32(px + 4),
	      (void *)target);
    }
  }
  lj_assertJ((delta & 1) == 0, "unaligned patched exit target");
  lj_assertJ(checki32((int64_t)(delta >> 1)),
	     "s390x patched exit target out of range");
  emit_u48_at(px, S390X_INS_BRCL(CC_AL, (int32_t)(delta >> 1)));
  lj_mcode_sync(cstart, px + 2);
  lj_mcode_patch(J, mcarea, 1);
}
