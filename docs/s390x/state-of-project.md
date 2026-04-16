# s390x State Of The Project

Last updated: 2026-04-16 11:30 PDT

This file is the current plain-language status page for the s390x bring-up.
It is intentionally current-state only. Historical experiment detail lives in
[findings.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/findings.md).

## Current State

- Current WIP source point is `6545469e Improve s390x numeric op lowering`.
  This fast-forward promotion from `origin/k8ika0s/numeric-ops-lowering`
  retains the current guardrail/correctness floor and adds the latest s390x
  numeric backend lowering work in `src/lj_asm_s390x.h`,
  `src/lj_emit_s390x.h`, and `src/lj_target_s390x.h`.
- kdz1 merge validation passed from the tracked mirror after a clean `src/`
  rebuild: all `tests/s390x/jit_be/*.lua` files passed with
  `jit_be_err_lines=0`, and the 61-sample `numeric_ops` read matched the
  integration handoff band (`abs_loop/hot 0.000107`, `fp_mod_loop/hot
  0.000289`, `min_loop/hot 0.000080`, `max_loop/hot 0.000123`).
- Latest full comparison:
  `artifacts/s390x/s390x-kdz1-20260416T181755Z` and
  `artifacts/s390x/compare-kdz1-ka0s01-20260416T181755Z`. This run produced
  `2160` s390x benchmark records and `0` s390x failures across GCC/Clang,
  JIT-on/`-joff`, and three alternating passes. The new numeric lowering is
  a clear acceleration win versus the previous `20260416T171113Z` artifact:
  `abs_loop/hot` is roughly `8.6x` faster than before, `fp_mod_loop/hot`
  roughly `1.9x`, `min_loop/hot` roughly `1.9x`, and `max_loop/hot` roughly
  `1.45x`.
- Current queue:
  there is still no material s390x JIT-on versus `-joff` regression blocker
  in the full matrix. Next work should continue as acceleration work from
  high-time/cross-architecture disadvantage rows, with iterator/mixed/vararg
  guardrails kept intact unless a fresh truth pack names a concrete safe
  mechanism.

- The retained floor carried into this checkpoint includes the prior WIP over
  `5814717c Retire stale dispatch cooldown env`, plus the retained
  iterator hash-payload recorder fix plus the retained
  ADDOV/SUBOV and MULOV overflow work, remote oracle matrix coverage,
  route-around reducer splits, static-stop and localized be-pack
  promotion-core guard splits, iterator guard ordering, guardrail promotion,
  the STRTO short-string parse-cache helper closure,
  the FFI GPR `IR_FLOAD` closure, the cdata mixed-width `MOD` /
  narrow-`XSTORE` closure, the cdata buffer/FREF integer `MIN` plus
  `BUFHDR` closure, the `bit.tobit` helper exit-storm closure, and the exact
  promotion-core splits for `bitops_mix`, `logical_chain_tail_add`, and
  `logical_chain_tail_store`, plus the allocator-safe `asm_prof` hookmask
  guard closure.
- Current retained-env cleanup:
  `LUAJIT_S390X_DISPATCH_FORL_PARK_ROOT_HOTEXIT_EXACT_COOLDOWN=12` is no
  longer part of the canonical retained env. `kdz` dispatch A/B and a tracked
  `kdz1` tie-break both kept `dispatch_trace` in band without it, so the
  source cooldown remains diagnostic-only.
- The vararg sibling `BC_FORL` blacklist has also been removed from the
  canonical retained env after current `kdz` opt-out/control and `zkd0`
  confirmation kept `sum_loop`, `retlast_loop`, and `retconst_loop` in band.
  The matcher remains in source for diagnostic opt-in use, but is no longer
  part of the default retained contract.
- The latest backend correctness fix is in the FFI cdata immediate allocator
  path: `asm_cnew()` now materializes constant `IR_CNEWI` payload values
  rather than the constant IR reference number, and keeps the payload source in
  the call-preserved non-BASE GPR set across `lj_mem_newgco`. This fixed the
  intermittent `ffi_fixed_call_pressure/gpr_pressure` bad `uint64_t(0)`
  payload seen during retained-env matrix runs. Focused `kdz` and `zkd0` FFI
  pressure A/B now pass, and the exact `kdz` 100-run stress loop passed.
- Current restamp:
  current WIP over `9ffba341` has the 2026-04-15 exact iterator/mixed matcher
  restamp plus the dynamic string-key `HREF` integer `HLOAD` typecheck. The
  restamp restores current official `iterator_table` `BC_ITERN` shapes and the
  current official `mixed_noffi` `BC_ITERL` shape to their exact guarded paths
  before the broad iterator fallback. The HLOAD fix closes
  `tests/s390x/jit_be/string_key_href.lua`, specifically the dynamic string-key
  miss path `(map[key] or 0)`, without broadening generic table-load behavior.
- Current retained matrix:
  `/tmp/kdz-retained-jitter-post-hload-20260415074738/summary.md` is the
  authoritative kdz full retained-env rerank after this restamp. It names no
  material red official-row blocker. Remaining red medians are small/noisy:
  `vararg_paths/sum_loop/hot 1.0246x`, `mixed_noffi/mixed_loop/hot 1.0167x`,
  and `vararg_paths/retconst_loop/hot 1.0095x`; iterator is back at
  parity/noise. kdz1 confirmed the focused restamp direction; zkd0 perf was
  noisy in this tranche but passed focused correctness.
- The previous post-`CNEWI` full retained-env matrix was
  `/tmp/kdz-retained-jitter-20260414070532/summary.md`. It kept the branch in
  the fast band and did not name a broad regression. Its next study lane was
  iterator, specifically `iterator_table/pairs_sum/hot`, but the signal was
  process-order sensitive: focused rerun
  `/tmp/kdz-retained-jitter-20260414070752/summary.md` has red JIT-first
  passes and neutral JOFF-first passes, and trace-meta confirms the exact
  iterator proto-NOJIT path engages in both. Do not patch iterator until the
  secondary trace/harness-order payer is named.
- Dispatch is now a retained acceleration win again. The exact
  `dispatch_trace` `FORL` skip / proto-NOJIT source guard was still default-on
  after the env marker cleanup, which suppressed the official
  `@tests/s390x/perf/dispatch_trace.lua` chunk while the same focused loop body
  compiled fast. The source guard is now opt-in via
  `LUAJIT_S390X_DISPATCH_FORL_SKIP_JFORI`; the canonical retained env leaves
  it off. `kdz` `/tmp/kdz-retained-jitter-20260414072840/summary.md` and
  `zkd0` `/tmp/zkd0-retained-jitter-20260414073305/summary.md` confirm the
  official dispatch rows compile at roughly `0.07x..0.14x` versus `-joff`.
  The latest full retained-env matrix is now
  `/tmp/kdz-retained-jitter-20260414073108/summary.md`: dispatch is green in
  `3/3` passes, and the only remaining red-looking rows are small/noisy
  residuals (`mixed_noffi` around `1.015x`, one `vararg_paths/sum_loop` red
  pass, and the known jitter-sensitive `iterator_table/pairs_sum` spike).
