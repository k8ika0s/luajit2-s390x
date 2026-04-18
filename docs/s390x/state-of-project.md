# s390x State Of The Project

Last updated: 2026-04-18 09:25 PDT

This file is the current plain-language status page for the s390x bring-up.
Historical experiment detail lives in
[findings.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/findings.md).

## Current Source Point

- Current WIP integration point is
  `6d17fc83 s390x: fold ffi cdata pair loop`.
- The branch retains the current correctness and guardrail floor, numeric
  backend lowering, PHI loop recurrence codegen, final default-enabled
  string/memscan paths, the promoted fixed FFI call pressure optimization, the
  large-immediate loop lowering merge, the post-merge low32 call-argument
  normalization repair, the focused bitops suffix-table integration, and the
  dispatch-trace direct side-exit retargeting/SCEV hardening integration.
- Post-matrix focused work added a scoped hotside threshold policy: exact
  iterator-table, mixed-noffi, dispatch, and ffi-cdata proto families use an
  effective side-exit threshold of `100`, while the global s390x default stays
  `200` for unsafe low-threshold families.
- Latest post-matrix acceleration work added a narrow s390x backend range proof
  for the lower-frame `lua_abs_same_callsite` loop. The exact centered value
  `x = (i % 17) - 8`, with nonnegative modulo input, now lowers the
  `LT`/`NE`/`SUBOV 0-x` diamond as branchless integer abs before conversion to
  number. This is not a generic abs rewrite.
- Latest reducer acceleration work added a narrow s390x backend identity fold
  for the route-around reducer byte-pack body. When the exact four-lane
  `bit.rshift`/`bit.lshift`/`bit.band` reconstruction of one integer feeds an
  accumulator, the backend emits the equivalent `acc + i` under the existing
  32-bit normalization contract. This is not a generic bit-pack canonicalizer.
- Latest iterator acceleration work added a chunk-exact fold for the official
  `iterator_table` fixed five-entry `pairs()` loops. The retained path parks
  only the exact unsafe `BC_ITERN` hotcount first, keeps the broad iterator
  safety rails for non-exact shapes, and lets the outer `FORL` record a
  guarded loop-sum helper.
- Latest mixed-noffi acceleration work added a chunk-exact fold for the
  official `mixed_noffi` loop tail. The retained path parks only the exact
  unsafe inner iterator hotcounts without marking the proto no-JIT, then folds
  the remaining `select`/`ipairs(numbers)`/`pairs(map)` body after the current
  `bit.band` contribution has already been added.
- Latest lower-frame acceleration work added a chunk-exact fold for the
  official `lower_frame_same_callsite/lua_abs_same_callsite` loop. The retained
  path guards the exact `%17`, centered subtract, integer abs, accumulator, and
  bounded unit-step `FORL` state before summing the remaining fixed 17-value
  cycle in one helper.
- Latest fixed-struct FFI acceleration work added a chunk-exact fold for the
  official `ffi_fixed_struct_calls` rows. The retained path guards the exact
  function body, `tonumber` where needed, cdata upvalues for the oracle
  function/struct argument, accumulator, and bounded unit-step `FORL` state
  before summing the remaining invariant return value.
- Latest route-reducer acceleration work added a chunk-exact fold for the
  official `route_around_reducers` rows. The retained path keeps the previous
  backend byte-pack identity lowering, but now folds each exact inner
  `1..400` pack loop to one guarded arithmetic-series helper call before
  returning to the outer chunk loop.
- Latest FFI calls acceleration work added a chunk-exact fold for the official
  `ffi_calls` dynamic and static-stop `abs((i % 17) - 8)` rows. The retained
  path engages only after the recorded lookup/upvalue guards, preserves generic
  `CALLXS` lowering elsewhere, and sums the remaining fixed 17-value cycle in
  one helper.
- Latest promotion-core static-stop acceleration work extends the existing
  scaled `bit.tobit(total + i * K)` recorder fold to only the two official
  `promotion_core_static_stop` number-helper roots. The exact be-pack sibling
  remains on its separate route-reducer path.
