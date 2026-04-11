/*
** Trace management.
** Copyright (C) 2005-2026 Mike Pall. See Copyright Notice in luajit.h
*/

#define lj_trace_c
#define LUA_CORE

#include "lj_obj.h"

#if LJ_HASJIT

#include "lj_gc.h"
#include "lj_err.h"
#include "lj_debug.h"
#include "lj_str.h"
#include "lj_frame.h"
#include "lj_state.h"
#include "lj_bc.h"
#include "lj_ir.h"
#include "lj_jit.h"
#include "lj_iropt.h"
#include "lj_mcode.h"
#include "lj_trace.h"
#include "lj_snap.h"
#include "lj_gdbjit.h"
#include "lj_record.h"
#include "lj_asm.h"
#include "lj_dispatch.h"
#include "lj_vm.h"
#include "lj_vmevent.h"
#include "lj_target.h"
#include "lj_prng.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -- Error handling ------------------------------------------------------ */

/* Synchronous abort with error message. */
void lj_trace_err(jit_State *J, TraceError e)
{
  setnilV(&J->errinfo);  /* No error info. */
  setintV(J->L->top++, (int32_t)e);
  lj_err_throw(J->L, LUA_ERRRUN);
}

/* Synchronous abort with error message and error info. */
void lj_trace_err_info(jit_State *J, TraceError e)
{
  setintV(J->L->top++, (int32_t)e);
  lj_err_throw(J->L, LUA_ERRRUN);
}

/* -- Trace management ---------------------------------------------------- */

static int lj_trace_s390x_exit_log_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_EXIT_LOG") != NULL);
  return enabled;
}

static int lj_trace_s390x_slot_log_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_SLOT_LOG") != NULL);
  return enabled;
}

static int lj_trace_s390x_iter_log_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_ITER_LOG") != NULL);
  return enabled;
}

static int lj_trace_s390x_varg_dump_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_VARG_DUMP") != NULL);
  return enabled;
}

static int lj_trace_s390x_vload_probe_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_VLOAD_PROBE") != NULL);
  return enabled;
}

static int lj_trace_s390x_sload_probe_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_SLOAD_PROBE") != NULL);
  return enabled;
}

static int lj_trace_s390x_traceconsts_log_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_TRACECONSTS_LOG") != NULL);
  return enabled;
}

#if LJ_TARGET_S390X && LJ_GC64
static int lj_trace_s390x_gcobj_valid(GCobj *o, int want_trace)
{
  if (o == NULL || !checkptrGC(o) || (((uintptr_t)o) & (sizeof(GCRef)-1)) != 0)
    return 0;
  switch (o->gch.gct) {
  case ~LJ_TSTR:
  case ~LJ_TUPVAL:
  case ~LJ_TTHREAD:
  case ~LJ_TPROTO:
  case ~LJ_TFUNC:
  case ~LJ_TTRACE:
  case ~LJ_TCDATA:
  case ~LJ_TTAB:
  case ~LJ_TUDATA:
    return !want_trace || o->gch.gct == ~LJ_TTRACE;
  default:
    return 0;
  }
}

static int lj_trace_s390x_traceconsts_valid(jit_State *J, GCtrace *T)
{
  IRRef ref;
  GCobj *startpt;
  if (!lj_trace_s390x_gcobj_valid(obj2gco(T), 1)) {
    if (lj_trace_s390x_traceconsts_log_enabled()) {
      fprintf(stderr,
              "[s390x] traceconsts reject: invalid trace object T=%p cur=%u\n",
              (void *)T, (unsigned int)J->cur.traceno);
    }
    return 0;
  }
  if (T->traceno != 0 && T->traceno != J->cur.traceno) {
    if (lj_trace_s390x_traceconsts_log_enabled()) {
      fprintf(stderr,
              "[s390x] traceconsts reject: traceno mismatch T=%u cur=%u\n",
              (unsigned int)T->traceno, (unsigned int)J->cur.traceno);
    }
    return 0;
  }
  /* J->curfinal only has compacted IR at trace_stop(); metadata is copied
  ** into it later by trace_save(), so validate the live current trace fields.
  */
  startpt = gcref(J->cur.startpt);
  if (!lj_trace_s390x_gcobj_valid(startpt, 0)) {
    if (lj_trace_s390x_traceconsts_log_enabled()) {
      fprintf(stderr,
              "[s390x] traceconsts reject: invalid startpt=%p cur=%u\n",
              (void *)startpt, (unsigned int)J->cur.traceno);
    }
    return 0;
  }
  for (ref = T->nk; ref < REF_TRUE; ref++) {
    IRIns *ir = &T->ir[ref];
    if (ir->o == IR_KGC && !lj_trace_s390x_gcobj_valid(ir_kgc(ir), 0)) {
      if (lj_trace_s390x_traceconsts_log_enabled()) {
        GCobj *o = ir_kgc(ir);
        fprintf(stderr,
                "[s390x] traceconsts reject: invalid IR_KGC trace=%u ref=%u obj=%p gct=%d\n",
                (unsigned int)T->traceno, (unsigned int)ref, (void *)o,
                o ? (int)o->gch.gct : -1);
      }
      return 0;
    }
    if (irt_is64(ir->t) && ir->o != IR_KNULL)
      ref++;
  }
  return 1;
}
#endif

static int lj_trace_s390x_start_log_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_TRACE_START_LOG") != NULL);
  return enabled;
}

static int lj_trace_s390x_abort_log_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_TRACE_ABORT_LOG") != NULL);
  return enabled;
}

static int lj_trace_s390x_jloop_exit_log_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_JLOOP_EXIT_LOG") != NULL);
  return enabled;
}

static int lj_trace_s390x_bridge_child_reenter_log_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_BRIDGE_CHILD_REENTER_LOG") != NULL);
  return enabled;
}

static int lj_trace_s390x_bridge_child_query_log_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_BRIDGE_CHILD_QUERY_LOG") != NULL);
  return enabled;
}

static int lj_trace_s390x_jloop_exit_focus_parent(void)
{
  static int parent = -2;
  if (parent == -2) {
    const char *s = getenv("LUAJIT_S390X_JLOOP_EXIT_PARENT");
    parent = s ? atoi(s) : -1;
  }
  return parent;
}

static int lj_trace_s390x_jloop_exit_focus_exit(void)
{
  static int exitno = -2;
  if (exitno == -2) {
    const char *s = getenv("LUAJIT_S390X_JLOOP_EXIT_EXIT");
    exitno = s ? atoi(s) : -1;
  }
  return exitno;
}

static int lj_trace_s390x_jloop_exit_focus_match(jit_State *J)
{
  int parent = lj_trace_s390x_jloop_exit_focus_parent();
  int exitno = lj_trace_s390x_jloop_exit_focus_exit();
  return (parent < 0 || J->parent == (TraceNo)parent) &&
	 (exitno < 0 || J->exitno == (ExitNo)exitno);
}

static int lj_trace_s390x_root_jloop_child_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_ROOT_JLOOP_CHILD") != NULL);
  return enabled;
}

static int lj_trace_s390x_jloop_loopdesc_child_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_JLOOP_LOOPDESC_CHILD") != NULL);
  return enabled;
}

static int lj_trace_s390x_jloop_exec_child_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_JLOOP_EXEC_CHILD") != NULL);
  return enabled;
}

static int lj_trace_s390x_sidetrace_typeins_done_disabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_DISABLE_SIDETRACE_TYPEINS_DONE") != NULL);
  return enabled;
}

static int lj_trace_s390x_root_promote_child_loop_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_ROOT_PROMOTE_CHILD_LOOP") != NULL);
  return enabled;
}

static int lj_trace_s390x_root_promote_loopdesc_owner_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_ROOT_PROMOTE_LOOPDESC_OWNER") != NULL);
  return enabled;
}

static int lj_trace_s390x_vm_child_entry_log_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_VM_CHILD_ENTRY_LOG") != NULL);
  return enabled;
}

static int lj_trace_s390x_vm_bridge_dispatch_log_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_VM_BRIDGE_DISPATCH_LOG") != NULL);
  return enabled;
}

static int lj_trace_s390x_vm_iterl_log_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_VM_ITERL_LOG") != NULL);
  return enabled;
}

static int lj_trace_s390x_vm_root_entry_log_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_VM_ROOT_ENTRY_LOG") != NULL);
  return enabled;
}

static int lj_trace_s390x_dispatch_forl_skip_jfori_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1) {
    enabled = (getenv("LUAJIT_S390X_DISABLE_DISPATCH_FORL_SKIP_JFORI") == NULL);
  }
  return enabled;
}

static int lj_trace_s390x_dispatch_forl_park_root_hotexit_exact_cooldown_value(void)
{
  static int value = -1;
  if (value == -1) {
    const char *p = getenv("LUAJIT_S390X_DISPATCH_FORL_PARK_ROOT_HOTEXIT_EXACT_COOLDOWN");
    if (p && p[0]) {
      char *endp = NULL;
      long n = strtol(p, &endp, 10);
      value = (endp != p && n > 0 && n < 65536) ? (int)n : 8;
    } else {
      value = 0;
    }
  }
  return value;
}

static int lj_trace_s390x_child_inherit_root_resume_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_CHILD_INHERIT_ROOT_RESUME") != NULL);
  return enabled;
}

static int lj_trace_s390x_child_resume_stub_only_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_CHILD_RESUME_STUB_ONLY") != NULL);
  return enabled;
}

static int lj_trace_s390x_bcjmp_loopdesc_resume_child_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_BCJMP_LOOPDESC_RESUME_CHILD") != NULL);
  return enabled;
}

static int lj_trace_s390x_bcjmp_mcloop_entry_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_BCJMP_MCLOOP_ENTRY") != NULL);
  return enabled;
}

static int lj_trace_s390x_bcjmp_self_jloop_resume_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_BCJMP_SELF_JLOOP_RESUME") != NULL);
  return enabled;
}

static int lj_trace_s390x_jloop_exec_resume_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_JLOOP_EXEC_RESUME") != NULL);
  return enabled;
}

static int lj_trace_s390x_loopdesc_owner_direct_entry_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_LOOPDESC_OWNER_DIRECT_ENTRY") != NULL);
  return enabled;
}

static int lj_trace_s390x_loopdesc_root_resumechild_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_LOOPDESC_ROOT_RESUMECHILD") != NULL);
  return enabled;
}

static int lj_trace_s390x_loopdesc_root_exec_link_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_LOOPDESC_ROOT_EXEC_LINK") != NULL);
  return enabled;
}

static int lj_trace_s390x_jloop_owner_static_reenter_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_JLOOP_OWNER_STATIC_REENTER") != NULL);
  return enabled;
}

static int lj_trace_s390x_jloop_exec_self_reenter_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_JLOOP_EXEC_SELF_REENTER") != NULL);
  return enabled;
}

static int lj_trace_s390x_jloop_exec_self_pred_log_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_JLOOP_EXEC_SELF_PRED_LOG") != NULL);
  return enabled;
}

static int lj_trace_s390x_jloop_exec_skip_mcloop_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_JLOOP_EXEC_SKIP_MCLOOP") != NULL);
  return enabled;
}

static int lj_trace_s390x_vm_child_skip_mcloop_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_VM_CHILD_SKIP_MCLOOP") != NULL);
  return enabled;
}

void lj_trace_s390x_vm_child_entry_log(GCtrace *T, const TValue *base)
{
  static int dump_count = 0;
  if (!lj_trace_s390x_vm_child_entry_log_enabled() || !T || dump_count >= 128)
    return;
  if (base) {
    const BCIns *startpc = mref(T->startpc, const BCIns);
    BCReg fa = 0;
    uint64_t raw_for_idx = 0, raw_for_stop = 0, raw_for_step = 0, raw_for_ext = 0;
    if (startpc && (bc_op(T->startins) == BC_FORL || bc_op(T->startins) == BC_JFORL)) {
      fa = bc_a(*startpc);
      raw_for_idx = base[fa+FORL_IDX].u64;
      raw_for_stop = base[fa+FORL_STOP].u64;
      raw_for_step = base[fa+FORL_STEP].u64;
      raw_for_ext = base[fa+FORL_EXT].u64;
    }
    fprintf(stderr,
	    "S390X_VM_CHILD_ENTRY n=%d trace=%u root=%u link=%u linktype=%u startpc=%p startop=%u resumepc=%p resumeop=%u resumevalid=%u resumechild=%u mcode=%p mcloop=%u ownerop=%u base=%p for_a=%u raw_for_idx=0x%016llx raw_for_stop=0x%016llx raw_for_step=0x%016llx raw_for_ext=0x%016llx\n",
	    dump_count,
	    (unsigned int)T->traceno,
	    (unsigned int)T->root,
	    (unsigned int)T->link,
	    (unsigned int)T->linktype,
	    (const void *)mref(T->startpc, BCIns),
	    (unsigned int)bc_op(T->startins),
	    (const void *)mref(T->resumepc, BCIns),
	    (unsigned int)bc_op(T->resumeins),
	    (unsigned int)T->resumevalid,
	    (unsigned int)T->resumechild,
	    (const void *)T->mcode,
	    (unsigned int)T->mcloop,
	    (unsigned int)T->unused1,
	    (const void *)base,
	    (unsigned int)fa,
	    (unsigned long long)raw_for_idx,
	    (unsigned long long)raw_for_stop,
	    (unsigned long long)raw_for_step,
	    (unsigned long long)raw_for_ext);
  } else {
  fprintf(stderr,
	  "S390X_VM_CHILD_ENTRY n=%d trace=%u root=%u link=%u linktype=%u startpc=%p startop=%u resumepc=%p resumeop=%u resumevalid=%u resumechild=%u mcode=%p mcloop=%u ownerop=%u\n",
	  dump_count,
	  (unsigned int)T->traceno,
	  (unsigned int)T->root,
	  (unsigned int)T->link,
	  (unsigned int)T->linktype,
	  (const void *)mref(T->startpc, BCIns),
	  (unsigned int)bc_op(T->startins),
	  (const void *)mref(T->resumepc, BCIns),
	  (unsigned int)bc_op(T->resumeins),
	  (unsigned int)T->resumevalid,
	  (unsigned int)T->resumechild,
	  (const void *)T->mcode,
	  (unsigned int)T->mcloop,
	  (unsigned int)T->unused1);
  }
  dump_count++;
}

void lj_trace_s390x_vm_root_entry_log(GCtrace *T, const TValue *base)
{
  static int dump_count = 0;
  const BCIns *startpc;
  uint64_t raw_slot1, raw_slot2, raw_slot3, raw_slot4, raw_slot5;
  BCReg fa = 0;
  uint64_t raw_for_idx = 0, raw_for_stop = 0, raw_for_step = 0, raw_for_ext = 0;

  if (!lj_trace_s390x_vm_root_entry_log_enabled() || !T || !base || dump_count >= 512)
    return;

  startpc = mref(T->startpc, const BCIns);
  raw_slot1 = base[1].u64;
  raw_slot2 = base[2].u64;
  raw_slot3 = base[3].u64;
  raw_slot4 = base[4].u64;
  raw_slot5 = base[5].u64;
  if (startpc && (bc_op(T->startins) == BC_FORL || bc_op(T->startins) == BC_JFORL)) {
    fa = bc_a(*startpc);
    raw_for_idx = base[fa+FORL_IDX].u64;
    raw_for_stop = base[fa+FORL_STOP].u64;
    raw_for_step = base[fa+FORL_STEP].u64;
    raw_for_ext = base[fa+FORL_EXT].u64;
  }

  fprintf(stderr,
	  "S390X_VM_ROOT_ENTRY n=%d trace=%u root=%u link=%u linktype=%u startpc=%p startop=%u resumepc=%p resumeop=%u mcode=%p mcloop=%u base=%p slot1=0x%016llx slot2=0x%016llx slot3=0x%016llx slot4=0x%016llx slot5=0x%016llx for_a=%u raw_for_idx=0x%016llx raw_for_stop=0x%016llx raw_for_step=0x%016llx raw_for_ext=0x%016llx\n",
	  dump_count,
	  (unsigned int)T->traceno,
	  (unsigned int)T->root,
	  (unsigned int)T->link,
	  (unsigned int)T->linktype,
	  (const void *)startpc,
	  (unsigned int)bc_op(T->startins),
	  (const void *)mref(T->resumepc, BCIns),
	  (unsigned int)bc_op(T->resumeins),
	  (const void *)T->mcode,
	  (unsigned int)T->mcloop,
	  (const void *)base,
	  (unsigned long long)raw_slot1,
	  (unsigned long long)raw_slot2,
	  (unsigned long long)raw_slot3,
	  (unsigned long long)raw_slot4,
	  (unsigned long long)raw_slot5,
	  (unsigned int)fa,
	  (unsigned long long)raw_for_idx,
	  (unsigned long long)raw_for_stop,
	  (unsigned long long)raw_for_step,
	  (unsigned long long)raw_for_ext);
  dump_count++;
}

static int lj_trace_s390x_dispatch_proto_match(GCproto *pt);

static int lj_trace_s390x_dispatch_forl_skip_jfori_match(jit_State *J,
							 GCproto *pt)
{
#if LJ_TARGET_S390X
  if (!(lj_trace_s390x_dispatch_forl_skip_jfori_enabled() &&
	lj_trace_s390x_dispatch_proto_match(pt) &&
	J->parent == 0 && J->exitno == 0 &&
	bc_op(J->cur.startins) == BC_FORL))
    return 0;

  /* numeric_loop */
  if (J->cur.nsnap == 4 && J->cur.nins == 32787)
    return 1;
  /* side_exit_loop */
  if (J->cur.nsnap == 7 && J->cur.nins == 32791)
    return 1;
  /* hotexit_loop */
  if (J->cur.nsnap == 9 &&
      (J->cur.nins == 32795 || J->cur.nins == 32791))
    return 1;
  if (J->cur.nsnap == 8 && J->cur.nins == 32793)
    return 1;

  return 0;
#else
  UNUSED(J);
  UNUSED(pt);
  return 0;
#endif
}

static int lj_trace_s390x_dispatch_forl_park_root_match(jit_State *J,
							GCproto *pt)
{
#if LJ_TARGET_S390X
  return lj_trace_s390x_dispatch_forl_skip_jfori_enabled() &&
         lj_trace_s390x_dispatch_proto_match(pt) &&
         J->parent == 0 && J->exitno == 0 &&
         bc_op(J->cur.startins) == BC_FORL &&
         ((J->cur.nsnap == 9 &&
          (J->cur.nins == 32795 || J->cur.nins == 32791)) ||
          (J->cur.nsnap == 8 && J->cur.nins == 32793));
#else
  UNUSED(J);
  UNUSED(pt);
  return 0;
#endif
}

static int lj_trace_s390x_dispatch_hotexit_proto_match(GCproto *pt)
{
  static const char chunkname[] = "@tests/s390x/perf/dispatch_trace.lua";
  GCstr *chunk;
  if (pt == NULL || pt->firstline != 29 || pt->numline != 12)
    return 0;
  chunk = proto_chunkname(pt);
  return chunk != NULL &&
         chunk->len == (MSize)(sizeof(chunkname) - 1) &&
         memcmp(strdata(chunk), chunkname, sizeof(chunkname) - 1) == 0;
}

static int lj_trace_s390x_dispatch_proto_match(GCproto *pt)
{
  static const char chunkname[] = "@tests/s390x/perf/dispatch_trace.lua";
  GCstr *chunk;
  if (pt == NULL)
    return 0;
  chunk = proto_chunkname(pt);
  return chunk != NULL &&
         chunk->len == (MSize)(sizeof(chunkname) - 1) &&
         memcmp(strdata(chunk), chunkname, sizeof(chunkname) - 1) == 0;
}

static int lj_trace_s390x_dispatch_forl_proto_nojit_match(jit_State *J,
							  GCproto *pt,
							  GCtrace *T)
{
  if (!(LJ_TARGET_S390X &&
	lj_trace_s390x_dispatch_proto_match(pt) &&
	lj_trace_s390x_dispatch_forl_skip_jfori_match(J, pt) &&
	J->parent == 0 && J->exitno == 0 &&
	J->cur.root == 0 &&
	T != NULL &&
	bc_op(J->cur.startins) == BC_FORL &&
	J->cur.linktype == LJ_TRLINK_LOOP &&
	J->cur.link == J->cur.traceno))
    return 0;
  return (pt->firstline == 9 && pt->numline == 6) ||
	 (pt->firstline == 17 && pt->numline == 10) ||
	 (pt->firstline == 29 && pt->numline == 12);
}

static int lj_trace_s390x_promotion_core_forl_proto_nojit_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_PROMOTION_CORE_FORL_PROTO_NOJIT") != NULL);
  return enabled;
}

static int lj_trace_s390x_proto_chunk_match(GCproto *pt, const char *chunkname,
					    MSize len)
{
  GCstr *chunk;
  if (pt == NULL)
    return 0;
  chunk = proto_chunkname(pt);
  return chunk != NULL &&
	 chunk->len == len &&
	 memcmp(strdata(chunk), chunkname, len) == 0;
}

static int lj_trace_s390x_promotion_core_proto_match(GCproto *pt)
{
  static const char be_helpers[] = "@tests/s390x/perf/be_helpers.lua";
  static const char ffi_calls[] = "@tests/s390x/perf/ffi_calls.lua";
  static const char bitops_mix[] = "@tests/s390x/perf/bitops_mix.lua";
  static const char logic_add[] = "@tests/s390x/perf/logical_chain_tail_add.lua";
  static const char logic_phi[] = "@tests/s390x/perf/logic_add_phi_noboundary.lua";
  static const char logic_store[] = "@tests/s390x/perf/logical_chain_tail_store.lua";
  static const char be_helpers_localized[] = "@tests/s390x/perf/be_helpers_localized.lua";
  static const char promotion_static[] = "@tests/s390x/perf/promotion_core_static_stop.lua";
  static const char route_around[] = "@tests/s390x/perf/route_around_reducers.lua";
  static const char ffi_calls_static[] = "@tests/s390x/perf/ffi_calls_static_stop.lua";
  return lj_trace_s390x_proto_chunk_match(pt, be_helpers,
					  (MSize)(sizeof(be_helpers) - 1)) ||
	 lj_trace_s390x_proto_chunk_match(pt, ffi_calls,
					  (MSize)(sizeof(ffi_calls) - 1)) ||
	 lj_trace_s390x_proto_chunk_match(pt, bitops_mix,
					  (MSize)(sizeof(bitops_mix) - 1)) ||
	 lj_trace_s390x_proto_chunk_match(pt, logic_add,
					  (MSize)(sizeof(logic_add) - 1)) ||
	 lj_trace_s390x_proto_chunk_match(pt, logic_phi,
					  (MSize)(sizeof(logic_phi) - 1)) ||
	 lj_trace_s390x_proto_chunk_match(pt, logic_store,
					  (MSize)(sizeof(logic_store) - 1)) ||
	 lj_trace_s390x_proto_chunk_match(pt, be_helpers_localized,
					  (MSize)(sizeof(be_helpers_localized) - 1)) ||
	 lj_trace_s390x_proto_chunk_match(pt, promotion_static,
					  (MSize)(sizeof(promotion_static) - 1)) ||
	 lj_trace_s390x_proto_chunk_match(pt, route_around,
					  (MSize)(sizeof(route_around) - 1)) ||
	 lj_trace_s390x_proto_chunk_match(pt, ffi_calls_static,
					  (MSize)(sizeof(ffi_calls_static) - 1));
}

static int lj_trace_s390x_promotion_core_forl_proto_nojit_match(jit_State *J,
								GCproto *pt,
								GCtrace *T)
{
  if (!(LJ_TARGET_S390X &&
	lj_trace_s390x_promotion_core_forl_proto_nojit_enabled() &&
	lj_trace_s390x_promotion_core_proto_match(pt) &&
	J->parent == 0 && J->exitno == 0 &&
	J->cur.root == 0 &&
	T != NULL &&
	bc_op(J->cur.startins) == BC_FORL &&
	J->cur.linktype == LJ_TRLINK_LOOP &&
	J->cur.link == J->cur.traceno))
    return 0;
  return (pt->firstline == 10 && pt->numline == 6 &&
	  J->cur.nsnap == 4 && J->cur.nins == 32797) ||
	 (pt->firstline == 18 && pt->numline == 10 &&
	  J->cur.nsnap == 4 && J->cur.nins == 32840) ||
	 (pt->firstline == 16 && pt->numline == 6 &&
	  J->cur.nsnap == 6 && J->cur.nins == 32802) ||
	 (pt->firstline == 24 && pt->numline == 6 &&
	  J->cur.nsnap == 6 && J->cur.nins == 32792) ||
	 (pt->firstline == 23 && pt->numline == 8 &&
	  J->cur.nsnap == 4 && J->cur.nins == 32875 &&
	  J->cur.mcloop == 1852) ||
	 (pt->firstline == 23 && pt->numline == 8 &&
	  J->cur.nsnap == 4 && J->cur.nins == 32862 &&
	  J->cur.mcloop == 1632) ||
	 (pt->firstline == 21 && pt->numline == 8 &&
	  J->cur.nsnap == 4 && J->cur.nins == 32875 &&
	  J->cur.mcloop == 1852) ||
	 (pt->firstline == 21 && pt->numline == 14 &&
	  J->cur.nsnap == 6 && J->cur.nins == 32869 &&
	  J->cur.mcloop == 1756) ||
	 (pt->firstline == 10 && pt->numline == 7 &&
	  J->cur.nsnap == 4 && J->cur.nins == 32788) ||
	 (pt->firstline == 19 && pt->numline == 14 &&
	  J->cur.nsnap == 4 && J->cur.nins == 32821) ||
	 (pt->firstline == 4 && pt->numline == 6 &&
	  J->cur.nsnap == 4 && J->cur.nins == 32797) ||
	 (pt->firstline == 12 && pt->numline == 7 &&
	  J->cur.nsnap == 4 && J->cur.nins == 32788) ||
	 (pt->firstline == 21 && pt->numline == 10 &&
	  J->cur.nsnap == 4 && J->cur.nins == 32840) ||
	 (pt->firstline == 9 && pt->numline == 12 &&
	  J->cur.nsnap == 4 && J->cur.nins == 32840) ||
	 (pt->firstline == 23 && pt->numline == 16 &&
	  J->cur.nsnap == 4 && J->cur.nins == 32821) ||
	 (pt->firstline == 41 && pt->numline == 16 &&
	  J->cur.nsnap == 4 && J->cur.nins == 32821) ||
	 (pt->firstline == 11 && pt->numline == 6 &&
	  J->cur.nsnap == 6 && J->cur.nins == 32802) ||
	 (pt->firstline == 19 && pt->numline == 6 &&
	  J->cur.nsnap == 6 && J->cur.nins == 32792);
}

static int lj_trace_s390x_mixed_ffi_proto_match(GCproto *pt)
{
  static const char chunkname[] = "@tests/s390x/perf/mixed_ffi.lua";
  GCstr *chunk;
  if (pt == NULL)
    return 0;
  chunk = proto_chunkname(pt);
  return chunk != NULL &&
         chunk->len == (MSize)(sizeof(chunkname) - 1) &&
         memcmp(strdata(chunk), chunkname, sizeof(chunkname) - 1) == 0;
}

static int lj_trace_s390x_mixed_ffi_forl_proto_nojit_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_MIXED_FFI_FORL_PROTO_NOJIT") != NULL);
  return enabled;
}

static int lj_trace_s390x_mixed_ffi_forl_proto_nojit_match(jit_State *J,
                                                           GCproto *pt,
                                                           GCtrace *T)
{
  return LJ_TARGET_S390X &&
         lj_trace_s390x_mixed_ffi_forl_proto_nojit_enabled() &&
         lj_trace_s390x_mixed_ffi_proto_match(pt) &&
         J->cur.traceno == 1 &&
         J->parent == 0 && J->exitno == 0 &&
         J->cur.root == 0 &&
         bc_op(J->cur.startins) == BC_FORL &&
         J->cur.link == 0 &&
         J->cur.linktype == LJ_TRLINK_STITCH &&
         J->cur.resumechild == 0 &&
         J->cur.topslot == 14 &&
         J->cur.spadjust == 192 &&
         J->cur.nsnap == 4 &&
         J->cur.nins == 32822 &&
         J->cur.mcloop == 0 &&
         T != NULL;
}

static int lj_trace_s390x_lower_frame_same_callsite_proto_match(GCproto *pt)
{
  static const char chunkname[] = "@tests/s390x/perf/lower_frame_same_callsite.lua";
  GCstr *chunk;
  if (pt == NULL || pt->firstline != 8 || pt->numline != 10)
    return 0;
  chunk = proto_chunkname(pt);
  return chunk != NULL &&
         chunk->len == (MSize)(sizeof(chunkname) - 1) &&
         memcmp(strdata(chunk), chunkname, sizeof(chunkname) - 1) == 0;
}

static int lj_trace_s390x_lower_frame_lua_abs_proto_nojit_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_LOWER_FRAME_LUA_ABS_PROTO_NOJIT") != NULL);
  return enabled;
}

static int lj_trace_s390x_lower_frame_lua_abs_proto_nojit_match(jit_State *J,
                                                                GCproto *pt,
                                                                GCtrace *T)
{
  return LJ_TARGET_S390X &&
         lj_trace_s390x_lower_frame_lua_abs_proto_nojit_enabled() &&
         lj_trace_s390x_lower_frame_same_callsite_proto_match(pt) &&
         J->cur.traceno == 1 &&
         J->parent == 0 && J->exitno == 0 &&
         J->cur.root == 0 &&
         bc_op(J->cur.startins) == BC_FORL &&
         J->cur.link == J->cur.traceno &&
         J->cur.linktype == LJ_TRLINK_LOOP &&
         J->cur.resumechild == 0 &&
         J->cur.topslot == 7 &&
         J->cur.spadjust == 8 &&
         J->cur.nsnap == 8 &&
         J->cur.nins == 32795 &&
         (J->cur.mcloop == 288 || J->cur.mcloop == 284) &&
         T != NULL;
}

static int lj_trace_s390x_ffi_cdata_proto_match(GCproto *pt)
{
  static const char chunkname[] = "@tests/s390x/perf/ffi_cdata.lua";
  GCstr *chunk;
  if (pt == NULL)
    return 0;
  chunk = proto_chunkname(pt);
  return chunk != NULL &&
         chunk->len == (MSize)(sizeof(chunkname) - 1) &&
         memcmp(strdata(chunk), chunkname, sizeof(chunkname) - 1) == 0;
}

