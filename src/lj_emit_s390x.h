/*
** S390X instruction emitter scaffolding.
** Copyright (C) 2005-2026 Mike Pall. See Copyright Notice in luajit.h
**
** This header starts with the minimal integer/control-flow subset needed
** for native JIT bring-up. More lowering still needs to be filled in.
*/

#define emit_u32(as, ins)	(*--(as)->mcp = (ins))

static void emit_u16_pad4(ASMState *as, uint16_t ins)
{
  uint8_t *p = (uint8_t *)as->mcp - 4;
  p[0] = (uint8_t)(ins >> 8);
  p[1] = (uint8_t)ins;
  p[2] = 0x07;  /* nopr %r7 */
  p[3] = 0x07;
  as->mcp = (MCode *)p;
}

static void emit_u48_pad8(ASMState *as, uint64_t ins)
{
  uint8_t *p = (uint8_t *)as->mcp - 8;
  p[0] = (uint8_t)(ins >> 40);
  p[1] = (uint8_t)(ins >> 32);
  p[2] = (uint8_t)(ins >> 24);
  p[3] = (uint8_t)(ins >> 16);
  p[4] = (uint8_t)(ins >> 8);
  p[5] = (uint8_t)ins;
  p[6] = 0x07;  /* nopr %r7 */
  p[7] = 0x07;
  as->mcp = (MCode *)p;
}

static void emit_u48_at(MCode *p, uint64_t ins)
{
  uint8_t *q = (uint8_t *)p;
  q[0] = (uint8_t)(ins >> 40);
  q[1] = (uint8_t)(ins >> 32);
  q[2] = (uint8_t)(ins >> 24);
  q[3] = (uint8_t)(ins >> 16);
  q[4] = (uint8_t)(ins >> 8);
  q[5] = (uint8_t)ins;
  q[6] = 0x07;  /* nopr %r7 */
  q[7] = 0x07;
}

#define S390X_INS_RXE(op, r1, r2) \
  ((uint32_t)(op) | (((uint32_t)(r1) & 15u) << 4) | ((uint32_t)(r2) & 15u))
#define S390X_INS_RRF_M(op, r1, m3, r2) \
  ((uint32_t)(op) | (((uint32_t)(m3) & 15u) << 12) | \
   (((uint32_t)(r1) & 15u) << 4) | ((uint32_t)(r2) & 15u))
#define S390X_INS_RR(op, r1, r2) \
  ((uint16_t)(op) | (((uint16_t)(r1) & 15u) << 4) | ((uint16_t)(r2) & 15u))
#define S390X_INS_RI(op, r, imm) \
  ((uint32_t)(op) | (((uint32_t)(r) & 15u) << 20) | (uint16_t)(imm))
#define S390X_INS_RX(op, r1, x2, b2, disp) \
  ((uint32_t)(op) | (((uint32_t)(r1) & 15u) << 20) | \
   (((uint32_t)(x2) & 15u) << 16) | (((uint32_t)(b2) & 15u) << 12) | \
   ((uint32_t)(disp) & 0xfffu))
static LJ_AINLINE uint64_t s390x_disp20(int32_t disp)
{
  uint32_t udisp = (uint32_t)disp & 0xfffffu;
  return (((uint64_t)(udisp & 0xfffu)) << 16) |
	 (((uint64_t)(udisp & 0xff000u)) >> 4);
}

#define S390X_INS_RXY(op, r1, x2, b2, disp) \
  ((uint64_t)(op) | (((uint64_t)(r1) & 15u) << 36) | \
   (((uint64_t)(x2) & 15u) << 32) | (((uint64_t)(b2) & 15u) << 28) | \
   s390x_disp20((int32_t)(disp)))
#define S390X_INS_RSYB(op, r1, r3, b2, disp) \
  ((uint64_t)(op) | (((uint64_t)(r1) & 15u) << 36) | \
   (((uint64_t)(r3) & 15u) << 32) | (((uint64_t)(b2) & 15u) << 28) | \
   s390x_disp20((int32_t)(disp)))
#define S390X_INS_RSYI(op, r1, r3, imm) \
  ((uint64_t)(op) | (((uint64_t)(r1) & 15u) << 36) | \
   (((uint64_t)(r3) & 15u) << 32) | (((uint64_t)(imm) & 0xfffu) << 16))
