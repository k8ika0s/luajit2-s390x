/*
** Definitions for IBM z/Architecture (s390x) CPUs.
** Copyright (C) 2005-2017 Mike Pall. See Copyright Notice in luajit.h
*/

#ifndef _LJ_TARGET_S390X_H
#define _LJ_TARGET_S390X_H

/* -- Registers IDs ------------------------------------------------------- */

#define GPRDEF(_) \
  _(R0) _(R1) _(R2) _(R3) _(R4) _(R5) _(R6) _(R7) \
  _(R8) _(R9) _(R10) _(R11) _(R12) _(R13) _(R14) _(R15)
#define FPRDEF(_) \
  _(F0) _(F1) _(F2) _(F3) \
  _(F4) _(F5) _(F6) _(F7) \
  _(F8) _(F9) _(F10) _(F11) \
  _(F12) _(F13) _(F14) _(F15)
#define VRIDDEF(_)

#define RIDENUM(name)	RID_##name,

enum {
  GPRDEF(RIDENUM)		/* General-purpose registers (GPRs). */
  FPRDEF(RIDENUM)		/* Floating-point registers (FPRs). */
  RID_MAX,
  RID_TMP = RID_R0,

  /* Calling conventions. */
  RID_RETHI = RID_R2,
  RID_RETLO = RID_R3,
  RID_SP = RID_R15,
  RID_RET = RID_R2,
  RID_FPRET = RID_F0,

  /* These definitions must match with the *.dasc file(s): */
  RID_GL = RID_R7,		/* On-trace global_State. */
  RID_LREG = RID_R8,		/* On-trace lua_State. */
  RID_BASE = RID_R13,		/* Interpreter BASE. */
  RID_LPC = RID_R9,		/* Interpreter PC. */
  RID_DISPATCH = RID_R10,	/* Interpreter DISPATCH table. */

  /* Register ranges [min, max) and number of registers. */
  RID_MIN_GPR = RID_R0,
  RID_MAX_GPR = RID_R15+1,
  RID_MIN_FPR = RID_F0,
  RID_MAX_FPR = RID_F15+1,
  RID_NUM_GPR = RID_MAX_GPR - RID_MIN_GPR,
  RID_NUM_FPR = RID_MAX_FPR - RID_MIN_FPR
};

#define RID_NUM_KREF		RID_NUM_GPR
#define RID_MIN_KREF		RID_R0

/* -- Register sets ------------------------------------------------------- */

/* Reserve non-allocatable VM registers from the allocator.
**
** BASE follows the usual LuaJIT backend contract and remains allocatable.
** Side-trace linking needs to be able to materialize REF_BASE into RID_BASE.
*/
#define RSET_FIXED \
  (RID2RSET(RID_TMP)|RID2RSET(RID_GL)|RID2RSET(RID_LREG)|\
   RID2RSET(RID_LPC)|RID2RSET(RID_DISPATCH)|\
   RID2RSET(RID_R14)|RID2RSET(RID_SP))
#define RSET_GPR_SAVED \
  (RID2RSET(RID_R6)|RID2RSET(RID_R7)|RID2RSET(RID_R8)|RID2RSET(RID_R9)|\
   RID2RSET(RID_R10)|RID2RSET(RID_R11)|RID2RSET(RID_R12)|RID2RSET(RID_R13))
#define RSET_GPR_BASE \
  (RID2RSET(RID_R11)|RID2RSET(RID_R12)|RID2RSET(RID_BASE))
#define RSET_GPR	(RSET_RANGE(RID_MIN_GPR, RID_MAX_GPR) - RSET_FIXED)
#define RSET_FPR	RSET_RANGE(RID_MIN_FPR, RID_MAX_FPR)
#define RSET_ALL	(RSET_GPR|RSET_FPR)
#define RSET_INIT	RSET_ALL

#define RSET_SCRATCH_GPR	RSET_RANGE(RID_R1, RID_R6+1)
#define RSET_SCRATCH_FPR	RSET_RANGE(RID_F0, RID_F7+1)
#define RSET_SCRATCH		(RSET_SCRATCH_GPR|RSET_SCRATCH_FPR)
#define REGARG_FIRSTGPR		RID_R2
#define REGARG_LASTGPR		RID_R6
#define REGARG_NUMGPR		5
/*
** s390x FP arguments use the even registers f0,f2,f4,f6. The backend call
** lowering must honor that physical mapping instead of assuming contiguity.
*/
#define REGARG_FIRSTFPR		RID_F0
#define REGARG_LASTFPR		RID_F6
#define REGARG_NUMFPR		4
#define S390X_CALL_SPS_EXTRA	20

/* -- Spill slots --------------------------------------------------------- */

/* Spill slots are 32 bit wide. An even/odd pair is used for FPRs.
**
** SPS_FIXED: Available fixed spill slots in interpreter frame.
** This definition must match with the *.dasc file(s).
**
** SPS_FIRST: First spill slot for general use. Reserve min. two 32 bit slots.
*/
#define SPS_FIXED	2
#define SPS_FIRST	2

#define SPOFS_TMP	0

#define sps_scale(slot)		(4 * (int32_t)(slot))
#define sps_align(slot)		(((slot) - SPS_FIXED + 1) & ~1)

/* -- Exit state ---------------------------------------------------------- */

/* This definition must match with the *.dasc file(s). */
typedef struct {
  lua_Number fpr[RID_NUM_FPR];	/* Floating-point registers. */
  intptr_t gpr[RID_NUM_GPR];	/* General-purpose registers. */
  int32_t spill[256];		/* Spill slots. */
} ExitState;

#define EXITSTATE_CHECKEXIT	1
#define EXITSTUB_SPACING	4
/* Avoid dependence on lj_jit.h if only including lj_target.h. */
#define exitstub_trace_addr(T, exitno) \
  ((MCode *)((char *)(T)->mcode + (T)->szmcode) + \
   EXITSTUB_SPACING * (exitno))

/* -- Instructions -------------------------------------------------------- */

typedef enum S390XCC {
  CC_OF = 1,
  CC_HI = 2,
  CC_GT = CC_HI,
  CC_LT = 4,
  CC_LO = CC_LT,
  CC_NE = 6,
  CC_EQ = 8,
  CC_GE = 10,
  CC_HS = CC_GE,
  CC_LE = 12,
  CC_LS = CC_LE,
  CC_AL = 15
} S390XCC;

#endif