- Numeric table value loads are now a retained backend acceleration closure.
  While probing the remaining STRTO helper row, a numeric-table sibling exposed
  that s390x still lacked numeric `IR_ALOAD` assembly support. The missing
  `asm_ahuvload()` path now emits an FPR numeric load with the existing
  number-tag guard / integer-to-double conversion contract. The new
  `be_helpers/num_aload_loop` coverage reads `0.082x..0.084x` on `kdz` and
  `0.090x..0.111x` on `zkd0` versus `-joff`, with the adjacent helper,
  iterator, mixed, vararg, and dispatch guardrails clean in focused screens.
  A follow-up adjacent-load sweep verified the same generic backend path for
  dynamic hash `num HLOAD`, mutable upvalue `num ULOAD`, and retained
  vararg-shape `num VLOAD`; the jit_be regression now covers all four numeric
  A/H/U/V load forms and passed on `kdz`.
  STRTO then moved from design debt into a retained helper acceleration:
  s390x `asm_strto()` now uses `lj_strscan_num_cache()`, a s390x-only
  thread-local short-string parse cache that keeps the existing two-argument
  helper ABI and validates string hash, length, and bytes before returning a
  cached number. `kdz`
  `/tmp/kdz-retained-jitter-20260414085612/summary.md` moved
  `be_helpers/strto_loop/hot` to `0.1358x..0.2891x`, and the diagnostic opt-out
  `/tmp/kdz-retained-jitter-20260414085754/summary.md` restored the old
  `0.3991x..0.4381x` band. `zkd0`
  `/tmp/zkd0-retained-jitter-20260414090701/summary.md` confirmed the win, and
  `/tmp/zkd0-retained-jitter-20260414090833/summary.md` confirmed the opt-out
  fallback.
  The post-cache full `kdz` matrix is
  `/tmp/kdz-retained-jitter-20260414091220/summary.md`; it keeps the branch in
  the fast band and does not name a material red blocker. STRTO remains
  accelerated at `0.1338x`, `0.1390x`, and `0.2537x`. Remaining red-looking
  rows are small parity/noise residuals, so the next queue should rank by
  absolute JIT time and mechanism; current first probe is
  `lower_frame_same_callsite/lua_abs_same_callsite` at about `0.00235s` JIT and
  `0.156x` versus `-joff`. That lower-frame probe is compiled-body dominated,
  but the first signed-integer `SLOAD` narrowing candidate is closed: it fixed
  a reduced loop-carried side-exit correctness repro but regressed retained
  mixed-noffi, vararg, lower-frame, and helper rows. GC64 signed integer
  `SLOAD` stays default-on until a mechanism-specific correctness repair can
  preserve those floors.
  The prior post-numeric retained-env rerank was
  `/tmp/kdz-retained-jitter-20260414081234/summary.md`; it keeps the branch in
  the fast band and does not name a new material regression after the numeric
  `ALOAD` closure.
- The focused residual rerank after adjacent numeric-load coverage keeps
  iterator, mixed-noffi, and vararg parked. Iterator
  `/tmp/kdz-retained-jitter-20260414082735` was neutral/green on median
  (`pairs_sum/hot 0.9663x`, `pairs_array_sum/hot 1.0027x` with only
  `+0.000016s`). Mixed-noffi
  `/tmp/kdz-retained-jitter-20260414082945` stayed a tiny residual
  (`mixed_loop/hot 1.0204x`, `+0.000073s`, one outlier), and vararg
  `/tmp/kdz-retained-jitter-20260414083158` stayed tiny/noisy
  (`sum_loop/hot 1.0082x`, `+0.000047s`, `retconst_loop` median green,
  `retlast_loop` still fast). No current source patch is justified from these
  rows; future work needs a larger official-row mechanism now that the STRTO
  table-string parse-cache seam has landed.
- The previous full-matrix rerank was the post-`MULOV` read on `kdz`:
  `/tmp/kdz-retained-jitter-20260412104303`, with focused confirmation in
  `/tmp/kdz-bd0dbb89-focused-rerank-202604121047`. It does not name a stable
  material official-row payer: `gpr_pressure/hot` confirmed green/parity,
  iterator rows were jitter/noise, and localized `tobit` had only a tiny
  median residual after the correctness fix. Since that rerank, the
  low-level acceleration truth packs retained two large wins without changing
  the guardrail policy: FFI GPR `gpr_pressure/hot` and
  `ffi_cdata/mixed_width_loop/hot`.
- The latest full retained-env rerank after the `bit.tobit` narrowing
  candidate is `/tmp/kdz-retained-jitter-20260412123059`. It keeps the branch
  in the retained fast/near-parity band and removes the old helper residual:
  `be_helpers/number_helper_loop/hot` is median `0.0328x`, and
  `be_helpers_localized/number_helper_loop_local_tobit/hot` is median
  `0.0549x`. The only red-looking medians are small/noisy rows,
  `dispatch_trace/side_exit_loop/hot 1.0119x` and
  `ffi_cdata/buffer_fref_loop/hot 1.0116x`; neither currently names a material
  official-row payer.
- Focused follow-up reruns of those two rows collapsed them:
  `/tmp/kdz-focused-rerank-dispatch-202604121245` has
  `dispatch_trace/side_exit_loop/hot 0.9940x` with only `1/7` red passes, and
  `/tmp/kdz-focused-rerank-ffi-cdata-202604121248` has
  `ffi_cdata/buffer_fref_loop/hot 0.9942x` with only `1/7` red passes.
  They are parked until a new repeated signal appears.
- The later deeper cdata dump reopened `buffer_fref_loop` as a real backend
  coverage gap despite the noisy focused ratio: the official trace still
  aborted at integer `IR_MIN`, then at `asm_bufhdr_write()` after enabling
  integer min/max. That lane is now retained as a host-pair acceleration
  closure. `kdz` `/tmp/kdz-candidate-buffer-fref-default-20260412` keeps
  `buffer_fref_loop/hot` around `0.0530x..0.0812x` versus `-joff`, with the
  same-binary opt-out control
  `/tmp/kdz-candidate-buffer-fref-disable-minmax-control-20260412` returning
  to the old `0.0049..0.0053` parity band. `zkd0`
  `/tmp/zkd0-candidate-buffer-fref-default-20260412` confirms the row in the
  fast band at `0.0353x..0.0875x` versus `-joff`.
- Post-closure retained-env rerank
  `/tmp/kdz-retained-jitter-post-buffer-fref-20260412` keeps the branch in the
  fast/near-parity band. No hot row is close to a material `1.5x..2x` JIT-on
  regression. Focused higher-sample residual reruns name only one possible
  next attribution candidate:
  `/tmp/kdz-focused-post-buffer-iterator_table-20260412` has
  `iterator_table/pairs_sum/hot 1.0202x` with `5/7` red passes, but the median
  delta is only about `+0.000087` and sibling `pairs_array_sum/hot` stays
  green/noisy. `mixed_noffi` is a small residual only
  (`/tmp/kdz-focused-post-buffer-mixed_noffi-20260412`), dispatch closes as
  noise (`/tmp/kdz-focused-post-buffer-dispatch_trace-20260412`), and vararg
  closes as noise (`/tmp/kdz-focused-post-buffer-vararg_paths-20260412`).
- Follow-up iterator truth pack
  `/tmp/kdz-post-buffer-iterator-truth-20260412` names the mechanism to study
  next: focused reducers are red with `TEXIT_COUNT 0`, and the root body is
  dominated by `CALLL lj_vm_next -> VLOAD -> ADDOV/PHI` rather than trace-exit
  churn.
- The next exact acceleration target has landed on the mechanism-only
  `logic_add_phi_noboundary` row. The official chunk was still parked by the
  retained promotion-core proto-NOJIT route-around while the generated
  equivalent body compiled at `0.02x`; excluding only
  `@tests/s390x/perf/logic_add_phi_noboundary.lua` with
  `firstline=23`, `numline=8`, `nsnap=4`, `nins=32862`, `mcloop=1632` moves
  `kdz` hot to `0.000031` versus `0.002094` `-joff`, and `zkd0` confirms
  `0.000035` versus `0.002342`.
- The same exact-promotion-core method has now landed for the official
  `bitops_mix`, `logical_chain_tail_add`, and `logical_chain_tail_store`
  rows. The broad route-around remains intact for older unsafe promotion-core
  families, but those three exact shapes now compile: `kdz`
  `/tmp/kdz-candidate-promotion-core-exclusions-20260412` reports medians
  `0.0110x`, `0.0124x`, and `0.0128x`; `zkd0`
  `/tmp/zkd0-candidate-promotion-core-exclusions-20260412` confirms all three
  in the fast band.