#define S390X_INS_RIE_D(op, r1, r3, imm) \
  ((uint64_t)(op) | (((uint64_t)(r1) & 15u) << 36) | \
   (((uint64_t)(r3) & 15u) << 32) | (((uint64_t)(imm) & 0xffffu) << 16))
#define S390X_INS_BRC(cc, disp) \
  ((uint32_t)0xa7040000u | (((uint32_t)(cc) & 15u) << 20) | \
   (uint16_t)(disp))
#define S390X_INS_BRCL(cc, disp) \
  ((uint64_t)0xc00400000000ull | (((uint64_t)(cc) & 15u) << 36) | \
   ((uint32_t)(disp) & 0xffffffffu))
#define S390X_INS_BRASL(r, disp) \
  ((uint64_t)0xc00500000000ull | (((uint64_t)(r) & 15u) << 36) | \
   ((uint32_t)(disp) & 0xffffffffu))
#define S390X_INS_BASR(r1, r2) \
  (0x0d00u | (((uint32_t)(r1) & 15u) << 4) | ((uint32_t)(r2) & 15u))
#define S390X_INS_RIL(op, r1, imm) \
  ((uint64_t)(op) | (((uint64_t)(r1) & 15u) << 36) | \
   ((uint32_t)(imm) & 0xffffffffu))
#define S390X_INS_SI(op, b1, disp, imm) \
  ((uint32_t)(op) | (((uint32_t)(imm) & 0xffu) << 16) | \
   (((uint32_t)(b1) & 15u) << 12) | ((uint32_t)(disp) & 0xfffu))

#define S390XI_LGR	0xb9040000u
#define S390XI_LRVGR	0xb90f0000u
#define S390XI_LGFR	0xb9140000u
#define S390XI_LLGFR	0xb9160000u
#define S390XI_LRVR	0xb91f0000u
#define S390XI_CDBR	0xb3190000u
#define S390XI_ADBR	0xb31a0000u
#define S390XI_SDBR	0xb31b0000u
#define S390XI_MDBR	0xb31c0000u
#define S390XI_DDBR	0xb31d0000u
#define S390XI_LPDBR	0xb3100000u
#define S390XI_CDFBR	0xb3950000u
#define S390XI_CFDBR	0xb3990000u
#define S390XI_CDGBR	0xb3a50000u
#define S390XI_CGDBR	0xb3a90000u
#define S390XI_LDGR	0xb3c10000u
#define S390XI_LGDR	0xb3cd0000u
#define S390XI_LCGFR	0xb9130000u
#define S390XI_LTR	0x1200u
#define S390XI_CR	0x1900u
#define S390XI_DSGR	0xb90d0000u
#define S390XI_DLGR	0xb9870000u
#define S390XI_MSGFR	0xb91c0000u
#define S390XI_CGR	0xb9200000u
#define S390XI_CLGR	0xb9210000u
#define S390XI_LGHI	0xa7090000u
#define S390XI_AGHI	0xa70b0000u
#define S390XI_CGHI	0xa70f0000u
#define S390XI_AGFI	0xc20800000000ull
#define S390XI_CGFI	0xc20c00000000ull
#define S390XI_AGR	0xb9080000u
#define S390XI_AGFR	0xb9180000u
#define S390XI_OGR	0xb9810000u
#define S390XI_SGR	0xb9090000u
#define S390XI_XGR	0xb9820000u
#define S390XI_NGR	0xb9800000u
#define S390XI_NRK	0xb9f40000u
#define S390XI_ORK	0xb9f60000u
#define S390XI_XRK	0xb9f70000u
#define S390XI_NGRK	0xb9e40000u
#define S390XI_OGRK	0xb9e60000u
#define S390XI_XGRK	0xb9e70000u
#define S390XI_ARK	0xb9f80000u
#define S390XI_AGRK	0xb9e80000u
#define S390XI_SGRK	0xb9e90000u
#define S390XI_AGHIK	0xec00000000d9ull
#define S390XI_LG	0xe30000000004ull
#define S390XI_LLGF	0xe30000000016ull
#define S390XI_LGH	0xe30000000015ull
#define S390XI_LLGC	0xe30000000090ull
#define S390XI_LLGH	0xe30000000091ull
#define S390XI_LGB	0xe30000000077ull
#define S390XI_LD	0x68000000u
#define S390XI_LEY	0xed0000000064ull
#define S390XI_STG	0xe30000000024ull
#define S390XI_STD	0x60000000u
#define S390XI_STCY	0xe30000000072ull
#define S390XI_STHY	0xe30000000070ull
#define S390XI_STY	0xe30000000050ull
#define S390XI_STEY	0xed0000000066ull
#define S390XI_STDY	0xed0000000067ull
#define S390XI_TM	0x91000000u
#define S390XI_NI	0x94000000u
#define S390XI_XILF	0xc00700000000ull
#define S390XI_IIHF	0xc00800000000ull
#define S390XI_LLILF	0xc00f00000000ull
#define S390XI_SRL	0x88000000u
#define S390XI_SLL	0x89000000u
#define S390XI_SRA	0x8a000000u
#define S390XI_SRAG	0xeb000000000aull
#define S390XI_SRLG	0xeb000000000cull
#define S390XI_SLLG	0xeb000000000dull
#define S390XI_RLL	0xeb000000001dull
#define S390XI_SRAK	0xeb00000000dcull
#define S390XI_SRLK	0xeb00000000deull
#define S390XI_SLLK	0xeb00000000dfull
#define S390XI_LDR	0x2800u