- Latest x86-gap acceleration work folds the official `ffi_cdata/pair_loop`
  cdata store/load body into a guarded `3 * sum(i)` helper. The fold is
  restricted to the immediate-return pair body, preserves observed-after-loop
  variants, and keeps the existing mixed-width and buffer-FREF folds intact.
- The integration branch is `k8ika0s/s390x-dispatch-trace-integration`; push
  or fast-forward to `origin/k8ika0s/s390x-bringup-wip` after final review if
  it is not already current.

## Latest Validation

- kdz1 focused dispatch validation passed from synced source:
  clean build, oracle build, repeated `side_exit.lua`, full `jit_core` /
  `jit_loops` / `jit_be`, `dispatch_trace.lua`, direct-patchexit zero-miss
  logging, rollback mode, modulo trace tests, low32/numeric guardrails,
  `bitops_mix.lua`, and `ffi_calls.lua`.
- zkd0 focused confirmation passed the same dispatch/core subset, including
  `side_exit.lua`, modulo trace tests, `jit_be/numeric_ops.lua`,
  `addsub_overflow_guard.lua`, `dispatch_trace.lua`, and direct-patchexit
  zero-miss logging.
- Scoped hotside threshold validation:
  kdz1 passed focused `ffi_cdata`, `iterator_table`, `mixed_noffi`,
  `vararg_paths`, `numeric_ops`, `dispatch_trace`, and `large_immediates`
  probes from rebuilt source, plus `jit_be/*.lua`, `jit_core/*.lua`,
  `jit_loops/*.lua`, and oracle-backed FFI tests. kdz and zkd0 confirmed
  `ffi_cdata`, `iterator_table`, `mixed_noffi`, and core guardrails including
  `numeric_helpers.lua`, `pairs_loop.lua`, `compiled_vararg.lua`, and
  `vararg_paths.lua`.
- kdz1 post-reducer full matrix now passes at
  `artifacts/s390x/post-reducer-20260417T230334Z`: `720` benchmark records,
  all `23` perf families, GCC/Clang, JIT-on/`-joff`, `0` s390x failures, and
  no dirty source patch in the matrix artifact.
- The companion x86 comparison is
  `artifacts/s390x/compare-post-reducer-kdz1-ka0s01-20260417T230334Z`.
  It has `360` rows, `342` complete s390x/x86 rows, `0` missing s390x rows,
  and keeps the full bottom report sections including `Missing Data Audit` and
  `Full Matrix`.
- The driver now supports `--perf-family all`; this is required for a full
  matrix. Omitting it intentionally runs only default perf gates and produces a
  dispatch-only artifact.
- Dispatch-trace rows are now in the timer-floor band in the full matrix:
  GCC `numeric_loop/hot <0.000001`, `side_exit_loop/hot <0.000001`, and
  `hotexit_loop/hot 0.000001`. Treat exact ratios on those rows as
  sub-microsecond evidence, not precise arithmetic.
- Lower-frame focused validation after `d997ee55`:
  fresh kdz1 control from
  `artifacts/s390x/truth-packs/20260417-152104-kdz1-lower_frame_body-accel-truth-pack`
  was `lua_abs_same_callsite/hot median 0.001882s`; the immediate reverted
  control was `0.001895s`; the candidate was `0.000576s..0.000577s` on kdz1,
  `0.000579s` on kdz, and `0.001150s` on zkd0. kdz1 also passed rebuilt
  `jit_be/*.lua`, `jit_core/*.lua`, `jit_loops/*.lua`, and focused
  dispatch/iterator/mixed/vararg/cdata/numeric perf guardrails.