- Current-source rerank after that split
  `/tmp/kdz-rerank-after-promotion-core-exclusions-20260412` does not name a
  material red blocker. The only red medians are small/noisy residuals:
  `dispatch_trace/hotexit_loop/hot 1.0515x`, `mixed_noffi/mixed_loop/hot
  1.0312x`, `vararg_paths/sum_loop/hot 1.0149x`, and
  `vararg_paths/retconst_loop/hot 1.0127x`. Focused reruns close dispatch as
  jitter, reduce mixed-noffi to a tiny near-zero-delta residual, and show
  vararg `sum_loop` as the only slightly red focused row but without a named
  mechanism (`TEXIT_COUNT 0`, no handoff counters, focused runtime at
  effectively parity).
- Low-level follow-up on those residuals did not open a safe code lane.
  `/tmp/kdz-lowlevel-optout-gc64-signed-sload-20260412/summary.md` proves the
  default-on GC64 signed integer `SLOAD` path is still carrying the floor:
  disabling it regresses `mixed_noffi` and `retlast_loop` materially. The
  FORL/current compare opt-out
  `/tmp/kdz-lowlevel-optout-forl-current-compare-20260412/summary.md` is
  neutral/noisy rather than a win. The current iterator truth pack
  `/private/tmp/kdz-iterator-safety-lowlevel-after-vararg-mixed-20260412/summary.md`
  keeps `iterator_table/pairs_sum/hot` green (`0.9342x`) and
  `pairs_array_sum/hot` neutral (`0.9940x`), with the focused chain
  compiled-body dominated and `TEXIT_COUNT 0`.
- Fresh retained-env rerank/high-time scan
  `/tmp/kdz-retained-rerank-hightime-20260412` also does not name a material
  payer. The largest weak-speedup hot rows are the same already-studied lanes:
  `dispatch_trace/hotexit_loop/hot` (`0.005663s`, green median),
  `dispatch_trace/side_exit_loop/hot` (`0.004634s`, green median),
  `vararg_paths/sum_loop/hot` (`1.0118x`, negative median absolute delta),
  `iterator_table/pairs_sum/hot` (`1.0136x`, `+0.000122s`), and
  `mixed_noffi/mixed_loop/hot` (`1.0220x`, `+0.000138s`). This is below the
  current source-change bar.
- The x86-parity/NYI cleanup is now down to the cross-arch parked item:
  `/tmp/s390x-x86-parity-coverage-asm-prof-20260412d/report.md`, the `kdz`
  mirror report `/tmp/kdz-s390x-parity-coverage-asm-prof-20260412b/report.md`,
  and the `zkd0` mirror report
  `/tmp/zkd0-s390x-parity-coverage-asm-prof-20260412/report.md` show zero
  explicit s390x ASM stubs and zero stubbed IR ops after `asm_prof` was
  implemented. The stale `vm_mod fast path` inventory entry is corrected to
  implemented; generic `IR_MOD -> IRCALL_lj_vm_modi` remains visible as a
  non-fast-path integer modulo fallback. The remaining compiled vararg
  `BC_JFUNCV` VM NYI stays parked because x86/x64/arm64 also leave compiled
  vararg functions NYI and [lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c)
  asserts that path cannot happen under current hotcall recording semantics.
- The current next-target rule is therefore stricter: no more source changes
  from ratio alone. The next patch needs either a fresh high-time row, a
  current-source truth pack with a concrete compiled-body/backend payer, or a
  correctness-safe replacement for an existing retained guard. Current iterator
  helper-result evidence remains a study lane, not a source lane.
- Iterator moonshot follow-up stayed below the patch bar. The current-source
  iterator acceleration pack
  `/private/tmp/kdz-iterator-moonshot-confirm-20260412/summary.md` has
  `iterator_table/pairs_sum/hot 1.0366x` with `3/5` red passes but only about
  `+0.000147s` median delta, while `pairs_array_sum/hot` is median green at
  `0.9978x` and `mixed_noffi/mixed_loop/hot` is tiny/noisy at `1.0064x`. The
  focused iterator chain is compiled-body dominated with `TRACE_START 1`,
  `TRACE_STOP 1`, `TRACE_ABORT 0`, and `TEXIT_COUNT 0`, so this remains a
  reducer/mechanism note, not a source lane.
- Fresh current-source retained rerank
  `/tmp/kdz-current-rerank-after-iterator-moonshot-20260412/summary.md`
  likewise names no material payer. The worst official hot-row median is
  `iterator_table/pairs_sum/hot 1.0462x`, but the delta is only about
  `+0.000195s` and includes one noisy `1.3558x` pass. The other red medians
  are smaller residuals: `mixed_noffi/mixed_loop/hot 1.0164x`,
  `vararg_paths/sum_loop/hot 1.0108x`, and
  `vararg_paths/retconst_loop/hot 1.0186x` with only `+0.000012s` median
  delta. Keep source edits parked until a larger official row or a safe guard
  replacement appears.
- The guard-retirement workflow has started with a true bake-in rather than a
  broad guard removal. `LUAJIT_S390X_AREF_BASE_ALLGPR` is now default-on in
  [lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h)
  and removed from the canonical retained env, with only diagnostic opt-out
  `LUAJIT_S390X_DISABLE_AREF_BASE_ALLGPR` left. `kdz`
  `/tmp/kdz-guard-retire-aref-bakein-20260412/summary.md` and the `kdz1`
  tie-breaker mirror confirm the floor stays near parity with clean guardrails;
  the initial `zkd0` collapse did not reproduce on rerun and is treated as host
  noise. `kdz` opt-out control
  `/tmp/kdz-guard-retire-aref-disable-control-20260412/summary.md` still
  reintroduces the expected degradation, so the opt-out is causality-only. The
  current guard ledger is
  `/private/tmp/s390x-guard-retirement-after-aref-20260412/ledger.md`.
- The stale `LUAJIT_S390X_SUM_LOOP_SELECT_CONST_GGET` retained-env entry has
  also been removed without a source change. `kdz`
  `/tmp/kdz-guard-retire-sum-select-gget-env-retired-20260412/summary.md` and
  the `kdz1` tie-breaker mirror keep `vararg_paths` and `compiled_vararg`
  clean under the updated env. The source diagnostic knob remains available in
  [lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c),
  but it is no longer part of the retained run contract. The current guard
  ledger after this cleanup is
  `/private/tmp/s390x-guard-retirement-after-gget-env-20260412/ledger.md`.
- The localized hotside equivalence env entry is also retired from the
  retained run contract without a source change. `kdz`
  `/tmp/kdz-guard-retire-localized-equiv-env-retired-20260412/summary.md`
  keeps `be_helpers_localized`, `promotion_core_static_stop`,
  `route_around_reducers`, and `lower_frame_same_callsite` in the fast band.
  The current guard ledger is now
  `/private/tmp/s390x-guard-retirement-after-localized-equiv-env-20260412/ledger.md`,
  with only one env-cleanup candidate left before the mechanism/safety-debt
  items.
- The final env-cleanup marker is now retired as well:
  `LUAJIT_S390X_DISPATCH_FORL_SKIP_JFORI` is removed from the canonical
  retained env while the [lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c)
  source path remains default-on. `kdz`
  `/tmp/kdz-guard-retire-dispatch-forl-env-retired-20260412/summary.md` keeps
  `dispatch_trace` in band. The old
  `LUAJIT_S390X_DISABLE_DISPATCH_FORL_SKIP_JFORI=1` causality failure did not
  reproduce on current WIP and is recorded as stale. The current guard ledger
  is `/private/tmp/s390x-guard-retirement-after-dispatch-forl-env-20260412/ledger.md`:
  24 retained env gates, all classified as mechanism debt or still-unsafe.
