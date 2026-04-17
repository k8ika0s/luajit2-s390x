# s390x State Of The Project

Last updated: 2026-04-17 12:41 PDT

This file is the current plain-language status page for the s390x bring-up.
Historical experiment detail lives in
[findings.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/findings.md).

## Current Source Point

- Current WIP integration point is
  `ae1b2103 Add full s390x perf-family selector`, on top of
  `b27708d7 Document s390x bitops mix merge path`.
- The branch retains the current correctness and guardrail floor, numeric
  backend lowering, PHI loop recurrence codegen, final default-enabled
  string/memscan paths, the promoted fixed FFI call pressure optimization, the
  large-immediate loop lowering merge, the post-merge low32 call-argument
  normalization repair, and the focused bitops suffix-table integration.
- The integration branch is `k8ika0s/s390x-wip-bitops-mix-integration`; push
  to `origin/k8ika0s/s390x-bringup-wip` after final review.

## Latest Validation

- kdz1/kdz/zkd0 focused bitops validation passed from synced source. The
  mechanism log shows `add_bxor_mix_suffix200_tail`; kdz1 and kdz report
  `bitops_mix/mix_bits/hot` around `0.000002s`, and zkd0 around `0.000003s`.
- kdz1 clean full matrix now passes at
  `artifacts/s390x/20260417T192108.036453Z-p98275`: `720` benchmark records,
  all `23` perf families, GCC/Clang, JIT-on/`-joff`, `0` failures, and no
  dirty patch.
- The companion x86 comparison is
  `artifacts/s390x/compare-kdz1-ka0s01-20260417T192732Z`. It has `360` rows,
  `342` complete s390x/x86 rows, `0` missing s390x rows, and keeps the full
  bottom report sections including `Missing Data Audit` and `Full Matrix`.
- The driver now supports `--perf-family all`; this is required for a full
  matrix. Omitting it intentionally runs only default perf gates and produces a
  dispatch-only artifact.
- Focused mixed-noffi follow-up after the full matrix:
  kdz1 `artifacts/s390x/20260417T192839.151719Z-p4160` and kdz
  `artifacts/s390x/20260417T193222.690803Z-p6537` are green; zkd0
  `artifacts/s390x/20260417T193612.667585Z-p9038` remains hot-red. Treat it as
  a cross-host watch item requiring fresh attribution before code.

## Latest Matrix

- s390x artifact:
  `artifacts/s390x/20260417T192108.036453Z-p98275`.
- x86 comparison:
  `artifacts/s390x/compare-kdz1-ka0s01-20260417T192732Z`, compared against
  `artifacts/s390x/x86-ka0s01-20260415T191112Z`.
- Run health:
  `720` s390x benchmark records, `360` comparison rows, `0` s390x failures,
  GCC/Clang, JIT-on/`-joff`, full-family selector.
- Regression posture:
  only GCC `mixed_noffi/mixed_loop` is slower than `-joff` in the full kdz1
  matrix. Immediate focused reruns disagree by host, so this is a watch item,
  not a merge blocker for the bitops integration.

## Current Performance Posture

- Regression queue: `mixed_noffi/mixed_loop` watch only. Reprobe with dense
  same-host A/B and mechanism logs before patching.
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
- `bitops_mix` is closed as a high-time target at the current matrix scale:
  `mix_bits/hot` is `0.000004s` GCC and `0.000003s` Clang in the full matrix.
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
