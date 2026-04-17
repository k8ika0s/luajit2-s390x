# s390x State Of The Project

Last updated: 2026-04-17 13:10 PDT

This file is the current plain-language status page for the s390x bring-up.
Historical experiment detail lives in
[findings.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/findings.md).

## Current Source Point

- Current WIP integration point is
  `ef3db658 s390x: accelerate dispatch trace side exits`, on top of
  `28f70e67 Update s390x bitops matrix status`.
- The branch retains the current correctness and guardrail floor, numeric
  backend lowering, PHI loop recurrence codegen, final default-enabled
  string/memscan paths, the promoted fixed FFI call pressure optimization, the
  large-immediate loop lowering merge, the post-merge low32 call-argument
  normalization repair, the focused bitops suffix-table integration, and the
  dispatch-trace direct side-exit retargeting/SCEV hardening integration.
- The integration branch is `k8ika0s/s390x-dispatch-trace-integration`; push
  or fast-forward to `origin/k8ika0s/s390x-bringup-wip` after final review.

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
- kdz1 clean full matrix now passes at
  `artifacts/s390x/dispatch-trace-integration-20260417T200154Z`: `720`
  benchmark records, all `23` perf families, GCC/Clang, JIT-on/`-joff`, `0`
  failures, and no dirty patch.
- The companion x86 comparison is
  `artifacts/s390x/compare-kdz1-ka0s01-20260417T200936Z`. It has `360` rows,
  `342` complete s390x/x86 rows, `0` missing s390x rows, and keeps the full
  bottom report sections including `Missing Data Audit` and `Full Matrix`.
- The driver now supports `--perf-family all`; this is required for a full
  matrix. Omitting it intentionally runs only default perf gates and produces a
  dispatch-only artifact.
- Dispatch-trace rows are now in the timer-floor band in the full matrix:
  GCC `numeric_loop/hot <0.000001`, `side_exit_loop/hot <0.000001`, and
  `hotexit_loop/hot 0.000001`. Treat exact ratios on those rows as
  sub-microsecond evidence, not precise arithmetic.

## Latest Matrix

- s390x artifact:
  `artifacts/s390x/dispatch-trace-integration-20260417T200154Z`.
- x86 comparison:
  `artifacts/s390x/compare-kdz1-ka0s01-20260417T200936Z`, compared against
  `artifacts/s390x/x86-ka0s01-20260415T191112Z`.
- Run health:
  `720` s390x benchmark records, `360` comparison rows, `0` s390x failures,
  GCC/Clang, JIT-on/`-joff`, full-family selector.
- Regression posture:
  no material official row is currently red. Only `large_immediates/add_large`
  small/medium is slower than `-joff`, and the absolute runtimes are too small
  to patch without focused repeat evidence.

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
- Current acceleration queue by absolute JIT time:
  `mixed_noffi/mixed_loop`, `iterator_table/pairs_sum`,
  `iterator_table/pairs_array_sum`,
  `lower_frame_same_callsite/lua_abs_same_callsite`, GCC/Clang
  `be_helpers/strto_loop`, and selected `ffi_fixed_struct_calls` pressure rows.
- Cross-architecture watch rows:
  `large_immediates` and selected `numeric_ops` small/medium rows. Treat these
  as acceleration research, not branch blockers.

## Documentation Pointers

- The authoritative top matrix is in
  [perf.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/perf.md).
- Append experiment closures, rejected candidates, and retained-win details to
  [findings.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/findings.md).
- This page should stay short and current. Do not add historical experiment
  logs here.