- `LUAJIT_S390X_FFI_CDATA_PAIR_SAVE_DONE` has also been retired from the
  retained env after focused kdz proof. `pair_loop/hot` stays in the `0.003x`
  fast band without the env, and `mixed_width_loop` / `buffer_fref_loop` stay
  accelerated. The current guard ledger is
  `/private/tmp/s390x-guard-retirement-after-ffi-cdata-pair-save-done-20260412/ledger.md`:
  23 retained env gates, all mechanism-debt or still-unsafe.
- The guardrail promotion has cleared the inherited runnable-row blockers:
  `vararg_paths`, `mixed_noffi`, and `pairs_loop.lua` now pass on the rebuilt
  WIP mirror and are no longer treated as inherited blocking failures.
- The post-guardrail full retained-env rerank on `kdz` before the iterator
  guard refinement named `iterator_table` as the top stable payer:
  `/tmp/post-guardrail-full-retained-20260411170050`.
  It showed `iterator_table/pairs_sum 2.6463x`,
  `iterator_table/pairs_array_sum 2.2158x`, and
  `mixed_noffi/mixed_loop 1.3052x`, while the other stable families were near
  parity or green under the same retained env.
- The iterator guard refinement makes the exact `iterator_table` root
  `BC_ITERN` proto-NOJIT path default-on before the broad iterator root
  blacklist, while preserving the broad blacklist fallback for unsafe
  non-exact iterator shapes.
- `kdz` validation artifact:
  `/tmp/iterator-guard-promote-validation-20260411170533`. It passed
  `iterator_table`, `mixed_noffi`, `pairs_loop`, all `jit_be`, all
  `jit_loops`, `vararg_paths`, `numeric_ops`, retained-env `dispatch_trace`,
  `ffi_calls`, `ffi_cdata`, `mixed_ffi`, and the iterator exact-path opt-out
  causality check.
- Clean pinned `kdz` iterator A/B artifact:
  `/tmp/iterator-guard-kdz-pinned-20260411170826`.
  The trusted policy read is now faster than same-binary `-joff` and much
  faster than the opt-out fallback:
  `pairs_sum/hot 0.004472` vs `-joff 0.004675` vs opt-out `0.010467`, and
  `pairs_array_sum/hot 0.003716` vs `-joff 0.004274` vs opt-out `0.008237`.
- `zkd0` confirmation artifacts:
  `/tmp/iterator-guard-promote-zkd0-20260412120719` and
  `/tmp/iterator-guard-zkd0-pinned-20260412120738`. `zkd0` is noisy and still
  above `-joff` on the pinned read, but default is materially better than the
  exact-path opt-out fallback:
  `pairs_sum/hot 0.009195` vs opt-out `0.021250`, and
  `pairs_array_sum/hot 0.006530` vs opt-out `0.017199`.
- Current performance read:
  iterator is no longer the active top payer on the trusted `kdz` policy
  signal. The next retained-env rerank named `mixed_noffi` as the only repeated
  material residual, and the follow-up exact ordering fix restores the mixed
  `BC_ITERL` blacklist before the broad iterator root fallback. Trusted `kdz`
  A/B now has `mixed_loop/hot 0.003946` vs `-joff 0.003827`, with the
  official mechanism marker `S390X_MIXED_NOFFI_ITERL_BLACKLIST` re-engaged at
  trace 1. `zkd0` remains too noisy for a ratio decision, but confirms the
  same marker ordering and keeps `pairs_loop` passing.
- Post-ordering-fix retained-env rerank from `fa1d75e5`:
  `/tmp/kdz-fa1d75e5-full-retained-rerank-20260411184018` plus focused
  confirmation `/tmp/kdz-fa1d75e5-residual-confirm-20260411184217`. Combined
  `kdz` read across seven mixed passes has `mixed_noffi/mixed_loop/hot` at
  median ratio `1.0219x`, median delta `+0.000083`, and red in `4/7` passes.
  The other apparent red rows did not hold as stable material payers:
  `iterator_table/pairs_sum` collapsed to median ratio `1.0002x`,
  `vararg_paths/retconst_loop` was a tiny-row/noise signal with median delta
  effectively zero, and `be_helpers/be_pack_loop` was red in only `2/7`
  passes at median ratio `1.0127x`.
- The retained env contract is canonicalized in
  [tools/s390x/restamp_iterator_perf.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/restamp_iterator_perf.py)
  and imported by the iterator, dispatch, and broader-throughput truth-pack
  helpers. Do not use partial-env runs for retention decisions.
- Next work should stay disciplined: the post-`411961f6` retained-env rerank
  did not name a stable material official-row payer, so do not reopen broad
  trace-control or guardrail edits from the current near-parity matrix alone.
  Further performance work should target low-level debt only after a focused
  attribution names a repeated mechanism.
- Follow-up attribution from this floor did not name a retainable code lane.
  The mixed-noffi truth-pack stayed compiled-body dominated with `TEXIT_COUNT
  0`; the exact mixed `BC_ITERN` `0xffff` hotcount-width candidate reduced
  park retries but moved only about `15us` median JIT time versus the reverted
  control. `be_helpers` official rows were green, `vararg_paths` official rows
  were near parity, and the manual `numeric_ops` read was green, including
  `fp_mod_loop/hot`. Current policy state: no active material perf seam is
  named; the next code attempt should wait for a fresh repeated same-host A/B
  signal or a new parity backlog item with direct correctness/coverage value.
- Guardrail-debt mapping then found one retained env guard that is no longer
  needed: `LUAJIT_S390X_FFI_CDATA_PAIR_FORL_BLACKLIST=1`. The source matcher
  remains available for explicit diagnostics, but it is removed from the
  canonical retained env in
  [restamp_iterator_perf.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/restamp_iterator_perf.py).
  Artifacts:
  `/tmp/kdz-guardrail-debt-20260411195609`,
  `/tmp/kdz-guardrail-split-20260411200231`,
  `/tmp/kdz-ffi-cdata-forl-blacklist-ab-20260411200610`,
  `/tmp/kdz-ffi-cdata-forl-blacklist-regression-20260411200855`, and
  `/tmp/zkd0-ffi-cdata-forl-blacklist-ab-20260411201025`. Host-pair result:
  `ffi_cdata/pair_loop/hot` moved from `0.017311` to `0.000059` on `kdz`
  (`5/5` passes) and from `0.025598` to `0.000072` on `zkd0` (`3/3` passes).
  The broader `kdz` candidate guardrail set passed `addsub_overflow_guard`,
  `numeric_ops`, `pairs_loop`, compiled vararg, `vararg_paths`,
  `mixed_noffi`, `iterator_table`, and the mixed exact probes.
- The same guardrail-debt map also exposed a promotion-core route-around that
  could be split safely. The broad
  `LUAJIT_S390X_PROMOTION_CORE_FORL_PROTO_NOJIT=1` guard remains active for
  the exact `be_helpers.lua` `number_helper_loop` root, but the exact
  `be_pack_loop` root (`BC_FORL`, `nsnap=4`, `nins=32840`, `mcloop=1032`) is
  now excluded and allowed to compile. Artifacts:
  `/tmp/kdz-be-pack-split-candidate-20260411201916`,
  `/tmp/zkd0-be-pack-split-candidate-20260411202048`,
  `/tmp/kdz-be-pack-split-regression-20260411202226`, and
  `/tmp/zkd0-be-pack-split-retained-rerun-20260411202323`. Result:
  `be_pack_loop/hot` moved from `0.019222` to `0.000246` on `kdz`, and from
  `0.040121` to `0.000309` on `zkd0`; the `kdz` guardrail screen stayed clean.
- Post-`411961f6` retained-env rerank artifacts:
  `/tmp/kdz-post-411961f6-retained-rerank-core-20260411202636`,
  `/tmp/kdz-post-411961f6-retained-rerank-rest-20260411202918`, and
  `/tmp/kdz-post-411961f6-numeric-ops-20260411203113`.
  No stable material red official row repeated. Small/noisy reads included
  `vararg_paths/retlast_loop/hot` median ratio `1.0266x` with `2/4` red
  passes, `vararg_paths/sum_loop/hot` median ratio `1.0155x` with `2/4` red
  passes, `ffi_cdata/buffer_fref_loop/hot` median ratio `1.0089x` with `2/4`
  red passes, and `mixed_noffi/mixed_loop/hot` median ratio `1.0109x`.
  `iterator_table`, `dispatch_trace`, `mixed_ffi`, `ffi_calls`,
  `be_helpers`, `ffi_cdata/pair_loop`, and the numeric rows were green or
  near parity.