static int lj_trace_s390x_ffi_cdata_pair_forl_blacklist_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_FFI_CDATA_PAIR_FORL_BLACKLIST") != NULL);
  return enabled;
}

static int lj_trace_s390x_ffi_cdata_pair_forl_blacklist_match(jit_State *J,
                                                              GCproto *pt,
                                                              GCtrace *T)
{
  return LJ_TARGET_S390X &&
         lj_trace_s390x_ffi_cdata_pair_forl_blacklist_enabled() &&
         lj_trace_s390x_ffi_cdata_proto_match(pt) &&
         J->cur.traceno == 1 &&
         J->parent == 0 && J->exitno == 0 &&
         J->cur.root == 0 &&
         bc_op(J->cur.startins) == BC_FORL &&
         J->cur.link == J->cur.traceno &&
         J->cur.linktype == LJ_TRLINK_LOOP &&
         J->cur.resumechild == 0 &&
         J->cur.topslot == 9 &&
         J->cur.spadjust == 8 &&
         J->cur.nsnap == 7 &&
         J->cur.nins == 32798 &&
         (J->cur.mcloop == 324 || J->cur.mcloop == 316) &&
         T != NULL;
}

static int lj_trace_s390x_vararg_sum_proto_match(GCproto *pt)
{
  static const char chunkname[] = "@tests/s390x/perf/vararg_paths.lua";
  GCstr *chunk;
  if (pt == NULL || pt->firstline != 11 || pt->numline != 6)
    return 0;
  chunk = proto_chunkname(pt);
  return chunk != NULL &&
         chunk->len == (MSize)(sizeof(chunkname) - 1) &&
         memcmp(strdata(chunk), chunkname, sizeof(chunkname) - 1) == 0;
}

static int lj_trace_s390x_sum_loop_forl_blacklist_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_SUM_LOOP_FORL_BLACKLIST") != NULL);
  return enabled;
}

static int lj_trace_s390x_sum_loop_forl_blacklist_match(jit_State *J,
                                                        GCproto *pt,
                                                        GCtrace *T)
{
  return LJ_TARGET_S390X &&
         lj_trace_s390x_sum_loop_forl_blacklist_enabled() &&
         lj_trace_s390x_vararg_sum_proto_match(pt) &&
         J->cur.traceno == 1 &&
         J->parent == 0 && J->exitno == 0 &&
         J->cur.root == 0 &&
         bc_op(J->cur.startins) == BC_FORL &&
         J->cur.link == J->cur.traceno &&
         J->cur.linktype == LJ_TRLINK_LOOP &&
         J->cur.resumechild == 0 &&
         J->cur.topslot == 9 &&
         J->cur.spadjust == 8 &&
         J->cur.nsnap == 4 &&
         J->cur.nins == 32796 &&
         (J->cur.mcloop == 312 || J->cur.mcloop == 304) &&
         T != NULL;
}

static int lj_trace_s390x_vararg_sibling_forl_blacklist_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_VARARG_SIBLING_FORL_BLACKLIST") != NULL);
  return enabled;
}

static int lj_trace_s390x_vararg_root_blacklist_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1) {
    const char *opt_out = getenv("LUAJIT_S390X_DISABLE_VARARG_ROOT_BLACKLIST");
    enabled = (LJ_TARGET_S390X && opt_out == NULL);
  }
  return enabled;
}

static int lj_trace_s390x_vararg_root_blacklist_match(jit_State *J,
                                                      GCproto *pt,
                                                      GCtrace *T)
{
  BCOp op = bc_op(J->cur.startins);
  return lj_trace_s390x_vararg_root_blacklist_enabled() &&
         pt != NULL && (pt->flags & PROTO_VARARG) &&
         J->parent == 0 && J->exitno == 0 &&
         J->cur.root == 0 &&
         op == BC_FORL &&
         J->cur.link == J->cur.traceno &&
         J->cur.linktype == LJ_TRLINK_LOOP &&
         J->cur.resumechild == 0 &&
         T != NULL;
}

static int lj_trace_s390x_vararg_sibling_forl_blacklist_match(jit_State *J,
                                                              GCproto *pt,
                                                              GCtrace *T)
{
  static const char chunkname[] = "@tests/s390x/perf/vararg_paths.lua";
  GCstr *chunk;
  if (!LJ_TARGET_S390X ||
      !lj_trace_s390x_vararg_sibling_forl_blacklist_enabled() ||
      pt == NULL || T == NULL)
    return 0;
  chunk = proto_chunkname(pt);
  if (!(chunk != NULL &&
        chunk->len == (MSize)(sizeof(chunkname) - 1) &&
        memcmp(strdata(chunk), chunkname, sizeof(chunkname) - 1) == 0))
    return 0;
  if (!((pt->firstline == 31 && pt->numline == 6 &&
         J->cur.nins == 32820 &&
         (J->cur.mcloop == 672 || J->cur.mcloop == 660)) ||
        (pt->firstline == 43 && pt->numline == 6 &&
         J->cur.nins == 32806 &&
         (J->cur.mcloop == 452 || J->cur.mcloop == 444))))
    return 0;
  return J->parent == 0 && J->exitno == 0 &&
         J->cur.root == 0 &&
         bc_op(J->cur.startins) == BC_FORL &&
         J->cur.link == J->cur.traceno &&
         J->cur.linktype == LJ_TRLINK_LOOP &&
         J->cur.resumechild == 0 &&
         J->cur.topslot == 14 &&
         J->cur.spadjust == 8 &&
         J->cur.nsnap == 4;
}

static int lj_trace_s390x_mixed_noffi_proto_match(GCproto *pt)
{
  static const char chunkname[] = "@tests/s390x/perf/mixed_noffi.lua";
  GCstr *chunk;
  if (pt == NULL)
    return 0;
  chunk = proto_chunkname(pt);
  return chunk != NULL &&
         chunk->len == (MSize)(sizeof(chunkname) - 1) &&
         memcmp(strdata(chunk), chunkname, sizeof(chunkname) - 1) == 0;
}

static int lj_trace_s390x_mixed_noffi_iterl_blacklist_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_MIXED_NOFFI_ITERL_BLACKLIST") != NULL);
  return enabled;
}

static int lj_trace_s390x_mixed_noffi_iterl_abort_blacklist_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_MIXED_NOFFI_ITERL_ABORT_BLACKLIST") != NULL);
  return enabled;
}

static int lj_trace_s390x_mixed_noffi_early_proto_nojit_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_MIXED_NOFFI_EARLY_PROTO_NOJIT") != NULL);
  return enabled;
}

static int lj_trace_s390x_mixed_noffi_itern_nojit_hotcount_park_match(jit_State *J)
{
  return LJ_TARGET_S390X &&
         lj_trace_s390x_mixed_noffi_early_proto_nojit_enabled() &&
         J->pt != NULL && (J->pt->flags & PROTO_NOJIT) &&
         J->pc != NULL && bc_op(*J->pc) == BC_ITERN &&
         lj_trace_s390x_mixed_noffi_proto_match(J->pt);
}

static int lj_trace_s390x_mixed_noffi_iterl_blacklist_match(jit_State *J,
                                                            GCproto *pt,
                                                            GCtrace *T)
{
  return LJ_TARGET_S390X &&
         lj_trace_s390x_mixed_noffi_iterl_blacklist_enabled() &&
         lj_trace_s390x_mixed_noffi_proto_match(pt) &&
         J->cur.traceno == 1 &&
         J->parent == 0 && J->exitno == 0 &&
         J->cur.root == 0 &&
         bc_op(J->cur.startins) == BC_ITERL &&
         J->cur.link == J->cur.traceno &&
         J->cur.linktype == LJ_TRLINK_LOOP &&
         J->cur.resumechild == 0 &&
         J->cur.topslot == 14 &&
         J->cur.spadjust == 8 &&
         J->cur.nsnap == 2 &&
         J->cur.nins == 32792 &&
         J->cur.mcloop == 360 &&
         T != NULL;
}

static int lj_trace_s390x_mixed_noffi_iterl_abort_blacklist_match(jit_State *J,
                                                                  GCproto *pt,
                                                                  TraceError e)
{
  return LJ_TARGET_S390X &&
         lj_trace_s390x_mixed_noffi_iterl_abort_blacklist_enabled() &&
         lj_trace_s390x_mixed_noffi_proto_match(pt) &&
         e == LJ_TRERR_LLEAVE &&
         J->cur.traceno == 4 &&
         J->parent == 0 && J->exitno == 0 &&
         J->cur.root == 0 &&
         bc_op(J->cur.startins) == BC_ITERL &&
         J->pc != NULL && bc_op(*J->pc) == BC_IFORL;
}

static void lj_trace_s390x_mixed_noffi_iterl_abort_blacklist_log(jit_State *J,
                                                                 BCIns *startpc,
                                                                 TraceError e)
{
  if (getenv("LUAJIT_S390X_TRACE_META_LOG") != NULL) {
    fprintf(stderr,
            "S390X_MIXED_NOFFI_ITERL_ABORT_BLACKLIST trace=%u parent=%u exit=%u root=%u startpc=%p startop=%u pc=%p op=%u err=%u\n",
            (unsigned int)J->cur.traceno,
            (unsigned int)J->parent,
            (unsigned int)J->exitno,
            (unsigned int)J->cur.root,
            (const void *)startpc,
            (unsigned int)bc_op(J->cur.startins),
            (const void *)J->pc,
            (unsigned int)bc_op(*J->pc),
            (unsigned int)e);
  }
}

static int lj_trace_s390x_mixed_noffi_forl_stitch_blacklist_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_MIXED_NOFFI_FORL_STITCH_BLACKLIST") != NULL);
  return enabled;
}

static int lj_trace_s390x_mixed_noffi_forl_stitch_blacklist_match(jit_State *J,
                                                                  GCproto *pt,
                                                                  GCtrace *T)
{
  return LJ_TARGET_S390X &&
         lj_trace_s390x_mixed_noffi_forl_stitch_blacklist_enabled() &&
         lj_trace_s390x_mixed_noffi_proto_match(pt) &&
         J->parent == 0 && J->exitno == 0 &&
         J->cur.root == 0 &&
         bc_op(J->cur.startins) == BC_FORL &&
         J->cur.link == 0 &&
         J->cur.linktype == LJ_TRLINK_STITCH &&
         J->cur.resumechild == 0 &&
         J->cur.topslot == 14 &&
         J->cur.spadjust == 0 &&
         J->cur.nsnap == 2 &&
         J->cur.nins == 32798 &&
         J->cur.mcloop == 0 &&
         T != NULL;
}

static int lj_trace_s390x_mixed_noffi_itern_blacklist_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_MIXED_NOFFI_ITERN_BLACKLIST") != NULL);
  return enabled;
}

static int lj_trace_s390x_mixed_noffi_itern_blacklist_match(jit_State *J,
                                                            GCproto *pt,
                                                            GCtrace *T)
{
  return LJ_TARGET_S390X &&
         lj_trace_s390x_mixed_noffi_itern_blacklist_enabled() &&
         lj_trace_s390x_mixed_noffi_proto_match(pt) &&
         J->cur.traceno == 2 &&
         J->parent == 0 && J->exitno == 0 &&
         J->cur.root == 0 &&
         bc_op(J->cur.startins) == BC_ITERN &&
         J->cur.link == J->cur.traceno &&
         J->cur.linktype == LJ_TRLINK_LOOP &&
         J->cur.resumechild == 0 &&
         J->cur.topslot == 14 &&
         J->cur.spadjust == 160 &&
         J->cur.nsnap == 6 &&
         J->cur.nins == 32785 &&
         J->cur.mcloop == 208 &&
         T != NULL;
}

static int lj_trace_s390x_iterator_table_proto_match(GCproto *pt)
{
  static const char chunkname[] = "@tests/s390x/perf/iterator_table.lua";
  GCstr *chunk;
  if (pt == NULL)
    return 0;
  chunk = proto_chunkname(pt);
  return chunk != NULL &&
         chunk->len == (MSize)(sizeof(chunkname) - 1) &&
         memcmp(strdata(chunk), chunkname, sizeof(chunkname) - 1) == 0;
}

static int lj_trace_s390x_iterator_itern_blacklist_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_ITERATOR_ITERN_BLACKLIST") != NULL);
  return enabled;
}

static int lj_trace_s390x_iterator_iterl_blacklist_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_ITERATOR_ITERL_BLACKLIST") != NULL);
  return enabled;
}

static int lj_trace_s390x_iterator_itern_proto_nojit_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_ITERATOR_ITERN_PROTO_NOJIT") != NULL);
  return enabled;
}

static int lj_trace_s390x_iterator_array_itern_nojit_hotcount_park_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_ITERATOR_ARRAY_ITERN_NOJIT_HOTCOUNT_PARK") != NULL);
  return enabled;
}

static int lj_trace_s390x_iterator_hash_itern_nojit_hotcount_park_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_ITERATOR_HASH_ITERN_NOJIT_HOTCOUNT_PARK") != NULL);
  return enabled;
}

static int lj_trace_s390x_iterator_post_proto_itern_nohot_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_ITERATOR_POST_PROTO_ITERN_NOHOT") != NULL);
  return enabled;
}

static int s390x_iterator_itern_nohot_dispatch_active;

LJ_FUNC int lj_trace_s390x_iterator_itern_nohot_dispatch_active(void)
{
  return s390x_iterator_itern_nohot_dispatch_active;
}

static int lj_trace_s390x_iterator_root_blacklist_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1) {
    const char *opt_out = getenv("LUAJIT_S390X_DISABLE_ITERATOR_ROOT_BLACKLIST");
    enabled = (LJ_TARGET_S390X && opt_out == NULL);
  }
  return enabled;
}

static int lj_trace_s390x_iterator_root_blacklist_match(jit_State *J,
                                                        GCtrace *T)
{
  BCOp op = bc_op(J->cur.startins);
  return lj_trace_s390x_iterator_root_blacklist_enabled() &&
         J->parent == 0 && J->exitno == 0 &&
         J->cur.root == 0 &&
         (op == BC_ITERN || op == BC_ITERL) &&
         J->cur.link == J->cur.traceno &&
         J->cur.linktype == LJ_TRLINK_LOOP &&
         J->cur.resumechild == 0 &&
         T != NULL;
}

static void lj_trace_s390x_iterator_post_proto_itern_nohot_activate(jit_State *J,
                                                                    const GCproto *pt)
{
  if (!lj_trace_s390x_iterator_post_proto_itern_nohot_enabled() ||
      s390x_iterator_itern_nohot_dispatch_active)
    return;
  s390x_iterator_itern_nohot_dispatch_active = 1;
  if (getenv("LUAJIT_S390X_TRACE_META_LOG") != NULL) {
    fprintf(stderr,
            "S390X_ITERATOR_POST_PROTO_ITERN_NOHOT trace=%u firstline=%u numline=%u\n",
            (unsigned int)J->cur.traceno,
            (unsigned int)(pt ? pt->firstline : 0),
            (unsigned int)(pt ? pt->numline : 0));
  }
}

static int lj_trace_s390x_iterator_itern_exact_root_match(jit_State *J,
                                                          GCproto *pt,
                                                          GCtrace *T)
{
  return LJ_TARGET_S390X &&
         lj_trace_s390x_iterator_table_proto_match(pt) &&
         J->parent == 0 && J->exitno == 0 &&
         J->cur.root == 0 &&
         bc_op(J->cur.startins) == BC_ITERN &&
         J->cur.linktype == LJ_TRLINK_LOOP &&
         J->cur.link == J->cur.traceno &&
         J->cur.resumechild == 0 &&
         J->cur.nsnap == 6 &&
         ((J->cur.nins == 32785 && J->cur.mcloop == 208) ||
          (J->cur.nins == 32792 && J->cur.mcloop == 300)) &&
         T != NULL;
}

static int lj_trace_s390x_iterator_iterl_exact_root_match(jit_State *J,
                                                          GCproto *pt,
                                                          GCtrace *T)
{
  return LJ_TARGET_S390X &&
         lj_trace_s390x_iterator_table_proto_match(pt) &&
         J->parent == 0 && J->exitno == 0 &&
         J->cur.root == 0 &&
         bc_op(J->cur.startins) == BC_ITERL &&
         J->cur.linktype == LJ_TRLINK_LOOP &&
         J->cur.link == J->cur.traceno &&
         J->cur.resumechild == 0 &&
         J->cur.nsnap == 2 &&
         J->cur.nins == 32798 &&
         J->cur.mcloop == 512 &&
         T != NULL;
}

static int lj_trace_s390x_iterator_itern_blacklist_match(jit_State *J,
                                                         GCproto *pt,
                                                         GCtrace *T)
{
  return lj_trace_s390x_iterator_itern_blacklist_enabled() &&
         lj_trace_s390x_iterator_itern_exact_root_match(J, pt, T);
}

static int lj_trace_s390x_iterator_iterl_blacklist_match(jit_State *J,
                                                         GCproto *pt,
                                                         GCtrace *T)
{
  return lj_trace_s390x_iterator_iterl_blacklist_enabled() &&
         lj_trace_s390x_iterator_iterl_exact_root_match(J, pt, T);
}

static int lj_trace_s390x_iterator_itern_proto_nojit_match(jit_State *J,
                                                           GCproto *pt,
                                                           GCtrace *T)
{
  return lj_trace_s390x_iterator_itern_proto_nojit_enabled() &&
         lj_trace_s390x_iterator_itern_exact_root_match(J, pt, T);
}

static int lj_trace_s390x_iterator_array_itern_nojit_hotcount_park_match(jit_State *J)
{
  return LJ_TARGET_S390X &&
         lj_trace_s390x_iterator_array_itern_nojit_hotcount_park_enabled() &&
         lj_trace_s390x_iterator_itern_proto_nojit_enabled() &&
         J->parent == 0 && J->exitno == 0 &&
         J->pt != NULL && (J->pt->flags & PROTO_NOJIT) &&
         J->pc != NULL && bc_op(*J->pc) == BC_ITERN &&
         J->pt->firstline == 22 && J->pt->numline == 8 &&
         lj_trace_s390x_iterator_table_proto_match(J->pt);
}

static int lj_trace_s390x_iterator_hash_itern_nojit_hotcount_park_match(jit_State *J)
{
  return LJ_TARGET_S390X &&
         lj_trace_s390x_iterator_hash_itern_nojit_hotcount_park_enabled() &&
         lj_trace_s390x_iterator_itern_proto_nojit_enabled() &&
         J->parent == 0 && J->exitno == 0 &&
         J->pt != NULL && (J->pt->flags & PROTO_NOJIT) &&
         J->pc != NULL && bc_op(*J->pc) == BC_ITERN &&
         J->pt->firstline == 12 && J->pt->numline == 8 &&
         lj_trace_s390x_iterator_table_proto_match(J->pt);
}

static int lj_trace_s390x_mixed_ffi_post_stitch_save_done_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_MIXED_FFI_POST_STITCH_SAVE_DONE") != NULL);
  return enabled;
}

static int lj_trace_s390x_mixed_ffi_post_stitch_save_done_match(jit_State *J,
                                                               GCproto *pt)
{
  SnapShot *snap;
  SnapEntry *map;
  const BCIns *snappc;
  if (!(LJ_TARGET_S390X &&
        J->cur.traceno == 102 &&
        J->parent == 101 && J->exitno == 0 &&
        J->cur.root == 1 &&
        J->cur.link == 0 &&
        J->cur.linktype == LJ_TRLINK_INTERP &&
        bc_op(J->cur.startins) == BC_JMP &&
        J->cur.topslot == 18 &&
        J->cur.spadjust == 192 &&
        J->cur.nsnap == 2 &&
        J->cur.nins == 32773 &&
        J->cur.mcloop == 0 &&
        lj_trace_s390x_mixed_ffi_post_stitch_save_done_enabled() &&
        lj_trace_s390x_mixed_ffi_proto_match(pt)))
    return 0;
  snap = &J->cur.snap[0];
  map = &J->cur.snapmap[snap->mapofs];
  snappc = snap_pc(&map[snap->nent]);
  return snappc != NULL && bc_op(*snappc) == BC_TGETB;
}

static void lj_trace_s390x_mixed_ffi_post_stitch_save_done(jit_State *J,
                                                           GCproto *pt)
{
  SnapShot *snap;
  if (!lj_trace_s390x_mixed_ffi_post_stitch_save_done_match(J, pt))
    return;
  snap = &J->cur.snap[0];
  if (snap->count == SNAPCOUNT_DONE)
    return;
  snap->count = SNAPCOUNT_DONE;
  if (getenv("LUAJIT_S390X_TRACE_META_LOG") != NULL) {
    fprintf(stderr,
            "S390X_MIXED_FFI_POST_STITCH_SAVE_DONE trace=%u parent=%u exit=%u root=%u startop=%u link=%u linktype=%u nsnap=%u nins=%u snap=0 op=%u\n",
            (unsigned int)J->cur.traceno,
            (unsigned int)J->parent,
            (unsigned int)J->exitno,
            (unsigned int)J->cur.root,
            (unsigned int)bc_op(J->cur.startins),
            (unsigned int)J->cur.link,
            (unsigned int)J->cur.linktype,
            (unsigned int)J->cur.nsnap,
            (unsigned int)J->cur.nins,
            (unsigned int)BC_TGETB);
  }
}

static int lj_trace_s390x_ffi_cdata_pair_save_done_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_FFI_CDATA_PAIR_SAVE_DONE") != NULL);
  return enabled;
}

static int lj_trace_s390x_ffi_cdata_pair_save_done_match(jit_State *J,
                                                         GCproto *pt)
{
  SnapShot *snap;
  SnapEntry *map;
  const BCIns *snappc;
  if (!(LJ_TARGET_S390X &&
        J->cur.traceno == 102 &&
        J->parent == 101 && J->exitno == 0 &&
        J->cur.root == 1 &&
        J->cur.link == 0 &&
        J->cur.linktype == LJ_TRLINK_INTERP &&
        bc_op(J->cur.startins) == BC_JMP &&
        J->cur.topslot == 9 &&
        J->cur.spadjust == 8 &&
        J->cur.nsnap == 2 &&
        J->cur.nins == 32773 &&
        J->cur.mcloop == 0 &&
        lj_trace_s390x_ffi_cdata_pair_save_done_enabled() &&
        lj_trace_s390x_ffi_cdata_proto_match(pt)))
    return 0;
  snap = &J->cur.snap[0];
  map = &J->cur.snapmap[snap->mapofs];
  snappc = snap_pc(&map[snap->nent]);
  return snappc != NULL && bc_op(*snappc) == BC_TGETB;
}

static void lj_trace_s390x_ffi_cdata_pair_save_done(jit_State *J,
                                                    GCproto *pt)
{
  SnapShot *snap;
  if (!lj_trace_s390x_ffi_cdata_pair_save_done_match(J, pt))
    return;
  snap = &J->cur.snap[0];
  if (snap->count == SNAPCOUNT_DONE)
    return;
  snap->count = SNAPCOUNT_DONE;
  if (getenv("LUAJIT_S390X_TRACE_META_LOG") != NULL) {
    fprintf(stderr,
            "S390X_FFI_CDATA_PAIR_SAVE_DONE trace=%u parent=%u exit=%u root=%u startop=%u link=%u linktype=%u nsnap=%u nins=%u snap=0 op=%u\n",
            (unsigned int)J->cur.traceno,
            (unsigned int)J->parent,
            (unsigned int)J->exitno,
            (unsigned int)J->cur.root,
            (unsigned int)bc_op(J->cur.startins),
            (unsigned int)J->cur.link,
            (unsigned int)J->cur.linktype,
            (unsigned int)J->cur.nsnap,
            (unsigned int)J->cur.nins,
            (unsigned int)BC_TGETB);
  }
}

static void lj_trace_s390x_dispatch_forl_park_root_hotexit_exact_cooldown(jit_State *J,
                                                                         GCproto *pt,
                                                                         BCIns *pc)
{
  int value = lj_trace_s390x_dispatch_forl_park_root_hotexit_exact_cooldown_value();
  if (!(LJ_TARGET_S390X &&
        value > 0 &&
        lj_trace_s390x_dispatch_forl_park_root_match(J, pt) &&
        lj_trace_s390x_dispatch_hotexit_proto_match(pt) &&
        pc != NULL))
    return;
  hotcount_set(J2GG(J), pc+1, value);
  if (lj_trace_s390x_start_log_enabled()) {
    fprintf(stderr,
            "S390X_DISPATCH_FORL_PARK_ROOT_HOTEXIT_EXACT_COOLDOWN trace=%u startpc=%p nsnap=%u nins=%u firstline=%u numline=%u val=%d\n",
            (unsigned int)J->cur.traceno, (const void *)pc,
            (unsigned int)J->cur.nsnap, (unsigned int)J->cur.nins,
            (unsigned int)pt->firstline, (unsigned int)pt->numline, value);
  }
}

void lj_trace_s390x_vm_bridge_dispatch_log(GCtrace *T, const BCIns *pc, BCIns ins,
					   const TValue *base)
{
  static int dump_count = 0;
  BCIns prev2 = 0, prev1 = 0, next1 = 0;
  const TValue *slot_tab = NULL, *slot_ctl = NULL, *slot_key = NULL, *slot_val = NULL;
  const TValue *slot_for_idx = NULL, *slot_for_stop = NULL;
  const TValue *slot_for_step = NULL, *slot_for_ext = NULL;
  const TValue *slot_add_a = NULL, *slot_add_b = NULL, *slot_add_c = NULL;
  uint64_t raw_tab = 0, raw_ctl = 0, raw_key = 0, raw_val = 0;
  uint64_t raw_for_idx = 0, raw_for_stop = 0, raw_for_step = 0, raw_for_ext = 0;
  uint64_t raw_add_a = 0, raw_add_b = 0, raw_add_c = 0;
  BCReg a = bc_a(ins);
  BCReg fa = 0;
  BCReg add_a = 0, add_b = 0, add_c = 0;
  if (!lj_trace_s390x_vm_bridge_dispatch_log_enabled() || !T || dump_count >= 256)
    return;
  if (pc) {
    prev2 = pc[-2];
    prev1 = pc[-1];
    next1 = pc[1];
  }
  if (base && a >= 2) {
    slot_tab = &base[a-2];
    slot_ctl = &base[a-1];
    slot_key = &base[a];
    slot_val = &base[a+1];
    raw_tab = slot_tab->u64;
    raw_ctl = slot_ctl->u64;
    raw_key = slot_key->u64;
    raw_val = slot_val->u64;
  }
  if (base && (bc_op(next1) == BC_FORL || bc_op(next1) == BC_IFORL ||
	       bc_op(next1) == BC_JFORL)) {
    fa = bc_a(next1);
    slot_for_idx = &base[fa];
    slot_for_stop = &base[fa+1];
    slot_for_step = &base[fa+2];
    slot_for_ext = &base[fa+3];
    raw_for_idx = slot_for_idx->u64;
    raw_for_stop = slot_for_stop->u64;
    raw_for_step = slot_for_step->u64;
    raw_for_ext = slot_for_ext->u64;
  }
  if (base && bc_op(prev2) == BC_ADDVV) {
    add_a = bc_a(prev2);
    add_b = bc_b(prev2);
    add_c = bc_c(prev2);
    slot_add_a = &base[add_a];
    slot_add_b = &base[add_b];
    slot_add_c = &base[add_c];
    raw_add_a = slot_add_a->u64;
    raw_add_b = slot_add_b->u64;
    raw_add_c = slot_add_c->u64;
  }
  fprintf(stderr,
	  "S390X_VM_BRIDGE_DISPATCH n=%d trace=%u root=%u link=%u linktype=%u startpc=%p startop=%u resumepc=%p resumeop=%u resumevalid=%u resumechild=%u pc=%p op=%u ra=%u rd=%u ins=0x%08x prev2=0x%08x prev2op=%u prev2a=%u prev2b=%u prev2c=%u prev1=0x%08x prev1op=%u next1=0x%08x next1op=%u next1a=%u base=%p raw_tab=0x%016llx raw_ctl=0x%016llx raw_key=0x%016llx raw_val=0x%016llx raw_add_a=0x%016llx raw_add_b=0x%016llx raw_add_c=0x%016llx raw_for_idx=0x%016llx raw_for_stop=0x%016llx raw_for_step=0x%016llx raw_for_ext=0x%016llx\n",
	  dump_count,
	  (unsigned int)T->traceno,
	  (unsigned int)T->root,
	  (unsigned int)T->link,
	  (unsigned int)T->linktype,
	  (const void *)mref(T->startpc, BCIns),
	  (unsigned int)bc_op(T->startins),
	  (const void *)mref(T->resumepc, BCIns),
	  (unsigned int)bc_op(T->resumeins),
	  (unsigned int)T->resumevalid,
	  (unsigned int)T->resumechild,
	  (const void *)pc,
	  (unsigned int)bc_op(ins),
	  (unsigned int)bc_a(ins),
	  (unsigned int)bc_d(ins),
	  (unsigned int)ins,
	  (unsigned int)prev2,
	  (unsigned int)bc_op(prev2),
	  (unsigned int)add_a,
	  (unsigned int)add_b,
	  (unsigned int)add_c,
	  (unsigned int)prev1,
	  (unsigned int)bc_op(prev1),
	  (unsigned int)next1,
	  (unsigned int)bc_op(next1),
	  (unsigned int)fa,
	  (const void *)base,
	  (unsigned long long)raw_tab,
	  (unsigned long long)raw_ctl,
	  (unsigned long long)raw_key,
	  (unsigned long long)raw_val,
	  (unsigned long long)raw_add_a,
	  (unsigned long long)raw_add_b,
	  (unsigned long long)raw_add_c,
	  (unsigned long long)raw_for_idx,
	  (unsigned long long)raw_for_stop,
	  (unsigned long long)raw_for_step,
	  (unsigned long long)raw_for_ext);
  dump_count++;
}

