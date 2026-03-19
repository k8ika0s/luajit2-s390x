/*
** S390X instruction emitter scaffolding.
** Copyright (C) 2005-2026 Mike Pall. See Copyright Notice in luajit.h
**
** This header starts with the minimal integer/control-flow subset needed
** for native JIT bring-up. More lowering still needs to be filled in.
*/

#define emit_u32(as, ins)	(*--(as)->mcp = (ins))

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

#define S390X_INS_RXE(op, r1, r2) \
  ((uint32_t)(op) | (((uint32_t)(r1) & 15u) << 4) | ((uint32_t)(r2) & 15u))
#define S390X_INS_RI(op, r, imm) \
  ((uint32_t)(op) | (((uint32_t)(r) & 15u) << 20) | (uint16_t)(imm))
#define S390X_INS_RXY(op, r1, x2, b2, disp) \
  ((uint64_t)(op) | (((uint64_t)(r1) & 15u) << 36) | \
   (((uint64_t)(x2) & 15u) << 32) | (((uint64_t)(b2) & 15u) << 28) | \
   (((uint64_t)(disp) & 0xfffu) << 16))
#define S390X_INS_RSYI(op, r1, r3, imm) \
  ((uint64_t)(op) | (((uint64_t)(r1) & 15u) << 36) | \
   (((uint64_t)(r3) & 15u) << 32) | (((uint64_t)(imm) & 0xfffu) << 16))
#define S390X_INS_BRC(cc, disp) \
  ((uint32_t)0xa7040000u | (((uint32_t)(cc) & 15u) << 20) | \
   (uint16_t)(disp))
#define S390X_INS_BRASL(r, disp) \
  ((uint64_t)0xc00500000000ull | (((uint64_t)(r) & 15u) << 36) | \
   ((uint32_t)(disp) & 0xffffffffu))

#define S390XI_LGR	0xb9040000u
#define S390XI_LGFR	0xb9140000u
#define S390XI_CGR	0xb9200000u
#define S390XI_CLGR	0xb9210000u
#define S390XI_LGHI	0xa7090000u
#define S390XI_AGHI	0xa70b0000u
#define S390XI_CGHI	0xa70f0000u
#define S390XI_AGR	0xb9080000u
#define S390XI_LG	0xe30000000004ull
#define S390XI_LLGF	0xe30000000016ull
#define S390XI_SRAG	0xeb000000000aull
#define S390XI_SRLG	0xeb000000000cull
#define S390XI_SLLG	0xeb000000000dull

/* Prefer rematerialization of BASE/L from global_State over spills. */
#define emit_canremat(ref)	((ref) <= REF_BASE)

static void emit_loadi(ASMState *as, Reg r, int32_t i)
{
  lj_assertA(checki16(i), "s390x immediate out of range");
  emit_u32(as, S390X_INS_RI(S390XI_LGHI, r, i));
}

static void emit_loadu64(ASMState *as, Reg r, uint64_t u64)
{
  lj_assertA(checki16((int64_t)u64), "s390x u64 immediate out of range");
  emit_loadi(as, r, (int32_t)u64);
}

static void emit_loadk64(ASMState *as, Reg r, IRIns *ir)
{
  UNUSED(as); UNUSED(r); UNUSED(ir);
}

static void emit_load64ofs(ASMState *as, Reg r, Reg base, int32_t ofs)
{
  lj_assertA(ofs >= 0 && ofs <= 0xfff, "s390x load64 offset out of range");
  emit_u48_pad8(as, S390X_INS_RXY(S390XI_LG, r, 0, base, ofs));
}

static void emit_loadu32ofs(ASMState *as, Reg r, Reg base, int32_t ofs)
{
  lj_assertA(ofs >= 0 && ofs <= 0xfff, "s390x load32 offset out of range");
  emit_u48_pad8(as, S390X_INS_RXY(S390XI_LLGF, r, 0, base, ofs));
}

static void emit_shiftimm(ASMState *as, uint64_t op, Reg r1, Reg r3, uint32_t imm)
{
  lj_assertA(imm <= 63, "s390x shift immediate out of range");
  emit_u48_pad8(as, S390X_INS_RSYI(op, r1, r3, imm));
}

#define emit_getgl(as, r, field)	((void)(as), (void)(r))
#define emit_setgl(as, r, field)	((void)(as), (void)(r))
#define emit_setvmstate(as, i)		((void)(as), (void)(i))

static void emit_loadofs(ASMState *as, IRIns *ir, Reg r, Reg base, int32_t ofs)
{
  UNUSED(as); UNUSED(ir); UNUSED(r); UNUSED(base); UNUSED(ofs);
}

static void emit_storeofs(ASMState *as, IRIns *ir, Reg r, Reg base, int32_t ofs)
{
  UNUSED(as); UNUSED(ir); UNUSED(r); UNUSED(base); UNUSED(ofs);
}

static void emit_movrr(ASMState *as, IRIns *ir, Reg dst, Reg src)
{
  UNUSED(ir);
  if (dst != src)
    emit_u32(as, S390X_INS_RXE(S390XI_LGR, dst, src));
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

static void emit_jmp(ASMState *as, MCode *target)
{
  emit_condbranch(as, CC_AL, target);
}

static void emit_addptr(ASMState *as, Reg r, int32_t ofs)
{
  UNUSED(as); UNUSED(r); UNUSED(ofs);
}

#define emit_spsub(as, ofs)	emit_addptr(as, RID_SP, -(ofs))
#define emit_branch_track(as)	UNUSED(as)
