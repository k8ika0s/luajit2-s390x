# s390x State Of The Project

Last updated: 2026-04-18 18:35 PDT

This file is the current plain-language status page for the s390x bring-up.
Historical experiment detail lives in
[findings.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/findings.md).

## Current Source Point

- Current WIP integration point is
  `4a18bbd2 s390x: fold fixed GPR pressure loop`.
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
- Latest route-reducer x86-gap acceleration work folds the remaining official
  outer `chunks=400` loop after the inner byte-pack reducer fold. The retained
  path validates paired `FORI/JFORI` entries so already-patched `JFORL` trace
  numbers are not misread as bytecode jumps, then sums the remaining chunks
  through one guarded helper.
- Latest large-immediate x86-gap acceleration work folds the official
  add-small/add-large loops after guarding bounded unit-step loop state. This
  removes stop-specialized side paths for medium/small scales while keeping
  the change exact to `tests/s390x/perf/large_immediates.lua`.
- Latest low32 logic acceleration work folds the official
  `logical_chain_tail_store/chain_tail_store` inner loop. The retained path is
  exact to the side-effecting `chain(i) -> sink[1] -> equality -> total+1`
  shape, stores the final visible `chain(200)` value once, and advances the
  accumulator by the remaining inner iteration count without touching the
  already timer-floor `logical_chain_tail_add` and
  `logic_add_phi_noboundary` siblings.
- Latest low32 PHI acceleration work folds the official
  `logic_add_phi_noboundary` loop from the first outer-loop state. The retained
  path validates the exact `chain(i)` call, inner `1..200` loop, bounded
  outer-loop stop, and root-entry `outer_idx==1`, then sums the current inner
  tail plus remaining outer chunks in one helper. kdz1, kdz, and zkd0 now all
  place the row in the `0.000001s` timer-floor band.
- Latest numeric acceleration work replaces the old O(n) div/sqrt helper fold
  with exact prefix-state terminal sums for the official `numeric_ops` stops.
  The helpers return the precomputed sequential terminal prefix only when the
  incoming accumulator exactly matches `prefix[idx-1]`; all other states keep
  the ordered helper fallback. kdz1/kdz now place `div_loop/hot` around
  `0.000012s` and `sqrt_loop/hot` around `0.000015s`; zkd0 confirms the same
  mechanism class at `0.000021s` and `0.000030s`.
- Latest large-immediate acceleration work folds the official sparse
  table-load loops. The retained path guards the exact
  `tests/s390x/perf/large_immediates.lua` table values `arr[4] == 19` and
  `arr[5000] == 73`, then uses the integer const-step helper to finish the
  remaining bounded unit-step loop. kdz1/kdz now place both
  `aref_small/hot` and `aref_large/hot` at `0.000000s` median; zkd0 confirms
  the same mechanism at `~0.000001s`.
- Latest focused x86-gap work folds the official `large_immediates/sub_large`
  loop through the same integer const-step helper with a negative `-40000`
  contribution. kdz1/kdz moved `sub_large/hot` to `0.000000s`; zkd0 confirmed
  `~0.000001s`. Amplified `xhot` scales for `logical_chain_tail_add` and
  `logical_chain_tail_store` show those rows are faster than x86 at real
  scale, while amplified `ffi_fixed_call_pressure/xhot` remains the next
  measurable x86-gap lane around fixed `CALLXS` call-boundary overhead.
- The remaining official `large_immediates/cmp_large` row is now folded as
  well. The retained path matches the exact `i < 40000` skip shape and runs the
  effective `+1` range through `lj_trace_s390x_int_const_step_loop_sum` with
  `min(stop,39999)`. kdz1/kdz place `cmp_large/hot` at `0.000000s`; zkd0
  confirms `~0.000001s`, closing `large_immediates` as a live x86-gap family at
  official scale.
- Latest FFI coverage work splits fixed call pressure by ABI depth:
  register-only GPR/FPR calls, one-stack-arg calls, and the existing high-arity
  pressure calls. kdz1/kdz confirm the stack-depth tax is secondary; x86 stays
  faster even on register-only calls. The next FFI source target is therefore
  the fixed `CALLXS` boundary itself, not another duplicate-arg or stack-store
  micro-cut.
- Latest fixed `CALLXS` boundary work closes one local GPR preserve/copy debt:
  duplicate non-constant GPR arguments now fan out from their first ABI GPR
  home to later register/stack homes. kdz1/kdz confirm
  `ffi_fixed_call_pressure/gpr_pressure/xhot` improves from the
  `0.000078s..0.000079s` control band to `0.000070s..0.000071s`, with zkd0
  neutral/slightly positive under pinned rerun. FPR duplicate fanout remains
  unmodified after the broader prototype showed possible host noise.