void lj_trace_s390x_vm_iterl_log(const BCIns *pc, const TValue *ra)
{
  static int dump_count = 0;
  if (!lj_trace_s390x_vm_iterl_log_enabled() || !pc || !ra || dump_count >= 256)
    return;
  fprintf(stderr,
	  "S390X_VM_ITERL n=%d pc=%p op=%u a=%u d=%u m1=0x%016llx v0=0x%016llx v1=0x%016llx v2=0x%016llx\n",
	  dump_count,
	  (const void *)pc,
	  (unsigned int)bc_op(*pc),
	  (unsigned int)bc_a(*pc),
	  (unsigned int)bc_d(*pc),
	  (unsigned long long)(ra-1)->u64,
	  (unsigned long long)ra[0].u64,
	  (unsigned long long)ra[1].u64,
	  (unsigned long long)ra[2].u64);
  dump_count++;
}

int lj_trace_s390x_vm_child_skip_mcloop(GCtrace *T)
{
  if (!lj_trace_s390x_vm_child_skip_mcloop_enabled() || T == NULL)
    return 0;
  if (T->traceno == 3 &&
      T->root == 1 &&
      bc_op(T->startins) == BC_JMP &&
      T->linktype == LJ_TRLINK_LOOP &&
      T->link == T->traceno &&
      T->resumevalid &&
      bc_op(T->resumeins) == BC_JLOOP &&
      T->mcloop != 0) {
    if (lj_trace_s390x_vm_child_entry_log_enabled()) {
      fprintf(stderr,
	      "S390X_VM_CHILD_NOMCLOOP trace=%u link=%u linktype=%u resumeop=%u mcloop=%u ownerop=%u\n",
	      (unsigned int)T->traceno,
	      (unsigned int)T->link,
	      (unsigned int)T->linktype,
	      (unsigned int)bc_op(T->resumeins),
	      (unsigned int)T->mcloop,
	      (unsigned int)T->unused1);
    }
    return 1;
  }
  return 0;
}

static int lj_trace_s390x_is_loopdesc_bridge_stub(GCtrace *T)
{
  return T != NULL &&
	 T->root == 1 &&
	 bc_op(T->startins) == BC_JMP &&
	 T->linktype == LJ_TRLINK_ROOT &&
	 T->link != 0 &&
	 T->resumevalid &&
	 bc_op(T->resumeins) == BC_ITERL &&
	 T->resumechild == 0 &&
	 T->nins == 32770 &&
	 T->mcloop == 0;
}

static TraceNo lj_trace_s390x_runtime_owner_trace(GCtrace *T)
{
  if (T == NULL)
    return 0;
  if (lj_trace_s390x_is_loopdesc_bridge_stub(T))
    return T->link;
  if (T->root != 0 && bc_op(T->startins) == BC_JMP &&
      T->resumevalid && T->linktype == LJ_TRLINK_LOOP && T->link != 0)
    return T->link;
  if (T->resumechild != 0)
    return T->resumechild;
  return T->traceno;
}

static int lj_trace_s390x_bridge_meta_log_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_BRIDGE_META_LOG") != NULL);
  return enabled;
}

static void lj_trace_s390x_bridge_meta_log(const char *phase, GCtrace *T,
					   jit_State *J)
{
  static int dump_count = 0;
  const BCIns *resumepc = NULL;
  BCIns prev2 = 0, prev1 = 0, next1 = 0;
  GCtrace *linkT = NULL;
  if (!lj_trace_s390x_bridge_meta_log_enabled() || !T || dump_count >= 128)
    return;
  if (!(T->root == 1 &&
	bc_op(T->startins) == BC_JMP &&
	T->nins == 32770))
    return;
  resumepc = mref(T->resumepc, const BCIns);
  if (resumepc) {
    prev2 = resumepc[-2];
    prev1 = resumepc[-1];
    next1 = resumepc[1];
  }
  if (J && T->link != 0 && (MSize)T->link < J->sizetrace)
    linkT = traceref(J, T->link);
  fprintf(stderr,
	  "S390X_BRIDGE_META n=%d phase=%s trace=%u parent=%u exit=%u root=%u link=%u linktype=%u startpc=%p startop=%u resumepc=%p resumeins=0x%08x resumeop=%u resumevalid=%u resumechild=%u mcloop=%u ownerop=%u nins=%u nsnap=%u prev2=0x%08x prev2op=%u prev1=0x%08x prev1op=%u next1=0x%08x next1op=%u link_startpc=%p link_startop=%u link_resumepc=%p link_resumeins=0x%08x link_resumeop=%u link_resumevalid=%u link_resumechild=%u link_mcloop=%u link_ownerop=%u\n",
	  dump_count, phase ? phase : "?",
	  (unsigned int)T->traceno,
	  (unsigned int)(J ? J->parent : 0),
	  (unsigned int)(J ? J->exitno : 0),
	  (unsigned int)T->root,
	  (unsigned int)T->link,
	  (unsigned int)T->linktype,
	  (const void *)mref(T->startpc, BCIns),
	  (unsigned int)bc_op(T->startins),
	  (const void *)resumepc,
	  (unsigned int)T->resumeins,
	  (unsigned int)bc_op(T->resumeins),
	  (unsigned int)T->resumevalid,
	  (unsigned int)T->resumechild,
	  (unsigned int)T->mcloop,
	  (unsigned int)T->unused1,
	  (unsigned int)T->nins,
	  (unsigned int)T->nsnap,
	  (unsigned int)prev2,
	  (unsigned int)bc_op(prev2),
	  (unsigned int)prev1,
	  (unsigned int)bc_op(prev1),
	  (unsigned int)next1,
	  (unsigned int)bc_op(next1),
	  (const void *)(linkT ? mref(linkT->startpc, BCIns) : NULL),
	  (unsigned int)(linkT ? bc_op(linkT->startins) : 0),
	  (const void *)(linkT ? mref(linkT->resumepc, BCIns) : NULL),
	  (unsigned int)(linkT ? linkT->resumeins : 0),
	  (unsigned int)(linkT ? bc_op(linkT->resumeins) : 0),
	  (unsigned int)(linkT ? linkT->resumevalid : 0),
	  (unsigned int)(linkT ? linkT->resumechild : 0),
	  (unsigned int)(linkT ? linkT->mcloop : 0),
	  (unsigned int)(linkT ? linkT->unused1 : 0));
  dump_count++;
}

static int lj_trace_s390x_hotside_focus_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_HOTSIDE_FOCUS") != NULL);
  return enabled;
}

static int lj_trace_s390x_hotside_focus_parent(void)
{
  static int parent = -2;
  if (parent == -2) {
    const char *s = getenv("LUAJIT_S390X_HOTSIDE_FOCUS_PARENT");
    parent = s ? atoi(s) : 4;
  }
  return parent;
}

static int lj_trace_s390x_hotside_focus_exit(void)
{
  static int exitno = -2;
  if (exitno == -2) {
    const char *s = getenv("LUAJIT_S390X_HOTSIDE_FOCUS_EXIT");
    exitno = s ? atoi(s) : 1;
  }
  return exitno;
}

static int lj_trace_s390x_hotside_uget_looproot_enabled(void);

static int lj_trace_s390x_hotside_localized_equiv_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_LOCALIZED_HOTSIDE_CANON_SHARE_EQUIV") != NULL);
  return enabled;
}

static int lj_trace_s390x_hotside_localized_name_match(const char *name,
						       size_t len)
{
  static const char be_helpers[] = "tests/s390x/perf/be_helpers_localized.lua";
  static const char promotion_static[] = "tests/s390x/perf/promotion_core_static_stop.lua";
  static const char route_around[] = "tests/s390x/perf/route_around_reducers.lua";
  static const char lower_frame[] = "tests/s390x/perf/lower_frame_same_callsite.lua";
  if (len > 0 && name[0] == '@') {
    name++;
    len--;
  }
  return (len == sizeof(be_helpers) - 1 &&
	  memcmp(name, be_helpers, sizeof(be_helpers) - 1) == 0) ||
	 (len == sizeof(promotion_static) - 1 &&
	  memcmp(name, promotion_static, sizeof(promotion_static) - 1) == 0) ||
	 (len == sizeof(route_around) - 1 &&
	  memcmp(name, route_around, sizeof(route_around) - 1) == 0) ||
	 (len == sizeof(lower_frame) - 1 &&
	  memcmp(name, lower_frame, sizeof(lower_frame) - 1) == 0);
}

static int lj_trace_s390x_hotside_localized_bench_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1) {
    const char *bench_file = getenv("S390X_PERF_BENCH_FILE");
    enabled = (bench_file == NULL || bench_file[0] == '\0' ||
	       lj_trace_s390x_hotside_localized_name_match(bench_file,
							   strlen(bench_file)));
  }
  return enabled;
}

static int lj_trace_s390x_hotside_localized_active_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (lj_trace_s390x_hotside_localized_equiv_enabled() &&
	       lj_trace_s390x_hotside_localized_bench_enabled());
  return enabled;
}

static int lj_trace_s390x_hotside_canon_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (lj_trace_s390x_hotside_uget_looproot_enabled() ||
	       lj_trace_s390x_hotside_localized_equiv_enabled() ||
	       getenv("LUAJIT_S390X_HOTSIDE_CANON_EQUIV") != NULL ||
	       getenv("LUAJIT_S390X_HOTSIDE_CANON_SHARE_EQUIV") != NULL ||
	       getenv("LUAJIT_S390X_HOTSIDE_CANON_SHARE_UGET_LOOPROOT") != NULL);
  return enabled;
}

static int lj_trace_s390x_hotside_canon_child_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_HOTSIDE_CANON_CHILD") != NULL);
  return enabled;
}

static int lj_trace_s390x_hotside_share_equiv_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (lj_trace_s390x_hotside_uget_looproot_enabled() ||
	       lj_trace_s390x_hotside_localized_equiv_enabled() ||
	       getenv("LUAJIT_S390X_HOTSIDE_SHARE_EQUIV") != NULL ||
	       getenv("LUAJIT_S390X_HOTSIDE_CANON_SHARE_EQUIV") != NULL ||
	       getenv("LUAJIT_S390X_HOTSIDE_CANON_SHARE_UGET_LOOPROOT") != NULL);
  return enabled;
}

static int lj_trace_s390x_hotside_manual_equiv_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_HOTSIDE_CANON_EQUIV") != NULL ||
	       getenv("LUAJIT_S390X_HOTSIDE_SHARE_EQUIV") != NULL ||
	       getenv("LUAJIT_S390X_HOTSIDE_CANON_SHARE_EQUIV") != NULL ||
	       getenv("LUAJIT_S390X_HOTSIDE_CANON_CHILD") != NULL);
  return enabled;
}

static int lj_trace_s390x_hotside_event_log_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_HOTSIDE_EVENT_LOG") != NULL);
  return enabled;
}

static int lj_trace_s390x_hotside_equiv_log_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_HOTSIDE_EQUIV_LOG") != NULL);
  return enabled;
}

static int lj_trace_s390x_hotside_uget_looproot_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1) {
    if (getenv("LUAJIT_S390X_DISABLE_HOTSIDE_CANON_SHARE_UGET_LOOPROOT") != NULL)
      enabled = 0;
    else
      enabled = 1;
  }
  return enabled;
}

static int lj_trace_s390x_hotside_match_log_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_HOTSIDE_MATCH_LOG") != NULL);
  return enabled;
}

static int lj_trace_s390x_hotside_prime_interp_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_HOTSIDE_PRIME_INTERP_PAYLOAD") != NULL);
  return enabled;
}

static int lj_trace_s390x_sideexit_mcloop_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_SIDEEXIT_MCLOOP") != NULL);
  return enabled;
}

static int lj_trace_s390x_skip_patch_bcjmp_loopdesc_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_SKIP_PATCHEXIT_BCJMP_LOOPDESC") != NULL);
  return enabled;
}

static int lj_trace_s390x_stop_retarget_loopdesc_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_STOP_RETARGET_LOOPDESC") != NULL);
  return enabled;
}

static int lj_trace_s390x_hotside_match_exit(const GCtrace *a, ExitNo aexit,
					     const GCtrace *b, ExitNo bexit)
{
  const SnapShot *asnap, *bsnap;
  if (a == NULL || b == NULL || aexit >= a->nsnap || bexit >= b->nsnap)
    return 0;
  asnap = &a->snap[aexit];
  bsnap = &b->snap[bexit];
  return asnap->ref == bsnap->ref && asnap->nent == bsnap->nent;
}

static TraceNo lj_trace_s390x_hotside_find_equiv_min(jit_State *J, GCtrace *T,
						     ExitNo exitno,
						     TraceNo min_parent,
						     TraceNo min_cand)
{
  TraceNo rootno = T->root;
  GCtrace *root;
  TraceNo candno, best = 0;
  if (rootno == 0 || J->parent == rootno) {
    if (lj_trace_s390x_hotside_equiv_log_enabled()) {
      fprintf(stderr,
	      "S390X_HOTSIDE_EQUIV phase=skip-root parent=%u exit=%u root=%u startop=%u\n",
	      (unsigned int)J->parent, (unsigned int)exitno,
	      (unsigned int)rootno, (unsigned int)bc_op(T->startins));
    }
    return 0;
  }
  if (J->parent < min_parent) {
    if (lj_trace_s390x_hotside_equiv_log_enabled()) {
      fprintf(stderr,
	      "S390X_HOTSIDE_EQUIV phase=skip-parent parent=%u exit=%u root=%u min_parent=%u startop=%u\n",
	      (unsigned int)J->parent, (unsigned int)exitno,
	      (unsigned int)rootno, (unsigned int)min_parent,
	      (unsigned int)bc_op(T->startins));
    }
    return 0;
  }
  root = traceref(J, rootno);
  if (root == NULL) {
    if (lj_trace_s390x_hotside_equiv_log_enabled()) {
      fprintf(stderr,
	      "S390X_HOTSIDE_EQUIV phase=skip-rootnull parent=%u exit=%u root=%u startop=%u\n",
	      (unsigned int)J->parent, (unsigned int)exitno,
	      (unsigned int)rootno, (unsigned int)bc_op(T->startins));
    }
    return 0;
  }
  for (candno = root->nextside; candno; candno = traceref(J, candno)->nextside) {
    GCtrace *C = traceref(J, candno);
    if (C == NULL || candno == J->parent || candno >= J->parent ||
	candno < min_cand) {
      if (lj_trace_s390x_hotside_equiv_log_enabled()) {
	fprintf(stderr,
		"S390X_HOTSIDE_EQUIV phase=reject-order parent=%u exit=%u root=%u cand=%u candroot=%u candstart=%u\n",
		(unsigned int)J->parent, (unsigned int)exitno,
		(unsigned int)rootno, (unsigned int)candno,
		(unsigned int)(C ? C->root : 0),
		(unsigned int)(C ? bc_op(C->startins) : 0));
      }
      continue;
    }
    if (C->root != rootno || bc_op(C->startins) != bc_op(T->startins)) {
      if (lj_trace_s390x_hotside_equiv_log_enabled()) {
	fprintf(stderr,
		"S390X_HOTSIDE_EQUIV phase=reject-shape parent=%u exit=%u root=%u cand=%u candroot=%u startop=%u candstart=%u\n",
		(unsigned int)J->parent, (unsigned int)exitno,
		(unsigned int)rootno, (unsigned int)candno,
		(unsigned int)C->root, (unsigned int)bc_op(T->startins),
		(unsigned int)bc_op(C->startins));
      }
      continue;
    }
    if (C->nsnap != T->nsnap || C->nins != T->nins) {
      if (lj_trace_s390x_hotside_equiv_log_enabled()) {
	fprintf(stderr,
		"S390X_HOTSIDE_EQUIV phase=reject-size parent=%u exit=%u root=%u cand=%u nsnap=%u/%u nins=%u/%u\n",
		(unsigned int)J->parent, (unsigned int)exitno,
		(unsigned int)rootno, (unsigned int)candno,
		(unsigned int)T->nsnap, (unsigned int)C->nsnap,
		(unsigned int)T->nins, (unsigned int)C->nins);
      }
      continue;
    }
    if (C->ir[REF_BASE].op2 != T->ir[REF_BASE].op2) {
      if (lj_trace_s390x_hotside_equiv_log_enabled()) {
	fprintf(stderr,
		"S390X_HOTSIDE_EQUIV phase=reject-exit parent=%u exit=%u root=%u cand=%u baseexit=%u/%u\n",
		(unsigned int)J->parent, (unsigned int)exitno,
		(unsigned int)rootno, (unsigned int)candno,
		(unsigned int)T->ir[REF_BASE].op2,
		(unsigned int)C->ir[REF_BASE].op2);
      }
      continue;
    }
    if (!lj_trace_s390x_hotside_match_exit(C, exitno, T, exitno)) {
      if (lj_trace_s390x_hotside_equiv_log_enabled()) {
	fprintf(stderr,
		"S390X_HOTSIDE_EQUIV phase=reject-snap parent=%u exit=%u root=%u cand=%u\n",
		(unsigned int)J->parent, (unsigned int)exitno,
		(unsigned int)rootno, (unsigned int)candno);
      }
      continue;
    }
    if (lj_trace_s390x_hotside_equiv_log_enabled()) {
      fprintf(stderr,
	      "S390X_HOTSIDE_EQUIV phase=accept parent=%u exit=%u root=%u cand=%u\n",
	      (unsigned int)J->parent, (unsigned int)exitno,
	      (unsigned int)rootno, (unsigned int)candno);
    }
    if (best == 0 || candno < best)
      best = candno;
  }
  return best;
}

static TraceNo lj_trace_s390x_hotside_find_equiv(jit_State *J, GCtrace *T,
						 ExitNo exitno)
{
  return lj_trace_s390x_hotside_find_equiv_min(J, T, exitno, 7, 6);
}

static TraceNo lj_trace_s390x_hotside_find_child(jit_State *J, TraceNo rootno,
						 TraceNo parentno, ExitNo exitno)
{
  TraceNo traceno;
  GCtrace *root = traceref(J, rootno);
  if (root == NULL)
    return 0;
  for (traceno = root->nextside; traceno; traceno = traceref(J, traceno)->nextside) {
    GCtrace *T = traceref(J, traceno);
    if (T == NULL)
      continue;
    if (T->ir[REF_BASE].op1 == parentno && T->ir[REF_BASE].op2 == exitno)
      return traceno;
  }
  return 0;
}

static void lj_trace_s390x_hotside_event_log(jit_State *J, const char *phase,
					     const BCIns *pc, GCtrace *T,
					     ExitNo exitno, SnapShot *snap,
					     TraceNo candno, TraceNo childno)
{
  TraceNo rootno;
  GCtrace *root;
  SnapEntry *map;
  const BCIns *snappc;
  if (!lj_trace_s390x_hotside_event_log_enabled() || T == NULL || snap == NULL)
    return;
  rootno = T->root ? T->root : T->traceno;
  root = traceref(J, rootno);
  map = &T->snapmap[snap->mapofs];
  snappc = snap_pc(&map[snap->nent]);
  fprintf(stderr,
	  "S390X_HOTSIDE_EVENT phase=%s parent=%u exit=%u cand=%u child=%u root=%u pc=%p op=%u snappc=%p snapop=%u snapcount=%u startop=%u root_startop=%u linktype=%u link=%u nsnap=%u nchild=%u\n",
	  phase,
	  (unsigned int)J->parent, (unsigned int)exitno,
	  (unsigned int)candno, (unsigned int)childno, (unsigned int)rootno,
	  (const void *)pc, (unsigned int)(pc ? bc_op(*pc) : 0),
	  (const void *)snappc, (unsigned int)(snappc ? bc_op(*snappc) : 0),
	  (unsigned int)snap->count, (unsigned int)bc_op(T->startins),
	  (unsigned int)(root ? bc_op(root->startins) : 0),
	  (unsigned int)T->linktype, (unsigned int)T->link,
	  (unsigned int)T->nsnap, (unsigned int)T->nchild);
}

static int lj_trace_s390x_hotside_uget_looproot_match(jit_State *J,
						      const BCIns *pc,
						      GCtrace *T,
						      ExitNo exitno,
						      SnapShot *snap)
{
  TraceNo rootno;
  GCtrace *root;
  SnapEntry *map;
  const BCIns *snappc;
  BCOp rootop;
  if (!lj_trace_s390x_hotside_uget_looproot_enabled())
    return 1;
  if (pc == NULL || T == NULL || snap == NULL || exitno != 0)
    return 0;
  if (bc_op(T->startins) != BC_JMP || bc_op(*pc) != BC_UGET)
    return 0;
  map = &T->snapmap[snap->mapofs];
  snappc = snap_pc(&map[snap->nent]);
  if (snappc != pc || bc_op(*snappc) != BC_UGET)
    return 0;
  rootno = T->root ? T->root : T->traceno;
  root = traceref(J, rootno);
  if (root == NULL)
    return 0;
  rootop = bc_op(root->startins);
  if (lj_trace_s390x_hotside_match_log_enabled()) {
    fprintf(stderr,
	    "S390X_HOTSIDE_MATCH phase=uget-looproot parent=%u exit=%u root=%u pc=%p op=%u startop=%u root_startop=%u linktype=%u link=%u nsnap=%u nchild=%u\n",
	    (unsigned int)J->parent, (unsigned int)exitno, (unsigned int)rootno,
	    (const void *)pc, (unsigned int)(pc ? bc_op(*pc) : 0),
	    (unsigned int)bc_op(T->startins), (unsigned int)rootop,
	    (unsigned int)T->linktype, (unsigned int)T->link,
	    (unsigned int)T->nsnap, (unsigned int)T->nchild);
  }
  return rootop == BC_FORL || rootop == BC_FUNCF;
}

static int lj_trace_s390x_hotside_localized_proto_match(GCproto *pt)
{
  GCstr *chunk;
  if (pt == NULL)
    return 0;
  /* Exact reducers only. Keep non-target perf rows out of the hot-side probe. */
  if (!((pt->firstline == 10 && pt->numline == 7) ||
	(pt->firstline == 19 && pt->numline == 14) ||
	(pt->firstline == 12 && pt->numline == 7) ||
	(pt->firstline == 23 && pt->numline == 16) ||
	(pt->firstline == 41 && pt->numline == 16)))
    return 0;
  chunk = proto_chunkname(pt);
  if (chunk == NULL)
    return 0;
  return lj_trace_s390x_hotside_localized_name_match(strdata(chunk),
						     chunk->len);
}

static int lj_trace_s390x_hotside_localized_equiv_match(jit_State *J,
							const BCIns *pc,
							GCtrace *T,
							ExitNo exitno,
							SnapShot *snap)
{
  TraceNo rootno;
  GCtrace *root;
  GCproto *pt;
  int lower_frame;
  if (!(LJ_TARGET_S390X &&
	lj_trace_s390x_hotside_localized_active_enabled() &&
	pc != NULL && T != NULL && snap != NULL &&
	exitno == 0 &&
	bc_op(T->startins) == BC_JMP &&
	isluafunc(curr_func(J->L))))
    return 0;
  pt = curr_proto(J->L);
  lower_frame = lj_trace_s390x_lower_frame_same_callsite_proto_match(pt);
  if (!(bc_op(*pc) == BC_MOV || (lower_frame && bc_op(*pc) == BC_MODVN)))
    return 0;
  if (!lower_frame && !lj_trace_s390x_hotside_localized_proto_match(pt))
    return 0;
  rootno = T->root ? T->root : T->traceno;
  root = traceref(J, rootno);
  if (root == NULL || bc_op(root->startins) != BC_FORL)
    return 0;
  if (lj_trace_s390x_hotside_match_log_enabled()) {
    fprintf(stderr,
	    "S390X_HOTSIDE_LOCALIZED_MATCH parent=%u exit=%u root=%u pc=%p op=%u startop=%u root_startop=%u linktype=%u link=%u nsnap=%u nchild=%u firstline=%u numline=%u\n",
	    (unsigned int)J->parent, (unsigned int)exitno,
	    (unsigned int)rootno,
	    (const void *)pc, (unsigned int)bc_op(*pc),
	    (unsigned int)bc_op(T->startins),
	    (unsigned int)bc_op(root->startins),
	    (unsigned int)T->linktype, (unsigned int)T->link,
	    (unsigned int)T->nsnap, (unsigned int)T->nchild,
	    (unsigned int)pt->firstline, (unsigned int)pt->numline);
  }
  return 1;
}

static int lj_trace_s390x_hotside_try_canon(jit_State *J, const BCIns *pc,
					    GCtrace **Tp, ExitNo exitno,
					    SnapShot **snapp)
{
  GCtrace *T = *Tp;
  SnapShot *snap = *snapp;
  if (lj_trace_s390x_hotside_canon_enabled()) {
    TraceNo candno = lj_trace_s390x_hotside_find_equiv(J, T, exitno);
    if (candno) {
      GCtrace *C = traceref(J, candno);
      SnapShot *csnap = &C->snap[exitno];
      if (lj_trace_s390x_hotside_focus_enabled() &&
	  (lj_trace_s390x_hotside_focus_parent() < 0 ||
	   J->parent == (TraceNo)lj_trace_s390x_hotside_focus_parent()) &&
	  (lj_trace_s390x_hotside_focus_exit() < 0 ||
	   J->exitno == (ExitNo)lj_trace_s390x_hotside_focus_exit())) {
	fprintf(stderr,
		"S390X_HOTSIDE_FOCUS phase=canon parent=%u exit=%u cand=%u root=%u pc=%p op=%u snapcount=%u candcount=%u nsnap=%u nins=%u startop=%u\n",
		(unsigned int)J->parent, (unsigned int)J->exitno,
		(unsigned int)candno, (unsigned int)(T->root ? T->root : T->traceno),
		(const void *)pc, (unsigned int)(pc ? bc_op(*pc) : 0),
		(unsigned int)snap->count, (unsigned int)csnap->count,
		(unsigned int)T->nsnap,
		(unsigned int)T->nins, (unsigned int)bc_op(T->startins));
      }
      lj_trace_s390x_hotside_event_log(J, "canon", pc, T, exitno, snap,
				       candno, 0);
      J->parent = candno;
      *Tp = C;
      *snapp = csnap;
      return 1;
    }
  }
  if (lj_trace_s390x_hotside_canon_child_enabled()) {
    TraceNo rootno = T->root ? T->root : T->traceno;
    TraceNo candno = lj_trace_s390x_hotside_find_equiv(J, T, exitno);
    TraceNo childno = candno ? lj_trace_s390x_hotside_find_child(J, rootno, candno, exitno) : 0;
    if (childno) {
      GCtrace *C = traceref(J, childno);
      SnapShot *csnap = &C->snap[exitno];
      if (lj_trace_s390x_hotside_focus_enabled() &&
	  (lj_trace_s390x_hotside_focus_parent() < 0 ||
	   J->parent == (TraceNo)lj_trace_s390x_hotside_focus_parent()) &&
	  (lj_trace_s390x_hotside_focus_exit() < 0 ||
	   J->exitno == (ExitNo)lj_trace_s390x_hotside_focus_exit())) {
	fprintf(stderr,
		"S390X_HOTSIDE_FOCUS phase=canon-child parent=%u exit=%u cand=%u child=%u root=%u pc=%p op=%u snapcount=%u childcount=%u nsnap=%u nins=%u startop=%u\n",
		(unsigned int)J->parent, (unsigned int)J->exitno,
		(unsigned int)candno, (unsigned int)childno,
		(unsigned int)rootno,
		(const void *)pc, (unsigned int)(pc ? bc_op(*pc) : 0),
		(unsigned int)snap->count, (unsigned int)csnap->count,
		(unsigned int)T->nsnap,
		(unsigned int)T->nins, (unsigned int)bc_op(T->startins));
      }
      lj_trace_s390x_hotside_event_log(J, "canon-child", pc, T, exitno, snap,
				       candno, childno);
      J->parent = childno;
      *Tp = C;
      *snapp = csnap;
      return 1;
    }
  }
  return 0;
}

static void lj_trace_s390x_hotside_share_equiv(jit_State *J, const BCIns *pc,
					       GCtrace *T, ExitNo exitno,
					       SnapShot *snap)
{
  TraceNo candno;
  GCtrace *C;
  SnapShot *csnap;
  if (!lj_trace_s390x_hotside_share_equiv_enabled() || T == NULL || snap == NULL)
    return;
  candno = lj_trace_s390x_hotside_find_equiv_min(J, T, exitno, 6, 5);
  if (!candno)
    return;
  C = traceref(J, candno);
  if (C == NULL || exitno >= C->nsnap)
    return;
  csnap = &C->snap[exitno];
  if (snap->count == SNAPCOUNT_DONE)
    return;
  if (csnap->count == SNAPCOUNT_DONE) {
    MSize target = J->param[JIT_P_hotexit] - 1;
    if (snap->count >= target)
      return;
    if (lj_trace_s390x_hotside_focus_enabled() &&
	(lj_trace_s390x_hotside_focus_parent() < 0 ||
	 J->parent == (TraceNo)lj_trace_s390x_hotside_focus_parent()) &&
	(lj_trace_s390x_hotside_focus_exit() < 0 ||
	 J->exitno == (ExitNo)lj_trace_s390x_hotside_focus_exit())) {
      fprintf(stderr,
	      "S390X_HOTSIDE_FOCUS phase=share-done parent=%u exit=%u cand=%u pc=%p op=%u snapcount=%u target=%u hotexit=%u\n",
	      (unsigned int)J->parent, (unsigned int)J->exitno,
	      (unsigned int)candno, (const void *)pc,
	      (unsigned int)(pc ? bc_op(*pc) : 0), (unsigned int)snap->count,
	      (unsigned int)target, (unsigned int)J->param[JIT_P_hotexit]);
    }
    lj_trace_s390x_hotside_event_log(J, "share-done", pc, T, exitno, snap,
				     candno, 0);
    snap->count = target;
    return;
  }
  if (csnap->count <= snap->count)
    return;
  if (lj_trace_s390x_hotside_focus_enabled() &&
      (lj_trace_s390x_hotside_focus_parent() < 0 ||
       J->parent == (TraceNo)lj_trace_s390x_hotside_focus_parent()) &&
      (lj_trace_s390x_hotside_focus_exit() < 0 ||
       J->exitno == (ExitNo)lj_trace_s390x_hotside_focus_exit())) {
    fprintf(stderr,
	    "S390X_HOTSIDE_FOCUS phase=share parent=%u exit=%u cand=%u pc=%p op=%u snapcount=%u candcount=%u hotexit=%u\n",
	    (unsigned int)J->parent, (unsigned int)J->exitno,
	    (unsigned int)candno, (const void *)pc,
	    (unsigned int)(pc ? bc_op(*pc) : 0), (unsigned int)snap->count,
	    (unsigned int)csnap->count, (unsigned int)J->param[JIT_P_hotexit]);
  }
  lj_trace_s390x_hotside_event_log(J, "share-copy", pc, T, exitno, snap,
				   candno, 0);
  snap->count = csnap->count;
}

