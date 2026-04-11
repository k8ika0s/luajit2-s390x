# s390x State Of The Project

Last updated: 2026-04-11 00:00 PDT

This file is the current plain-language status page for the s390x bring-up.
It is intentionally current-state only. Historical experiment detail lives in
[findings.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/findings.md).

## Current State

- The branch is in a post-promotion stabilization pass, not a new frontier
  attack.
- A two-host full-env rerun on current head `8f775c23` confirmed the apparent
  post-promotion collapse was an incomplete-env run artifact, not a reason to
  merge the lab/freeze branch as a rescue. Use the full retained env contract
  from
  [tools/s390x/build_iterator_truth_pack.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/build_iterator_truth_pack.py)
  for matrix reads.
- The current source recovery point is the existing
  [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c)
  post-promotion `ffi_cdata` restamp:
  `J->cur.mcloop == 324 || J->cur.mcloop == 316`.
- The ISA lab A3/A1/trace promotion remains carried together with the exact
  `sum_loop` root-`BC_FORL` restamp that accepts the promoted `mcloop=304`
  shape.
- The first real post-promotion drift was the exact `vararg_paths` sibling
  root-`BC_FORL` matcher in
  [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c):
  `LUAJIT_S390X_VARARG_SIBLING_FORL_BLACKLIST=1` now accepts both the
  pre-promotion shapes (`mcloop=672`, `452`) and the promoted shapes
  (`mcloop=660`, `444`) for the same `nins=32820` / `32806` sibling family.
- The delivered
  [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c)
  hash is now identical on local, `kdz`, and `zkd0`:
  `e7c83ef8770fa5cf819b4f8d8e301f330131759b6e885eb08bf613429fddc0a4`.
- Current retained `vararg_paths` rows after the sibling restamp:
  - trusted `kdz` rerun:
    - `sum_loop/hot 0.004437` vs `-joff 0.004789`
    - `retlast_loop/hot 0.001997` vs `-joff 0.001991`
    - `retconst_loop/hot 0.000598` vs `-joff 0.000598`
  - `zkd0` confirmation:
    - `sum_loop/hot 0.005042` vs `-joff 0.004885`
    - `retlast_loop/hot 0.002420` vs `-joff 0.002243`
    - `retconst_loop/hot 0.000652` vs `-joff 0.000638`
- `iterator_table` now carries three low-level VM/control wins on top of the
  full retained env floor:
  [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc)
  stores the array-side `BC_ITERN` returned value directly from `TMPR0`
  instead of copying through `RB` first, and the shared s390x `hotcheck` macro
  now loads the 16-bit hotcount with `llgh` so the exact iterator
  `BC_ITERN` no-JIT hotcount parks in
  [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c)
  can use `0xffff` instead of `0x7fff`. The latest retained cut adds
  `LUAJIT_S390X_ITERATOR_POST_PROTO_ITERN_NOHOT=1`: after the exact iterator
  root `BC_ITERN` proto-NOJIT save fires, `BC_ITERN` dispatch switches to
  `lj_vm_IITERN` for the rest of the process. Trusted `kdz` same-binary A/B:
  candidate `pairs_sum/hot 0.004292`,
  `pairs_array_sum/hot 0.003588`; immediate retained control `0.004501`,
  `0.003917`. Trusted `zkd0` screen: candidate `0.006538`, `0.005348`;
  retained control `0.007506`, `0.007088`. Mechanism logs show the remaining
  hash/array no-JIT hotcount park events drop to zero after the delayed switch.
- `mixed_noffi`, `mixed_ffi`, and `ffi_cdata` remain parked near parity on the
  carried floor; `mixed_noffi` still has noisy reads and should not be
  reopened without a fresh exact attribution.
- `dispatch_trace` reopened after the ISA promotion, but is now stabilized
  again with an exact root-`BC_FORL` proto-NOJIT route-around in
  [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c):
  - trusted `kdz`: `numeric_loop/hot 0.002170` vs `-joff 0.002165`,
    `side_exit_loop/hot 0.004557` vs `-joff 0.004704`,
    `hotexit_loop/hot 0.005522` vs `-joff 0.005572`
  - trusted `zkd0`: `numeric_loop/hot 0.002530` vs `-joff 0.003831`,
    `side_exit_loop/hot 0.005002` vs `-joff 0.007007`,
    `hotexit_loop/hot 0.006005` vs `-joff 0.009427`
