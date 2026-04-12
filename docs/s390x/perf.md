# s390x Performance Status

Last updated: 2026-04-12 10:50 PDT

## Post-Guardrail Retained Checkpoint

- Current runtime/code source point for this checkpoint:
  `bd0dbb89 Fix s390x guarded MULOV exit state`, including the retained
  route-around reducer, static-stop be-pack, localized be-pack
  promotion-core guard splits, iterator/vararg guardrails, remote oracle
  matrix coverage, and the loop-body guarded `MULOV` exit-state fix. The retained
  env omits the obsolete `LUAJIT_S390X_FFI_CDATA_PAIR_FORL_BLACKLIST` guard,
  keeps the broad promotion-core guard for the retained route-around families,
  excludes the exact `be_helpers.lua` `be_pack_loop` root, the exact
  `route_around_reducers.lua` be-pack reducer family, and the exact
  `promotion_core_static_stop.lua` be-pack literal root, and now excludes the
  exact `be_helpers_localized.lua` `be_pack_loop_local_ops_real` root so
  those shapes can compile.
- Post-`MULOV` retained-env rerank:
  `/tmp/kdz-retained-jitter-20260412104303` ran the full retained matrix on
  `kdz` (`samples=5`, `warmup=2`, three alternating passes). Focused
  confirmation `/tmp/kdz-bd0dbb89-focused-rerank-202604121047` reran the only
  suspicious rows at `samples=9`, `warmup=2`, seven alternating passes.
  Result: no stable material red official row. `gpr_pressure/hot` confirmed
  green/parity (`0.9977x`, `0/7` red), iterator spikes collapsed under
  focused rerun (`pairs_sum/hot` median `0.9726x`), and localized `tobit`
  is a tiny/noisy residual after the correctness fix (`1.0007x` median,
  high run-order jitter). Current queue remains attribution-only until a
  repeated official-row mechanism appears with material absolute time.
- The post-guardrail full retained-env rerank on `kdz` before the iterator
  guard refinement named `iterator_table` as the top stable payer:
  `/tmp/post-guardrail-full-retained-20260411170050`.
  Median hot-row ratios were `iterator_table/pairs_sum 2.6463x`,
  `iterator_table/pairs_array_sum 2.2158x`, and
  `mixed_noffi/mixed_loop 1.3052x`; `vararg_paths`, `dispatch_trace`,
  `mixed_ffi`, `ffi_calls`, `numeric_ops`, and `ffi_cdata` were near parity or
  green under the same retained env.
- The iterator guard promotion makes the exact `iterator_table` root
  `BC_ITERN` proto-NOJIT path default-on before the broad iterator root
  blacklist, while keeping the broad fallback for unsafe non-exact iterator
  shapes.
- `kdz` promotion validation artifact:
  `/tmp/iterator-guard-promote-validation-20260411170533`. It passed
  `iterator_table`, `mixed_noffi`, `pairs_loop`, all `jit_be`, all
  `jit_loops`, `vararg_paths`, `numeric_ops`, retained-env `dispatch_trace`,
  `ffi_calls`, `ffi_cdata`, `mixed_ffi`, and the iterator exact-path opt-out
  causality check.
- Clean pinned `kdz` iterator A/B artifact:
  `/tmp/iterator-guard-kdz-pinned-20260411170826`.
  Default now beats the same-binary `-joff` comparator and is much faster than
  the exact-path opt-out fallback:
  - `pairs_sum/hot`: default `0.004472`, `-joff 0.004675`, opt-out `0.010467`
  - `pairs_array_sum/hot`: default `0.003716`, `-joff 0.004274`, opt-out
    `0.008237`
- `zkd0` confirmation artifacts:
  `/tmp/iterator-guard-promote-zkd0-20260412120719` and
  `/tmp/iterator-guard-zkd0-pinned-20260412120738`. `zkd0` is noisy and still
  above `-joff` on the pinned read, but default is materially better than the
  opt-out fallback:
  `pairs_sum/hot 0.009195` vs opt-out `0.021250`, and
  `pairs_array_sum/hot 0.006530` vs opt-out `0.017199`.
- Current performance read:
  after the iterator guard promotion, the fresh retained-env rerank named
  `mixed_noffi` as the only repeated material residual. The follow-up ordering
  fix restores the exact mixed `BC_ITERL` path ahead of the broad iterator
  fallback and moves `mixed_loop/hot` back to near parity on `kdz`.
- Post-ordering-fix retained-env rerank artifacts:
  `/tmp/kdz-fa1d75e5-full-retained-rerank-20260411184018` and
  `/tmp/kdz-fa1d75e5-residual-confirm-20260411184217`. Combined read:
  `mixed_noffi/mixed_loop/hot` is the only repeated residual with a meaningful
  absolute delta, at median ratio `1.0219x` and median delta `+0.000083`.
  `iterator_table/pairs_sum` collapsed to median ratio `1.0002x` after a
  focused confirmation pass, and `vararg_paths/retconst_loop` showed a noisy
  tiny-row ratio with median delta effectively zero. Current queue from this
  read: re-attribute the remaining `mixed_noffi` compiled-body residual before
  code; keep iterator, vararg, dispatch, ffi, and helper rows parked unless a
  repeated same-host A/B names a larger official-row payer.
- Follow-up attribution closed the immediate small-row candidates rather than
  naming a new code lane. `mixed_noffi` truth-pack
  `/tmp/20260411-kdz-mixed_noffi-retained_baseline-truth-pack` stayed
  compiled-body dominated with `TEXIT_COUNT 0`; the exact mixed `BC_ITERN`
  `0xffff` park-width follow-up was mechanism-valid but too small to retain.
  `be_helpers` truth-pack
  `/tmp/20260411-kdz-be_helpers-retained_baseline-truth-pack` was green on
  official rows, `vararg_paths` truth-pack
  `/tmp/20260411-kdz-vararg_paths-retained_baseline-truth-pack` was near
  parity, and the manual `numeric_ops` read
  `/tmp/kdz-numeric-ops-retained-rerank-20260411190856` was green including
  `fp_mod_loop/hot 0.000540` vs `-joff 0.004580`. Current state: no stable
  material official-row perf target is named from this rerank.
- Guardrail-debt sweep reopened one real retained-route-around debt item:
  `/tmp/kdz-guardrail-debt-20260411195609` and split pass
  `/tmp/kdz-guardrail-split-20260411200231` showed the retained
  `LUAJIT_S390X_FFI_CDATA_PAIR_FORL_BLACKLIST=1` guard had become obsolete.
  Dropping only that env guard keeps the source matcher available for
  diagnostics, but removes it from the canonical retained env. Host-pair A/B:
  `kdz` `/tmp/kdz-ffi-cdata-forl-blacklist-ab-20260411200610`
  moved `pair_loop/hot 0.017311 -> 0.000059` in `5/5` passes; `zkd0`
  `/tmp/zkd0-ffi-cdata-forl-blacklist-ab-20260411201025`
  moved `0.025598 -> 0.000072` in `3/3` passes.
- The same guardrail-debt pass named one high-upside promotion-core debt item.
  The retained `LUAJIT_S390X_PROMOTION_CORE_FORL_PROTO_NOJIT=1` guard stays
  for the exact `number_helper_loop` shape, but the exact
  `be_helpers.lua` / `be_pack_loop` root is now allowed to compile. `kdz`
  `/tmp/kdz-be-pack-split-candidate-20260411201916` moved
  `be_pack_loop/hot 0.019222 -> 0.000246`, and
  `/tmp/kdz-be-pack-split-regression-20260411202226` kept the main
  correctness/perf guardrails clean. `zkd0`
  `/tmp/zkd0-be-pack-split-candidate-20260411202048` moved
  `0.040121 -> 0.000309`; a retained-env rerun
  `/tmp/zkd0-be-pack-split-retained-rerun-20260411202323` kept
  `number_helper_loop` around or faster than `-joff`.
- Post-`411961f6` retained-env rerank:
  `/tmp/kdz-post-411961f6-retained-rerank-core-20260411202636`,
  `/tmp/kdz-post-411961f6-retained-rerank-rest-20260411202918`, and
  `/tmp/kdz-post-411961f6-numeric-ops-20260411203113`.
  The official matrix did not name a stable material red row. The only red
  reads were small and inconsistent: `vararg_paths/retlast_loop` median ratio
  `1.0266x` with `2/4` red passes, `vararg_paths/sum_loop` median ratio
  `1.0155x` with `2/4` red passes, `ffi_cdata/buffer_fref_loop` median ratio
  `1.0089x` with `2/4` red passes, and `mixed_noffi/mixed_loop` median ratio
  `1.0109x`. `iterator_table`, `dispatch_trace`, `mixed_ffi`, `ffi_calls`,
  `be_helpers`, `ffi_cdata/pair_loop`, and the numeric rows were green or
  near parity.
- Follow-up guardrail opt-outs did not name another retained source/env edit.
  FFI/mixed guard splits were neutral or had sibling regressions
  (`/tmp/kdz-post-411961f6-ffi-mixed-guard-split-20260411203230`), broad
  vararg and exact `SUM_LOOP_FORL_BLACKLIST` opt-outs were noisy or regressed
  later passes (`/tmp/kdz-vararg-root-blacklist-ab-20260411203434`,
  `/tmp/kdz-sum-loop-forl-blacklist-remove-ab-20260411203535`), and the fully
  unguarded official iterator path was correct but catastrophically slower
  (`pairs_sum/hot 0.134399`, `pairs_array_sum/hot 0.128479`) because it
  re-entered an exit-1 `BC_JLOOP` / hotside churn path
  (`/tmp/kdz-iterator-fully-unguarded-nolog-20260411204011`).
- Current forward target:
  the previously named low-level `numeric_ops/max_loop` widened-tail payer is
  now closed by an exact exit-0 body side-trace allow in
  [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c).
  Same-binary causality:
  `kdz` `/tmp/kdz-numeric-max-body-allow-samebinary-20260411210817` moved
  `max_loop/hot` from opt-out `0.001802` to default `0.000177`, and `zkd0`
  `/tmp/zkd0-numeric-max-body-allow-samebinary-20260411210945` moved
  `0.002409` to `0.000229`, with `min_loop` unchanged on both hosts.
  Guardrails passed in
  `/tmp/kdz-numeric-max-body-allow-guardrails-20260411210837` and
  `/tmp/zkd0-numeric-max-body-allow-guardrails-20260411211004`. Next
  performance work should rerun the retained-env rerank and choose a fresh
  named payer rather than reopening the latest noisy iterator, vararg, mixed,
  or FFI guard opt-outs.
- Post-`d3430611` rerank and route-around reducer split:
  `/tmp/kdz-retained-jitter-20260412073653` kept the stable main matrix near
  parity or faster, but the expanded extra-family sweep
  `/tmp/kdz-retained-jitter-20260412074304` named
  `route_around_reducers_truth_pack/be_pack_literal_stop/hot` as a retained
  route-around debt item. Focused truth pack
  `/tmp/d3430611-route-around-truth/20260412-kdz-route_around_reducers-retained_baseline-truth-pack`
  showed the official reducer chunk was still caught by
  `LUAJIT_S390X_PROMOTION_CORE_FORL_PROTO_NOJIT=1`, while the equivalent
  temporary reducer body compiled at roughly `0.02x..0.03x` of `-joff`.
  Removing only `route_around_reducers.lua` from the promotion-core matcher
  moved the official hot rows to the compiled fast band:
  `kdz` `/tmp/kdz-retained-jitter-20260412074938` recorded
  `be_pack_literal_stop/hot 0.015x`,
  `be_pack_literal_stop_local_ops/hot 0.034x`, and
  `be_pack_loop_local_ops/hot 0.034x`; `zkd0`
  `/tmp/zkd0-retained-jitter-20260412075528` confirmed about `0.015x`,
  `0.036x`, and `0.035x`. Broader `kdz` screen
  `/tmp/kdz-retained-jitter-20260412075243` kept dispatch, iterator, vararg,
  mixed, FFI, helper, fixed-call-pressure, and numeric families in the
  retained near-parity/faster band.
- Static-stop be-pack literal split:
  after the helper default was corrected to run all known `BENCH_FILES`,
  `/tmp/kdz-retained-jitter-20260412080232` named the mechanism-only
  `promotion_core_static_stop/be_pack_literal_stop_real/hot` row as another
  over-guarded promotion-core shape. Dropping the whole promotion-core guard
  moved that row from `0.018943` to `0.000247`, but regressed the two
  number-helper static-stop siblings. The retained fix therefore excludes only
  the exact `@tests/s390x/perf/promotion_core_static_stop.lua` be-pack literal
  root (`firstline=21`, `numline=10`, `nsnap=4`, `nins=32840`,
  `mcloop=1032`) from the guard. `kdz` focused A/B
  `/tmp/kdz-retained-jitter-20260412080916` moved
  `be_pack_literal_stop_real/hot` to `0.013x` of `-joff` in `3/3` passes, and
  broader screen `/tmp/kdz-retained-jitter-20260412081025` kept the main
  retained families clean. `zkd0` `/tmp/zkd0-retained-jitter-20260412081200`
  confirmed the target row in the fast band, but remains noisy enough that
  `kdz` stays the policy signal.
- Post-static-split rerank:
  corrected full retained-env `kdz` rerank
  `/tmp/kdz-retained-jitter-20260412081545` did not name another material
  payer. The largest apparent red rows were `be_helpers/number_helper_loop`
  and `logic_add_phi_noboundary`, but focused confirmation
  `/tmp/kdz-retained-jitter-20260412081718` collapsed them to median
  `0.9964x` and `1.0096x` respectively, with only tiny absolute deltas. The
  current queue should therefore stay in attribution mode: do not open another
  performance code lane until a fresh same-host A/B or truth pack names a
  larger repeated payer.
- Localized be-pack split:
  focused proof `/tmp/kdz-retained-jitter-20260412082931` plus truth pack
  [20260412-kdz-be_helpers_localized-retained_baseline-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260412-kdz-be_helpers_localized-retained_baseline-truth-pack/summary.md)
  showed `be_helpers_localized/be_pack_loop_local_ops_real/hot` was still
  over-parked by `LUAJIT_S390X_PROMOTION_CORE_FORL_PROTO_NOJIT=1` in the
  official benchmark. Broadly removing the guard moved the be-pack root to
  `0.000256` but regressed the localized number-helper sibling, so the
  retained fix excludes only the exact localized be-pack root
  (`firstline=19`, `numline=14`, `nsnap=4`, `nins=32821`, `mcloop=656`).
  `kdz` `/tmp/kdz-retained-jitter-20260412083613` moved
  `be_pack_loop_local_ops_real/hot` to median ratio `0.0307x`; broader screen
  `/tmp/kdz-retained-jitter-20260412083948` kept stable retained families in
  band. `zkd0` `/tmp/zkd0-retained-jitter-20260412084217` confirmed the
  target row in the fast band (`0.0236x..0.0296x`) while keeping adjacent
  be-pack/static/route rows fast.
- Oracle-backed matrix coverage repair:
  the expanded retained-env helper now builds
  `tests/s390x/ffi_abi/build/liboracle.so` natively on the remote s390x mirror
  whenever `ffi_fixed_call_pressure` or `ffi_fixed_struct_calls` is selected.
  This keeps tracked-file sync correct while avoiding host-built `.so` reuse.
  Full `kdz` artifact `/tmp/kdz-retained-jitter-20260412085621` now includes
  those rows. The primary matrix below has been restamped from that artifact;
  the only red-ish rows are tiny/noisy deltas, while the fixed-call and
  fixed-struct oracle rows are mostly deep in the compiled fast band.

## Canonical Perf Suite

There is no existing repo-wide cross-architecture perf matrix checked into
this tree. The stable standard already in this branch is the fixed
`tests/s390x/perf/*.lua` suite, all built on
[benchlib.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/benchlib.lua)
with the same `family/workload/scale` schema. This suite list should stay
stable unless a workload is intentionally added or retired.

For scale-based suites, the checked-in policy order is now `hot` first via
[benchlib.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/benchlib.lua)
`bench.scale_order(scales)`. Earlier `pairs(scales)` runs were not stable
enough for retained policy rows.

| Suite file | Family | Workloads | Role in the matrix |
| --- | --- | --- | --- |
| [tests/s390x/perf/iterator_table.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/iterator_table.lua) | `iterator_table` | `pairs_sum`, `pairs_array_sum` | retained exact root-ITERN / root-ITERL blacklist wins plus root-ITERN proto-NOJIT fast fallback, hash/array-side hotcount parks, direct `BC_ITERN` array-slot store cut, and delayed post-proto `BC_ITERN` no-hot dispatch; at parity |
| [tests/s390x/perf/mixed_noffi.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/mixed_noffi.lua) | `mixed_noffi` | `mixed_loop` | retained exact root `BC_ITERL` / `BC_ITERN` / stitched `BC_FORL` blacklists, exact post-root `BC_ITERL` abort blacklist, and exact early proto-NOJIT / `BC_ITERN` hotcount park; now near parity |
| [tests/s390x/perf/mixed_ffi.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/mixed_ffi.lua) | `mixed_ffi` | `mixed_ffi_loop` | retained post-stitch save-time win plus exact root-FORL proto-NOJIT fallback; now near parity and a regression screen |
| [tests/s390x/perf/be_helpers.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/be_helpers.lua) | `be_helpers` | `number_helper_loop`, `be_pack_loop`, `strto_loop` | helper-heavy carried-floor controls; exact number-helper root remains guarded, while the exact `be_pack_loop` root is now allowed to compile after guardrail-debt proof |
| [tests/s390x/perf/numeric_ops.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/numeric_ops.lua) | `numeric_ops` | `abs_loop`, `div_loop`, `fp_mod_loop`, `sqrt_loop`, `min_loop`, `max_loop` | numeric backend/control suite; `max_loop` now carries the exact widened-tail exit-0 body side-trace allow, while FP modulo and integer overflow guards remain regression screens |
| [tests/s390x/perf/ffi_calls.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/ffi_calls.lua) | `ffi_calls` | `direct_abs`, `stored_abs` | call-heavy carried-floor controls; stabilized after post-promotion drift with the same exact root-`BC_FORL` proto-NOJIT route-around |
| [tests/s390x/perf/bitops_mix.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/bitops_mix.lua) | `bitops_mix` | `mix_bits` | helper-light logic/bitops control |
| [tests/s390x/perf/logical_chain_tail_add.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/logical_chain_tail_add.lua) | `logical_chain_tail_add` | `chain_tail_add` | recurring logic-chain sibling |
| [tests/s390x/perf/logical_chain_tail_store.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/logical_chain_tail_store.lua) | `logical_chain_tail_store` | `chain_tail_store` | recurring logic-chain sibling |
| [tests/s390x/perf/dispatch_trace.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/dispatch_trace.lua) | `dispatch_trace` | `numeric_loop`, `side_exit_loop`, `hotexit_loop` | dispatch-side mechanism suite; repaired after the post-promotion collapse with an exact root-`BC_FORL` proto-NOJIT route-around |
| [tests/s390x/perf/ffi_cdata.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/ffi_cdata.lua) | `ffi_cdata` | `pair_loop`, `mixed_width_loop`, `buffer_fref_loop` | retained pair-loop save-time win; the older exact root-FORL blacklist is now retired from the retained env after host-pair guardrail-debt proof |
| [tests/s390x/perf/vararg_paths.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/vararg_paths.lua) | `vararg_paths` | `sum_loop`, `retlast_loop`, `retconst_loop` | vararg regression suite; post-promotion sibling matcher restamp restored the carried near/parity floor |
| [tests/s390x/perf/lower_frame_same_callsite.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/lower_frame_same_callsite.lua) | `lower_frame_same_callsite` | `const_same_callsite`, `lua_abs_same_callsite` | callsite/lower-frame regression suite |
| [tests/s390x/perf/promotion_core_static_stop.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/promotion_core_static_stop.lua) | `promotion_core_static_stop` | `number_helper_literal_stop_real`, `number_helper_literal_stop_real_local_tobit`, `be_pack_literal_stop_real` | static-stop mechanism suite; exact be-pack literal root is now excluded from the broad promotion-core proto-NOJIT guard and allowed to compile |
| [tests/s390x/perf/ffi_calls_static_stop.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/ffi_calls_static_stop.lua) | `ffi_calls_static_stop` | `direct_abs_literal_stop_real`, `stored_abs_literal_stop_real` | static-stop FFI regression suite |
| [tests/s390x/perf/ffi_fixed_call_pressure.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/ffi_fixed_call_pressure.lua) | `ffi_fixed_call_pressure` | `gpr_pressure`, `fpr_pressure` | fixed-call ABI pressure coverage; requires remote native `liboracle.so` build |
| [tests/s390x/perf/ffi_fixed_struct_calls.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/ffi_fixed_struct_calls.lua) | `ffi_fixed_struct_calls` | fixed small/HFA struct call variants | fixed-struct ABI call coverage; requires remote native `liboracle.so` build |
| [tests/s390x/perf/be_helpers_localized.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/be_helpers_localized.lua) | `be_helpers_localized` | `number_helper_loop_local_tobit`, `be_pack_loop_local_ops_real` | localized helper experiments; exact be-pack root is now excluded from the broad promotion-core proto-NOJIT guard while the number-helper root remains guarded |
| [tests/s390x/perf/route_around_reducers.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/route_around_reducers.lua) | `route_around_reducers_truth_pack` | `be_pack_literal_stop`, `be_pack_literal_stop_local_ops`, `be_pack_loop_local_ops` | reducer route-around experiments; exact be-pack reducer family is now excluded from the broad promotion-core proto-NOJIT guard and allowed to compile |
| [tests/s390x/perf/int_add_phi_only.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/int_add_phi_only.lua) | `int_add_phi_only` | `add_phi_only` | narrow integer-phi experiment |
| [tests/s390x/perf/logic_add_phi_noboundary.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/logic_add_phi_noboundary.lua) | `logic_add_phi_noboundary` | `logic_add_phi_noboundary` | narrow logic-phi experiment |

## How To Read This Page

- The top matrix is the current retained row for each stable carried workload.
- The pinned blocker table is the short view for what still hurts most.
- Historical host tables later in the file are evidence snapshots for a
  specific host and candidate surface. They are not the current matrix.
- Experimental and mechanism-only suites stay out of the main matrix even when
  they have dramatic ratios.

## Full Stable Matrix

This is the current retained matrix for the stable carried workloads. If a
workload belongs to the carried suite, it should have one row here even if the
number is ugly.

Current source: `/tmp/kdz-retained-jitter-20260412085621`, full retained env,
all known `probe_retained_jitter.py` families, `S390X_PERF_SAMPLES=5`,
`S390X_PERF_WARMUP=2`, three alternating passes. Oracle-backed FFI rows are
included via a native remote `tests/s390x/build_oracles.sh` build.

| Workload | Family | Current retained JIT-on | `-joff` | Gap / Ratio | Host | Captured | Current state |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `pairs_sum/hot` | `iterator_table` | `0.004437` | `0.004129` | `+0.000308`, `1.0004x` | `kdz` | `2026-04-12 08:58 PDT` | exact iterator guard path retained; one noisy pass, median at parity |
| `pairs_array_sum/hot` | `iterator_table` | `0.003683` | `0.003668` | `+0.000015`, `1.0055x` | `kdz` | `2026-04-12 08:58 PDT` | exact iterator guard path retained; median near parity |
| `mixed_loop/hot` | `mixed_noffi` | `0.003867` | `0.003807` | `+0.000060`, `1.0171x` | `kdz` | `2026-04-12 08:58 PDT` | exact mixed `BC_ITERL` ordering retained; tiny residual only |
| `mixed_ffi_loop/hot` | `mixed_ffi` | `0.000824` | `0.012084` | `-0.011260`, `0.0682x` | `kdz` | `2026-04-12 08:58 PDT` | compiled fast band after guardrail promotions |
| `number_helper_loop/hot` | `be_helpers` | `0.002277` | `0.002229` | `+0.000048`, `1.0181x` | `kdz` | `2026-04-12 08:58 PDT` | guarded route-around retained for this shape; tiny residual |
| `be_pack_loop/hot` | `be_helpers` | `0.000247` | `0.018806` | `-0.018559`, `0.0131x` | `kdz` | `2026-04-12 08:58 PDT` | exact be-pack root allowed to compile |
| `strto_loop/hot` | `be_helpers` | `0.003479` | `0.008361` | `-0.004882`, `0.4170x` | `kdz` | `2026-04-12 08:58 PDT` | STRTO backend row green |
| `direct_abs/hot` | `ffi_calls` | `0.000282` | `0.010242` | `-0.009960`, `0.0275x` | `kdz` | `2026-04-12 08:58 PDT` | FFI call lowering fast band |
| `stored_abs/hot` | `ffi_calls` | `0.000281` | `0.007012` | `-0.006731`, `0.0401x` | `kdz` | `2026-04-12 08:58 PDT` | FFI call lowering fast band |
| `mix_bits/hot` | `bitops_mix` | `0.000254` | `0.002140` | `-0.001886`, `0.1187x` | `kdz` | `2026-04-12 08:58 PDT` | logic/bitops control green |
| `chain_tail_add/hot` | `logical_chain_tail_add` | `0.000242` | `0.002143` | `-0.001901`, `0.1129x` | `kdz` | `2026-04-12 08:58 PDT` | logic-chain add control green |
| `chain_tail_store/hot` | `logical_chain_tail_store` | `0.000166` | `0.002002` | `-0.001836`, `0.0829x` | `kdz` | `2026-04-12 08:58 PDT` | logic-chain store control green |
| `numeric_loop/hot` | `dispatch_trace` | `0.002206` | `0.002197` | `+0.000009`, `1.0023x` | `kdz` | `2026-04-12 08:58 PDT` | dispatch route-around retained; parity |
| `side_exit_loop/hot` | `dispatch_trace` | `0.004612` | `0.004654` | `-0.000042`, `0.9916x` | `kdz` | `2026-04-12 08:58 PDT` | dispatch side-exit row green |
| `hotexit_loop/hot` | `dispatch_trace` | `0.005724` | `0.005709` | `+0.000015`, `1.0030x` | `kdz` | `2026-04-12 08:58 PDT` | dispatch hotexit row parity |
| `max_loop/hot` | `numeric_ops` | `0.000176` | `0.002608` | `-0.002432`, `0.0675x` | `kdz` | `2026-04-12 08:58 PDT` | exact max body side-trace allow retained |
| `pair_loop/hot` | `ffi_cdata` | `0.000056` | `0.017314` | `-0.017258`, `0.0033x` | `kdz` | `2026-04-12 08:58 PDT` | obsolete cdata FORL guard retired; compiled fast band |
| `mixed_width_loop/hot` | `ffi_cdata` | `0.028289` | `0.028265` | `+0.000024`, `0.9976x` | `kdz` | `2026-04-12 08:58 PDT` | mixed-width cdata row at parity |
| `buffer_fref_loop/hot` | `ffi_cdata` | `0.004861` | `0.004934` | `-0.000073`, `0.9787x` | `kdz` | `2026-04-12 08:58 PDT` | FREF coverage row green |
| `sum_loop/hot` | `vararg_paths` | `0.004328` | `0.004329` | `-0.000001`, `1.0002x` | `kdz` | `2026-04-12 08:58 PDT` | vararg sum row parity under full retained env |
| `retlast_loop/hot` | `vararg_paths` | `0.002050` | `0.001975` | `+0.000075`, `1.0169x` | `kdz` | `2026-04-12 08:58 PDT` | tiny/noisy residual only |
| `retconst_loop/hot` | `vararg_paths` | `0.000548` | `0.000554` | `-0.000006`, `0.9734x` | `kdz` | `2026-04-12 08:58 PDT` | retconst row green |
| `gpr_pressure/hot` | `ffi_fixed_call_pressure` | `0.024893` | `0.024699` | `+0.000194`, `1.0079x` | `kdz` | `2026-04-12 08:58 PDT` | remote `liboracle.so` build now included; small one-pass residual only |
| `fpr_pressure/hot` | `ffi_fixed_call_pressure` | `0.000266` | `0.011621` | `-0.011355`, `0.0229x` | `kdz` | `2026-04-12 08:58 PDT` | remote `liboracle.so` build now included; fast band |
| `small_u32_call/hot` | `ffi_fixed_struct_calls` | `0.000416` | `0.010782` | `-0.010366`, `0.0390x` | `kdz` | `2026-04-12 08:58 PDT` | remote `liboracle.so` build now included; fixed-struct call fast band |
| `small_u64_call/hot` | `ffi_fixed_struct_calls` | `0.000431` | `0.010767` | `-0.010336`, `0.0400x` | `kdz` | `2026-04-12 08:58 PDT` | remote `liboracle.so` build now included; fixed-struct call fast band |
| `one_float_call/hot` | `ffi_fixed_struct_calls` | `0.000195` | `0.006208` | `-0.006013`, `0.0314x` | `kdz` | `2026-04-12 08:58 PDT` | remote `liboracle.so` build now included; fixed-struct call fast band |
| `one_double_call/hot` | `ffi_fixed_struct_calls` | `0.000173` | `0.006166` | `-0.005993`, `0.0281x` | `kdz` | `2026-04-12 08:58 PDT` | remote `liboracle.so` build now included; fixed-struct call fast band |
| `big_pair_call/hot` | `ffi_fixed_struct_calls` | `0.000429` | `0.012778` | `-0.012349`, `0.0334x` | `kdz` | `2026-04-12 08:58 PDT` | remote `liboracle.so` build now included; fixed-struct call fast band |
| `hfa2d_call/hot` | `ffi_fixed_struct_calls` | `0.000214` | `0.008441` | `-0.008227`, `0.0260x` | `kdz` | `2026-04-12 08:58 PDT` | remote `liboracle.so` build now included; fixed-struct call fast band |
| `small_u32_take6/hot` | `ffi_fixed_struct_calls` | `0.000647` | `0.021029` | `-0.020382`, `0.0305x` | `kdz` | `2026-04-12 08:58 PDT` | remote `liboracle.so` build now included; fixed-struct call fast band |
| `small_u32_take7/hot` | `ffi_fixed_struct_calls` | `0.000873` | `0.023503` | `-0.022630`, `0.0374x` | `kdz` | `2026-04-12 08:58 PDT` | remote `liboracle.so` build now included; fixed-struct call fast band |
| `small_u64_take6/hot` | `ffi_fixed_struct_calls` | `0.000695` | `0.021324` | `-0.020629`, `0.0326x` | `kdz` | `2026-04-12 08:58 PDT` | remote `liboracle.so` build now included; fixed-struct call fast band |
| `small_u64_take7/hot` | `ffi_fixed_struct_calls` | `0.000947` | `0.023701` | `-0.022754`, `0.0400x` | `kdz` | `2026-04-12 08:58 PDT` | remote `liboracle.so` build now included; fixed-struct call fast band |
| `one_double_take6/hot` | `ffi_fixed_struct_calls` | `0.000383` | `0.018252` | `-0.017869`, `0.0210x` | `kdz` | `2026-04-12 08:58 PDT` | remote `liboracle.so` build now included; fixed-struct call fast band |
| `one_double_take7/hot` | `ffi_fixed_struct_calls` | `0.000726` | `0.021625` | `-0.020899`, `0.0336x` | `kdz` | `2026-04-12 08:58 PDT` | remote `liboracle.so` build now included; fixed-struct call fast band |

## Pinned Recurring Workloads

This is the top progress view. It is not “best-only” anymore, and it is split
so the live blockers stay visually dominant.

### Gate And Blocker Workloads

These rows should remain at the top until the branch-level gaps materially
shrink.

