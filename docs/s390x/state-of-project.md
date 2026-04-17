# s390x State Of The Project

Last updated: 2026-04-16 19:51 PDT

This file is the current plain-language status page for the s390x bring-up.
Historical experiment detail lives in
[findings.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/findings.md).

## Current Source Point

- Current WIP source point is
  `d037816e Fix s390x low32 call arg normalization`.
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
  Clang z13 flags and is not a replacement matrix.

## Current Performance Posture

- Regression queue: empty. Do not patch from noise-level red rows without a
  repeated official-row mechanism.
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