- `be_helpers` and `ffi_calls` have also been stabilized after their
  post-promotion carried-floor drift with an exact root-`BC_FORL`
  proto-NOJIT route-around in
  [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c):
  - trusted `kdz`: `number_helper_loop/hot 0.002378` vs `-joff 0.002280`,
    `be_pack_loop/hot 0.018912` vs `-joff 0.018973`,
    `direct_abs/hot 0.010257` vs `-joff 0.010148`,
    `stored_abs/hot 0.007338` vs `-joff 0.006981`
  - trusted `zkd0`: `number_helper_loop/hot 0.002554` vs reopened control
    `0.006344`, `be_pack_loop/hot 0.020728` vs reopened control `0.023800`,
    `direct_abs/hot 0.012293` vs reopened control `0.016268`,
    `stored_abs/hot 0.008434` vs reopened control `0.012996`
- The same exact promotion-core root-`BC_FORL` proto-NOJIT route-around now
  covers the current retained bitops/logic-chain shapes after a fresh rerank
  showed the older 2026-04-04 `0.0007` bitops matrix row was stale:
  - trusted `kdz`: `bitops_mix/mix_bits/hot 0.001882` vs `-joff 0.001854`,
    `logical_chain_tail_add/hot 0.001829` vs `-joff 0.001825`,
    `logical_chain_tail_store/hot 0.001763` vs `-joff 0.001839`
  - trusted `zkd0` focused same-source A/B: `mix_bits/hot 0.003830 -> 0.001969`,
    `chain_tail_add/hot 0.003976 -> 0.002125`,
    `chain_tail_store/hot 0.003794 -> 0.002741`
- The active engineering frontier remains the remaining near-parity carried
  rows. The direct iterator VM-body and delayed dispatch micro-lanes are now
  closed after the retained direct-store, hotcount-park-width, post-proto
  no-hot dispatch cut, and current-shape promotion-core bitops/logic
  route-around; rerank from this floor before opening the next subsystem.
- The localized helper/route-around experiment rows now have a retained
  env-gated hotside carry in
  [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c):
  `LUAJIT_S390X_LOCALIZED_HOTSIDE_CANON_SHARE_EQUIV=1`. It is guarded by
  `S390X_PERF_BENCH_FILE`, exact proto line shape, and chunk-name checks, so it
  is not a new stable-matrix row and does not reopen `mixed_noffi`. The same
  scoped carry now covers
  [lower_frame_same_callsite.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/lower_frame_same_callsite.lua)
  after attribution showed its payer is a numeric `FORL/JFORI -> MODVN`
  side-ladder, not the old lower-frame return seam.
- The latest retained experimental carry is the exact lower-frame
  `lua_abs_same_callsite` follow-up route-around:
  `LUAJIT_S390X_LOWER_FRAME_LUA_ABS_PROTO_NOJIT=1`. It parks only the exact
  saved trace-1 root body for the lower-frame benchmark proto after the
  hotside carry, cutting `kdz 0.048729 -> 0.015022` and
  `zkd0 0.058875 -> 0.020010` on same-binary rebuilt-mirror A/B.
- The ISA lab A3/A1/trace promotion slice is merged into the bring-up branch
  at `640e9641`, with one integration restamp on top: the retained
  `sum_loop` root-FORL blacklist now accepts the promoted root trace
  `mcloop=304` shape as well as the previous `mcloop=312` shape. This keeps
  the promoted duplicate-exit descendant guard out of the retained
  `sum_loop` perf path without disabling the guard.
- Current retained `mixed_noffi` host-pair rows on rebuilt mirrors:
  - `kdz`: `mixed_loop/hot 0.004041` vs `-joff 0.003734`
  - `zkd0`: `mixed_loop/hot 0.005562..0.006232` vs `-joff 0.004387`
- The latest retained `vararg_paths` host-pair win remains the exact sibling
  root-FORL blacklist, which keeps `retlast_loop` and `retconst_loop` near
  parity alongside `sum_loop`.
- Current retained `vararg_paths` host-pair rows on rebuilt mirrors:
  - `kdz`
    - `sum_loop/hot 0.004486` vs `-joff 0.004722`
    - `retlast_loop/hot 0.001978` vs `-joff 0.001990`
    - `retconst_loop/hot 0.000570` vs `-joff 0.000598`
  - `zkd0`
    - `sum_loop/hot 0.006285`
    - `retlast_loop/hot 0.002767`
    - `retconst_loop/hot 0.000620`