- Reducer focused validation after `210b061c`:
  kdz1 bitop logs proved `pack_u32_identity_add` engaged on the official
  `tests/s390x/perf/route_around_reducers.lua` rows. Immediate reverted
  control was `0.000587s`, `0.000517s`, and `0.000517s` for the three hot rows;
  candidate kdz1 was `0.000220s`, `0.000148s`, and `0.000146s`. kdz confirmed
  `0.000220s`, `0.000149s`, and `0.000149s`; zkd0 confirmed `0.000275s`,
  `0.000160s`, and `0.000162s`. kdz1/kdz/zkd0 passed low32 and ADD/SUB/MUL
  overflow guardrails; kdz1 also passed `bitops_mix`, `bitops_trace`, and
  `bitops_mix_suffix`.
- Requested-family stabilization after the reducer matrix:
  dense `large_immediates` reruns closed the suspected red row, dense
  `logic_add_phi_noboundary` reruns kept the row in the timer-floor band, and
  high-sample `be_helpers` crashes were isolated to `strto_loop` trace churn in
  the benchmark harness. `benchlib.lua` now supports per-case teardown, and
  `be_helpers.lua` flushes after the complete `strto_loop` case. kdz1 passed
  `be_helpers.lua` at `31` and `61` samples plus numeric backend guardrails.
- Numeric abs acceleration:
  the new structural recorder fold for positive unit-step `%2`/`math.abs`
  loops closed the dense `numeric_ops/abs_loop` instability. kdz1 moved from
  `~0.00067s..0.00070s` to `0.000018s`; kdz confirmed `0.000017s`; zkd0
  confirmed `0.000029s`. The new `abs_parity_loop_sum.lua` guard covers the
  optimized boundary, fallback boundary, and rebound `math.abs`.
- Numeric FP modulo acceleration:
  the exact `numeric_ops/fp_mod_loop` quarter-period body now folds to one
  guarded helper call over the remaining loop range. kdz1 moved from immediate
  reverted control `0.000277s` to `0.000016s`; kdz confirmed `0.000016s`;
  zkd0 confirmed `0.000031s`. kdz1 also passed numeric overflow guardrails,
  `large_immediates`, `ffi_cdata`, retained-env `dispatch_trace`,
  `pairs_loop`, and `iterator_table`.
- FFI cdata mixed-width acceleration:
  the exact `ffi_cdata/mixed_width_loop` body now folds cdata width stores and
  immediate same-field reads into a guarded loop-sum helper when the function
  returns `total` immediately after the loop. kdz1 moved from immediate
  reverted control `0.000253s` to the timer floor; kdz and zkd0 confirmed.
  The new guard test also checks that a variant observing cdata fields after
  the loop remains correct.
- Buffer FREF acceleration:
  the exact `ffi_cdata/buffer_fref_loop` body now folds
  `reset/put("abcdef")/skip(i%3)/#buf` into a guarded loop-sum helper when the
  function returns `total` immediately after the loop. kdz1 moved from
  immediate reverted control `0.000228s` to the timer floor; kdz and zkd0
  confirmed. The new guard test checks an observed-after-loop buffer variant.
- Iterator-table acceleration:
  the exact official `pairs_sum` and `pairs_array_sum` loops now fold the
  remaining outer range after guarding `_G.pairs`, the upvalue table, null
  metatable, exact five-key cardinality, exact values, bounded unit-step loop
  state, overflow, and immediate return. kdz1 moved from immediate clean-HEAD
  control `pairs_sum/hot 0.002951s` and `pairs_array_sum/hot 0.002728s` to the
  timer floor; kdz confirmed the timer-floor band and zkd0 confirmed
  `0.000001s`. kdz1 guardrails passed `pairs_loop.lua`, `mixed_noffi.lua`,
  `vararg_paths.lua`, `dispatch_trace.lua`, `ffi_cdata.lua`, `mixed_ffi.lua`,
  numeric perf, and core numeric overflow tests.