| Workload | Family | Current retained JIT-on | `-joff` | Gap / Ratio | Host | Captured | Status / Notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `pairs_sum/hot` | `iterator_table` | `0.004437` | `0.004129` | `+0.000308`, `1.0004x` | `kdz` | `2026-04-12 08:58 PDT` | exact root-`BC_ITERN` proto-NOJIT path retained; one noisy pass, median at parity |
| `pairs_array_sum/hot` | `iterator_table` | `0.003683` | `0.003668` | `+0.000015`, `1.0055x` | `kdz` | `2026-04-12 08:58 PDT` | exact root-`BC_ITERN` proto-NOJIT path retained; median near parity |
| `mixed_loop/hot` | `mixed_noffi` | `0.003867` | `0.003807` | `+0.000060`, `1.0171x` | `kdz` | `2026-04-12 08:58 PDT` | exact mixed `BC_ITERL` blacklist still runs before broad iterator fallback; tiny residual only |
| `numeric_loop/hot` | `dispatch_trace` | `0.002206` | `0.002197` | `+0.000009`, `1.0023x` | `kdz` | `2026-04-12 08:58 PDT` | exact dispatch route-around retained; parity |
| `side_exit_loop/hot` | `dispatch_trace` | `0.004612` | `0.004654` | `-0.000042`, `0.9916x` | `kdz` | `2026-04-12 08:58 PDT` | exact dispatch route-around retained; green |
| `hotexit_loop/hot` | `dispatch_trace` | `0.005724` | `0.005709` | `+0.000015`, `1.0030x` | `kdz` | `2026-04-12 08:58 PDT` | exact dispatch route-around retained; parity |
| `sum_loop/hot` | `vararg_paths` | `0.004328` | `0.004329` | `-0.000001`, `1.0002x` | `kdz` | `2026-04-12 08:58 PDT` | vararg sum row at parity under full retained env |

### Regression And Control Workloads

These rows stay pinned too, but they are controls and regression screens rather
than the primary “still slow” blockers.

| Workload | Family | Current retained JIT-on | `-joff` | Gap / Ratio | Host | Captured | Status / Notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `number_helper_loop/hot` | `be_helpers` | `0.002277` | `0.002229` | `+0.000048`, `1.0181x` | `kdz` | `2026-04-12 08:58 PDT` | exact root-`BC_FORL` proto-NOJIT route-around retained; tiny residual |
| `be_pack_loop/hot` | `be_helpers` | `0.000247` | `0.018806` | `-0.018559`, `0.0131x` | `kdz` | `2026-04-12 08:58 PDT` | exact be-pack root allowed to compile |
| `direct_abs/hot` | `ffi_calls` | `0.000282` | `0.010242` | `-0.009960`, `0.0275x` | `kdz` | `2026-04-12 08:58 PDT` | FFI call lowering fast band |
| `stored_abs/hot` | `ffi_calls` | `0.000281` | `0.007012` | `-0.006731`, `0.0401x` | `kdz` | `2026-04-12 08:58 PDT` | FFI call lowering fast band |
| `mix_bits/hot` | `bitops_mix` | `0.000254` | `0.002140` | `-0.001886`, `0.1187x` | `kdz` | `2026-04-12 08:58 PDT` | retained logic/bitops control |
| `chain_tail_add/hot` | `logical_chain_tail_add` | `0.000242` | `0.002143` | `-0.001901`, `0.1129x` | `kdz` | `2026-04-12 08:58 PDT` | retained logic-chain add control |
| `chain_tail_store/hot` | `logical_chain_tail_store` | `0.000166` | `0.002002` | `-0.001836`, `0.0829x` | `kdz` | `2026-04-12 08:58 PDT` | retained logic-chain store control |

Pinned-workload rules from here:

- Do not add or remove rows casually.
- Keep the same recurring workloads at the top even when they look good or bad.
- If a row changes meaning, record the reason explicitly in `Status / Notes`.
- Put one-off experiments and branch-only candidates in the chronological log
  below, not in the pinned row set.

## Experimental And Mechanism-Only Suites

These checked-in perf files are real tests, but they are not part of the
stable carried matrix. Their rows later in this document are mechanism or
experiment evidence, not top-level progress rows.

| Suite file | Family | Why it is not in the stable matrix |
| --- | --- | --- |
| [tests/s390x/perf/be_helpers_localized.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/be_helpers_localized.lua) | `be_helpers_localized` | localized helper experiments; now covered by the env-gated localized hotside carry and exact promotion-core proto-NOJIT extension, but still not a stable matrix row |
| [tests/s390x/perf/promotion_core_static_stop.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/promotion_core_static_stop.lua) | `promotion_core_static_stop` | static-stop mechanism suite; number-helper roots remain covered by the exact promotion-core proto-NOJIT extension, while the exact be-pack literal root is now allowed to compile; still not a stable matrix row |
| [tests/s390x/perf/ffi_calls_static_stop.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/ffi_calls_static_stop.lua) | `ffi_calls_static_stop` | static-stop FFI mechanism suite; now covered by the exact promotion-core proto-NOJIT extension, but still not a stable matrix row |
| [tests/s390x/perf/route_around_reducers.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/route_around_reducers.lua) | `route_around_reducers_truth_pack` | route-around experiment family; exact be-pack reducer family is no longer covered by the promotion-core proto-NOJIT extension, but still remains outside the stable matrix |
| [tests/s390x/perf/int_add_phi_only.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/int_add_phi_only.lua) | `int_add_phi_only` | narrow experiment-only control |
| [tests/s390x/perf/logic_add_phi_noboundary.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/logic_add_phi_noboundary.lua) | `logic_add_phi_noboundary` | narrow experiment-only control; now covered by the exact promotion-core proto-NOJIT extension, but still not a stable matrix row |
| [tests/s390x/perf/lower_frame_same_callsite.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/lower_frame_same_callsite.lua) | `lower_frame_same_callsite` | lower-frame regression suite; now covered by the env-gated localized hotside carry, but still not a stable matrix row |

Current localized / route-around mechanism carries:

- exact env:
  - `LUAJIT_S390X_LOCALIZED_HOTSIDE_CANON_SHARE_EQUIV=1`
  - `LUAJIT_S390X_LOWER_FRAME_LUA_ABS_PROTO_NOJIT=1` for the exact
    lower-frame `lua_abs_same_callsite` follow-up route-around only
- exact scope:
  - [tests/s390x/perf/be_helpers_localized.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/be_helpers_localized.lua)
  - [tests/s390x/perf/promotion_core_static_stop.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/promotion_core_static_stop.lua)
  - [tests/s390x/perf/route_around_reducers.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/route_around_reducers.lua)
  - [tests/s390x/perf/lower_frame_same_callsite.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/lower_frame_same_callsite.lua)
- trusted `kdz` signal after the tightened bench-file/line-shape gate:
  - `be_helpers_localized/number_helper_loop_local_tobit/hot`:
    `0.337923 -> 0.009042`
  - `be_helpers_localized/be_pack_loop_local_ops_real/hot`:
    `0.352360 -> 0.013344`
  - `route_around_reducers_truth_pack/be_pack_literal_stop_local_ops/hot`:
    `2.989597 -> 0.032992`
  - `route_around_reducers_truth_pack/be_pack_loop_local_ops/hot`:
    `1.922286 -> 0.033094`
  - `lower_frame_same_callsite/lua_abs_same_callsite/hot`:
    `0.058123 -> 0.049683` with the hotside carry, then
    `0.048729 -> 0.015022` with the exact proto-NOJIT follow-up on the same
    rebuilt mirror; after current-source drift from `mcloop=288` to
    `mcloop=284`, the restamped matcher reads `0.030186 -> 0.015201` and
    `0.029826 -> 0.014881` on `kdz`
- host-pair `zkd0` signal:
  - `be_helpers_localized/number_helper_loop_local_tobit/hot`:
    `0.705713 -> 0.013471`
  - `be_helpers_localized/be_pack_loop_local_ops_real/hot`:
    `0.614358 -> 0.015366`
  - `route_around_reducers_truth_pack/be_pack_literal_stop_local_ops/hot`:
    `3.614130 -> 0.038311`
  - `route_around_reducers_truth_pack/be_pack_loop_local_ops/hot`:
    `2.340587 -> 0.048359`
  - `lower_frame_same_callsite/lua_abs_same_callsite/hot`:
    `0.094302 -> 0.063420` with the hotside carry, then
    `0.058875 -> 0.020010` with the exact proto-NOJIT follow-up on the same
    rebuilt mirror; after the `mcloop=284` restamp, `zkd0` reads
    `0.047349 -> 0.019229` and `0.048731 -> 0.020043`
- historical promotion-core proto-NOJIT reducer extension before the
  post-`d3430611` route-around reducer split:
  - same env knob as the stable promotion-core route-around:
    `LUAJIT_S390X_PROMOTION_CORE_FORL_PROTO_NOJIT=1`
  - exact chunks:
    [tests/s390x/perf/be_helpers_localized.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/be_helpers_localized.lua),
    [tests/s390x/perf/promotion_core_static_stop.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/promotion_core_static_stop.lua);
    [tests/s390x/perf/route_around_reducers.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/route_around_reducers.lua)
    was later removed from this matcher because the reducer be-pack family now
    compiles cleanly;
    the same exact class now also covers
    [tests/s390x/perf/ffi_calls_static_stop.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/ffi_calls_static_stop.lua)
    and
    [tests/s390x/perf/logic_add_phi_noboundary.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/logic_add_phi_noboundary.lua)
  - trusted `kdz` retained-source control -> candidate:
    `be_helpers_localized/number_helper_loop_local_tobit/hot 0.004864 -> 0.001386`,
    `be_helpers_localized/be_pack_loop_local_ops_real/hot 0.010978 -> 0.008632`,
    `promotion_core_static_stop/number_helper_literal_stop_real/hot 0.005313 -> 0.002281`,
    `promotion_core_static_stop/number_helper_literal_stop_real_local_tobit/hot 0.004824 -> 0.001368`,
    `promotion_core_static_stop/be_pack_literal_stop_real/hot 0.021907 -> 0.018765`,
    `route_around_reducers_truth_pack/be_pack_literal_stop/hot 0.054883 -> 0.047332`,
    `route_around_reducers_truth_pack/be_pack_literal_stop_local_ops/hot 0.027238 -> 0.020023`,
    and
    `route_around_reducers_truth_pack/be_pack_loop_local_ops/hot 0.027347 -> 0.020076`
  - follow-up trusted `kdz` retained-source control -> candidate for
    `ffi_calls_static_stop`:
    `direct_abs_literal_stop_real/hot 0.013968 -> 0.010142`
    and `stored_abs_literal_stop_real/hot 0.010697 -> 0.006919`;
    the compact regression read held at `0.010020` and `0.006893`
  - follow-up trusted `kdz` retained-source control -> candidate for
    `logic_add_phi_noboundary`:
    `logic_add_phi_noboundary/hot 0.002156 -> 0.001857`;
    the compact regression read held at `0.001937`
  - `zkd0` same-source screen:
    `be_helpers_localized/number_helper_loop_local_tobit/hot 0.001789`,
    `be_helpers_localized/be_pack_loop_local_ops_real/hot 0.008459`,
    `promotion_core_static_stop/number_helper_literal_stop_real/hot 0.002544`,
    `promotion_core_static_stop/number_helper_literal_stop_real_local_tobit/hot 0.001552`,
    `promotion_core_static_stop/be_pack_literal_stop_real/hot 0.020241`,
    `route_around_reducers_truth_pack/be_pack_literal_stop/hot 0.051895`,
    `route_around_reducers_truth_pack/be_pack_literal_stop_local_ops/hot 0.021296`,
    and
    `route_around_reducers_truth_pack/be_pack_loop_local_ops/hot 0.022199`
  - follow-up `zkd0` `ffi_calls_static_stop` screen:
    `direct_abs_literal_stop_real/hot 0.012448` vs `-joff 0.013297`,
    and `stored_abs_literal_stop_real/hot 0.008949` vs `-joff 0.009072`
  - follow-up `zkd0` `logic_add_phi_noboundary` screen:
    `logic_add_phi_noboundary/hot 0.002060` vs immediate control `0.002944`,
    with rerun `0.002036`
- read:
  - this restores the localized helper/route-around experiment rows without
    promoting them into the stable matrix
  - the implementation is guarded by `S390X_PERF_BENCH_FILE`, exact proto line
    shape, and chunk-name checks so non-target carried rows should stay on the
    retained floor
  - the lower-frame row did not hit the old `lua_lower_frame_retf` seam; the
    hotside extension targets the exact numeric `FORL/JFORI -> MODVN`
    side-ladder behind that regression suite, and the follow-up proto-NOJIT
    route-around parks only the exact saved root trace-1 body with
    `mcloop=288` or `mcloop=284`
  - the current promotion-core extension fixes exact exit-dominated root
    `BC_FORL` helper reducer bodies; it does not reopen broad hotside
    canon/share or helper arithmetic experiments
  - `mixed_noffi` remains noisy on `zkd0` and stays a regression screen, not a
    reopened primary target

Current frontier after the post-promotion stabilization pass:

- the ISA lab A3/A1/trace promotion plus the exact `sum_loop` `mcloop=304`
  restamp remains the carried baseline
- the first real post-promotion drift was the exact `vararg_paths` sibling
  root-`BC_FORL` matcher: `LUAJIT_S390X_VARARG_SIBLING_FORL_BLACKLIST=1` now
  accepts the pre-promotion `mcloop=672/452` shapes and the promoted
  `mcloop=660/444` shapes
- `vararg_paths` is back on its carried near/parity floor after that restamp:
  trusted `kdz` rerun `sum_loop/hot 0.004437`, `retlast_loop/hot 0.001997`,
  `retconst_loop/hot 0.000598`, with `zkd0` confirmation at
  `0.005042`, `0.002420`, and `0.000652`
- `iterator_table` is at parity on the full carried env floor after the
  iterator guard ordering refinement made the exact root `BC_ITERN`
  proto-NOJIT path default-on before the broad iterator blacklist
- `mixed_noffi`, `mixed_ffi`, and `ffi_cdata` remain parked near parity under
  the carried floor
- `dispatch_trace` has been stabilized after its post-promotion collapse with
  an exact root-`BC_FORL` proto-NOJIT route-around for the three official
  dispatch protos:
  - trusted `kdz`: `numeric_loop/hot 0.002170`,
    `side_exit_loop/hot 0.004557`, `hotexit_loop/hot 0.005522`
  - trusted `zkd0`: `numeric_loop/hot 0.002530`,
    `side_exit_loop/hot 0.005002`, `hotexit_loop/hot 0.006005`
- `be_helpers` and `ffi_calls` are also stabilized after their post-promotion
  carried-floor drift with an exact root-`BC_FORL` proto-NOJIT route-around:
  - trusted `kdz`: `number_helper_loop/hot 0.002378`,
    `be_pack_loop/hot 0.018912`, `direct_abs/hot 0.010257`,
    `stored_abs/hot 0.007338`
  - trusted `zkd0`: `number_helper_loop/hot 0.002554`,
    `be_pack_loop/hot 0.020728`, `direct_abs/hot 0.012293`,
    `stored_abs/hot 0.008434`
- the latest retained iterator cut is the iterator guard ordering refinement
  on top of delayed post-proto `BC_ITERN` no-hot dispatch:
  after the exact retained iterator root `BC_ITERN` proto-NOJIT save fires in
  [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c),
  [src/lj_dispatch.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_dispatch.c)
  switches process-local `BC_ITERN` dispatch to `lj_vm_IITERN`. This preserves
  the hash-side root formation that the earlier global no-hot candidate broke,
  then removes the remaining hotcount/proto retry cost for the official row.
  The promoted refinement checks the exact `iterator_table` root `BC_ITERN`
  proto-NOJIT shape before the broad iterator root blacklist, while preserving
  the broad fallback for unsafe non-exact iterator shapes. Trusted pinned
  `kdz` same-binary A/B: default `pairs_sum/hot 0.004472`,
  `pairs_array_sum/hot 0.003716`; `-joff 0.004675`, `0.004274`;
  exact-path opt-out fallback `0.010467`, `0.008237`. `zkd0` is noisy, but
  confirms default is materially better than opt-out:
  `pairs_sum/hot 0.009195` vs `0.021250`, and
  `pairs_array_sum/hot 0.006530` vs `0.017199`.
  - exact env:
    - `LUAJIT_S390X_ITERATOR_ITERN_BLACKLIST=1`
    - `LUAJIT_S390X_ITERATOR_ITERL_BLACKLIST=1`
    - `LUAJIT_S390X_ITERATOR_ITERN_PROTO_NOJIT=1`
    - `LUAJIT_S390X_ITERATOR_ARRAY_ITERN_NOJIT_HOTCOUNT_PARK=1`
    - `LUAJIT_S390X_ITERATOR_HASH_ITERN_NOJIT_HOTCOUNT_PARK=1`
    - `LUAJIT_S390X_ITERATOR_POST_PROTO_ITERN_NOHOT=1`
  - exact mechanism:
    - first keep the older exact iterator route-arounds in place:
      root `BC_ITERN`/`BC_ITERL` blacklists, proto-NOJIT fallback, hash/array
      hotcount parks at `0xffff`, and the direct `BC_ITERN` array-slot store
    - once the exact retained root `BC_ITERN` proto-NOJIT path forms, set a
      process-local s390x flag and make later `BC_ITERN` dispatch use the
      non-hot `lj_vm_IITERN` entry
    - the `kdz` mechanism run changed `S390X_ITERATOR_HASH_ITERN_NOJIT_HOTCOUNT_PARK`
      and `S390X_ITERATOR_ARRAY_ITERN_NOJIT_HOTCOUNT_PARK` from recurring park
      hits to `0` after one `S390X_ITERATOR_POST_PROTO_ITERN_NOHOT` activation
  - read:
    - the root-ITERN proto-NOJIT fallback still keeps the fast interpreter
      `ITERN` path instead of the slower blacklisted `ITERC` generic fallback
    - delaying the no-hot dispatch switch until after that root forms avoids
      the known hash-row regression from the rejected global no-hot candidate
    - both iterator hot rows are now near parity, so the next active queue
      should rerank rather than opening another iterator micro-edit by default
- the first post-iterator `mixed_ffi` save-time cut is retained:
  - `kdz`: `mixed_ffi_loop/hot 0.017600` against immediate controls
    `0.044956` and `0.044900`
  - `zkd0`: candidate examples `0.026970` and `0.023683` against immediate
    controls `0.056937` and `0.085072`
  - exact mechanism: one `S390X_MIXED_FFI_POST_STITCH_SAVE_DONE` marker at
    `trace=102 parent=101 exit=0 root=1 linktype=LJ_TRLINK_INTERP`
  - `mixed_noffi` marker count stays `0`; guardrail A/B is neutral overall
- the second `mixed_ffi` route-around cut is retained:
  - exact env:
    - `LUAJIT_S390X_MIXED_FFI_FORL_PROTO_NOJIT=1`
  - exact mechanism:
    - in [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c), when the official root trace is
      `@tests/s390x/perf/mixed_ffi.lua`, `trace=1`, `parent=0`, `exit=0`,
      `startop=BC_FORL`, `linktype=LJ_TRLINK_STITCH`, `topslot=14`,
      `spadjust=192`, `nsnap=4`, `nins=32822`, set `PROTO_NOJIT`
    - this collapses the remaining root/stitch ladder to the interpreter-speed
      path instead of saving the 100-trace stitched chain
  - host-pair result:
    - `kdz`: candidate rerun `mixed_ffi_loop/hot 0.012178`; immediate
      disabled-env control `0.018412`
    - `zkd0`: candidate rerun `mixed_ffi_loop/hot 0.013641`; immediate
      disabled-env control `0.019946`
- the first `ffi_cdata` save-time cut is retained:
  - exact env:
    - `LUAJIT_S390X_FFI_CDATA_PAIR_SAVE_DONE=1`
  - exact mechanism:
    - one-shot save-time `SNAPCOUNT_DONE` on the first pair-loop
      `BC_TGETB` interpreter child:
      `trace=102 parent=101 exit=0 root=1 startop=BC_JMP link=0 linktype=LJ_TRLINK_INTERP topslot=9 spadjust=8 nsnap=2 nins=32773`
    - mechanism marker fires exactly once on both hosts:
      `S390X_FFI_CDATA_PAIR_SAVE_DONE trace=102 parent=101 exit=0 root=1 startop=88 link=0 linktype=6 nsnap=2 nins=32773 snap=0 op=58`
  - host-pair result:
    - `kdz`: `pair_loop/hot 0.023094` against immediate controls
      `0.136461` and `0.133813`
    - `zkd0`: repeated candidates `0.026214`, `0.026737`,
      `0.027103` against immediate controls `0.137762`, `0.139439`,
      `0.140058`
    - `mixed_width_loop/hot` stayed noisy but neutral overall and remains a
      regression screen
  - read:
    - the live payer was trace-control churn: repeated root-1 same-start
      `BC_JMP` children degrading to `LJ_TRLINK_INTERP`, not a backend cdata
      body micro-cut
    - after the later retained root-FORL blacklist, this save-time cut remains
      part of the retained `ffi_cdata` floor
- the second `ffi_cdata` route-around cut is retained:
  - exact env:
    - `LUAJIT_S390X_FFI_CDATA_PAIR_FORL_BLACKLIST=1`
  - exact mechanism:
    - in [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c), for
      `@tests/s390x/perf/ffi_cdata.lua`, match the official pair-loop root
      `BC_FORL` trace:
      `trace=1`, `parent=0`, `exit=0`, `root=0`, `link=1`,
      `linktype=LJ_TRLINK_LOOP`, `topslot=9`, `spadjust=8`, `nsnap=7`,
      `nins=32798`, `mcloop=324`
    - use `blacklist_pc()` on that exact loop PC to bypass the upstream
      root-loop ladder; the downstream retained `PAIR_SAVE_DONE` marker drops
      to zero because the sidechain no longer forms
  - host-pair result:
    - `kdz`: candidate rerun `pair_loop/hot 0.017097`,
      `mixed_width_loop/hot 0.027798`; immediate disabled-env control
      `0.023377` and `0.027142`
    - `zkd0`: candidate rerun `pair_loop/hot 0.024469`,
      `mixed_width_loop/hot 0.054854`; immediate disabled-env control
      `0.061147` and `0.052549`, with an earlier candidate pass at
      `0.028269` and `0.044877`
  - read:
    - `pair_loop` is now effectively at parity on trusted `kdz`
    - `mixed_width_loop` remains noisy but near parity and stays a regression
      screen
- `vararg_paths` is no longer treated as a parked dominated family on the
  current retained branch state:
  - fresh retained host-pair result:
    - `kdz`
      - `sum_loop/hot 0.004486`
      - `retlast_loop/hot 0.001978`
      - `retconst_loop/hot 0.000570`
    - `zkd0`
      - `sum_loop/hot 0.006285`
      - `retlast_loop/hot 0.002767`
      - `retconst_loop/hot 0.000620`
  - retained mechanism on trusted `kdz`:
    - the first retained stop-gate remains:
      - `pcop=BC_GGET`
      - `prevop=BC_JFORI`
      - `startop=BC_JMP`
      - `linktype=LJ_TRLINK_INTERP`
      - `parent=110`
      - `exit=0`
      - `root=1`
    - exact recorder-side stop cut in
      [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c):
      - `LUAJIT_S390X_SUM_LOOP_SELECT_EXIT0_DONE=1`
      - one-shot `SNAPCOUNT_DONE` on that exact stop family
    - the next exact runtime payer was re-attributed after that win:
      - the apparent `trace 112/113` stitched tail from
        `/tmp/vararg_sum_phase_counts.lua` was wrapper pollution from
        `jit.util.traceinfo`, not real `sum_loop` work
      - the real hot runtime stays on `trace 110` at
        `vararg_paths.lua:14`
    - second retained cut in
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
    - third retained cut in
      [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c):
      - `LUAJIT_S390X_SUM_LOOP_SELECT_CONST_GGET=1`
      - exact `BC_GGET select` constant-fold for the inner `sum(...)` proto
    - mechanism proof on `kdz`:
      - exact engagement:
        - `S390X_SUM_LOOP_SELECT_CONST_GGET trace=91..110`
      - hot trace delta:
        - `trace 110` shrinks from `34` IRs to `27`
        - the dead `func.env -> HREFK -> HLOAD` prefix disappears
      - same-binary A/B:
        - candidate `sum_loop/hot 0.018707`
        - immediate control `0.019204`
      - host-pair confirmation:
        - `zkd0` candidate `0.022269`
        - immediate same-binary control `0.026834`
    - fourth retained cut in
      [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c):
      - `LUAJIT_S390X_SUM_LOOP_FORL_BLACKLIST=1`
      - exact root `BC_FORL` blacklist for the inner `sum(...)` proto:
        `trace=1 parent=0 exit=0 root=0 startop=BC_FORL link=1 linktype=LJ_TRLINK_LOOP topslot=9 spadjust=8 nsnap=4 nins=32796 mcloop=312`
    - mechanism proof on `kdz`:
      - `S390X_SUM_LOOP_FORL_BLACKLIST` marker count `1`
      - `TRACE_META_COUNT` drops to `10`
      - same-binary A/B:
        - candidate `sum_loop/hot 0.004533`
        - immediate control `0.019065`
      - host-pair confirmation:
        - `zkd0` candidate `0.007265`
        - immediate same-binary control `0.028302`
    - fifth retained cut in
      [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c):
      - `LUAJIT_S390X_VARARG_SIBLING_FORL_BLACKLIST=1`
      - exact root `BC_FORL` blacklist for the `retlast_loop` and
        `retconst_loop` caller-loop protos:
        `firstline=31 nsnap=4 nins=32820 mcloop=672` and
        `firstline=43 nsnap=4 nins=32806 mcloop=452`
      - mechanism proof on `kdz`:
        - `S390X_VARARG_SIBLING_FORL_BLACKLIST` marker count `2`
      - same-binary A/B:
        - `kdz` candidate rerun `retlast_loop/hot 0.001978`
        - immediate control `0.003504`
        - `kdz` candidate rerun `retconst_loop/hot 0.000570`
        - immediate control `0.001736`
        - `zkd0` candidate rerun `retlast_loop/hot 0.002767`
        - immediate control `0.004290`
        - `zkd0` candidate rerun `retconst_loop/hot 0.000620`
        - immediate control `0.002623`
  - read:
    - the retained `sum_loop` wins are inside the inner `sum(...)` callee
      runtime family, not the earlier broad nested-`BC_JFORI` handoff theory
    - the later whole-loop-contract backend lane on the carried `trace 110`
      body is exact-but-not-retainable and is now closed
    - `sum_loop`, `retlast_loop`, and `retconst_loop` are no longer carried
      red rows on trusted `kdz`; do not reopen the closed vararg lanes without
      a fresh attribution
- the retained exact branch control is now:
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
  - default-on `SIDETRACE_TYPEINS_DONE` in
    [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c)
- `ROOT_ITERN_NIL_DESC` remains a real but slower classifier.
- the narrowed root-1 replay-triplet work remains necessary, but it is no
  longer the branch-level limiter.
- `dispatch_trace` is now fully back on the right side of `-joff` on both
  hosts under the retained dispatch env gate:
  - `kdz`
    - `numeric_loop/hot 0.000158`
    - `side_exit_loop/hot 0.000353`
    - `hotexit_loop/hot 0.001047`
  - `zkd0`
    - `numeric_loop/hot 0.000232`
    - `side_exit_loop/hot 0.000402`
    - `hotexit_loop/hot 0.001191`
  - the retained dispatch env bundle is now:
    - `LUAJIT_S390X_DISPATCH_FORL_SKIP_JFORI=1`
    - `LUAJIT_S390X_DISPATCH_FORL_PARK_ROOT_HOTEXIT_EXACT_COOLDOWN=12`
  - `dispatch_trace` is no longer a live red family
- the retained mixed floor now includes:
  - the VM-side root-2 shift-address bridge cut in
    [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc)
  - the asm-side `lj_vm_next` KEYINDEX base-reuse cut in
    [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h)
  - the exact `mixed_noffi` tri-root route-around in
    [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c):
    - `LUAJIT_S390X_MIXED_NOFFI_ITERL_BLACKLIST=1`
    - `LUAJIT_S390X_MIXED_NOFFI_ITERN_BLACKLIST=1`
    - `LUAJIT_S390X_MIXED_NOFFI_FORL_STITCH_BLACKLIST=1`
  - the exact post-root `BC_ITERL` LLEAVE-abort blacklist in
    [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c):
    - `LUAJIT_S390X_MIXED_NOFFI_ITERL_ABORT_BLACKLIST=1`
  - the exact early proto-NOJIT plus `BC_ITERN` hotcount park in
    [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c):
    - `LUAJIT_S390X_MIXED_NOFFI_EARLY_PROTO_NOJIT=1`
- the checked-in policy suite now measures scale-based families in deterministic
  hot-first order through `bench.scale_order(scales)`, so the stable `mixed`
  row above is now valid again
- current host-pair validation for the retained mixed bundle:
  - `kdz`: `mixed_loop/hot 0.004041` after candidate/control/candidate
    `0.004077 -> 0.005073 -> 0.004041`; `-joff 0.003734`
  - `zkd0`: `mixed_loop/hot 0.005732` against immediate disabled-env
    control `0.007818`; compact retained regression screen `0.006232`;
    `-joff 0.004387`
  - exact on both hosts:
    - `/tmp/mixedprobe.lua -> RESULT 553416`
    - `/tmp/hash_value.lua -> HASH_VALUE 3000`
    - `/tmp/ipairs_only_probe.lua -> RESULT 576000`
- regression screens on the retained candidate stayed clean on `kdz`:
  - `dispatch_trace`
    - `numeric_loop/hot 0.013789`
    - `side_exit_loop/hot 0.017693`
    - `hotexit_loop/hot 0.467935`
    - absolute dispatch medians remain noisy under the full retained env, but
      this is not a chunk-coupled regression from the exact mixed gates
  - `vararg_paths`
    - `sum_loop/hot 0.004452`
    - `retlast_loop/hot 0.002070`
    - `retconst_loop/hot 0.000597`
  - `iterator_table`
    - `pairs_sum/hot 0.004673`
    - `pairs_array_sum/hot 0.003942`
  - `mixed_ffi/mixed_ffi_loop/hot 0.014760`
  - `ffi_cdata/pair_loop/hot 0.017028`
  - `ffi_cdata/mixed_width_loop/hot 0.028198`
- read:
  - the latest mixed attribution found three route-around roots, not a single
    helper seam: root `BC_ITERL`, root `BC_ITERN`, and stitched root `BC_FORL`
  - the blacklists collapse the focused retained run from `TRACE_META_STOP 106`
    to `3` and remove the high-churn ladder
  - `mixed_noffi` is now near parity, so the next step should be a fresh rerank
    rather than reopening the now-closed helper-side and recorder-side families

## Chronological Log

Everything below this heading stays chronological. Use it for run history,
mechanism notes, rejected directions, and branch-by-branch context. When a run
changes the retained scoreboard row above, update the row in place and then add
the new run here with the qualifying notes.

## Active Shipping Throughput Slice

Treat the envless filtered hotside path as the active shipping-throughput
default for `promotion_core` only:

- active default:
  `hotside_canon_share_uget_looproot_default`
- explicit baseline / opt-out:
  `LUAJIT_S390X_DISABLE_HOTSIDE_CANON_SHARE_UGET_LOOPROOT=1`
- `promotion_secondary`: carry-forward evidence only
- `same_seam_but_dominated`: excluded
- frozen iterator / frozen dispatch: out of scope

Pinned host-pair summary:

- [20260402-hotside-promotion-core-host-pair](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-hotside-promotion-core-host-pair/summary.md)

Post-`5e7b09fe` `kdz` re-quant:

- [be_helpers.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/be_helpers.lua)
  - default:
    - `number_helper_loop/hot 0.008927`
    - `be_pack_loop/hot 0.023920`
  - baseline:
    - `number_helper_loop/hot 0.411136`
    - `be_pack_loop/hot 0.431987`
  - `-joff`:
    - `number_helper_loop/hot 0.002251`
    - `be_pack_loop/hot 0.019077`
- [ffi_calls.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/ffi_calls.lua)
  - default:
    - `direct_abs/hot 0.018530`
    - `stored_abs/hot 0.013380`
  - baseline:
    - `direct_abs/hot 0.477617`
    - `stored_abs/hot 0.458766`
  - `-joff`:
    - `direct_abs/hot 0.009999`
    - `stored_abs/hot 0.006944`