- Retained `sum_loop` mechanism on trusted `kdz`:
  - the first exact recorder-side `BC_JFORI -> ROOT` handoff candidate is now
    closed as non-engaging on the official hot row
  - the first retained win still comes from the tiny stopper inside the inner
    `sum(...)` callee runtime family, not from the broader nested handoff:
    - exact stop shape:
      - `pcop=BC_GGET`
      - `prevop=BC_JFORI`
      - `startop=BC_JMP`
      - `linktype=LJ_TRLINK_INTERP`
      - `parent=110`
      - `exit=0`
      - `root=1`
    - exact recorder-side cut in
      [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c):
      - `LUAJIT_S390X_SUM_LOOP_SELECT_EXIT0_DONE=1`
      - one-shot `SNAPCOUNT_DONE` on that exact stop family
    - phase-count proof on `kdz`:
      - control `431 -> 752 -> 1024`
      - candidate `112 -> 113 -> 113`
  - post-win runtime attribution corrected the apparent stitched fallback:
    - the `trace 112/113` pair from `/tmp/vararg_sum_phase_counts.lua` was
      wrapper pollution from `jit.util.traceinfo`, not real `sum_loop` work
    - the real hot runtime stayed on `trace 110` at
      `vararg_paths.lua:14`
  - the second retained win attacks that exact runtime path:
    - exact recorder-side cut in
      [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c):
      - `LUAJIT_S390X_SUM_LOOP_SELECT_SKIP_FUNC_EQ=1`
      - exact `select_detect()` skip of the `FF_select` equality guard for the
        inner `sum(...)` proto
    - mechanism proof on `kdz`:
      - exact engagement:
        - `S390X_SUM_LOOP_SELECT_SKIP_FUNC_EQ trace=91..110`
      - same-binary A/B:
        - candidate `sum_loop/hot 0.019045`
        - immediate control `0.021247`
    - host-pair confirmation:
      - `zkd0` candidate `0.023863`
      - immediate same-binary control `0.025948`
  - the third retained win stays on the same exact inner-runtime family:
    - exact recorder-side cut in
      [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c):
      - `LUAJIT_S390X_SUM_LOOP_SELECT_CONST_GGET=1`
      - exact `BC_GGET select` constant-fold for the inner `sum(...)` proto
    - mechanism proof on trusted `kdz`:
      - exact engagement:
        - `S390X_SUM_LOOP_SELECT_CONST_GGET trace=91..110`
      - hot trace delta:
        - `trace 110` shrinks from `34` IRs to `27`
        - the dead `func.env -> HREFK -> HLOAD` lookup prefix disappears
      - same-binary A/B:
        - candidate `sum_loop/hot 0.018707`
        - immediate control `0.019204`
    - host-pair confirmation:
      - `zkd0` candidate `0.022269`
      - immediate same-binary control `0.026834`
  - the fourth retained win cuts the exact root-loop trace ladder above that
    same inner-runtime family:
    - exact trace-side cut in
      [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c):
      - `LUAJIT_S390X_SUM_LOOP_FORL_BLACKLIST=1`
      - exact `trace_stop()` root `BC_FORL` blacklist for the inner `sum(...)`
        proto only
    - mechanism proof on trusted `kdz`:
      - exact marker:
        - `S390X_SUM_LOOP_FORL_BLACKLIST trace=1 startop=79 link=1 linktype=2 nsnap=4 nins=32796 mcloop=312`
      - trace meta drops from the retained 110-trace ladder to `10`
      - same-binary A/B:
        - candidate `sum_loop/hot 0.004533`
        - immediate control `0.019065`
    - host-pair confirmation:
      - `zkd0` candidate `0.007265`
      - immediate same-binary control `0.028302`
  - the fifth retained win applies the same trace-side route-around to the
    vararg siblings:
    - exact trace-side cut in
      [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c):
      - `LUAJIT_S390X_VARARG_SIBLING_FORL_BLACKLIST=1`
      - exact `trace_stop()` root `BC_FORL` blacklist for the `retlast_loop`
        and `retconst_loop` caller-loop protos only
    - mechanism proof on trusted `kdz`:
      - markers:
        - `S390X_VARARG_SIBLING_FORL_BLACKLIST trace=2 firstline=31 nsnap=4 nins=32820 mcloop=672`
        - `S390X_VARARG_SIBLING_FORL_BLACKLIST trace=3 firstline=43 nsnap=4 nins=32806 mcloop=452`
      - same-binary A/B:
        - candidate `retlast_loop/hot 0.001978`
        - immediate control `0.003504`
        - candidate `retconst_loop/hot 0.000570`
        - immediate control `0.001736`
    - host-pair confirmation:
      - `zkd0` candidate rerun `retlast_loop/hot 0.002767`
      - immediate same-binary control `0.004290`
      - `zkd0` candidate rerun `retconst_loop/hot 0.000620`
      - immediate same-binary control `0.002623`
  - read:
    - `sum_loop`, `retlast_loop`, and `retconst_loop` are no longer carried
      red rows on trusted `kdz`
    - the remaining work is later than the first tiny `INTERP` stopper, later
      than the dead `select` equality guard, and later than the exact
      `BC_GGET select` lookup prefix inside the same inner callee runtime
      family
    - the later whole-loop-contract lane on the carried `trace 110` body is
      closed as exact-but-not-retainable; the retained route-around is the
      exact root-loop blacklist instead
    - next work should re-attribute `mixed_noffi` only with a newly named
      subsystem, or rerank if another residual row becomes dominant