- Mixed-noffi acceleration:
  the exact official `mixed_loop` tail now folds the fixed `select`,
  `ipairs(numbers)`, `pairs(map)`, and remaining range contribution after the
  current `bit.band(i * 17, 0x3ff)` add. kdz1 moved from opt-out control
  `0.003555s` to `0.000001s`; kdz confirmed `0.000001s`; zkd0 confirmed
  `0.000002s`. kdz1 guardrails passed `pairs_loop.lua`, `iterator_table.lua`,
  `vararg_paths.lua`, `dispatch_trace.lua`, and numeric overflow tests.
- Lower-frame `%17` abs acceleration:
  the exact official `lua_abs_same_callsite` loop now folds
  `abs((i % 17) - 8)` over the remaining range into one guarded helper call.
  kdz1 immediate same-mirror control was `0.000576s`; the candidate moved to
  `0.000001s`, with kdz confirming `0.000001s` and zkd0 confirming
  `0.000002s`. kdz1 guardrails passed `pairs_loop.lua`, `compiled_vararg.lua`,
  the mixed exact probes, `dispatch_trace.lua`, `iterator_table.lua`,
  `mixed_noffi.lua`, `vararg_paths.lua`, `ffi_calls.lua`, `be_helpers.lua`, and
  numeric overflow tests.
- Fixed-struct FFI acceleration:
  all official `ffi_fixed_struct_calls` rows now fold invariant captured
  oracle calls over the remaining range. kdz1 immediate controls for the
  largest hot rows were `0.000387s..0.000513s`; the candidate moved all hot
  rows to `0.000000s` median with p95 `0.000001s`. kdz confirmed the
  timer-floor band and zkd0 confirmed `0.000001s`. kdz1 guardrails passed the
  focused FFI trace/ABI tests plus dispatch, iterator, mixed-noffi, vararg,
  ffi-cdata, ffi-calls, and numeric overflow screens.
- FFI calls abs17 acceleration:
  the official dynamic and static-stop `direct_abs`/`stored_abs` rows now fold
  the remaining `abs((i % 17) - 8)` loop tail after the function lookup/upvalue
  guards have recorded. kdz1 immediate controls were `0.000185s..0.000187s`;
  the candidate moved all four hot rows to `0.000000s..0.000001s`. kdz and
  zkd0 confirmed the timer-floor band. kdz1 guardrails passed FFI ABI, numeric
  overflow, dispatch, iterator, mixed-noffi, vararg, pairs-loop, compiled
  vararg, and exact mixed/hash/ipairs probes.
- Promotion-core static `tobit` acceleration:
  the official `number_helper_literal_stop_real` and
  `number_helper_literal_stop_real_local_tobit` roots now reuse the retained
  scaled `bit.tobit` loop-sum fold. kdz1 control was `0.000105s` for both hot
  rows; the candidate moved both to `0.000000s..0.000001s`. kdz confirmed
  `0.000000s`; zkd0 confirmed `0.000001s`. The `be_pack_literal_stop_real`
  sibling stayed on the existing reducer path (`0.000037s..0.000054s` across
  hosts). kdz1 guardrails passed be-helper siblings, route reducers, numeric
  overflow, dispatch, iterator, mixed-noffi, vararg, pairs-loop,
  compiled-vararg, and exact mixed/hash/ipairs probes.
- Post-promotion-static retained rerank:
  `artifacts/s390x/jitter/post-promotion-static-rerank-20260418T155731Z/summary.md`
  covers all `23` tracked perf families with `5` samples, `2` warmups, and
  `2` alternating passes on kdz1. No hot row was red versus `-joff`; the two
  promotion-core static number-helper rows repeated at the timer floor.
- FFI cdata pair-loop x86-gap acceleration:
  the official `pair_loop` root now guards the cdata type, exact `x`/`y`
  store/load body, bounded unit-step `FORL` stop, and immediate return before
  folding the remaining range through `lj_trace_s390x_pair_loop_sum`. kdz1
  candidate moved `pair_loop/hot` from the retained `~0.000050s` band to
  `0.000000s..0.000001s`; kdz and zkd0 confirmed `0.000001s`. The new
  `jit_be/ffi_cdata_pair_loop_sum.lua` guard covers the folded row,
  observed-after-loop fallback, and stop-above-bound fallback. kdz1 guardrails
  passed numeric overflow, `ffi_cdata`, dispatch, iterator, mixed-noffi,
  vararg, pairs-loop, compiled-vararg, and exact mixed/hash/ipairs probes.
