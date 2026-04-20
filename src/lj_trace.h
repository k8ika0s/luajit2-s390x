/*
** Trace management.
** Copyright (C) 2005-2026 Mike Pall. See Copyright Notice in luajit.h
*/

#ifndef _LJ_TRACE_H
#define _LJ_TRACE_H

#include "lj_obj.h"

#if LJ_HASJIT
#include "lj_jit.h"
#include "lj_dispatch.h"

/* Trace errors. */
typedef enum {
#define TREDEF(name, msg)	LJ_TRERR_##name,
#include "lj_traceerr.h"
  LJ_TRERR__MAX
} TraceError;

LJ_FUNC_NORET void lj_trace_err(jit_State *J, TraceError e);
LJ_FUNC_NORET void lj_trace_err_info(jit_State *J, TraceError e);

/* Trace management. */
LJ_FUNC GCtrace * LJ_FASTCALL lj_trace_alloc(lua_State *L, GCtrace *T);
LJ_FUNC void LJ_FASTCALL lj_trace_free(global_State *g, GCtrace *T);
LJ_FUNC void lj_trace_reenableproto(GCproto *pt);
LJ_FUNC void lj_trace_flushproto(global_State *g, GCproto *pt);
LJ_FUNC void lj_trace_flush(jit_State *J, TraceNo traceno);
LJ_FUNC int lj_trace_flushall(lua_State *L);
LJ_FUNC void lj_trace_initstate(global_State *g);
LJ_FUNC void lj_trace_freestate(global_State *g);
#if LJ_TARGET_S390X
LJ_FUNC int32_t lj_trace_s390x_varg_probe(const void *effp, int32_t ignored);
LJ_FUNC void lj_trace_s390x_iter_log(const TValue *base, const TValue *iterslot);
LJ_FUNC double lj_trace_s390x_const_step_loop_sum(double acc, int32_t idx,
							  int32_t stop,
							  double per_iter);
enum {
  LJ_S390X_CONST_STRUCT_SMALL_U32,
  LJ_S390X_CONST_STRUCT_SMALL_U64,
  LJ_S390X_CONST_STRUCT_ONE_FLOAT,
  LJ_S390X_CONST_STRUCT_ONE_DOUBLE,
  LJ_S390X_CONST_STRUCT_BIG_PAIR,
  LJ_S390X_CONST_STRUCT_HFA2D
};
LJ_FUNC double lj_trace_s390x_const_struct_loop_sum(double acc, int32_t idx,
						    int32_t stop, void *func,
						    int32_t kind, int32_t reps,
						    uint64_t lo, uint64_t hi);
LJ_FUNC uint64_t lj_trace_s390x_ffi_fixed_gpr_loop_sum(uint64_t acc,
						       int32_t idx,
						       int32_t stop,
						       int32_t slope,
						       int32_t intercept);
LJ_FUNC double lj_trace_s390x_ffi_fixed_fpr_loop_sum(double acc,
						     int32_t idx,
						     int32_t stop,
						     int32_t slope,
						     int32_t intercept);
LJ_FUNC int32_t lj_trace_s390x_ffi_fixed_step16_postidx(int32_t idx,
							int32_t stop);
LJ_FUNC double lj_trace_s390x_centered_mod_abs_loop_sum(double acc,
						       int32_t idx,
						       int32_t stop,
						       int32_t mod,
						       int32_t center);
LJ_FUNC double lj_trace_s390x_div_loop_accum4(double acc, int32_t idx,
					      int32_t stop);
LJ_FUNC double lj_trace_s390x_sqrt_loop_accum4(double acc, int32_t idx,
					       int32_t stop);
LJ_FUNC int32_t lj_trace_s390x_abs17_loop_sum(int32_t idx, int32_t stop);
LJ_FUNC int32_t lj_trace_s390x_const_i32_mod17_loop_sum(void *func,
							int32_t idx,
							int32_t stop);
LJ_FUNC int32_t lj_trace_s390x_mod_mul_loop_sum(int32_t idx, int32_t stop,
						int32_t mod, int32_t mul);
LJ_FUNC int32_t lj_trace_s390x_mod_select_loop_sum(int32_t idx, int32_t stop,
						   int32_t mod,
						   int32_t then_mul,
						   int32_t else_mul);