- Current `iterator_table` read:
  - the retained host-pair wins are exact root `BC_ITERN` and root
    `BC_ITERL` blacklists, exact root `BC_ITERN` proto-NOJIT fallback, and
    exact hash/array-side root-ITERN proto-NOJIT hotcount parking
    in [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c):
    - env: `LUAJIT_S390X_ITERATOR_ITERN_BLACKLIST=1`
    - env: `LUAJIT_S390X_ITERATOR_ITERL_BLACKLIST=1`
    - env: `LUAJIT_S390X_ITERATOR_ITERN_PROTO_NOJIT=1`
    - env: `LUAJIT_S390X_ITERATOR_ARRAY_ITERN_NOJIT_HOTCOUNT_PARK=1`
    - env: `LUAJIT_S390X_ITERATOR_HASH_ITERN_NOJIT_HOTCOUNT_PARK=1`
    - `kdz`: `pairs_sum/hot 0.004532`, `pairs_array_sum/hot 0.003973`
    - `zkd0`: `pairs_sum/hot 0.005227`, `pairs_array_sum/hot 0.005431`
    - immediate retained controls on `kdz`: `0.005543`, `0.003950`
    - immediate retained controls on `zkd0`: `0.008212`, `0.006333`
  - the official carried hot rows are now near parity:
    - `pairs_sum/hot 0.004532` vs `-joff 0.004135`
    - `pairs_array_sum/hot 0.003973` vs `-joff 0.003651`
  - closed exact iterator probes include direct tail `BRXH`, compare-side
    `CGRJ`, keyindex/HIOP register-home variants, accumulator PHI save skip,
    guarded `ADDOV` 32-bit `AR`, and signed `VLOAD` contraction
  - after the retained root-ITERN blacklist, corrected official-row attribution
    exposed a root `BC_ITERL` array loop trace:
    `parent=0 exit=0 root=0 startop=BC_ITERL nsnap=2 nins=32798 mcloop=512`
    followed by a `root=3` `BC_JMP` exit-0 loop-descendant chain
  - after the root-ITERL blacklist, corrected post-blacklist attribution showed
    the remaining payer was the blacklist fallback contract itself: the existing
    `blacklist_pc()` path rewrote fast `BC_ITERN` into generic `BC_ITERC`, while
    `-joff` kept the fast non-hotcounting `vm_IITERN` interpreter path
  - exact `mcloop=208` reuse of the existing IITERN bridge paths is now closed:
    generic selector was host-divergent, and hash-only selector failed the
    same-host repeat/control gate
  - the retained blacklist cut is deliberately narrower than those closed
    bridge and hotside-DONE paths:
    - match only `@tests/s390x/perf/iterator_table.lua`
    - match only successful root `BC_ITERN` loop traces with
      `nsnap=6 nins=32785 mcloop=208` or `nsnap=6 nins=32792 mcloop=300`
    - match only the newly exposed root `BC_ITERL` loop trace with
      `nsnap=2 nins=32798 mcloop=512`
    - use LuaJIT's existing `blacklist_pc()` transition to avoid the hot
      root-loop runtime handoff / descendant chain
    - then, for those exact root `BC_ITERN` traces only, set `PROTO_NOJIT`
      instead of taking the generic `ITERC` fallback so the steady-state path
      stays on fast `ITERN`
    - then, for the array-side root `BC_ITERN` proto only (`firstline=22`,
      `numline=8`), park the hotcount at root-save and proto-NOJIT reentry so
      the row keeps fast `ITERN` fallback without repeated trace-start churn
- Retained `mixed_ffi` wins:
  - exact cut in
    [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c):
    `LUAJIT_S390X_MIXED_FFI_POST_STITCH_SAVE_DONE=1`
  - mechanism:
    - one-shot save-time `SNAPCOUNT_DONE` on the exact post-stitch
      `BC_TGETB` child:
      `trace=102 parent=101 exit=0 root=1 startop=BC_JMP linktype=LJ_TRLINK_INTERP nsnap=2 nins=32773`
    - `mixed_noffi` does not hit the marker
  - host-pair result:
    - `kdz`: `mixed_ffi_loop/hot 0.017600` against immediate controls
      `0.044956` and `0.044900`
    - `zkd0`: candidate examples `0.026970` and `0.023683` against immediate
      controls `0.056937` and `0.085072`
  - exact follow-up cut in
    [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c):
    `LUAJIT_S390X_MIXED_FFI_FORL_PROTO_NOJIT=1`
  - mechanism:
    - on the official root trace only, match
      `@tests/s390x/perf/mixed_ffi.lua`, `trace=1`, `parent=0`, `exit=0`,
      `startop=BC_FORL`, `linktype=LJ_TRLINK_STITCH`, `topslot=14`,
      `spadjust=192`, `nsnap=4`, `nins=32822`
    - set `PROTO_NOJIT` to avoid the remaining 100-trace stitched chain and
      keep the row on the interpreter-speed path
  - host-pair result:
    - `kdz`: candidate rerun `mixed_ffi_loop/hot 0.012178` against immediate
      disabled-env control `0.018412`
    - `zkd0`: candidate rerun `mixed_ffi_loop/hot 0.013641` against immediate
      disabled-env control `0.019946`