/* Prefer rematerialization of BASE/L from global_State over spills. */
#define emit_canremat(ref)	((ref) <= REF_BASE)

#define checki20(x) \
  ((x) >= -524288 && (x) <= 524287)

#define emit_gl_ofs(field)	(GG_DISP2G + (int32_t)offsetof(global_State, field))

typedef MCode *MCLabel;
#define emit_label(as)		((as)->mcp)

static void emit_loadi(ASMState *as, Reg r, int32_t i)
{
  if (r >= RID_MIN_FPR) {
    emit_u32(as, S390X_INS_RXE(S390XI_LDGR, r, RID_TMP));
    emit_loadi(as, RID_TMP, i);
    return;
  }
  lj_assertA(checki16(i), "s390x immediate out of range");
  emit_u32(as, S390X_INS_RI(S390XI_LGHI, r, i));
}

static void emit_loadu64(ASMState *as, Reg r, uint64_t u64)
{
  if (r >= RID_MIN_FPR) {
    emit_u32(as, S390X_INS_RXE(S390XI_LDGR, r, RID_TMP));
    emit_loadu64(as, RID_TMP, u64);
    return;
  }
  uint32_t lo = (uint32_t)u64;
  uint32_t hi = (uint32_t)(u64 >> 32);
  if (checki16((int64_t)u64)) {
    emit_loadi(as, r, (int32_t)u64);
    return;
  }
  if (hi != 0)
    emit_u48_pad8(as, S390X_INS_RIL(S390XI_IIHF, r, hi));
  emit_u48_pad8(as, S390X_INS_RIL(S390XI_LLILF, r, lo));
}

static void emit_loadk64(ASMState *as, Reg r, IRIns *ir)
{
  if (ir->o == IR_KNUM || ir->o == IR_KINT64) {
    emit_loadu64(as, r, ir_k64(ir)->u64);
  } else if (ir->o == IR_KGC) {
    emit_loadu64(as, r, (uintptr_t)ir_kgc(ir));
  } else if (ir->o == IR_KPTR || ir->o == IR_KKPTR) {
    emit_loadu64(as, r, (uintptr_t)ir_kptr(ir));
  } else if (ir->o == IR_KNULL) {
    emit_loadu64(as, r, 0);
  } else {
    lj_assertA(ir->o == IR_KINT, "bad 64-bit constant op %d", ir->o);
    emit_loadu64(as, r, (uint32_t)ir->i);
  }
}

static void emit_load64ofs(ASMState *as, Reg r, Reg base, int32_t ofs)
{
  lj_assertA(checki20(ofs), "s390x load64 offset out of range");
  emit_u48_pad8(as, S390X_INS_RXY(S390XI_LG, r, 0, base, ofs));
}

static void emit_loadu32ofs(ASMState *as, Reg r, Reg base, int32_t ofs)
{
  lj_assertA(checki20(ofs), "s390x load32 offset out of range");
  emit_u48_pad8(as, S390X_INS_RXY(S390XI_LLGF, r, 0, base, ofs));
}