- read:
  - the literal-stop FFI correctness fix does not materially lift the active
    `promotion_core` throughput floor on `kdz`
  - `be_pack_loop` remains the closest live family to `-joff`
  - `number_helper_loop` remains the cleanest control for mechanism work
- caveat:
  - fresh `zkd0` throughput restamps are blocked by a broken clean bench tree

Post-`5e7b09fe` `kdz` mechanism rerun:

- [20260403-131228-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-131228-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism/summary.md)
  - `number_helper_loop`
    - `TRACE_START 6`, `TRACE_STOP 5`, `TRACE_ABORT 0`, `TEXIT_COUNT 64001`
    - dominant seam: `trace 7 exit 0` x `63457`
    - first `sload_int`: `curins 15`, `op1 3`, `op2 4`, `ofs 8`, `extra 12`
  - `be_pack_loop`
    - `TRACE_START 5`, `TRACE_STOP 5`, `TRACE_ABORT 0`, `TEXIT_COUNT 64001`
    - dominant seam: `trace 7 exit 0` x `63457`
    - first `sload_int`: `curins 35`, `op1 3`, `op2 4`, `ofs 8`, `extra 12`
  - read:
    - the literal-stop recorder fix does not move the live `promotion_core`
      mechanism on `kdz`
    - `number_helper_loop` remains the cleanest control
    - `be_pack_loop` remains the best payoff sibling

Exact-taken host-pair reruns on current `HEAD`:

- `kdz`:
  [20260403-135247-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-135247-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism/summary.md)
- `zkd0`:
  [20260403-135538-zkd0-hotside_canon_share_uget_looproot_default-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-135538-zkd0-hotside_canon_share_uget_looproot_default-core-exit-mechanism/summary.md)
- read:
  - `number_helper_loop` and `be_pack_loop` match exactly on both hosts
  - dominant seam still `trace 7 exit 0`
  - first literal taken guard is still:
    - `curins 3`
    - `IR=SLOAD`
    - `op1 4`
    - `op2 36`
    - `ofs 16`
    - `extra 20`
  - so the live payer is still the inherited current-value lane

Matched-fastpath visible-idx no-guard reject:

- opt-in experiment:
  - `LUAJIT_S390X_FORL_FASTPATH_VISIBLE_IDX_NOGUARD=1`
- artifact:
  [20260403-135832-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-135832-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism/summary.md)
- read:
  - exact taken guard stayed unchanged on both real workloads
  - `number_helper_loop`
    - `nins 28 -> 29`
    - first literal taken guard still `curins 3`, `SLOAD op1 4 op2 36`
  - `be_pack_loop`
    - `nins 71 -> 72`
    - first literal taken guard still `curins 3`, `SLOAD op1 4 op2 36`
  - the only structural change was a later `SLOAD op1 4 op2 32`
  - this family is rejected

Root-only visible-idx no-guard reject:

- opt-in experiment:
  - `LUAJIT_S390X_FORL_ROOT_VISIBLE_IDX_NOGUARD=1`
- exact-taken artifacts:
  - [number_helper_loop](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-144356-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism/summary.md)
  - [be_pack_loop](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-144556-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism/summary.md)
- throughput artifact:
  - [20260403-kdz-be_helpers-hotside_canon_share_uget_looproot_default-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260403-kdz-be_helpers-hotside_canon_share_uget_looproot_default-truth-pack/summary.md)
- read:
  - the first literal taken guard does shift on both live `kdz` workloads:
    - `number_helper_loop`: `curins 15`, `SLOAD op1 3 op2 4`
    - `be_pack_loop`: `curins 35`, `SLOAD op1 3 op2 4`
  - but the throughput gets worse, not better:
    - `number_helper_loop/hot 0.010545` vs default `0.008927`
    - `be_pack_loop/hot 0.025796` vs default `0.023920`
  - both workloads remain `exit-dominated` at
    `TRACE_START 6`, `TRACE_STOP 5`, `TRACE_ABORT 1`, `TEXIT_COUNT 64001`
  - this family is rejected

What that reject proves about the next payer:

- the shipping default still pays first on the inherited current-value lane
  (`curins 3`, `SLOAD op1 4 op2 36`)
- but the root-only reject shows that lane is not the whole floor by itself
- when the root-born inherited-current typecheck is relaxed, the first exact
  guard immediately becomes the loop-carried accumulator lane instead:
  - `number_helper_loop`: `curins 15`, `SLOAD op1 3 op2 4`
  - `be_pack_loop`: `curins 35`, `SLOAD op1 3 op2 4`
- source-backed read:
  - inherited current-value is born by
    [rec_for_loop()](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c)
    through `fori_load(... IRSLOAD_INHERIT | IRSLOAD_TYPECHECK | ...)`
  - carried `total` is the ordinary `getslot()->sload()` stack
    specialization path in
    [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c),
    not a numeric-for helper lane
- direct slot attribution on clean `kdz` corrects the lane identity:
  - artifact:
    [20260403-kdz-visible-lane-slot-attribution](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-kdz-visible-lane-slot-attribution/summary.md)
  - `S390X_SLOADMAP` pins the exact guard as `base=2`, `op1=4`, `ofs=16`
  - repeated `S390X_SLOT` dumps show:
    - `idx=2` advances as the loop current value
    - `idx=4` stays constant `1`
    - `idx=5` mirrors the same advancing current value one slot later
- so the exact payer is hidden `FORL_IDX`, not hidden `STEP` and not the
  visible `FORL_EXT` alias
- the loop-carried accumulator lane remains the immediate secondary payer once
  hidden `FORL_IDX` is relaxed; that evidence is still valid
- next exact remediation target:
  - explicit rebinding or rematerialization from hidden `FORL_IDX` to the
    already-live `FORL_EXT` alias was the next honest experiment, and it is
    now closed

Hidden-idx to visible-alias rebind reject:

- opt-in experiment:
  - `LUAJIT_S390X_FORL_REBIND_EXT_ALIAS=1`
- exact-taken artifact:
  - [20260403-kdz-forl-ext-alias-rebind](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-kdz-forl-ext-alias-rebind/summary.md)
- throughput artifacts:
  - [jit-on](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-kdz-forl-ext-alias-truth/jit-on.jsonl)
  - [joff](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-kdz-forl-ext-alias-truth/joff.jsonl)
- read:
  - the live `trace 7 exit 0` family survives cleanly
  - the first exact guard does move from hidden `FORL_IDX` to visible
    `FORL_EXT`:
    - `number_helper_loop`: `curins 3`, `SLOAD op1 7 op2 36`
    - `be_pack_loop`: same `SLOAD op1 7 op2 36` shift
  - smoke remains correct
  - throughput still gets worse:
    - `number_helper_loop/hot 0.009876` vs default `0.008927`
    - `be_pack_loop/hot 0.024501` vs default `0.023920`
  - so slot rebinding is evidence, not remediation
- next exact remediation target:
  - the current-value replay/typecheck contract itself, independent of whether
    the value comes from hidden `FORL_IDX` or visible `FORL_EXT`

Visible-alias no-guard redirect reject:

- opt-in experiment:
  - `LUAJIT_S390X_FORL_EXT_ALIAS_NOGUARD=1`
- exact-taken artifact:
  - [20260403-kdz-forl-ext-alias-noguard](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-kdz-forl-ext-alias-noguard/summary.md)
- read:
  - removing the `FORL_EXT` replay typecheck does not clear the live floor
  - it simply redirects the first exact guard onto the carried-`total` lane:
    - `number_helper_loop`: `curins 15`, `SLOAD op1 3 op2 4`
    - `be_pack_loop`: `curins 35`, `SLOAD op1 3 op2 4`
  - so this is not a new remediation family
  - it only recreates the earlier carried-accumulator redirect
- next exact remediation target:
  - the current-value replay/typecheck contract itself
  - not another visible-alias no-guard variant

Signed-int current-seam reject:

- opt-in experiment:
  - `LUAJIT_S390X_GC64_SIGNED_INT_SLOAD=1`
- mechanism artifact:
  - [20260403-kdz-signed-int-current-seam](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-kdz-signed-int-current-seam/summary.md)
- truth pack:
  - [20260403-kdz-be_helpers-hotside_canon_share_uget_looproot_default-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260403-kdz-be_helpers-hotside_canon_share_uget_looproot_default-truth-pack/summary.md)
- read:
  - on the exact live `promotion_core` seam, the old hidden current-value
    guard no longer dominates
  - the repeated `trace 7 exit 0` family survives anyway
  - smoke is wrong:
    - `number_helper_loop check 13762770`
    - `be_pack_loop check 210`
  - throughput stays red:
    - `number_helper_loop/hot 0.008709` vs `-joff 0.002304`
    - `be_pack_loop/hot 0.023758` vs `-joff 0.018765`
- conclusion:
  - the signed GC64 int-tag path is not the missing default fix for the
    hidden `FORL_IDX` seam
  - this closes as another reject, not a promotion lane

Compare-truth attribution and first narrow compare-fix classification:

- compare-truth control artifact:
  - [20260403-kdz-cmptruth-current-seam](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-kdz-cmptruth-current-seam/raw/number_helper_loop.stderr.log)
- read:
  - the live hidden `FORL_IDX` lane is a valid boxed int at the exact taken
    guard:
    - `raw=0xfff9000000000013`
    - `itype=-14`
    - payload is the advancing current value
  - the compare constants loaded by the backend are wrong on that lane:
    - extracted tags:
      - logical `0x1fff2`
      - signed `0xfffffffffffffff2`
    - emitted expected constants:
      - logical `0x1ffff`
      - signed `0xffffffffffffffff`
  - so the live issue is a backend compare-semantics bug on inherited int
    `SLOAD`, not replay materialization and not guard ownership
- first narrow fix artifact:
  - [20260403-kdz-cmptruth-number-helper-fixed](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-kdz-cmptruth-number-helper-fixed/summary.md)
- control result:
  - `number_helper_loop`
  - `RESULT 1323881804`
  - `TRACE_START 3`
  - `TRACE_STOP 2`
  - `TEXIT_COUNT 202`
  - dominant runtime guardmark moves to `curins 14`
  - the old hidden-current `curins 3` seam is gone
- payoff sibling result:
  - [20260403-kdz-be-pack-after-int-compare-fix](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-kdz-be-pack-after-int-compare-fix/raw/be_pack_loop.stderr.log)
  - `be_pack_loop` crashes with remote exit `139`
- conclusion:
  - the compare fix is directionally real but not promotable yet
  - the next live target is the post-current-seam `be_pack_loop` failure that
    opens after the hidden int compare is corrected

## Historical Broader-Suite Snapshots

These tables preserve one host-specific candidate-surface snapshot in the form
it was captured. They intentionally mix stable carried workloads with
experiment-only rows. They are evidence tables, not alternative top-level
scoreboards.

Use this rule when a row appears here and also has a row at the top:

| Row class | What it means | Should it have a retained row in `Full Stable Matrix`? |
| --- | --- | --- |
| stable carried workload | recurring checked-in suite member that we want to track over time | yes |
| non-retained surface result | same workload, but on an older or non-retained candidate surface | no; keep only as history |
| experimental / mechanism row | localized, static-stop, route-around, or narrow reproducer evidence | no |

If a workload is part of the stable carried suite, the current retained answer
for that workload lives in `Full Stable Matrix` near the top. If it only
appears in the tables below, it is historical evidence or an experiment row,
not part of the retained current matrix.

### Historical Broader-Suite Snapshot: kdz

| Updated | Path | Surface | JIT-on | `-joff` | On/Off |
| --- | --- | --- | ---: | ---: | ---: |
| 2026-04-01 17:07:40 PDT | `add_phi_only/hot` | `hotside_canon_share_uget_looproot` | `0.000664` | `0.000020` | `33.20x` |
| 2026-04-01 14:32:40 PDT | `logic_add_phi_noboundary/hot` | `hotside_canon_share` | `0.002619` | `0.002159` | `1.21x` |
| 2026-04-01 19:56:12 PDT | `chain_tail_add/hot` | `hotside_canon_share_uget_looproot_default` | `0.003212` | `0.002104` | `1.53x` |
| 2026-04-01 19:56:12 PDT | `chain_tail_store/hot` | `hotside_canon_share_uget_looproot_default` | `0.002907` | `0.002036` | `1.43x` |
| 2026-04-01 19:56:12 PDT | `mix_bits/hot` | `hotside_canon_share_uget_looproot_default` | `0.003182` | `0.002086` | `1.53x` |
| 2026-04-02 19:10:44 PDT | `be_pack_literal_stop/hot` | `baseline` | `0.241946` | `0.047685` | `5.07x` |
| 2026-04-02 19:10:40 PDT | `be_pack_literal_stop/hot` | `hotside_canon_share_uget_looproot_default` | `0.073034` | `0.046844` | `1.56x` |
| 2026-04-02 19:10:44 PDT | `be_pack_literal_stop_local_ops/hot` | `baseline` | `0.126152` | `0.019453` | `6.48x` |
| 2026-04-02 19:10:40 PDT | `be_pack_literal_stop_local_ops/hot` | `hotside_canon_share_uget_looproot_default` | `0.126189` | `0.019927` | `6.33x` |
| 2026-04-02 19:10:44 PDT | `be_pack_loop_local_ops/hot` | `baseline` | `0.126122` | `0.019463` | `6.48x` |
| 2026-04-02 19:10:40 PDT | `be_pack_loop_local_ops/hot` | `hotside_canon_share_uget_looproot_default` | `0.126296` | `0.019513` | `6.47x` |
| 2026-04-02 20:03:13 PDT | `number_helper_loop_local_tobit/hot` | `baseline` | `0.436932` | `0.001442` | `303.00x` |
| 2026-04-02 20:03:20 PDT | `number_helper_loop_local_tobit/hot` | `hotside_canon_share_uget_looproot_default` | `0.621525` | `0.001435` | `433.12x` |
| 2026-04-02 20:39:30 PDT | `number_helper_literal_stop_real_local_tobit/hot` | `baseline` | `0.019114` | `0.001359` | `14.06x` |
| 2026-04-02 20:39:23 PDT | `number_helper_literal_stop_real_local_tobit/hot` | `hotside_canon_share_uget_looproot_default` | `0.018959` | `0.001359` | `13.95x` |
| 2026-04-03 05:07:58 PDT | `number_helper_literal_stop_real/hot` | `hotside_canon_share + gc64_signed_int_sload` | `0.010431` | `0.002247` | `4.64x` |
| 2026-04-03 05:07:58 PDT | `number_helper_literal_stop_real_local_tobit/hot` | `hotside_canon_share + gc64_signed_int_sload` | `0.006251` | `0.001359` | `4.60x` |
| 2026-04-03 05:07:58 PDT | `be_pack_literal_stop_real/hot` | `hotside_canon_share + gc64_signed_int_sload` | `0.025815` | `0.018846` | `1.37x` |
| 2026-04-02 20:03:13 PDT | `be_pack_loop_local_ops_real/hot` | `baseline` | `0.300697` | `0.007985` | `37.66x` |
| 2026-04-02 20:03:20 PDT | `be_pack_loop_local_ops_real/hot` | `hotside_canon_share_uget_looproot_default` | `0.244329` | `0.008069` | `30.28x` |
| 2026-04-02 20:10:45 PDT | `number_helper_literal_stop_real/hot` | `baseline` | `0.040660` | `0.002244` | `18.12x` |
| 2026-04-02 20:10:38 PDT | `number_helper_literal_stop_real/hot` | `hotside_canon_share_uget_looproot_default` | `0.009910` | `0.002230` | `4.44x` |
| 2026-04-02 20:10:45 PDT | `be_pack_literal_stop_real/hot` | `baseline` | `0.097101` | `0.019263` | `5.04x` |
| 2026-04-02 20:10:38 PDT | `be_pack_literal_stop_real/hot` | `hotside_canon_share_uget_looproot_default` | `0.025347` | `0.018812` | `1.35x` |
| 2026-04-01 20:20:45 PDT | `numeric_loop/hot` | `hotside_canon_share_uget_looproot_default` | `0.342594` | `0.002173` | `157.66x` |
| 2026-04-01 20:20:45 PDT | `side_exit_loop/hot` | `hotside_canon_share_uget_looproot_default` | `0.526504` | `0.004692` | `112.21x` |
| 2026-04-01 20:20:45 PDT | `hotexit_loop/hot` | `hotside_canon_share_uget_looproot_default` | `0.611632` | `0.005619` | `108.85x` |
| 2026-04-02 16:23:53 PDT | `be_pack_loop/hot` | `hotside_canon_share_uget_looproot_default` | `0.023373` | `0.018732` | `1.25x` |
| 2026-04-02 16:23:53 PDT | `number_helper_loop/hot` | `hotside_canon_share_uget_looproot_default` | `0.008248` | `0.002282` | `3.61x` |
| 2026-04-01 19:56:12 PDT | `direct_abs/hot` | `hotside_canon_share_uget_looproot_default` | `0.017982` | `0.010090` | `1.78x` |
| 2026-04-01 19:56:12 PDT | `stored_abs/hot` | `hotside_canon_share_uget_looproot_default` | `0.013056` | `0.006895` | `1.89x` |
| 2026-04-01 16:25:27 PDT | `mixed_width_loop/hot` | `hotside_canon_share_uget_looproot` | `0.027964` | `0.027969` | `1.00x` |
| 2026-04-01 16:25:27 PDT | `pair_loop/hot` | `hotside_canon_share_uget_looproot` | `0.137179` | `0.017319` | `7.92x` |
| 2026-04-01 17:53:04 PDT | `retconst_loop/hot` | `hotside_canon_share_uget_looproot` | `0.001660` | `0.000591` | `2.81x` |
| 2026-04-01 17:53:04 PDT | `retlast_loop/hot` | `hotside_canon_share_uget_looproot` | `0.003093` | `0.002025` | `1.53x` |
| 2026-04-01 16:25:27 PDT | `sum_loop/hot` | `hotside_canon_share_uget_looproot` | `0.675950` | `0.005150` | `131.25x` |
| 2026-04-01 16:25:27 PDT | `mixed_ffi_loop/hot` | `hotside_canon_share_uget_looproot` | `0.059215` | `0.012404` | `4.77x` |
| 2026-04-01 17:55:23 PDT | `mixed_loop/hot` | `hotside_canon_share_uget_looproot` | `0.036412` | `0.003764` | `9.67x` |
| 2026-04-01 20:14:22 PDT | `pairs_sum/hot` | `hotside_canon_share_uget_looproot_default` | `0.058992` | `0.005459` | `10.81x` |
| 2026-04-01 20:14:22 PDT | `pairs_array_sum/hot` | `hotside_canon_share_uget_looproot_default` | `0.064010` | `0.003679` | `17.40x` |

Read for this snapshot:

- rows such as `pairs_sum`, `mixed_loop`, `numeric_loop`, and `sum_loop` are
  stable carried workloads, but these specific numbers are still just one
  historical candidate-surface capture
- rows such as `add_phi_only`, `logic_add_phi_noboundary`,
  `number_helper_loop_local_tobit`, and `be_pack_literal_stop_real` are not
  part of the retained matrix; they stay here as experiment or mechanism
  evidence

### Historical Broader-Suite Snapshot: zkd0

| Updated | Path | Surface | JIT-on | `-joff` | On/Off |
| --- | --- | --- | ---: | ---: | ---: |
| 2026-04-01 19:56:12 PDT | `chain_tail_add/hot` | `hotside_canon_share_uget_looproot_default` | `0.004231` | `0.002338` | `1.81x` |
| 2026-04-01 19:56:12 PDT | `chain_tail_store/hot` | `hotside_canon_share_uget_looproot_default` | `0.003796` | `0.002276` | `1.67x` |
| 2026-04-01 19:56:12 PDT | `mix_bits/hot` | `hotside_canon_share_uget_looproot_default` | `0.004088` | `0.003184` | `1.28x` |
| 2026-04-01 21:18:33 PDT | `numeric_loop/hot` | `hotside_canon_share_uget_looproot_default` | `0.777567` | `0.003257` | `238.72x` |
| 2026-04-01 21:18:33 PDT | `side_exit_loop/hot` | `hotside_canon_share_uget_looproot_default` | `1.299877` | `0.006691` | `194.28x` |
| 2026-04-01 21:18:33 PDT | `hotexit_loop/hot` | `hotside_canon_share_uget_looproot_default` | `1.586069` | `0.008171` | `194.11x` |
| 2026-04-01 19:56:12 PDT | `be_pack_loop/hot` | `hotside_canon_share_uget_looproot_default` | `0.031455` | `0.026039` | `1.21x` |
| 2026-04-01 19:56:12 PDT | `number_helper_loop/hot` | `hotside_canon_share_uget_looproot_default` | `0.012384` | `0.002873` | `4.31x` |
| 2026-04-01 19:56:12 PDT | `direct_abs/hot` | `hotside_canon_share_uget_looproot_default` | `0.024177` | `0.012092` | `2.00x` |
| 2026-04-01 19:56:12 PDT | `stored_abs/hot` | `hotside_canon_share_uget_looproot_default` | `0.016539` | `0.008667` | `1.91x` |
| 2026-04-01 16:39:41 PDT | `mixed_width_loop/hot` | `hotside_canon_share_uget_looproot` | `0.045325` | `0.045030` | `1.01x` |
| 2026-04-01 16:39:41 PDT | `pair_loop/hot` | `hotside_canon_share_uget_looproot` | `0.366784` | `0.024836` | `14.77x` |
| 2026-04-01 17:55:45 PDT | `retconst_loop/hot` | `hotside_canon_share_uget_looproot` | `0.003574` | `0.000849` | `4.21x` |
| 2026-04-01 17:55:45 PDT | `retlast_loop/hot` | `hotside_canon_share_uget_looproot` | `0.012619` | `0.003196` | `3.95x` |
| 2026-04-01 16:39:41 PDT | `sum_loop/hot` | `hotside_canon_share_uget_looproot` | `1.499405` | `0.005568` | `269.29x` |
| 2026-04-01 16:39:41 PDT | `mixed_ffi_loop/hot` | `hotside_canon_share_uget_looproot` | `0.081518` | `0.015637` | `5.21x` |
| 2026-04-01 17:59:38 PDT | `mixed_loop/hot` | `hotside_canon_share_uget_looproot` | `0.079668` | `0.006760` | `11.79x` |
| 2026-04-01 20:54:30 PDT | `pairs_sum/hot` | `hotside_canon_share_uget_looproot_default` | `0.082382` | `0.006774` | `12.16x` |
| 2026-04-01 20:54:30 PDT | `pairs_array_sum/hot` | `hotside_canon_share_uget_looproot_default` | `0.106179` | `0.005346` | `19.86x` |

Read for this snapshot:

- this table is the `zkd0` companion evidence dump for the same broader-suite
  candidate surface
- it is useful for host-to-host comparison and historical context, but it does
  not replace the retained current matrix at the top of the page

## Scope

This page tracks the current native s390x performance state after the branch
was frozen into three lanes:

- Lane A: build and stability only
- Lane B: promotable recorder-side iterator perf only
- Lane C: parked bridge and continuation research only

Iterator is still frozen at the current Lane A + Lane B checkpoint unless a
genuinely new seam appears outside the reject pile. Dispatch loop-clone work
on the old mechanism stays closed. The current live hotside policy candidate is
the filtered gate:

- `LUAJIT_S390X_HOTSIDE_CANON_SHARE_UGET_LOOPROOT=1`

That gate is now clearly narrower than the old global
`LUAJIT_S390X_HOTSIDE_CANON_SHARE_EQUIV=1` surface:

- it still wins on the reduced `UGET`/looproot throughput family
- it is effectively inert on the plain `int_add_phi_only` control reproducer
- it no longer carries as a broader-suite promotion candidate on `kdz`
- it does not carry as a broader-suite promotion candidate on either host
- `zkd0` completes the same narrower read:
  - dispatch stays catastrophically far from `-joff`
  - helper-heavy and call-heavy families can improve sharply
  - `sum_loop` remains extremely red even when it improves
  - iterator is no longer the main regression driver there, but it is still
    far from `-joff`

So the active queue is no longer “broader gate promotion”. It is:

1. broader positive-family mechanism proof for
   `LUAJIT_S390X_HOTSIDE_CANON_SHARE_UGET_LOOPROOT=1`
2. confirm whether helper-heavy and call-heavy winners are still improving
   through the exact same reduced `UGET`/looproot seam
3. only after that, any wider promotion claim outside the filtered
   `UGET`/looproot mechanism

Frozen-family fence status is now helper-backed on both hosts:

- iterator:
  [baseline](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-201055-kdz-baseline-iterator-truth-pack/summary.md)
  vs
  [promoted default](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-201051-kdz-hotside_canon_share_uget_looproot_default-iterator-truth-pack/summary.md)
  - medians improve slightly while trace/exit shape is unchanged
- dispatch:
  [baseline](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-201441-kdz-baseline-dispatch-truth-pack/summary.md)
  vs
  [promoted default](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-201748-kdz-hotside_canon_share_uget_looproot_default-dispatch-truth-pack/summary.md)
  - medians are effectively flat and trace/exit shape is unchanged

- `zkd0` iterator:
  [baseline](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-204030-zkd0-baseline-iterator-truth-pack/summary.md)
  vs
  [promoted default](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-205106-zkd0-hotside_canon_share_uget_looproot_default-iterator-truth-pack/summary.md)
  - `pairs_sum/hot`: `0.115511 -> 0.082382`
  - `pairs_array_sum/hot`: `0.115958 -> 0.106179`
  - trace/exit shape is unchanged
- `zkd0` dispatch:
  [baseline](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-211042-zkd0-baseline-dispatch-truth-pack/summary.md)
  vs
  [promoted default](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-211442-zkd0-hotside_canon_share_uget_looproot_default-dispatch-truth-pack/summary.md)
  - `numeric_loop/hot`: `0.850875 -> 0.777567`
  - `side_exit_loop/hot`: `1.192460 -> 1.299877`
  - `hotexit_loop/hot`: `1.327162 -> 1.586069`
  - trace/exit shape is unchanged

So the envless promoted default now reads as a scoped throughput improvement,
not a frozen-family promotion candidate. It remains fenced out of iterator and
dispatch on both hosts.

Reduced host-pair mechanism proof for the remaining `promotion_core` red is now
in hand:

- [20260401-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-214311-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism/summary.md)
- [20260401-zkd0-hotside_canon_share_uget_looproot_default-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-214447-zkd0-hotside_canon_share_uget_looproot_default-core-exit-mechanism/summary.md)

That read is narrower than the older “helper-heavy” / “FFI-heavy” labels:

- `be_helpers` and `ffi_calls` both converge to the same dominant steady-state
  exit on both hosts: `trace 7 exit 0`
- `number_helper_loop` and `be_pack_loop` both spend `63457 / 64001` exits on
  that one site; the remaining split is mostly body size (`nins 28` vs `71`)
- `direct_abs` and `stored_abs` both spend `79457 / 80001` exits on that same
  site; the remaining split is also body size (`nins 33` vs `23`)
- focused reduced dump on clean `kdz` corrects the first attribution target:
  - [20260401-214848-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-214848-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism/summary.md)
  - [20260401-215124-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-215124-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism/summary.md)
  - `number_helper_loop` hot loop has no call IR inside the traced body
  - `direct_abs` hot loop does keep `CALLXS`
- conclusion:
  - the filtered hotside default already removed the clone ladder on this
    slice
  - the front-most remaining payer is one stable loop exit, but the clean
    first attribution target is now `number_helper_loop`, because it removes
    the call-boundary complication that still exists in `direct_abs`

Reduced runtime exit attribution on clean `kdz` now pins that stable loop exit
to the same exact bytecode seam in both representative workloads:

- [20260401-kdz-core-exit-attribution-reduced](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-core-exit-attribution-reduced/summary.md)
  - `number_helper_loop`:
    - `TRACE_START 5`, `TRACE_STOP 5`, `TRACE_ABORT 0`, `TEXIT_COUNT 801`
    - dominant texit: `7:0` x `257`
    - runtime exit log: `trace 7 exit 0` resumes at `pc op=45`,
      `snapop=45`, `snapcount=0`
  - `direct_abs`:
    - `TRACE_START 5`, `TRACE_STOP 5`, `TRACE_ABORT 0`, `TEXIT_COUNT 801`
    - dominant texit: `7:0` x `257`
    - runtime exit log: `trace 7 exit 0` also resumes at `pc op=45`,
      `snapop=45`, `snapcount=0`
- `op=45` is `BC_UGET`, which matches the original filtered hotside seam in
  [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c)