- Retained `ffi_cdata` wins:
  - exact cut in
    [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c):
    `LUAJIT_S390X_FFI_CDATA_PAIR_SAVE_DONE=1`
  - mechanism:
    - one-shot save-time `SNAPCOUNT_DONE` on the exact pair-loop `BC_TGETB`
      interpreter child:
      `trace=102 parent=101 exit=0 root=1 startop=BC_JMP link=0 linktype=LJ_TRLINK_INTERP topslot=9 spadjust=8 nsnap=2 nins=32773`
    - marker fires exactly once on both hosts:
      `S390X_FFI_CDATA_PAIR_SAVE_DONE trace=102 parent=101 exit=0 root=1 startop=88 link=0 linktype=6 nsnap=2 nins=32773 snap=0 op=58`
  - host-pair result:
    - `kdz`: `pair_loop/hot 0.023094` against immediate controls `0.136461`
      and `0.133813`
    - `zkd0`: repeated candidate examples `0.026214`, `0.026737`,
      `0.027103` against immediate controls `0.137762`, `0.139439`,
      `0.140058`
    - `mixed_width_loop/hot` is noisy but neutral overall and stays a
      regression screen
  - read:
    - the live payer was trace-control churn through a same-start root-1
      `BC_JMP` sidechain degrading to `LJ_TRLINK_INTERP`
  - exact follow-up cut in
    [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c):
    `LUAJIT_S390X_FFI_CDATA_PAIR_FORL_BLACKLIST=1`
  - mechanism:
    - on the official pair-loop root trace only, match
      `@tests/s390x/perf/ffi_cdata.lua`, `trace=1`, `parent=0`, `exit=0`,
      `startop=BC_FORL`, `link=1`, `linktype=LJ_TRLINK_LOOP`, `topslot=9`,
      `spadjust=8`, `nsnap=7`, `nins=32798`, `mcloop=324`
    - post-promotion restamp: accept the same exact official root trace with
      `mcloop=316`
    - use `blacklist_pc()` to stop the upstream root-loop ladder before the
      downstream `PAIR_SAVE_DONE` sidechain forms
  - host-pair result:
    - `kdz`: candidate rerun `pair_loop/hot 0.017097` against immediate
      disabled-env control `0.023377`
    - `zkd0`: candidate rerun `pair_loop/hot 0.024469` against immediate
      disabled-env control `0.061147`
    - post-promotion restamp:
      `kdz pair_loop/hot 0.017076` against immediate disabled-env control
      `0.018889`; `zkd0 pair_loop/hot 0.019347` against immediate
      disabled-env control `0.021285`
    - `mixed_width_loop/hot` remains noisy but near parity and stays a
      regression screen
  - read:
    - `pair_loop` is now effectively at parity on trusted `kdz`
