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
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_GUARDMARK") != NULL ||
	       getenv("LUAJIT_S390X_GUARDMARK_TAKEN") != NULL);
  return enabled;
}

static int asm_s390x_guardmark_taken_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_GUARDMARK_TAKEN") != NULL);
  return enabled;
}

static int asm_s390x_gc64_signed_int_sload_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1) {
    const char *opt_in = getenv("LUAJIT_S390X_GC64_SIGNED_INT_SLOAD");
    const char *opt_out = getenv("LUAJIT_S390X_DISABLE_GC64_SIGNED_INT_SLOAD");
    enabled = ((LJ_GC64 && opt_out == NULL) || opt_in != NULL);
  }
  return enabled;
}

static int asm_s390x_call_log_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_CALL_LOG") != NULL);
  return enabled;
}

static int asm_s390x_direct_call_arg_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1) {
    const char *opt_in = getenv("LUAJIT_S390X_DIRECT_CALL_ARG");
    const char *opt_out = getenv("LUAJIT_S390X_DISABLE_DIRECT_CALL_ARG");
    enabled = (opt_in != NULL && opt_out == NULL);
  }
  return enabled;
}

static int asm_s390x_add_log_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_ADD_LOG") != NULL);
  return enabled;
}

static int asm_s390x_addhome_log_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_ADDHOME_LOG") != NULL);
  return enabled;
}

static int asm_s390x_low32home_log_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_LOW32HOME_LOG") != NULL);
  return enabled;
}

static int asm_s390x_low32cmp_log_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_LOW32CMP_LOG") != NULL);
  return enabled;
}

static int asm_s390x_bitop_log_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_BITOP_LOG") != NULL);
  return enabled;
}

static int asm_s390x_bnorm_log_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_BNORM_LOG") != NULL);
  return enabled;
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
  if (ir->o == IR_BAND && irref_isk(ir->op2) && IR(ir->op2)->o == IR_KINT &&
      IR(ir->op2)->i >= 0)
    return 1;
  asm_s390x_bnorm_use_counts(as, ir, &safe_bitop_uses, &unsafe_bitop_uses,
			     &intarith_uses, &other_uses, &guard_uses,
			     &first_use_op, &first_nonbitop_use_op);
  return safe_bitop_uses > 0 && unsafe_bitop_uses == 0 &&
	 intarith_uses == 0 && other_uses == 0 && guard_uses == 0;
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
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_SLOAD_LOG") != NULL);
  return enabled;
}

static int asm_s390x_sloadmap_log_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_SLOADMAP_LOG") != NULL);
  return enabled;
}

static int asm_s390x_forl_current_compare_fix_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1) {
    const char *opt_in = getenv("LUAJIT_S390X_FORL_CURRENT_COMPARE_FIX");
    const char *opt_out = getenv("LUAJIT_S390X_DISABLE_FORL_CURRENT_COMPARE_FIX");
    enabled = (opt_out == NULL) || opt_in != NULL;
  }
  return enabled;
}

static int asm_s390x_stack_restore_log_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_STACK_RESTORE_LOG") != NULL);
  return enabled;
}

static int asm_s390x_int_minmax_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1) {
    const char *opt_in = getenv("LUAJIT_S390X_INT_MINMAX");
    const char *opt_out = getenv("LUAJIT_S390X_DISABLE_INT_MINMAX");
    enabled = (opt_out == NULL) || opt_in != NULL;
  }
  return enabled;
}

static int asm_s390x_narrow_xstore_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1) {
    const char *opt_in = getenv("LUAJIT_S390X_NARROW_XSTORE");
    const char *opt_out = getenv("LUAJIT_S390X_DISABLE_NARROW_XSTORE");
    enabled = (opt_out == NULL) || opt_in != NULL;
  }
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
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_DISABLE_AREF_BASE_ALLGPR") == NULL);
  return enabled;
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
    *p = S390X_INS_BRC(CC_AL, (int32_t)(((char *)target - (char *)p) >> 1));
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