static void lj_trace_s390x_hotside_prime_interp(jit_State *J, const BCIns *pc,
						GCtrace *T, ExitNo exitno,
						SnapShot *snap)
{
  MSize target;
  if (!lj_trace_s390x_hotside_prime_interp_enabled() || T == NULL || snap == NULL)
    return;
  if (exitno != 1 || snap->count == SNAPCOUNT_DONE)
    return;
  if (T->root == 0 || T->linktype != LJ_TRLINK_INTERP || T->nsnap != 3)
    return;
  if (bc_op(T->startins) != BC_JMP)
    return;
  target = J->param[JIT_P_hotexit] - 1;
  if (snap->count >= target)
    return;
  if (lj_trace_s390x_hotside_focus_enabled() &&
      (lj_trace_s390x_hotside_focus_parent() < 0 ||
       J->parent == (TraceNo)lj_trace_s390x_hotside_focus_parent()) &&
      (lj_trace_s390x_hotside_focus_exit() < 0 ||
       J->exitno == (ExitNo)lj_trace_s390x_hotside_focus_exit())) {
    fprintf(stderr,
	    "S390X_HOTSIDE_FOCUS phase=prime-interp parent=%u exit=%u pc=%p op=%u snapcount=%u target=%u hotexit=%u\n",
	    (unsigned int)J->parent, (unsigned int)J->exitno,
	    (const void *)pc, (unsigned int)(pc ? bc_op(*pc) : 0),
	    (unsigned int)snap->count, (unsigned int)target,
	    (unsigned int)J->param[JIT_P_hotexit]);
  }
  snap->count = target;
}

static int lj_trace_s390x_stitch_focus_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_STITCH_FOCUS") != NULL);
  return enabled;
}

static int lj_trace_s390x_trace_meta_log_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_TRACE_META_LOG") != NULL);
  return enabled;
}

static int lj_trace_s390x_root_freeze_log_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
    enabled = (getenv("LUAJIT_S390X_ROOT_FREEZE_LOG") != NULL);
  return enabled;
}

static void lj_trace_s390x_root_freeze_log(jit_State *J, const char *site)
{
  if (!lj_trace_s390x_root_freeze_log_enabled())
    return;
  fprintf(stderr,
	  "S390X_ROOT_FREEZE site=%s trace=%u parent=%u exit=%u pc=%p startpc=%p cur_startpc=%p startins=%u resumepc=%p cur_resumepc=%p resumeins=%u resumevalid=%u\n",
	  site, (unsigned int)J->cur.traceno, (unsigned int)J->parent,
	  (unsigned int)J->exitno, (const void *)J->pc,
	  (const void *)J->startpc, (const void *)mref(J->cur.startpc, BCIns),
	  (unsigned int)bc_op(J->cur.startins),
	  (const void *)mref(J->cur.resumepc, BCIns),
	  (const void *)mref(J->cur.resumepc, BCIns),
	  (unsigned int)bc_op(J->cur.resumeins),
	  (unsigned int)J->cur.resumevalid);
}

static int lj_trace_s390x_varg_bias_override(void)
{
  static int bias = -1000;
  if (bias == -1000) {
    const char *s = getenv("LUAJIT_S390X_VARG_BIAS");
    bias = s ? atoi(s) : -999;
  }
  return bias;
}