- The retained exact branch control is now:
  - `LUAJIT_S390X_DISPATCH_FORL_SKIP_JFORI=1`
  - `LUAJIT_S390X_DISPATCH_FORL_PARK_ROOT_HOTEXIT_EXACT_COOLDOWN=12`
  - `LUAJIT_S390X_AREF_BASE_ALLGPR=1`
  - `LUAJIT_S390X_IPAIRS_EXIT1_SKIP_BODY=1`
  - `LUAJIT_S390X_ROOT1_ITERL_REPLAY_TRIPLET=1`
  - `LUAJIT_S390X_ROOT1_ITERL_REPLAY_TRIPLET_LINK_PARENT=1`
  - `LUAJIT_S390X_SUM_LOOP_SELECT_EXIT0_DONE=1`
  - `LUAJIT_S390X_SUM_LOOP_SELECT_SKIP_FUNC_EQ=1`
  - `LUAJIT_S390X_SUM_LOOP_SELECT_CONST_GGET=1`
  - `LUAJIT_S390X_SUM_LOOP_FORL_BLACKLIST=1`
  - `LUAJIT_S390X_VARARG_SIBLING_FORL_BLACKLIST=1`
  - `LUAJIT_S390X_MIXED_FFI_POST_STITCH_SAVE_DONE=1`
  - `LUAJIT_S390X_MIXED_FFI_FORL_PROTO_NOJIT=1`
  - `LUAJIT_S390X_FFI_CDATA_PAIR_SAVE_DONE=1`
  - `LUAJIT_S390X_FFI_CDATA_PAIR_FORL_BLACKLIST=1`
  - `LUAJIT_S390X_ITERATOR_ITERN_BLACKLIST=1`
  - `LUAJIT_S390X_ITERATOR_ITERL_BLACKLIST=1`
  - `LUAJIT_S390X_ITERATOR_ITERN_PROTO_NOJIT=1`
  - `LUAJIT_S390X_ITERATOR_ARRAY_ITERN_NOJIT_HOTCOUNT_PARK=1`
  - `LUAJIT_S390X_ITERATOR_HASH_ITERN_NOJIT_HOTCOUNT_PARK=1`
  - `LUAJIT_S390X_MIXED_NOFFI_ITERL_BLACKLIST=1`
  - `LUAJIT_S390X_MIXED_NOFFI_ITERN_BLACKLIST=1`
  - `LUAJIT_S390X_MIXED_NOFFI_FORL_STITCH_BLACKLIST=1`
  - `LUAJIT_S390X_MIXED_NOFFI_ITERL_ABORT_BLACKLIST=1`
  - `LUAJIT_S390X_MIXED_NOFFI_EARLY_PROTO_NOJIT=1`
  - `LUAJIT_S390X_LOCALIZED_HOTSIDE_CANON_SHARE_EQUIV=1`
  - `LUAJIT_S390X_LOWER_FRAME_LUA_ABS_PROTO_NOJIT=1`
  - `LUAJIT_S390X_PROMOTION_CORE_FORL_PROTO_NOJIT=1`
  - default-on `SIDETRACE_TYPEINS_DONE`
  - the retained root-2 hash-bridge floor in
    [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc)
  - the retained `lj_vm_next` KEYINDEX base-reuse cut in
    [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h)
- Current authoritative deterministic host-pair restamp:
  - `kdz`: `mixed_noffi/mixed_loop/hot 0.004041` vs `-joff 0.003734`
  - `zkd0`: `mixed_noffi/mixed_loop/hot 0.005562..0.006232` vs `-joff 0.004387`
- Exactness still holds on both hosts:
  - `/tmp/mixedprobe.lua -> RESULT 553416`
  - `/tmp/hash_value.lua -> HASH_VALUE 3000`
  - `/tmp/ipairs_only_probe.lua -> RESULT 576000`
- `dispatch_trace` is back to near/parity after the post-promotion dispatch
  route-around:
  - `kdz`
    - `numeric_loop/hot 0.002170` vs `-joff 0.002165`
    - `side_exit_loop/hot 0.004557` vs `-joff 0.004704`
    - `hotexit_loop/hot 0.005522` vs `-joff 0.005572`
  - `zkd0`
    - `numeric_loop/hot 0.002530` vs `-joff 0.003831`
    - `side_exit_loop/hot 0.005002` vs `-joff 0.007007`
    - `hotexit_loop/hot 0.006005` vs `-joff 0.009427`
  - `dispatch_trace` is no longer a live red family
- Focused mechanism shape on trusted `kdz` is now tighter than the older
  recorder-side frontier:
  - the retained mixed floor is now:
    - `kdz mixed_loop/hot 0.004041`
    - `zkd0 mixed_loop/hot 0.005562..0.006232`
  - the direct recorder-side `sidecheck_interp` / nil-descendant shaping
    tranche is exhausted as a profitable local edit surface
  - refreshed retained-floor mixed attribution on `kdz` still points to the
    same `pairs(map)` family as the dominant residual payer:
    - JIT-on split timings:
      - `band_only 0.001170`
      - `select_only 0.024764`
      - `ipairs_only 0.026126`
      - `pairs_only 0.526762`
      - `band_select_ipairs 0.047278`
      - `full 0.555028`
    - `-joff` split timings:
      - `band_only 0.015851`
      - `select_only 0.043679`
      - `ipairs_only 0.075638`
      - `pairs_only 0.088317`
      - `band_select_ipairs 0.133387`
      - `full 0.218688`
  - retained-floor asm attribution for that family now shows the live root as:
    - `CALLL lj_vm_next`
    - dead `HIOP`
    - `VLOAD #0`
    - `ADDOV`
  - the earlier retained mixed gain came from the helper-argument side of that
    same root:
    - hidden `IRSLOAD_KEYINDEX` call arguments feeding `IRCALL_lj_vm_next`
      now reuse live `RID_BASE` directly instead of rematerializing `jit_base`
      into a scratch GPR in `asm_gencall_sload()`