static void emit_loadu16ofs(ASMState *as, Reg r, Reg base, int32_t ofs)
{
  lj_assertA(checki20(ofs), "s390x load16 offset out of range");
  emit_u48_pad8(as, S390X_INS_RXY(S390XI_LLGH, r, 0, base, ofs));
}

static void emit_loadu8ofs(ASMState *as, Reg r, Reg base, int32_t ofs)
{
  lj_assertA(checki20(ofs), "s390x load8 offset out of range");
  emit_u48_pad8(as, S390X_INS_RXY(S390XI_LLGC, r, 0, base, ofs));
}

static void emit_loadi16ofs(ASMState *as, Reg r, Reg base, int32_t ofs)
{
  lj_assertA(checki20(ofs), "s390x load16 offset out of range");
  emit_u48_pad8(as, S390X_INS_RXY(S390XI_LGH, r, 0, base, ofs));
}

static void emit_loadi8ofs(ASMState *as, Reg r, Reg base, int32_t ofs)
{
  lj_assertA(checki20(ofs), "s390x load8 offset out of range");
  emit_u48_pad8(as, S390X_INS_RXY(S390XI_LGB, r, 0, base, ofs));
}

static void emit_store64ofs(ASMState *as, Reg r, Reg base, int32_t ofs)
{
  lj_assertA(checki20(ofs), "s390x store64 offset out of range");
  emit_u48_pad8(as, S390X_INS_RXY(S390XI_STG, r, 0, base, ofs));
}

static void emit_store8ofs(ASMState *as, Reg r, Reg base, int32_t ofs)
{
  lj_assertA(checki20(ofs), "s390x store8 offset out of range");
  emit_u48_pad8(as, S390X_INS_RXY(S390XI_STCY, r, 0, base, ofs));
}

static void emit_store16ofs(ASMState *as, Reg r, Reg base, int32_t ofs)
{
  lj_assertA(checki20(ofs), "s390x store16 offset out of range");
  emit_u48_pad8(as, S390X_INS_RXY(S390XI_STHY, r, 0, base, ofs));
}

static void emit_store32ofs(ASMState *as, Reg r, Reg base, int32_t ofs)
{
  lj_assertA(checki20(ofs), "s390x store32 offset out of range");
  emit_u48_pad8(as, S390X_INS_RXY(S390XI_STY, r, 0, base, ofs));
}

static void emit_shiftimm(ASMState *as, uint64_t op, Reg r1, Reg r3, uint32_t imm)
{
  lj_assertA(imm <= 63, "s390x shift immediate out of range");
  emit_u48_pad8(as, S390X_INS_RSYI(op, r1, r3, imm));
}

static void emit_getgl_ofs(ASMState *as, Reg r, int32_t ofs)
{
  emit_load64ofs(as, r, RID_DISPATCH, ofs);
}

static void emit_setgl_ofs(ASMState *as, Reg r, int32_t ofs)
{
  emit_store64ofs(as, r, RID_DISPATCH, ofs);
}

static void emit_setvmstate(ASMState *as, int32_t i)
{
  Reg tmp = RID_TMP;
  emit_loadu64(as, tmp, (uint32_t)i);
  emit_store32ofs(as, tmp, RID_DISPATCH, emit_gl_ofs(vmstate));
}

#define emit_getgl(as, r, field)	emit_getgl_ofs((as), (r), emit_gl_ofs(field))
#define emit_setgl(as, r, field)	emit_setgl_ofs((as), (r), emit_gl_ofs(field))

static void emit_loadofs(ASMState *as, IRIns *ir, Reg r, Reg base, int32_t ofs)
{
  UNUSED(as);
  if (r >= RID_MIN_FPR) {
    lj_assertA(irt_isnum(ir->t) || irt_isfloat(ir->t),
	       "NYI s390x FPR spill load for IR type %d",
	       irt_type(ir->t));
    if (irt_isfloat(ir->t)) {
      lj_assertA(checki20(ofs), "s390x FPR float load offset out of range");
      emit_u48_pad8(as, S390X_INS_RXY(S390XI_LEY, r, 0, base, ofs));
    } else {
      lj_assertA(ofs >= 0 && ofs <= 4095,
		 "s390x FPR spill load offset out of range");
      emit_u32(as, S390X_INS_RX(S390XI_LD, r, 0, base, ofs));
    }
    return;
  }
  if (irt_is64(ir->t) || irt_isaddr(ir->t) || irt_isgcv(ir->t)) {
    emit_load64ofs(as, r, base, ofs);
  } else {
    emit_loadu32ofs(as, r, base, ofs);
    if (irt_isint(ir->t))
      emit_u32(as, S390X_INS_RXE(S390XI_LGFR, r, r));
  }
}