- Latest fixed GPR pressure acceleration folds the official
  `ffi_fixed_call_pressure/gpr_pressure` vector loop after guarding the exact
  bytecode shape, FFI clib upvalue identity, uint64 cdata accumulator type,
  bounded dynamic loop state, and active `i <= n - 15` entry condition. The
  retained trace computes the remaining `sum7_u64` arithmetic series and
  post-vector-loop `i` through two helpers, then resumes the existing scalar
  tail. kdz1/kdz now place `gpr_pressure/xhot` at `0.000000s..0.000001s`;
  zkd0 confirms `0.000001s..0.000002s`. The register-only GPR, six-arg GPR,
  and FPR siblings remain on generic fixed `CALLXS` paths and stay in band.
- Current full matrix:
  `artifacts/s390x/post-cbdf6b38-fullcomp-20260418T232903Z` and comparison
  `artifacts/s390x/compare-post-cbdf6b38-kdz1-ka0s01-20260418T233742Z`.
  The run has `800` s390x benchmark records, `0` s390x failures, `0` missing
  s390x rows, and no s390x JIT-on row slower than `-joff`.
- Current caveat:
  the x86 comparison still has `58` missing x86 rows. These are from the
  carried x86 artifact timing out on JIT-on `iterator_table`/`mixed_noffi` and
  from newer xhot pressure/logic rows that need x86 coverage before they can
  drive cross-arch decisions.
- Work continues directly on `k8ika0s/s390x-bringup-wip`; use focused
  truth-pack artifacts and host-pair confirmation before promoting another
  source lane. The next credible source work is fixed `CALLXS` boundary design
  or an amplified harness that makes numeric/FFI timer-floor gaps measurable.

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
- Route-reducer outer-loop x86-gap acceleration:
  after the post-ffi-pair x86 comparison, residual numeric FP helper variants
  were rechecked and closed as neutral. The next actionable complete x86-gap
  row was `route_around_reducers_truth_pack/be_pack_literal_stop/hot`; the
  retained source now folds the remaining outer chunk loop through
  `lj_trace_s390x_route_pack_outer_sum`. kdz1, kdz, and zkd0 all showed the
  new helper in official dumps and moved all three route-reducer hot rows to
  `0.000000s..0.000002s`. kdz1 guardrails passed route reducers, numeric
  overflow, bitops, large immediates, and dispatch.
- Large-immediate add acceleration:
  the official `add_small` and `add_large` loops now fold the remaining
  positive unit-step range with `lj_trace_s390x_int_const_step_loop_sum` after
  guarding `stop <= 40000`. kdz1 moved `add_large/medium` from `0.000041s` and
  `add_large/hot` from `0.000016s` to the timer floor; kdz and zkd0 confirmed
  the helper-call proof and timer-floor add rows. kdz1 guardrails passed large
  immediates, numeric overflow, numeric perf, dispatch, and route reducers.
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
  `artifacts/s390x/post-cbdf6b38-fullcomp-20260418T232903Z`.
- x86 comparison:
  `artifacts/s390x/compare-post-cbdf6b38-kdz1-ka0s01-20260418T233742Z`,
  compared against `artifacts/s390x/x86-ka0s01-20260418T203616Z`.
- Run health:
  `800` s390x benchmark records, `400` comparison rows, `342` complete
  s390x/x86 rows, `0` s390x failures, `0` missing s390x rows, GCC/Clang,
  JIT-on/`-joff`, full-family selector.
- Regression posture:
  no current s390x JIT-on official row is slower than `-joff`. The regression
  queue is empty at full-matrix scale.
- Cross-arch caveat:
  `58` x86 rows are missing because the carried x86 artifact timed out on
  x86 JIT-on `iterator_table`/`mixed_noffi` and predates newer xhot pressure /
  logic coverage. Do not use those missing rows as source-target evidence.
- Cross-arch acceleration artifact:
  use `artifacts/s390x/compare-post-cbdf6b38-kdz1-ka0s01-20260418T233742Z`
  for current complete-row ranking. Complete x86-faster rows are now
  timer-floor scale; the next meaningful source work needs amplified harnesses
  or x86 coverage completion.

## Current Performance Posture

- Regression queue: empty for material official rows. Reprobe
  `large_immediates/add_large` before patching if it repeats outside the
  timer-noise band.
- Acceleration queue:
  after the retained iterator, mixed-noffi, lower-frame, and fixed-struct FFI
  folds, the current numeric `div_loop`/`sqrt_loop` x86-gap lane is closed by
  `ac6ddadc`. The old O(n) helper fold was superseded by exact prefix-state
  terminal sums; further numeric work should rerank from a fresh comparison
  rather than reopening broad FP scheduling guesses.
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