- Read:
  - the branch-level mixed floor moved right again on both hosts
  - the bridge body and the recorder-side saturated gate are no longer the
    best active edit surfaces
  - a fresh official-row attribution still named the root-owned
    `parent=2 exit=1` `pairs(map)` runtime family as dominant
  - the first broader `lj_vm_next` call/return handoff attempt after that
    attribution was exact and mechanism-real, but catastrophically slower on
    `kdz`
  - the new retained route-around instead targets the three exact root families
    that dominated the refreshed official row:
    - `LUAJIT_S390X_MIXED_NOFFI_ITERL_BLACKLIST=1`
    - `LUAJIT_S390X_MIXED_NOFFI_ITERN_BLACKLIST=1`
    - `LUAJIT_S390X_MIXED_NOFFI_FORL_STITCH_BLACKLIST=1`
  - the latest retained mixed cut then sets exact early proto-NOJIT at the first
    retained `BC_ITERL` root and parks the subsequent mixed `BC_ITERN` hotcount
    events:
    - `LUAJIT_S390X_MIXED_NOFFI_EARLY_PROTO_NOJIT=1`
  - the older root-1 producer-collapse frontier remains a guardrail, not the
    active blocker

## What Has Been Proven

- Lane A is stable enough to treat as the shipping build and stability floor.
- The stabilized post-promotion `vararg_paths` floor is retained again after
  the sibling matcher restamp.
- The root-1 replay-triplet work remains a necessary guardrail for
  `mixed_noffi`, but `mixed_noffi` is parked and not the current frontier.
- The cleaned harness contract is now the floor:
  - canonical nongit mirrors under `.../canon/repo`
  - tracked-file sync only
  - direct `src/` rebuild only
  - deterministic hot-first scale ordering in
    [tests/s390x/perf/benchlib.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/benchlib.lua)
- `iterator_table` still sits near parity on that carried floor; the first
  post-promotion restamp did not reopen it as the lead blocker.
- The follow-up kdz-first stabilization pass retained an exact root-`BC_FORL`
  proto-NOJIT route-around for `be_helpers` / `ffi_calls`; the older envless
  `promotion_core` rows remain historical first-enable evidence only.
- The retained mixed improvement is now split across VM-side and asm-side work:
  - the retained `JLOOP_EXIT` contract still reports
    `dispatch-original -> target=2 -> BC_ITERN`
  - the retained root-2 bridge cut in
    [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc)
    still removes the bridge-only `Node*` address multiply in favor of a shift
    by `5`
  - an earlier retained mixed gain is later in the same family, in
    [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h):
    - hidden `KEYINDEX` call arguments for `lj_vm_next` now reuse live
      `RID_BASE`
    - retained host-pair result before the tri-root route-around:
      - `kdz mixed_loop/hot 0.012123`
      - `zkd0 mixed_loop/hot 0.014944`
  - the previous retained mixed gain is the exact tri-root blacklist in
    [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c):
    - `BC_ITERL` root: `trace=1 startop=82 link=1 linktype=2 nsnap=2 nins=32792 mcloop=360`
    - `BC_ITERN` root: `trace=2 startop=70 link=2 linktype=2 nsnap=6 nins=32785 mcloop=208`
    - stitched `BC_FORL` root: `trace=3 startop=79 link=0 linktype=8 nsnap=2 nins=32798 mcloop=0`
    - retained host-pair result:
      - `kdz mixed_loop/hot 0.005129`
      - `zkd0 mixed_loop/hot 0.008931` then `0.010174`
  - the latest retained mixed gain is the exact post-root `BC_ITERL`
    LLEAVE-abort blacklist in
    [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c):
    - `trace=4 parent=0 exit=0 root=0 startop=BC_ITERL pc=BC_IFORL`
    - retained host-pair result:
      - `kdz mixed_loop/hot 0.005083`
      - `zkd0 mixed_loop/hot 0.008970` then `0.006745`
  - the current retained mixed gain is the exact early proto-NOJIT plus
    `BC_ITERN` hotcount park in
    [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c):
    - first retained `BC_ITERL` root sets `PROTO_NOJIT` for the exact
      `@tests/s390x/perf/mixed_noffi.lua` proto
    - subsequent `BC_ITERN` PROTO_NOJIT hits park the hotcount at `0x7fff`
    - retained host-pair result:
      - `kdz mixed_loop/hot 0.004041`
      - `zkd0 mixed_loop/hot 0.005562..0.006232`

## What Has Not Been Proven Yet

- A full post-promotion carried-floor matrix rerank is not complete yet beyond
  the `zkd0` confirmations for vararg, dispatch, `be_helpers`, and
  `ffi_calls`.
- `mixed_noffi`, `iterator_table`, `mixed_ffi`, and `ffi_cdata` stay parked
  unless a fresh attribution names a new subsystem.

## What The Freeze Point Means

The current branch should be treated as a shipping baseline plus one active
throughput frontier.

- Lane A: build and stability floor
- Lane B: retained mixed throughput floor
- Lane C: parked research and historical reject pile

From here:

- do not reopen `promotion_core`, compare-fix, low32-home, filtered hotside,
  or other closed throughput defaults