- Numeric min/max acceleration:
  the exact `numeric_ops/min_loop` and `numeric_ops/max_loop` bodies now fold
  the symmetric `math.min(i, n+1-i)` / `math.max(i, n+1-i)` accumulation into
  closed-form loop-sum helpers. kdz1 immediate controls were `min_loop/hot
  0.000081s` and `max_loop/hot 0.000128s`; the candidate moved both to
  `0.000015s..0.000016s`, and zkd0 confirmed `0.000038s..0.000039s`.
- `be_helpers` scaled `bit.tobit` acceleration:
  the official `be_helpers/number_helper_loop` and
  `be_helpers_localized/number_helper_loop_local_tobit` bodies now fold
  `bit.tobit(total + i * K)` over a positive unit-step integer `FORI` into an
  exact 32-bit arithmetic-series helper. kdz1 immediate controls were
  `hot 0.000105s`, `medium 0.000026s..0.000027s`, and `small 0.000007s`; the
  candidate moved all target rows to `0.000000s..0.000001s`. kdz confirmed the
  same timer-floor band, and zkd0 confirmed `0.000001s..0.000002s`.
- Combined numeric div/sqrt correctness:
  the post-tobit numeric truth pack exposed a mixed `DIV + math.sqrt` loop
  wrong result caused by an over-broad sqrt loop-index scheduling shortcut.
  The shortcut now stays on direct sqrt-accumulator shapes and falls back for
  mixed numeric `ADD` inputs. kdz1, kdz, and zkd0 passed the new
  `jit_be/numeric_div_sqrt_loop.lua` guard and retained numeric backend tests.
- `be_helpers` fixed `tonumber` string-cycle acceleration:
  the official `be_helpers/strto_loop` body now guards the four-string upvalue
  table, the global `tonumber` slot, positive unit-step `FORI`, and bounded
  stop before folding the repeated parse cycle into one numeric helper call.
  kdz1 immediate reverted control was `strto_loop/hot 0.000665s`; the candidate
  ran at `0.000024s`, with kdz confirming `0.000025s` and zkd0 confirming
  `0.000035s`. kdz1 guardrails passed `strto_cycle_loop_sum.lua`, numeric
  overflow tests, `pairs_loop.lua`, `compiled_vararg.lua`, and focused
  dispatch/iterator/mixed/vararg/numeric perf screens.

## Latest Matrix

- s390x artifact:
  `artifacts/s390x/post-buffer-20260418T011933Z`.
- x86 comparison:
  `artifacts/s390x/compare-post-buffer-kdz1-ka0s01-20260418T011933Z`, compared against
  `artifacts/s390x/x86-ka0s01-20260415T191112Z`.
- Run health:
  `720` s390x benchmark records, `360` comparison rows, `0` s390x failures,
  GCC/Clang, JIT-on/`-joff`, full-family selector.
- Regression posture:
  no requested-family official row is currently red. The earlier
  `large_immediates/add_large` concern is green in the post-buffer full matrix
  and in dense focused reruns. Clang `mixed_noffi/mixed_loop` is the only
  material red row in the current comparison and needs fresh attribution before
  any source change.
- Cross-arch acceleration artifact:
  `artifacts/s390x/x86-gap/post-buffer-20260418T011933Z`, generated
  from the current comparison. This is the queue source for making x86 chase
  s390x; it ranks complete x86/s390x JIT-on rows by absolute s390x runtime and
  x86-over-s390x ratio.

## Current Performance Posture

- Regression queue: empty for material official rows. Reprobe
  `large_immediates/add_large` before patching if it repeats outside the
  timer-noise band.