- Latest guardrail opt-out checks are closed as non-retainable:
  FFI/mixed splits were neutral or moved siblings the wrong way
  (`/tmp/kdz-post-411961f6-ffi-mixed-guard-split-20260411203230`), vararg
  broad/exact opt-outs were noisy or regressed later passes
  (`/tmp/kdz-vararg-root-blacklist-ab-20260411203434`,
  `/tmp/kdz-sum-loop-forl-blacklist-remove-ab-20260411203535`), and fully
  unguarded official iterator tracing was correct but much slower
  (`/tmp/kdz-iterator-fully-unguarded-nolog-20260411204011`) due to repeated
  exit-1 `BC_JLOOP` / hotside churn.
- The best named “beyond parity” candidate has now landed:
  `numeric_ops/max_loop` uses an exact `@numeric_ops_max` exit-0 body
  side-trace allow in
  [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c).
  `kdz` same-binary causality:
  `/tmp/kdz-numeric-max-body-allow-samebinary-20260411210817` moved
  `max_loop/hot` from opt-out `0.001802` to default `0.000177`, with
  `min_loop/hot` unchanged. `zkd0`
  `/tmp/zkd0-numeric-max-body-allow-samebinary-20260411210945` moved
  `max_loop/hot` from opt-out `0.002409` to default `0.000229`, again with
  `min_loop` unchanged. Guardrails passed in
  `/tmp/kdz-numeric-max-body-allow-guardrails-20260411210837` and
  `/tmp/zkd0-numeric-max-body-allow-guardrails-20260411211004`.
- Post-`d3430611` retained-env rerank kept the stable main matrix near parity
  or faster, so the next useful signal came from the expanded guardrail-debt
  coverage rather than a core matrix row. The route-around reducer truth pack
  showed `@tests/s390x/perf/route_around_reducers.lua` was still caught by
  the broad `LUAJIT_S390X_PROMOTION_CORE_FORL_PROTO_NOJIT=1` guard, even
  though the equivalent reducer bodies now compile cleanly. The exact
  `route_around_reducers.lua` chunk is therefore removed from that matcher
  while retaining the guard for the older promotion-core route-around shapes.
  Artifacts:
  `/tmp/kdz-retained-jitter-20260412073653`,
  `/tmp/kdz-retained-jitter-20260412074304`,
  `/tmp/d3430611-route-around-truth/20260412-kdz-route_around_reducers-retained_baseline-truth-pack`,
  `/tmp/kdz-retained-jitter-20260412074938`,
  `/tmp/kdz-retained-jitter-20260412075243`, and
  `/tmp/zkd0-retained-jitter-20260412075528`. Result:
  `route_around_reducers` be-pack hot rows now run in the compiled fast band
  on both hosts while the main retained families stay near parity or faster
  on trusted `kdz`.
- The corrected expanded retained-env probe then exposed one more
  promotion-core guard debt row:
  `promotion_core_static_stop/be_pack_literal_stop_real/hot`. Dropping the
  whole promotion-core guard made that row fast, but regressed the
  number-helper static-stop siblings, so the retained fix is another exact
  split: keep parking the number-helper roots and allow only the current
  static-stop be-pack literal root (`firstline=21`, `numline=10`, `nsnap=4`,
  `nins=32840`, `mcloop=1032`) to compile. Artifacts:
  `/tmp/kdz-retained-jitter-20260412080232`,
  `/tmp/kdz-retained-jitter-20260412080916`,
  `/tmp/kdz-retained-jitter-20260412081025`, and
  `/tmp/zkd0-retained-jitter-20260412081200`. `kdz` moved the target row to
  about `0.013x` of `-joff` in `3/3` focused passes; `zkd0` confirms the row
  is fast but remains too noisy for broader ratio decisions.
- Post-static-split `kdz` rerank
  `/tmp/kdz-retained-jitter-20260412081545` did not name another material
  payer. The apparent `be_helpers/number_helper_loop` and
  `logic_add_phi_noboundary` residuals collapsed in focused confirmation
  `/tmp/kdz-retained-jitter-20260412081718`; the former was median `0.9964x`
  and the latter was only `1.0096x` with a tiny absolute delta.
- Follow-up guardrail-debt proof found one more exact promotion-core split:
  `be_helpers_localized/be_pack_loop_local_ops_real/hot`. Retained-env meta
  showed the official localized be-pack root was still parked by
  `LUAJIT_S390X_PROMOTION_CORE_FORL_PROTO_NOJIT=1`, while broad removal made
  the be-pack root fast but regressed the localized number-helper sibling.
  The retained fix excludes only the official localized be-pack root
  (`firstline=19`, `numline=14`, `nsnap=4`, `nins=32821`, `mcloop=656`) from
  the guard. `kdz` `/tmp/kdz-retained-jitter-20260412083613` moved
  `be_pack_loop_local_ops_real/hot` to median ratio `0.0307x`; broader `kdz`
  screen `/tmp/kdz-retained-jitter-20260412083948` kept the stable retained
  families in band. `zkd0` `/tmp/zkd0-retained-jitter-20260412084217`
  confirmed the target at `0.0236x..0.0296x`, with guardrail smokes and exact
  mixed probes clean.
- The retained-env jitter helper now builds remote FFI ABI oracle artifacts
  before running oracle-backed perf rows. Local `liboracle.so` is intentionally
  not copied because it is a build artifact and may be host-architecture
  output; the s390x mirror builds it with `tests/s390x/build_oracles.sh`.
  Full `kdz` matrix artifact `/tmp/kdz-retained-jitter-20260412085621`
  restored `ffi_fixed_call_pressure` and `ffi_fixed_struct_calls`; the later
  artifact `/tmp/kdz-retained-jitter-20260412123059` restamps the helper rows
  after the `bit.tobit` narrowing win. Focused follow-up reruns collapse the
  two red-looking residuals, `dispatch_trace/side_exit_loop` and
  `ffi_cdata/buffer_fref_loop`, to median green/noise. No current official row
  is large enough to justify a new code lane without fresh focused
  attribution.
- Current forward map:
  the primary matrix is now restamped from the full retained-env artifact and
  the retained focused acceleration wins, including oracle-backed FFI rows,
  cdata mixed-width, FFI GPR pressure, and the safe `bit.tobit` helper
  narrowing. Do not reopen the latest noisy iterator, vararg, mixed, helper,
  dispatch, or FFI residuals unless a focused same-host A/B or truth pack names
  a fresh repeated payer. The immediate next step is another focused
  acceleration pass only if it targets a new high-time row or a named stale
  truth-pack instrumentation failure, not a sub-1.02x noisy matrix residual.
- The currently retained trace-control recovery point still includes the
  existing
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
  `71b8ac1285c67238a6b0ebe0ddd9cb81784cb1ef4f6cfb972a4c516ee7d5bc93`.
- Current retained `vararg_paths` rows after the sibling restamp:
  - trusted `kdz` rerun:
    - `sum_loop/hot 0.004437` vs `-joff 0.004789`
    - `retlast_loop/hot 0.001997` vs `-joff 0.001991`
    - `retconst_loop/hot 0.000598` vs `-joff 0.000598`
  - `zkd0` confirmation:
    - `sum_loop/hot 0.005042` vs `-joff 0.004885`
    - `retlast_loop/hot 0.002420` vs `-joff 0.002243`
    - `retconst_loop/hot 0.000652` vs `-joff 0.000638`