LJ_FUNC int32_t lj_trace_s390x_mod_rem_select_loop_sum(int32_t idx,
						       int32_t stop,
						       int32_t cond_mod,
						       int32_t rem_mod,
						       int32_t then_mul,
						       int32_t else_mul);
LJ_FUNC int32_t lj_trace_s390x_mod_scaled_loop_sum(int32_t idx, int32_t stop,
						   int32_t mod, int32_t mul);
LJ_FUNC int32_t lj_trace_s390x_mod_loop_sum(int32_t idx, int32_t stop,
					    int32_t mod);
LJ_FUNC int32_t lj_trace_s390x_mod97_loop_sum(int32_t idx, int32_t stop);
LJ_FUNC int32_t lj_trace_s390x_mod97_sub_loop_sum(int32_t idx, int32_t stop);
LJ_FUNC int32_t lj_trace_s390x_mod97_if5_else1_loop_sum(int32_t idx,
							int32_t stop);
LJ_FUNC int32_t lj_trace_s390x_mod97_if7_loop_sum(int32_t idx, int32_t stop);
LJ_FUNC int32_t lj_trace_s390x_mod97_if5_if3_loop_sum(int32_t idx,
						      int32_t stop);
LJ_FUNC double lj_trace_s390x_fpmod_quarter_loop_sum(int32_t idx,
						     int32_t stop);
LJ_FUNC double lj_trace_s390x_mixed_width_loop_sum(int32_t idx,
						  int32_t stop);
LJ_FUNC int32_t lj_trace_s390x_pair_loop_sum(int32_t idx, int32_t stop);
LJ_FUNC int32_t lj_trace_s390x_buffer_fref_loop_sum(int32_t idx,
						    int32_t stop);
LJ_FUNC double lj_trace_s390x_min_loop_sum(int32_t idx, int32_t stop);
LJ_FUNC double lj_trace_s390x_max_loop_sum(int32_t idx, int32_t stop);
LJ_FUNC int32_t lj_trace_s390x_scaled_tobit_loop_sum(int32_t idx,
						     int32_t stop,
						     int32_t mul);
LJ_FUNC int32_t lj_trace_s390x_logic_add_phi_remainder_sum(int32_t acc,
							   int32_t inner_idx,
							   int32_t inner_stop,
							   int32_t outer_stop);
LJ_FUNC int32_t lj_trace_s390x_logic_tail_add_sum(int32_t acc,
						  int32_t inner_idx,
						  int32_t inner_stop,
						  int32_t outer_idx,
						  int32_t outer_stop);
LJ_FUNC int32_t lj_trace_s390x_logic_tail_store_sum(int32_t outer_stop);
LJ_FUNC int32_t lj_trace_s390x_iter_table_loop_sum(int32_t acc,
						   int32_t idx,
						   int32_t stop,
						   int32_t per_iter);
LJ_FUNC int32_t lj_trace_s390x_mixed_noffi_tail_sum(int32_t acc,
						    int32_t idx,
						    int32_t stop);
#endif

/* Event handling. */
LJ_FUNC void lj_trace_ins(jit_State *J, const BCIns *pc);
LJ_FUNCA void LJ_FASTCALL lj_trace_hot(jit_State *J, const BCIns *pc);
LJ_FUNCA void LJ_FASTCALL lj_trace_stitch(jit_State *J, const BCIns *pc);
LJ_FUNCA int LJ_FASTCALL lj_trace_exit(jit_State *J, void *exptr);
#if LJ_UNWIND_EXT
LJ_FUNC uintptr_t LJ_FASTCALL lj_trace_unwind(jit_State *J, uintptr_t addr, ExitNo *ep);
#endif

/* Signal asynchronous abort of trace or end of trace. */
#define lj_trace_abort(g)	(G2J(g)->state &= ~LJ_TRACE_ACTIVE)
#define lj_trace_end(J)		(J->state = LJ_TRACE_END)

#else

#if LJ_TARGET_S390X
LJ_FUNC int32_t lj_trace_s390x_varg_probe(const void *effp, int32_t ignored);
LJ_FUNC void lj_trace_s390x_iter_log(const TValue *base, const TValue *iterslot);
#endif

#define lj_trace_flushall(L)	(UNUSED(L), 0)
#define lj_trace_initstate(g)	UNUSED(g)
#define lj_trace_freestate(g)	UNUSED(g)
#define lj_trace_abort(g)	UNUSED(g)
#define lj_trace_end(J)		UNUSED(J)

#endif

#endif