static void emit_storeofs(ASMState *as, IRIns *ir, Reg r, Reg base, int32_t ofs)
{
  UNUSED(as);
  if (r >= RID_MIN_FPR) {
    lj_assertA(irt_isnum(ir->t) || irt_isfloat(ir->t),
	       "NYI s390x FPR spill store for IR type %d",
	       irt_type(ir->t));
    if (irt_isfloat(ir->t)) {
      lj_assertA(checki20(ofs), "s390x FPR float store offset out of range");
      emit_u48_pad8(as, S390X_INS_RXY(S390XI_STEY, r, 0, base, ofs));
    } else {
      lj_assertA(ofs >= 0 && ofs <= 4095,
		 "s390x FPR spill store offset out of range");
      emit_u32(as, S390X_INS_RX(S390XI_STD, r, 0, base, ofs));
    }
    return;
  }
  if (irt_is64(ir->t) || irt_isaddr(ir->t) || irt_isgcv(ir->t))
    emit_store64ofs(as, r, base, ofs);
  else
    emit_store32ofs(as, r, base, ofs);
}

static void emit_movrr(ASMState *as, IRIns *ir, Reg dst, Reg src)
{
  UNUSED(ir);
  if (dst == src)
    return;
  if (dst < RID_MIN_FPR) {
    if (src < RID_MIN_FPR)
      emit_u32(as, S390X_INS_RXE(S390XI_LGR, dst, src));
    else
      emit_u32(as, S390X_INS_RXE(S390XI_LGDR, dst, src));
  } else {
    if (src < RID_MIN_FPR)
      emit_u32(as, S390X_INS_RXE(S390XI_LDGR, dst, src));
    else
      emit_u16_pad4(as, (uint16_t)(S390XI_LDR |
				   (((uint32_t)dst & 15u) << 4) |
				   ((uint32_t)src & 15u)));
  }
}

static void emit_condbranch(ASMState *as, S390XCC cc, MCode *target)
{
  MCode *p = as->mcp - 1;
  ptrdiff_t delta = (char *)target - (char *)p;
  lj_assertA((delta & 1) == 0, "unaligned branch target");
  lj_assertA(checki16((int32_t)(delta >> 1)), "s390x branch target out of range");
  emit_u32(as, S390X_INS_BRC(cc, (int32_t)(delta >> 1)));
}

static void emit_call(ASMState *as, Reg rlink, void *target)
{
  uint8_t *p = (uint8_t *)as->mcp - 8;
  ptrdiff_t delta = (char *)target - (char *)p;
  lj_assertA((delta & 1) == 0, "unaligned call target");
  lj_assertA(checki32((int64_t)(delta >> 1)), "s390x call target out of range");
  emit_u48_pad8(as, S390X_INS_BRASL(rlink, (int32_t)(delta >> 1)));
}

static void emit_callr(ASMState *as, Reg rlink, Reg target)
{
  emit_u32(as, (S390X_INS_BASR(rlink, target) << 16) | 0x0707u);
}

static void emit_jmp(ASMState *as, MCode *target)
{
  emit_condbranch(as, CC_AL, target);
}

static void emit_addptr(ASMState *as, Reg r, int32_t ofs)
{
  if (ofs == 0)
    return;
  if (checki16(ofs)) {
    emit_u32(as, S390X_INS_RI(S390XI_AGHI, r, ofs));
  } else {
    Reg tmp = (r == RID_TMP) ? RID_R1 : RID_TMP;
    emit_u32(as, S390X_INS_RXE(S390XI_AGR, r, tmp));
    emit_loadu64(as, tmp, (uint64_t)(int64_t)ofs);
  }
}

#define emit_spsub(as, ofs)	emit_addptr(as, RID_SP, -(ofs))
#define emit_branch_track(as)	UNUSED(as)