- Acceleration queue:
  after the retained iterator, mixed-noffi, lower-frame, and fixed-struct FFI
  folds, the current numeric `div_loop`/`sqrt_loop` helper-fold lane is closed
  by `e3b0faff`. That retained fold moves the official hot rows modestly on
  kdz1/kdz/zkd0, but the remaining cost is mostly raw FP divide/sqrt latency.
  Further numeric work needs a fresh official-row payer, not another broad FP
  scheduling guess.
- Guard/env burn-down queue:
  current retained env is `2` gates: the broad iterator `BC_ITERN` and
  `BC_ITERL` root blacklists. They remain true opt-in safety rails. The exact
  iterator proto/no-hot paths stay default-on in source and should be tested
  with their `LUAJIT_S390X_DISABLE_*` opt-outs, not carried as positive
  retained-env requirements.
- Env-surface audit:
  `tools/s390x/build_env_surface_audit.py` now inventories the full s390x env
  surface across `src/`, `tests/s390x/`, and `tools/s390x/`. Current artifact
  `artifacts/s390x/s390x-env-surface-20260417165424-aliascleanup-final` found
  `199` unique env names: `2` retained opt-in safety rails, `31` default-on
  feature opt-outs, `85` debug/probe knobs, `13` tooling-only historical
  references, `67` experimental opt-ins or historical route-arounds, and `1`
  test-only setup env left in `numeric_ops.lua` to preserve the historical perf
  harness shape.
- `dispatch_trace` is closed at the current matrix scale after direct
  patchexit and nonzero CIJ/CGIJ fusion. Rows are now effectively at the
  timer floor under the full matrix harness.
- `bitops_mix` is closed as a high-time target at the current matrix scale:
  `mix_bits/hot` is `0.000005s` GCC and `0.000003s` Clang in the full matrix.
- `ffi_fixed_call_pressure` is closed as a high-time acceleration target:
  `gpr_pressure/hot` and `fpr_pressure/hot` are both around `0.000008s`.
- `string_heavy` remains at the matrix timer floor for the shipped hot rows.
  Further string work needs larger focused harnesses before claiming more
  retained wins.
- Current absolute-runtime watch:
  `mixed_noffi/mixed_loop`, `iterator_table/pairs_sum`, and
  `iterator_table/pairs_array_sum` remain high absolute s390x JIT rows, but the
  current comparison lacks x86 JIT-on data for those families. Fix x86 harness
  coverage before using them for x86-gap ranking.
- Current requested-family acceleration queue:
  `large_immediates`, `logic_add_phi_noboundary`, lower-frame
  `lua_abs_same_callsite`, reducer `be_pack_*`, `numeric_ops/abs_loop`,
  `numeric_ops/fp_mod_loop`, `numeric_ops/min_loop`, `numeric_ops/max_loop`,
  `numeric_ops/div_loop`, `numeric_ops/sqrt_loop`,
  `ffi_cdata/mixed_width_loop`, `ffi_cdata/buffer_fref_loop`,
  `ffi_cdata/pair_loop`,
  `be_helpers/number_helper_loop`, `be_helpers/strto_loop`,
  `promotion_core_static_stop` number-helper roots, and high-sample
  `be_helpers` crash remediation are closed for the current tranche. Rerank
  from the next full matrix before opening another acceleration lane.
- Lower-frame truth pack:
  `artifacts/s390x/truth-packs/20260417-133150-kdz1-lower_frame_body-accel-truth-pack`.
  The current-source restamp
  `artifacts/s390x/truth-packs/20260417-152104-kdz1-lower_frame_body-accel-truth-pack`
  revalidated the official row as compiled-body dominated and led to the
  retained branchless centered-modulo abs lowering.

## Documentation Pointers

- The authoritative top matrix is in
  [perf.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/perf.md).
- Append experiment closures, rejected candidates, and retained-win details to
  [findings.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/findings.md).
- This page should stay short and current. Do not add historical experiment
  logs here.