static void asm_gencall(ASMState *as, const CCallInfo *ci, IRRef *args)
{
  uint32_t n, nargs = CCI_XNARGS(ci);
  int32_t spofs = S390X_CALL_SPS_EXTRA * 8;
  Reg gpr = REGARG_FIRSTGPR, fpr = REGARG_FIRSTFPR;
  int direct_call_arg = asm_s390x_direct_call_arg_enabled();
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
  for (n = 0; n < nargs; n++) {
    IRRef ref = args[n];
    if (!ref)
      continue;
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
  IRIns *lir, *rir = NULL;
  int cc;
  int cmp32u;
  int cmp32s;
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
  left = ra_alloc1_nobase(as, lref, RSET_GPR_NOB, -201);
  asm_guardcc(as, cc & 15);
  imm16_signed = irref_isk(rref) && !(cc & CC_UNSIGNED) && !irt_isaddr(ir->t) &&
			 checki16(IR(rref)->i);
  if (imm16_signed) {
    cmp_left = left;
    cmp32s = irt_isinteger(ir->t);
    asm_s390x_low32cmp_log(as, "intcomp", op, lref, rref, lir, rir, 0, 1);
    asm_s390x_ir_log_intcomp(as, ir, op, lref, rref, cc, cmp_left, RID_NONE, 1);
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
  asm_s390x_low32cmp_log(as, "intcomp", op, lref, rref, lir, rir, cmp32u, 0);
  cmp_left = left;
  cmp_right = right;
  if (cmp32s && !irref_isk(rref) && asm_s390x_is_s32_counter_inc(as, lir)) {
    asm_s390x_ir_log_intcomp(as, ir, op, lref, rref, cc, left, right, 0);
    emit_u32(as, S390X_INS_RXE(S390XI_CGFR, left, right));
    return;
  }
  if (cmp32u || cmp32s) {
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
  } else if (cmp32s) {
    if (!irref_isk(rref))
      emit_u32(as, S390X_INS_RXE(S390XI_LGFR, cmp_right, right));
    emit_u32(as, S390X_INS_RXE(S390XI_LGFR, cmp_left, left));
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
  asm_s390x_addhome_log(as, ir);
  asm_s390x_low32home_log(as, "add", ir);
  left = ra_hintalloc(as, ir->op1, dest, RSET_GPR_NOB);
  if (irref_isk(ir->op2)) {
    int32_t k = (int32_t)asm_kintptr(as, ir->op2);
    if (checki16(k)) {
      asm_s390x_ir_log_addk(as, ir, ir->op1, ir->op2, dest, left, k);
      if (irt_isguard(ir->t) && irt_isinteger(ir->t)) {
	RegSet allow = rset_exclude(RSET_GPR_NOB, dest);
	if (as->loopref && as->curins > as->loopref) {
	  RegSet sallow = rset_exclude(allow, left);
	  Reg res = ra_scratch(as, sallow);
	  int low32home = asm_s390x_guarded_addsub_op32home(as, ir->op1);
	  asm_s390x_add_log(as, "addov_k_int_eq", ir, dest, left, RID_NONE, RID_NONE);
	  asm_s390x_guard_log(as, "addov_k_int_eq", ir,
			      low32home ? CC_OF : CC_NE, 0, k);
	  emit_movrr(as, ir, dest, res);
	  asm_guardcc(as, low32home ? CC_OF : CC_NE);
	  if (low32home) {
	    emit_u48_pad8(as, S390X_INS_RIE_D(S390XI_AHIK, res, left, k));
	  } else {
	    emit_u32(as, S390X_INS_RXE(S390XI_CGFR, res, res));
	    emit_u32(as, S390X_INS_RI(S390XI_AGHI, res, k));
	    emit_u32(as, S390X_INS_RXE(S390XI_LGFR, res, left));
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
	int low32home = as->loopref && as->curins > as->loopref &&
			asm_s390x_plain_add_range_stripped(as, ir) &&
			asm_s390x_plain_add_op32home(as, ir->op1) &&
			asm_s390x_can_defer_plain_add_bnorm32(as, ir);
	if (irt_isguard(ir->t)) {
	  asm_s390x_guard_log(as, "addov_k", ir, CC_OF, 0, k);
	  asm_guardcc(as, CC_OF);
	}
	if (bnorm && !low32home &&
	    !asm_s390x_can_defer_counter_add_bnorm32(as, ir))
	  asm_bnorm32(as, ir, dest);
	if (low32home && !irt_isguard(ir->t) && dest != left)
	  emit_u48_pad8(as, S390X_INS_RIE_D(S390XI_AHIK, dest, left, k));
	else if (!irt_isguard(ir->t) && dest != left)
	  emit_u48_pad8(as, S390X_INS_RIE_D(S390XI_AGHIK, dest, left, k));
	else
	  emit_u32(as, S390X_INS_RI(S390XI_AGHI, dest, k));
      }
      if (dest != left && irt_isguard(ir->t))
	emit_movrr(as, ir, dest, left);
      return;
    }
    if (!irt_isguard(ir->t)) {
      asm_s390x_ir_log_addk(as, ir, ir->op1, ir->op2, dest, left, k);
      if (bnorm && !asm_s390x_can_defer_counter_add_bnorm32(as, ir))
	asm_bnorm32(as, ir, dest);
      emit_u48_pad8(as, S390X_INS_RIL(S390XI_AGFI, dest, k));
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
		      asm_s390x_guarded_addsub_op32home(as, ir->op2);
      asm_s390x_add_log(as, "addov_rr_int_eq", ir, dest, left, right, RID_NONE);
      asm_s390x_guard_log(as, "addov_rr_int_eq", ir,
			  low32home ? CC_OF : CC_NE, 0,
			  (int)(ir->op2 - REF_BIAS));
      if (low32home && dest != preserve) {
	asm_guardcc(as, CC_OF);
	emit_u32(as, S390X_INS_RRF_M(S390XI_LOCGR, dest, CC_OF, preserve));
	emit_u32(as, S390X_INS_RRF_M(S390XI_ARK, dest, right, left));
      } else {
	RegSet sallow = allow & ~RID2RSET(left);
	Reg res = ra_scratch(as, sallow);
	emit_movrr(as, ir, dest, res);
	asm_guardcc(as, low32home ? CC_OF : CC_NE);
	if (dest != preserve)
	  emit_movrr(as, ir, dest, preserve);
	if (low32home) {
	  emit_u32(as, S390X_INS_RRF_M(S390XI_ARK, res, right, left));
	} else {
	  emit_u32(as, S390X_INS_RXE(S390XI_CGFR, res, res));
	  emit_u32(as, S390X_INS_RXE(S390XI_AGR, res, right));
	  emit_u32(as, S390X_INS_RXE(S390XI_LGFR, res, left));
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
  Reg left, right, cmp_left, cmp_right;
  IRIns *lir, *rir = NULL;
  int cmp32s;
  if (irt_isfp(ir->t)) {
    asm_s390x_nyi_ir(as, ir);
    return;
  }
  lir = IR(ir->op1);
  if (!irref_isk(ir->op2))
    rir = IR(ir->op2);
  left = ra_alloc1_nobase(as, ir->op1, RSET_GPR_NOB, -203);
  cmp32s = irt_isinteger(ir->t);
  asm_s390x_low32cmp_log(as, "equal", ir->o, ir->op1, ir->op2, lir, rir, 0, 0);
  if (cmp32s && irref_isk(ir->op2) && IR(ir->op2)->o == IR_KINT) {
    asm_guardcc(as, ir->o == IR_EQ ? CC_NE : CC_EQ);
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
  asm_bnorm32(as, ir, dest);
  if (low32logic) {
    if (op == S390XI_NGR)
      emit_u32(as, S390X_INS_RRF_M(S390XI_NRK, dest, right, left));
    else if (op == S390XI_OGR)
      emit_u32(as, S390X_INS_RRF_M(S390XI_ORK, dest, right, left));
    else
      emit_u32(as, S390X_INS_RRF_M(S390XI_XRK, dest, right, left));
    return;
  }
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
  asm_s390x_bitop_log(as, "bswap", ir, dest, left, RID_NONE, 0);
  if (!irt_is64(ir->t))
    asm_bnorm32(as, ir, dest);
  emit_u32(as, S390X_INS_RXE(irt_is64(ir->t) ? S390XI_LRVGR : S390XI_LRVR,
			     dest, left));
}

static void asm_band(ASMState *as, IRIns *ir)
{
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
    emit_shiftimm(as, immop, dest, left, sh);
    asm_bnorm32(as, ir, dest);
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

static void asm_sub(ASMState *as, IRIns *ir)
{
  if (irt_isnum(ir->t)) {
    Reg dest = ra_dest(as, ir, RSET_FPR);
    Reg left = ra_hintalloc(as, ir->op1, dest, RSET_FPR);
    Reg right = ra_alloc1(as, ir->op2, rset_exclude(RSET_FPR, dest));
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

  if (irref_isk(ir->op2)) {
    int32_t k = (int32_t)asm_kintptr(as, ir->op2);
    if (k != INT32_MIN && checki16(-k)) {
      dest = ra_dest_nobase(as, ir, asm_s390x_dest_gprset(ir->t), -261);
      left = ra_hintalloc(as, ir->op1, dest, RSET_GPR_NOB);
      if (irt_isguard(ir->t) && irt_isinteger(ir->t)) {
	RegSet allow = rset_exclude(RSET_GPR_NOB, dest);
	if (as->loopref && as->curins > as->loopref) {
	  RegSet sallow = rset_exclude(allow, left);
	  Reg res = ra_scratch(as, sallow);
	  int low32home = asm_s390x_guarded_addsub_op32home(as, ir->op1);
	  asm_s390x_add_log(as, "subov_k_int_eq", ir, dest, left, RID_NONE, RID_NONE);
	  asm_s390x_guard_log(as, "subov_k_int_eq", ir,
			      low32home ? CC_OF : CC_NE, 0, -k);
	  emit_movrr(as, ir, dest, res);
	  asm_guardcc(as, low32home ? CC_OF : CC_NE);
	  if (low32home) {
	    emit_u48_pad8(as, S390X_INS_RIE_D(S390XI_AHIK, res, left, -k));
	  } else {
	    emit_u32(as, S390X_INS_RXE(S390XI_CGFR, res, res));
	    emit_u32(as, S390X_INS_RI(S390XI_AGHI, res, -k));
	    emit_u32(as, S390X_INS_RXE(S390XI_LGFR, res, left));
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
      if (dest != left && irt_isguard(ir->t))
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
      IRRef pref = asm_s390x_guarded_ov_preserve_ref(as, ir);
      Reg preserve = (pref == ir->op2) ? right : left;
      int low32home = asm_s390x_guarded_addsub_op32home(as, ir->op1) &&
		      asm_s390x_guarded_addsub_op32home(as, ir->op2);
      asm_s390x_add_log(as, "subov_rr_int_eq", ir, dest, left, right, RID_NONE);
      asm_s390x_guard_log(as, "subov_rr_int_eq", ir,
			  low32home ? CC_OF : CC_NE, 0,
			  (int)(ir->op2 - REF_BIAS));
      if (low32home && dest != preserve) {
	asm_guardcc(as, CC_OF);
	emit_u32(as, S390X_INS_RRF_M(S390XI_LOCGR, dest, CC_OF, preserve));
	emit_u32(as, S390X_INS_RRF_M(S390XI_SRK, dest, right, left));
      } else {
	RegSet sallow = allow & ~RID2RSET(left);
	Reg res = ra_scratch(as, sallow);
	emit_movrr(as, ir, dest, res);
	asm_guardcc(as, low32home ? CC_OF : CC_NE);
	if (dest != preserve)
	  emit_movrr(as, ir, dest, preserve);
	if (low32home) {
	  emit_u32(as, S390X_INS_RRF_M(S390XI_SRK, res, right, left));
	} else {
	  emit_u32(as, S390X_INS_RXE(S390XI_CGFR, res, res));
	  emit_u32(as, S390X_INS_RXE(S390XI_SGR, res, right));
	  emit_u32(as, S390X_INS_RXE(S390XI_LGFR, res, left));
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

  if (asm_s390x_mod_operand_nonnegative(as, ir->op1)) {
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
    if (dest != rem)
      emit_movrr(as, ir, dest, rem);
    emit_u32(as, S390X_INS_RXE(S390XI_LLGFR, rem, rem));
    emit_u16_pad4(as, S390X_INS_RR(S390XI_DR, rem, divr));
    emit_u32(as, S390X_INS_RXE(S390XI_XGR, rem, rem));
    if (quot != left)
      emit_movrr(as, ir, quot, left);
    return 1;
  }

  /* First fast path: signed integer modulo by a positive constant divisor.
  ** Use DSGR to avoid the generic lj_vm_modi helper on the common traced
  ** loop-index path. Keep all other cases on the existing helper fallback.
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

static void asm_fpdiv(ASMState *as, IRIns *ir)
{
  Reg dest = ra_dest(as, ir, RSET_FPR);
  Reg lr = ra_alloc2(as, ir, RSET_FPR);
  Reg left = lr & 255;
  Reg right = lr >> 8;
  emit_u32(as, S390X_INS_RXE(S390XI_DDBR, dest, right));
  asm_s390x_fpleft(as, ir, dest, left);
}

static void asm_fpmath(ASMState *as, IRIns *ir)
{
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
  MCode *l_done;

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
  l_done = as->mcp;

  if (dest != right)
    emit_movrr(as, ir, dest, right);
  emit_condbranch(as, ismax ? CC_GE : CC_LE, l_done);
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
    asm_s390x_nyi_ir(as, ir);
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