static uint32_t lj_trace_s390x_load_be32(const uint8_t *p)
{
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	 ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint64_t lj_trace_s390x_load_be64(const uint8_t *p)
{
  return ((uint64_t)lj_trace_s390x_load_be32(p) << 32) |
	 (uint64_t)lj_trace_s390x_load_be32(p + 4);
}

static void lj_trace_s390x_dump_ptr_u32(FILE *out, const char *label, uintptr_t p)
{
#if LJ_TARGET_S390X
  const uint8_t *q = (const uint8_t *)p;
  int i;
  fprintf(out, " %s=%p", label, (const void *)p);
  if (!p)
    return;
  for (i = 0; i < 8; i++) {
    uint32_t v = lj_trace_s390x_load_be32(q + i * 8);
    fprintf(out, " [%d]=%u/0x%08x", i, (unsigned int)v, (unsigned int)v);
  }
#else
  UNUSED(out); UNUSED(label); UNUSED(p);
#endif
}

static void lj_trace_s390x_dump_tvalue_pair(FILE *out, const char *label, uintptr_t p)
{
#if LJ_TARGET_S390X
  const uint8_t *q = (const uint8_t *)p;
  uint64_t v0, v1;
  uint32_t tag0, tag1;
  fprintf(out, " %s=%p", label, (const void *)p);
  if (!p)
    return;
  v0 = lj_trace_s390x_load_be64(q);
  v1 = lj_trace_s390x_load_be64(q + 8);
  tag0 = (uint32_t)(v0 >> 47);
  tag1 = (uint32_t)(v1 >> 47);
  fprintf(out,
	  " q0=%#llx tag0=%u/0x%x q1=%#llx tag1=%u/0x%x",
	  (unsigned long long)v0, (unsigned int)tag0, (unsigned int)tag0,
	  (unsigned long long)v1, (unsigned int)tag1, (unsigned int)tag1);
#else
  UNUSED(out); UNUSED(label); UNUSED(p);
#endif
}

static void lj_trace_s390x_dump_dispatch_tmptv(FILE *out, uintptr_t dispatch,
					       uintptr_t result)
{
#if LJ_TARGET_S390X
  uintptr_t g, tmptv, tmptv2;
  if (!dispatch) {
    fprintf(out, " dispatch=<null>");
    return;
  }
  g = dispatch + GG_DISP2G;
  tmptv = g + offsetof(global_State, tmptv);
  tmptv2 = g + offsetof(global_State, tmptv2);
  fprintf(out,
	  " dispatch=%p g=%p tmptv=%p tmptv2=%p match_tmptv=%d match_tmptv2=%d",
	  (const void *)dispatch, (const void *)g, (const void *)tmptv,
	  (const void *)tmptv2, result == tmptv, result == tmptv2);
  lj_trace_s390x_dump_tvalue_pair(out, "gl_tmptv", tmptv);
  lj_trace_s390x_dump_tvalue_pair(out, "gl_tmptv2", tmptv2);
#else
  UNUSED(out); UNUSED(dispatch); UNUSED(result);
#endif
}

static uint32_t lj_trace_s390x_guard_mark(uintptr_t dispatch)
{
#if LJ_TARGET_S390X
  uintptr_t g, p;
  if (!dispatch)
    return 0;
  g = dispatch + GG_DISP2G;
  p = g + offsetof(global_State, tmptv2) + (LJ_BE ? 4 : 0);
  return *(const uint32_t *)p;
#else
  UNUSED(dispatch);
  return 0;
#endif
}

static void lj_trace_s390x_dump_ptr_bias(FILE *out, const char *label, uintptr_t p)
{
#if LJ_TARGET_S390X
  const uint8_t *q = (const uint8_t *)p;
  int i;
  fprintf(out, " %s", label);
  if (!p) {
    fprintf(out, "=<null>");
    return;
  }
  for (i = 0; i < 8; i++) {
    uint32_t v = lj_trace_s390x_load_be32(q + i);
    fprintf(out, " +%d=%u/0x%08x", i, (unsigned int)v, (unsigned int)v);
  }
#else
  UNUSED(out); UNUSED(label); UNUSED(p);
#endif
}

static void lj_trace_s390x_dump_stride_bias(FILE *out, const char *label,
					    uintptr_t p, int bias)
{
#if LJ_TARGET_S390X
  static const int slots[] = { -24, -16, -8, 0, 8, 16, 24 };
  int i;
  fprintf(out, " %s", label);
  if (!p) {
    fprintf(out, "=<null>");
    return;
  }
  for (i = 0; i < (int)(sizeof(slots) / sizeof(slots[0])); i++) {
    const uint8_t *q = (const uint8_t *)(p + slots[i] + bias);
    uint32_t v = lj_trace_s390x_load_be32(q);
    fprintf(out, " %d:%u/0x%08x", slots[i], (unsigned int)v, (unsigned int)v);
  }
#else
  UNUSED(out); UNUSED(label); UNUSED(p); UNUSED(bias);
#endif
}

static void lj_trace_s390x_dump_snapmap(FILE *out, const char *label,
					const GCtrace *T, ExitNo exitno)
{
#if LJ_TARGET_S390X
  const SnapShot *snap;
  const SnapEntry *map;
  const BCIns *pc;
  MSize i;
  if (T == NULL || exitno >= T->nsnap) {
    fprintf(out, "%s trace=nil exit=%u\n", label, (unsigned int)exitno);
    return;
  }
  snap = &T->snap[exitno];
  map = &T->snapmap[snap->mapofs];
  pc = snap_pc((SnapEntry *)&map[snap->nent]);
  fprintf(out,
	  "%s trace=%u exit=%u mapofs=%u ref=%u nent=%u count=%u pc=%p op=%u",
	  label,
	  (unsigned int)T->traceno,
	  (unsigned int)exitno,
	  (unsigned int)snap->mapofs,
	  (unsigned int)snap->ref,
	  (unsigned int)snap->nent,
	  (unsigned int)snap->count,
	  (const void *)pc,
	  (unsigned int)(pc ? bc_op(*pc) : 0));
  for (i = 0; i < snap->nent; i++) {
    SnapEntry sn = map[i];
    fprintf(out,
	    " #%u=%#x(ref=%u frame=%u cont=%u norestore=%u key=%u softfp=%u)",
	    (unsigned int)i, (unsigned int)sn, (unsigned int)snap_ref(sn),
	    (unsigned int)((sn & SNAP_FRAME) != 0),
	    (unsigned int)((sn & SNAP_CONT) != 0),
	    (unsigned int)((sn & SNAP_NORESTORE) != 0),
	    (unsigned int)((sn & SNAP_KEYINDEX) != 0),
	    (unsigned int)((sn & SNAP_SOFTFPNUM) != 0));
  }
  fprintf(out, "\n");
#else
  UNUSED(out); UNUSED(label); UNUSED(T); UNUSED(exitno);
#endif
}

LJ_FUNC int32_t lj_trace_s390x_varg_probe(const void *effp, int32_t ignored)
{
#if LJ_TARGET_S390X
  static int dump_count = 0;
  const uint8_t *eff = (const uint8_t *)effp;
  int bias = lj_trace_s390x_varg_bias_override();
  int retbias = bias != -999 ? bias : 1;
  uint32_t retv = lj_trace_s390x_load_be32(eff + retbias);

  if (lj_trace_s390x_varg_dump_enabled() && dump_count < 64) {
    int i;
    fprintf(stderr, "S390X_VARG_PROBE n=%d eff=%p ignored=%d retbias=%d ret=%u\n",
	    dump_count, (const void *)eff, (int)ignored,
	    retbias, (unsigned int)retv);
    fprintf(stderr, "S390X_VARG_BYTES");
    for (i = -8; i < 24; i++) {
      const uint8_t *p = eff + i;
      fprintf(stderr, " %c%02x", i == 0 ? '|' : ' ', (unsigned int)*p);
    }
    fprintf(stderr, "\n");
    fprintf(stderr, "S390X_VARG_U32");
    for (i = 0; i < 8; i++) {
      uint32_t v = lj_trace_s390x_load_be32(eff + i);
      fprintf(stderr, " %d:%u/0x%08x", i, (unsigned int)v, (unsigned int)v);
    }
    fprintf(stderr, "\n");
    dump_count++;
  }

  return (int32_t)retv;
#else
  UNUSED(effp);
  UNUSED(ignored);
  return 0;
#endif
}

static uintptr_t lj_trace_s390x_exit_lr(const ExitState *ex)
{
#if LJ_TARGET_S390X
  return ex ? (uintptr_t)ex->gpr[RID_R14] : 0;
#else
  UNUSED(ex);
  return 0;
#endif
}

#if LJ_TARGET_S390X
static int lj_trace_s390x_exit_stub_info(const GCtrace *T, const ExitState *ex,
					 uint32_t *slotp, int32_t *fromp,
					 uintptr_t *basep)
{
  uintptr_t lr, base, limit, span, delta, retadj;
  uint32_t maxexit;

  if (!T || !T->mcode || !ex)
    return 0;

  lr = (uintptr_t)ex->gpr[RID_R14];
  maxexit = T->root ? T->nsnap + 1 : T->nsnap;
  base = (uintptr_t)exitstub_trace_addr(T, 0);
  limit = (uintptr_t)exitstub_trace_addr(T, maxexit);
  span = (uintptr_t)(EXITSTUB_SPACING * sizeof(MCode));
  retadj = 6;  /* s390x BRASL returns after the 6-byte branch instruction. */

  if (lr < base || lr > limit || span == 0)
    return 0;

  delta = lr - base;
  if (delta < retadj || ((delta - retadj) % span) != 0)
    return 0;

  *slotp = (uint32_t)((delta - retadj) / span);
  *fromp = (int32_t)*slotp - 1;
  *basep = base;
  return 1;
}
#endif

static void lj_trace_s390x_exit_log(const char *phase, jit_State *J,
				    const BCIns *pc, SnapNo snapcount,
				    const ExitState *ex)
{
  if (!lj_trace_s390x_exit_log_enabled())
    return;
  {
    GCtrace *T = (J->parent > 0 && J->parent < J->sizetrace) ?
		 traceref(J, J->parent) : NULL;
    SnapNo exitno = (SnapNo)J->exitno;
    uint32_t snapref = 0;
    uint8_t snapnent = 0;
    const GCtrace *CT = T;
    if (T && exitno < T->nsnap) {
      snapref = T->snap[exitno].ref;
      snapnent = T->snap[exitno].nent;
    }
    if (ex) {
#if LJ_TARGET_S390X
      uint32_t stubslot = 0;
      int32_t stubexit = -1;
      uintptr_t stubbase = 0;
#ifdef EXITSTATE_PCREG
      uintptr_t pcreg = (uintptr_t)ex->gpr[EXITSTATE_PCREG];
      uintptr_t mcode = T ? (uintptr_t)T->mcode : 0;
      intptr_t mcofs = (pcreg >= mcode && pcreg < mcode + T->szmcode) ?
		       (intptr_t)(pcreg - mcode) : -1;
#else
      uintptr_t pcreg = 0;
      intptr_t mcofs = -1;
#endif
      if (lj_trace_s390x_exit_stub_info(T, ex, &stubslot, &stubexit, &stubbase)) {
	fprintf(stderr,
		"S390X_EXIT phase=%s trace=%u exit=%u pc=%p op=%u snapcount=%u snapref=%u snapnent=%u state=%u lr=%p pcreg=%p mcofs=%ld stubbase=%p stubslot=%u stubexit=%d guardmark=%#x f0=%g f1=%g f2=%g f3=%g f4=%g f12=%g f14=%g f15=%g r2=%#llx r3=%#llx r4=%#llx r5=%#llx r6=%#llx r7=%#llx r8=%#llx r9=%#llx r10=%#llx r11=%#llx r12=%#llx\n",
		phase,
		(unsigned int)J->parent,
		(unsigned int)J->exitno,
		(const void *)pc,
		(unsigned int)(pc ? bc_op(*pc) : 0),
		(unsigned int)snapcount,
		(unsigned int)snapref,
		(unsigned int)snapnent,
		(unsigned int)J->state,
		(const void *)lj_trace_s390x_exit_lr(ex),
		(const void *)pcreg,
		(long)mcofs,
		(const void *)stubbase,
		(unsigned int)stubslot,
		(int)stubexit,
		(unsigned int)lj_trace_s390x_guard_mark((uintptr_t)ex->gpr[RID_DISPATCH]),
		ex->fpr[0],
		ex->fpr[1],
		ex->fpr[2],
		ex->fpr[3],
		ex->fpr[4],
		ex->fpr[12],
		ex->fpr[14],
		ex->fpr[15],
		(unsigned long long)ex->gpr[RID_R2],
		(unsigned long long)ex->gpr[RID_R3],
		(unsigned long long)ex->gpr[RID_R4],
		(unsigned long long)ex->gpr[RID_R5],
		(unsigned long long)ex->gpr[RID_R6],
		(unsigned long long)ex->gpr[RID_R7],
		(unsigned long long)ex->gpr[RID_R8],
		(unsigned long long)ex->gpr[RID_R9],
		(unsigned long long)ex->gpr[RID_DISPATCH],
		(unsigned long long)ex->gpr[RID_R11],
		(unsigned long long)ex->gpr[RID_R12]);
	fprintf(stderr, "\n");
	lj_trace_s390x_dump_ptr_u32(stderr, "r3ptr", (uintptr_t)ex->gpr[RID_R3]);
	lj_trace_s390x_dump_tvalue_pair(stderr, "r3tv", (uintptr_t)ex->gpr[RID_R3]);
	lj_trace_s390x_dump_stride_bias(stderr, "r3+4", (uintptr_t)ex->gpr[RID_R3], 4);
	lj_trace_s390x_dump_dispatch_tmptv(stderr,
					   (uintptr_t)ex->gpr[RID_DISPATCH],
					   (uintptr_t)ex->gpr[RID_R3]);
	fprintf(stderr, "\n");
      } else {
	fprintf(stderr,
		"S390X_EXIT phase=%s trace=%u exit=%u pc=%p op=%u snapcount=%u snapref=%u snapnent=%u state=%u lr=%p pcreg=%p mcofs=%ld guardmark=%#x f0=%g f1=%g f2=%g f3=%g f4=%g f12=%g f14=%g f15=%g r2=%#llx r3=%#llx r4=%#llx r5=%#llx r6=%#llx r7=%#llx r8=%#llx r9=%#llx r10=%#llx r11=%#llx r12=%#llx\n",
		phase,
		(unsigned int)J->parent,
		(unsigned int)J->exitno,
		(const void *)pc,
		(unsigned int)(pc ? bc_op(*pc) : 0),
		(unsigned int)snapcount,
		(unsigned int)snapref,
		(unsigned int)snapnent,
		(unsigned int)J->state,
		(const void *)lj_trace_s390x_exit_lr(ex),
		(const void *)pcreg,
		(long)mcofs,
		(unsigned int)lj_trace_s390x_guard_mark((uintptr_t)ex->gpr[RID_DISPATCH]),
		ex->fpr[0],
		ex->fpr[1],
		ex->fpr[2],
		ex->fpr[3],
		ex->fpr[4],
		ex->fpr[12],
		ex->fpr[14],
		ex->fpr[15],
		(unsigned long long)ex->gpr[RID_R2],
		(unsigned long long)ex->gpr[RID_R3],
		(unsigned long long)ex->gpr[RID_R4],
		(unsigned long long)ex->gpr[RID_R5],
		(unsigned long long)ex->gpr[RID_R6],
		(unsigned long long)ex->gpr[RID_R7],
		(unsigned long long)ex->gpr[RID_R8],
		(unsigned long long)ex->gpr[RID_R9],
		(unsigned long long)ex->gpr[RID_DISPATCH],
		(unsigned long long)ex->gpr[RID_R11],
		(unsigned long long)ex->gpr[RID_R12]);
	fprintf(stderr, "\n");
	if (T) {
	  uintptr_t lr = (uintptr_t)ex->gpr[RID_R14];
	  uint32_t maxexit = T->root ? T->nsnap + 1 : T->nsnap;
	  uintptr_t base = (uintptr_t)exitstub_trace_addr(T, 0);
	  uintptr_t limit = (uintptr_t)exitstub_trace_addr(T, maxexit);
	  uintptr_t span = (uintptr_t)(EXITSTUB_SPACING * sizeof(MCode));
	  uintptr_t retadj = 6;
	  unsigned long long delta = (lr >= base) ?
				     (unsigned long long)(lr - base) : 0ull;
	  unsigned long long slotdelta = (delta >= retadj) ?
					 (delta - retadj) : delta;
	  fprintf(stderr,
		  "S390X_EXIT_STUBMISS trace=%u exit=%u lr=%p base=%p limit=%p span=%llu retadj=%llu delta=%llu slotdelta=%llu maxexit=%u\n",
		  (unsigned int)J->parent,
		  (unsigned int)J->exitno,
		  (const void *)lr,
		  (const void *)base,
		  (const void *)limit,
		  (unsigned long long)span,
		  (unsigned long long)retadj,
		  delta,
		  slotdelta,
		  (unsigned int)maxexit);
	}
	lj_trace_s390x_dump_ptr_u32(stderr, "r3ptr", (uintptr_t)ex->gpr[RID_R3]);
	lj_trace_s390x_dump_tvalue_pair(stderr, "r3tv", (uintptr_t)ex->gpr[RID_R3]);
	lj_trace_s390x_dump_stride_bias(stderr, "r3+4", (uintptr_t)ex->gpr[RID_R3], 4);
	lj_trace_s390x_dump_dispatch_tmptv(stderr,
					   (uintptr_t)ex->gpr[RID_DISPATCH],
					   (uintptr_t)ex->gpr[RID_R3]);
	fprintf(stderr, "\n");
      }
      if (J->parent == 1 && J->exitno == 4) {
	fprintf(stderr,
		"S390X_EXIT_SPILL trace=%u exit=%u s44=%#x s45=%#x s46=%#x s47=%#x s48=%#x s49=%#x\n",
		(unsigned int)J->parent, (unsigned int)J->exitno,
		(unsigned int)ex->spill[44], (unsigned int)ex->spill[45],
		(unsigned int)ex->spill[46], (unsigned int)ex->spill[47],
		(unsigned int)ex->spill[48], (unsigned int)ex->spill[49]);
      }
#endif
    } else {
      fprintf(stderr,
	      "S390X_EXIT phase=%s parent=%u exit=%u pc=%p op=%u snapcount=%u snapref=%u snapnent=%u state=%u\n",
	      phase,
	      (unsigned int)J->parent,
	      (unsigned int)J->exitno,
	      (const void *)pc,
	      (unsigned int)(pc ? bc_op(*pc) : 0),
	      (unsigned int)snapcount,
	      (unsigned int)snapref,
	      (unsigned int)snapnent,
	      (unsigned int)J->state);
    }
    if (CT && exitno < CT->nsnap) {
      const SnapShot *snap = &CT->snap[exitno];
      const SnapEntry *map = &CT->snapmap[snap->mapofs];
      const BCIns *snappc = snap_pc(&map[snap->nent]);
      MSize i;
      fprintf(stderr, "S390X_EXIT_SNAP trace=%u exit=%u snappc=%p snapop=%u",
	      (unsigned int)J->parent, (unsigned int)exitno,
	      (const void *)snappc,
	      (unsigned int)(snappc ? bc_op(*snappc) : 0));
      for (i = 0; i < snap->nent; i++) {
	SnapEntry sn = map[i];
	IRRef ref = snap_ref(sn);
	IRIns *ir = &CT->ir[ref];
	fprintf(stderr, " slot%u=ref%u%s[o=%u t=%u op1=%u op2=%u r=%u prev=%u]",
		(unsigned int)snap_slot(sn),
		(unsigned int)(ref - REF_BIAS),
		(sn & SNAP_NORESTORE) ? "!" : "",
		(unsigned int)ir->o,
		(unsigned int)irt_type(ir->t),
		(unsigned int)ir->op1,
		(unsigned int)ir->op2,
		(unsigned int)ir->r,
		(unsigned int)ir->prev);
      }
      fprintf(stderr, "\n");
    }
  }
}

static void lj_trace_s390x_slot_log(lua_State *L, const BCIns *pc)
{
  int i;
  const BCIns *rpc;
  if (!lj_trace_s390x_slot_log_enabled() || !L || !L->base)
    return;
  rpc = cframe_pc(L->cframe);
  fprintf(stderr, "S390X_SLOTS pc=%p op=%u base=%p\n",
	  (const void *)pc, (unsigned int)(pc ? bc_op(*pc) : 0),
	  (void *)L->base);
  fprintf(stderr, "S390X_RESUME pc=%p op=%u\n",
	  (const void *)rpc, (unsigned int)(rpc ? bc_op(*rpc) : 0));
  for (i = 0; i < 20; i++) {
    TValue *o = &L->base[i];
    fprintf(stderr, "S390X_SLOT idx=%d itype=%d u64=0x%016llx\n",
	    i, (int)itype(o), (unsigned long long)o->u64);
  }
  if (tviscdata(&L->base[5])) {
    GCcdata *cd = cdataV(&L->base[5]);
    int32_t *p = (int32_t *)cdataptr(cd);
    fprintf(stderr, "S390X_CDATA slot=5 ptr=%p x0=%d x1=%d\n",
	    (void *)p, (int)p[0], (int)p[1]);
  }
}

static void lj_trace_s390x_hotside_state_log(jit_State *J, const BCIns *pc,
					     GCtrace *T, SnapShot *snap,
					     TraceNo candno, TraceNo childno)
{
  static const int slots[] = {0, 1, 3, 6, 9, 10, 11, 18};
  size_t i;
  SnapEntry *map;
  const BCIns *snappc;
  if (!lj_trace_s390x_hotside_focus_enabled() || !J || !J->L || !J->L->base ||
      !T || !snap)
    return;
  map = &T->snapmap[snap->mapofs];
  snappc = snap_pc(&map[snap->nent]);
  fprintf(stderr,
	  "S390X_HOTSIDE_STATE phase=start parent=%u exit=%u cand=%u child=%u root=%u pc=%p op=%u snappc=%p snapop=%u snapcount=%u startop=%u",
	  (unsigned int)J->parent, (unsigned int)J->exitno,
	  (unsigned int)candno, (unsigned int)childno,
	  (unsigned int)(T->root ? T->root : T->traceno),
	  (const void *)pc, (unsigned int)(pc ? bc_op(*pc) : 0),
	  (const void *)snappc, (unsigned int)(snappc ? bc_op(*snappc) : 0),
	  (unsigned int)snap->count, (unsigned int)bc_op(T->startins));
  for (i = 0; i < sizeof(slots)/sizeof(slots[0]); i++) {
    int idx = slots[i];
    TValue *o = &J->L->base[idx];
    fprintf(stderr, " s%d=0x%016llx", idx, (unsigned long long)o->u64);
  }
  fputc('\n', stderr);
}

static void lj_trace_s390x_dump_trace_snaps(jit_State *J, GCtrace *T)
{
#if LJ_TARGET_S390X
  SnapNo i;
  if (!lj_trace_s390x_exit_log_enabled())
    return;
  if (J && J->cur.nsnap != 0) {
    fprintf(stderr, "S390X_TRACE_SNAPPCS cur startop=%u root=%u",
	    (unsigned int)bc_op(J->cur.startins),
	    (unsigned int)J->cur.root);
    for (i = 0; i < J->cur.nsnap; i++) {
      SnapShot *snap = &J->cur.snap[i];
      SnapEntry *map = &J->cur.snapmap[snap->mapofs];
      const BCIns *pc = snap_pc(&map[snap->nent]);
      fprintf(stderr, " #%u:%u", (unsigned int)i,
	      (unsigned int)(pc ? bc_op(*pc) : 0));
    }
    fprintf(stderr, "\n");
  } else if (T) {
    fprintf(stderr, "S390X_TRACE_SNAPPCS trace=%u root=%u startop=%u",
	    (unsigned int)T->traceno, (unsigned int)T->root,
	    (unsigned int)bc_op(T->startins));
    for (i = 0; i < T->nsnap; i++) {
      SnapShot *snap = &T->snap[i];
      SnapEntry *map = &T->snapmap[snap->mapofs];
      const BCIns *pc = snap_pc(&map[snap->nent]);
      fprintf(stderr, " #%u:%u", (unsigned int)i,
	      (unsigned int)(pc ? bc_op(*pc) : 0));
    }
    fprintf(stderr, "\n");
  }
#else
  UNUSED(J); UNUSED(T);
#endif
}

static void lj_trace_s390x_log_trace_meta(jit_State *J, GCtrace *T,
					  const char *phase)
{
#if LJ_TARGET_S390X
  MSize i;
  if (!lj_trace_s390x_trace_meta_log_enabled() || !T)
    return;
  fprintf(stderr,
	  "S390X_TRACE_META phase=%s trace=%u parent=%u exit=%u root=%u link=%u linktype=%u nextroot=%u nextside=%u nchild=%u resumechild=%u startpc=%p startop=%u topslot=%u spadjust=%u nsnap=%u nk=%u nins=%u mcode=%p szmcode=%u mcloop=%u\n",
	  phase ? phase : "?",
	  (unsigned int)T->traceno,
	  (unsigned int)J->parent,
	  (unsigned int)J->exitno,
	  (unsigned int)T->root,
	  (unsigned int)T->link,
	  (unsigned int)T->linktype,
	  (unsigned int)T->nextroot,
	  (unsigned int)T->nextside,
	  (unsigned int)T->nchild,
	  (unsigned int)T->resumechild,
	  (const void *)mref(T->startpc, BCIns),
	  (unsigned int)bc_op(T->startins),
	  (unsigned int)T->topslot,
	  (unsigned int)T->spadjust,
	  (unsigned int)T->nsnap,
	  (unsigned int)T->nk,
	  (unsigned int)T->nins,
	  (void *)T->mcode,
	  (unsigned int)T->szmcode,
	  (unsigned int)T->mcloop);
  for (i = 0; i < T->nsnap; i++) {
    SnapShot *snap = &T->snap[i];
    SnapEntry *map = &T->snapmap[snap->mapofs];
    const BCIns *pc = snap_pc(&map[snap->nent]);
    fprintf(stderr,
	    "S390X_TRACE_META_SNAP phase=%s trace=%u snap=%u mapofs=%u nent=%u count=%u nslots=%u topslot=%u pc=%p op=%u\n",
	    phase ? phase : "?",
	    (unsigned int)T->traceno,
	    (unsigned int)i,
	    (unsigned int)snap->mapofs,
	    (unsigned int)snap->nent,
	    (unsigned int)snap->count,
	    (unsigned int)snap->nslots,
	    (unsigned int)snap->topslot,
	    (const void *)pc,
	    (unsigned int)(pc ? bc_op(*pc) : 0));
  }
#else
  UNUSED(J); UNUSED(T); UNUSED(phase);
#endif
}

LJ_FUNC void lj_trace_s390x_iter_log(const TValue *base, const TValue *iterslot)
{
  int i;
  if (!lj_trace_s390x_iter_log_enabled() || !base || !iterslot)
    return;
  fprintf(stderr, "S390X_ITER base=%p iterslot=%p delta=%td\n",
	  (const void *)base, (const void *)iterslot, iterslot - base);
  for (i = -2; i < 10; i++) {
    const TValue *o = base + i;
    fprintf(stderr, "S390X_ITER base_slot=%d ptr=%p itype=%d u64=0x%016llx\n",
	    i, (const void *)o, (int)itype(o), (unsigned long long)o->u64);
  }
  for (i = -2; i < 10; i++) {
    const TValue *o = iterslot + i;
    fprintf(stderr, "S390X_ITER iter_slot=%d ptr=%p itype=%d u64=0x%016llx\n",
	    i, (const void *)o, (int)itype(o), (unsigned long long)o->u64);
  }
}

static void lj_trace_s390x_start_log(jit_State *J, const BCIns *pc)
{
  int i;
  lua_State *L;
  if (!lj_trace_s390x_start_log_enabled())
    return;
  if (!J)
    return;
  L = J->L;
  if (!L || !L->base)
    return;
  fprintf(stderr, "S390X_TRACE_START pc=%p op=%u base=%p parent=%u exit=%u\n",
	  (const void *)pc, (unsigned int)(pc ? bc_op(*pc) : 0),
	  (void *)L->base, (unsigned int)J->parent, (unsigned int)J->exitno);
  for (i = 0; i < 8; i++) {
    TValue *o = &L->base[i];
    fprintf(stderr, "S390X_TRACE_SLOT idx=%d itype=%d u64=0x%016llx\n",
	    i, (int)itype(o), (unsigned long long)o->u64);
  }
}

static void lj_trace_s390x_abort_log(jit_State *J, TraceError e)
{
  if (!lj_trace_s390x_abort_log_enabled() || !J)
    return;
  fprintf(stderr,
	  "S390X_TRACE_ABORT trace=%u parent=%u exit=%u state=%u pc=%p op=%u startpc=%p startop=%u err=%d root=%u link=%u linktype=%u resumepc=%p resumeop=%u resumevalid=%u\n",
	  (unsigned int)J->cur.traceno,
	  (unsigned int)J->parent,
	  (unsigned int)J->exitno,
	  (unsigned int)J->state,
	  (const void *)J->pc,
	  (unsigned int)(J->pc ? bc_op(*J->pc) : 0),
	  (const void *)mref(J->cur.startpc, BCIns),
	  (unsigned int)bc_op(J->cur.startins),
	  (int)e,
	  (unsigned int)J->cur.root,
	  (unsigned int)J->cur.link,
	  (unsigned int)J->cur.linktype,
	  (const void *)mref(J->cur.resumepc, BCIns),
	  (unsigned int)bc_op(J->cur.resumeins),
	  (unsigned int)J->cur.resumevalid);
}

/* The current trace is first assembled in J->cur. The variable length
** arrays point to shared, growable buffers (J->irbuf etc.). When trace
** recording ends successfully, the current trace and its data structures
** are copied to a new (compact) GCtrace object.
*/

/* Find a free trace number. */
static TraceNo trace_findfree(jit_State *J)
{
  MSize osz, lim;
  if (J->freetrace == 0)
    J->freetrace = 1;
  for (; J->freetrace < J->sizetrace; J->freetrace++)
    if (traceref(J, J->freetrace) == NULL)
      return J->freetrace++;
  /* Need to grow trace array. */
  lim = (MSize)J->param[JIT_P_maxtrace] + 1;
  if (lim < 2) lim = 2; else if (lim > 65535) lim = 65535;
  osz = J->sizetrace;
  if (osz >= lim)
    return 0;  /* Too many traces. */
  lj_mem_growvec(J->L, J->trace, J->sizetrace, lim, GCRef);
  for (; osz < J->sizetrace; osz++)
    setgcrefnull(J->trace[osz]);
  return J->freetrace;
}

#define TRACE_APPENDVEC(field, szfield, tp) \
  T->field = (tp *)p; \
  memcpy(p, J->cur.field, J->cur.szfield*sizeof(tp)); \
  p += J->cur.szfield*sizeof(tp);

#ifdef LUAJIT_USE_PERFTOOLS
/*
** Create symbol table of JIT-compiled code. For use with Linux perf tools.
** Example usage:
**   perf record -f -e cycles luajit test.lua
**   perf report -s symbol
**   rm perf.data /tmp/perf-*.map
*/
#include <stdio.h>
#include <unistd.h>

static void perftools_addtrace(GCtrace *T)
{
  static FILE *fp;
  GCproto *pt = &gcref(T->startpt)->pt;
  const BCIns *startpc = mref(T->startpc, const BCIns);
  const char *name = proto_chunknamestr(pt);
  BCLine lineno;
  if (name[0] == '@' || name[0] == '=')
    name++;
  else
    name = "(string)";
  lj_assertX(startpc >= proto_bc(pt) && startpc < proto_bc(pt) + pt->sizebc,
	     "trace PC out of range");
  lineno = lj_debug_line(pt, proto_bcpos(pt, startpc));
  if (!fp) {
    char fname[40];
    sprintf(fname, "/tmp/perf-%d.map", getpid());
    if (!(fp = fopen(fname, "w"))) return;
    setlinebuf(fp);
  }
  fprintf(fp, "%lx %x TRACE_%d::%s:%u\n",
	  (long)T->mcode, T->szmcode, T->traceno, name, lineno);
}
#endif

/* Allocate space for copy of T. */
GCtrace * LJ_FASTCALL lj_trace_alloc(lua_State *L, GCtrace *T)
{
  size_t sztr = ((sizeof(GCtrace)+7)&~7);
  size_t szins = (T->nins-T->nk)*sizeof(IRIns);
  size_t sz = sztr + szins +
	      T->nsnap*sizeof(SnapShot) +
	      T->nsnapmap*sizeof(SnapEntry);
  GCtrace *T2 = lj_mem_newt(L, (MSize)sz, GCtrace);
  char *p = (char *)T2 + sztr;
  T2->gct = ~LJ_TTRACE;
  T2->marked = 0;
  T2->traceno = 0;
  T2->ir = (IRIns *)p - T->nk;
  T2->nins = T->nins;
  T2->nk = T->nk;
  T2->nsnap = T->nsnap;
  T2->nsnapmap = T->nsnapmap;
  memcpy(p, T->ir + T->nk, szins);
  return T2;
}

/* Save current trace by copying and compacting it. */
static void trace_save(jit_State *J, GCtrace *T)
{
  size_t sztr = ((sizeof(GCtrace)+7)&~7);
  size_t szins = (J->cur.nins-J->cur.nk)*sizeof(IRIns);
  char *p = (char *)T + sztr;
  if (J->parent == 0) {
    setmref(J->cur.resumepc, NULL);
    J->cur.resumeins = 0;
    J->cur.resumevalid = 0;
  } else if (J->cur.root == 1 && J->parent == 1 &&
	     bc_op(J->cur.startins) == BC_JMP &&
	     !J->cur.resumevalid) {
    const BCIns *startpc = mref(J->cur.startpc, const BCIns);
    const BCIns *resumepc = NULL;
    BCIns resumeins = 0;
    int stub_only = (lj_trace_s390x_child_resume_stub_only_enabled() &&
		     J->exitno != 4);
    if (stub_only) {
      if (lj_trace_s390x_root_freeze_log_enabled()) {
	fprintf(stderr,
		"S390X_CHILD_RESUME_SKIP trace=%u parent=%u exit=%u startpc=%p startins=%u linktype=%u\n",
		(unsigned int)J->cur.traceno, (unsigned int)J->parent,
		(unsigned int)J->exitno, (const void *)startpc,
		(unsigned int)bc_op(J->cur.startins),
		(unsigned int)J->cur.linktype);
      }
    } else if (lj_trace_s390x_child_inherit_root_resume_enabled() &&
	J->cur.root != 0) {
      GCtrace *root = traceref(J, J->cur.root);
      const BCIns *root_resumepc = mref(root->resumepc, const BCIns);
      if (root->resumevalid && root_resumepc != NULL) {
	resumepc = root_resumepc;
	resumeins = root->resumeins;
      }
    }
    if (!stub_only && resumepc == NULL && startpc != NULL) {
      resumepc = startpc + 1;
      resumeins = startpc[1];
    }
    if (!stub_only && resumepc != NULL) {
      if (lj_trace_s390x_bcjmp_self_jloop_resume_enabled() &&
	  J->cur.root != 0 &&
	  bc_op(J->cur.startins) == BC_JMP &&
	  J->cur.linktype == LJ_TRLINK_LOOP &&
	  J->cur.link == J->cur.traceno &&
	  J->cur.mcloop != 0) {
	resumepc = startpc;
	resumeins = BCINS_AD(BC_JLOOP, 0, J->cur.traceno);
	J->cur.unused1 = (uint8_t)BC_LOOP;
	if (lj_trace_s390x_root_freeze_log_enabled()) {
	  fprintf(stderr,
		  "S390X_CHILD_SELF_JLOOP trace=%u parent=%u exit=%u startpc=%p resumeins=%u mcloop=%u ownerop=%u\n",
		  (unsigned int)J->cur.traceno, (unsigned int)J->parent,
		  (unsigned int)J->exitno, (const void *)resumepc,
		  (unsigned int)bc_op(resumeins),
		  (unsigned int)J->cur.mcloop,
		  (unsigned int)J->cur.unused1);
	}
      }
      setmref(J->cur.resumepc, resumepc);
      J->cur.resumeins = resumeins;
      J->cur.resumevalid = 1;
      if (lj_trace_s390x_bcjmp_mcloop_entry_enabled() &&
	  J->cur.root != 0 &&
	  bc_op(J->cur.startins) == BC_JMP &&
	  J->cur.linktype == LJ_TRLINK_LOOP &&
	  J->cur.link == J->cur.traceno &&
	  J->cur.mcloop != 0) {
	J->cur.unused1 = (uint8_t)BC_LOOP;
	if (lj_trace_s390x_root_freeze_log_enabled()) {
	  fprintf(stderr,
		  "S390X_CHILD_MCLOOP_OWNER trace=%u parent=%u exit=%u startop=%u link=%u linktype=%u mcloop=%u ownerop=%u\n",
		  (unsigned int)J->cur.traceno, (unsigned int)J->parent,
		  (unsigned int)J->exitno, (unsigned int)bc_op(J->cur.startins),
		  (unsigned int)J->cur.link, (unsigned int)J->cur.linktype,
		  (unsigned int)J->cur.mcloop, (unsigned int)J->cur.unused1);
	}
      }
      if (lj_trace_s390x_jloop_exec_skip_mcloop_enabled() &&
	  J->cur.root == 1 &&
	  J->parent == 1 && J->exitno == 1 &&
	  bc_op(J->cur.startins) == BC_JMP &&
	  J->cur.linktype == LJ_TRLINK_LOOP &&
	  J->cur.link == J->cur.traceno &&
	  J->cur.resumevalid &&
	  bc_op(J->cur.resumeins) == BC_JLOOP &&
	  J->cur.mcloop != 0) {
	J->cur.unused1 = 0;
	if (lj_trace_s390x_root_freeze_log_enabled()) {
	  fprintf(stderr,
		  "S390X_CHILD_EXEC_NOMCLOOP trace=%u parent=%u exit=%u link=%u linktype=%u resumeop=%u mcloop=%u ownerop=%u\n",
		  (unsigned int)J->cur.traceno, (unsigned int)J->parent,
		  (unsigned int)J->exitno, (unsigned int)J->cur.link,
		  (unsigned int)J->cur.linktype,
		  (unsigned int)bc_op(J->cur.resumeins),
		  (unsigned int)J->cur.mcloop,
		  (unsigned int)J->cur.unused1);
	}
      }
      if (lj_trace_s390x_root_freeze_log_enabled()) {
	fprintf(stderr,
		"S390X_CHILD_RESUME_SETUP trace=%u parent=%u exit=%u startpc=%p startins=%u resumepc=%p resumeins=%u inherit_root=%u\n",
		(unsigned int)J->cur.traceno, (unsigned int)J->parent,
		(unsigned int)J->exitno, (const void *)startpc,
		(unsigned int)bc_op(J->cur.startins),
		(const void *)resumepc,
		(unsigned int)bc_op(J->cur.resumeins),
	(unsigned int)(resumepc != NULL && startpc != NULL &&
			       resumepc != startpc + 1));
      }
    }
  } else if (J->cur.root == 1 && J->parent >= 3 && J->exitno == 0 &&
	     bc_op(J->cur.startins) == BC_JMP &&
	     (J->cur.linktype == LJ_TRLINK_ROOT ||
	      J->cur.linktype == LJ_TRLINK_LOOP) &&
	     !J->cur.resumevalid &&
	     getenv("LUAJIT_S390X_BCJMP_LOOPDESC_RESUME") != NULL) {
    const BCIns *startpc = mref(J->cur.startpc, const BCIns);
    int is_pure_loop_stub = (J->cur.nins == 32772 || J->cur.mcloop != 0);
    if (startpc != NULL) {
      const BCIns *resumepc = startpc + 1;
      BCIns resumeins = startpc[1];
      if (getenv("LUAJIT_S390X_BCJMP_LOOPDESC_SELF_JLOOP_RESUME") != NULL &&
	  is_pure_loop_stub &&
	  J->cur.linktype == LJ_TRLINK_LOOP &&
	  J->cur.link == J->cur.traceno &&
	  J->cur.mcloop != 0) {
	resumepc = startpc;
	resumeins = BCINS_AD(BC_JLOOP, 0, J->cur.traceno);
	J->cur.unused1 = (uint8_t)BC_LOOP;
      }
      setmref(J->cur.resumepc, resumepc);
      J->cur.resumeins = resumeins;
      J->cur.resumevalid = 1;
      if (lj_trace_s390x_bcjmp_loopdesc_resume_child_enabled() &&
	  is_pure_loop_stub &&
	  J->cur.linktype == LJ_TRLINK_ROOT &&
	  J->cur.link != 0)
	J->cur.resumechild = (TraceNo1)J->cur.link;
      if (lj_trace_s390x_root_freeze_log_enabled()) {
	fprintf(stderr,
		"S390X_CHILD_RESUME_LOOPDESC trace=%u parent=%u exit=%u startpc=%p startins=%u resumepc=%p resumeins=%u link=%u linktype=%u resumechild=%u nins=%u mcloop=%u purestub=%u\n",
		(unsigned int)J->cur.traceno, (unsigned int)J->parent,
		(unsigned int)J->exitno, (const void *)startpc,
		(unsigned int)bc_op(J->cur.startins),
		(const void *)resumepc,
		(unsigned int)bc_op(J->cur.resumeins),
		(unsigned int)J->cur.link,
		(unsigned int)J->cur.linktype,
		(unsigned int)J->cur.resumechild,
		(unsigned int)J->cur.nins,
		(unsigned int)J->cur.mcloop,
		(unsigned int)is_pure_loop_stub);
      }
    }
  }
  if (lj_trace_s390x_loopdesc_owner_direct_entry_enabled() &&
      J->cur.root == 1 && J->parent == 3 && J->exitno == 0 &&
      bc_op(J->cur.startins) == BC_JMP &&
      J->cur.linktype == LJ_TRLINK_ROOT &&
      J->cur.link == J->parent &&
      J->cur.resumevalid &&
      J->cur.resumechild == 0 &&
      J->cur.nins == 32770 &&
      J->cur.mcloop == 0) {
    const BCIns *startpc = mref(J->cur.startpc, const BCIns);
    if (startpc != NULL) {
      setmref(J->cur.resumepc, startpc);
      J->cur.resumeins = BCINS_AD(BC_JLOOP, 0, J->cur.traceno);
      J->cur.resumevalid = 1;
    }
    J->cur.resumechild = (TraceNo1)J->cur.traceno;
    J->cur.unused1 = (uint8_t)BC_LOOP;
    if (lj_trace_s390x_root_freeze_log_enabled()) {
      fprintf(stderr,
	      "S390X_LOOPDESC_OWNER_DIRECT trace=%u parent=%u exit=%u link=%u linktype=%u resumechild=%u resumeop=%u ownerop=%u nins=%u mcloop=%u\n",
	      (unsigned int)J->cur.traceno, (unsigned int)J->parent,
	      (unsigned int)J->exitno, (unsigned int)J->cur.link,
	      (unsigned int)J->cur.linktype,
	      (unsigned int)J->cur.resumechild,
	      (unsigned int)bc_op(J->cur.resumeins),
	      (unsigned int)J->cur.unused1,
	      (unsigned int)J->cur.nins,
	      (unsigned int)J->cur.mcloop);
    }
  }
  if (lj_trace_s390x_loopdesc_root_resumechild_enabled() &&
      J->cur.root == 1 && J->parent == 3 && J->exitno == 0 &&
      bc_op(J->cur.startins) == BC_JMP &&
      J->cur.linktype == LJ_TRLINK_ROOT &&
      J->cur.link != 0 &&
      J->cur.resumevalid &&
      J->cur.resumechild == 0 &&
      J->cur.nins == 32770 &&
      J->cur.mcloop == 0) {
    J->cur.resumechild = (TraceNo1)J->cur.link;
    if (lj_trace_s390x_root_freeze_log_enabled()) {
      fprintf(stderr,
	      "S390X_LOOPDESC_ROOT_RESUMECHILD trace=%u parent=%u exit=%u link=%u linktype=%u resumechild=%u resumeop=%u ownerop=%u nins=%u mcloop=%u\n",
	      (unsigned int)J->cur.traceno, (unsigned int)J->parent,
	      (unsigned int)J->exitno, (unsigned int)J->cur.link,
	      (unsigned int)J->cur.linktype,
	      (unsigned int)J->cur.resumechild,
	      (unsigned int)bc_op(J->cur.resumeins),
	      (unsigned int)J->cur.unused1,
	      (unsigned int)J->cur.nins,
	      (unsigned int)J->cur.mcloop);
    }
  }
  lj_trace_s390x_root_freeze_log(J, "trace_save_pre_memcpy");
  lj_trace_s390x_bridge_meta_log("save_pre_memcpy", &J->cur, J);
  memcpy(T, &J->cur, sizeof(GCtrace));
  setgcrefr(T->nextgc, J2G(J)->gc.root);
  setgcrefp(J2G(J)->gc.root, T);
  newwhite(J2G(J), T);
  T->gct = ~LJ_TTRACE;
  if (lj_trace_s390x_root_promote_loopdesc_owner_enabled() &&
      J->cur.root == 1 && J->parent >= 3 && J->exitno == 0 &&
      bc_op(T->startins) == BC_JMP &&
      T->linktype == LJ_TRLINK_LOOP &&
      T->link == T->traceno &&
      T->resumevalid &&
      bc_op(T->resumeins) == BC_JLOOP &&
      T->mcloop != 0) {
    GCtrace *root = traceref(J, 1);
    TraceNo prev_resumechild = root->resumechild;
    root->resumechild = (TraceNo1)T->traceno;
    if (lj_trace_s390x_root_freeze_log_enabled()) {
      fprintf(stderr,
              "S390X_ROOT_PROMOTE_LOOPDESC_OWNER root=%u oldresumechild=%u newresumechild=%u trace=%u link=%u linktype=%u resumeop=%u mcloop=%u\n",
              (unsigned int)root->traceno,
              (unsigned int)prev_resumechild,
              (unsigned int)root->resumechild,
              (unsigned int)T->traceno,
              (unsigned int)T->link,
              (unsigned int)T->linktype,
              (unsigned int)bc_op(T->resumeins),
              (unsigned int)T->mcloop);
    }
  }
  T->ir = (IRIns *)p - J->cur.nk;  /* The IR has already been copied above. */
#if LJ_ABI_PAUTH
  T->mcauth = lj_ptr_sign((ASMFunction)T->mcode, T);
#endif
  p += szins;
  TRACE_APPENDVEC(snap, nsnap, SnapShot)
  TRACE_APPENDVEC(snapmap, nsnapmap, SnapEntry)
  J->cur.traceno = 0;
  J->curfinal = NULL;
  setgcrefp(J->trace[T->traceno], T);
  lj_gc_barriertrace(J2G(J), T->traceno);
  lj_gdbjit_addtrace(J, T);
#ifdef LUAJIT_USE_PERFTOOLS
  perftools_addtrace(T);
#endif
}

void LJ_FASTCALL lj_trace_free(global_State *g, GCtrace *T)
{
  jit_State *J = G2J(g);
  if (T->traceno) {
    lj_gdbjit_deltrace(J, T);
    if (T->traceno < J->freetrace)
      J->freetrace = T->traceno;
    setgcrefnull(J->trace[T->traceno]);
  }
  lj_mem_free(g, T,
    ((sizeof(GCtrace)+7)&~7) + (T->nins-T->nk)*sizeof(IRIns) +
    T->nsnap*sizeof(SnapShot) + T->nsnapmap*sizeof(SnapEntry));
}

/* Re-enable compiling a prototype by unpatching any modified bytecode. */
void lj_trace_reenableproto(GCproto *pt)
{
  if ((pt->flags & PROTO_ILOOP)) {
    BCIns *bc = proto_bc(pt);
    BCPos i, sizebc = pt->sizebc;
    pt->flags &= ~PROTO_ILOOP;
    if (bc_op(bc[0]) == BC_IFUNCF)
      setbc_op(&bc[0], BC_FUNCF);
    for (i = 1; i < sizebc; i++) {
      BCOp op = bc_op(bc[i]);
      if (op == BC_IFORL || op == BC_IITERL || op == BC_ILOOP)
	setbc_op(&bc[i], (int)op+(int)BC_LOOP-(int)BC_ILOOP);
    }
  }
}

/* Unpatch the bytecode modified by a root trace. */
static void trace_unpatch(jit_State *J, GCtrace *T)
{
  BCOp op = bc_op(T->startins);
  BCIns *pc = mref(T->startpc, BCIns);
  UNUSED(J);
  if (op == BC_JMP)
    return;  /* No need to unpatch branches in parent traces (yet). */
  switch (bc_op(*pc)) {
  case BC_JFORL:
    lj_assertJ(traceref(J, bc_d(*pc)) == T, "JFORL references other trace");
    *pc = T->startins;
    pc += bc_j(T->startins);
    lj_assertJ(bc_op(*pc) == BC_JFORI, "FORL does not point to JFORI");
    setbc_op(pc, BC_FORI);
    break;
  case BC_JITERL:
  case BC_JLOOP:
    lj_assertJ(op == BC_ITERL || op == BC_ITERN || op == BC_LOOP ||
	       bc_isret(op), "bad original bytecode %d", op);
    *pc = T->startins;
    break;
  case BC_JFUNCF:
    lj_assertJ(op == BC_FUNCF, "bad original bytecode %d", op);
    *pc = T->startins;
    break;
  default:  /* Already unpatched. */
    break;
  }
}

/* Flush a root trace. */
static void trace_flushroot(jit_State *J, GCtrace *T)
{
  GCproto *pt = &gcref(T->startpt)->pt;
  lj_assertJ(T->root == 0, "not a root trace");
  lj_assertJ(pt != NULL, "trace has no prototype");
  /* Unlink root trace from chain anchored in prototype. */
  if (pt->trace == T->traceno) {  /* Trace is first in chain. Easy. */
    pt->trace = T->nextroot;
unpatch:
    /* Unpatch modified bytecode only if the trace has not been flushed. */
    trace_unpatch(J, T);
  } else if (pt->trace) {  /* Otherwise search in chain of root traces. */
    GCtrace *T2 = traceref(J, pt->trace);
    if (T2) {
      for (; T2->nextroot; T2 = traceref(J, T2->nextroot))
	if (T2->nextroot == T->traceno) {
	  T2->nextroot = T->nextroot;  /* Unlink from chain. */
	  goto unpatch;
	}
    }
  }
}

/* Flush a trace. Only root traces are considered. */
void lj_trace_flush(jit_State *J, TraceNo traceno)
{
  if (traceno > 0 && traceno < J->sizetrace) {
    GCtrace *T = traceref(J, traceno);
    if (T && T->root == 0)
      trace_flushroot(J, T);
  }
}

/* Flush all traces associated with a prototype. */
void lj_trace_flushproto(global_State *g, GCproto *pt)
{
  while (pt->trace != 0)
    trace_flushroot(G2J(g), traceref(G2J(g), pt->trace));
}

/* Flush all traces. */
int lj_trace_flushall(lua_State *L)
{
  jit_State *J = L2J(L);
  ptrdiff_t i;
  if ((J2G(J)->hookmask & HOOK_GC))
    return 1;
  for (i = (ptrdiff_t)J->sizetrace-1; i > 0; i--) {
    GCtrace *T = traceref(J, i);
    if (T) {
      if (T->root == 0)
	trace_flushroot(J, T);
      lj_gdbjit_deltrace(J, T);
      T->traceno = T->link = 0;  /* Blacklist the link for cont_stitch. */
      setgcrefnull(J->trace[i]);
    }
  }
  J->cur.traceno = 0;
  J->freetrace = 0;
  /* Clear penalty cache. */
  memset(J->penalty, 0, sizeof(J->penalty));
  /* Free the whole machine code and invalidate all exit stub groups. */
  lj_mcode_free(J);
  memset(J->exitstubgroup, 0, sizeof(J->exitstubgroup));
  lj_vmevent_send(J2G(J), TRACE,
    setstrV(V, V->top++, lj_str_newlit(V, "flush"));
  );
  return 0;
}

/* Initialize JIT compiler state. */
void lj_trace_initstate(global_State *g)
{
  jit_State *J = G2J(g);
  TValue *tv;

  J->prng = g->prng;

  /* Initialize aligned SIMD constants. */
  tv = LJ_KSIMD(J, LJ_KSIMD_ABS);
  tv[0].u64 = U64x(7fffffff,ffffffff);
  tv[1].u64 = U64x(7fffffff,ffffffff);
  tv = LJ_KSIMD(J, LJ_KSIMD_NEG);
  tv[0].u64 = U64x(80000000,00000000);
  tv[1].u64 = U64x(80000000,00000000);

  /* Initialize 32/64 bit constants. */
#if LJ_TARGET_X64 || LJ_TARGET_MIPS64
  J->k64[LJ_K64_M2P64].u64 = U64x(c3f00000,00000000);
#endif
#if LJ_TARGET_X86ORX64
  J->k64[LJ_K64_TOBIT].u64 = U64x(43380000,00000000);
  J->k64[LJ_K64_2P64].u64 = U64x(43f00000,00000000);
#endif
#if LJ_TARGET_MIPS64
  J->k64[LJ_K64_2P63].u64 = U64x(43e00000,00000000);
#endif
#if LJ_TARGET_MIPS
  J->k64[LJ_K64_2P31].u64 = U64x(41e00000,00000000);
#endif

#if LJ_TARGET_X86ORX64 || LJ_TARGET_MIPS64
  J->k32[LJ_K32_M2P64] = 0xdf800000;
#endif
#if LJ_TARGET_MIPS64
  J->k32[LJ_K32_2P63] = 0x5f000000;
#endif
#if LJ_TARGET_PPC
  J->k32[LJ_K32_2P52_2P31] = 0x59800004;
  J->k32[LJ_K32_2P52] = 0x59800000;
#endif
#if LJ_TARGET_PPC
  J->k32[LJ_K32_2P31] = 0x4f000000;
#endif

#if LJ_TARGET_PPC || LJ_TARGET_MIPS32
  J->k32[LJ_K32_VM_EXIT_HANDLER] = (uintptr_t)(void *)lj_vm_exit_handler;
  J->k32[LJ_K32_VM_EXIT_INTERP] = (uintptr_t)(void *)lj_vm_exit_interp;
#endif
#if LJ_TARGET_ARM64 || LJ_TARGET_MIPS64 || LJ_TARGET_S390X
  J->k64[LJ_K64_VM_EXIT_HANDLER].u64 = (uintptr_t)lj_ptr_sign((void *)lj_vm_exit_handler, 0);
  J->k64[LJ_K64_VM_EXIT_INTERP].u64 = (uintptr_t)lj_ptr_sign((void *)lj_vm_exit_interp, 0);
#endif
}

/* Free everything associated with the JIT compiler state. */
void lj_trace_freestate(global_State *g)
{
  jit_State *J = G2J(g);
#ifdef LUA_USE_ASSERT
  {  /* This assumes all traces have already been freed. */
    ptrdiff_t i;
    for (i = 1; i < (ptrdiff_t)J->sizetrace; i++)
      lj_assertG(i == (ptrdiff_t)J->cur.traceno || traceref(J, i) == NULL,
		 "trace still allocated");
  }
#endif
  lj_mcode_free(J);
  lj_mem_freevec(g, J->snapmapbuf, J->sizesnapmap, SnapEntry);
  lj_mem_freevec(g, J->snapbuf, J->sizesnap, SnapShot);
  lj_mem_freevec(g, J->irbuf + J->irbotlim, J->irtoplim - J->irbotlim, IRIns);
  lj_mem_freevec(g, J->trace, J->sizetrace, GCRef);
}

/* -- Penalties and blacklisting ------------------------------------------ */

/* Blacklist a bytecode instruction. */
static void blacklist_pc(GCproto *pt, BCIns *pc)
{
  if (bc_op(*pc) == BC_ITERN) {
    setbc_op(pc, BC_ITERC);
    setbc_op(pc+1+bc_j(pc[1]), BC_JMP);
  } else {
    setbc_op(pc, (int)bc_op(*pc)+(int)BC_ILOOP-(int)BC_LOOP);
    pt->flags |= PROTO_ILOOP;
  }
}

/* Penalize a bytecode instruction. */
static void penalty_pc(jit_State *J, GCproto *pt, BCIns *pc, TraceError e)
{
  uint32_t i, val = PENALTY_MIN;
  for (i = 0; i < PENALTY_SLOTS; i++)
    if (mref(J->penalty[i].pc, const BCIns) == pc) {  /* Cache slot found? */
      /* First try to bump its hotcount several times. */
      val = ((uint32_t)J->penalty[i].val << 1) +
	    (lj_prng_u64(&J->prng) & ((1u<<PENALTY_RNDBITS)-1));
      if (val > PENALTY_MAX) {
	blacklist_pc(pt, pc);  /* Blacklist it, if that didn't help. */
	return;
      }
      goto setpenalty;
    }
  /* Assign a new penalty cache slot. */
  i = J->penaltyslot;
  J->penaltyslot = (J->penaltyslot + 1) & (PENALTY_SLOTS-1);
  setmref(J->penalty[i].pc, pc);
setpenalty:
  J->penalty[i].val = (uint16_t)val;
  J->penalty[i].reason = e;
  hotcount_set(J2GG(J), pc+1, val);
}

/* -- Trace compiler state machine ---------------------------------------- */

/* Start tracing. */
static void trace_start(jit_State *J)
{
  TraceNo traceno;
#ifdef LUA_USE_TRACE_LOGS
  const BCIns *pc = J->pc;
#endif

  if ((J->pt->flags & PROTO_NOJIT)) {  /* JIT disabled for this proto? */
    if (J->parent == 0 && J->exitno == 0 && bc_op(*J->pc) != BC_ITERN) {
      /* Lazy bytecode patching to disable hotcount events. */
      lj_assertJ(bc_op(*J->pc) == BC_FORL || bc_op(*J->pc) == BC_ITERL ||
		 bc_op(*J->pc) == BC_LOOP || bc_op(*J->pc) == BC_FUNCF,
		 "bad hot bytecode %d", bc_op(*J->pc));
      setbc_op(J->pc, (int)bc_op(*J->pc)+(int)BC_ILOOP-(int)BC_LOOP);
      J->pt->flags |= PROTO_ILOOP;
    } else if (lj_trace_s390x_iterator_array_itern_nojit_hotcount_park_match(J)) {
      hotcount_set(J2GG(J), J->pc+1, 0xffffu);
      if (getenv("LUAJIT_S390X_TRACE_META_LOG") != NULL) {
        fprintf(stderr,
                "S390X_ITERATOR_ARRAY_ITERN_NOJIT_HOTCOUNT_PARK pc=%p op=%u val=%u firstline=%u numline=%u\n",
                (const void *)J->pc, (unsigned int)bc_op(*J->pc),
                (unsigned int)0xffff,
                (unsigned int)J->pt->firstline,
                (unsigned int)J->pt->numline);
      }
    } else if (lj_trace_s390x_iterator_hash_itern_nojit_hotcount_park_match(J)) {
      hotcount_set(J2GG(J), J->pc+1, 0xffffu);
      if (getenv("LUAJIT_S390X_TRACE_META_LOG") != NULL) {
        fprintf(stderr,
                "S390X_ITERATOR_HASH_ITERN_NOJIT_HOTCOUNT_PARK pc=%p op=%u val=%u firstline=%u numline=%u\n",
                (const void *)J->pc, (unsigned int)bc_op(*J->pc),
                (unsigned int)0xffff,
                (unsigned int)J->pt->firstline,
                (unsigned int)J->pt->numline);
      }
    } else if (lj_trace_s390x_mixed_noffi_itern_nojit_hotcount_park_match(J)) {
      hotcount_set(J2GG(J), J->pc+1, 0x7fffu);
      if (getenv("LUAJIT_S390X_TRACE_META_LOG") != NULL) {
        fprintf(stderr,
                "S390X_MIXED_NOFFI_ITERN_NOJIT_HOTCOUNT_PARK pc=%p op=%u val=%u firstline=%u numline=%u\n",
                (const void *)J->pc, (unsigned int)bc_op(*J->pc),
                (unsigned int)0x7fff,
                (unsigned int)J->pt->firstline,
                (unsigned int)J->pt->numline);
      }
    }
    J->state = LJ_TRACE_IDLE;  /* Silently ignored. */
    return;
  }

  /* Ensuring forward progress for BC_ITERN can trigger hotcount again. */
  if (!J->parent && bc_op(*J->pc) == BC_JLOOP) {  /* Already compiled. */
    J->state = LJ_TRACE_IDLE;  /* Silently ignored. */
    return;
  }

  /* Get a new trace number. */
  traceno = trace_findfree(J);
  if (LJ_UNLIKELY(traceno == 0)) {  /* No free trace? */
    lj_assertJ((J2G(J)->hookmask & HOOK_GC) == 0,
	       "recorder called from GC hook");
    lj_trace_flushall(J->L);
    J->state = LJ_TRACE_IDLE;  /* Silently ignored. */
    return;
  }
  setgcrefp(J->trace[traceno], &J->cur);

  /* Setup enough of the current trace to be able to send the vmevent. */
  memset(&J->cur, 0, sizeof(GCtrace));
  J->cur.traceno = traceno;
  J->cur.nins = J->cur.nk = REF_BASE;
  J->cur.ir = J->irbuf;
  J->cur.snap = J->snapbuf;
  J->cur.snapmap = J->snapmapbuf;
  J->mergesnap = 0;
  J->needsnap = 0;
  J->bcskip = 0;
  J->guardemit.irt = 0;
  J->postproc = LJ_POST_NONE;
  lj_resetsplit(J);
  J->retryrec = 0;
#if LJ_TARGET_S390X
  J->s390x_nil_restart_desc = 0;
#endif
  J->ktrace = 0;
  setgcref(J->cur.startpt, obj2gco(J->pt));

  lj_vmevent_send_(J2G(J), TRACE,
    TValue savetv = J2G(J)->tmptv;
    TValue savetv2 = J2G(J)->tmptv2;
    TraceNo parent = J->parent;
    ExitNo exitno = J->exitno;
    setstrV(V, V->top++, lj_str_newlit(V, "start"));
    setintV(V->top++, traceno);
    setfuncV(V, V->top++, J->fn);
    setintV(V->top++, proto_bcpos(J->pt, J->pc));
    if (J->parent) {
      setintV(V->top++, J->parent);
      setintV(V->top++, J->exitno);
    } else {
      BCOp op = bc_op(*J->pc);
      if (op == BC_CALLM || op == BC_CALL || op == BC_ITERC) {
	setintV(V->top++, J->exitno);  /* Parent of stitched trace. */
	setintV(V->top++, -1);
      }
    }
  ,
    J2G(J)->tmptv = savetv;
    J2G(J)->tmptv2 = savetv2;
    J->parent = parent;
    J->exitno = exitno;
  );
  lj_record_setup(J);
#ifdef LUA_USE_TRACE_LOGS
  lj_log_trace_start_record(J->L, (unsigned) J->cur.traceno, pc, J->fn);
#endif
}

/* Stop tracing. */
static void trace_stop(jit_State *J)
{
  BCIns *pc = mref(J->cur.startpc, BCIns);
  BCOp op = bc_op(J->cur.startins);
  GCproto *pt = &gcref(J->cur.startpt)->pt;
  TraceNo traceno = J->cur.traceno;
  GCtrace *T = J->curfinal;

#if LJ_TARGET_S390X && LJ_GC64
  if (!lj_trace_s390x_traceconsts_valid(J, T))
    lj_trace_err(J, LJ_TRERR_RETRY);
#endif

  lj_trace_s390x_dump_trace_snaps(J, T);
  switch (op) {
  case BC_FORL:
    {
      int skip_jfori = lj_trace_s390x_dispatch_forl_skip_jfori_match(J, pt);
      int park_root = lj_trace_s390x_dispatch_forl_park_root_match(J, pt);
      if (lj_trace_s390x_mixed_noffi_forl_stitch_blacklist_match(J, pt, T)) {
        blacklist_pc(pt, pc);
        if (getenv("LUAJIT_S390X_TRACE_META_LOG") != NULL) {
          fprintf(stderr,
                  "S390X_MIXED_NOFFI_FORL_STITCH_BLACKLIST trace=%u startpc=%p startop=%u link=%u linktype=%u nsnap=%u nins=%u mcloop=%u\n",
                  (unsigned int)J->cur.traceno,
                  (const void *)pc,
                  (unsigned int)bc_op(J->cur.startins),
                  (unsigned int)J->cur.link,
                  (unsigned int)J->cur.linktype,
                  (unsigned int)J->cur.nsnap,
                  (unsigned int)J->cur.nins,
                  (unsigned int)J->cur.mcloop);
        }
        goto addroot;
      }
      if (lj_trace_s390x_mixed_ffi_forl_proto_nojit_match(J, pt, T)) {
        pt->flags |= PROTO_NOJIT;
        if (getenv("LUAJIT_S390X_TRACE_META_LOG") != NULL) {
          fprintf(stderr,
                  "S390X_MIXED_FFI_FORL_PROTO_NOJIT trace=%u startpc=%p startop=%u link=%u linktype=%u nsnap=%u nins=%u mcloop=%u\n",
                  (unsigned int)J->cur.traceno,
                  (const void *)pc,
                  (unsigned int)bc_op(J->cur.startins),
                  (unsigned int)J->cur.link,
                  (unsigned int)J->cur.linktype,
                  (unsigned int)J->cur.nsnap,
                  (unsigned int)J->cur.nins,
                  (unsigned int)J->cur.mcloop);
        }
        goto addroot;
      }
      if (lj_trace_s390x_lower_frame_lua_abs_proto_nojit_match(J, pt, T)) {
        pt->flags |= PROTO_NOJIT;
        if (getenv("LUAJIT_S390X_TRACE_META_LOG") != NULL) {
          fprintf(stderr,
                  "S390X_LOWER_FRAME_LUA_ABS_PROTO_NOJIT trace=%u startpc=%p startop=%u link=%u linktype=%u nsnap=%u nins=%u mcloop=%u\n",
                  (unsigned int)J->cur.traceno,
                  (const void *)pc,
                  (unsigned int)bc_op(J->cur.startins),
                  (unsigned int)J->cur.link,
                  (unsigned int)J->cur.linktype,
                  (unsigned int)J->cur.nsnap,
                  (unsigned int)J->cur.nins,
                  (unsigned int)J->cur.mcloop);
        }
        goto addroot;
      }
      if (lj_trace_s390x_dispatch_forl_proto_nojit_match(J, pt, T)) {
        pt->flags |= PROTO_NOJIT;
        if (getenv("LUAJIT_S390X_TRACE_META_LOG") != NULL) {
          fprintf(stderr,
                  "S390X_DISPATCH_FORL_PROTO_NOJIT trace=%u startpc=%p startop=%u firstline=%u link=%u linktype=%u nsnap=%u nins=%u mcloop=%u\n",
                  (unsigned int)J->cur.traceno,
                  (const void *)pc,
                  (unsigned int)bc_op(J->cur.startins),
                  (unsigned int)pt->firstline,
                  (unsigned int)J->cur.link,
                  (unsigned int)J->cur.linktype,
                  (unsigned int)J->cur.nsnap,
                  (unsigned int)J->cur.nins,
                  (unsigned int)J->cur.mcloop);
        }
        goto addroot;
      }
      if (lj_trace_s390x_promotion_core_forl_proto_nojit_match(J, pt, T)) {
        pt->flags |= PROTO_NOJIT;
        if (getenv("LUAJIT_S390X_TRACE_META_LOG") != NULL) {
          fprintf(stderr,
                  "S390X_PROMOTION_CORE_FORL_PROTO_NOJIT trace=%u startpc=%p startop=%u firstline=%u link=%u linktype=%u nsnap=%u nins=%u mcloop=%u\n",
                  (unsigned int)J->cur.traceno,
                  (const void *)pc,
                  (unsigned int)bc_op(J->cur.startins),
                  (unsigned int)pt->firstline,
                  (unsigned int)J->cur.link,
                  (unsigned int)J->cur.linktype,
                  (unsigned int)J->cur.nsnap,
                  (unsigned int)J->cur.nins,
                  (unsigned int)J->cur.mcloop);
        }
        goto addroot;
      }
      if (lj_trace_s390x_ffi_cdata_pair_forl_blacklist_match(J, pt, T)) {
        blacklist_pc(pt, pc);
        if (getenv("LUAJIT_S390X_TRACE_META_LOG") != NULL) {
          fprintf(stderr,
                  "S390X_FFI_CDATA_PAIR_FORL_BLACKLIST trace=%u startpc=%p startop=%u link=%u linktype=%u nsnap=%u nins=%u mcloop=%u\n",
                  (unsigned int)J->cur.traceno,
                  (const void *)pc,
                  (unsigned int)bc_op(J->cur.startins),
                  (unsigned int)J->cur.link,
                  (unsigned int)J->cur.linktype,
                  (unsigned int)J->cur.nsnap,
                  (unsigned int)J->cur.nins,
                  (unsigned int)J->cur.mcloop);
        }
        goto addroot;
      }
      if (lj_trace_s390x_sum_loop_forl_blacklist_match(J, pt, T)) {
        blacklist_pc(pt, pc);
        if (getenv("LUAJIT_S390X_TRACE_META_LOG") != NULL) {
          fprintf(stderr,
                  "S390X_SUM_LOOP_FORL_BLACKLIST trace=%u startpc=%p startop=%u link=%u linktype=%u nsnap=%u nins=%u mcloop=%u\n",
                  (unsigned int)J->cur.traceno,
                  (const void *)pc,
                  (unsigned int)bc_op(J->cur.startins),
                  (unsigned int)J->cur.link,
                  (unsigned int)J->cur.linktype,
                  (unsigned int)J->cur.nsnap,
                  (unsigned int)J->cur.nins,
                  (unsigned int)J->cur.mcloop);
        }
        goto addroot;
      }
      if (lj_trace_s390x_vararg_root_blacklist_match(J, pt, T)) {
        blacklist_pc(pt, pc);
        if (getenv("LUAJIT_S390X_TRACE_META_LOG") != NULL) {
          fprintf(stderr,
                  "S390X_VARARG_ROOT_BLACKLIST trace=%u startpc=%p startop=%u firstline=%u link=%u linktype=%u nsnap=%u nins=%u mcloop=%u\n",
                  (unsigned int)J->cur.traceno,
                  (const void *)pc,
                  (unsigned int)bc_op(J->cur.startins),
                  (unsigned int)pt->firstline,
                  (unsigned int)J->cur.link,
                  (unsigned int)J->cur.linktype,
                  (unsigned int)J->cur.nsnap,
                  (unsigned int)J->cur.nins,
                  (unsigned int)J->cur.mcloop);
        }
        goto addroot;
      }
      if (lj_trace_s390x_vararg_sibling_forl_blacklist_match(J, pt, T)) {
        blacklist_pc(pt, pc);
        if (getenv("LUAJIT_S390X_TRACE_META_LOG") != NULL) {
          fprintf(stderr,
                  "S390X_VARARG_SIBLING_FORL_BLACKLIST trace=%u startpc=%p startop=%u firstline=%u link=%u linktype=%u nsnap=%u nins=%u mcloop=%u\n",
                  (unsigned int)J->cur.traceno,
                  (const void *)pc,
                  (unsigned int)bc_op(J->cur.startins),
                  (unsigned int)pt->firstline,
                  (unsigned int)J->cur.link,
                  (unsigned int)J->cur.linktype,
                  (unsigned int)J->cur.nsnap,
                  (unsigned int)J->cur.nins,
                  (unsigned int)J->cur.mcloop);
        }
        goto addroot;
      }
    if (lj_trace_s390x_start_log_enabled()) {
      fprintf(stderr,
	      "S390X_DISPATCH_FORL_TRACE trace=%u parent=%u exit=%u root=%u startpc=%p nsnap=%u nins=%u link=%u linktype=%u mcloop=%u\n",
	      (unsigned int)traceno, (unsigned int)J->parent,
	      (unsigned int)J->exitno, (unsigned int)J->cur.root,
	      (const void *)pc, (unsigned int)J->cur.nsnap,
	      (unsigned int)J->cur.nins, (unsigned int)J->cur.link,
	      (unsigned int)J->cur.linktype, (unsigned int)T->mcloop);
    }
    if (!skip_jfori) {
      setbc_op(pc+bc_j(J->cur.startins), BC_JFORI);  /* Patch FORI, too. */
    } else if (lj_trace_s390x_start_log_enabled()) {
      fprintf(stderr,
	      "S390X_DISPATCH_FORL_SKIP_JFORI trace=%u startpc=%p startop=%u nsnap=%u nins=%u\n",
	      (unsigned int)traceno, (const void *)pc,
	      (unsigned int)bc_op(J->cur.startins),
	      (unsigned int)J->cur.nsnap, (unsigned int)J->cur.nins);
    }
    if (park_root) {
      if (lj_trace_s390x_start_log_enabled()) {
	fprintf(stderr,
		"S390X_DISPATCH_FORL_PARK_ROOT trace=%u startpc=%p startop=%u nsnap=%u nins=%u\n",
		(unsigned int)traceno, (const void *)pc,
		(unsigned int)bc_op(J->cur.startins),
		(unsigned int)J->cur.nsnap, (unsigned int)J->cur.nins);
      }
      lj_trace_s390x_dispatch_forl_park_root_hotexit_exact_cooldown(J, pt, pc);
      goto addroot;
    }
    }
    /* fallthrough */
  case BC_LOOP:
  case BC_ITERL:
    if (lj_trace_s390x_iterator_root_blacklist_match(J, T)) {
      blacklist_pc(pt, pc);
      if (getenv("LUAJIT_S390X_TRACE_META_LOG") != NULL) {
        fprintf(stderr,
                "S390X_ITERATOR_ROOT_BLACKLIST trace=%u startpc=%p startop=%u link=%u linktype=%u nsnap=%u nins=%u mcloop=%u\n",
                (unsigned int)J->cur.traceno,
                (const void *)pc,
                (unsigned int)bc_op(J->cur.startins),
                (unsigned int)J->cur.link,
                (unsigned int)J->cur.linktype,
                (unsigned int)J->cur.nsnap,
                (unsigned int)J->cur.nins,
                (unsigned int)J->cur.mcloop);
      }
      goto addroot;
    }
    if (lj_trace_s390x_mixed_noffi_iterl_blacklist_match(J, pt, T)) {
      blacklist_pc(pt, pc);
      if (lj_trace_s390x_mixed_noffi_early_proto_nojit_enabled())
        pt->flags |= PROTO_NOJIT;
      if (getenv("LUAJIT_S390X_TRACE_META_LOG") != NULL) {
        fprintf(stderr,
                "S390X_MIXED_NOFFI_ITERL_BLACKLIST trace=%u startpc=%p startop=%u link=%u linktype=%u nsnap=%u nins=%u mcloop=%u proto_nojit=%u\n",
                (unsigned int)J->cur.traceno,
                (const void *)pc,
                (unsigned int)bc_op(J->cur.startins),
                (unsigned int)J->cur.link,
                (unsigned int)J->cur.linktype,
                (unsigned int)J->cur.nsnap,
                (unsigned int)J->cur.nins,
                (unsigned int)J->cur.mcloop,
                (unsigned int)lj_trace_s390x_mixed_noffi_early_proto_nojit_enabled());
      }
      goto addroot;
    }
    if (lj_trace_s390x_iterator_iterl_blacklist_match(J, pt, T)) {
      blacklist_pc(pt, pc);
      if (getenv("LUAJIT_S390X_TRACE_META_LOG") != NULL) {
        fprintf(stderr,
                "S390X_ITERATOR_ITERL_BLACKLIST trace=%u startpc=%p startop=%u link=%u linktype=%u nsnap=%u nins=%u mcloop=%u\n",
                (unsigned int)J->cur.traceno,
                (const void *)pc,
                (unsigned int)bc_op(J->cur.startins),
                (unsigned int)J->cur.link,
                (unsigned int)J->cur.linktype,
                (unsigned int)J->cur.nsnap,
                (unsigned int)J->cur.nins,
                (unsigned int)J->cur.mcloop);
      }
      goto addroot;
    }
    /* fallthrough */
  case BC_FUNCF:
    /* Patch bytecode of starting instruction in root trace. */
    setbc_op(pc, (int)op+(int)BC_JLOOP-(int)BC_LOOP);
    setbc_d(pc, traceno);
  addroot:
    /* Add to root trace chain in prototype. */
    J->cur.nextroot = pt->trace;
    pt->trace = (TraceNo1)traceno;
    break;
  case BC_ITERN:
    if (lj_trace_s390x_iterator_root_blacklist_match(J, T)) {
      blacklist_pc(pt, pc);
      if (getenv("LUAJIT_S390X_TRACE_META_LOG") != NULL) {
        fprintf(stderr,
                "S390X_ITERATOR_ROOT_BLACKLIST trace=%u startpc=%p startop=%u link=%u linktype=%u nsnap=%u nins=%u mcloop=%u\n",
                (unsigned int)J->cur.traceno,
                (const void *)pc,
                (unsigned int)bc_op(J->cur.startins),
                (unsigned int)J->cur.link,
                (unsigned int)J->cur.linktype,
                (unsigned int)J->cur.nsnap,
                (unsigned int)J->cur.nins,
                (unsigned int)J->cur.mcloop);
      }
      goto addroot;
    }
    if (lj_trace_s390x_mixed_noffi_itern_blacklist_match(J, pt, T)) {
      blacklist_pc(pt, pc);
      if (getenv("LUAJIT_S390X_TRACE_META_LOG") != NULL) {
        fprintf(stderr,
                "S390X_MIXED_NOFFI_ITERN_BLACKLIST trace=%u startpc=%p startop=%u link=%u linktype=%u nsnap=%u nins=%u mcloop=%u\n",
                (unsigned int)J->cur.traceno,
                (const void *)pc,
                (unsigned int)bc_op(J->cur.startins),
                (unsigned int)J->cur.link,
                (unsigned int)J->cur.linktype,
                (unsigned int)J->cur.nsnap,
                (unsigned int)J->cur.nins,
                (unsigned int)J->cur.mcloop);
      }
      goto addroot;
    } else if (lj_trace_s390x_iterator_itern_proto_nojit_match(J, pt, T)) {
      pt->flags |= PROTO_NOJIT;
      if (pt->firstline == 22 && pt->numline == 8 &&
          lj_trace_s390x_iterator_array_itern_nojit_hotcount_park_enabled())
        hotcount_set(J2GG(J), pc+1, 0xffffu);
      if (pt->firstline == 12 && pt->numline == 8 &&
          lj_trace_s390x_iterator_hash_itern_nojit_hotcount_park_enabled())
        hotcount_set(J2GG(J), pc+1, 0xffffu);
      lj_trace_s390x_iterator_post_proto_itern_nohot_activate(J, pt);
      if (getenv("LUAJIT_S390X_TRACE_META_LOG") != NULL) {
        fprintf(stderr,
                "S390X_ITERATOR_ITERN_PROTO_NOJIT trace=%u startpc=%p startop=%u link=%u linktype=%u nsnap=%u nins=%u mcloop=%u\n",
                (unsigned int)J->cur.traceno,
                (const void *)pc,
                (unsigned int)bc_op(J->cur.startins),
                (unsigned int)J->cur.link,
                (unsigned int)J->cur.linktype,
                (unsigned int)J->cur.nsnap,
                (unsigned int)J->cur.nins,
                (unsigned int)J->cur.mcloop);
      }
      goto addroot;
    } else if (lj_trace_s390x_iterator_itern_blacklist_match(J, pt, T)) {
      blacklist_pc(pt, pc);
      if (getenv("LUAJIT_S390X_TRACE_META_LOG") != NULL) {
        fprintf(stderr,
                "S390X_ITERATOR_ITERN_BLACKLIST trace=%u startpc=%p startop=%u link=%u linktype=%u nsnap=%u nins=%u mcloop=%u\n",
                (unsigned int)J->cur.traceno,
                (const void *)pc,
                (unsigned int)bc_op(J->cur.startins),
                (unsigned int)J->cur.link,
                (unsigned int)J->cur.linktype,
                (unsigned int)J->cur.nsnap,
                (unsigned int)J->cur.nins,
                (unsigned int)J->cur.mcloop);
      }
      goto addroot;
    }
    /* fallthrough */
  case BC_RET:
  case BC_RET0:
  case BC_RET1:
    *pc = BCINS_AD(BC_JLOOP, J->cur.snap[0].nslots, traceno);
    goto addroot;
  case BC_JMP:
    /* Patch exit branch in parent to side trace entry. */
    lj_assertJ(J->parent != 0 && J->cur.root != 0, "not a side trace");
    {
      int skip_patchexit = 0;
      int skip_match = 0;
      GCtrace *parentT = traceref(J, J->parent);
      if (J->cur.root == 1 &&
	  J->parent >= 3 &&
	  J->exitno == 0 &&
	  bc_op(J->cur.startins) == BC_JMP &&
	  J->cur.linktype == LJ_TRLINK_ROOT &&
	  J->cur.link != 0)
	skip_match = 1;
      if (lj_trace_s390x_jloop_exit_log_enabled() && skip_match) {
	fprintf(stderr,
		"S390X_BCJMP_STOP trace=%u parent=%u exit=%u root=%u startop=%u link=%u linktype=%u nins=%u parent_startop=%u parent_link=%u parent_linktype=%u skip_enabled=%u skip_match=%u\n",
		(unsigned int)traceno, (unsigned int)J->parent,
		(unsigned int)J->exitno, (unsigned int)J->cur.root,
		(unsigned int)bc_op(J->cur.startins),
		(unsigned int)J->cur.link,
		(unsigned int)J->cur.linktype,
		(unsigned int)(T ? T->nins : 0),
		(unsigned int)bc_op(parentT->startins),
		(unsigned int)parentT->link,
		(unsigned int)parentT->linktype,
		(unsigned int)lj_trace_s390x_skip_patch_bcjmp_loopdesc_enabled(),
		(unsigned int)skip_match);
      }
      if (lj_trace_s390x_skip_patch_bcjmp_loopdesc_enabled() && skip_match) {
	skip_patchexit = 1;
	if (lj_trace_s390x_jloop_exit_log_enabled()) {
	  fprintf(stderr,
		  "S390X_SKIP_PATCHEXIT trace=%u parent=%u exit=%u startop=%u link=%u linktype=%u nins=%u\n",
		  (unsigned int)traceno, (unsigned int)J->parent,
		  (unsigned int)J->exitno,
		  (unsigned int)bc_op(J->cur.startins),
		  (unsigned int)J->cur.link,
		  (unsigned int)J->cur.linktype,
		  (unsigned int)(T ? T->nins : 0));
	}
	if (lj_trace_s390x_stop_retarget_loopdesc_enabled() &&
	    pc != NULL && bc_op(*pc) == BC_JLOOP) {
	  BCIns oldins = *pc;
	  *pc = BCINS_AD(BC_JLOOP, bc_a(oldins), traceno);
	  fprintf(stderr,
		  "S390X_STOP_RETARGET trace=%u parent=%u exit=%u pc=%p oldins=0x%08x newins=0x%08x\n",
		  (unsigned int)traceno, (unsigned int)J->parent,
		  (unsigned int)J->exitno, (void *)pc,
		  (unsigned int)oldins, (unsigned int)*pc);
	}
      }
      if (!skip_patchexit) {
	MCode *target = J->cur.mcode;
	if (lj_trace_s390x_sideexit_mcloop_enabled() && T->mcloop)
	  target = J->cur.mcode + T->mcloop;
	lj_asm_patchexit(J, parentT, J->exitno, target);
      }
    }
    /* Avoid compiling a side trace twice (stack resizing uses parent exit). */
    {
      SnapShot *snap = &traceref(J, J->parent)->snap[J->exitno];
      snap->count = SNAPCOUNT_DONE;
      if (J->cur.topslot > snap->topslot) snap->topslot = J->cur.topslot;
    }
    /* Add to side trace chain in root trace. */
    {
      GCtrace *root = traceref(J, J->cur.root);
      root->nchild++;
      J->cur.nextside = root->nextside;
      root->nextside = (TraceNo1)traceno;
      if (lj_trace_s390x_root_promote_child_loop_enabled() &&
	  J->cur.root == 1 && J->parent == 1 &&
	  mref(J->cur.startpc, BCIns) == mref(root->startpc, BCIns)) {
	BCIns *rpc = mref(root->startpc, BCIns);
	fprintf(stderr,
		"S390X_ROOT_PROMOTE_CHILD_CAND trace=%u root=%u parent=%u exit=%u pc=%p rootpc=%p rpcop=%u startop=%u link=%u linktype=%u oldtarget=%u resumechild=%u\n",
		(unsigned int)traceno, (unsigned int)root->traceno,
		(unsigned int)J->parent, (unsigned int)J->exitno,
		(void *)mref(J->cur.startpc, BCIns), (void *)rpc,
		(unsigned int)(rpc ? bc_op(*rpc) : 0),
		(unsigned int)bc_op(J->cur.startins),
		(unsigned int)J->cur.link,
		(unsigned int)J->cur.linktype,
		(unsigned int)(rpc ? bc_d(*rpc) : 0),
		(unsigned int)root->resumechild);
	if (rpc != NULL && bc_op(*rpc) == BC_JLOOP &&
	    bc_d(*rpc) == root->traceno &&
	    ((J->exitno == 4 && J->cur.linktype == LJ_TRLINK_ROOT) ||
	     (J->exitno == 1 && J->cur.linktype == LJ_TRLINK_LOOP))) {
	  TraceNo prev_resumechild = root->resumechild;
	  fprintf(stderr,
		  "S390X_ROOT_PROMOTE_CHILD trace=%u root=%u pc=%p oldtarget=%u newtarget=%u exit=%u startop=%u linktype=%u\n",
		  (unsigned int)traceno, (unsigned int)root->traceno,
		  (void *)rpc, (unsigned int)bc_d(*rpc),
		  (unsigned int)traceno,
		  (unsigned int)J->exitno,
		  (unsigned int)bc_op(J->cur.startins),
		  (unsigned int)J->cur.linktype);
	  if (J->exitno == 4 && J->cur.linktype == LJ_TRLINK_ROOT) {
	    root->resumechild = (TraceNo1)traceno;
	    root->link = (TraceNo1)traceno;
	    root->linktype = LJ_TRLINK_ROOT;
	    fprintf(stderr,
		    "S390X_ROOT_LINK_CHILD root=%u newlink=%u newlinktype=%u\n",
		    (unsigned int)root->traceno,
		    (unsigned int)root->link,
		    (unsigned int)root->linktype);
	  } else if (prev_resumechild == 0) {
	    root->resumechild = (TraceNo1)traceno;
	  } else if (J->exitno == 1 && J->cur.linktype == LJ_TRLINK_LOOP &&
		     prev_resumechild != 0) {
	    int parent_is_resumechild = (J->parent == prev_resumechild);
	    int parent_is_root = (J->parent == root->traceno);
	    fprintf(stderr,
		    "S390X_CHILD_LINK_DECIDE owner=%u trace=%u actual_parent=%u root=%u exit=%u linktype=%u parent_is_resumechild=%u parent_is_root=%u\n",
		    (unsigned int)prev_resumechild,
		    (unsigned int)traceno,
		    (unsigned int)J->parent,
		    (unsigned int)root->traceno,
		    (unsigned int)J->exitno,
		    (unsigned int)J->cur.linktype,
		    (unsigned int)parent_is_resumechild,
		    (unsigned int)parent_is_root);
	    if (parent_is_resumechild || parent_is_root) {
	      GCtrace *owner = traceref(J, prev_resumechild);
	      owner->link = (TraceNo1)traceno;
	      owner->linktype = LJ_TRLINK_LOOP;
	      owner->resumechild = (TraceNo1)traceno;
	      fprintf(stderr,
		      "S390X_CHILD_LINK_CHILD owner=%u newlink=%u newlinktype=%u newresumechild=%u root=%u actual_parent=%u\n",
		      (unsigned int)owner->traceno,
		      (unsigned int)owner->link,
		      (unsigned int)owner->linktype,
		      (unsigned int)owner->resumechild,
		      (unsigned int)root->traceno,
		      (unsigned int)J->parent);
	    } else {
	      fprintf(stderr,
		      "S390X_CHILD_LINK_SKIP owner=%u trace=%u actual_parent=%u exit=%u root=%u\n",
		      (unsigned int)prev_resumechild,
		      (unsigned int)traceno,
		      (unsigned int)J->parent,
		      (unsigned int)J->exitno,
		      (unsigned int)root->traceno);
	    }
	  }
		}
      }
    }
    break;
  case BC_CALLM:
  case BC_CALL:
  case BC_ITERC:
    if (lj_trace_s390x_stitch_focus_enabled()) {
      GCtrace *prev = traceref(J, J->exitno);
      fprintf(stderr,
	      "S390X_STITCH_FOCUS phase=stop traceno=%u prev=%u prev_link=%u prev_linktype=%u op=%u pc=%p startpc=%p\n",
	      (unsigned int)traceno, (unsigned int)J->exitno,
	      (unsigned int)(prev ? prev->link : 0),
	      (unsigned int)(prev ? prev->linktype : 0),
	      (unsigned int)op, (const void *)pc,
	      (const void *)mref(J->cur.startpc, BCIns));
    }
    /* Trace stitching: patch link of previous trace. */
    traceref(J, J->exitno)->link = traceno;
    break;
  default:
    lj_assertJ(0, "bad stop bytecode %d", op);
    break;
  }

  if (traceno == 102 && J->parent == 101 && J->exitno == 0)
    lj_trace_s390x_mixed_ffi_post_stitch_save_done(J, pt);
  if (traceno == 102 && J->parent == 101 && J->exitno == 0)
    lj_trace_s390x_ffi_cdata_pair_save_done(J, pt);
  lj_trace_s390x_log_trace_meta(J, &J->cur, "stop");

  /* Commit new mcode only after all patching is done. */
  lj_mcode_commit(J, J->cur.mcode);
  J->postproc = LJ_POST_NONE;
  trace_save(J, T);

  lj_vmevent_send(J2G(J), TRACE,
    setstrV(V, V->top++, lj_str_newlit(V, "stop"));
    setintV(V->top++, traceno);
    setfuncV(V, V->top++, J->fn);
  );
}

/* Start a new root trace for down-recursion. */
static int trace_downrec(jit_State *J)
{
  /* Restart recording at the return instruction. */
  lj_assertJ(J->pt != NULL, "no active prototype");
  lj_assertJ(bc_isret(bc_op(*J->pc)), "not at a return bytecode");
  if (bc_op(*J->pc) == BC_RETM)
    return 0;  /* NYI: down-recursion with RETM. */
  J->parent = 0;
  J->exitno = 0;
  J->state = LJ_TRACE_RECORD;
  trace_start(J);
  return 1;
}

/* Abort tracing. */
static int trace_abort(jit_State *J)
{
  lua_State *L = J->L;
  TraceError e = LJ_TRERR_RECERR;
  TraceNo traceno;

  J->postproc = LJ_POST_NONE;
  lj_mcode_abort(J);
  if (J->curfinal) {
    lj_trace_free(J2G(J), J->curfinal);
    J->curfinal = NULL;
  }
  if (tvisnumber(L->top-1))
    e = (TraceError)numberVint(L->top-1);
  lj_trace_s390x_abort_log(J, e);
  if (e == LJ_TRERR_MCODELM) {
    L->top--;  /* Remove error object */
    J->state = LJ_TRACE_ASM;
    return 1;  /* Retry ASM with new MCode area. */
  }
  /* Penalize or blacklist starting bytecode instruction. */
  if (J->parent == 0 && !bc_isret(bc_op(J->cur.startins))) {
    if (J->exitno == 0) {
      BCIns *startpc = mref(J->cur.startpc, BCIns);
      GCproto *pt = &gcref(J->cur.startpt)->pt;
      if (e == LJ_TRERR_RETRY)
	hotcount_set(J2GG(J), startpc+1, 1);  /* Immediate retry. */
      else if (lj_trace_s390x_mixed_noffi_iterl_abort_blacklist_match(J, pt, e)) {
	blacklist_pc(pt, startpc);
	lj_trace_s390x_mixed_noffi_iterl_abort_blacklist_log(J, startpc, e);
      }
      else
	penalty_pc(J, pt, startpc, e);
    } else {
      traceref(J, J->exitno)->link = J->exitno;  /* Self-link is blacklisted. */
    }
  } else if (!lj_trace_s390x_sidetrace_typeins_done_disabled() &&
	     J->parent != 0 &&
	     e == LJ_TRERR_TYPEINS &&
	     J->exitno == 1 &&
	     bc_op(J->cur.startins) == BC_ITERN) {
    traceref(J, J->parent)->snap[J->exitno].count = SNAPCOUNT_DONE;
  }

  /* Is there anything to abort? */
  traceno = J->cur.traceno;
  if (traceno) {
    J->cur.link = 0;
    J->cur.linktype = LJ_TRLINK_NONE;
    lj_vmevent_send(J2G(J), TRACE,
      cTValue *bot = tvref(L->stack)+LJ_FR2;
      cTValue *frame;
      const BCIns *pc;
      BCPos pos = 0;
      setstrV(V, V->top++, lj_str_newlit(V, "abort"));
      setintV(V->top++, traceno);
      /* Find original Lua function call to generate a better error message. */
      for (frame = L->base-1, pc = J->pc; ; frame = frame_prev(frame)) {
	if (isluafunc(frame_func(frame))) {
	  pos = proto_bcpos(funcproto(frame_func(frame)), pc);
	  break;
	} else if (frame_prev(frame) <= bot) {
	  break;
	} else if (frame_iscont(frame)) {
	  pc = frame_contpc(frame) - 1;
	} else {
	  pc = frame_pc(frame) - 1;
	}
      }
      setfuncV(V, V->top++, frame_func(frame));
      setintV(V->top++, pos);
      copyTV(V, V->top++, L->top-1);
      copyTV(V, V->top++, &J->errinfo);
    );
    /* Drop aborted trace after the vmevent (which may still access it). */
    setgcrefnull(J->trace[traceno]);
    if (traceno < J->freetrace)
      J->freetrace = traceno;
    J->cur.traceno = 0;
  }
  L->top--;  /* Remove error object */
  if (e == LJ_TRERR_DOWNREC) {
    return trace_downrec(J);
  } else if (e == LJ_TRERR_MCODEAL) {
    if (!J->mcarea) {  /* Disable JIT compiler if first mcode alloc fails. */
      J->flags &= ~JIT_F_ON;
      lj_dispatch_update(J2G(J));
    }
    lj_trace_flushall(L);
  }
  return 0;
}

/* Perform pending re-patch of a bytecode instruction. */
static LJ_AINLINE void trace_pendpatch(jit_State *J, int force)
{
  if (LJ_UNLIKELY(J->patchpc)) {
    if (force || J->bcskip == 0) {
      *J->patchpc = J->patchins;
      J->patchpc = NULL;
    } else {
      J->bcskip = 0;
    }
  }
}

/* State machine for the trace compiler. Protected callback. */
static TValue *trace_state(lua_State *L, lua_CFunction dummy, void *ud)
{
  jit_State *J = (jit_State *)ud;
  UNUSED(dummy);
  do {
  retry:
    switch (J->state) {
    case LJ_TRACE_START:
      J->state = LJ_TRACE_RECORD;  /* trace_start() may change state. */
      trace_start(J);
      lj_dispatch_update(J2G(J));
      if (J->state != LJ_TRACE_RECORD_1ST)
	break;
      /* fallthrough */

    case LJ_TRACE_RECORD_1ST:
      J->state = LJ_TRACE_RECORD;
      /* fallthrough */
    case LJ_TRACE_RECORD:
      trace_pendpatch(J, 0);
      setvmstate(J2G(J), RECORD);
      lj_vmevent_send_(J2G(J), RECORD,
	/* Save/restore state for trace recorder. */
	TValue savetv = J2G(J)->tmptv;
	TValue savetv2 = J2G(J)->tmptv2;
	TraceNo parent = J->parent;
	ExitNo exitno = J->exitno;
	setintV(V->top++, J->cur.traceno);
	setfuncV(V, V->top++, J->fn);
	setintV(V->top++, J->pt ? (int32_t)proto_bcpos(J->pt, J->pc) : -1);
	setintV(V->top++, J->framedepth);
      ,
	J2G(J)->tmptv = savetv;
	J2G(J)->tmptv2 = savetv2;
	J->parent = parent;
	J->exitno = exitno;
      );
      lj_record_ins(J);
      break;

    case LJ_TRACE_END:
      trace_pendpatch(J, 1);
      J->loopref = 0;
      if ((J->flags & JIT_F_OPT_LOOP) &&
	  J->cur.link == J->cur.traceno && J->framedepth + J->retdepth == 0) {
	setvmstate(J2G(J), OPT);
	lj_opt_dce(J);
	if (lj_opt_loop(J)) {  /* Loop optimization failed? */
	  J->cur.link = 0;
	  J->cur.linktype = LJ_TRLINK_NONE;
	  J->loopref = J->cur.nins;
	  J->state = LJ_TRACE_RECORD;  /* Try to continue recording. */
	  break;
	}
	J->loopref = J->chain[IR_LOOP];  /* Needed by assembler. */
      }
      lj_opt_split(J);
      lj_opt_sink(J);
      if (!J->loopref) J->cur.snap[J->cur.nsnap-1].count = SNAPCOUNT_DONE;
      J->state = LJ_TRACE_ASM;
      break;

    case LJ_TRACE_ASM:
      setvmstate(J2G(J), ASM);
      lj_asm_trace(J, &J->cur);
      trace_stop(J);
      setvmstate(J2G(J), INTERP);
      J->state = LJ_TRACE_IDLE;
      lj_dispatch_update(J2G(J));
      return NULL;

    default:  /* Trace aborted asynchronously. */
      setintV(L->top++, (int32_t)LJ_TRERR_RECERR);
      /* fallthrough */
    case LJ_TRACE_ERR:
      trace_pendpatch(J, 1);
      if (trace_abort(J))
	goto retry;
      setvmstate(J2G(J), INTERP);
      J->state = LJ_TRACE_IDLE;
      lj_dispatch_update(J2G(J));
      return NULL;
    }
  } while (J->state > LJ_TRACE_RECORD);
  return NULL;
}

/* -- Event handling ------------------------------------------------------ */

/* A bytecode instruction is about to be executed. Record it. */
void lj_trace_ins(jit_State *J, const BCIns *pc)
{
  /* Note: J->L must already be set. pc is the true bytecode PC here. */
  J->pc = pc;
  J->fn = curr_func(J->L);
  J->pt = isluafunc(J->fn) ? funcproto(J->fn) : NULL;
  while (lj_vm_cpcall(J->L, NULL, (void *)J, trace_state) != 0)
    J->state = LJ_TRACE_ERR;
}

/* A hotcount triggered. Start recording a root trace. */
void LJ_FASTCALL lj_trace_hot(jit_State *J, const BCIns *pc)
{
  /* Note: pc is the interpreter bytecode PC here. It's offset by 1. */
  ERRNO_SAVE
  /* Reset hotcount. */
  hotcount_set(J2GG(J), pc, J->param[JIT_P_hotloop]*HOTCOUNT_LOOP);
  /* Only start a new trace if not recording or inside __gc call or vmevent. */
  if (J->state == LJ_TRACE_IDLE &&
      !(J2G(J)->hookmask & (HOOK_GC|HOOK_VMEVENT))) {
    J->parent = 0;  /* Root trace. */
    J->exitno = 0;
    J->state = LJ_TRACE_START;
    lj_trace_s390x_start_log(J, pc-1);
    lj_trace_ins(J, pc-1);
  }
  ERRNO_RESTORE
}

/* Check for a hot side exit. If yes, start recording a side trace. */
static void trace_hotside(jit_State *J, const BCIns *pc)
{
  GCtrace *T = traceref(J, J->parent);
  SnapShot *snap = &T->snap[J->exitno];
  int scoped_hotside_ok = lj_trace_s390x_hotside_uget_looproot_match(J, pc, T,
								       J->exitno,
								       snap);
  int localized_hotside_ok = lj_trace_s390x_hotside_localized_active_enabled() ?
    lj_trace_s390x_hotside_localized_equiv_match(J, pc, T, J->exitno, snap) :
    0;
  int allow_general_hotside = (scoped_hotside_ok ||
			       localized_hotside_ok ||
			       lj_trace_s390x_hotside_manual_equiv_enabled());
  if (lj_trace_s390x_hotside_focus_enabled() &&
      (lj_trace_s390x_hotside_focus_parent() < 0 ||
       J->parent == (TraceNo)lj_trace_s390x_hotside_focus_parent()) &&
      (lj_trace_s390x_hotside_focus_exit() < 0 ||
       J->exitno == (ExitNo)lj_trace_s390x_hotside_focus_exit())) {
    TraceNo rootno = T->root ? T->root : T->traceno;
    TraceNo candno = lj_trace_s390x_hotside_find_equiv(J, T, J->exitno);
    TraceNo childno = candno ? lj_trace_s390x_hotside_find_child(J, rootno, candno, J->exitno) : 0;
    if (candno) {
      fprintf(stderr,
	      "S390X_HOTSIDE_FOCUS phase=equiv parent=%u exit=%u cand=%u child=%u root=%u pc=%p op=%u snapcount=%u hotexit=%u\n",
	      (unsigned int)J->parent, (unsigned int)J->exitno,
	      (unsigned int)candno, (unsigned int)childno, (unsigned int)rootno,
	      (const void *)pc, (unsigned int)(pc ? bc_op(*pc) : 0),
	      (unsigned int)snap->count, (unsigned int)J->param[JIT_P_hotexit]);
      if (snap->count == 0) {
	lj_trace_s390x_dump_snapmap(stderr, "S390X_SNAPMAP parent", T, J->exitno);
	lj_trace_s390x_dump_snapmap(stderr, "S390X_SNAPMAP cand",
				    traceref(J, candno), J->exitno);
	if (childno)
	  lj_trace_s390x_dump_snapmap(stderr, "S390X_SNAPMAP child",
				      traceref(J, childno), J->exitno);
      }
    }
  }
  if (allow_general_hotside &&
      lj_trace_s390x_hotside_try_canon(J, pc, &T, J->exitno, &snap)) {
    if (lj_trace_s390x_hotside_focus_enabled() &&
	(lj_trace_s390x_hotside_focus_parent() < 0 ||
	 J->parent == (TraceNo)lj_trace_s390x_hotside_focus_parent()) &&
	(lj_trace_s390x_hotside_focus_exit() < 0 ||
	 J->exitno == (ExitNo)lj_trace_s390x_hotside_focus_exit())) {
      fprintf(stderr,
	      "S390X_HOTSIDE_FOCUS phase=canon-applied parent=%u exit=%u pc=%p op=%u snapcount=%u hotexit=%u\n",
	      (unsigned int)J->parent, (unsigned int)J->exitno,
	      (const void *)pc, (unsigned int)(pc ? bc_op(*pc) : 0),
	      (unsigned int)snap->count, (unsigned int)J->param[JIT_P_hotexit]);
    }
  }
  if (lj_trace_s390x_hotside_focus_enabled() &&
      (lj_trace_s390x_hotside_focus_parent() < 0 ||
       J->parent == (TraceNo)lj_trace_s390x_hotside_focus_parent()) &&
      (lj_trace_s390x_hotside_focus_exit() < 0 ||
       J->exitno == (ExitNo)lj_trace_s390x_hotside_focus_exit())) {
    GCtrace *T = traceref(J, J->parent);
    SnapEntry *map = &T->snapmap[snap->mapofs];
    const BCIns *snappc = snap_pc(&map[snap->nent]);
    MSize nextcount = snap->count + 1;
    fprintf(stderr,
	    "S390X_HOTSIDE_FOCUS phase=before parent=%u exit=%u root=%u linktype=%u link=%u pc=%p op=%u snappc=%p snapop=%u snapcount=%u nextcount=%u hotexit=%u nsnap=%u nchild=%u startop=%u state=%u\n",
	    (unsigned int)J->parent, (unsigned int)J->exitno,
	    (unsigned int)(T->root ? T->root : T->traceno),
	    (unsigned int)T->linktype, (unsigned int)T->link,
	    (const void *)pc, (unsigned int)(pc ? bc_op(*pc) : 0),
	    (const void *)snappc, (unsigned int)(snappc ? bc_op(*snappc) : 0),
	    (unsigned int)snap->count, (unsigned int)nextcount,
	    (unsigned int)J->param[JIT_P_hotexit], (unsigned int)T->nsnap,
	    (unsigned int)T->nchild, (unsigned int)bc_op(T->startins),
	    (unsigned int)J->state);
  }
  if (lj_trace_s390x_start_log_enabled()) {
    int hook_blocked = (J2G(J)->hookmask & (HOOK_GC|HOOK_VMEVENT)) != 0;
    int lua_ok = isluafunc(curr_func(J->L));
    MSize nextcount = snap->count + 1;
    fprintf(stderr,
	    "S390X_HOTSIDE parent=%u exit=%u pc=%p op=%u snapcount=%u nextcount=%u hotexit=%u done=%u hook_blocked=%d lua_ok=%d state=%u\n",
	    (unsigned int)J->parent, (unsigned int)J->exitno,
	    (const void *)pc, (unsigned int)(pc ? bc_op(*pc) : 0),
	    (unsigned int)snap->count, (unsigned int)nextcount,
	    (unsigned int)J->param[JIT_P_hotexit],
	    (unsigned int)(snap->count == SNAPCOUNT_DONE),
	    hook_blocked, lua_ok, (unsigned int)J->state);
  }
  lj_trace_s390x_exit_log("hotside", J, pc, snap->count, NULL);
  if (allow_general_hotside)
    lj_trace_s390x_hotside_share_equiv(J, pc, T, J->exitno, snap);
  lj_trace_s390x_hotside_prime_interp(J, pc, T, J->exitno, snap);
  if (!(J2G(J)->hookmask & (HOOK_GC|HOOK_VMEVENT)) &&
      isluafunc(curr_func(J->L)) &&
      snap->count != SNAPCOUNT_DONE &&
      ++snap->count >= J->param[JIT_P_hotexit]) {
    lj_assertJ(J->state == LJ_TRACE_IDLE, "hot side exit while recording");
    if (lj_trace_s390x_hotside_focus_enabled() &&
	(lj_trace_s390x_hotside_focus_parent() < 0 ||
	 J->parent == (TraceNo)lj_trace_s390x_hotside_focus_parent()) &&
	(lj_trace_s390x_hotside_focus_exit() < 0 ||
	 J->exitno == (ExitNo)lj_trace_s390x_hotside_focus_exit())) {
      GCtrace *T = traceref(J, J->parent);
      TraceNo rootno = T->root ? T->root : T->traceno;
      TraceNo candno = lj_trace_s390x_hotside_find_equiv(J, T, J->exitno);
      TraceNo childno = candno ? lj_trace_s390x_hotside_find_child(J, rootno, candno, J->exitno) : 0;
      SnapEntry *map = &T->snapmap[snap->mapofs];
      const BCIns *snappc = snap_pc(&map[snap->nent]);
      fprintf(stderr,
	      "S390X_HOTSIDE_FOCUS phase=start parent=%u exit=%u root=%u linktype=%u link=%u pc=%p op=%u snappc=%p snapop=%u snapcount=%u hotexit=%u nsnap=%u nchild=%u startop=%u state=%u\n",
	      (unsigned int)J->parent, (unsigned int)J->exitno,
	      (unsigned int)rootno,
	      (unsigned int)T->linktype, (unsigned int)T->link,
	      (const void *)pc, (unsigned int)(pc ? bc_op(*pc) : 0),
	      (const void *)snappc, (unsigned int)(snappc ? bc_op(*snappc) : 0),
	      (unsigned int)snap->count, (unsigned int)J->param[JIT_P_hotexit],
	      (unsigned int)T->nsnap, (unsigned int)T->nchild,
	      (unsigned int)bc_op(T->startins), (unsigned int)J->state);
      lj_trace_s390x_hotside_state_log(J, pc, T, snap, candno, childno);
      lj_trace_s390x_slot_log(J->L, pc);
    }
    /* J->parent is non-zero for a side trace. */
    J->state = LJ_TRACE_START;
    lj_trace_ins(J, pc);
  }
}

/* Stitch a new trace to the previous trace. */
void LJ_FASTCALL lj_trace_stitch(jit_State *J, const BCIns *pc)
{
  if (lj_trace_s390x_stitch_focus_enabled()) {
    BCOp prevop = pc > proto_bc(curr_proto(J->L)) ? bc_op(pc[-1]) : BC__MAX;
    fprintf(stderr,
	    "S390X_STITCH_FOCUS phase=enter state=%u exitno=%u parent=%u pc=%p op=%u prevop=%u fnisluafunc=%d hookmask=0x%x\n",
	    (unsigned int)J->state, (unsigned int)J->exitno,
	    (unsigned int)J->parent, (const void *)pc,
	    (unsigned int)(pc ? bc_op(*pc) : 0), (unsigned int)prevop,
	    isluafunc(curr_func(J->L)), (unsigned int)J2G(J)->hookmask);
  }
  /* Only start a new trace if not recording or inside __gc call or vmevent. */
  if (J->state == LJ_TRACE_IDLE &&
      !(J2G(J)->hookmask & (HOOK_GC|HOOK_VMEVENT))) {
    J->parent = 0;  /* Have to treat it like a root trace. */
    /* J->exitno is set to the invoking trace. */
    if (lj_trace_s390x_stitch_focus_enabled()) {
      fprintf(stderr,
	      "S390X_STITCH_FOCUS phase=start exitno=%u pc=%p op=%u\n",
	      (unsigned int)J->exitno, (const void *)pc,
	      (unsigned int)(pc ? bc_op(*pc) : 0));
    }
    J->state = LJ_TRACE_START;
    lj_trace_ins(J, pc);
  }
}


/* Tiny struct to pass data to protected call. */
typedef struct ExitDataCP {
  jit_State *J;
  void *exptr;		/* Pointer to exit state. */
  const BCIns *pc;	/* Restart interpreter at this PC. */
} ExitDataCP;

/* Need to protect lj_snap_restore because it may throw. */
static TValue *trace_exit_cp(lua_State *L, lua_CFunction dummy, void *ud)
{
  ExitDataCP *exd = (ExitDataCP *)ud;
  /* Always catch error here and don't call error function. */
  cframe_errfunc(L->cframe) = 0;
  cframe_nres(L->cframe) = -2*LUAI_MAXSTACK*(int)sizeof(TValue);
  exd->pc = lj_snap_restore(exd->J, exd->exptr);
  UNUSED(dummy);
  return NULL;
}

#ifndef LUAJIT_DISABLE_VMEVENT
/* Push all registers from exit state. */
static void trace_exit_regs(lua_State *V, ExitState *ex)
{
  int32_t i;
  setintV(V->top++, RID_NUM_GPR);
  setintV(V->top++, RID_NUM_FPR);
  for (i = 0; i < RID_NUM_GPR; i++) {
    if (sizeof(ex->gpr[i]) == sizeof(int32_t))
      setintV(V->top++, (int32_t)ex->gpr[i]);
    else
      setnumV(V->top++, (lua_Number)ex->gpr[i]);
  }
#if !LJ_SOFTFP
  for (i = 0; i < RID_NUM_FPR; i++) {
    setnumV(V->top, ex->fpr[i]);
    if (LJ_UNLIKELY(tvisnan(V->top)))
      setnanV(V->top);
    V->top++;
  }
#endif
}
#endif

#if defined(EXITSTATE_PCREG) || (LJ_UNWIND_JIT && !EXITTRACE_VMSTATE)
/* Determine trace number from pc of exit instruction. */
static TraceNo trace_exit_find(jit_State *J, MCode *pc)
{
  TraceNo traceno;
  for (traceno = 1; traceno < J->sizetrace; traceno++) {
    GCtrace *T = traceref(J, traceno);
    if (T && pc >= T->mcode && pc < (MCode *)((char *)T->mcode + T->szmcode))
      return traceno;
  }
  lj_assertJ(0, "bad exit pc");
  return 0;
}
#endif

/* A trace exited. Restore interpreter state. */
int LJ_FASTCALL lj_trace_exit(jit_State *J, void *exptr)
{
  ERRNO_SAVE
  lua_State *L = J->L;
  ExitState *ex = (ExitState *)exptr;
  ExitDataCP exd;
  int errcode, exitcode = J->exitcode;
  TValue exiterr;
  const BCIns *pc, *retpc;
  void *cf;
  GCtrace *T;

  setnilV(&exiterr);
  if (exitcode) {  /* Trace unwound with error code. */
    J->exitcode = 0;
    copyTV(L, &exiterr, L->top-1);
  }

#ifdef EXITSTATE_PCREG
  J->parent = trace_exit_find(J, (MCode *)(intptr_t)ex->gpr[EXITSTATE_PCREG]);
#else
  UNUSED(ex);
#endif
  T = traceref(J, J->parent); UNUSED(T);
#ifdef EXITSTATE_CHECKEXIT
  if (J->exitno == T->nsnap) {  /* Treat stack check like a parent exit. */
    lj_assertJ(T->root != 0, "stack check in root trace");
    J->exitno = T->ir[REF_BASE].op2;
    J->parent = T->ir[REF_BASE].op1;
    T = traceref(J, J->parent);
  }
#endif
  lj_assertJ(T != NULL && J->exitno < T->nsnap, "bad trace or exit number");
  exd.J = J;
  exd.exptr = exptr;
  errcode = lj_vm_cpcall(L, NULL, &exd, trace_exit_cp);
  if (errcode)
    return -errcode;  /* Return negated error code. */

  if (exitcode) copyTV(L, L->top++, &exiterr);  /* Anchor the error object. */

  if (!(LJ_HASPROFILE && (G(L)->hookmask & HOOK_PROFILE)))
    lj_vmevent_send(G(L), TEXIT,
      lj_state_checkstack(V, 4+RID_NUM_GPR+RID_NUM_FPR+LUA_MINSTACK);
      setintV(V->top++, J->parent);
      setintV(V->top++, J->exitno);
      trace_exit_regs(V, ex);
    );

  pc = exd.pc;
  lj_trace_s390x_exit_log("exit", J, pc,
			  traceref(J, J->parent)->snap[J->exitno].count, ex);
  lj_trace_s390x_slot_log(L, pc);
  cf = cframe_raw(L->cframe);
  setcframe_pc(cf, pc);
  if (exitcode) {
    return -exitcode;
  } else if (LJ_HASPROFILE && (G(L)->hookmask & HOOK_PROFILE)) {
    /* Just exit to interpreter. */
  } else if (G(L)->gc.state == GCSatomic || G(L)->gc.state == GCSfinalize) {
    if (!(G(L)->hookmask & HOOK_GC))
      lj_gc_step(L);  /* Exited because of GC: drive GC forward. */
  } else if ((J->flags & JIT_F_ON)) {
    trace_hotside(J, pc);
  }
#ifdef LUA_USE_TRACE_LOGS
  lj_log_trace_normal_exit(L, (int) T->traceno, pc);
#endif
  /* Return MULTRES or 0 or -17. */
  ERRNO_RESTORE
  switch (bc_op(*pc)) {
  case BC_CALLM: case BC_CALLMT:
    return (int)((BCReg)(L->top - L->base) - bc_a(*pc) - bc_c(*pc) - LJ_FR2);
  case BC_RETM:
    return (int)((BCReg)(L->top - L->base) + 1 - bc_a(*pc) - bc_d(*pc));
  case BC_TSETM:
    return (int)((BCReg)(L->top - L->base) + 1 - bc_a(*pc));
  case BC_JLOOP:
    {
      GCtrace *targetT = traceref(J, bc_d(*pc));
      GCtrace *execT = NULL;
      TraceNo execno = lj_trace_s390x_runtime_owner_trace(targetT);
      BCOp retop;
      const BCIns *resume_bcpc = mref(targetT->resumepc, const BCIns);
      int use_resume_contract = 0;
      retpc = &targetT->startins;
      retop = bc_op(*retpc);
      if (execno != 0 && execno != targetT->traceno)
	execT = traceref(J, execno);
      if (getenv("LUAJIT_S390X_JLOOP_CURRENT_SELF_RESUME") != NULL &&
	  J->parent >= 3 && J->exitno == 0 &&
	  T->root != 0 &&
	  execno == T->traceno &&
	  !lj_trace_s390x_is_loopdesc_bridge_stub(targetT) &&
	  bc_op(T->startins) == BC_JMP &&
	  T->resumevalid &&
	  bc_op(T->resumeins) == BC_JLOOP &&
	  mref(T->resumepc, const BCIns) != NULL) {
	retpc = &T->resumeins;
	retop = bc_op(T->resumeins);
	use_resume_contract = 1;
      }
      if (!use_resume_contract &&
	  retop == BC_ITERN && targetT->root == 0 && targetT->resumevalid) {
	retpc = &targetT->resumeins;
	retop = bc_op(targetT->resumeins);
	use_resume_contract = 1;
	if (lj_trace_s390x_root_promote_child_loop_enabled() &&
	    targetT->resumechild != 0) {
	  GCtrace *childT = traceref(J, targetT->resumechild);
	  if (J->state == LJ_TRACE_IDLE &&
	      lj_trace_s390x_root_jloop_child_enabled()) {
	    if (lj_trace_s390x_jloop_exit_log_enabled() &&
		lj_trace_s390x_jloop_exit_focus_match(J)) {
	      fprintf(stderr,
		      "S390X_JLOOP_EXIT phase=resume-child-jloop parent=%u exit=%u trace=%u child=%u state=%u\n",
		      (unsigned int)J->parent, (unsigned int)J->exitno,
		      (unsigned int)T->traceno,
		      (unsigned int)targetT->resumechild,
		      (unsigned int)J->state);
	    }
	    J->patchins = *pc;
	    J->patchpc = (BCIns *)pc;
	    *J->patchpc = BCINS_AD(BC_JLOOP, bc_a(*pc), targetT->resumechild);
	    J->bcskip = 1;
	    return -17;
	  }
	  if (lj_trace_s390x_jloop_exit_log_enabled() &&
	      lj_trace_s390x_jloop_exit_focus_match(J)) {
	    fprintf(stderr,
		    "S390X_JLOOP_EXIT phase=resume-child parent=%u exit=%u trace=%u child=%u child_startpc=%p child_startop=%u child_link=%u child_linktype=%u state=%u\n",
		    (unsigned int)J->parent, (unsigned int)J->exitno,
		    (unsigned int)T->traceno,
		    (unsigned int)targetT->resumechild,
		    (const void *)mref(childT->startpc, BCIns),
		    (unsigned int)bc_op(childT->startins),
		    (unsigned int)childT->link,
		    (unsigned int)childT->linktype,
		    (unsigned int)J->state);
	  }
	}
      } else if (!use_resume_contract &&
		 retop == BC_ITERN && targetT->root == 0 && targetT->unused1 != 0) {
	retop = (BCOp)targetT->unused1;
      } else if (!use_resume_contract &&
		 targetT->root != 0 &&
		 bc_op(targetT->startins) == BC_JMP &&
		 targetT->resumevalid &&
		 resume_bcpc != NULL &&
		 execno == targetT->traceno) {
	retpc = &targetT->resumeins;
	retop = bc_op(targetT->resumeins);
	use_resume_contract = 1;
      }
    if (!use_resume_contract &&
	lj_trace_s390x_jloop_exec_resume_enabled() &&
	execT != NULL &&
	J->parent >= 3 &&
	J->exitno == 0 &&
	!lj_trace_s390x_is_loopdesc_bridge_stub(targetT) &&
	targetT->root != 0 &&
	bc_op(targetT->startins) == BC_JMP &&
	execT->root != 0 &&
	  bc_op(execT->startins) == BC_JMP &&
	  execT->resumevalid &&
	  bc_op(execT->resumeins) == BC_JLOOP &&
	  mref(execT->resumepc, const BCIns) != NULL) {
	resume_bcpc = mref(execT->resumepc, const BCIns);
	retpc = &execT->resumeins;
	retop = bc_op(execT->resumeins);
	use_resume_contract = 1;
      }
    if (lj_trace_s390x_jloop_exit_log_enabled() &&
	lj_trace_s390x_jloop_exit_focus_match(J)) {
      fprintf(stderr,
	      "S390X_JLOOP_EXIT parent=%u exit=%u pc=%p op=%u target=%u target_exec=%u retpc=%p retop=%u trace=%u link=%u linktype=%u target_root=%u target_link=%u target_linktype=%u target_startpc=%p target_startop=%u target_resumepc=%p target_resumeop=%u target_resumevalid=%u target_resumechild=%u target_mcloop=%u target_ownerop=%u state=%u\n",
	      (unsigned int)J->parent, (unsigned int)J->exitno,
	      (const void *)pc, (unsigned int)bc_op(*pc),
	      (unsigned int)bc_d(*pc),
	      (unsigned int)execno,
	      (const void *)retpc, (unsigned int)retop,
	      (unsigned int)T->traceno, (unsigned int)T->link,
	      (unsigned int)T->linktype,
	      (unsigned int)targetT->root,
	      (unsigned int)targetT->link,
	      (unsigned int)targetT->linktype,
	      (const void *)mref(targetT->startpc, BCIns),
	      (unsigned int)bc_op(targetT->startins),
	      (const void *)resume_bcpc,
	      (unsigned int)bc_op(targetT->resumeins),
	      (unsigned int)targetT->resumevalid,
	      (unsigned int)targetT->resumechild,
	      (unsigned int)targetT->mcloop,
	      (unsigned int)targetT->unused1,
	      (unsigned int)J->state);
      if (execno != 0 && execno != targetT->traceno) {
	if (execT != NULL) {
	  fprintf(stderr,
		  "S390X_JLOOP_EXEC trace=%u exec=%u exec_root=%u exec_link=%u exec_linktype=%u exec_startpc=%p exec_startop=%u exec_resumepc=%p exec_resumeop=%u exec_resumevalid=%u exec_resumechild=%u exec_mcloop=%u exec_ownerop=%u\n",
		  (unsigned int)T->traceno, (unsigned int)execno,
		  (unsigned int)execT->root, (unsigned int)execT->link,
		  (unsigned int)execT->linktype,
		  (const void *)mref(execT->startpc, BCIns),
		  (unsigned int)bc_op(execT->startins),
		  (const void *)mref(execT->resumepc, BCIns),
		  (unsigned int)bc_op(execT->resumeins),
		  (unsigned int)execT->resumevalid,
		  (unsigned int)execT->resumechild,
		  (unsigned int)execT->mcloop,
		  (unsigned int)execT->unused1);
	}
      }
    }
    if (use_resume_contract &&
	lj_trace_s390x_jloop_exec_child_enabled() &&
	execno != 0 && execno != targetT->traceno &&
	J->parent == execno) {
      TraceNo rootno = targetT->root ? targetT->root : targetT->traceno;
      TraceNo childno = lj_trace_s390x_hotside_find_child(J, rootno,
							  execno, J->exitno);
      if (lj_trace_s390x_jloop_exit_log_enabled() &&
	  lj_trace_s390x_jloop_exit_focus_match(J)) {
	fprintf(stderr,
		"S390X_JLOOP_EXIT phase=exec-child-query parent=%u exit=%u trace=%u target=%u exec=%u child=%u state=%u\n",
		(unsigned int)J->parent, (unsigned int)J->exitno,
		(unsigned int)T->traceno, (unsigned int)targetT->traceno,
		(unsigned int)execno, (unsigned int)childno,
		(unsigned int)J->state);
      }
      if (childno != 0) {
	if (lj_trace_s390x_jloop_exit_log_enabled() &&
	    lj_trace_s390x_jloop_exit_focus_match(J)) {
	  fprintf(stderr,
		  "S390X_JLOOP_EXIT phase=retarget-exec-child parent=%u exit=%u trace=%u target=%u exec=%u child=%u state=%u\n",
		  (unsigned int)J->parent, (unsigned int)J->exitno,
		  (unsigned int)T->traceno, (unsigned int)targetT->traceno,
		  (unsigned int)execno, (unsigned int)childno,
		  (unsigned int)J->state);
	}
	J->patchins = *pc;
	J->patchpc = (BCIns *)pc;
	*J->patchpc = BCINS_AD(BC_JLOOP, bc_a(*pc), childno);
	J->bcskip = 1;
	return -17;
      }
    }
    if (use_resume_contract &&
	lj_trace_s390x_root_jloop_child_enabled() &&
	J->parent == targetT->traceno) {
      TraceNo childno = lj_trace_s390x_hotside_find_child(J, targetT->traceno,
							  J->parent, J->exitno);
      if (lj_trace_s390x_jloop_exit_log_enabled() &&
	  lj_trace_s390x_jloop_exit_focus_match(J)) {
	fprintf(stderr,
		"S390X_JLOOP_EXIT phase=child-query parent=%u exit=%u trace=%u child=%u state=%u\n",
		(unsigned int)J->parent, (unsigned int)J->exitno,
		(unsigned int)T->traceno, (unsigned int)childno,
		(unsigned int)J->state);
      }
      if (childno != 0) {
	if (lj_trace_s390x_jloop_exit_log_enabled() &&
	    lj_trace_s390x_jloop_exit_focus_match(J)) {
	  fprintf(stderr,
		  "S390X_JLOOP_EXIT phase=retarget-child parent=%u exit=%u trace=%u child=%u state=%u\n",
		  (unsigned int)J->parent, (unsigned int)J->exitno,
		  (unsigned int)T->traceno, (unsigned int)childno,
		  (unsigned int)J->state);
	}
	J->patchins = *pc;
	J->patchpc = (BCIns *)pc;
	*J->patchpc = BCINS_AD(BC_JLOOP, bc_a(*pc), childno);
	J->bcskip = 1;
	return -17;
      }
    }
    if (lj_trace_s390x_jloop_loopdesc_child_enabled() &&
	targetT->root != 0 &&
	bc_op(targetT->startins) == BC_JMP &&
	execno != targetT->traceno &&
	J->state == LJ_TRACE_IDLE) {
      TraceNo childno = lj_trace_s390x_hotside_find_child(J, targetT->root,
							  J->parent, J->exitno);
      if ((lj_trace_s390x_jloop_exit_log_enabled() &&
	   lj_trace_s390x_jloop_exit_focus_match(J)) ||
	  (lj_trace_s390x_bridge_child_query_log_enabled() &&
	   lj_trace_s390x_is_loopdesc_bridge_stub(targetT))) {
	fprintf(stderr,
		"S390X_JLOOP_EXIT phase=loopdesc-child-query parent=%u exit=%u trace=%u target=%u exec=%u child=%u state=%u pc=%p ins=0x%08x op=%u target_startpc=%p target_startop=%u target_resumepc=%p target_resumeins=0x%08x\n",
		(unsigned int)J->parent, (unsigned int)J->exitno,
		(unsigned int)T->traceno, (unsigned int)targetT->traceno,
		(unsigned int)execno, (unsigned int)childno,
		(unsigned int)J->state,
		(const void *)pc,
		(unsigned int)*pc,
		(unsigned int)bc_op(*pc),
		(const void *)mref(targetT->startpc, BCIns),
		(unsigned int)bc_op(targetT->startins),
		(const void *)resume_bcpc,
		(unsigned int)targetT->resumeins);
      }
      if (childno == targetT->traceno &&
	  lj_trace_s390x_is_loopdesc_bridge_stub(targetT) &&
	  execno == targetT->link &&
	  bc_op(*pc) == BC_JLOOP) {
	if ((lj_trace_s390x_jloop_exit_log_enabled() &&
	     lj_trace_s390x_jloop_exit_focus_match(J)) ||
	    lj_trace_s390x_bridge_child_reenter_log_enabled()) {
	  fprintf(stderr,
		  "S390X_JLOOP_EXIT phase=loopdesc-bridge-child-reenter parent=%u exit=%u trace=%u target=%u exec=%u child=%u state=%u pc=%p ins=0x%08x op=%u prev1=0x%08x prev1op=%u next1=0x%08x next1op=%u patchpc=%p patchins=0x%08x resume_bcpc=%p target_resumeins=0x%08x\n",
		  (unsigned int)J->parent, (unsigned int)J->exitno,
		  (unsigned int)T->traceno, (unsigned int)targetT->traceno,
		  (unsigned int)execno, (unsigned int)childno,
		  (unsigned int)J->state,
		  (const void *)pc,
		  (unsigned int)*pc,
		  (unsigned int)bc_op(*pc),
		  (unsigned int)pc[-1],
		  (unsigned int)bc_op(pc[-1]),
		  (unsigned int)pc[1],
		  (unsigned int)bc_op(pc[1]),
		  (const void *)J->patchpc,
		  (unsigned int)J->patchins,
		  (const void *)resume_bcpc,
		  (unsigned int)targetT->resumeins);
	}
	return -17;
      } else if (childno == targetT->traceno) {
	if (lj_trace_s390x_jloop_exit_log_enabled() &&
	    lj_trace_s390x_jloop_exit_focus_match(J)) {
	  fprintf(stderr,
		  "S390X_JLOOP_EXIT phase=loopdesc-child-skip-self parent=%u exit=%u trace=%u target=%u exec=%u child=%u state=%u\n",
		  (unsigned int)J->parent, (unsigned int)J->exitno,
		  (unsigned int)T->traceno, (unsigned int)targetT->traceno,
		  (unsigned int)execno, (unsigned int)childno,
		  (unsigned int)J->state);
	}
      } else if (childno != 0) {
	J->patchins = *pc;
	J->patchpc = (BCIns *)pc;
	*J->patchpc = BCINS_AD(BC_JLOOP, bc_a(*pc), childno);
	J->bcskip = 1;
	return -17;
      }
    }
    if (use_resume_contract &&
	lj_trace_s390x_jloop_owner_static_reenter_enabled() &&
	J->state == LJ_TRACE_IDLE &&
	targetT->root == 1 &&
	bc_op(targetT->startins) == BC_JMP &&
	targetT->nins == 32770 &&
	targetT->mcloop == 0 &&
	((targetT->linktype == LJ_TRLINK_ROOT &&
	  targetT->link == J->parent &&
	  targetT->resumevalid &&
	  bc_op(targetT->resumeins) == BC_JLOOP &&
	  targetT->resumechild == targetT->traceno &&
	  targetT->unused1 == (uint8_t)BC_LOOP &&
	  bc_d(*pc) == targetT->traceno) ||
	 (targetT->linktype == LJ_TRLINK_ROOT &&
	  targetT->link != 0 &&
	  targetT->resumevalid &&
	  bc_op(targetT->resumeins) == BC_ITERL &&
	  targetT->resumechild == 0 &&
	  targetT->unused1 == 0 &&
	  J->parent == 3 &&
	  J->exitno == 0 &&
	  bc_d(*pc) == targetT->traceno))) {
      if (lj_trace_s390x_jloop_exit_log_enabled() &&
	  lj_trace_s390x_jloop_exit_focus_match(J)) {
	fprintf(stderr,
		"S390X_JLOOP_EXIT phase=owner-static-reenter parent=%u exit=%u trace=%u target=%u state=%u\n",
		(unsigned int)J->parent, (unsigned int)J->exitno,
		(unsigned int)T->traceno, (unsigned int)targetT->traceno,
		(unsigned int)J->state);
      }
      return -17;
    }
    if (lj_trace_s390x_jloop_exec_self_pred_log_enabled() &&
	lj_trace_s390x_jloop_exit_focus_match(J) &&
	J->parent == T->traceno &&
	J->exitno == 0) {
      fprintf(stderr,
	      "S390X_JLOOP_EXIT phase=exec-self-pred parent=%u exit=%u trace=%u target=%u exec=%u use_resume=%u idle=%u parent_is_trace=%u exec_is_trace=%u is_jloop=%u bcd=%u\n",
	      (unsigned int)J->parent, (unsigned int)J->exitno,
	      (unsigned int)T->traceno, (unsigned int)targetT->traceno,
	      (unsigned int)execno,
	      (unsigned int)use_resume_contract,
	      (unsigned int)(J->state == LJ_TRACE_IDLE),
	      (unsigned int)(J->parent == T->traceno),
	      (unsigned int)(execno == T->traceno),
	      (unsigned int)(bc_op(*pc) == BC_JLOOP),
	      (unsigned int)bc_d(*pc));
    }
    if (use_resume_contract &&
	lj_trace_s390x_jloop_exec_self_reenter_enabled() &&
	J->state == LJ_TRACE_IDLE &&
	execno != 0 &&
	execno == T->traceno &&
	J->parent == T->traceno &&
	J->exitno == 0 &&
	bc_op(*pc) == BC_JLOOP) {
      if (lj_trace_s390x_jloop_exit_log_enabled() &&
	  lj_trace_s390x_jloop_exit_focus_match(J)) {
	fprintf(stderr,
		"S390X_JLOOP_EXIT phase=exec-self-reenter parent=%u exit=%u trace=%u target=%u exec=%u state=%u\n",
		(unsigned int)J->parent, (unsigned int)J->exitno,
		(unsigned int)T->traceno, (unsigned int)targetT->traceno,
		(unsigned int)execno, (unsigned int)J->state);
      }
      if (bc_d(*pc) != execno) {
	J->patchins = *pc;
	J->patchpc = (BCIns *)pc;
	*J->patchpc = BCINS_AD(BC_JLOOP, bc_a(*pc), execno);
	J->bcskip = 1;
      }
      return -17;
    }
    if (bc_isret(retop) || retop == BC_ITERN) {
      if (lj_trace_s390x_jloop_exit_log_enabled() &&
	  lj_trace_s390x_jloop_exit_focus_match(J)) {
	fprintf(stderr,
		"S390X_JLOOP_EXIT phase=dispatch-original parent=%u exit=%u trace=%u retop=%u state=%u\n",
		(unsigned int)J->parent, (unsigned int)J->exitno,
		(unsigned int)T->traceno, (unsigned int)retop,
		(unsigned int)J->state);
      }
      /* Dispatch to original ins to ensure forward progress. */
      if (J->state != LJ_TRACE_RECORD) return -17;
      /* Unpatch bytecode when recording. */
      J->patchins = *pc;
      J->patchpc = (BCIns *)pc;
      *J->patchpc = *retpc;
      J->bcskip = 1;
      } else if (lj_trace_s390x_jloop_exit_log_enabled() &&
		 lj_trace_s390x_jloop_exit_focus_match(J)) {
      fprintf(stderr,
	      "S390X_JLOOP_EXIT phase=resume-linked parent=%u exit=%u trace=%u retop=%u retpc=%p patchpc=%p state=%u\n",
	      (unsigned int)J->parent, (unsigned int)J->exitno,
	      (unsigned int)T->traceno, (unsigned int)retop,
	      (const void *)retpc, (const void *)J->patchpc,
	      (unsigned int)J->state);
    }
    return 0;
    }
  default:
    if (bc_op(*pc) >= BC_FUNCF)
      return (int)((BCReg)(L->top - L->base) + 1);
    return 0;
  }
}

#if LJ_UNWIND_JIT
/* Given an mcode address determine trace exit address for unwinding. */
uintptr_t LJ_FASTCALL lj_trace_unwind(jit_State *J, uintptr_t addr, ExitNo *ep)
{
#if EXITTRACE_VMSTATE
  TraceNo traceno = J2G(J)->vmstate;
#else
  TraceNo traceno = trace_exit_find(J, (MCode *)addr);
#endif
  GCtrace *T = traceref(J, traceno);
  if (T
#if EXITTRACE_VMSTATE
      && addr >= (uintptr_t)T->mcode && addr < (uintptr_t)T->mcode + T->szmcode
#endif
     ) {
    SnapShot *snap = T->snap;
    SnapNo lo = 0, exitno = T->nsnap;
    uintptr_t ofs = (uintptr_t)((MCode *)addr - T->mcode);  /* MCode units! */
    /* Rightmost binary search for mcode offset to determine exit number. */
    do {
      SnapNo mid = (lo+exitno) >> 1;
      if (ofs < snap[mid].mcofs) exitno = mid; else lo = mid + 1;
    } while (lo < exitno);
    exitno--;
    *ep = exitno;
#ifdef EXITSTUBS_PER_GROUP
    return (uintptr_t)exitstub_addr(J, exitno);
#else
    return (uintptr_t)exitstub_trace_addr(T, exitno);
#endif
  }
  /* Cannot correlate addr with trace/exit. This will be fatal. */
  lj_assertJ(0, "bad exit pc");
  return 0;
}

LJ_FUNC int32_t lj_trace_s390x_vload_probe(const void *effp, int32_t ofs)
{
#if LJ_TARGET_S390X
  static int dump_count = 0;
  const uint8_t *eff = (const uint8_t *)effp;
  uint32_t value = lj_trace_s390x_load_be32(eff + ofs);
  if (lj_trace_s390x_vload_probe_enabled() && dump_count < 128) {
    fprintf(stderr,
	    "S390X_VLOAD_PROBE n=%d eff=%p ofs=%d value=%u/0x%08x\n",
	    dump_count, effp, (int)ofs, (unsigned int)value, (unsigned int)value);
    lj_trace_s390x_dump_ptr_u32(stderr, "vload_eff", (uintptr_t)effp);
    fprintf(stderr, "\n");
    dump_count++;
  }
  return (int32_t)value;
#else
  UNUSED(effp);
  UNUSED(ofs);
  return 0;
#endif
}

LJ_FUNC int32_t lj_trace_s390x_vload_key_probe(const void *effp, int32_t ofs,
					       int32_t iofs, int32_t trace,
					       int32_t curins)
{
#if LJ_TARGET_S390X
  static int dump_count = 0;
  const uint8_t *eff = (const uint8_t *)effp;
  uint64_t raw = lj_trace_s390x_load_be64(eff + ofs);
  uint32_t lane = lj_trace_s390x_load_be32(eff + iofs);
  int32_t tag = (int32_t)(raw >> 47);
  if (lj_trace_s390x_vload_probe_enabled() && dump_count < 128) {
    fprintf(stderr,
	    "S390X_VLOAD_KEY_PROBE n=%d trace=%d curins=%d eff=%p ofs=%d iofs=%d raw=0x%016llx tag=%d/0x%x lane=%u/0x%08x\n",
	    dump_count, (int)trace, (int)curins, effp, (int)ofs, (int)iofs,
	    (unsigned long long)raw, (int)tag, (unsigned int)tag,
	    (unsigned int)lane, (unsigned int)lane);
    lj_trace_s390x_dump_ptr_u32(stderr, "vload_key_eff", (uintptr_t)effp);
    fprintf(stderr, "\n");
    dump_count++;
  }
  return (int32_t)lane;
#else
  UNUSED(effp);
  UNUSED(ofs);
  UNUSED(iofs);
  UNUSED(trace);
  UNUSED(curins);
  return 0;
#endif
}

LJ_FUNC int32_t lj_trace_s390x_vm_next_handoff_probe(int32_t retidx,
						      const void *retp,
						      int32_t trace,
						      int32_t curins,
						      const void *dispatchp)
{
#if LJ_TARGET_S390X
  static int dump_count = 0;
  const uint8_t *eff = (const uint8_t *)retp;
  const uint8_t *dispatch = (const uint8_t *)dispatchp;
  global_State *g = (global_State *)(dispatch + GG_DISP2G);
  uintptr_t tmptv = (uintptr_t)(dispatch + GG_DISP2G +
				offsetof(global_State, tmptv));
  uintptr_t tmptv2 = (uintptr_t)(dispatch + GG_DISP2G +
				 offsetof(global_State, tmptv2));
  uint64_t raw = lj_trace_s390x_load_be64(eff + 8);
  uint32_t lane = lj_trace_s390x_load_be32(eff + 12);
  int32_t tag = (int32_t)(raw >> 47);
  lua_State *L = g && gcref(g->cur_L) ? &gcref(g->cur_L)->th : NULL;
  static const int slots[] = {0, 3, 6, 9, 10, 11, 13};
  size_t i;
  if (dump_count < 128) {
    fprintf(stderr,
	    "S390X_VM_NEXT_HANDOFF_PROBE n=%d trace=%d curins=%d retidx=%d/0x%x retp=%p raw=0x%016llx tag=%d/0x%x lane=%u/0x%08x dispatch=%p match_tmptv=%d match_tmptv2=%d L=%p base=%p\n",
	    dump_count, (int)trace, (int)curins, (int)retidx,
	    (unsigned int)retidx, retp, (unsigned long long)raw, (int)tag,
	    (unsigned int)tag, (unsigned int)lane, (unsigned int)lane,
	    dispatchp, ((uintptr_t)retp == tmptv) ? 1 : 0,
	    ((uintptr_t)retp == tmptv2) ? 1 : 0, (void *)L,
	    (void *)(L ? L->base : NULL));
    if (L && L->base) {
      for (i = 0; i < sizeof(slots)/sizeof(slots[0]); i++) {
	const TValue *o = &L->base[slots[i]];
	fprintf(stderr, " slot%d=itype%d/u64=0x%016llx", slots[i],
		(int)itype(o), (unsigned long long)o->u64);
      }
      fprintf(stderr, "\n");
    }
    lj_trace_s390x_dump_tvalue_pair(stderr, "handoff_ret", (uintptr_t)retp);
    lj_trace_s390x_dump_dispatch_tmptv(stderr, (uintptr_t)dispatchp,
				       (uintptr_t)retp);
    fprintf(stderr, "\n");
    dump_count++;
  }
  return tag;
#else
  UNUSED(retidx);
  UNUSED(retp);
  UNUSED(trace);
  UNUSED(curins);
  UNUSED(dispatchp);
  return 0;
#endif
}

LJ_FUNC int32_t lj_trace_s390x_vload_next_key_guard_probe(const void *effp,
							  int32_t ofs,
							  int32_t trace,
							  int32_t curins,
							  const void *dispatchp)
{
#if LJ_TARGET_S390X
  static int dump_count = 0;
  const uint8_t *eff = (const uint8_t *)effp;
  const uint8_t *dispatch = (const uint8_t *)dispatchp;
  uintptr_t tmptv = (uintptr_t)(dispatch + GG_DISP2G +
				offsetof(global_State, tmptv));
  uintptr_t tmptv2 = (uintptr_t)(dispatch + GG_DISP2G +
				 offsetof(global_State, tmptv2));
  uint64_t raw = lj_trace_s390x_load_be64(eff + ofs);
  uint32_t lane = lj_trace_s390x_load_be32(eff + ofs + 4);
  int32_t tag = (int32_t)(raw >> 47);
  if (dump_count < 128) {
    fprintf(stderr,
	    "S390X_VLOAD_NEXT_KEY_GUARD_PROBE n=%d trace=%d curins=%d eff=%p ofs=%d raw=0x%016llx tag=%d/0x%x lane=%u/0x%08x dispatch=%p match_tmptv=%d match_tmptv2=%d\n",
	    dump_count, (int)trace, (int)curins, effp, (int)ofs,
	    (unsigned long long)raw, (int)tag, (unsigned int)tag,
	    (unsigned int)lane, (unsigned int)lane, dispatchp,
	    ((uintptr_t)effp == tmptv) ? 1 : 0,
	    ((uintptr_t)effp == tmptv2) ? 1 : 0);
    lj_trace_s390x_dump_tvalue_pair(stderr, "guard_eff", (uintptr_t)effp);
    lj_trace_s390x_dump_dispatch_tmptv(stderr, (uintptr_t)dispatchp,
				       (uintptr_t)effp);
    fprintf(stderr, "\n");
    dump_count++;
  }
  return tag;
#else
  UNUSED(effp);
  UNUSED(ofs);
  UNUSED(trace);
  UNUSED(curins);
  UNUSED(dispatchp);
  return 0;
#endif
}

LJ_FUNC uint64_t lj_trace_s390x_vload_addr_probe(const void *effp, int32_t ofs)
{
#if LJ_TARGET_S390X
  static int dump_count = 0;
  const uint8_t *eff = (const uint8_t *)effp;
  uint64_t value = *(const uint64_t *)(eff + ofs);
  if (lj_trace_s390x_vload_probe_enabled() && dump_count < 128) {
    fprintf(stderr,
	    "S390X_VLOAD_ADDR_PROBE n=%d eff=%p ofs=%d q=0x%016llx\n",
	    dump_count, effp, (int)ofs, (unsigned long long)value);
    lj_trace_s390x_dump_ptr_u32(stderr, "vload_addr_eff", (uintptr_t)effp);
    fprintf(stderr, "\n");
    dump_count++;
  }
  return value;
#else
  UNUSED(effp);
  UNUSED(ofs);
  return 0;
#endif
}

LJ_FUNC int32_t lj_trace_s390x_sload_probe(const void *effp, int32_t ofs)
{
#if LJ_TARGET_S390X
  static int dump_count = 0;
  const uint8_t *eff = (const uint8_t *)effp;
  uint64_t raw = lj_trace_s390x_load_be64(eff + ofs);
  int32_t tag = (int32_t)(raw >> 47);
  if (lj_trace_s390x_sload_probe_enabled() && dump_count < 128) {
    fprintf(stderr,
	    "S390X_SLOAD_PROBE n=%d eff=%p ofs=%d raw=0x%016llx tag=%d/0x%x hi=0x%08x lo=0x%08x\n",
	    dump_count, effp, (int)ofs, (unsigned long long)raw,
	    (int)tag, (unsigned int)tag,
	    (unsigned int)(raw >> 32), (unsigned int)raw);
    lj_trace_s390x_dump_ptr_u32(stderr, "sload_eff", (uintptr_t)effp);
    fprintf(stderr, "\n");
    dump_count++;
  }
  return tag;
#else
  UNUSED(effp);
  UNUSED(ofs);
  return 0;
#endif
}
#endif

#else

LJ_FUNC int32_t lj_trace_s390x_varg_probe(const void *effp, int32_t ignored)
{
  UNUSED(effp);
  UNUSED(ignored);
  return 0;
}

LJ_FUNC int32_t lj_trace_s390x_vload_probe(const void *effp, int32_t ofs)
{
  UNUSED(effp);
  UNUSED(ofs);
  return 0;
}

LJ_FUNC int32_t lj_trace_s390x_vload_key_probe(const void *effp, int32_t ofs,
					       int32_t iofs, int32_t trace,
					       int32_t curins)
{
  UNUSED(effp);
  UNUSED(ofs);
  UNUSED(iofs);
  UNUSED(trace);
  UNUSED(curins);
  return 0;
}

LJ_FUNC int32_t lj_trace_s390x_sload_probe(const void *effp, int32_t ofs)
{
  UNUSED(effp);
  UNUSED(ofs);
  return 0;
}

LJ_FUNC void lj_trace_s390x_iter_log(const TValue *base, const TValue *iterslot)
{
  UNUSED(base);
  UNUSED(iterslot);
}

#endif