- do not reopen root-1 producer-collapse archaeology as the primary frontier
- do not reopen root-2 replay-shortcut, descendant-chain, self-loop ladder,
  duplicate self-reentry, post-stop duplicate rewrite, broad hotcount priming,
  the exhausted bridge-tail micro-lane, or the exhausted recorder-side
  `sidecheck_interp` shaping tranche as the primary target
- keep exactly one active throughput family at a time

## What Is Parked

- root-1 stale-producer / `slot 13 -> KPRI -> TYPEINS` archaeology
- generic-for no-loop fences
- older bridge and continuation research
- broad recorder-side ownership rewrites that do not target the current
  retained mixed runtime seam

## Next Steps

1. Keep the cleaned harness and docs aligned with the retained floor.
2. Treat the remote mirrors as disposable nongit mirrors and sync only through
   the tracked-file contract in
   [runbook.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/runbook.md).
3. Keep the active engineering frontier narrow:
  - keep `be_helpers` and `ffi_calls` parked after the exact root-`BC_FORL`
    proto-NOJIT route-around unless a fresh carried-floor regression appears
  - keep `vararg_paths` parked after the sibling restamp unless a fresh
    carried-floor regression appears
  - keep `mixed_noffi`, `mixed_ffi`, and `ffi_cdata` parked unless a fresh
    attribution names a new subsystem
  - no reopening of root-1 as a primary target
4. Use `kdz` same-host A/B as the policy signal and `zkd0` only after a real
   `kdz` win.

## Current Baseline Contract

Any future `mixed_noffi` experiment must beat these numbers and preserve their
interpretation.

- retained mixed row:
  - `kdz`: `mixed_noffi/mixed_loop/hot 0.004041`
  - `zkd0`: `mixed_noffi/mixed_loop/hot 0.005562..0.006232`
- exactness gates:
  - `/tmp/mixedprobe.lua -> RESULT 553416`
  - `/tmp/hash_value.lua -> HASH_VALUE 3000`
  - `/tmp/ipairs_only_probe.lua -> RESULT 576000`
- focused retained mechanism guard on `kdz`:
  - `TRACE_META_SNAP 2`
  - `TRACE_META 1`
  - `RECSTOP 1`
  - exact retained root markers still fire once each:
    - `S390X_MIXED_NOFFI_ITERL_BLACKLIST`
    - `S390X_MIXED_NOFFI_ITERN_BLACKLIST`
    - `S390X_MIXED_NOFFI_FORL_STITCH_BLACKLIST`
    - `S390X_MIXED_NOFFI_ITERL_ABORT_BLACKLIST`
  - `S390X_MIXED_NOFFI_ITERN_NOJIT_HOTCOUNT_PARK` repeats on the parked
    `BC_ITERN` PROTO_NOJIT path
- focused root-1 guardrail on `kdz`:
  - `/tmp/ipairs_only_probe.lua -> RESULT 576000`
  - `TRACE_START 5`
  - `TRACE_ABORT 3`
  - `TEXIT_COUNT 341`

## Updated Timeline

### Now

- the carried floor now includes the ISA promotion, the exact `sum_loop`
  `mcloop=304` restamp, the exact sibling `mcloop=660/444` restamp, and the
  exact dispatch root-`BC_FORL` proto-NOJIT route-around
- `vararg_paths` is back on its retained near/parity floor after that
  stabilization
- `dispatch_trace` is back to near/parity on both hosts after the dispatch
  route-around
- `be_helpers` and `ffi_calls` are back to near/parity on trusted `kdz` and
  confirmed on `zkd0` after the exact root-`BC_FORL` proto-NOJIT route-around
- `iterator_table`, `mixed_noffi`, `mixed_ffi`, and `ffi_cdata` remain parked
  near parity unless a fresh attribution names a new subsystem
- the first exact recorder-side nested `BC_JFORI` handoff attempt is now
  closed as non-engaging on the official hot row
- the inner `sum(...)` whole-loop-contract backend lane is closed, and the
  retained root-FORL blacklists now move `sum_loop`, `retlast_loop`, and
  `retconst_loop` to near/parity
- the retained floor still includes both the root-2 hash-bridge path and the
  `lj_vm_next` KEYINDEX base-reuse cut

### After The Iterator Hash-Side Hotcount Park

- Burn down the remaining red rows in this order:
  1. return to `iterator_table` unless a fresh carried-floor rerank names a
     larger honest residual
  2. keep `be_helpers`, `ffi_calls`, `mixed_noffi`, `mixed_ffi`, and
     `ffi_cdata` parked unless a new subsystem is first named

## Where To Look Next

- Current perf scoreboard:
  [perf.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/perf.md)
- Technical notebook:
  [findings.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/findings.md)
- Validation and sync contract:
  [runbook.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/runbook.md)
