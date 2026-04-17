# s390x State Of The Project

Last updated: 2026-04-17 15:53 PDT

This file is the current plain-language status page for the s390x bring-up.
Historical experiment detail lives in
[findings.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/findings.md).

## Current Source Point

- Current WIP integration point is
  `210b061c s390x: fold reducer byte-pack identity`.
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
- kdz1 clean full matrix now passes at
  `artifacts/s390x/scoped-hotexit100-20260417T213642Z`: `720` benchmark
  records, all `23` perf families, GCC/Clang, JIT-on/`-joff`, `0` failures,
  and no dirty patch.
- The companion x86 comparison is
  `artifacts/s390x/compare-scoped-hotexit100-kdz1-ka0s01-20260417T213642Z`.
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

## Latest Matrix

- s390x artifact:
  `artifacts/s390x/scoped-hotexit100-20260417T213642Z`.
- x86 comparison:
  `artifacts/s390x/compare-scoped-hotexit100-kdz1-ka0s01-20260417T213642Z`, compared against
  `artifacts/s390x/x86-ka0s01-20260415T191112Z`.
- Run health:
  `720` s390x benchmark records, `360` comparison rows, `0` s390x failures,
  GCC/Clang, JIT-on/`-joff`, full-family selector.
- Regression posture:
  no material official row is currently red. The scoped-hotexit matrix reports
  no JIT-on row slower than `-joff`; the earlier `large_immediates/add_large`
  small/medium concern is green in the full matrix and in focused reruns.
- Cross-arch acceleration artifact:
  `artifacts/s390x/x86-gap/x86-gap-20260417T-crossarch-baseline`, generated
  from the current comparison. This is the queue source for making x86 chase
  s390x; it ranks complete x86/s390x JIT-on rows by absolute s390x runtime and
  x86-over-s390x ratio.

## Current Performance Posture

- Regression queue: empty for material official rows. Reprobe
  `large_immediates/add_large` before patching if it repeats outside the
  timer-noise band.
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
- Current cross-architecture acceleration queue:
  numeric `abs_loop` dense-sample instability, reducer `be_pack_*`, and Clang
  `be_helpers/strto_loop`. Lower-frame `lua_abs_same_callsite` is closed by the
  focused branchless-abs backend win pending the next full matrix refresh.
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