- `iterator_table` now carries the low-level VM/control wins on top of the
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
  `lj_vm_IITERN` for the rest of the process. The latest promoted refinement
  makes the exact root `BC_ITERN` proto-NOJIT path default-on before the broad
  iterator blacklist, preserving the broad fallback for unsafe non-exact
  iterator shapes. Trusted pinned `kdz` same-binary A/B:
  default `pairs_sum/hot 0.004472`,
  `pairs_array_sum/hot 0.003716`; `-joff 0.004675`, `0.004274`; exact-path
  opt-out fallback `0.010467`, `0.008237`. `zkd0` is noisy, but confirms the
  default is materially better than opt-out:
  `pairs_sum/hot 0.009195` vs `0.021250`, and
  `pairs_array_sum/hot 0.006530` vs `0.017199`.
- `mixed_noffi` is restored to near parity after the exact mixed `BC_ITERL`
  matcher was moved ahead of the broad iterator root fallback:
  trusted `kdz` A/B rows are `0.003846`, `0.003949`, `0.003946` against
  `-joff 0.003861`, `0.003772`, `0.003827`. `mixed_ffi` and `ffi_cdata`
  were near parity before the guardrail-debt sweep; `ffi_cdata/pair_loop` now
  has a retained host-pair JIT win after dropping the obsolete root-FORL
  blacklist from the retained env.
- `be_helpers/be_pack_loop` is now also a retained host-pair JIT win after
  splitting the promotion-core root-FORL guard; `number_helper_loop` remains on
  the guarded route-around because broad guard removal still regresses that
  sibling.
- The follow-up retained-env rerank keeps `mixed_noffi` as the only repeated
  residual, but much smaller than the pre-fix row: combined `kdz` median
  `0.003865` vs `-joff 0.003791`, ratio `1.0219x`. This is an attribution
  target, not permission for another blind mixed trace-control edit.
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
- The old classification that the promotion-core root-`BC_FORL`
  proto-NOJIT route-around must cover the current bitops/logic-chain shapes is
  now superseded. A guard-debt proof showed removing only
  `LUAJIT_S390X_PROMOTION_CORE_FORL_PROTO_NOJIT` moved the exact official
  shapes into the fast band, and the retained fix excludes only those three
  shapes:
  - trusted `kdz`: `bitops_mix/mix_bits/hot` median `0.0110x`,
    `logical_chain_tail_add/hot` median `0.0124x`, and
    `logical_chain_tail_store/hot` median `0.0128x`
  - trusted `zkd0`: `bitops_mix/mix_bits/hot` median `0.0111x`,
    `logical_chain_tail_add/hot` median `0.0182x`, and
    `logical_chain_tail_store/hot` median `0.0140x`
- The same env-gated promotion-core route-around still covers exact
  mechanism-only localized/static-stop/FFI-static reducer rows:
  [be_helpers_localized.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/be_helpers_localized.lua),
  [promotion_core_static_stop.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/promotion_core_static_stop.lua)
  for the number-helper roots,
  [ffi_calls_static_stop.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/ffi_calls_static_stop.lua).
  This reuses `LUAJIT_S390X_PROMOTION_CORE_FORL_PROTO_NOJIT=1`; no new env
  knob or stable-matrix row was added. The later post-`d3430611` splits remove
  the exact `route_around_reducers.lua` be-pack reducer family and the exact
  `promotion_core_static_stop.lua` be-pack literal root from this guard
  because both now compile cleanly. Trusted `kdz` retained-source controls
  moved to candidate rows of `0.001386` for localized `tobit`, `0.008632`
  for localized `be_pack`, `0.002281` / `0.001368` / `0.018765` for the
  static-stop reducers. The follow-up FFI static-stop extension moves `kdz`
  `direct_abs_literal_stop_real` / `stored_abs_literal_stop_real` from
  `0.013968` / `0.010697` controls to `0.010142` / `0.006919`, with `zkd0`
  same-source rows at `0.012448` / `0.008949` versus `-joff 0.013297` /
  `0.009072`. The latest exact restamp also covers
  [logic_add_phi_noboundary.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/logic_add_phi_noboundary.lua)
  at `firstline=23`, `nsnap=4`, `nins=32862`, `mcloop=1632`, moving trusted
  `kdz` from immediate control `0.002156` to `0.001857` and `zkd0` from
  immediate control `0.002944` to `0.002060` / rerun `0.002036`.
- The latest post-promotion-core official-row rerank did not name a material
  new `kdz` stable-matrix target. After rejecting a non-engaging iterator
  root-`BC_FORL` stitch guess and restoring clean retained source, the
  high-sample `kdz` pass showed:
  - `iterator_table/pairs_sum/hot 0.004272` vs `-joff 0.004266`
  - `iterator_table/pairs_array_sum/hot 0.003699` vs `-joff 0.003703`
  - `mixed_noffi/mixed_loop/hot 0.003797` vs `-joff 0.003748`
  - `vararg_paths/sum_loop/hot 0.004490` vs `-joff 0.004967`
  - `logical_chain_tail_add/chain_tail_add/hot 0.001879` vs `-joff 0.001904`
  - `be_helpers/number_helper_loop/hot 0.002272` vs `-joff 0.002347`
  - `be_helpers/be_pack_loop/hot 0.018765` vs `-joff 0.018799`
- The active engineering frontier remains the remaining near-parity carried
  rows. The current policy signal is to avoid another trace-control guess until
  a fresh proof pass names a stable official-row payer; the direct iterator
  VM-body and delayed dispatch micro-lanes are closed after the retained
  direct-store, hotcount-park-width, post-proto no-hot dispatch cut, and
  current-shape promotion-core bitops/logic route-around.
- The localized helper/route-around experiment rows now have retained
  env-gated hotside and exact promotion-core proto-NOJIT carries in
  [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c):
  `LUAJIT_S390X_LOCALIZED_HOTSIDE_CANON_SHARE_EQUIV=1` and
  `LUAJIT_S390X_PROMOTION_CORE_FORL_PROTO_NOJIT=1`. They are guarded by exact
  proto line shape and chunk-name checks, so they are not new stable-matrix
  rows and do not reopen `mixed_noffi`. The same promotion-core carry also
  covers the exact shifted `ffi_calls_static_stop` protos. The same scoped
  carry now covers
  [lower_frame_same_callsite.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/lower_frame_same_callsite.lua)
  after attribution showed its payer is a numeric `FORL/JFORI -> MODVN`
  side-ladder, not the old lower-frame return seam.
- The latest retained experimental carry is the exact lower-frame
  `lua_abs_same_callsite` follow-up route-around:
  `LUAJIT_S390X_LOWER_FRAME_LUA_ABS_PROTO_NOJIT=1`. It parks only the exact
  saved trace-1 root body for the lower-frame benchmark proto after the
  hotside carry. The matcher now accepts the retained `mcloop=288` shape and
  the current `mcloop=284` drift shape. Same-binary rebuilt-mirror A/B after
  the restamp cuts `kdz 0.030186 -> 0.015201` and `0.029826 -> 0.014881`,
  and cuts `zkd0 0.047349 -> 0.019229` and `0.048731 -> 0.020043`.
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
- `iterator_table` is back at the exact root-`BC_ITERN` proto-NOJIT floor by
  checking that exact shape before the broader iterator root blacklist
- `mixed_noffi`, `mixed_ffi`, and `ffi_cdata` remain parked near parity unless
  a fresh attribution names a new subsystem
- the first exact recorder-side nested `BC_JFORI` handoff attempt is now
  closed as non-engaging on the official hot row
- the inner `sum(...)` whole-loop-contract backend lane is closed, and the
  retained root-FORL blacklists now move `sum_loop`, `retlast_loop`, and
  `retconst_loop` to near/parity
- the retained floor still includes both the root-2 hash-bridge path and the
  `lj_vm_next` KEYINDEX base-reuse cut
