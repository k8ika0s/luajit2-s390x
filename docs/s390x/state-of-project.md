# s390x State Of The Project

Last updated: 2026-04-17 07:40 PDT

This file is the current plain-language status page for the s390x bring-up.
Historical experiment detail lives in
[findings.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/findings.md).

## Current Source Point

- Current WIP cleanup base is
  `52a863be Retire stale s390x retained env gates`; this update carries the
  follow-on two-gate retained-env cleanup.
- The branch retains the current correctness and guardrail floor, numeric
  backend lowering, PHI loop recurrence codegen, final default-enabled
  string/memscan paths, the promoted fixed FFI call pressure optimization, the
  large-immediate loop lowering merge, and the post-merge low32 call-argument
  normalization repair.
- The tracked WIP branch has been pushed to
  `origin/k8ika0s/s390x-bringup-wip`.

## Latest Validation

- kdz1 focused low32 call-argument repair validation passed from the tracked
  mirror after committed source sync.
- The post-repair default driver perf gate now passes at
  `artifacts/s390x/s390x-kdz1-20260417T032039Z-de121bc1-driverfix`. The driver
  no longer attempts unsupported Clang z13 builds; z13 tuning remains covered
  by GCC.
- The post-repair retained-env all-family rerank now passes at
  `artifacts/s390x/kdz-retained-jitter-20260417032751-41abe5a5`, covering all
  `23` tracked perf families in `3` alternating JIT/JIT-off passes with no red
  rows versus `-joff`.
- The retained env has since been reduced from `15` gates to `2` real opt-in
  gates. Removed entries are stale vararg select gates, the root1 ITERL replay
  pair, exact mixed-noffi gates that no longer carry retained behavior, and
  default-on exact iterator routes that are controlled only by `DISABLE_*`
  opt-outs in source. The current post-cleanup kdz all-family rerank is
  `artifacts/s390x/kdz-retained-jitter-20260417060026-2gate`, also clean
  across all `23` tracked perf families.
- The reduced FFI pressure reproducer now matches `-joff`.
- The isolated fixed-call arg probe now returns correct values for arguments
  1..7, including argument 4 in R5.
- GCC and Clang focused `tests/s390x/perf/ffi_fixed_call_pressure.lua` passed
  with `gpr_pressure/hot` and `fpr_pressure/hot` in the
  `0.000007..0.000009s` median band.
- Guardrails passed:
  `tests/s390x/jit_be/*.lua`,
  `tests/s390x/jit_core/ffi_fixed_call_pressure_trace.lua`,
  `tests/s390x/jit_core/ffi_stack_call_trace.lua`,
  `tests/s390x/ffi_abi/run.lua`, focused `large_immediates.lua`,
  `ffi_calls.lua`, `ffi_cdata.lua`, and `mixed_ffi.lua`.

## Latest Matrix

- s390x artifact:
  `artifacts/s390x/s390x-kdz1-20260416T235054Z`.
- x86 comparison:
  `artifacts/s390x/compare-kdz1-ka0s01-20260416T235054Z`, compared against
  `artifacts/s390x/x86-ka0s01-20260415T191112Z`.
- Run health:
  `2160` s390x benchmark records, `360` comparison rows, `0` s390x failures,
  GCC/Clang, JIT-on/`-joff`, and three alternating passes.
- Regression posture:
  no material s390x JIT-on blocker. The generated summary lists only two tiny
  per-pass GCC `large_immediates/add_large` rows slower than `-joff`; the
  top-matrix median keeps `large_immediates/add_large/medium` green at
  `1.077x`.
- Matrix caveat:
  the top matrix is still the last clean full comparison. The post-large
  immediate attempt `artifacts/s390x/s390x-kdz1-20260417T015612Z` is invalid
  because it exposed a now-fixed `ffi_fixed_call_pressure` JIT-on wrong result.
  The driver-style post-fix rerun
  `artifacts/s390x/s390x-kdz1-20260417T024302Z-d037816e` stopped on unsupported
  Clang z13 flags and is not a replacement matrix. The follow-up driver rerun
  at `artifacts/s390x/s390x-kdz1-20260417T032039Z-de121bc1-driverfix` is clean,
  but it is the default driver perf gate, not the full cross-arch comparison.

## Current Performance Posture

- Regression queue: empty. Do not patch from noise-level red rows without a
  repeated official-row mechanism.
- Guard/env burn-down queue:
  current retained env is `2` gates: the broad iterator `BC_ITERN` and
  `BC_ITERL` root blacklists. They remain true opt-in safety rails. The exact
  iterator proto/no-hot paths stay default-on in source and should be tested
  with their `LUAJIT_S390X_DISABLE_*` opt-outs, not carried as positive
  retained-env requirements.
- Env-surface audit:
  `tools/s390x/build_env_surface_audit.py` now inventories the full s390x env
  surface across `src/`, `tests/s390x/`, and `tools/s390x/`. Current artifact
  `artifacts/s390x/s390x-env-surface-20260417143448-bcb578fb` found `202`
  unique env names: `2` retained opt-in safety rails, `31` default-on feature
  opt-outs, `4` default-on positive aliases, `85` debug/probe knobs, `13`
  tooling-only historical references, and `67` experimental opt-ins or
  historical route-arounds.
- `ffi_fixed_call_pressure` is closed as a high-time acceleration target at
  the current matrix scale: `gpr_pressure/hot` is `0.000008s` GCC /
  `0.000009s` Clang, and `fpr_pressure/hot` is `0.000008s` on both compilers.
- `string_heavy` remains at the matrix timer floor for the shipped hot rows.
  Further string work needs larger focused harnesses before claiming more
  retained wins.
- Current acceleration queue by absolute JIT time:
  `mixed_noffi/mixed_loop`, `iterator_table/pairs_sum`,
  `iterator_table/pairs_array_sum`,
  `lower_frame_same_callsite/lua_abs_same_callsite`, and GCC
  `be_helpers/strto_loop`.
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