- queue correction:
  - the promoted-default core red is not front-most call/FFI exit churn
  - the remaining steady seam is the full `SNAP #0` header-guard interval,
    with `BC_UGET` only marking the original helper form's restore point
  - on `number_helper_loop`, that is no longer described as a helper-only
    `bit.tobit` identity seam
  - reduced baseline comparison now closes the mechanism split:
    - [20260401-kdz-core-exit-attribution-baseline-reduced](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-core-exit-attribution-baseline-reduced/summary.md)
    - baseline and promoted default restore to the same `BC_UGET` header seam
    - only the dominant owning loop clone changes:
      - baseline: `trace 6 exit 0`
      - promoted default: `trace 7 exit 0`
    - the promoted default collapses the equivalent-parent ladder; it does not
      remove the restored `UGET bit -> TGETS "tobit"` header seam
  - focused first-clone proof now explains why the seam survives:
    - [20260401-kdz-core-exit-hotside-focus-parent1](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-core-exit-hotside-focus-parent1/summary.md)
    - on the first hot source (`parent=1 exit=0`), hotside sees:
      - `pc=snappc`
      - `op=snapop=BC_UGET`
      - `cand=0`
      - `child=0`
    - it simply counts that restored header seam to `hotexit` and starts the
      first side trace from the same header snapshot
    - the first clone therefore inherits the same loop shape as the root,
      rather than a deeper arithmetic-only body
  - source-backed start-point rule now confirms there is no generic deeper
    entry on the current mechanism:
    - [lj_snap_restore()](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_snap.c#L1196) returns the restored `snap_pc`
    - [lj_trace_exit()](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c#L3197) forwards that `pc` to
      [trace_hotside()](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c#L2941)
    - [trace_hotside()](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c#L3055) starts the side trace with `lj_trace_ins(J, pc)`
    - [lj_record_setup()](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c#L3833) then copies that `pc` into the new side
      trace's `startpc`
    - `resumepc` setup in [trace_save()](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c#L1902) only changes how an already-saved
      child later re-enters; it does not let the first clone start later than
      the restored snapshot
  - reduced exit attribution narrows the remaining red to the pre-snapshot
    header guard cluster:
    - [20260401-kdz-core-exit-attribution-reduced](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-core-exit-attribution-reduced/summary.md)
    - dominant `exit 0` reports:
      - `pc op=BC_UGET`
      - `snapop=BC_UGET`
      - `snapnent=0`
    - so the hot seam is still inside the full `SNAP #0` guard interval before
      any snapshot-carried state exists

Header variants on clean `kdz` now close the helper-only reading:

- [20260402-kdz-uget-header-variant-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-uget-header-variant-audit/summary.md)
  - original helper form:
    - restored hot seam starts at `BC_UGET` (`45`)
  - local/arg `tobit` form:
    - restored hot seam moves to `BC_MOV` (`18`)
  - pure-add reducer:
    - restored hot seam moves to `BC_MULVN` (`24`)
  - all three still keep the same reduced `exit 0` clone ladder
- queue correction:
  - the helper lookup/identity chain is not required to reproduce the flurry
  - the live family is generic `SNAP #0` header-guard failure, not a
    helper-only `BC_UGET/TGETS/HLOAD/fun EQ` seam

Guard-log intersection on the same reduced `kdz` scripts narrows the shared
candidate set:

- original helper `snap=0`: `curins=17,15,14,13,12,10,8,7,3,2`
- pure-add reducer `snap=0`: `curins=6,5,4,3,2`
- shared surviving guard kinds:
  - `sload_int`

Exact runtime guard attribution now sharpens that further:

- real workload:
  [20260402-kdz-number-helper-guardmark-attribution-v5](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-number-helper-guardmark-attribution-v5/summary.md)
  - dominant runtime failure on `trace 7 exit 0` is `curins=3`, `IR=SLOAD`
  - exact runtime guard:
    - `op1=4`
    - `op2=36`
    - `sload_int ofs=16 extra=20 cc=6`
- no-helper sibling:
  [20260402-kdz-number-helper-guardmark-attribution-v4](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-number-helper-guardmark-attribution-v4/raw/pure_add_reducer.stderr.log)
  - first repeated reduced exit cluster lands on `guardmark=0x3`
  - matching early guard log is the same inherited
    `sload_int curins=3 ofs=16 extra=20`
  - restored header marker moves to `BC_MULVN`, but the first exact runtime
    failure stays on the same inherited `sload_int`
- semantic meaning:
  - this is the first shared marked header guard, `IR=SLOAD #4 TI`
  - corrected slot map artifact:
    [20260402-kdz-number-helper-fori-slot-map](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-number-helper-fori-slot-map/summary.md)
  - on `number_helper_loop`, `FORI/FORL A=2` means:
    - slot `2` = hidden `IDX`
    - slot `3` = hidden `STOP`
    - slot `4` = hidden `STEP`
    - slot `5` = visible `EXT`
  - so `IR=SLOAD op1=4` / `ofs=16 extra=20` is hidden `STEP`, not hidden
    `IDX`
  - and `ofs=8 extra=12` is hidden `STOP`, not the carried accumulator
  - the live header seam is therefore a numeric-for hidden control-slot replay
    family, with `STEP` now pinned as the front-most exact-taken marker on the
    real workload
  - origin correction:
    - the old “root `FORI` constructor” read is now closed
    - [rec_for_loop()](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c#L1086)
      already uses `fori_arg(... find_kinit(...))` for hidden `STOP` and
      `STEP` on the `FORL` side-trace path
    - the attempted root-only gate
      `LUAJIT_S390X_FORI_CONST_INIT=1` changed only
      [rec_for(..., isforl=0)](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c#L1127)
    - clean `kdz` proof under that gate kept the real workload on the same
      seam:
      - artifact:
        [20260402-kdz-fori-const-init-v1](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-fori-const-init-v1/summary.md)
      - `TRACE_START 5`
      - `TRACE_STOP 5`
      - `TRACE_ABORT 0`
      - `TEXIT_COUNT 64001`
      - dominant texit still `7:0=63457`
      - repeated exit still reports `guardmark=0x3`
    - queue correction:
      - root-`FORI` const-init surgery is the wrong mechanism for the live
        promoted-slice seam
      - the remaining question stays inside restored numeric-for
        replay/typecheck semantics, not constructor choice
  - `snapnent=0` remains true on the dominant exit, so this guard is checking
    live interpreter frame state at restored `SNAP #0`
  - stricter taken-only marking on the real workload now closes that gap:
    - [20260402-kdz-number-helper-guardmark-taken-v1](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-number-helper-guardmark-taken-v1/summary.md)
    - dominant `trace 7 exit 0` still lands on `guardmark=0x3`
    - exact taken guard stays:
      - `curins=3`
      - `IR=SLOAD`
      - `op1=4`
      - `op2=36`
      - `sload_int ofs=16 extra=20`
  - focused slot logging still matters for interpretation:
    - restored top-frame slot `2` is already int-tagged at the repeated exit
      point
    - so the live question is no longer “which guard is first?”
    - it is “why does the inherited hidden `STEP` replay/typecheck contract
      still fail every trip at restored `SNAP #0`?”

So the current promoted-slice red is no longer best described as the
carried-`total` reload seam. On the real workload, the first literal taken
guard in the merged restored-`SNAP #0` numeric-`for` header cluster is now
the inherited numeric-for hidden `STEP` `sload_int` guard (`IR=SLOAD #4 TI`,
`ofs=16 extra=20`). The later stop-bound `LE` on `n` remains present in the
same cluster, but it is no longer the front-most competing failure on the
real workload.

Direct shifted-tag repair is now closed on the current mechanism:

- reduced no-helper sibling:
  [20260402-kdz-pure-add-tagfix-v1](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-pure-add-tagfix-v1/summary.md)
  - after replacing the inherited integer `SLOAD` compare with the exact GC64
    shifted int tag, the old reducer flurry mostly disappears:
    `TRACE_START 8`, `TEXIT_COUNT 6`
- real helper workload:
  [20260402-kdz-be_helpers-hotside_canon_share_uget_looproot_default-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260402-kdz-be_helpers-hotside_canon_share_uget_looproot_default-truth-pack/raw/jit-on.stderr.log)
  - the same direct repair is not semantically safe on the real promoted
    helper path:
    `number_helper_loop/hot: expected 1323881804, got 34304`
- queue correction:
  - the raw tag mismatch at `IR=SLOAD #4 TI` is informative, but it is not a
    standalone promotable fix
  - the live seam stays the inherited numeric-for hidden-control
    replay/header contract at restored `SNAP #0`, not “swap in the exact GC64
    int tag and ship it”

Exact backend mismatch is now pinned on the real workload:

- [20260402-kdz-number-helper-sloadmap-v1](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-number-helper-sloadmap-v1/summary.md)
  - env-gated `asm_sload()` register-map logging in
    [lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h)
    now ties the exact-taken guard to the emitted compare shape
  - current s390x integer `SLOAD` typecheck lowering at the live seam is:
    - `tmp = slot64`
    - `tmp >>= 47`
    - `expected = ((uint32_t)LJ_TISNUM >> 15)` -> `0x1ffff`
    - `CGR tmp, expected`
  - repeated taken values on the real reduced helper workload are:
    - live shifted tag: `0x1fff2`
    - expected constant: `0x1ffff`
  - cross-backend contrast:
    - [lj_asm_x86.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_x86.h)
      and
      [lj_asm_arm64.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_arm64.h)
      compare against the GC64 high-word int-tag form
      (`LJ_TISNUM << 15` / upper 32 bits), not the s390x-shifted constant
- queue correction:
  - the remaining promoted-slice seam is now best described as the s390x GC64
    inherited integer-`SLOAD` typecheck on hidden `STEP`
  - not generic helper-header replay
  - not root-`FORI` constructor choice
  - not a promotable direct-tag swap on the current mechanism
  - the sharper source correction is signed extraction:
    - GC64 `itype()` in
      [lj_obj.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_obj.h)
      uses arithmetic shift on signed `it64`
    - s390x integer `SLOAD` lowering in
      [lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h)
      currently uses logical `SRLG ... 47`
    - that is why the real workload sees `0x1fff2` where the GC64 contract
      expects `0xfffffff2`
  - design boundary:
    [gc64-sload-int-repair.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/gc64-sload-int-repair.md)
  - first signed-extraction prototype is rejected on the real helper path:
    - [20260402-kdz-gc64-int-sload-ashift-number-helper-v1](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-gc64-int-sload-ashift-number-helper-v1/summary.md)
    - no-helper sibling still looks structurally good:
      [20260402-kdz-gc64-int-sload-ashift-pure-add-v1](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-gc64-int-sload-ashift-pure-add-v1/summary.md)
    - real helper workload fails with `RC=139` after moving beyond the old
      repeated `guardmark=0x3` seam
    - next queue:
      explain the downstream bad state after the hidden-`STEP` typecheck
      begins to pass, not another naive compare swap

Shared `sload_int` attribution remains useful, but it is now explicitly
secondary:

- [20260402-kdz-number-helper-sload-attribution](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-number-helper-sload-attribution/summary.md)
  - dominant guard order:
    `25,22,20,17,15,14,13,12,10,8,7,3,2`
  - first shared `sload_int` on the real workload:
    - `curins=15`
    - `IR=SLOAD`
    - `op1=3`
    - `op2=4`
    - `ofs=8`
    - `extra=12`
    - semantic source: hidden `STOP` (`n`)
  - later shared `sload_int`:
    - `curins=3`
    - `ofs=16`
    - `extra=20`
    - semantic source: hidden `STEP` (`1`)

Current queue correction:

- the next live seam on the promoted slice is the inherited numeric-for
  hidden-control replay/header contract, with `STEP` front-most and the later
  `LE` on `n` remaining secondary on the real workload
- more precisely: inherited root-`FORI` control-slot replay, not fresh
  `FORL` side-trace creation
- the carried-`total` reload stays relevant as the first shared `sload_int`
  seam across reducers, but it is no longer the front-most exact runtime
  failure
- the direct GC64 shifted-tag repair for inherited integer `SLOAD` is now
  rejected on the current mechanism
- do not reopen helper-header, low32-home, or generic hotside-population work
  from this result

x64 control status:

- there is still no checked-in mature x64 reduced runner for this seam
- an ad hoc Rosetta x64 path was tested on this workstation, but a direct
  `arch -x86_64 make -C src ...` still selected the arm64 VM build and failed
  in `vm_arm64.dasc`
- treat mature x64 reduced control as a tooling gap for now, not as a blocker
  on the s390x seam read

Exact reduced-family scope proof on clean `kdz` is now recorded here:

- [20260401-kdz-hotside-uget-looproot-scope-proof](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-uget-looproot-scope-proof/summary.md)
  - `int_add_phi_only`: `match_count 0`
  - `logical_chain_tail_add`: `match_count 8792`
  - `logical_chain_tail_store`: `match_count 8792`
  - `bitops_mix`: `match_count 8792`
  - all positive reduced hits stay on:
    - `exit=0`
    - `op=BC_UGET`
    - `startop=BC_JMP`
    - `root_startop in {BC_FORL, BC_FUNCF}`

Broader positive-family mechanism proof on clean `kdz` now says the helper and
FFI-call winners are improving through that same seam:

- [20260401-kdz-hotside-uget-looproot-broader-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-uget-looproot-broader-mechanism/summary.md)
  - `number_helper_loop`: `match_count 63858`
  - `be_pack_loop`: `match_count 63858`
  - `direct_abs`: `match_count 79858`
  - `stored_abs`: `match_count 79858`
  - every positive broader family still stayed on:
    - `exit=0`
    - `op=BC_UGET`
    - `startop=BC_JMP`
    - `root_startop=BC_FORL`

Representative host-pair confirmation now matches on `zkd0` too:

- [20260401-zkd0-hotside-uget-looproot-broader-mechanism-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-zkd0-hotside-uget-looproot-broader-mechanism-check/summary.md)
  - `number_helper_loop`: `match_count 63858`
  - `direct_abs`: `match_count 79858`
  - both representative broader winners still stayed on:
    - `exit=0`
    - `op=BC_UGET`
    - `startop=BC_JMP`
    - `root_startop=BC_FORL`

Current clean-`kdz` broader-throughput frontier:

- `vararg_paths` stays parked, not live
  - `sum_loop` remains classified as the normal caller-root stop into an
    already-compiled nested callee loop
  - no narrower recorder seam has been named before nested `BC_JFORI` entry
- broader-throughput observability is now corrected:
  - [tools/s390x/build_throughput_truth_pack.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/build_throughput_truth_pack.py)
    now parses `REMOTE_RC=...` correctly
  - [tests/s390x/helpers/testlib.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/helpers/testlib.lua)
    now exposes lightweight trace/texit counters so reduced trace validation
    does not wedge on hist bookkeeping
- corrected clean-`kdz` frontier:
  - [20260401-kdz-bitops_mix-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-bitops_mix-truth-pack)
    - `mix_bits/hot`: JIT-on `0.008267`, `-joff` `0.002084`, ratio `3.97x`
    - `REMOTE_RC 0`, `TRACE_START 41`, `TRACE_STOP 41`, `TRACE_ABORT 0`,
      `TEXIT_COUNT 7981`
    - classification: `exit-dominated`
  - [20260401-kdz-logical_chain_tail_add-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-logical_chain_tail_add-truth-pack)
    - `chain_tail_add/hot`: JIT-on `0.008269`, `-joff` `0.002123`, ratio
      `3.89x`
    - `REMOTE_RC 0`, `TRACE_START 41`, `TRACE_STOP 41`, `TRACE_ABORT 0`,
      `TEXIT_COUNT 7981`
    - classification: `exit-dominated`
  - [20260401-kdz-logical_chain_tail_store-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-logical_chain_tail_store-truth-pack)
    - `chain_tail_store/hot`: JIT-on `0.006676`, `-joff` `0.002319`, ratio
      `2.88x`
    - `REMOTE_RC 0`, `TRACE_START 42`, `TRACE_STOP 42`, `TRACE_ABORT 0`,
      `TEXIT_COUNT 7983`
    - classification: `exit-dominated`
  - [20260401-kdz-int_add_phi_only-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-int_add_phi_only-truth-pack)
    - `add_phi_only/hot`: JIT-on `0.000672`, `-joff` `0.000020`, ratio
      `33.60x`
    - `REMOTE_RC 0`, `TRACE_START 20`, `TRACE_STOP 20`, `TRACE_ABORT 0`,
      `TEXIT_COUNT 4001`
    - classification: `exit-dominated`
  - [20260401-kdz-logic_add_phi_noboundary-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-logic_add_phi_noboundary-truth-pack)
    - `logic_add_phi_noboundary/hot`: JIT-on `0.003527`, `-joff` `0.002153`,
      ratio `1.64x`
    - `REMOTE_RC 0`, `TRACE_START 23`, `TRACE_STOP 21`, `TRACE_ABORT 2`,
      `TEXIT_COUNT 4001`
    - classification: `exit-dominated`
- queue correction:
  - `bitops_mix` is no longer an honest compiled-body / low32-home frontier on
    the current evidence
  - the low32-home / normalized-result contract note is parked design context,
    not the active main-line queue:
    [docs/s390x/low32-home-contract.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/low32-home-contract.md)
  - the live target is now a narrower hotside policy family on that generic
    throughput loop-clone surface:
    - `int_add_phi_only` as the smallest reproducer
    - `logical_chain_tail_add` as the value-tail sibling
    - `bitops_mix` as the larger mixed logic/add reproducer
  - do not reopen low32-home prototype work until a finite compiled-body family
    exists again under the corrected validator
- first real policy candidate on that family:
  - `LUAJIT_S390X_HOTSIDE_CANON_EQUIV=1` plus
    `LUAJIT_S390X_HOTSIDE_SHARE_EQUIV=1`
  - smallest reproducer artifact:
    [20260401-kdz-hotside-share-equiv-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-share-equiv-audit/summary.md)
  - `int_add_phi_only`:
    - baseline `hot 0.000786`, `TRACE_START 21`, `TEXIT_COUNT 4001`,
      `TRACEINFO_COUNT 27`
    - `SHARE_EQUIV` alone `hot 0.000659`, `TRACE_START 100`,
      `TEXIT_COUNT 300`, `TRACEINFO_COUNT 106`
    - `CANON_EQUIV + SHARE_EQUIV` `hot 0.000341`, `TRACE_START 3`,
      `TEXIT_COUNT 4001`, `TRACEINFO_COUNT 9`
    - `CANON_CHILD + SHARE_EQUIV` `hot 0.000407`, `TRACE_START 4`,
      `TEXIT_COUNT 4001`, `TRACEINFO_COUNT 10`
  - focused read:
    - `SHARE_EQUIV` alone wins by accelerating new trace formation
    - the combined canon/share policy wins differently: it collapses actual
      trace population while preserving the throughput gain
  - mechanism artifact:
    [20260401-kdz-hotside-canon-share-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-mechanism/summary.md)
  - mechanism read:
    - `SHARE_EQUIV` alone still reaches late `parent=24 exit=0` and primes
      that late parent straight to `hotexit - 1`, then immediately starts a
      new trace there
    - the combined canon/share policy does not lower total exits on this
      reproducer
    - instead, the warmed measured run no longer reaches `parent=24` at all
    - it keeps paying the same repeated `exit 0` seam on early `parent=4`,
      which is why `TEXIT_COUNT` stays flat while trace population collapses
- clean `kdz` sibling validation:
  - artifact:
    [20260401-kdz-hotside-canon-share-family-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-family-check/summary.md)
  - `logical_chain_tail_add`
    - baseline `hot 0.008741`, `TRACE_START 41`, `TEXIT_COUNT 7981`
    - candidate `hot 0.002683`, `TRACE_START 2`, `TEXIT_COUNT 8000`
  - `bitops_mix`
    - baseline `hot 0.008902`, `TRACE_START 41`, `TEXIT_COUNT 7981`
    - candidate `hot 0.002968`, `TRACE_START 2`, `TEXIT_COUNT 8000`
- first `zkd0` screen:
  - artifact:
    [20260401-zkd0-hotside-canon-share-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-zkd0-hotside-canon-share-check/summary.md)
  - `logical_chain_tail_add`: baseline `0.018642`, candidate `0.005414`
  - `bitops_mix`: baseline `0.009459`, candidate `0.004000`
  - after tracked-file resync and rebuild, structural counts match `kdz`:
    `TRACE_START 41 -> 2`, `TEXIT_COUNT 7981 -> 8000`
- dedicated single-gate promotion:
  - `LUAJIT_S390X_HOTSIDE_CANON_SHARE_EQUIV=1`
  - clean `kdz` check:
    [20260401-kdz-hotside-canon-share-gate-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-gate-check/summary.md)
    - `int_add_phi_only`: `hot 0.000349`, `TRACE_START 3`, `TEXIT_COUNT 4001`
    - `logical_chain_tail_add`: `hot 0.003015`, `TRACE_START 2`, `TEXIT_COUNT 8000`
    - `bitops_mix`: `hot 0.003064`, `TRACE_START 2`, `TEXIT_COUNT 8000`
  - `zkd0` screen:
    [20260401-zkd0-hotside-canon-share-gate-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-zkd0-hotside-canon-share-gate-check/summary.md)
    - `logical_chain_tail_add`: `hot 0.003333`, `TRACE_START 2`, `TEXIT_COUNT 8000`
    - `bitops_mix`: `hot 0.004170`, `TRACE_START 2`, `TEXIT_COUNT 8000`
- queue correction:
  - the root-cause question is answered on this mechanism
  - the live candidate surface is now the dedicated single gate, not the old
    ad hoc env pair
  - helper integration is now complete in
    [tools/s390x/build_throughput_truth_pack.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/build_throughput_truth_pack.py)
    via `--candidate hotside_canon_share`
  - helper-backed `kdz` candidate artifacts:
    - [20260401-kdz-int_add_phi_only-hotside_canon_share-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-int_add_phi_only-hotside_canon_share-truth-pack)
      - `add_phi_only/hot`: JIT-on `0.000350`, `-joff` `0.000032`,
        `TRACE_START 3`, `TEXIT_COUNT 4001`
    - [20260401-kdz-logic_add_phi_noboundary-hotside_canon_share-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-logic_add_phi_noboundary-hotside_canon_share-truth-pack)
      - `logic_add_phi_noboundary/hot`: JIT-on `0.002619`, `-joff`
        `0.002159`, `TRACE_START 5`, `TRACE_ABORT 2`, `TEXIT_COUNT 4001`
    - [20260401-kdz-logical_chain_tail_add-hotside_canon_share-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-logical_chain_tail_add-hotside_canon_share-truth-pack)
      - `chain_tail_add/hot`: JIT-on `0.003052`, `-joff` `0.002541`,
        `TRACE_START 2`, `TEXIT_COUNT 8000`
    - [20260401-kdz-logical_chain_tail_store-hotside_canon_share-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-logical_chain_tail_store-hotside_canon_share-truth-pack)
      - `chain_tail_store/hot`: JIT-on `0.002800`, `-joff` `0.002016`,
        `TRACE_START 2`, `TEXIT_COUNT 8000`
    - [20260401-kdz-bitops_mix-hotside_canon_share-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-bitops_mix-hotside_canon_share-truth-pack)
      - `mix_bits/hot`: JIT-on `0.003084`, `-joff` `0.002168`,
        `TRACE_START 2`, `TEXIT_COUNT 8000`
  - helper-backed `zkd0` screen:
    - [20260401-zkd0-logical_chain_tail_add-baseline-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-zkd0-logical_chain_tail_add-baseline-truth-pack)
      - baseline `chain_tail_add/hot`: JIT-on `0.016437`,
        `TRACE_START 41`, `TEXIT_COUNT 7981`
    - [20260401-zkd0-logical_chain_tail_add-hotside_canon_share-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-zkd0-logical_chain_tail_add-hotside_canon_share-truth-pack)
      - candidate `chain_tail_add/hot`: JIT-on `0.003722`,
        `-joff 0.003899`, `TRACE_START 2`, `TEXIT_COUNT 8000`
    - [20260401-zkd0-bitops_mix-hotside_canon_share-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-zkd0-bitops_mix-hotside_canon_share-truth-pack)
      - `mix_bits/hot`: JIT-on `0.004230`, `-joff` `0.002933`,
        `TRACE_START 2`, `TEXIT_COUNT 8000`
    - [20260401-zkd0-logical_chain_tail_store-baseline-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-zkd0-logical_chain_tail_store-baseline-truth-pack)
      - baseline `chain_tail_store/hot`: JIT-on `0.009260`,
        `TRACE_START 42`, `TEXIT_COUNT 7983`
    - [20260401-zkd0-logical_chain_tail_store-hotside_canon_share-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-zkd0-logical_chain_tail_store-hotside_canon_share-truth-pack)
      - candidate `chain_tail_store/hot`: JIT-on `0.003955`,
        `TRACE_START 2`, `TEXIT_COUNT 8000`
  - queue correction:
    - the dedicated gate now covers every active reduced throughput surface in
      the current queue with helper-backed evidence
    - broader non-reduced screens now also hold:
      - `kdz`:
        - [20260401-kdz-hotside-canon-share-broader-suite-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-broader-suite-check/summary.md)
        - [20260401-kdz-hotside-canon-share-ffi-screen](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-ffi-screen/summary.md)
        - [20260401-kdz-hotside-canon-share-ffi-cdata-rerun](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-ffi-cdata-rerun/summary.md)
        - [20260401-kdz-hotside-canon-share-promotion-scope-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-promotion-scope-check/summary.md)
      - `zkd0`:
        - [20260401-zkd0-hotside-canon-share-broader-suite-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-zkd0-hotside-canon-share-broader-suite-check/summary.md)
        - [20260401-zkd0-hotside-canon-share-promotion-scope-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-zkd0-hotside-canon-share-promotion-scope-check/summary.md)
    - the remaining off-family caveat is gone:
      - `ffi_cdata/mixed_width_loop` on `kdz` reran from
        `0.028053 -> 0.027835`
    - the promotion boundary is now explicit:
      - broader throughput families win heavily on both hosts
      - `iterator_table` regresses on both hosts
      - so `LUAJIT_S390X_HOTSIDE_CANON_SHARE_EQUIV=1` is a broader throughput
        candidate, not a safe global default
    - first selective-scope retries are now rejected on `kdz`:
      - exact loop-clone seam only:
        [20260401-kdz-hotside-canon-share-loop0-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-loop0-check/summary.md)
        - throughput still wins, iterator still regresses
      - loop-clone seam plus root-`ITERN` exclusion:
        [20260401-kdz-hotside-canon-share-loop0-noitern-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-loop0-noitern-check/summary.md)
        - throughput still wins, iterator still regresses
      - fast rerun after removing avoidable no-op overhead:
        [20260401-kdz-hotside-canon-share-loop0-noitern-fastcheck](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-loop0-noitern-fastcheck/summary.md)
        - `chain_tail_add/hot`: `0.007930 -> 0.003045`
        - `pairs_sum/hot`: `0.064785 -> 0.072845`
        - `pairs_array_sum/hot`: `0.068692 -> 0.076979`
    - deeper selective activation is now proven and the current active
      candidate is filtered rather than global:
      - exact throughput activation proof:
        [20260401-kdz-hotside-throughput-shape-proof](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-throughput-shape-proof/summary.md)
        - actual canon/share hits sit on:
          - `exit=0`
          - `startop=BC_JMP`
          - `pcop=snapop=BC_UGET`
          - `root_startop=BC_FORL` or `BC_FUNCF`
      - exact iterator non-hit proof:
        [20260401-kdz-hotside-canon-share-iterator-bench-proof](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-iterator-bench-proof/summary.md)
        - full `iterator_table` shows zero actual canon/share hits under the
          old dedicated gate
      - filtered gate:
        - `LUAJIT_S390X_HOTSIDE_CANON_SHARE_UGET_LOOPROOT=1`
        - first inner-only filter still left iterator slightly red because
          the remaining cost was no-op check overhead, not bad rewrites
        - match proof:
          [20260401-kdz-hotside-uget-looproot-match-proof/logical_chain_tail_add.summary.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-uget-looproot-match-proof/logical_chain_tail_add.summary.md)
          and
          [20260401-kdz-hotside-uget-looproot-match-proof/iterator_table.summary.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-uget-looproot-match-proof/iterator_table.summary.md)
      - hoisted prefilter retry is the first selective gate that keeps the
        throughput win without iterator fallout:
        - `kdz`:
          [20260401-kdz-hotside-canon-share-uget-looproot-perf](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-uget-looproot-perf/summary.md)
          - `logical_chain_tail_add/hot`: `0.003130`
          - `bitops_mix/hot`: `0.003232`
          - `pairs_sum/hot`: `0.059845`
          - `pairs_array_sum/hot`: `0.061876`
        - `zkd0`:
          [20260401-zkd0-hotside-canon-share-uget-looproot-perf](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-zkd0-hotside-canon-share-uget-looproot-perf/summary.md)
          - `logical_chain_tail_add/hot`: `0.004477`
          - `bitops_mix/hot`: `0.003607`
          - `pairs_sum/hot`: `0.090623`
          - `pairs_array_sum/hot`: `0.102340`
    - queue correction:
      - the active candidate is now the filtered gate, not the global
        `LUAJIT_S390X_HOTSIDE_CANON_SHARE_EQUIV=1` surface
      - helper support now exists via
        [build_throughput_truth_pack.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/build_throughput_truth_pack.py)
        as `--candidate hotside_canon_share_uget_looproot`
      - but the first clean `kdz` broader-suite restamp closes broad
        promotion for this gate:
        [20260401-kdz-hotside-uget-looproot-broader-suite-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-uget-looproot-broader-suite-check/summary.md)
        - `dispatch_trace/numeric_loop`: `0.341080 -> 0.340611`
        - `mixed_ffi/mixed_ffi_loop`: `0.059399 -> 0.059215`
        - `ffi_cdata/mixed_width_loop`: `0.027778 -> 0.027964`
        - `vararg_paths/sum_loop`: `1.207311 -> 0.675950`, still far from
          `-joff 0.005150`
        - `iterator_table` still regresses:
          - `pairs_sum/hot`: `0.068483 -> 0.076181`
          - `pairs_array_sum/hot`: `0.066698 -> 0.084550`
      - broader scope proof now makes the promotion boundary explicit on
        clean `kdz`:
        [20260401-kdz-hotside-uget-looproot-promotion-scope](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-uget-looproot-promotion-scope/summary.md)
        - same-seam positives:
          - `retconst_loop`: `match_count 15858`
          - `retlast_loop`: `match_count 15858`
          - `sum_loop`: `match_count 15858`
          - `mixed_loop`: `match_count 15858`
        - zero-hit non-targets:
          - `mixed_ffi_loop`: `match_count 0`
          - `pair_loop`: `match_count 0`
          - `mixed_width_loop`: `match_count 0`
          - `pairs_sum`: `match_count 0`
          - `pairs_array_sum`: `match_count 0`
      - representative `zkd0` confirmation matches:
        [20260401-zkd0-hotside-uget-looproot-promotion-scope-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-zkd0-hotside-uget-looproot-promotion-scope-check/summary.md)
        - `retconst_loop`: `match_count 15858`
        - `mixed_loop`: `match_count 15858`
        - `mixed_ffi_loop`: `match_count 0`
        - `pairs_sum`: `match_count 0`
      - promotion scope is now:
        - in-scope candidate slice:
          - reduced `UGET`/looproot siblings
          - `be_helpers`
          - `ffi_calls`
          - `retconst_loop`
          - `retlast_loop`
          - `mixed_loop`
        - same-seam but not promotion evidence:
          - `sum_loop`
            - same seam hit, but still dominated by the parked nested-callee
              vararg frontier
        - out of scope on the current mechanism:
          - `dispatch_trace`
          - `iterator_table`
          - `mixed_ffi`
          - `ffi_cdata`
          - `int_add_phi_only`
      - that scope is now also codified in
        [build_throughput_truth_pack.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/build_throughput_truth_pack.py)
        so helper-backed candidate summaries report:
        - `promotion_core`
        - `promotion_secondary`
        - `same_seam_but_dominated`
        - `out_of_scope`
      - helper-backed host-pair truth packs now cover the full non-dominated
        scoped slice, which now splits cleanly into:
        - `promotion_core`
          - reduced `UGET`/looproot siblings
          - `be_helpers`
          - `ffi_calls`
        - `promotion_secondary`
          - `retconst_loop`
          - `retlast_loop`
          - `mixed_loop`
      - the secondary slice now has enough baseline/candidate A/B to stay
        explicitly secondary instead of floating as generic same-seam evidence:
        - `kdz`
          - `retlast_loop/hot`: `0.029461 -> 0.003093`
          - `retconst_loop/hot`: `0.028523 -> 0.001660`
          - `mixed_loop/hot`: `0.084383 -> 0.036412`
          - `sum_loop/hot`: `1.123796 -> 0.675104`, still dominated
        - `zkd0`
          - `retlast_loop/hot`: `0.052306 -> 0.012619`
          - `retconst_loop/hot`: `0.063160 -> 0.003574`
          - `mixed_loop/hot`: `0.189387 -> 0.079668`
          - `sum_loop/hot`: `2.894826 -> 2.985526`, still dominated and not
            promotion evidence
        - `zkd0` vararg baseline numbers above come from the completed raw
          benchmark logs while the helper summary is still pending:
          - [jit-on](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-zkd0-vararg_paths-baseline-truth-pack/raw/jit-on.stdout.log)
          - [joff](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-zkd0-vararg_paths-baseline-truth-pack/raw/joff.stdout.log)
        - `kdz`
          - [20260401-kdz-be_helpers-hotside_canon_share_uget_looproot-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-be_helpers-hotside_canon_share_uget_looproot-truth-pack/summary.md)
            - `number_helper_loop/hot`: `0.008169` vs `-joff 0.002285`,
              `TRACE_START 6`, `TEXIT_COUNT 64001`
            - `be_pack_loop/hot`: `0.023346` vs `-joff 0.018789`,
              `TRACE_START 6`, `TEXIT_COUNT 64001`
          - [20260401-kdz-ffi_calls-hotside_canon_share_uget_looproot-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-ffi_calls-hotside_canon_share_uget_looproot-truth-pack/summary.md)
            - `direct_abs/hot`: `0.018044` vs `-joff 0.009995`,
              `TRACE_START 5`, `TEXIT_COUNT 80001`
            - `stored_abs/hot`: `0.012581` vs `-joff 0.006919`,
              `TRACE_START 5`, `TEXIT_COUNT 80001`
          - [20260401-kdz-vararg_paths-hotside_canon_share_uget_looproot-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-vararg_paths-hotside_canon_share_uget_looproot-truth-pack/summary.md)
            - `retlast_loop/hot`: `0.003093` vs `-joff 0.002025`, `TRACE_START 5`
            - `retconst_loop/hot`: `0.001660` vs `-joff 0.000591`, `TRACE_START 5`
            - `sum_loop/hot`: `0.675104` vs `-joff 0.004523`, still
              `same_seam_but_dominated`
          - [20260401-kdz-mixed_noffi-hotside_canon_share_uget_looproot-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-mixed_noffi-hotside_canon_share_uget_looproot-truth-pack/summary.md)
            - `mixed_loop/hot`: `0.036412` vs `-joff 0.003764`,
              `TRACE_START 102`, `TEXIT_COUNT 195722`
        - `zkd0`
          - [20260401-zkd0-be_helpers-hotside_canon_share_uget_looproot-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-zkd0-be_helpers-hotside_canon_share_uget_looproot-truth-pack/summary.md)
            - `number_helper_loop/hot`: `0.015573` vs `-joff 0.003520`,
              `TRACE_START 6`, `TEXIT_COUNT 64001`
            - `be_pack_loop/hot`: `0.051882` vs `-joff 0.039644`,
              `TRACE_START 6`, `TEXIT_COUNT 64001`
          - [20260401-zkd0-ffi_calls-hotside_canon_share_uget_looproot-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-zkd0-ffi_calls-hotside_canon_share_uget_looproot-truth-pack/summary.md)
            - `direct_abs/hot`: `0.039104` vs `-joff 0.024293`,
              `TRACE_START 5`, `TEXIT_COUNT 80001`
            - `stored_abs/hot`: `0.041040` vs `-joff 0.019167`,
              `TRACE_START 5`, `TEXIT_COUNT 80001`
          - [20260401-zkd0-vararg_paths-hotside_canon_share_uget_looproot-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-zkd0-vararg_paths-hotside_canon_share_uget_looproot-truth-pack/summary.md)
            - `retlast_loop/hot`: `0.012619` vs `-joff 0.003196`, `TRACE_START 5`
            - `retconst_loop/hot`: `0.003574` vs `-joff 0.000849`, `TRACE_START 5`
            - `sum_loop/hot`: `2.985526` vs `-joff 0.007298`, still
              `same_seam_but_dominated`
          - [20260401-zkd0-mixed_noffi-hotside_canon_share_uget_looproot-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-zkd0-mixed_noffi-hotside_canon_share_uget_looproot-truth-pack/summary.md)
            - `mixed_loop/hot`: `0.079668` vs `-joff 0.006760`,
              `TRACE_START 102`, `TEXIT_COUNT 195722`
      - promotion decision:
        - treat the envless filtered seam as the active promoted/default
          scoped throughput surface, with
          `LUAJIT_S390X_HOTSIDE_CANON_SHARE_UGET_LOOPROOT=1` kept as a
          compatibility alias, for:
          - `promotion_core` workloads as the immediate promotion surface
          - `promotion_secondary` workloads as carry-forward same-seam evidence,
            not the first promotion bar
        - keep `sum_loop` out of promotion evidence
        - keep `dispatch_trace`, `iterator_table`, `mixed_ffi`, `ffi_cdata`,
          `int_add_phi_only`, and `logic_add_phi_noboundary` out of this
          candidate surface
      - the next honest target is no longer deciding whether this filtered
        gate has a promotable slice or filling host-pair gaps inside it
      - it is selective promotion planning from this helper-backed split:
        core promotion first, secondary same-seam carry-forward second
      - that now means:
        - promote only `promotion_core` on the first surface
        - keep `promotion_secondary` as documented same-seam carry-forward
          evidence, not the first enable set
      - checked-in boundary:
        - [hotside-uget-looproot-promotion.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/hotside-uget-looproot-promotion.md)
          now pins the first enable set and deferred slices explicitly
      - next honest target:
        - broader rollout criteria from that note, now that
          [build_hotside_promotion_slice.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/build_hotside_promotion_slice.py),
          has completed the full host-pair wave on the envless promoted default
      - runner-backed read:
        - all core families still stamp `promotion_core`
        - all core families still stamp `eligible_first_enable_set`
        - all runner-produced reduced trace probes completed with
          `REMOTE_RC 0` on both hosts
- first invariant-driven reduced-probe gate is now a clean `kdz` reject:
  - artifact:
    [20260401-kdz-low32home-add-boundary-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-low32home-add-boundary-check/summary.md)
  - gate shape:
    - keep current 64-bit logical lowering
    - skip producer-side `asm_bnorm32()` only for proven logical-chain carry
      nodes and `ADD`-tail nodes
    - normalize explicitly at the `ADD` consumer boundary
  - host result:
    - `logical_chain_tail_add`: baseline `0.008265`, gated `0.008807`,
      regression `+0.000542s` (`1.066x`)
    - `logical_chain_tail_store`: baseline `0.006822`, gated `0.007940`,
      regression `+0.001118s` (`1.164x`)
  - structural result:
    - the gate did fire:
      - `add-boundary:add-tail`: `46`
      - `skip-bnorm:add-tail`: `46`
      - `skip-bnorm:carry`: `483` on add-tail, `487` on store-tail
    - logged `asm_bnorm32()` totals dropped from:
      - add-tail `991 -> 439`
      - store-tail `994 -> 489`
    - but both reduced trace probes timed out with `REMOTE_RC=124`
  - result:
    - source returned to the non-behavior baseline after the host check
    - this exact consumer-boundary low32-home gate is closed
    - if `bitops_mix` stays open, the next honest target is not another
      partial `ADD`/tail gate
    - it has to be a fuller stateful low32-home / normalized-result contract,
      or the family should close too
- next classifier read on clean `kdz`:
  - artifact:
    [20260401-kdz-addhome-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-addhome-audit/summary.md)
  - `logical_chain_tail_add`:
    - the first live low32-home carry seam is plain non-guard integer `ADD`,
      not the earlier “normalize at `ADD` boundary” shape
    - `70` `ADD` sites matched the carry shape:
      - `46` with the current bitop result as the only low32-home source
      - `24` with both the carried total and current bitop result in the same
        low32-home carry family
    - those candidates only feed `PHI` / later plain `ADD`
  - `logical_chain_tail_store`:
    - no `ADD` site matched that carry shape
    - the bitop chain still first leaves into `ASTORE`
  - classifier note:
    - the verbose `LUAJIT_S390X_ADDHOME_LOG=1` trace probes timed out with
      `REMOTE_RC=124`, so this is structural attribution, not a perf result
  - queue correction:
    - the next backend family, if opened, is a fuller low32-home carry across
      plain non-guard integer `ADD` plus `PHI`
    - `ASTORE`, `LE`/guard, helper, and other noncarry consumers remain hard
      boundaries
- first host check on that `ADD`/`PHI` carry gate:
  - artifact:
    [20260401-kdz-low32home-addphi-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-low32home-addphi-check/summary.md)
  - gated medians:
    - `logical_chain_tail_add`: `0.007441`
    - `logical_chain_tail_store`: `0.006737`
  - structural gate:
    - proof scripts finished cleanly and the gate fired at the targeted seam:
      - `proof_add`: `REMOTE_RC=0`, `skip_count=2`
      - `proof_store`: `REMOTE_RC=0`, `skip_count=0`
    - both reduced trace probes timed out on clean `kdz`:
      - `chain_tail_add`: `REMOTE_RC=124`
      - `chain_tail_store`: `REMOTE_RC=124`
  - result:
    - this exact low32-home `ADD`/`PHI` carry gate is not promotable
    - source returned to the non-behavior baseline after the host check
    - the backend line now either needs a fuller stateful low32-home /
      normalized-result contract that preserves finite trace behavior and
      normalizes before guard/compare, helper/call, store, and
      snapshot-visible boundaries, or it should close too
  - design map from the lowering read:
    - safe internal family only:
      - bitop logic/unary/shift/rotate
      - plain non-guard integer `ADD`
      - loop `PHI` whose incoming arms stay inside that family
    - hard boundaries remain:
      - guard/compare
      - helper/call
      - store consumers such as `ASTORE`
      - snapshot-visible exits/restores
- next structural classifier on clean `kdz`:
  - artifact:
    [20260401-kdz-low32home-boundary-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-low32home-boundary-audit/summary.md)
  - `logical_chain_tail_add`:
    - `can_carry=1`: `1039`
    - first hard consumer split:
      - `guard`: `65`
      - `other`: `29`
      - `store`: `0`
  - `logical_chain_tail_store`:
    - `can_carry=1`: `898`
    - first hard consumer split:
      - `store`: `68`
      - `guard`: `45`
      - `other`: `25`
  - `bitops_mix`:
    - treat the live benchmark family as matching the add-tail seam rather
      than the store-tail seam
  - result:
    - the current compiled-body red is add/guard-boundary dominated
    - store-tail is a real separate boundary family, but it is not the main
      `bitops_mix` seam
    - if this backend line stays open, the next honest target is low32-home
      consumption at the compare/guard boundary, not another store-tail or
      add-only gate
- reduced clean `kdz` compare-boundary check:
  - artifact:
    [20260401-kdz-low32cmp-add-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-low32cmp-add-check/summary.md)
  - reduced add-tail probe:
    - `BUILD_RC=0`
    - `RUN_RC=0`
    - `RESULT 1746150614`
  - compare consumers:
    - total: `19`
    - `LE`: `14`
    - `NE`: `5`
    - `intcomp`: `14`
    - `equal`: `5`
    - left source is always carried `ADD`: `19`
    - right source is always constant: `19`
    - unsigned compare path is unused: `cmp32u=0`
    - hot compare path is the signed immediate path:
      - `imm16_signed=1`: `14`
      - `imm16_signed=0`: `5`
  - current lowering match:
    - `asm_intcomp()` signed-immediate compare lowers through `CGHI`
    - `asm_equal()` remaining equality path lowers through `CGR`
    - `src/lj_emit_s390x.h` only exposes the 64-bit compare forms used here:
      `CGR`, `CLGR`, `CGHI`
    - there is no pre-existing 32-bit compare-consumer path already wired in
  - result:
    - the active `bitops_mix` boundary is not a generic guard frontier
    - it is specifically carried `ADD` into signed immediate `LE`, with
      constant `NE` equality as a secondary boundary
    - the next honest backend target is consumption at that exact compare
      boundary, not another store-tail or broad low32-home skip variant
    - if the family stays open, the next real code branch is emitter plus
      backend compare-consumer design, not another local skip gate
- corrected clean `kdz` add-kind proof:
  - artifact:
    [20260401-kdz-low32cmp-addkind-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-low32cmp-addkind-check/summary.md)
  - same compare counts:
    - total `19`
    - `LE`: `14`
    - `NE`: `5`
    - left source `ADD`: `19`
    - right source constant: `19`
  - decisive split:
    - left add kind `ctrl_inc`: `19`
    - left add kind `bitop_tail`: `0`
  - reduced `-jdump=is` proof on clean `kdz` matches that:
    - `LE` is the induction increment compare `i + 1 <= 200`
    - `NE` is the induction zero-check in the traced `arshift` path
    - the carried value path remains separate as `ADD total, bitop_chain`
      followed by loop `PHI`
  - result:
    - the compare boundary is a loop-control seam, not the carried bitop value
      seam
    - close compare-consumer work for `bitops_mix` on the current mechanism
    - the next honest backend target is the value-tail `ADD` plus `PHI`
      boundary only, explicitly excluding the control-increment compare path
- narrowed value-tail `ADD` / `PHI` gate check on clean `kdz`:
  - artifact:
    [20260401-kdz-low32valueaddphi-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-low32valueaddphi-check/summary.md)
  - gate:
    - `LUAJIT_S390X_LOW32VALUEADDPHI=1`
  - reduced checks:
    - add-tail reduced probe:
      - `RUN_RC=0`
      - `S390X_LOW32VALUEADDPHI_SUMMARY candidates=10 skips=10`
    - store-tail reduced probe:
      - `RUN_RC=0`
      - no gate hits
  - structural gate:
    - add-tail trace probe: `RUN_RC=124`
    - store-tail trace probe: `RUN_RC=124`
  - result:
    - reject this exact value-tail producer-side skip gate
    - if the backend family stays open, the next honest target is not another
      producer-side skip and not another compare-consumer branch
    - it is a deeper normalized-result / low32-home contract that remains
      finite under real trace formation

First broader-throughput family read from clean `kdz`:

- artifact root:
  - [20260331-kdz-vararg_paths-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260331-kdz-vararg_paths-truth-pack)
- current raw medians:
  - `sum_loop/hot`
    - JIT-on `1.109134`
    - `-joff` `0.004543`
    - gap `+1.104591s`
    - ratio `244.14x`
  - `retlast_loop/hot`
    - JIT-on `0.029367`
    - `-joff` `0.001993`
    - gap `+0.027374s`
    - ratio `14.74x`
  - `retconst_loop/hot`
    - JIT-on `0.028397`
    - `-joff` `0.000561`
    - gap `+0.027836s`
    - ratio `50.62x`
- focused hot-only medians:
  - `sum_loop/hot`
    - JIT-on `0.459993`
    - `-joff` `0.004453`
    - ratio `103.30x`
  - `retlast_loop/hot`
    - JIT-on `0.029780`
    - `-joff` `0.002047`
    - ratio `14.55x`
  - `retconst_loop/hot`
    - JIT-on `0.027820`
    - `-joff` `0.000574`
    - ratio `48.47x`
- current interpretation:
  - `vararg_paths` is a real broader-throughput red family on the current
    tree, not a mild widening check
  - `sum_loop` is the front-most hot case by a wide margin
  - the branch should not widen farther into `bitops_mix` before naming the
    traced hot vararg seam first
- exact next tasks on this family:
  1. isolate traced hot `sum_loop` on clean `kdz`
  2. compare that path against `retlast_loop` and `retconst_loop`
  3. classify the extra red as:
     - `select()` control,
     - vararg value/materialization,
     - or exit-heavy traced hot flow

That first contrast is now partially answered:

- `retlast_loop` and `retconst_loop` both show the same base loop-clone
  pattern on `kdz`
- `sum_loop` is different in kind, not just in degree:
  - it forms the inner traced vararg loop
  - then adds caller-side handoff / return traces back into the outer loop
- so the next live seam is:
  - nested `sum(...)` vararg scan plus caller return/handoff
  - not generic vararg throughput
  - and not the already-shared base loop-clone behavior by itself

That seam is narrower again after reduced clean-host handoff probes:

- the extra `sum_loop` red is not coming from a reopened lower-frame return
  path
- reduced `kdz` `LUAJIT_S390X_RECRET_LOG=1` probes show:
  - all three workloads return through `lua_intrace_return`
  - none of them hit `lua_lower_frame_retf`
  - none of them hit `lua_root_lower_frame_lleave`
- what singles `sum_loop` out is the extra trace family:
  - `sum_loop` forms an inner callee loop family, then a separate caller
    handoff family (`TRACE 2`, later `TRACE 7`) linking back into it
  - `retlast_loop` and `retconst_loop` stay inside the already-shared
    single-family loop-clone pattern
- focused artifact bundle for this seam:
  - [20260331-kdz-vararg-handoff-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260331-kdz-vararg-handoff-audit)
- the next exact target is now:
  - caller/callee handoff around a traced Lua callee loop
  - not generic vararg return lowering
  - and not another lower-frame return bug family
- one narrower candidate inside that seam is now closed:
  - a focused clean-host `LUAJIT_S390X_FUNCJIT_LOG` pass was silent on the
    reduced `sum_loop` and `retlast_loop` probes
  - so the extra `sum_loop` family is not being born at `rec_func_jit()` /
    compiled-callee entry
  - the next exact target moves later in the path:
    - caller-side re-entry after `lua_intrace_return`
    - before it settles into the separate `TRACE 2` / `TRACE 7` handoff family
- two more clean-host classifiers narrow that caller-side seam further:
  - reduced traceinfo probes:
    - `sum_loop`
      - `trace 1`: callee vararg scan loop
      - `trace 2`: caller-side root trace
      - `trace 7`: later caller-side root trace in the same family
    - `retlast_loop`
      - caller loop family only through the hot phase
      - later stitch traces exist, but they are not unique to this workload
  - focused artifact bundles:
    - [20260331-kdz-vararg-rootstart-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260331-kdz-vararg-rootstart-audit)
    - [20260331-kdz-vararg-callhandoff-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260331-kdz-vararg-callhandoff-audit)
  - a synced-and-rebuilt `LUAJIT_S390X_CALLHANDOFF_LOG` classifier then stayed
    completely silent while `sum_loop` still formed `TRACE 2` / `TRACE 7`
  - that rules out the generic `trace_stop(... BC_CALL/BC_CALLM/BC_ITERC ...)`
    plus `lj_trace_stitch()` handoff path as the birth point of the extra
    caller family
- one corrected clean-host start classifier changed that read again:
  - the actual gate is `LUAJIT_S390X_TRACE_START_LOG`
  - with that gate on clean `kdz`:
    - `sum_loop`
      - `trace 1` starts as a root at the callee vararg loop
      - `trace 2` starts as a second independent root at the caller site
    - `retlast_loop`
      - `trace 1` starts as a root at the caller site
      - later traces then grow from that caller root
  - so `sum_loop` is not creating `trace 2` through hidden post-return
    root-link selection
  - it is splitting across two hotcounted root sites instead
  - the next exact target therefore shifts:
    - explain why the caller root in `sum_loop` stops `-> 1` and keeps feeding
      the inner-loop ladder instead of converging into the stable caller-loop
      family seen in `retlast_loop`
- the reduced caller-root dumps make that stop-point explicit:
  - focused artifact bundle:
    - [20260331-kdz-vararg-rootdump-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260331-kdz-vararg-rootdump-audit)
  - `retlast_loop`
    - caller root already contains:
      - the caller add
      - the outer loop increment/check
      - outer-loop PHIs
    - it stops as a loop immediately
  - `sum_loop`
    - caller root stops after:
      - callee function identity guard
      - `select` env / identity guards
    - it does not yet materialize:
      - the caller add of callee result into the outer total
      - the outer-loop carried total / PHIs
    - it stops `-> 1` before the caller body becomes a real loop owner
  - the next exact target is therefore no longer “why is there a second root?”
  - it is:
    - why traced-callee return to caller in `sum_loop` stops before caller-body
      materialization, while `retlast_loop` reaches caller add/loop formation
      in the caller root itself
  - reduced recstop logs narrow that one step further:
    - focused artifact bundle:
      - [20260401-kdz-vararg-recstop-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-vararg-recstop-audit)
    - `retlast_loop`
      - caller side traces reach `rec_loop_jit()` on the caller loop seam
      - they log `S390X_RECLOOP ... ev=2 ...` and stop `-> loop`
    - `sum_loop`
      - caller roots (`TRACE 2`, later `TRACE 7`) stop `-> 1` before any
        caller-path `S390X_RECLOOP` appears
    - only the separate callee loop family reaches `rec_loop_jit()`
    - so `rec_loop_jit_root` is not the live vararg seam
    - the live target is now the earlier recorder/return condition that stops
      `sum_loop` caller roots before they ever reach the caller loop op
  - reduced return logs correct that again:
    - focused artifact bundle:
      - [20260401-kdz-vararg-recret-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-vararg-recret-audit)
    - the focused clean-host `sum_loop` probe does not show a divergent
      post-return branch in `lj_record_ret()`
    - the only observed branch is `lua_intrace_return`, and it belongs to the
      inner `select()` work in the callee loop
    - the actual caller-root cutoff is earlier:
      - `TRACE 2` starts at the caller site
      - enters `sum(...)`
      - then stops `-> 1` on the callee `JFORI` path, with `pc` already moved
        to the callee loop body start (`GGET`, previous op `JFORI`)
      - so the caller root links directly into the already-compiled callee loop
        trace before caller add / outer-loop PHIs materialize
    - `retlast_loop` does not have that nested callee loop boundary, so its
      caller root reaches caller add / loop formation directly
  - the next exact target is therefore:
    - decide whether this vararg cliff is simply the normal root-stop behavior
      for a caller trace that enters an already-compiled nested callee loop via
      `BC_JFORI`, or whether there is still a narrower recorder ownership seam
      above that boundary
  - code reading now matches that exactly:
    - [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c#L3597)
      handles `BC_JFORI`
    - when the loop is entered in a root trace, it takes:
      `lj_record_stop(J, LJ_TRLINK_ROOT, bc_d(...))`
    - that matches the observed `sum_loop` `TRACE 2 ... -> 1` stop with
      `prevop=JFORI`, `pc` already moved to the callee loop body start, and
      `link=1`
  - so the current live question is:
    - is caller-root ownership across a call into an already-compiled nested
      callee loop a real remaining optimization surface, or is that just the
      normal boundary on the current mechanism

That branch decision is now clean enough to move the broader-throughput queue:

- `sum_loop` still explains the vararg cliff, but its front-most split is the
  normal root `BC_JFORI -> existing loop` stop into an already-compiled nested
  callee loop
- that is not the next grounded local optimization target on the current
  mechanism
- the broader-throughput queue therefore moves forward to
  [tests/s390x/perf/bitops_mix.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/bitops_mix.lua)

First `bitops_mix` read from clean `kdz`:

- artifact root:
  - [20260331-kdz-bitops_mix-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260331-kdz-bitops_mix-truth-pack)
- raw medians:
  - `mix_bits/small`: JIT-on `0.000273`, `-joff` `0.000105`, ratio `2.60x`
  - `mix_bits/medium`: JIT-on `0.002093`, `-joff` `0.000525`, ratio `3.99x`
  - `mix_bits/hot`: JIT-on `0.007645`, `-joff` `0.002099`, ratio `3.64x`
- focused hot read:
  - JIT-on `0.007662`
  - `-joff` `0.002123`
  - gap `+0.005539s`
  - ratio `3.61x`
- runtime classification:
  - `TRACE_START 0`
  - `TRACE_STOP 0`
  - `TRACE_ABORT 0`
  - `TEXIT_COUNT 0`
  - `compiled-body-dominated`
- interpretation:
  - this is the first clean non-iterator, non-dispatch, non-helper family in
    the current queue that is materially red without any live exit churn
  - so the next work is a real compiled-body / lowering audit, not more
    ownership or exit chasing

Focused backend audit on that family:

- artifact root:
  - [20260401-kdz-bitop-log-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-bitop-log-audit)
  - [20260401-kdz-bitop-log-audit-v2](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-bitop-log-audit-v2)
- the hot chain is exactly the workload shape:
  - `band`, `bxor`, `bor`, shifts, rotates, `bswap`, `bnot`
  - every logged hot op is `IRT_INT`
  - no helper-call seam and no exit seam appear in this classifier
  - the refined producer log shows the hot body is mostly bitop-on-bitop:
    - later `logic` ops repeatedly consume earlier `logic`, `shiftk`, `brolk`,
      `bswap`, and `bnot` producers
    - the chain is not repeatedly reloading a fresh non-bitop value each step
- the s390x backend path is the interesting part:
  - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h#L1428)
    `asm_bitop_logic()`
  - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h#L1443)
    `asm_bitshift()`
  - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h#L1490)
    `asm_brot()`
  - all of those paths currently run through `asm_bnorm32()`
- next exact target:
  - prove whether repeated `asm_bnorm32()` work is being paid across an
    already-`IRT_INT` producer chain in `bitops_mix`
  - only then decide whether one narrow normalization-state / int32-home
    experiment is justified
- focused `asm_bnorm32()` classifier on clean `kdz` now confirms the payer
  shape:
  - [20260401-kdz-bnorm-log-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-bnorm-log-audit)
  - `84` total normalization sites in the focused hot run
  - exact split:
    - `42` unary/shift sites normalize values coming straight from the source
      integer or loop-carried arithmetic
    - `42` binary chain sites normalize results whose left and right inputs are
      already prior bitops
  - so the live backend question is now narrow:
    - can the s390x backend safely carry “already normalized int32” state
      across the binary bitop chain instead of reissuing `asm_bnorm32()` on
      every chain node
    - if not, this family should be closed without opening a backend patch
- first exact skip experiment on that seam is now rejected:
  - [20260401-kdz-bitop-chain-bnorm-skip-direct-v2](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-bitop-chain-bnorm-skip-direct-v2)
  - structural proof:
    - `S390X_BNORM_SKIP` fired on the intended binary chain nodes
  - `kdz` medians with the gate enabled:
    - `mix_bits/small`: `0.000340`
    - `mix_bits/medium`: `0.001965`
    - `mix_bits/hot`: `0.008870`
  - compared with the frozen baseline:
    - `small` regressed from `0.000273`
    - `hot` regressed from `0.007645`
  - conclusion:
    - a plain chain-node `asm_bnorm32()` delete is not promotable
    - any next backend step must be narrower than “skip result normalization on
      binary bitops”
- focused clean-`kdz` mcode dump now confirms the emitted hot-loop shape:
  - [20260401-kdz-bitops-mcode-audit-v3](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-bitops-mcode-audit-v3)
  - using the repo-local dump module, the hot binary chain repeatedly emits:
    - `LGR`
    - `NGR` / `OGR` / `XGR`
    - `LGFR`
  - `BSWAP` likewise emits `LRVR` followed by `LGFR`
  - no 32-bit logical register forms appear in the dumped hot body
- consequence:
  - the current backend is not just “doing some extra normalization”
  - it is explicitly materializing a `64-bit logical op + post-op sign-extend`
    pattern throughout the chain
  - clean source review now shows that this contract is broader than bitops:
    - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h#L1341)
      `asm_add()` uses `asm_bnorm32()` or `LGFR` around integer result ops
    - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h#L1635)
      `asm_sub()` uses the same pattern
    - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h#L1710)
      `asm_mul()` does too
    - [src/lj_emit_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_emit_s390x.h#L97)
      exposes only the 64-bit register forms currently used by these paths:
      `AGR`, `SGR`, `NGR`, `OGR`, `XGR`, `MSGFR`
    - there are no active 32-bit register `AR` / `SR` / `NR` / `OR` / `XR`
      forms to retarget to from the current emitter surface
  - if this family stays open, the next exact target is not another bitops-only
    skip gate
  - it is whether the backend has a broader valid 32-bit integer-result
    lowering surface that would need to be added at all; without that, this
    `bitops_mix` line is close to closure as a local family
- one backend-wide 32-bit integer-result lowering pass is now measured and
  rejected:
  - native `kdz` probes showed the candidate 32-bit ops (`AR`, `SR`, `NR`,
    `OR`, `XR`, `AHI`, `MSR`, `LR`) do not auto-normalize in 64-bit mode
  - so the current backend contract still needs explicit `LGFR` / `LLGFR`
    after those ops
  - first code pass:
    - three-register arithmetic/logical forms plus the existing normalize step
    - clean truth-pack artifact:
      [20260401-kdz-bitops_mix-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-bitops_mix-truth-pack)
    - `mix_bits/hot` regressed to `0.008957`
  - second code pass:
    - two-register `AR` / `SR` / `NR` / `OR` / `XR`
    - `AHI`
    - direct `CC_OF` guards for int32 add/sub overflow
    - selective `LR` setup before a later normalize
    - best clean `kdz` rerun improved to `mix_bits/hot 0.007879`
    - still slower than the frozen `0.007645`
  - follow-up shift setup variants were also negative:
    - remove setup move: `0.008114`
    - `LR` setup move: `0.008079`
  - conclusion:
    - the backend-wide opcode-swap family is not promotable on the current
      normalize-every-result contract
    - if `bitops_mix` stays open, the next real family is a deeper
      normalized-result / int32-home design, not more local opcode
      substitutions
    - first clean `kdz` classifier for that deeper family:
      [20260401-kdz-bitops-int32home-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-bitops-int32home-audit/summary.md)
    - corrected `asm_bnorm32()` split:
      - `chain-binary`: `1337` sites total, `1146` of them pure carry sites
        consumed only by later bitops, `174` first leave the chain through
        `ADD`
      - `source-binary`: `189` sites, all still feed later bitops
      - `source-shift`: `763` sites, all still feed later bitops
      - `source-unary`: `382` sites, all still feed later bitops
    - the only named first non-bitop consumer is op `41` (`ADD`) with `190`
      hits
    - so the next honest backend target is not “fewer `LGFR`s everywhere”
    - it is a carried normalized int32/result-home across the bitop chain with
      one explicit leave-the-chain boundary into integer arithmetic
    - first env-gated carry-skip check on that exact seam:
      [20260401-kdz-bitops-int32home-gate-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-bitops-int32home-gate-check/summary.md)
    - clean `kdz` A/B:
      - baseline `mix_bits/hot 0.008095`
      - gated `mix_bits/hot 0.008593`
      - regression `+0.000498s` (`1.062x`)
    - the gate was not dead:
      - `2392` candidate sites fired in the focused structural probe
      - probe completed cleanly with `REMOTE_RC=0`
    - conclusion:
      - the boundary is real, but plain candidate-site `LGFR` skip is not
        promotable
      - any remaining backend family here has to carry a real normalized
        int32/result-home state rather than simply suppressing producer
        normalization at candidate nodes
    - first low32-home logical-subchain variant is also rejected:
      [20260401-kdz-bitops-low32home-subchain-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-bitops-low32home-subchain-check/summary.md)
    - exact shape:
      - only `band` / `bor` / `bxor`
      - only classifier-proven carry and `ADD`-tail nodes
      - 32-bit `LR` / `NR` / `OR` / `XR` low32-home lowering with no internal
        normalize on those nodes
    - clean `kdz` read:
      - first focused pass:
        - baseline `mix_bits/hot 0.008784`
        - gated `mix_bits/hot 0.008609`
      - same-host rerun without rebuild:
        - baseline `mix_bits/hot 0.008312`
        - gated `mix_bits/hot 0.008749`
    - supporting structural read:
      - reduced gated check completed and hit the new path:
        - `logic32carry 21`
        - `logic32tail 2`
      - the focused trace-count script timed out in both baseline and gated
        forms, so it did not separate the variant structurally
    - conclusion:
      - the gate is live, but the same-host perf result is unstable and not
        promotable
      - this exact low32-home logical-subchain variant is closed
      - if `bitops_mix` stays open from here, the next honest target is a
        deeper consumer-side normalized-result / int32-home design, not more
        local logical-subchain rewrites

## Authoritative Validation Surfaces

- Primary perf host:
  - `kdz:/root/luajit2-s390x/perf-clean-20260330/repo`
  - machine type `8561` (`z15`)
- Regression screen host:
  - `zkd0:/root/luajit2-s390x/perf-clean-20260330/repo`
  - machine type `3906` (`z14`)

Validation rules:

- tracked-file sync only
- direct `src/` rebuild only
- same-host pinned `kdz` A/B is the policy signal
- `zkd0` is regression-only
- low-noise manual logs or debugger only
- no dirty-tree `iterator_probe.py` runs for perf decisions

Checked-in restamp helper:

- [tools/s390x/restamp_iterator_perf.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/restamp_iterator_perf.py)
  now owns the authoritative iterator restamp path
- it syncs tracked files only, rebuilds directly in `src/`, captures both
  `jit.on` and `-joff`, runs the three focused micros, and writes:
  - `metadata.json`
  - `jit-on.jsonl`
  - `joff.jsonl`
  - `summary.md`
  - raw build, micro, owner-log, and IR-dump logs

Checked-in truth-pack helper:

- [tools/s390x/build_iterator_truth_pack.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/build_iterator_truth_pack.py)
  now owns the focused frozen-baseline evidence pack
- it reuses the same tracked-file sync and direct `src/` rebuild path, then
  adds:
  - focused hot medians for value-only hash, key-using hash, and array
    value-only control
  - `-jdump=im` IR+mcode for the same three loops
  - low-noise owner logs
  - smaller non-resume owner-selection probes for the same three loops
  - `jit.attach("trace")` and `jit.attach("texit")` counts after warmup
  - `perf stat` capture when the host supports those events

Checked-in dispatch truth-pack helper:

- [tools/s390x/build_dispatch_truth_pack.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/build_dispatch_truth_pack.py)
  now owns the queued dispatch/side-exit evidence pack
- it reuses the same tracked-file sync and direct `src/` rebuild path, then
  captures:
  - `dispatch_trace` JIT-on and `-joff` medians
  - focused hot medians for `numeric_loop`, `side_exit_loop`, and
    `hotexit_loop`
  - `jit.attach("trace")` and `jit.attach("texit")` counts after warmup
  - `-jdump=ism` IR+mcode for the focused loops
  - focused runtime `JLOOP_EXIT`, `HOTSIDE_FOCUS`, and recorder
    `SIDE_FOCUS` logs for the dominant seam
  - `perf stat` when the host supports those events

Checked-in broader-throughput truth-pack helper:

- [tools/s390x/build_throughput_truth_pack.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/build_throughput_truth_pack.py)
  now owns the first broader-throughput restamp path
- current supported families:
  - `vararg_paths`
  - `bitops_mix`
- it reuses the same tracked-file sync and direct `src/` rebuild path, then
  captures:
  - full-family JIT-on and `-joff` medians
  - focused hot-only medians
  - `jit.attach("trace")` and `jit.attach("texit")` counts after warmup
  - `perf stat` when the host supports those events
  - raw smoke, trace-count, and perf-stat logs
- the first native `vararg_paths` pass also forced one hardening step:
  - focused per-workload probes now run under a fixed timeout instead of
    hanging the entire helper when a hot traced surface wedges
- the helper now also owns reduced vararg handoff probes for this family:
  - `-jv` reduced scripts with `LUAJIT_S390X_RECRET_LOG=1`
  - saved under `raw/handoff`
  - summarized in `handoff-counts.json`

## Queued Dispatch / Side-Exit Frontier

Current `kdz` dispatch hot medians from the latest clean truth pack:

- `numeric_loop/hot`
  - JIT-on `0.350184`
  - `-joff` `0.002168`
  - gap `+0.348016s`
  - ratio `161.52x`
- `side_exit_loop/hot`
  - JIT-on `0.535201`
  - `-joff` `0.004667`
  - gap `+0.530534s`
  - ratio `114.68x`
- `hotexit_loop/hot`
  - JIT-on `0.622022`
  - `-joff` `0.005627`
  - gap `+0.616395s`
  - ratio `110.55x`

Focused runtime read on the same clean rerun:

- `numeric_loop` after warmup:
  - `TRACE_START 11`
  - `TRACE_STOP 11`
  - `TRACE_ABORT 0`
  - `TEXIT_COUNT 2001`
  - `TEXIT_HIST 10:0=200,11:0=200,12:0=200,13:0=58,1:0=142,2:0=1,4:0=200,5:0=200,6:0=200,7:0=200,8:0=200,9:0=200`
- `side_exit_loop` after warmup:
  - `TRACE_START 11`
  - `TRACE_STOP 11`
  - `TRACE_ABORT 0`
  - `TEXIT_COUNT 2001`
  - `TEXIT_HIST 10:0=200,11:0=200,12:0=200,13:0=58,1:0=142,2:0=1,4:0=200,5:0=200,6:0=200,7:0=200,8:0=200,9:0=200`
- `hotexit_loop` after warmup:
  - `TRACE_START 11`
  - `TRACE_STOP 11`
  - `TRACE_ABORT 0`
  - `TEXIT_COUNT 2001`
  - `TEXIT_HIST 10:0=200,11:0=200,12:0=200,13:0=58,1:0=142,2:0=1,4:0=200,5:0=200,6:0=200,7:0=200,8:0=200,9:0=200`
- current `kdz` still reports `perf stat` hardware counters as:
  - `<not supported>`

The key read is stronger now:

- the branch-free numeric loop already reproduced the same pathology
- and the branchy `side_exit_loop` / `hotexit_loop` surfaces now reproduce the
  exact same trace/exit histogram and focused side-entry markers
- so the current dispatch red is not opening a second branch-payload seam
  outside the closed loop-clone mechanism

## Dispatch Seam Attribution

The active seam on the frozen dispatch baseline is now mechanically pinned:

- root `trace 1` starts at `BC_FORL` and stops as a loop
- the hot seam is `trace 1 exit 0`
- focused runtime logs show the hot-side replay at:
  - `pc = BC_MODVN`
  - `prevop = BC_JFORI`
  - `snappc = BC_MODVN`
  - `parent_startop = BC_FORL`
- focused recorder logs show the first side trace enters as:
  - `parent=1 exit=0`
  - `startop = BC_JMP`
  - `startpc == pc == snappc`
  - `parent_snapnent = 0`
- a focused recorder rerun now shows that same first side trace does pass the
  current extra-loop narrow gate:
  - `prev_is_jfori = 1`
  - `fori_target = 1`
  - `target_match = 1`
  - `site=extra_loop_narrow`
- after `sidecheck`, that trace is still on the same bare body-entry state

Current named seam:

- `loop-body-entry-after-JFORI`

Current read:

- the hot failure is in the generic `FORL` / `JFORI` loop-entry path
- it is not a missed side-trace `JFORI` / `FORL` eligibility check
- the current extra-loop narrow path is firing
- hot-side duplication is downstream of that seam
- this is not an iterator seam, not bridge/continuation machinery, and not a
  late backend lowering opportunity

A focused `traceinfo` snapshot on the same `kdz` numeric seam corrects the
owner read:

- the descendants are not staying root-linked stubs
- `trace 3` through `trace 12` are already self-loop loop traces with the same
  `nins=18`, `nk=7`, and `nexit=4`
- the remaining dispatch problem is churn/reuse:
  - equivalent self-loop loop traces keep getting cloned on the same `exit 0`
    seam instead of reusing a stable earlier owner

Dispatch hotside classifiers are now split:

- `LUAJIT_S390X_HOTSIDE_CANON_EQUIV=1` does not fix the problem
- on the focused numeric probe it collapses the observed exit traffic into one
  reused site:
  - `7:0=160743`
- that is not a real owner/materialization win
- `LUAJIT_S390X_HOTSIDE_CANON_CHILD=1` reduces trace churn but not the real
  payer:
  - `TRACE_START` drops from `10` to `6`
  - `TEXIT_COUNT` stays at `2001`
  - the last trace still absorbs `8:0=858`
- `LUAJIT_S390X_HOTSIDE_SHARE_EQUIV=1` timed out after `20s` on the focused
  `numeric_loop` probe with no result and is not safe to treat as a live path

Next exact target from there:

- default hotside reuse/adoption policy itself:
  - on the late steady-state focused probe, default `trace_hotside()` already
    logs `phase=equiv parent=10 exit=0 cand=6 child=7`
  - but with the reuse gates off it still just counts toward `hotexit` and
    starts another trace
  - so the remaining dispatch red is now explicitly a policy choice, not a
    failure to discover equivalent loop owners

Next exact target from there:

- one narrow dispatch-side reuse/adoption experiment that proves a real
  owner/exit win on this seam, or closes the family if it only reproduces the
  earlier branch-hostile classifier behavior

First narrow reuse/adoption experiment from this seam is now rejected:

- `LUAJIT_S390X_HOTSIDE_REUSE_LOOP_CHILD`
  - exact attempt:
    - patch a late `exit 0` self-loop parent directly to an already-existing
      equivalent child loop
    - skip recording another equivalent side trace
  - why it was worth testing:
    - narrower than `CANON_EQUIV`, `CANON_CHILD`, or `SHARE_EQUIV`
    - directly targeted the known `cand` + `child` late seam
  - clean `kdz` structural gate:
    - focused `numeric_loop_trace.lua`
    - `timeout 20`
    - `REMOTE_RC=124`
  - decision:
    - reject before perf
    - do not keep the gate in-tree

The next earlier patch-target classifier from the same seam is now rejected:

- `LUAJIT_S390X_SIDEEXIT_MCLOOP`
  - exact attempt:
    - patch parent side exits to the loop-body target (`T->mcloop`) instead of
      generic trace entry
  - clean `kdz` structural gate:
    - focused `numeric_loop_trace.lua`
    - clean rebuild succeeded
    - the native probe then segfaulted before any trace-count output
  - code-level autopsy:
    - `trace_stop()` can redirect to `J->cur.mcode + T->mcloop`
    - but `mcloop` is only defined as an internal loop-body entry offset
    - the VM consumes that path only through owner/resume-gated entry flow
    - so it is not a generic safe side-exit landing target
  - decision:
    - reject before wider classification
    - do not widen to `side_exit_loop` or `hotexit_loop`

That closed the current dispatch loop-clone mechanism:

- default hotside policy sees equivalent candidates
- direct late child-retarget is unsafe
- patch-target shape does not unlock a safe owner/exit win
- no new seam remains in this mechanism

The follow-up dispatch-adjacent side-exit pass is now also classified:

- `side_exit_loop` and `hotexit_loop` do not expose a second hot seam
- on clean `kdz` they collapse back to the same `loop-body-entry-after-JFORI`
  practical shape:
  - `pc = BC_MODVN`
  - `prevop = BC_JFORI`
  - `startop = BC_JMP`
  - `site=extra_loop_narrow`
- so the current generic dispatch/side-exit line is now closed on this
  mechanism too

Next queued redirect:

1. helper-boundary storage/materialization audits where the ABI may help
2. only then broader JIT throughput families

The first helper-boundary follow-up from that redirect is now classified:

- surface:
  - [hotexit_update_preinterned.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/jit_loops/hotexit_update_preinterned.lua)
  - artifact:
    - [20260331-kdz-href-helper-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260331-kdz-href-helper-audit)
- clean `kdz` result:
  - `TRACE_start iter=2 tr=1`
  - `TRACE_start iter=21 tr=2 otr=1 oex=2`
  - `TRACE_start iter=20 tr=3 otr=1 oex=0`
  - `TRACE_start iter=101 tr=4 otr=3 oex=3`
  - `TRACEINFO tr=1 link=1 type=loop`
  - `TRACEINFO tr=2 link=1 type=root`
  - `TRACEINFO tr=3 link=3 type=loop`
  - `TRACEINFO tr=4 link=0 type=stitch`
- interpretation:
  - the existing helper-backed dynamic `HREF` update path is structurally
    converged on the current tree
  - it does not expose a new broken helper-boundary family and does not reopen
    the older mixed-update hot-exit line

Updated queue:

1. any new helper-boundary work must name a fresh seam first
2. otherwise move to broader JIT throughput families

The first two broader-throughput targets are now fixed:

1. `vararg_paths`
2. `bitops_mix`

That order is intentional:

- `vararg_paths` is the first ABI-sensitive throughput family that avoids the
  closed iterator and dispatch seams
- `bitops_mix` is the first helper-light control for telling exit-heavy red
  from pure compiled-body red

## Frozen Iterator Baseline

The current promotable iterator perf baseline is the four-piece recorder split
in [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c):

- array visible numeric key comes from successor index:
  - `HIOP(trvk) - 1`
- non-array visible key stays lazy
- hash table live-in is trusted and read-only in `rec_isnext()` / `rec_itern()`
- non-array value lane is seeded from `ix.val` so the hash root trace no longer
  frame-source the visible value slot

This is a split policy, not a full-lazy collapse.

## Current Checkpoint Baseline

Frozen checkpoint branch:

- `k8ika0s/s390x-jit-on-freeze-20260331`

Latest checkpoint evidence:

- `kdz` truth pack:
  - `pairs_sum/hot median=0.066259`
  - `pairs_array_sum/hot median=0.069155`
- `zkd0`:
  - `pairs_sum/hot median=0.104839`
  - `pairs_array_sum/hot median=0.097884`

Current same-harness `-joff` comparator on `kdz`:

- `pairs_sum/hot median=0.005540`
- `pairs_array_sum/hot median=0.003716`

These are the numbers new iterator perf work must beat.

The earlier freeze-point reference is still useful as a historical anchor:

- `kdz`: `0.059818 / 0.061622`
- `zkd0`: `0.093881 / 0.087945`

But the measured branch-tip contract is now the post-cleanup restamp above,
not the older reference.

## Distance To Expectation

Current `kdz` JIT-on distance to same-harness `-joff`:

- `pairs_sum/hot`
  - JIT-on `0.066259`
  - `-joff` `0.005540`
  - gap `+0.060719s`
  - ratio `11.96x`
- `pairs_array_sum/hot`
  - JIT-on `0.069155`
  - `-joff` `0.003716`
  - gap `+0.065439s`
  - ratio `18.61x`

Delivery ladder from the current `kdz` restamp:

- Restamp bar:
  - still failed
  - hash `+10.77%` slower than the earlier freeze-point reference
  - array `+12.22%` slower than the earlier freeze-point reference
- Recovery bar:
  - hash target `<= 0.056341`, current gap `+0.009918s`
  - array target `<= 0.059806`, current gap `+0.009349s`
- First real-results bar:
  - hash target `<= 0.050000`, current gap `+0.016259s`
  - array target `<= 0.055000`, current gap `+0.014155s`

## What The Current Baseline Proved

- The branch is no longer blocked on the old late crash in dispatch helper
  errno handling.
- Array and hash do not pay the same owners.
- Array-side post-call numeric key-lane waste was reduced by deriving the
  visible numeric key from the successor index instead of rereading the helper
  tuple key lane.
- Hash-side eager visible-key and table-slot costs were both removed.
- Hash root traces no longer frame-source the visible value lane.
  - helper `VLOAD #0` now feeds the hash add path directly
  - the old extra frame value `SLOAD` is gone

## Current Owner Map

Low-noise manual logging and raw IR on the post-cleanup branch tip show:

- Value-only hash:
  - dominant shared payer is still `addov_rr_int_eq`
  - main non-value cluster is still the hidden `KEYINDEX` load
  - only other frame `SLOAD` is the carried total slot
  - helper `VLOAD #0` feeds the visible value lane directly
- Key-using hash:
  - still pays shared `addov_rr_int_eq`
  - still pays the hidden `KEYINDEX` load
  - adds a visible key/type `SLOAD`
- Array value-only control:
  - still pays shared `addov_rr_int_eq`
  - still pays numeric-key control loads

Current read:

- shared `addov_rr_int_eq` is now the dominant cross-family payer
- hash still carries the hidden `KEYINDEX` load cluster
- array still carries numeric-key control loads
- the refreshed owner map did not expose a new target outside the reject pile

## Freeze-Point Truth-Pack Decision

The newest focused truth pack answered the next gating question directly:

- steady-state trace and exit activity is still materially nonzero after
  warmup
- value-only hash:
  - `TRACE_START 10`
  - `TRACE_ABORT 9`
  - `TEXIT_COUNT 960000`
- key-using hash:
  - `TRACE_START 10`
  - `TRACE_ABORT 9`
  - `TEXIT_COUNT 640000`
- array value-only control:
  - `TRACE_START 11`
  - `TRACE_ABORT 10`
  - `TEXIT_COUNT 960000`

So the remaining red is not yet just compiled-loop throughput. The next
justified target is still root-trace or side-trace ownership on the frozen
baseline, starting from the exact steady-state exit site for value-only hash.
This is not permission to reopen bridge work, no-guard families, or backend
micro-surgery.

Focused non-resume owner-selection follow-up on `kdz` now gives the next
decision boundary directly:

- the root-`ITERN` resume-contract family is closed again on the current tree
- the next open seam is non-resume owner selection only

Value-only hash:

- root `trace 1` still stops as `link=1`, `linktype=2`, `startop=70`
- the hot steady-state seam is still:
  - `S390X_JLOOP_EXIT phase=dispatch-original parent=1 exit=1 trace=1`
- the first materially different owner candidate is `trace 2`
- that candidate does not reach child-link/runtime owner logic
- it dies immediately in recorder loop-stop handling:
  - `S390X_RECSETUP site=root_ready trace=2 ... startop=79`
  - `S390X_LINNER site=rec_loop_jit_root trace=2 ...`
  - `S390X_TRACE_ABORT trace=2 ... err=9`

Key-using hash:

- same owner-selection outcome as value-only hash
- the first candidate also dies in `rec_loop_jit_root` before any later
  ownership machinery can matter

Array value-only control:

- root `trace 1` also spends the early hot seam in `dispatch-original`
- its first side trace gets farther than hash:
  - `trace 2 parent=1 exit=1 root=1 startop=88`
  - repeated nil-path aborts with `err=8`
  - eventual stop as `linktype=6`, `link=0`, `root=1`
- later descendants do stop, but they are still root-linked:
  - `trace 3`, `trace 4`, `trace 6` stop as `linktype=1`, `link=1`, `root=1`

Current read:

- hash dies too early, in
  [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c)
  inside `rec_loop_jit()`
- array survives farther, but still first lands in interpreter/root-linked
  ownership instead of a stable non-root owner
- so the next valid code family, if one exists at all, is one narrow
  non-resume owner-selection cut that changes that exact outcome

Fresh proof artifacts from the checked-in helpers:

- `kdz` truth-pack bundle:
  - [summary.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260331-kdz-nonresume-owner-selection/summary.md)
- `zkd0` restamp bundle:
  - [summary.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/restamps/20260331-zkd0-nonresume-owner-screen/summary.md)
- focused array owner probe:
  - [stdout.log](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260331-kdz-array-owner-probe/stdout.log)
  - [stderr.log](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260331-kdz-array-owner-probe/stderr.log)
- value-only hash IR proof on `kdz`:
  - [hash_value.stdout.log](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260331-kdz-nonresume-owner-selection/raw/dump/hash_value.stdout.log)
  - still shows:
    - `int VLOAD 0005 #0`
    - `int SLOAD #3 T`
    - `int ADDOV`
  - and no extra visible value-lane frame `SLOAD`

Corrected finite owner-selection rerun on `kdz`:

- the first rerun target was a fresh truth-pack directory using the fixed
  finite owner-selection probe path:
  - [hash_value.stderr.log](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260331-kdz-nonresume-owner-selection-v3/raw/owner-selection/hash_value.stderr.log)
  - [hash_key.stderr.log](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260331-kdz-nonresume-owner-selection-v3/raw/owner-selection/hash_key.stderr.log)
- that corrected rerun tightens the mechanism:
  - value-only hash and key-using hash share the same first-side failure
  - on both loops, the hot path stays in:
    - `S390X_JLOOP_EXIT phase=dispatch-original parent=1 exit=1 trace=1`
  - the first fresh root candidate is still:
    - `trace 2 startop=79`
    - `S390X_LINNER site=rec_loop_jit_root`
    - `err=9`
  - and the side attempts still show:
    - `TRACE 2 start 1/1`
    - `abort ... leaving loop in root trace`
- current read after the corrected rerun:
  - the hash owner-selection seam is not a distinct later runtime-owner
    problem
  - it is the same first-side nil-descendant / unloaded-visible-key family
    already exposed by the earlier focused hash seam probes
  - subagent forensics and the mature-control diff both pin the first
    divergence earlier, at the `rec_itern()` payload-vs-nil fork on `ix.key`
    after the helper result already exists
  - so this family is closed again on the current tree
  - any future cut must be genuinely different from those rejected
    first-side lazy-key classifiers

Four-track frozen-baseline restamp on `kdz` and `zkd0` now closes the current
iterator reopening window:

- authoritative `kdz` truth pack:
  - [summary.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260331-kdz-frozen-baseline-v3/summary.md)
  - `pairs_sum/hot median=0.062519`
  - `pairs_array_sum/hot median=0.066353`
  - focused same-harness `-joff` gaps:
    - `hash_value` `10.84x`
    - `hash_key` `11.58x`
    - `array_value` `15.58x`
  - `perf stat` is still unsupported, so the active exit/body attribution uses
    the runtime fallback section in the truth pack
  - all three focused loops still classify as `exit-dominated`
- exact seam read from that restamp:
  - `hash_value` and `hash_key` still classify as the same closed first-side
    lazy-key family
  - `array_value` still reaches the payload/root-linked side path, but not a
    new iterator family worth opening
  - `rec_loop_jit_root` remains a downstream symptom, not a new stop-target
    seam
- ABI-aware preserved-GPR audit is also negative on the current tree
  - no proven loop-carried value is being dropped only because current s390x
    register-home/liveness fails to keep it in a preserved GPR across
    `lj_vm_next`
- `zkd0` regression screen:
  - [summary.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/restamps/20260331-zkd0-post-tracks-screen/summary.md)
  - `pairs_sum/hot median=0.119175` (`+26.94%` vs frozen)
  - `pairs_array_sum/hot median=0.120802` (`+37.36%` vs frozen)

Queueing decision:

- no new iterator seam is open from the current mechanism
- iterator stays frozen at the current Lane A + Lane B checkpoint
- next queued perf workstream moves to dispatch/side-exit

## What Is Rejected

These are not active perf candidates anymore:

- full-lazy collapse
- any `KEYINDEX` no-guard path
- direct `KEYINDEX` tag-word compare
- backend dedup of `KEYINDEX` guard generation
- hidden-control carry through the unused visible-key slot
- body-scan loopback overrides as landing policy
- `TRACE 2` churn elimination as a perf proxy
- bridge-local producer and consumer fusion
- exact `rec_itern()` accumulator preloads that still leave the root trace on
  `int SLOAD #3` plus `ADDOV`
- accumulator-to-`num` cuts that still keep the loop-unroll `int.num` check
- backend `AR/SR` overflow rewrites
- backend `AGFR/CGFR` equality-guard rewrites

The common failure modes were:

- semantic breakage
- cross-host regression
- same-host pinned `kdz` regression
- or real structural change with no promotable hot-loop win

## Current Gate Result

The one remaining accumulator-family pass was tried and rejected.

- Exact experiment:
  - preload the exact iterator accumulator slot from `rec_itern()` as a real
    `num` `SLOAD`
  - add the minimal s390x `num-from-int` `IRSLOAD_CONVERT` path needed to
    support that slot load
- Structural result:
  - rejected immediately
  - raw IR on `kdz` still showed:
    - `int SLOAD #3`
    - `int ADDOV`
  - the carried slot was not actually born as `num`
  - the back-edge `int.num` problem therefore was not removed
- Decision:
  - there is no remaining justified accumulator-family pass from the current
    mechanism
  - do not reopen that family unless a future cut can prove the original
    carried slot becomes `num` before `loop_unroll()` sees it

The one allowed backend classifier also came back negative:

- the surviving hash `sload_keyindex` / `sload_type` cluster does not lower as
  a plain load + compare + branch sequence
- current lowering in
  [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h)
  is a load + tag-extract shift + compare + branch sequence
- there is no narrow semantic-preserving load/test or compare/branch fusion
  candidate visible from the current lowering

## Future Entry Gate

Do not start another iterator perf patch unless all of these are true first:

- a named remaining payer exists
- there is a direct structural proof target in raw IR or low-noise logs
- the idea is not already in the reject pile
- the candidate can be tested against this exact measured branch-tip `kdz` baseline with
  `zkd0` used only as a regression screen

Current status against that gate:

- the checkpoint truth pack is complete on `kdz`
- the minimal checkpoint regression screen is complete on `zkd0`
- the owner map is refreshed
- the next open question is now narrower:
  - can hash `exit 1` survive past `rec_loop_jit_root` into a materially
    different non-root owner shape, or is this family exhausted?
- there is still no justified new code-level perf patch until that site is
  identified cleanly

Acceptable future target shapes:

- one new recorder/live-in idea that removes a remaining root-trace
  storage/control read
- one new semantic-preserving lowering idea only if it targets an actually
  fuseable sequence, not a hoped-for micro-op win

Unacceptable future target shapes:

- anything whose main claim is “fewer backend instructions”
- anything whose proof is only “the IR looks cleaner”
- anything that depends on bridge or continuation policy
- anything that treats the reduced `BC_ISF` path from the rejected
  signed-extraction prototype as the next workload seam

Current queue correction:

- the reduced `BC_ISF` path is probe-hook Lua from the trace/texit counter
  callbacks, not the promoted-slice workload body
- future reruns of any header-repair idea should use the no-counter reduced
  mechanism probe path first
- the real remaining question is still the helper-backed wrong-result path once
  the hidden-`STEP` typecheck starts passing
- that helper-backed wrong-result path is now narrowed further:
  - with both counters and post-run `traceinfo/traceir` hooks removed, the
    arithmetic-shift repair runs the helper reducer correctly on the first hot
    pass
  - the correctness break appears on the second hot run immediately after the
    first successful long run
  - current repeated replay seam under that local-only gate is:
    - `trace 1 exit 0`
    - restored `BC_UGET`
    - exact-taken `guardmark=0xe`
    - on the real workload trace, `guardmark=0xe` is `curins 14`,
      `int MULOV 0003 +65537`
    - `0003` is `int SLOAD #4 TI`
    - runtime state at that seam is packed numeric-`for` replay, not a plain
      loop index
  - source-side contract correction:
    - the VM integer `FORI/FORL` fast path on s390x is explicitly
      `checkint -> 32-bit add -> setint -> store`
    - so the remaining seam is now the replay materialization path before the
      header `MULOV`, not the tag compare by itself
  - tighter handoff correction from the real workload dump:
    - `TRACE 1` is the loop trace and starts at `BC_FORL`
    - `TRACE 2` is a tiny `FUNCF` root that only proves `n` is in range and
      stops `-> 1`
    - that `stop -> 1` shape matches the compiled-loop handoff path for an
      already-compiled loop, not the VM `FORI/FORL` path
    - that means the second hot run can reach `TRACE 1` through a
      function-entry handoff, not only through the VM `FORI/FORL` path
    - `lj_snap_replay()` only recreates inherited `IR_SLOAD` refs on that
      path, so the current replay bug is no longer “typecheck but no clear
      32-bit arithmetic value inside trace 1” in the abstract
    - it is “the handoff into trace 1 is still not rebuilding the numeric-for
      index/current-value state the way VM `FORI/FORL` does before trace 1
      consumes it”
    - current best source candidate:
      [rec_for(..., isforl=0)](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c#L1127)
      can prove loop entry and stop into the compiled loop, but it does not
      emit the VM-style integer update/store mirror that would rebuild
      `IDX/EXT` before the handoff

## Promotable Patch Gate

A future iterator patch is promotable only if it:

- keeps value-only hash, key-using hash, and array control micros green
- beats the measured branch-tip `kdz` baseline
- does not regress `zkd0`
- removes a real steady-state payer in IR or low-noise logs
- does not rely on branch-shape churn or late backend micro-surgery

## Benchmark And Logging Commands

From the clean local repo, drive the authoritative host restamp with:

```sh
python3 tools/s390x/restamp_iterator_perf.py \
  --host kdz \
  --output-dir artifacts/s390x/restamps/20260331-kdz-post-cleanup-restamp2

python3 tools/s390x/restamp_iterator_perf.py \
  --host zkd0 \
  --output-dir artifacts/s390x/restamps/20260331-zkd0-post-cleanup-restamp
```

From the clean local repo, drive the frozen-baseline truth pack with:

```sh
python3 tools/s390x/build_iterator_truth_pack.py \
  --host kdz \
  --output-dir artifacts/s390x/truth-packs/20260331-kdz-frozen-baseline-truth-pack
```

The helpers enforce:

- tracked-file sync only
- direct `src/` rebuild only
- `S390X_PERF_SAMPLES=9`
- `S390X_PERF_WARMUP=2`
- pinned `taskset -c 0` benchmark runs
- both `jit.on` and `-joff` in the same restamp

Equivalent manual `kdz` benchmark command from the clean remote repo:

```sh
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
make -C src clean && make -C src -j4
taskset -c 0 ./src/luajit tests/s390x/perf/iterator_table.lua
```

Focused low-noise owner mapping:

```sh
LUAJIT_S390X_ADD_LOG=1 LUAJIT_S390X_SLOAD_LOG=1 ./src/luajit /tmp/hash_value.lua
LUAJIT_S390X_ADD_LOG=1 LUAJIT_S390X_SLOAD_LOG=1 ./src/luajit /tmp/hash_key.lua
LUAJIT_S390X_ADD_LOG=1 LUAJIT_S390X_SLOAD_LOG=1 ./src/luajit /tmp/array_value.lua
```

## GC64 Replay Repair Slice

Clean `kdz` paired-gate check on the promoted `number_helper_loop` seam:

- artifact:
  [20260402-kdz-gc64-signed-sload-plus-jfori-handoff](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-gc64-signed-sload-plus-jfori-handoff/summary.md)
- gates:
  - `LUAJIT_S390X_GC64_SIGNED_INT_SLOAD=1`
  - `LUAJIT_S390X_JFORI_INTERP_HANDOFF=1`
- result:
  - the earlier second-hot wrong-result path is corrected on both the pure-add
    sibling and the real helper workload
  - but the steady promoted-slice counters remain flat:
    - baseline `TRACE_START 7`, `TEXIT_COUNT 64001`
    - paired gate `TRACE_START 7`, `TEXIT_COUNT 64001`
  - the tiny entry trace changes from `root -> 1` to `interpreter`, but the
    dominant repeated seam stays `trace 1 exit 0`

So this pair is a real correctness probe, not a promotable perf fix.

## Numeric-for Header Split

Clean `kdz` reduced recorder/header probes now split the dynamic numeric-for
seam:

- on the real `number_helper_loop` path, recorder logging shows hidden
  `STEP` already constantizes while hidden `STOP` stays inherited from runtime
  argument `n`
- a literal-stop sibling (`for i = 1, 400 do`) constantizes both hidden
  `STOP` and `STEP`
- the repeated exit flurry still survives on that literal-stop sibling
- a clean isolated literal-stop probe now pins the steady shifted seam:
  - artifact:
    [20260402-kdz-number-helper-literal-stop-exact-seam](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-number-helper-literal-stop-exact-seam/summary.md)
  - `TRACE_START 1`, `TRACE_STOP 1`, `TRACE_ABORT 0`, `TEXIT_COUNT 400`
  - dominant texit `7:0=400`
  - dominant exit still restores at:
    - `op 45`
    - `snapop 45`
    - `snapnent 0`
  - exact runtime guard:
    - `guardmark=0xd`
    - `curins 13`
    - `IR SLOAD`
    - `op1 2`
    - `op2 4`
    - `sload_int ofs 0 extra 4`
  - in the isolated trace IR, that guard is:
    - `0013 > int SLOAD #2 T`
    - immediately before the carried-total add
  - so the shifted steady seam is the carried `total` reload, not a helper
    lookup guard
- the earlier mixed `guardmark=0xd` / `GGET` read was a later alternating
  family inside a broad artifact, not the steady literal-stop seam
- a focused slot-log follow-up closes the rematerialization question:
  - artifact:
    [20260402-kdz-number-helper-literal-stop-slotlog](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-number-helper-literal-stop-slotlog/summary.md)
  - at the exact repeated `guardmark=0xd` seam:
    - restored carried `total` is already present as a valid int TValue:
      - `S390X_SLOT idx=0 itype=-14 u64=0xfff9000000030003`
      - low word `0x00030003` matches the expected carried total
    - nearby loop state is also coherent (`3`, `400`, `1`)
  - so the shifted seam is not “missing carried-total rematerialization”
  - it points back to the inherited GC64 integer `SLOAD`
    typecheck/extraction contract on valid restored int slots

So the next seam is no longer “can recorder constantize numeric-for header
constants?” It is the inherited GC64 integer `SLOAD` replay/typecheck family
on valid restored carried state exposed after that constantization.

## GC64 Repair Rejection

The direct inherited-int replay/typecheck mismatch was real, but the first
exact repair is rejected and is not in branch source.

What was tested:

- signed/arithmetic GC64 integer `SLOAD` extraction with the corrected signed
  expected constant
- matching `JFORI` interpreter handoff

What carried:

- reduced `kdz` localized helper probe:
  [20260402-kdz-dynamic-local-after-signed-expected-fix](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-dynamic-local-after-signed-expected-fix/summary.md)
  - correctness restored
  - old inherited `SLOAD(op1=5)` seam cleared
- reduced `kdz` real helper probe:
  [20260402-kdz-number-helper-after-signed-expected-fix](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-number-helper-after-signed-expected-fix/summary.md)
  - reduced correctness restored
  - live seam moved forward

What failed:

- real helper truth-pack on `kdz`:
  [20260402-kdz-be_helpers-hotside_canon_share_uget_looproot_default-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260402-kdz-be_helpers-hotside_canon_share_uget_looproot_default-truth-pack/raw/jit-on.stderr.log)
  - `number_helper_loop/hot: expected 1323881804, got 34304`

Failure shape:

- first long hot run is correct
- second long hot run in the same process is wrong
- smaller second runs still pass; failure starts only at larger reruns
- direct second-run counter check on `kdz`:
  - `TRACE_START 3`
  - `TRACE_STOP 2`
  - `TRACE_ABORT 1`
  - `TEXIT_COUNT 2`

The moved seam is now the warmed overflow side-loop continuation, not the
original inherited `SLOAD` compare:

- `TRACE 1`: main loop
- `TRACE 2 (1/0)`: overflow side loop
- `TRACE 3`: fallback/interpreter path
- hot shifted body:
  - `num CONV`
  - `num MUL`
  - `int TOBIT`
  - `int ADD`

So the current branch state is:

- inherited GC64 integer `SLOAD` mismatch: understood
- corrected signed compare: useful classifier, rejected as unsafe
- next live seam: warm-built overflow side loop on the real helper workload

The next reduced two-run dump removes one wrong continuation read:

- [20260402-kdz-signedfix-two-run-noprint-mid](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-signedfix-two-run-noprint-mid/summary.md)
  - `WARM 132610`
  - `SECOND 25535`
  - `TRACE 1`: main int loop
  - `TRACE 2 (1/2)`: overflow side path, `stop -> 1`
  - `TRACE 3 (1/0)`: warmed overflow loop
  - `TRACE 4 (3/3)`: return-side continuation at line `8`,
    `return bit.tobit(total)`, `stop -> 1`
  - `TRACE 5 (4/0)`: later stitch into `print`
  - exact narrowed return seam:
    - `trace 4 exit 0`
    - `guardmark=0xd`
    - `TRACE 4` `curins 13`
    - `0013 > p64 RETF ...`

So the post-repair seam is no longer best described as generic overflow-loop
replay. The next honest target is the helper return-to-caller continuation
after that warmed overflow loop, specifically the `RETF` / lower-frame return
handoff, not the later print stitch.

The next two reduced controls close that further.

- direct `RETF` probe on `kdz`:
  - `/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-retf-runtime-contract/raw`
  - two-call-site reducer still gives:
    - `WARM 132610`
    - `SECOND 25535`
  - `TRACE 4` really does specialize to one lower-frame caller PC and then
    exit when the later call site returns:
    - recorder `frame_pc=0x...6b70`
    - runtime taken exit carries `r2=0x...6b70`, `r11=0x...6b7c`
  - local bytecode listing explains the `0xc` gap as the two top-level call
    sites:
    - `warm = run(64000)`
    - `second = run(40000)`
- stable-callsite control on `kdz`:
  - `/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-retf-single-callsite-loop/raw`
  - same-caller-site loop still fails:
    - `RESULT 25535`
  - but the seam moves past direct `RETF` and into the caller loop header:
    - `TRACE 4` contains:
      - `p64 RETF`
      - `int SLOAD #6 RI`
      - `int SLOAD #5 TI`
      - `int ADD`
      - `int LE`
    - exact taken exit is:
      - `trace 4 exit 2`
      - restored `pc op=76`
      - `guardmark=0x11`

So the current post-repair read is:

- polymorphic lower-frame return PCs create a real `RETF` side seam
- but stable-callsite replay still fails
- the live family has moved into the caller numeric-for header after return,
  not generic `RETF` alone

The next caller-loop slot-state slice closes that further:

- [20260402-kdz-caller-forl-seam](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-caller-forl-seam/raw)
  - exact taken exit on the stable-callsite control:
    - `trace 4 exit 2`
    - restored `pc op=76`
    - `guardmark=0x11`
  - local bytecode listing identifies `op=76` as the caller `FORL` in
    `drive(n, reps)`
  - slot-state at that exact exit is coherent for the caller loop:
    - caller `idx=3 -> 2`
    - caller `stop=4 -> 2`
    - caller `step=5 -> 1`
    - caller visible current/ext `idx=6 -> 2`
  - but the caller-visible result slot is already wrong:
    - caller `out idx=2 -> 25535`

So the stable-callsite post-repair failure is no longer honestly described as
caller loop-state corruption. The wrong value is already in the returned result
slot when the normal caller `FORL` exit happens.

Reduced helper variants after the repair show the seam is helper-form
specific:

- [20260402-kdz-postrepair-helper-variant-only](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-postrepair-helper-variant-only/summary.md)
  - original helper form:
    - `number_helper_literal_stop`: `TRACE_START 1`, `TEXIT_COUNT 399`
  - local helper form:
    - `number_helper_local_tobit`: `TRACE_START 1`, `TEXIT_COUNT 0`
  - arg helper form:
    - `number_helper_arg_tobit`: `TRACE_START 2`, `TEXIT_COUNT 0`

So the next exact target is helper-form interaction with the inherited
numeric-for index/current-value `SLOAD` seam, not another inherited GC64
integer extraction variant and not generic `TGETS`.

Dynamic helper localization does not clear the promotion bar on the real
workload:

- [20260402-kdz-dynamic-helper-localization-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-dynamic-helper-localization-check/raw/number_helper_loop_local_tobit.stderr.log)
  - `number_helper_loop_local_tobit`
  - `RESULT -149783296`
  - `TRACE_START 321`, `TRACE_STOP 321`, `TRACE_ABORT 0`, `TEXIT_COUNT 64001`
  - ends with `table overflow`

So the reduced local/arg zero-exit split is evidence only. The next exact
target stays on the dynamic helper-form interaction with the inherited
numeric-for index/current-value `SLOAD` seam.

Stripped reduced real-workload localization runs narrow that interaction
further:

- local helper:
  [20260402-kdz-dynamic-local-iter400](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-dynamic-local-iter400/summary.md)
- arg helper:
  [20260402-kdz-dynamic-arg-iter400](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-dynamic-arg-iter400/summary.md)

Both reduced real-workload dynamic localized forms stay finite and now agree on
the same moved replay seam:

- `RESULT 961100104`
- restored `pc op=18`
- restored `snapop=18`
- repeated exact-taken `guardmark=0x3`
- recorder setup plus reduced `TRACEIR` now pins the localized frame layout:
  - `baseslot=2`
  - `op1=3` -> carried `total`
  - `op1=4` -> localized `tobit`
  - `op1=5` -> current numeric-for value feeding `* 65537`
  - `op1=6` -> loop bound `n`
- exact moved inherited guard on both:
  - `curins=3`
  - `IR=SLOAD`
  - `op1=5`
  - `op2=36`
  - `kind=sload_int`
  - `ofs=24`
  - `extra=28`

So the promoted-slice replay problem is no longer best described as imported
helper `BC_UGET` churn once the helper is localized. It survives as
stack-visible helper/value `BC_MOV` replay one step later in the header/call
setup, and the exact shifted replay lane is now the same inherited current
numeric-for-value `SLOAD` on both localized forms.

A focused reduced slot-state follow-up now closes the remaining
rematerialization split:

- [20260402-kdz-dynamic-local-slotlog](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-dynamic-local-slotlog/summary.md)
- repeated seam still lands on:
  - `trace 1 exit 0`
  - restored `pc op=18`, `snapop=18`
  - exact taken `guardmark=0x3`
  - `curins=3`, `IR=SLOAD`, `op1=5`, `op2=36`
- but the replayed loop state is already coherent and advancing:
  - current value register `r11`: `0x3`, `0x4`, `0x5`, ...
  - carried `total` dump `r3tv q0`:
    - `0xfff9000000060006`
    - `0xfff90000000a000a`
    - `0xfff90000000f000f`
    - `0xfff9000000150015`

So the live localized seam is not missing current-value rematerialization. It
is the inherited integer `SLOAD` replay/typecheck contract still firing on a
live current numeric-for value lane.

Current-`HEAD` slot logging now makes that stronger:

- [20260402-kdz-dynamic-local-slotlog-v2](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-dynamic-local-slotlog-v2/summary.md)
- `S390X_SLOADMAP` for the repeated seam shows:
  - `curins=3`
  - `op1=5`
  - `ofs=24`
  - `base=12`
- the runtime exit dump for the same seam shows `r12 == L->base`
- the slot logger then proves the loaded lane is the correct live slot:
  - `baseslot=2`, so `op1=5 -> idx=3`
  - `S390X_SLOT idx=3` is a valid boxed int and advances
    `3, 4, 5, ...`

So this is no longer a stale-slot theory. The inherited integer `SLOAD`
typecheck is firing on the correct live current-value slot.

## Current GC64 Pair Read

The current opt-in GC64 repair pair is now closed as a correctness-positive but
perf-inert branch on the active helper slice:

- opt-in pair:
  - `LUAJIT_S390X_GC64_SIGNED_INT_SLOAD=1`
  - `LUAJIT_S390X_JFORI_INTERP_HANDOFF=1`
- cross-host correctness checks:
  - [20260402-kdz-number-helper-optinpair-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-number-helper-optinpair-check/raw/stdout.log)
  - [20260402-zkd0-number-helper-optinpair-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-zkd0-number-helper-optinpair-check/raw/stdout.log)
  - both return the expected:
    - `WARM -149783296`
    - `SECOND -149783296`
- stable-callsite control on `kdz` is also correct under the same pair:
  - [20260402-kdz-recret-slotlog-signedfix-v1](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-recret-slotlog-signedfix-v1/raw/stdout.log)
  - `RESULT -2050009568`
  - no `lua_lower_frame_retf`; only ordinary `lua_intrace_return`

Authoritative helper-backed restamp on `kdz`:

- [20260402-kdz-be_helpers-hotside_canon_share_uget_looproot_default-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260402-kdz-be_helpers-hotside_canon_share_uget_looproot_default-truth-pack/summary.md)
- `number_helper_loop/hot`: `0.008248` vs `-joff 0.002282`
- `be_pack_loop/hot`: `0.023373` vs `-joff 0.018732`
- focused read stays `exit-dominated`:
  - `TRACE_START 6`
  - `TRACE_STOP 5`
  - `TRACE_ABORT 1`
  - `TEXIT_COUNT 64001`

Focused mechanism read under the pair:

- [20260402-kdz-number-helper-optinpair-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-number-helper-optinpair-mechanism/summary.md)
- dominant seam is still:
  - `trace 7 exit 0`
  - restored `BC_UGET`
  - first `sload_int`: `curins 15`, `IR=SLOAD`, `op1=3`, `ofs=8`, `extra=12`

So the current pair no longer supports the old lower-frame return-value failure
story. It is correct on both hosts, but on the active helper slice it does not
materially change the steady perf seam or the hot medians.

## Current Promotion-Core Seam Read

The remaining promoted-default red is now pinned as one shared replay seam
across the representative core winners on `kdz`:

- `number_helper_loop`
- `be_pack_loop`
- `direct_abs`

The shared steady signature is:

- exact runtime guard:
  - `curins 3`
  - `sload_int`
  - `ofs 16`
  - `extra 20`
- reduced dump front lane:
  - `0003 >  int SLOAD  #4    TI`

New reduced artifacts:

- [20260402-kdz-direct-abs-current-seam](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-direct-abs-current-seam)
- [20260402-kdz-be-pack-current-seam](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-be-pack-current-seam)
- [20260402-zkd0-direct-abs-current-seam](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-zkd0-direct-abs-current-seam)
- [20260402-zkd0-be-pack-current-seam](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-zkd0-be-pack-current-seam)

That means the remaining gap is no longer best described as a helper-only
header problem. It is a shared dynamic-stop numeric-for replay seam on the
current promoted-default slice, and the same exact reduced signature is present
on both `kdz` and `zkd0`.

## Reduced Route-Around Split

New reduced `kdz` route-around probes show the current floor can be escaped,
but not by one generic trick:

- [20260402-kdz-direct-abs-literal-stop-seam](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-direct-abs-literal-stop-seam)
  - `TRACE_START 4`
  - `TRACE_STOP 3`
  - `TEXIT_COUNT 1`
- [20260402-kdz-be-pack-literal-stop-seam-nodump](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-be-pack-literal-stop-seam-nodump)
  - `TRACE_START 1`
  - `TRACE_STOP 0`
  - `TEXIT_COUNT 399`
  - dominant exact guard:
    - `trace 7 exit 0`
    - `op 45`
    - `curins 33`
    - `sload_int ofs 0 extra 4`
- [20260402-kdz-be-pack-literal-stop-local-ops](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-be-pack-literal-stop-local-ops)
  - `TRACE_START 1`
  - `TRACE_STOP 1`
  - `TEXIT_COUNT 0`
- [20260402-kdz-be-pack-loop-local-ops](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-be-pack-loop-local-ops)
  - `TRACE_START 4`
  - `TRACE_STOP 3`
  - `TEXIT_COUNT 401`
  - dominant exact guard:
    - `trace 4 exit 0`
    - `op 18`
    - `curins 3`
    - `sload_int ofs 48 extra 52`

So:

- static stop is enough to largely free `direct_abs`
- static stop alone is not enough for `be_pack`
- localizing `bit` helpers frees the literal-stop `be_pack` reducer
- but on the real dynamic-stop `be_pack` shape, helper localization only moves
  the replay seam later instead of removing the exit-dominated floor

## Route-Around Quantification

Helper-backed `kdz` A/B now closes the reduced route-around subgroup as a
performance remediation lane:

- baseline:
  [20260402-kdz-route_around_reducers-baseline-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260402-kdz-route_around_reducers-baseline-truth-pack/summary.md)
- promoted default:
  [20260402-kdz-route_around_reducers-hotside_canon_share_uget_looproot_default-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260402-kdz-route_around_reducers-hotside_canon_share_uget_looproot_default-truth-pack/summary.md)

Important corrections:

- `stored_abs_literal_stop` is not a valid reducer on remote `kdz`:
  - direct reduced probes return `RESULT 0` under both baseline JIT and the
    promoted default
  - it was removed from the throughput family and is out as route-around
    evidence on this host
- the remaining valid `be_pack` subgroup stays slower than `-joff` throughout:
  - `be_pack_literal_stop`
    - baseline `0.241946` vs `0.047685`
    - promoted default `0.073034` vs `0.046844`
  - `be_pack_literal_stop_local_ops`
    - baseline `0.126152` vs `0.019453`
    - promoted default `0.126189` vs `0.019927`
  - `be_pack_loop_local_ops`
    - baseline `0.126122` vs `0.019463`
    - promoted default `0.126296` vs `0.019513`

So:

- the promoted default materially helps the static-stop `be_pack` reducer
- none of the valid reduced `be_pack` siblings cross into `jit.on < -joff`
- the small-scale structural escape on `be_pack_literal_stop_local_ops` does
  not survive throughput-scale work
- corrected reduced boundary after fixing the helper:
  - one chunk:
    [20260402-kdz-be-pack-local-ops-chunks1-v3](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-be-pack-local-ops-chunks1-v3/summary.md)
    - `TRACE_START 2`
    - `TRACE_STOP 2`
    - `TEXIT_COUNT 1`
  - throughput-sized `run(400)`:
    [raw stdout](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-be-pack-local-ops-chunks400-v3/raw/be_pack_literal_stop_local_ops.stdout.log)
    - `RESULT 32080000`
    - `TRACE_START 367`
    - `TRACE_STOP 367`
    - `TEXIT_COUNT 365`
  - helper correction:
    - the reduced core probe now honors `--iterations` for literal-stop reducers
    - it also uses unique remote `/tmp/<workload>-<id>.lua` names so parallel
      runs no longer clobber one another
- reduced route-around siblings are evidence only, not the next promotable
  performance lane

## Real-Shape Helper Localization

The real dynamic-stop helper-localization family is now closed as a performance
lane on clean `kdz`:

- baseline:
  [20260402-kdz-be_helpers_localized-baseline-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260402-kdz-be_helpers_localized-baseline-truth-pack/summary.md)
- promoted default:
  [20260402-kdz-be_helpers_localized-hotside_canon_share_uget_looproot_default-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260402-kdz-be_helpers_localized-hotside_canon_share_uget_looproot_default-truth-pack/summary.md)

Key read:

- `number_helper_loop_local_tobit/hot`
  - baseline `0.436932` vs `-joff 0.001442`
  - promoted default `0.621525` vs `0.001435`
- `be_pack_loop_local_ops_real/hot`
  - baseline `0.300697` vs `-joff 0.007985`
  - promoted default `0.244329` vs `0.008069`

Focused runtime read:

- baseline:
  - both localized real-shape workloads are `exit-dominated`
  - `TRACE_START 321`, `TRACE_STOP 321`, `TEXIT_COUNT 64001`
- promoted default:
  - both are still `exit-dominated`
  - `TRACE_START 5`, `TRACE_STOP 5`, `TEXIT_COUNT 64001`

So helper localization on the real dynamic-stop shape is not a hidden
promotion-core win:

- it is far worse than the existing shipping `be_helpers` slice
- it keeps the same replay-floor exit count
- it only changes trace population, not the performance floor

## Real-Shape Static Stop

Real-shape static stop does not clear the remaining promotion-core gap under
the promoted default, but it changes the baseline read in an important way:

- baseline:
  [20260402-kdz-promotion_core_static_stop-baseline-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260402-kdz-promotion_core_static_stop-baseline-truth-pack/summary.md)
- promoted default:
  [20260402-kdz-promotion_core_static_stop-hotside_canon_share_uget_looproot_default-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260402-kdz-promotion_core_static_stop-hotside_canon_share_uget_looproot_default-truth-pack/summary.md)

Key read on clean `kdz`:

- `number_helper_literal_stop_real/hot`
  - baseline `0.040660` vs `-joff 0.002244`
  - promoted default `0.009910` vs `0.002230`
- `be_pack_literal_stop_real/hot`
  - baseline `0.097101` vs `-joff 0.019263`
  - promoted default `0.025347` vs `0.018812`

Focused runtime split:

- baseline:
  - `number_helper_literal_stop_real`: `TRACE_START 1`, `TRACE_STOP 0`, `TRACE_ABORT 1`, `TEXIT_COUNT 0`
  - `be_pack_literal_stop_real`: `TRACE_START 1`, `TRACE_STOP 1`, `TRACE_ABORT 0`, `TEXIT_COUNT 0`
  - classification: `compiled-body-dominated`
- promoted default:
  - both workloads still carry `TEXIT_COUNT 63999`
  - `number_helper_literal_stop_real`: `TRACE_START 1`, `TRACE_STOP 0`, `TRACE_ABORT 1`
  - `be_pack_literal_stop_real`: `TRACE_START 1`, `TRACE_STOP 1`, `TRACE_ABORT 0`
  - classification: `exit-dominated`

So the remaining core gap is not “dynamic stop only” in the simple sense:

- removing dynamic stop makes the baseline path compiled-body-dominated
- the promoted default still routes the real-shape static-stop siblings through
  an exit-heavy steady state
- that exit-heavy promoted path is still much faster than the compiled-body
  baseline, so the gate remains a net win
- the next honest target is the exact static-stop seam under the promoted
  default, not helper localization and not more reduced route-around work

Static-stop seam probe on clean `kdz`:

- [20260402-201327 kdz core exit mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-201327-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism/summary.md)
- `be_pack_literal_stop_real`
  - `TRACE_START 2`, `TRACE_STOP 1`, `TEXIT_COUNT 63999`
  - dominant texit:
    - `trace 7 exit 0`
    - restored `op 45` / `snapop 45` = `BC_UGET`
    - first `sload_int`: `curins 33`, `IR=SLOAD`, `op1 2`, `op2 4`
- `number_helper_literal_stop_real`
  - raw exit logs in the same artifact show the repeated restored `pc` at
    `op 57` / `snapop 57` = `BC_TGETS`
  - exact taken inner guard in that `TGETS` cluster is not yet isolated

So the remaining static-stop cap is now narrower than the old dynamic-stop
numeric-for replay floor:

- `be_pack` is front-most at restored `BC_UGET`
- `number_helper` has already moved one step later to restored `BC_TGETS`

Static-stop helper-localized A/B on clean `kdz`:

- direct remote bench artifact:
  [20260402-kdz-static-stop-local-tobit-direct](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-static-stop-local-tobit-direct/raw/candidate.stdout.log)
- `number_helper_literal_stop_real_local_tobit/hot`
  - baseline `0.019114s` vs `-joff 0.001359s`
  - promoted default `0.018959s` vs `-joff 0.001359s`
- focused trace-count artifact:
  [20260402-kdz-static-stop-local-tobit-trace](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-static-stop-local-tobit-trace/raw/candidate.stdout.log)
  - candidate: `TRACE_START 1`, `TRACE_STOP 1`, `TRACE_ABORT 0`, `TEXIT_COUNT 0`
  - baseline: `TRACE_START 1`, `TRACE_STOP 1`, `TRACE_ABORT 0`, `TEXIT_COUNT 0`
- repeated-call artifact:
  [20260402-kdz-static-stop-local-tobit-postcompile](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-static-stop-local-tobit-postcompile/raw/candidate.stdout.log)
  - `RUN1`: `0.018934s`, `TRACE_START 1`, `TRACE_STOP 1`, `TRACE_ABORT 0`, `TEXIT_COUNT 0`
  - `RUN2`: `0.018927s`, `TRACE_START 1`, `TRACE_STOP 0`, `TRACE_ABORT 1`, `TEXIT_COUNT 1`
- `-jv` artifact:
  [20260402-kdz-static-stop-local-tobit-postcompile-jv](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-static-stop-local-tobit-postcompile-jv/raw/jv.stderr.log)
  - repeated calls build a loop-clone ladder:
    `TRACE 1`, `TRACE 2 (1/0)`, ..., `TRACE 102 (101/0)`, then fallback
- native dump artifact:
  [20260402-kdz-static-stop-local-tobit-dump](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-static-stop-local-tobit-dump/raw/dump.stdout.log)
  - front compiled-body loop is now:
    - `int SLOAD #4 I`
    - `fun SLOAD #3 T`
    - `int MULOV`
    - `int SLOAD #2 T`
    - `fun EQ ... bit.tobit`
    - `int ADD`
    - `int LE`

So helper localization plus static stop does remove the within-run replay floor
for `number_helper`, but it still does not create a stable fast JIT lane. On
repeated calls the workload walks a loop-clone ladder and falls back again.
The next honest target for that subgroup is that cross-call clone/fallback
behavior, not more replay/header work inside a single run.

Bounded opt-in broad-canon/share remediation on clean `kdz`:

- summary:
  [20260402-kdz-static-stop-local-broad-canonshare](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-static-stop-local-broad-canonshare/summary.md)
- boundary correction:
  - explicit broad canon/share envs were accidentally blocked by the shipping
    `UGET`/looproot prefilter in `trace_hotside()`
  - the current source now lets manual broad envs reach canon/share on
    non-`UGET` seams without widening the envless default
- `number_helper_literal_stop_real_local_tobit`
  - default repeated-call probe:
    - `RUN 1 -149783296 0.027580 103`
    - `RUN 2 -149783296 0.030517 103`
  - broad opt-in repeated-call probe:
    - `RUN 1 -149783296 0.007981 7`
    - `RUN 2 -149783296 0.007844 7`
- localized static-stop `be_pack` sibling:
  - default:
    - `RUN 1 2048032000 0.016238 7`
    - `RUN 2 2048032000 0.016659 7`
  - broad opt-in:
    - `RUN 1 2048032000 0.015817 7`
    - `RUN 2 2048032000 0.015774 7`

So the localized static-stop subgroup is no longer one uniform dead end:

- `number_helper` has a real opt-in canon/share remediation lane
- `be_pack` does not materially move under the same explicit broad policy
- the next honest quant is not more structural probing; it is whether this
  opt-in `number_helper` lane can actually beat `-joff`

Host-pair truth-pack quant closes that question:

- `kdz`:
  [20260402-kdz-promotion_core_static_stop-hotside_canon_share_uget_looproot_default-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260402-kdz-promotion_core_static_stop-hotside_canon_share_uget_looproot_default-truth-pack/summary.md)
  - `number_helper_literal_stop_real_local_tobit/hot`
    - JIT-on `0.006327s`
    - `-joff 0.001361s`
    - ratio `4.65x`
  - focused:
    - `TRACE_START 1`, `TRACE_STOP 1`, `TRACE_ABORT 0`, `TEXIT_COUNT 63999`
    - still `exit-dominated`
- `zkd0`:
  [20260402-zkd0-promotion_core_static_stop-hotside_canon_share_uget_looproot_default-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260402-zkd0-promotion_core_static_stop-hotside_canon_share_uget_looproot_default-truth-pack/summary.md)
  - `number_helper_literal_stop_real_local_tobit/hot`
    - JIT-on `0.013725s`
    - `-joff 0.002224s`
    - ratio `6.17x`
  - focused:
    - `TRACE_START 1`, `TRACE_STOP 1`, `TRACE_ABORT 0`, `TEXIT_COUNT 63999`
    - still `exit-dominated`

So the broad non-`UGET` opt-in lane is real, but it does not cross into
`jit.on < -joff`. The next honest target is selective non-`UGET` canon/share
policy design or a fresh exit seam inside this localized subgroup, not a
shipping-default expansion.

Reduced post-collapse seam on clean `kdz`:

- localized no-helper sibling (`number_helper_local_tobit`, `n=400`)
- broad opt-in env:
  - `LUAJIT_S390X_HOTSIDE_CANON_SHARE_EQUIV=1`
- restored hot seam:
  - `op=18`
  - `snapop=18`
  - `BC_MOV`
- exact first surviving guard:
  - `sload_int curins=4 ofs=0 extra=4`
- later same-cluster guards:
  - `curins=3`
  - `sload_type curins=2 ofs=8 extra=8`

So the clone ladder is no longer the first payer on this reduced lane. After
the broad opt-in collapse, the next exact seam is the stack-visible `MOV` /
`sload_int` header cluster.

That seam is now tighter than “some stack-visible `SLOAD`”:

- direct reduced `kdz` trace-IR under broad opt-in shows:
  - `TRACEIR tr=1 ins=4 op=SLOAD op1=2 op2=4`
  - `TRACEIR tr=1 ins=2 op=SLOAD op1=3 op2=4`
  - `TRACEIR tr=1 ins=3 op=MULOV op1=1 op2=-6`
- so the first marked guard is the carried `total` reload
- direct `SLOADMAP` + slot logging shows that lane is already good:
  - `curins=4 ref=4 kind=int op1=2 op2=0x4 ofs=0 vofs=4 base=11`
  - restored `idx=0` is a valid boxed int and advances correctly

So the remaining payer on this reduced lane is now the carried-`total`
`sload_int` compare/lowering path itself, not stack-lane selection.

That direct remediation line is now rejected:

- artifact:
  [20260402-kdz-number-helper-local-broad-skip-total-guard](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-number-helper-local-broad-skip-total-guard/summary.md)
- reduced `kdz` run with:
  - `LUAJIT_S390X_HOTSIDE_CANON_SHARE_EQUIV=1`
  - `LUAJIT_S390X_SKIP_TOTAL_SLOAD_INT_GUARD=1`
- result stays correct:
  - `RESULT -149783296`
- but the structural floor does not improve:
  - `TRACE_START 5`
  - `TRACE_STOP 5`
  - `TRACE_ABORT 0`
  - `TEXIT_COUNT 64001`

Corrected signed-expected follow-up is now explicitly rejected on the dynamic
localized-helper lane:

- reduced `kdz` artifact:
  [20260403-kdz-localized-current-fix-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-kdz-localized-current-fix-check/summary.md)
- the old visible-current `sload_int` exact guard clears, but control just
  advances into an overflow side loop:
  - dominant runtime `guardmark`: `curins 5`
  - `TRACEIR tr=1 ins=5 op=MULOV op1=3 op2=-6`
  - `trace 4`: `num CONV -> num MUL -> int TOBIT -> int ADD`
- result is wrong and therefore non-promotable:
  - `RESULT 1323881804`
  - `TRACE_START 2`
  - `TRACE_STOP 2`
  - `TRACE_ABORT 0`
  - `TEXIT_COUNT 202`
- adding `LUAJIT_S390X_JFORI_INTERP_HANDOFF=1` on the same lane does not help:
  [20260403-kdz-localized-current-fix-paired-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-kdz-localized-current-fix-paired-check/summary.md)

So the carried-`total` guard is only the first marked branch after the ladder
collapse. It is not the whole remaining payer. The next honest target is the
later unmarked exit path on the same reduced localized lane.

The exploratory static-stop FFI subgroup is now fenced off from this family:

- reducer file:
  [ffi_calls_static_stop.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/ffi_calls_static_stop.lua)
- direct `kdz` proof:
  [20260403-kdz-ffi-static-stop-direct-probe](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-kdz-ffi-static-stop-direct-probe/summary.md)
- both real static-stop FFI forms are correctness-broken under JIT:
  - `direct_abs_literal_stop_real`: `RUN1 486`, `RUN2 0`, `RUN3 0`
  - `stored_abs_literal_stop_real`: `RUN1 486`, `RUN2 0`, `RUN3 0`
- repeated-call structure is small and stable rather than ladder-shaped:
  - `TRACE_START 3`
  - `TRACE_STOP 3`
  - `TRACE_ABORT 0`
  - `TEXIT 401`
  - texit histogram:
    - `1:1=200`
    - `2:1=2`
    - `3:0=199`

So this subgroup is a separate correctness lane, not usable benchmark-ready
route-around evidence for the current promotion-core performance map.

One more direct `kdz` check closes the first wrong attribution on that lane:

- exact-taken guard probe with `LUAJIT_S390X_GUARDMARK_TAKEN=1` shows the
  repeated seam is still `trace 1 exit 1`, but the first literal failing
  guard is:
  - `guardmark=0x12`
  - `curins 18`
  - `int SLOAD #2 T`
- so the broken static-stop FFI lane is not first failing at `ADDOV`; it is
  first failing at the accumulator-slot `SLOAD` typecheck immediately before
  `ADDOV`

And a numeric-accumulator control does not route around it:

- direct `kdz` control with `local total = 0.0` and the same `ffi.C.abs` loop
  still returns:
  - `RUN1 486`
  - `RUN2 0`
  - `RUN3 0`

So this remains a separate FFI/call correctness lane, not promotion-core
route-around evidence and not a plain int-accumulator specialization bug.

The next direct remediation attempt on that lane is now closed too:

- `CALLXS`-fed `ADDOV` narrowing block:
  [20260403-kdz-ffi-static-stop-no-callxs-addov](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-kdz-ffi-static-stop-no-callxs-addov/summary.md)
- same-callsite control under the same gate:
  [20260403-kdz-ffi-static-stop-same-callsite-no-callxs-addov](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-kdz-ffi-static-stop-same-callsite-no-callxs-addov/summary.md)
- root IR does change as intended:
  - `CALLXS -> SLOAD #2 T -> ADDOV`
  - becomes `CALLXS -> SLOAD #2 T -> num CONV -> num ADD`
- but the lane is still wrong:
  - `direct_abs_literal_stop_real`: `RESULT 0`
  - `stored_abs_literal_stop_real`: `RESULT 0`
  - `direct_abs_literal_stop_same_callsite`: `RESULT 0`
  - `stored_abs_literal_stop_same_callsite`: `RESULT 0`
- same-callsite stays small and finite, but the dominant exact seam moves later:
  - `trace 4 exit 2`
  - restored `snapop=76`
  - dominant runtime `guardmark=0x6`
  - exact runtime guard `curins 6`, `IR LE`

So the remaining payer on this FFI slice is no longer the `CALLXS` arithmetic
classifier. It is the later lower-frame return / caller-loop continuation on
the num-accumulation path.

That continuation family is now known to be generic rather than FFI-specific:

- checked-in reducer:
  [lower_frame_same_callsite.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/lower_frame_same_callsite.lua)
- reducer boundary on clean `kdz`:
  - the bench harness stays correct
  - the exact direct same-callsite script still returns `LUA1 0`, `LUA2 0`
  - so the checked-in file is a control surface, not the authoritative bug
    reproducer
- control result:
  - same-callsite constant-return outer shape stays correct
  - pure-Lua inner hot loop under the same outer call/loop/return shape
    reproduces the same lower-frame continuation family on `kdz`
- queue correction:
  - the active bug class is generic same-callsite lower-frame continuation /
    result-slot identity loss after a hot inner loop
  - the old static-stop FFI lane is one reproducer, not the whole family

That continuation is now pinned more exactly:

- `RECRET` artifact:
  [20260403-kdz-ffi-static-stop-same-callsite-recret](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-kdz-ffi-static-stop-same-callsite-recret/summary.md)
- [lj_record_ret()](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c#L2016) does shift the single live result `TRef`
  during `lua_lower_frame_retf`, but only into the lower-frame call-result
  destination
  - pre-shift `idx=0`
  - post-shift `idx=5`
  - `cbase=5`
  - `nresults=1`
- the same-callsite continuation is already at caller `RET1` with `prevop=JFORL`,
  not at the bytecode `MOV` that would usually materialize the caller-visible
  destination/local from that call-result slot
- but `trace 4` still begins with the inherited caller-visible result lane:
  - `TRACEIR tr=4 ins=1 op=SLOAD op1=2 op2=33`
  - exit snapshots still keep `slot2=ref1[o=71 t=14 op1=2 op2=33 ...]`
- [snapshot_slots()](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_snap.c#L103) uses `IR_RETF` as the cutoff for SLOAD restore
  elimination, so the continuation can keep the old inherited result identity
  even though the lower-frame path only materialized the shifted `cbase=5`
  destination
- [snap_usedef()](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_snap.c#L305) explains why that wrong identity wins:
  by the time this path is at caller `RET1`, only the caller-visible return
  slot is live, so the shifted call-result destination is not preserved unless
  it has already been rebound to that return-slot identity
- direct `BC_RET1`-side rebinding after the lower-frame shift is now rejected:
  - artifact:
    [20260403-075054-kdz-baseline-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-075054-kdz-baseline-core-exit-mechanism/summary.md)
  - it leaves `RESULT 0`, `TRACEIR tr=4 ins=1 op=SLOAD op1=2 op2=33`, and
    `slot2=ref1[...]` unchanged
- fresh lower-frame destination rematerialization is also rejected:
  - artifact:
    [20260403-080443-kdz-baseline-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-080443-kdz-baseline-core-exit-mechanism/summary.md)
  - explicit `sload(J, cbase)` rematerialization inside `lua_lower_frame_retf`
    leaves `RESULT 0`, `TRACEIR tr=4 ins=1 op=SLOAD op1=2 op2=33`, and
    `slot2=ref1[...]` unchanged
  - the correction is that the active seam is later than the local
    `lua_lower_frame_retf` handoff window: `frame_pc(frame)` is still at an
    earlier caller PC, while the failing continuation snapshot is already one
    bytecode later at caller `RET1`
- targeted `snapshot_slots()` logging narrows that further:
  - artifact:
    [20260403-080945-kdz-baseline-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-080945-kdz-baseline-core-exit-mechanism/summary.md)
  - at the actual failing `RET1` snapshot pass, the current frame has already
    collapsed to `baseslot=2`, `maxslot=1`
  - in that final pass, only the stale caller-visible lane is still
    considered/kept:
    - `slot=2`, `rel=0`, `op=SLOAD`, `op1=2`, `op2=33`
  - the shifted lower-frame destination is not being skipped there; it is
    already outside the current frame window
- so `snapshot_slots()` itself is now closed as the local repair site on this
  lane; the remaining target is earlier, in the frame-window / slot-identity
  collapse between `IR_RETF` and the later caller `RET1`
- exact `snap_usedef()` logging now pins that earlier collapse point:
  - artifact:
    [20260403-082040-kdz-baseline-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-082040-kdz-baseline-core-exit-mechanism/summary.md)
  - the first real loss is at caller `RET0`, not the later failing `RET1`
  - under active `IR_RETF`, the `BC_RET0/RET1` liveness rule in
    [snap_usedef()](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_snap.c#L305)
    is already running with:
    - `baseslot=2`
    - `maxslot=18`
    - only `idx=17` carrying `ref=1`, `type=14`
  - that means the caller-visible result identity has already been collapsed
    by the earlier return-window rule before the failing `RET1` snapshot is
    built
- first direct remediation attempts on that boundary are now closed:
  - keep-live gate:
    [20260403-082728-kdz-baseline-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-082728-kdz-baseline-core-exit-mechanism/summary.md)
    - `LUAJIT_S390X_RETF_RET0_KEEP_LIVE=1`
    - structurally inert:
      - same `RESULT 0`
      - same `TRACE_START 4`, `TRACE_STOP 3`, `TEXIT_COUNT 1`
      - same `TRACEIR tr=4 ins=1 op=SLOAD op1=2 op2=33`
  - slot0-rebind gate:
    [20260403-082941-kdz-baseline-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-082941-kdz-baseline-core-exit-mechanism/summary.md)
    - `LUAJIT_S390X_RETF_RET0_REBIND_SLOT0=1`
    - also structurally inert:
      - same `RESULT 0`
      - same continuation front and same exit snapshot
  - queue correction:
    - preserving liveness is not enough
    - rebinding local slot `0` before snapshot build is not enough

So the next honest remediation lane on this slice is lower-frame result-slot
rebasing/rematerialization across `IR_RETF`, specifically at the active
snapshot-map / inherited-lane identity boundary, not more caller-loop `LE`
attribution and not local resumed-`MOV` patches.

That later path is now confirmed to be workload-local:

- artifact:
  [20260402-kdz-number-helper-local-broad-skip-total-guard-nopost](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-number-helper-local-broad-skip-total-guard-nopost/summary.md)
- same reduced lane, with post-run hooks disabled
- still:
  - `TRACE_START 6`
  - `TRACE_STOP 5`
  - `TRACE_ABORT 0`
  - `TEXIT_COUNT 64001`
- dominant workload seam:
  - `trace 7 exit 0`
  - restored `BC_MOV` (`op=18`)
  - `snapop=18`
  - dominant runtime `guardmark=0`
- corrected lane mapping:
  - the one-off skip only targeted the no-helper sibling's carried-`total`
    guard (`curins=4`, `op1=2`)
  - on the localized-helper workload, reduced trace-IR lays out:
    - `op1=6` -> loop bound `n`
    - `op1=5` -> current numeric-for value
    - `op1=4` -> localized `tobit`
    - `op1=3` -> carried `total`
- first surviving workload-only `sload_int` on this workload is therefore:
  - `curins=6`
  - `op1=3`
  - `ofs=8`
  - `extra=12`

So the next reduced target is no longer “is the unmarked path real?” It is the
localized-helper carried-`total` lane
`curins=6 / op1=3 / ofs=8 / extra=12` inside the workload itself.

## Relationship To Other Docs

- High-level status:
  [state-of-project.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/state-of-project.md)
- Detailed findings and reject pile:
  [findings.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/findings.md)
- Validation discipline:
  [runbook.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/runbook.md)

## 2026-04-03 15:20 PDT

- The carried-`total` lane is now source-pinned to its birth site.
- Paired control:
  - `LUAJIT_S390X_FORL_ROOT_VISIBLE_IDX_NOGUARD=1`
  - `LUAJIT_S390X_KEEP_FIRST_LOCAL_SLOAD_SNAP=1`
- exact-taken artifact:
  [20260403-150846-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-150846-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism/summary.md)
- still pays first on:
  - `curins 15`
  - `IR=SLOAD`
  - `op1=3`
  - `op2=4`
  - `ofs=8`
  - `extra=12`
- widened birth-log artifact:
  [20260403-151707-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-151707-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism/summary.md)
- birth attribution:
  - visible current-value lanes are `sloadt()` births at `BC_UGET`
  - the carried accumulator lane is a later plain `sload()` birth at
    `BC_ADDVV`:
    - `baseslot=2`
    - `slot=1`
    - `abs=3`
    - `mode=4`
    - `ref=15`
- direct header seeding reject:
  [20260403-151928-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-151928-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism/summary.md)
  - seeding the preserved first local integer lane into `J->base[1]` at the
    restored header is structurally inert
  - same `trace 7 exit 0`
  - same exact guard `curins 15 / SLOAD op1=3 op2=4`
- next remediation family:
  - `BC_ADDVV` accumulator operand specialization
  - not restored-header rematerialization

## 2026-04-03 16:11 PDT

- The direct `BC_ADDVV` accumulator specialization family is now measured.
- Consumer-entry read on the carried-`total` control:
  - `BC_ADDVV`
  - `ra=8`, `rb=1`, `rc=8`
  - `baseslot=2`
  - `J->base[1] == 0`
  - `J->slot[3] == 0`
- Patch family:
  - `LUAJIT_S390X_ADDVV_ACCUM_INT_NOGUARD=1`
  - on exact `BC_ADDVV`, `rb==1`, int runtime operand
  - use `sloadt(... IRT_INT, 0)` instead of `getslot()->sload()`
- Exact mechanism result:
  - `number_helper_loop`:
    [20260403-160805-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-160805-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism/summary.md)
  - `be_pack_loop`:
    [20260403-160946-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-160946-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism/summary.md)
  - on both, the carried accumulator lane remains but is no longer guarded:
    - `SLOAD op1=3 op2=0`
  - the first exact taken guard becomes the old visible current-value seam
    again:
    - `curins 3`
    - `IR=SLOAD`
    - `op1=4`
    - `op2=36`
- Throughput result:
  - truth pack:
    [20260403-kdz-be_helpers-hotside_canon_share_uget_looproot_default-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260403-kdz-be_helpers-hotside_canon_share_uget_looproot_default-truth-pack/summary.md)
  - `number_helper_loop/hot`: `0.008697`
  - `be_pack_loop/hot`: `0.023731`
  - still exit-dominated and still materially above `-joff`
- Classification:
  - this is a real sub-remediation for the carried accumulator lane
  - but not a promotable lane by itself because it only re-exposes the
    visible current-value payer

## 2026-04-03 16:22 PDT

- The combined recorder family is now classified and rejected:
  - `LUAJIT_S390X_FORL_ROOT_VISIBLE_IDX_NOGUARD=1`
  - `LUAJIT_S390X_ADDVV_ACCUM_INT_NOGUARD=1`
- Mechanism control:
  [20260403-161644-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-161644-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism/summary.md)
  - first exact taken guard moves beyond both stack `SLOAD` payers
  - it lands on the first loop-body arithmetic consumer:
    - `TRACEIR tr=1 ins=14 op=MULOV`
    - runtime `guardmark curins 14`
  - but the result is already wrong:
    - `RESULT 1323881804`
- Payoff sibling:
  [20260403-161839-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-161839-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism/summary.md)
  - the same family is not seam-preserving on `be_pack_loop`
  - it collapses to:
    - `TRACE_START 1`
    - `TRACE_STOP 1`
    - `TEXIT_COUNT 2`
    - dominant `trace 2 exit 0`
- Classification:
  - this is not a throughput lane
  - it is a control-only redirect that identifies the next issue:
    once both stack `SLOAD` lanes are relaxed, the front-most payer is the
    first loop-body `MULOV` consumer
  - the arithmetic audit closes the easy follow-up:
    wrapped `bit.tobit()` semantics do not license stripping `MULOV`, so this
    is not a narrow promotable arithmetic lane

## 2026-04-03 16:42 PDT

- The replay-side-only visible-current rebuild family is rejected:
  - `LUAJIT_S390X_FORL_REPLAY_VISIBLE_IDX_NOGUARD=1`
- Mechanism control:
  [20260403-164012-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-164012-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism/summary.md)
  - exact result is structurally identical to the shipping default:
    - `RESULT -149783296`
    - `TRACE_START 6`
    - `TRACE_STOP 5`
    - `TEXIT_COUNT 64001`
    - same dominant `trace 7 exit 0`
    - same exact runtime guard `curins 3 / SLOAD op1=4 op2=36`
    - same first carried lane `curins 15 / SLOAD op1=3 op2=4`
- Classification:
  - this replay-only emitter hook is inert
  - it does not justify a payoff rerun because the mechanism control never
    leaves the old seam

## 2026-04-03 16:55 PDT

- The backend-only visible-current `SLOAD` family is now measured and rejected:
  - `LUAJIT_S390X_ASM_VISIBLE_IDX_SLOAD_NOGUARD=1`
- Exact mechanism reads:
  - control:
    [20260403-164918-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-164918-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism/summary.md)
  - payoff sibling:
    [20260403-165053-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-165053-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism/summary.md)
  - both workloads keep the same dominant `trace 7 exit 0` family
  - both displace the first exact taken guard from visible-current
    `SLOAD op1=4 op2=36` to carried-total `SLOAD op1=3 op2=4`
- Truth pack:
  [20260403-kdz-be_helpers-hotside_canon_share_uget_looproot_default-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260403-kdz-be_helpers-hotside_canon_share_uget_looproot_default-truth-pack/summary.md)
- Read:
  - smoke is wrong:
    - `number_helper_loop check: 13762770`
    - `be_pack_loop check: 210`
  - throughput regresses:
    - `number_helper_loop/hot 0.010473` vs default `0.008927`
    - `be_pack_loop/hot 0.027104` vs default `0.023920`
- Classification:
  - backend compare removal is real ownership evidence
  - but it is not a viable remediation lane

## 2026-04-03 18:36 PDT

- The next `be_pack_loop` payer after the hidden-current compare fix is now
  classified.
- Discovery artifact:
  [20260403-kdz-be-pack-carried-cmptruth](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-kdz-be-pack-carried-cmptruth/summary.md)
- Read:
  - with opt-in `LUAJIT_S390X_FORL_CURRENT_COMPARE_FIX=1`, the clean direct
    `kdz` rerun returns the correct hot result:
    - `2048032000`
  - the exact taken guard still reports the carried accumulator lane:
    - `curins 35 / SLOAD op1=3 op2=4`
  - but compare-truth proves that lane is semantically clean:
    - valid boxed int
    - `logical_eq=1`
    - `signed_eq=1`
  - the live issue therefore moves to adjacent guard ownership inside the same
    restored-header cluster:
    - `curins 37`
    - `curins 35`
    - `curins 33`
    - nearby IR: `UGT`, `SLOAD`, `EQ`
- Classification:
  - do not open another `SLOAD` compare patch on the carried accumulator lane
  - the next remediation target should start with the adjacent
    `UGT curins 37` consumer

## 2026-04-03 21:00 PDT

- The generic signed GC64 integer compare correction is now retained as a real
  backend fix in [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h).
- Exact code change:
  - load expected signed `LJ_TISNUM` directly in the generic signed-int
    `SLOAD` compare path
  - drop the old arithmetic-right-shifted `-1` constant
- Effect:
  - with the hidden-current compare fix also enabled, the carried signed-int
    lane no longer miscompares on `be_pack_loop`
  - the remaining blocker is later than compare semantics; the compare-fixed
    stable-callsite reducer still crashes after the old compare seams clear
- Rejected follow-ups this cycle:
  - replay-side `lj_snap_replay()` mutation probes
  - `lj_record_ret()` rematerialization probes
  - `FLOAD` base-mask probe
  - `AHUVLOAD` base-mask probe

## 2026-04-04 00:42 PDT

- The cleaned compare-fix checkpoint is now validated on both `kdz` and
  `zkd0`.
- Checkpoint artifact:
  [20260404-hostpair-comparefix-phi-checkpoint](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260404-hostpair-comparefix-phi-checkpoint/summary.md)
- Env surface:
  - `LUAJIT_S390X_FORL_CURRENT_COMPARE_FIX=1`
  - `LUAJIT_S390X_GC64_SIGNED_INT_SLOAD=1`
- Exact `32768` oracle:
  - `kdz`: `JIT 1610629120`, `INTERP 1610629120`
  - `zkd0`: `JIT 1610629120`, `INTERP 1610629120`
- Reduced helper validators:
  - `number_helper_loop(64000)`:
    - `kdz`: `-149783296 == -149783296`
    - `zkd0`: `-149783296 == -149783296`
  - reduced `be_pack_loop(64000)`:
    - `kdz`: `-32000 == -32000`
    - `zkd0`: `-32000 == -32000`
- Focused `kdz` perf under the same gates:
  - `number_helper_loop/small median=0.000007`
  - `be_pack_loop/small median=0.000017`
  - `number_helper_loop/medium median=0.000025`
  - `be_pack_loop/medium median=0.000067`
  - `number_helper_loop/hot median=0.000092`
  - `be_pack_loop/hot median=0.000270`
- Read:
  - after stripping compare-truth and `prev` probes, the retained backend fix
    set still holds
  - the decisive warmed-overflow correction is the `asm_phi()` right-side
    duplication on the exact `ref18/ref23 -> phi27` handoff
  - this is still an env-gated checkpoint, not a promoted default throughput
    restamp

## 2026-04-04 08:42 PDT

- The `FORL_CURRENT_COMPARE_FIX` family is now promoted to default-on with an
  explicit opt-out.
- Promotion artifact:
  [20260404-hostpair-default-on-forl-compare-fix](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260404-hostpair-default-on-forl-compare-fix/summary.md)
- Envless host-pair validation:
  - exact `32768` oracle:
    - `kdz`: `JIT 1610629120`, `INTERP 1610629120`
    - `zkd0`: `JIT 1610629120`, `INTERP 1610629120`
  - reduced helper validators:
    - `number_helper_loop(64000)`:
      - `kdz`: `-149783296 == -149783296`
      - `zkd0`: `-149783296 == -149783296`
    - reduced `be_pack_loop(64000)`:
      - `kdz`: `-32000 == -32000`
      - `zkd0`: `-32000 == -32000`
- Focused envless `kdz` perf:
  - `number_helper_loop/small median=0.000007`
  - `be_pack_loop/small median=0.000017`
  - `number_helper_loop/medium median=0.000025`
  - `be_pack_loop/medium median=0.000068`
  - `number_helper_loop/hot median=0.000090`
  - `be_pack_loop/hot median=0.000270`
- Opt-out control:
  - `LUAJIT_S390X_DISABLE_FORL_CURRENT_COMPARE_FIX=1`
  - exact `kdz` oracle falls back to:
    - `JIT 65536`
    - `INTERP 1610629120`
- Read:
  - this is now a default-on remediation, not only a gated checkpoint
  - the next step should quantify broader throughput and regression surface
    under the new default

## 2026-04-04 08:52 PDT

- Envless host-pair `promotion_core` restamp:
  [20260404-hostpair-promotion-core-envless-restamp](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260404-hostpair-promotion-core-envless-restamp/summary.md)
- `kdz`
  - `be_helpers`
    - `number_helper_loop/hot 0.000113` vs `-joff 0.002241`
    - `be_pack_loop/hot 0.000319` vs `0.018557`
  - `ffi_calls`
    - `direct_abs/hot 0.000291` vs `0.010114`
    - `stored_abs/hot 0.000289` vs `0.007024`
  - `bitops_mix/hot 0.000730` vs `0.002148`
  - `logical_chain_tail_add/hot 0.000748` vs `0.002107`
  - `logical_chain_tail_store/hot 0.000594` vs `0.002025`
- `zkd0`
  - `be_helpers`
    - `number_helper_loop/hot 0.000131` vs `-joff 0.002609`
    - `be_pack_loop/hot 0.000339` vs `0.022114`
  - `ffi_calls`
    - `direct_abs/hot 0.000338` vs `0.021233`
    - `stored_abs/hot 0.000325` vs `0.014235`
  - `bitops_mix/hot 0.000836` vs `0.002243`
  - `logical_chain_tail_add/hot 0.000968` vs `0.002235`
  - `logical_chain_tail_store/hot 0.000713` vs `0.002362`
- Read:
  - the current first-enable `promotion_core` set is now broadly green on both
    hosts
  - the old `promotion_core` throughput floor is no longer the active
    branch-level blocker

## 2026-04-10 19:02 PDT

- ISA lab A3/A1/trace promotion slice merged into the bring-up branch at
  `640e9641`, then integrated with one retained-matcher restamp.
- Promotion shifted `vararg_paths/sum_loop` trace 1 from the old
  `nins=32796, mcloop=312` root-FORL shape to
  `nins=32796, mcloop=304`.
- Fix:
  - restamp `LUAJIT_S390X_SUM_LOOP_FORL_BLACKLIST=1` to accept both
    `mcloop=312` and `mcloop=304`
  - leave the promoted duplicate-exit descendant guard intact
- `kdz` post-restamp focused perf:
  - `vararg_paths/sum_loop/hot 0.004541`
  - `vararg_paths/retlast_loop/hot 0.002790`
  - `vararg_paths/retconst_loop/hot 0.001287`
  - `iterator_table/pairs_sum/hot 0.004495`
  - `iterator_table/pairs_array_sum/hot 0.003950`
  - `mixed_ffi/mixed_ffi_loop/hot 0.012258`
  - `mixed_noffi/mixed_loop/hot 0.004123`
  - `ffi_cdata/mixed_width_loop/small 0.001720`
- `zkd0` post-restamp focused perf:
  - `vararg_paths/sum_loop/hot 0.005279`
  - `vararg_paths/retlast_loop/hot 0.003226`
  - `vararg_paths/retconst_loop/hot 0.001708`
  - `iterator_table/pairs_sum/hot 0.004850`
  - `iterator_table/pairs_array_sum/hot 0.005406`
  - `mixed_ffi/mixed_ffi_loop/hot 0.014151`
  - `ffi_cdata/mixed_width_loop/small 0.001907`
  - `dispatch_trace/numeric_loop/hot 0.020358`
- Exactness stayed clean on both hosts:
  - `/tmp/mixedprobe.lua -> RESULT 553416`
  - `/tmp/hash_value.lua -> HASH_VALUE 3000`
  - `/tmp/ipairs_only_probe.lua -> RESULT 576000`
- Focused promoted correctness probes passed on both hosts, including
  `ffi_abi/run.lua`, `ffi_stack_call_trace.lua`, `math_random_trace.lua`,
  `jit_be/large_immediates.lua`, `jit_be/numeric_ops.lua`, and focused FFI
  call trace probes.
- Read:
  - the promotion can be carried with the `sum_loop` matcher restamp
  - do not use the rejected sum-proto exclusion from the generic duplicate
    descendant guard; it exposed a worse `sum_loop` ladder

## 2026-04-11 07:10 PDT

- Post-promotion-core retained-floor rerank after the current-shape
  bitops/logic route-around:
  - rejected a throwaway iterator root-`BC_FORL` stitch blacklist probe because
    it did not engage on the official retained row
  - restored the clean retained source and rebuilt `kdz`
  - artifact:
    `/tmp/top-residual-stability-20260411070957`
- Current trusted `kdz` high-sample hot rows:
  - `iterator_table/pairs_sum/hot 0.004272` vs `-joff 0.004266`
  - `iterator_table/pairs_array_sum/hot 0.003699` vs `-joff 0.003703`
  - `mixed_noffi/mixed_loop/hot 0.003797` vs `-joff 0.003748`
  - `vararg_paths/sum_loop/hot 0.004490` vs `-joff 0.004967`
  - `logical_chain_tail_add/chain_tail_add/hot 0.001879` vs `-joff 0.001904`
  - `be_helpers/number_helper_loop/hot 0.002272` vs `-joff 0.002347`
  - `be_helpers/be_pack_loop/hot 0.018765` vs `-joff 0.018799`
- Focused recheck of broad `mixed_ffi` / `ffi_cdata` red reads:
  `/tmp/ffi-mixed-focused-rerun-20260411070907`
  - `mixed_ffi_loop/hot` retained reads were `0.012038`, `0.012038`,
    `0.012734` against same-window `-joff` reads `0.012290`, `0.013850`,
    `0.011993`
  - `ffi_cdata/pair_loop/hot` retained reads were `0.017102`, `0.017304`,
    `0.017402` against same-window `-joff` reads `0.017051`, `0.017301`,
    `0.017808`
  - `ffi_cdata/mixed_width_loop/hot` retained reads were `0.027988`,
    `0.028163`, `0.028290` against same-window `-joff` reads `0.027947`,
    `0.028574`, `0.029335`
- `zkd0` top-residual screen:
  `/tmp/zkd0-top-residual-stability-20260412021114`
  - iterator and `logical_chain_tail_add` were faster than `-joff`
  - `mixed_noffi` was effectively parity
  - several other rows showed noisy host-local red reads with high p95 tails,
    but trusted `kdz` did not name a stable payer
- Read:
  - the active floor is near parity under the full retained env
  - do not reopen iterator trace-control, mixed-noffi, or FFI lanes from this
    pass without a fresh official-row proof
  - the next code mutation should start with a fresh matrix/proof pass, not a
    trace-meta-only ladder or a stale reduced-probe seam

## 2026-04-11 21:40 PDT

- Post-`994ce16f` iterator confirmation:
  `/tmp/kdz-post-994ce16f-iterator-official-confirm-20260411212302`
  - `pairs_sum/hot`: median ratio `0.9735x`, red `2/7`
  - `pairs_array_sum/hot`: median ratio `1.0041x`, red `1/7`
  - read: iterator stays parked; this is not a stable official-row payer.
- Non-iterator confirmation:
  `/tmp/kdz-post-994ce16f-noniterator-confirm-20260411212537`
  - `ffi_cdata/buffer_fref_loop/hot`: median ratio `1.0279x`, red `3/5`
  - `mixed_noffi/mixed_loop/hot`: median ratio `1.0222x`, red `4/5`
  - `be_helpers/number_helper_loop/hot`: median ratio `1.0165x`, red `3/5`
  - read: residuals are small; the `ffi_cdata` follow-up exposed a correctness
    crash before it could be treated as a perf target.
- `ffi_cdata` stress after the `UREFO` ordering fix:
  `/tmp/kdz-urefo-order-ffi-cdata-confirm-20260411213547`
  - `pair_loop/hot`: median ratio `0.0035x`
  - `mixed_width_loop/hot`: median ratio `1.0113x`
  - `buffer_fref_loop/hot`: median ratio `1.0173x`
  - read: the generated-code crash is fixed; the remaining cdata residuals are
    still small/noisy.
- Host confirmation:
  `/tmp/zkd0-urefo-order-ffi-cdata-confirm-20260411213838` completed the
  same long-sample `ffi_cdata` run without crash. The `buffer_fref_loop` timing
  was noisy (`1.2936x` then `0.9647x`), so it is not a retained perf signal.

## 2026-04-11 21:58 PDT

- Post-`0f398870` full retained-env rerank:
  `/tmp/kdz-post-0f398870-retained-rerank-20260411214254`
  - top median red: `mixed_noffi/mixed_loop/hot 1.0049x`, red `1/5`
  - `be_helpers/number_helper_loop/hot 1.0045x`, red `2/5`, high jitter
  - `dispatch_trace/side_exit_loop/hot 1.0040x`, red `0/5`
  - `ffi_cdata/mixed_width_loop/hot 1.0038x`, red `1/5`
  - read: no row meets the material/repeated threshold for a code target.
- Guardrail debt sweep:
  `/tmp/kdz-guardrail-debt-0f398870-20260411215000`
  - exact iterator opt-out is much worse:
    `pairs_sum/hot 2.7477x` and `pairs_array_sum/hot 2.2467x` versus retained
    JIT-on
  - broad iterator root opt-out is neutral
  - mixed-noffi guard removal is worse: `mixed_loop/hot 1.0593x`
  - exact vararg guard removal is worse on `retlast_loop/hot 1.4505x`
  - promotion-core/localized/lower-frame removal is worse on
    `be_helpers/number_helper_loop/hot 1.2703x`
  - FFI/cdata guard removal is neutral
  - read: the retained guardrails are not currently hiding an obvious safe
    high-upside path.
- Numeric perf fill-in:
  `/tmp/kdz-post-0f398870-numeric-ops-20260411215500`
  - all hot rows remain strongly faster than `-joff`
  - `fp_mod_loop/hot 0.1183x`, `max_loop/hot 0.0694x`,
    `min_loop/hot 0.0627x`
- Current queue:
  park performance source edits until a repeated retained-env `kdz` A/B names
  a material official-row payer. If the team wants to keep pushing for
  above-parity wins, the next work should be mechanism truth packs against a
  named guardrail debt seam, not another broad trace-control edit.

## 2026-04-11 22:50 PDT

- Numeric `SLOAD` FPR accumulator fix:
  - `ffi_fixed_call_pressure/fpr_pressure/hot` had been failing with a zero
    result when a traced `num SLOAD` re-entered from an integer initial total.
  - [lj_asm_s390x.h](../../src/lj_asm_s390x.h) now converts the integer payload
    into the FPR destination for used `num SLOAD` instead of loading raw integer
    TValue bits as a double.
- Focused retained-env `kdz` reads:
  - `ffi_fixed_call_pressure/fpr_pressure/hot median=0.000267`
  - `numeric_ops/min_loop/hot median=0.000154`
  - `numeric_ops/max_loop/hot median=0.000177`
- Broad retained-env `kdz` smoke:
  `/tmp/kdz-retained-jitter-20260411224707`.
  The run stayed near the recovered floor and did not name a stable new red
  payer. The largest apparent row, `iterator_table/pairs_sum/hot`, flipped from
  `0.7089x` to `1.3955x` across two alternating passes and is not a retained
  signal.
- `zkd0`:
  passed the new regression test, all `jit_be` tests, `ffi_fixed_call_pressure`,
  and `numeric_ops`. The retained-env smoke artifacts
  `/tmp/zkd0-retained-jitter-20260411224915` and
  `/tmp/zkd0-retained-jitter-20260411224940` were noisy confirmation reads, not
  queue-ranking evidence.
- Current queue:
  unchanged. Keep performance code edits parked until a repeated full-env
  `kdz` pass names a material official-row payer; use mechanism truth packs if
  continuing above-parity exploration.

## 2026-04-12 09:45 PDT

- Post-oracle retained matrix:
  `/tmp/kdz-retained-jitter-20260412085621` is still the current primary
  matrix. It includes remote-built `liboracle.so` rows and does not name a
  material red official-row blocker.
- Acceleration target 1:
  `/tmp/kdz-retained-jitter-20260412092847` rechecked
  `ffi_cdata/mixed_width_loop/hot` with higher samples. The target closed as
  parity/noise (`0.9966x` median, red `1/7`), with `pair_loop/hot` still in
  the fast band and `buffer_fref_loop/hot` only small/noisy.
- Acceleration target 2:
  `/tmp/kdz-retained-jitter-20260412093220` rechecked
  `ffi_fixed_call_pressure/gpr_pressure/hot`. It also closed as parity/noise
  (`1.0030x` median, red `2/7`), while `fpr_pressure/hot` stayed strongly
  compiled (`0.0228x`).
- Guardrail debt read:
  `/tmp/kdz-accel-guard-sweep-20260412093737` found no safe high-upside
  opt-out. The only tempting broad-iterator-root movement was rechecked in
  `/tmp/kdz-iterator-root-blacklist-focus-20260412094054`; it was too small
  (`pairs_sum/hot 0.9864x`, `pairs_array_sum/hot 0.9986x`), regressed
  `mixed_noffi/mixed_loop/hot` (`1.0210x`), and caused `pairs_loop.lua` to
  timeout when the broad fallback was disabled.
- Matrix policy:
  do not change retained matrix rows from these attribution reads. The next
  source mutation still requires a repeated official-row payer, not a noisy
  near-parity row or an unsafe guard opt-out.

## 2026-04-12 11:15 PDT

- FFI GPR acceleration retained:
  - pre-patch current-source truth pack
    `artifacts/s390x/truth-packs/20260412-110158-kdz-ffi_fixed_gpr-accel-truth-pack`
    reproduced repeated official `gpr_pressure` aborts at `IR_FLOAD`
  - [lj_asm_s390x.h](../../src/lj_asm_s390x.h) now supports 64-bit integer
    `FLOAD` fields with the existing full-width load path
  - `kdz` retained truth pack
    `artifacts/s390x/truth-packs/20260412-110542-kdz-ffi_fixed_gpr-accel-truth-pack`:
    `ffi_fixed_call_pressure/gpr_pressure/hot median=0.000260` versus
    `0.024619 -joff` (`0.0106x`), `TRACE_ABORT 0`
  - `zkd0` confirmation
    `artifacts/s390x/truth-packs/20260412-110939-zkd0-ffi_fixed_gpr-accel-truth-pack`:
    `gpr_pressure/hot median=0.000384` versus `0.048581 -joff` (`0.0079x`),
    `TRACE_ABORT 0`
- Sibling status:
  `fpr_pressure` and fixed-struct call rows remained in the compiled fast band
  on both hosts. `kdz` retained-env `vararg_paths`, `mixed_noffi`,
  `iterator_table`, and `dispatch_trace` smoke rows stayed in band.
- Current acceleration queue:
  continue with localized `bit.tobit` overflow-chain attribution next, then
  cdata mixed-width, then iterator safety debt. Keep broad guardrail removal
  out of scope unless a truth pack names a correctness-safe replacement.