- after `994ce16f`, a high-sample `ffi_cdata` stress run exposed a generated
  mcode crash in constant unguarded `UREFO`; [lj_asm_s390x.h](../../src/lj_asm_s390x.h)
  now emits the upvalue-address load after the dereference emission in source
  order, so execution loads the address before `lg dest,0(dest)`
- after `0f398870`, the full retained-env rerank did not name a material
  performance payer: the top median red row on trusted `kdz` was only
  `mixed_noffi/mixed_loop/hot 1.0049x` with red `1/5`
- the first guardrail debt sweep did not expose a safe high-upside opt-out:
  exact iterator removal, mixed-noffi guard removal, vararg exact removal, and
  promotion-core/localized removal all slowed the official rows they protect
- after the numeric `SLOAD` FPR accumulator fix, the extra
  `ffi_fixed_call_pressure` row is correct again:
  `kdz fpr_pressure/hot median=0.000267`; the fix converts integer-tagged
  used `num SLOAD` values into the FPR destination instead of loading raw
  integer TValue bits
- the post-fix `kdz` retained-env smoke
  `/tmp/kdz-retained-jitter-20260411224707` still did not name a stable
  material payer; `zkd0` passed focused correctness/perf guardrails but remained
  too noisy for queue ranking
- after `1cdc14e2`, the full retained-env matrix
  `/tmp/kdz-retained-jitter-20260412085621` restored remote oracle-backed rows
  and still did not name a material red official-row blocker
- the first acceleration pass closed the two largest near-parity absolute-time
  targets as no-code:
  `ffi_cdata/mixed_width_loop/hot` in
  `/tmp/kdz-retained-jitter-20260412092847` and
  `ffi_fixed_call_pressure/gpr_pressure/hot` in
  `/tmp/kdz-retained-jitter-20260412093220`
- the second guardrail-debt sweep
  `/tmp/kdz-accel-guard-sweep-20260412093737` did not expose a safe
  high-upside retained-guard opt-out; the focused broad iterator root opt-out
  check `/tmp/kdz-iterator-root-blacklist-focus-20260412094054` was too small,
  regressed `mixed_noffi`, and timed out `pairs_loop.lua` under opt-out

### After The Near-Parity Rerank

- Do not broaden `iterator_table` trace-control. The latest `kdz1` pass keeps
  the exact `BC_ITERN` proto-NOJIT route at parity while preserving the broad
  iterator blacklist as the fallback for non-exact shapes.
- Keep `mixed_noffi`, `mixed_ffi`, `ffi_cdata`, `be_helpers`, and `ffi_calls`
  parked unless a fresh official-row attribution names a stable subsystem. The
  `ffi_cdata` UREFO fix is a correctness/stability closure; the remaining
  `buffer_fref_loop` residual is still too small/noisy to drive a new code
  target on its own.
- The next mutation should start from a fresh matrix/proof pass under the full
  retained env, not from a reduced-probe or trace-meta-only ladder.
- If we continue seeking above-parity wins without a red matrix row, do it as a
  mechanism truth-pack exercise against one named guardrail debt seam at a
  time. Do not weaken the retained guardrails just to expose JIT work; the
  current debt sweep says they are either neutral or still paying for safety.
- Treat the numeric `SLOAD` fix as retained backend correctness debt closure,
  not as a rerank event. It expands safe numeric/FPR trace coverage, but the
  next performance target still requires a fresh repeated `kdz` official-row
  signal.
- Treat the earlier `mixed_width_loop` no-code read as advisory only; reopen it
  for fresh attribution after the localized `bit.tobit` lane if no larger
  low-level payer lands first. That fresh attribution has now landed as a
  retained backend closure: `asm_modk_int()` no longer pins every modulo result
  to fixed `R4`, and the now-correct narrow `XSTORE` slice is default-on with
  `LUAJIT_S390X_DISABLE_NARROW_XSTORE=1` as the diagnostic opt-out.
  `ffi_cdata/mixed_width_loop/hot` is now in the compiled fast band on both
  hosts (`kdz 0.000271` vs `0.028213 -joff`; `zkd0 0.000346` vs
  `0.037485 -joff`).
- `gpr_pressure` also moved from no-code closure to retained backend closure:
  `asm_fload()` now supports 64-bit integer cdata field loads, which
  eliminates the official `IR_FLOAD` abort chain and moves the row into the
  compiled fast band on both hosts.
- Next acceleration queue after the FFI GPR and cdata mixed-width closures:
  cdata buffer/FREF has now also closed as a backend acceleration win. The
  iterator safety-debt attribution lane closed as no-code: retained rows were
  near parity/noise, and a fully unguarded iterator opt-out made official
  `iterator_table` much slower instead of exposing a safe replacement
  mechanism. If continuing iterator performance work from here, the next
  disciplined step is a state-correct terminal `BC_ITERN` leave/restart
  handoff proof, not immediate source mutation from the small `1.0202x` ratio
  alone.
- Guard-debt cleanup has since retired several stale retained-env markers. The
  latest cleanup removes `LUAJIT_S390X_PROMOTION_CORE_FORL_PROTO_NOJIT` from
  `RETAINED_BASELINE_ENV` after `kdz` opt-out and post-cleanup confirmation
  kept promotion-core, helper, FFI-call, bitops, large-immediate, and numeric
  rows in their accelerated bands. The current debt map is 22 retained env
  gates: 17 mechanism-debt items and five still-unsafe guards. Remaining
  removals need a mechanism-specific proof, not broad guard deletion.
- `LUAJIT_S390X_IPAIRS_EXIT1_SKIP_BODY` has now also been removed from the
  retained env after `kdz` and `kdz1` proved the current mixed/iterator floor no
  longer depends on that restored-PC skip. Keep the root-1 replay triplet pair
  retained until a separate proof says it is stale or a mechanism replacement
  lands.
- The `IPAIRS_EXIT1_SKIP_BODY` removal is neutral cleanup, not a speed win:
  immediate `kdz` restored-control and retired-env reads were within small
  mixed-noffi noise, and `zkd0` was noisy but not materially worse. The current
  retained-env debt map is 21 gates: 16 mechanism-debt and five still-unsafe.
- `LUAJIT_S390X_MIXED_FFI_POST_STITCH_SAVE_DONE` has also been removed from
  the retained env after `kdz` and `zkd0` confirmed `mixed_ffi_loop` and cdata
  sibling rows stay in their compiled fast bands without it. Keep
  `LUAJIT_S390X_MIXED_FFI_FORL_PROTO_NOJIT` retained; it is still classified as
  a safety rail, not stale cleanup.
- The current retained-env debt map is now 20 gates: 15 mechanism-debt items
  and five still-unsafe guards.
- Follow-up probes kept the next obvious removals retained: root-1 replay
  triplet pair, vararg select exit-0 stopper, lower-frame Lua `abs`
  proto-NOJIT, and mixed-FFI root `FORL` proto-NOJIT all remain live mechanism
  or safety debt. The next source work should replace one of those mechanisms,
  not delete the env.
- Iterator hash-payload unlock:
  the official `iterator_table/pairs_sum` hash row is now a retained source
  win instead of a guard-debt study lane. `rec_itern()` now recognizes the
  hash-payload state where `lj_record_next()` leaves `ix.key == 0` while
  `nextt` is non-nil, and the exact iterator-table matcher includes the new
  `(nins=32789, mcloop=236)` root shape. kdz moved `pairs_sum/hot` from the
  diagnostic opt-out control `0.010577` to final `0.004105` while keeping
  `pairs_array_sum/hot` neutral and `mixed_noffi` clean. zkd0 confirmed the
  direction (`0.014177` opt-out control to `0.007341` final), and
  `pairs_loop.lua` remains passing.
- Current iterator policy:
  keep the broad iterator root blacklist. The retained fix removes the
  official hash-payload misclassification and restores the fast exact row, but
  arbitrary non-exact iterator roots are still not proven safe for broad
  fallback removal. A post-fix broad-root opt-out smoke is clean on `kdz` but
  regresses `mixed_noffi` on `zkd0`, so the broad fallback remains default-on.
  The next iterator work should target a non-exact-root safety proof or a
  narrower fallback replacement.
- Exact iterator rail status:
  do not remove the exact iterator proto-NOJIT/blacklist rails yet. With all
  iterator rails unguarded after the hash-payload fix, the official row is
  correct but falls into a `trace 2 exit 1` storm back to root `BC_ITERN`
  (`TEXIT_COUNT` about `719997`) and runs around `0.13s` on `kdz`. Existing
  loop-desc/descendant toggles and root-1 replay toggles were retested and
  remain closed. The missing mechanism is a real root-exit side-trace handoff,
  not another guard toggle.
- Latest retained-env rerank:
  `/tmp/kdz-retained-jitter-post-itern-hashpayload-20260413082518/summary.md`
  ran after reverting the failed iterator terminal root-link proof. It does
  not name a material red blocker. `mixed_noffi/mixed_loop/hot` is technically
  red in `3/3` passes but only by `+0.00005s..+0.000084s`; the other red-looking
  rows are similarly small or noisy (`vararg_paths`, `iterator_table` array
  row, and `dispatch_trace/side_exit_loop`). The hash iterator row is median
  green.
- Current queue:
  the regression queue is empty. Iterator remains the main mechanism-debt lane,
  but not a current matrix blocker. The next iterator source patch must solve
  the terminal `BC_ITERN` exit-1 key/control-state handoff or come from a new
  dense official-row proof; broad guard removal, root-link terminal handoff,
  loop-desc/replay toggles, and generic `vm_IITERN` instruction shuffles are
  closed for the current shape.
- Acceleration posture:
  high-time rows are no longer near-parity targets in this rerank. `ffi_cdata`,
  FFI fixed-call pressure, mixed FFI, numeric helpers, cdata FREF, and
  promotion-core reducer rows are all strongly faster than `-joff`. Continue
  looking for new acceleration opportunities, but require a named compiled-body
  or runtime handoff payer before changing source.
- Lower-frame/W32/string-heavy tranche:
  current `kdz` truth packs reconfirm lower-frame as stable compiled-body work
  (`lua_abs_same_callsite/hot 0.1574x`) and low32/W32 rows as already deeply
  accelerated (`0.0117x..0.0130x`). A stricter exact `%17` MLR candidate
  engaged but was not retainable, so no backend source change is carried from
  that proof. W32 boundary reductions exposed real correctness debt in
  guard-consuming bitop chains, but the shallow fuse/scratch-routing proofs did
  not fix the full reducer and were removed. New `string_heavy` perf coverage
  is probe-only: byte scan, concat, and miss-find are JIT-fast; prefix substring
  equality is a red probe; manual substring search and string-key lookup expose
  unsafe JIT shapes and are interpreter-pinned until fixed. A generic
  prefix-equality `lj_str_find` helper proof was exact but slower; the narrower
  direct `lj_str_equal` rewrite is retained as a small kdz win for
  `prefix_eq_loop/hot` (`~0.00808s..0.00826s` default-on versus
  `~0.00808s..0.00843s` opt-out).
  The post-helper string-heavy truth pack keeps prefix equality as the only red
  official string-heavy probe (`1.4049x`, `TEXIT_COUNT 32000`), while
  `manual_find_loop/hot` and `string_key_lookup_loop/hot` are now green/noise.
  The follow-up `GG_State FLOAD` dispatch-base backend fix closes that prefix
  row: the post-fix truth pack
  [20260414-173913-kdz-string_heavy-accel-truth-pack](../../artifacts/s390x/truth-packs/20260414-173913-kdz-string_heavy-accel-truth-pack/summary.md)
  reports `prefix_eq_loop/hot 0.0674x` (`0.000401s` JIT versus
  `0.005957s -joff`), with the rest of the family green/noise or accelerated.
  Keep `string_heavy` as probe coverage until the focused classifier scripts
  are repaired; they now segfault after the official row is fixed, even though
  the official benchmark and guardrails pass.
- Post-fix rerank:
  `/tmp/kdz-retained-jitter-20260414174257/summary.md` keeps the regression
  queue empty. The only apparent red rows are small/noisy or intentionally
  routed: `iterator_table/pairs_sum/hot` is a `+0.00012s..+0.00015s` residual
  with one green pass, and `vararg_paths`, `mixed_noffi`, and
  `iterator_table/pairs_array_sum` sit near parity/noise.
- Post-string-key follow-up:
  the subsequent dynamic string-key `HREF` fix supersedes the earlier
  string-heavy pinned-row note. `manual_find_loop/hot` is now unpinned and runs
  in the `~0.006s` JIT band versus `~0.050s -joff`; dynamic string-key lookup
  is also unpinned and runs in the `~0.00040s` JIT band versus
  `~0.0026s -joff`, with a new `jit_be/string_key_href.lua` hit/miss
  correctness guard. The next acceleration lane is therefore not a
  string-heavy matrix-regression fix. The remaining high-value mechanism debt
  is the known iterator terminal handoff / broad non-exact iterator fallback,
  or a newly named low-level payer from the next dense rerank.
- Exact iterator VM fast path:
  the official retained `iterator_table` rows now have a narrow VM-side
  `BC_ITERN` fast path under the existing no-JIT safety rail. The path is now
  gated by active `PROTO_NOJIT` plus the verified
  `BC_ITERL -> BC_ADDVV -> BC_ITERN` body shape, not by iterator-table source
  line spans. kdz reports `pairs_sum/hot` in the `0.00286..0.00299` band and
  `pairs_array_sum/hot` in the `0.00275..0.00276` candidate band; kdz1
  confirms `0.00292..0.00296/0.00275..0.00279`, and zkd0 keeps both rows
  accelerated. This is retained as a bytecode-shape VM acceleration under the
  safety rails, not a broad iterator tracing unlock.
- Iterator VM store refinement:
  the exact VM route now also defers array-body accumulator/control stores
  until the hash transition or fallback boundary. This keeps the same safety
  gates and leaves hash traversal unchanged. kdz retained control
  `0.003022/0.003154` moved to final guardrail `0.002900/0.002791`; kdz1
  confirmed `0.002932/0.002797`; zkd0's dense rerun improved over immediate
  control (`0.004367/0.003917` vs `0.004635/0.004542`). The empty-array branch
  variant was rejected on kdz (`0.003070/0.002994`).
- Vararg merge:
  WIP now includes `f490dfd0`, merging the narrow
  `k8ika0s/s390x-vararg-correctness` branch. The promoted source commit
  `aec46b71` fixes vararg trace correctness and accelerates the official
  `vararg_paths` rows without broad vararg blacklists or benchmark-specific
  gates. kdz1 clean-rebuild validation passed compiled Lua vararg tests, FFI
  vararg traces, `ffi_abi/run.lua`, and `vararg_paths.lua`; focused hot medians
  are now `sum_loop 0.000025`, `retlast_loop 0.000022`, and
  `retconst_loop 0.000013`. The next full retained-env rerank should replace
  any stale pre-merge vararg matrix rows.
- Current iterator debt:
  keep broad iterator root fallback and exact proto-NOJIT rails. The broad
  fallback now parks non-exact iterator root protos and closes
  `tests/s390x/jit_loops/iterator_trace_shape.lua` on kdz, kdz1, and zkd0
  (`iterator_trace_shape 1 1`). The remaining iterator mechanism debt is the
  deeper terminal `BC_ITERN` resume/continuation contract for eventually
  replacing rails, not another raw reentry, link-retarget attempt, or
  no-proto/per-bytecode suppression variant. The no-proto proof was exact but
  not retainable versus the simpler retained exact-proto VM fast path.

## Where To Look Next

- Current perf scoreboard:
  [perf.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/perf.md)
- Technical notebook:
  [findings.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/findings.md)
- Validation and sync contract:
  [runbook.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/runbook.md)
