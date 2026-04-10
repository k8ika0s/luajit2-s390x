# s390x ISA Lab Next-Wave Plan

This document resets the ISA lab after the first text-helper wave and defines
the next ranked tracks, their first implementation slices, and their stop/go
criteria.

## Current Position

- The text lane has produced real wins with `span8`, while `ascii8` is now
  mixed on the current bring-up base.
- The ISA lab wrapper now defaults to `span8` only; `ascii8` remains opt-in
  until the upper-case and carry regressions are explained.
- The next wave should diversify away from text and attack:
  1. backend-only codegen quality
  2. opt-in feature differentiation
  3. narrow runtime microprimitives with real asynchronous behavior

## Decision Summary

Priority order:

1. backend-only s390x codegen polish
2. opt-in `s390x.experimental.decimal` module
3. runtime microprimitive probes around the async profiler path
4. deferred research: guarded storage, transactional execution, `PLO`, vector
   backend work

This order is intentional:

- backend work can improve existing traces without changing Lua-visible
  semantics
- decimal is the strongest feature-differentiation story on the platform
- runtime concurrency features do not fit the current GC/runtime shape except
  in a tiny number of narrow places
- guarded storage and transactional execution remain architecture-interesting
  but are not a near-term fit

## Track A: Backend Polish

Goal:

- improve generated code quality and trace coverage without changing core
  runtime semantics or depending on vector-register work

Primary files:

- `src/lj_asm_s390x.h`
- `src/lj_emit_s390x.h`
- `src/vm_s390x.dasc`
- `src/lj_target_s390x.h`

Current status:

- A1 is started.
- The first slice removes two narrow immediate choke points in the s390x
  backend:
  - large-constant `IR_ADD` now falls back to the normal register path instead
    of hard-`NYI`
  - large constant-index `IR_AREF` now uses `emit_addptr()` instead of
    hard-`NYI`
- Native `kdz1` validation is green for the focused `jit_be` slice, including
  tracked regression coverage in `tests/s390x/jit_be/large_immediates.lua`.
- The next A1 slice adds signed 32-bit RIL immediate forms for non-guarded
  large constants:
  - `CGFI` for signed integer compares beyond the 16-bit `CGHI` range
  - `AGFI` for large immediate add/sub cases that previously materialized a
    register and used the register-register path
  - focused coverage now includes large add, sub, compare, and constant-index
    array reference cases
- Native `kdz1` build and focused `tests/s390x/jit_be/large_immediates.lua`
  validation are green for this slice.
- Do not claim a perf promotion for the A1 RIL slice yet. The generic exit-0
  loop churn that previously dominated `tests/s390x/perf/large_immediates.lua`
  is now bounded by the A2 retrace guards, and the perf rows complete cleanly on
  `kdz1`, but this still needs a same-host A/B read before promotion.
- A JIT-on `dispatch_trace` perf restamp on `kdz1` still fails in the existing
  benchmark with `numeric_loop/hot: expected 3839172, got 0`; treat that as an
  open follow-up, not as proof that A1 is finished.
- A2 convert-first probe is rejected.
  - A minimal `IRSLOAD_CONVERT` `num <- int` cut in `asm_sload()` did not
    clear the carried `dispatch_trace` failure on `kdz1`.
  - The same cut also opened a new `jit_core` regression on
    `tests/s390x/jit_core/tostring_type_matrix.lua`.
  - Do not reopen that exact cut without a trace-level reproducer that proves
    the convert path is the real payer and that the unrelated `jit_core`
    regression is understood.

### A1. Immediate-Form Widening

First patch set:

- widen integer compare/add/sub/address emission beyond the current 16-bit
  immediate comfort zone
- use the widened helpers in:
  - `asm_intcomp`
  - `asm_add`
  - `asm_sub`
  - `asm_aref`
  - `emit_addptr`

Why first:

- this is the most likely backend-only win with low semantic risk
- it directly targets places where the current backend either materializes
  constants poorly or falls into `NYI`

Qualification:

- direct qualification on `large_immediates`
- same-host A/B on `bitops_mix`, `be_helpers`, and `mixed_noffi`
- only use `dispatch_trace` if the retained dispatch exactness env is carried
  explicitly; it is not a clean backend signal by default on this floor
- no regression in current text-family controls
- truth-pack restamp after each patch

Stop conditions:

- if the immediate-form changes do not produce either a measurable perf win or
  a meaningful reduction in `NYI`/fallback behavior after two clean passes,
  stop widening work and move to A2

### A2. Numeric Stub Closure And Convert Path

First patch set:

- implement `asm_abs`
- implement `asm_fpdiv`
- implement `asm_min`
- implement `asm_max`
- only revisit `IRSLOAD_CONVERT` support in `asm_sload` if a narrower,
  trace-proven reproducer exists

Current readout:

- `asm_abs` and `asm_fpdiv` are now implemented.
- Integer `asm_min`/`asm_max` are now implemented behind the
  `LUAJIT_S390X_INT_MINMAX` env gate.
- Native `kdz1` `jit_be` validation is green for
  `tests/s390x/jit_be/numeric_ops.lua`.
- With the min/max gate left off, `tests/s390x/jit_core/numeric_helpers.lua`
  is back to the previous broad-suite behavior and no longer crashes.
- A focused isolated same-host microbench for integer `math.min/math.max`
  against `k8ika0s/s390x-isa-lab-snapshot` is strongly favorable on `kdz1`:
  current gated build completes the benchmark in less than `0.01s`, while the
  snapshot baseline takes about `0.07s` on the same script.
- Same-host synthetic A/B against the clean `k8ika0s/s390x-isa-lab-snapshot`
  baseline is strongly favorable on the direct opcode targets:
  - `abs_loop`: about `48%` faster on `small`, `70%` on `medium`, and `77%`
    on `hot`
  - `div_loop`: about `83%` faster on `small`, `90%` on `medium`, and `92%`
    on `hot`
  - `sqrt_loop`: effectively flat, within about `-3%` to `0%`, which is the
    expected control because this slice does not touch `math.sqrt`
- There is still a separate s390x FP loop-exit issue on repeated calls to the
  same traced numeric loop body. The ISA-lab perf family works around that by
  using JIT-disabled reference functions and a fresh compiled prototype per
  sample, so treat the current readout as opcode-path qualification, not as a
  sign that the broader FP exit bug is resolved.
- The dedicated `tests/s390x/perf/numeric_ops.lua` crash is now understood and
  resolved:
  - moving the local `l_done` label in `asm_intmin_max()` to after register
    allocation removes the previous `max_loop` segfault on `kdz1`
  - the remaining wrong-answer path was caused by the integer `sload_int`
    typecheck compare being emitted in the wrong order for backward code
    generation on the ISA-lab floor
  - porting the current bring-up working-tree fix, specifically moving
    `emit_u32(... S390XI_CGR, tmp, expected)` to after the `expected`/`tmp`
    setup in `asm_sload()`, restores correct traced overflow behavior on
    `kdz1`
  - with that minimal fix in place:
    - plain traced `sum_loop(70000)` returns the correct `2450035000`
    - gated `max_loop(64000)` returns the correct `3072032000`
    - `tests/s390x/perf/numeric_ops.lua` completes cleanly on `kdz1`
- Treat the lane as unblocked for continued backend qualification. The
  corrected `sload_int` ordering should stay with the lab branch and be kept in
  sync with the active bring-up floor.
- The repeated-call retrace pathology is now isolated and bounded:
  - the first churn source was an exit-0 duplicate FORL/JFORI extra-loop
    descendant in `rec_loop_jit()`; the lab guard marks the parent exit snapshot
    done and leaves the duplicate path, but now excludes `KSTR` loop-body starts
    after `pairs_loop.lua` showed that the broader guard poisoned a string-key
    table-construction loop and fed an iterator side-trace ladder
  - the second churn source was a deeper exit-0 root-bridge descendant in
    `lj_record_stop()` linking back to the same compiled inner loop; the lab
    guard allows the direct bridge, marks deeper duplicate parent exits done,
    and leaves those descendants
  - a control loop using only integer `total = total + i` dropped from `2581`
    traces for 20 calls at `n=128` to `8` traces on `kdz1`
  - `tests/s390x/perf/numeric_retrace_probe.lua` stays bounded under repeated
    pressure: at `n=128`, `calls=200`, trace counts are `add=10`, `abs=11`,
    `min=10`, and `max=12`; the same counts remain bounded at `n=512`,
    `calls=200`
  - `tests/s390x/perf/numeric_ops.lua` now completes cleanly on `kdz1`; current
    hot medians are roughly `abs=0.00666`, `div=0.00521`, `sqrt=0.00633`,
    `min=0.00525`, and `max=0.00537`
- Treat A2 as unblocked for qualification, but not yet promoted. The direct
  duplicate churn is fixed enough for same-host A/B reads; still require two
  clean passes and broad controls before carrying the recorder guards outside
  the lab branch.
- The first widened correctness pass found a `pairs_loop.lua` hang. Rechecking
  against `k8ika0s/s390x-bringup-wip` showed bring-up passes the test, and
  overlaying only the lab `lj_record.c` reproduced the timeout. Narrowing the
  `rec_loop_jit()` guard to skip `KSTR` loop-body starts fixes the hang while
  preserving the bounded numeric retrace counts; the full current
  `tests/s390x/jit_loops` directory now passes on `kdz1`.
- The focused reproducer is `tests/s390x/perf/numeric_retrace_probe.lua`; run
  with `S390X_RETRACE_CASE=add|abs|min|max`, `S390X_RETRACE_N`, and
  `S390X_RETRACE_CALLS`.
- Same-host A/B against a clean `k8ika0s/s390x-bringup-wip` archive plus the
  shared lab probes showed the split clearly:
  - clean bring-up still hits the `add` retrace pathology at `n=512`,
    `calls=200`: `4096` scanned traces, `12` flushes, and `102626` starts
  - the lab branch stays bounded for the same run: `add=10`, `abs=11`,
    `min=10`, and `max=12` traces with no flushes
  - A1 large-immediate rows mostly improve sharply, but the `add_large` row is
    not a valid promotion signal yet because the baseline read is suspiciously
    near zero
  - A2 numeric helper perf is not promotable wholesale: `sqrt` improves, but
    `abs`, `div`, `min`, and `max` are slower than bring-up in this harness
  - keep the retrace guard as a churn/correctness-enablement fix, but do not
    promote the numeric helper family on speed without narrower follow-up work

Why second:

- the repo already has direct correctness and perf coverage for these
- this is a concrete trace-enablement lane, not speculative cleanup

Qualification:

- `tests/s390x/jit_core/numeric_helpers.lua`
- `tests/s390x/jit_be/number_helpers.lua`
- `tests/s390x/perf/be_helpers.lua`
- `tests/s390x/perf/dispatch_trace.lua`
- `tests/s390x/perf/numeric_retrace_probe.lua`

Stop conditions:

- if correctness becomes unstable or the work expands into broad VM interface
  changes, stop and park `asm_strto` plus deeper numeric work for a later pass

### A3. Call-Lowering Completeness

Third patch set:

- stack-passed call arguments beyond the register bank
- `CCI_VARARG`
- `CCI_CASTU64`

Current status:

- The first A3 slice is implemented for fixed-prototype stack-passed call
  arguments beyond the register bank.
- `asm_gencall()` now lowers GPR and FPR overflow arguments to the psABI stack
  argument area after the 160-byte s390x caller save area.
- `CCI_CASTU64` is implemented for the current `lj_prng_u64d()` user by
  bit-moving the GPR return value into the chosen FPR result; the traced
  `math.random()` loop now records on `kdz1`.
- `CCI_VARARG` FFI calls now use the same register/stack path for
  scalar integer, pointer, and FP arguments. The FP classification pass follows
  the interpreter-side s390x FFI behavior: double varargs continue through the
  FPR bank and then spill to the normal stack argument area. Current coverage
  includes traced `sum_varargs()`, `sum_varargs_double()`, mixed integer/FP
  `sum_varargs_mixed()`, signed/unsigned 32-bit `sum_varargs_i32()` and
  `sum_varargs_u32()`, string-pointer `sum_varargs_strlen()`, promoted
  small-int/bool/enum `sum_varargs_promoted_int()`, cdata-float promotion
  `sum_varargs_float_cdata()`, refarray/pointer/nil
  `sum_varargs_ptr_values()`, and function-pointer
  `sum_varargs_i32_callbacks()` tests. The mixed case overflows both GPR and
  FPR vararg banks and validates the shared stack-slot cursor in call order.
- Small s390x aggregate varargs are now covered for structs of size 1, 2, 4,
  and 8 bytes. The interpreter-side vararg type inference keeps those structs
  as aggregate values, and the recorder lowers them by loading the cdata payload
  as an unsigned integer call argument. The traced
  `ffi_struct_vararg_call_trace.lua` case validates `small_u8`, `small_u16`,
  `small_u32`, and 8-byte `small_u64` paths with enough arguments to force
  stack overflow.
- Single-field FP aggregate varargs are now covered separately for
  `struct { float }` and `struct { double }`. Interpreter-side classification
  routes those structs through the s390x FPR convention, while the recorder
  lowers them as FP payload loads. The backend now accepts FP `XLOAD`, emits
  short-BFP load/store forms for `IRT_FLOAT`, and uses the big-endian
  aggregate stack slot for float overflow. `ffi_fp_struct_vararg_call_trace.lua`
  validates both fixed-FP-seed overflow and GPR-seed calls that exercise the
  full F0/F2/F4/F6 vararg bank plus overflow.
- Larger s390x aggregate varargs are covered through the ABI's existing
  indirect aggregate path. `ffi_large_struct_vararg_call_trace.lua` validates
  16-byte `big_pair` varargs consumed by the C callee with
  `va_arg(ap, struct big_pair)`, including stack pressure.
- Complex s390x varargs are covered through the ABI's indirect complex path.
  The interpreter continues to infer complex varargs as complex values, while
  the recorder lowers traced complex cdata varargs as payload pointers for the
  s390x call path. `ffi_complex_vararg_call_trace.lua` validates
  `complex double` arguments under trace.
- Fixed-prototype aggregate arguments now reuse the same s390x call-lowering
  classification. Small 1/2/4/8-byte structs and single-field FP structs lower
  from cdata payload loads by value; larger fixed aggregates lower as cdata
  payload pointers to match the interpreter's by-reference path.
  `ffi_fixed_struct_call_trace.lua` validates scalar-return callees for
  `small_u8`, `small_u16`, `small_u32`, 8-byte `small_u64`,
  `struct { float }`, `struct { double }`, 16-byte `big_pair`, and 16-byte
  `hfa2d` arguments without opening the struct-return path.
- `ffi_fixed_struct_calls.lua` now provides the perf/promotion harness for
  fixed aggregate call lowering. It covers register and overflow forms for
  small integer aggregates, single-field FP aggregates, and read-only indirect
  large aggregates; use it as the A/B truth pack before broadening the fixed
  aggregate claim.
- Fixed complex arguments now use the same s390x payload-pointer lowering as
  complex varargs. `ffi_fixed_complex_call_trace.lua` validates scalar-return
  read calls and a mutation guard that proves caller cdata is not aliased.
- Stop line: `long double` and vector varargs are not part of the current claim.
  Cheap `kdz1` probes showed `long double` construction from Lua numbers fails
  at conversion time and GCC vector vararg calls are already `NYI` at the FFI
  call layer. Do not broaden this lane into vector/long-double call support
  without a separate ABI plan.
- The FFI oracle now covers `sum7_u64()` for 64-bit GPR overflow,
  `sum7_i32()` for signed 32-bit stack-argument extension, and `sum6_double()`
  for FPR overflow. `tests/s390x/jit_core/ffi_stack_call_trace.lua` tracks the
  traced fixed-prototype stack-call case.
- Native `kdz1` validation is green for the new stack-call trace, the existing
  FFI ABI oracle, `ffi_call_trace.lua`, `ffi_ptr_call_trace.lua`,
  `math_random_trace.lua`, `ffi_vararg_call_trace.lua`,
  `ffi_fp_vararg_call_trace.lua`, `ffi_mixed_vararg_call_trace.lua`,
  `ffi_width_vararg_call_trace.lua`, `ffi_string_vararg_call_trace.lua`,
  `ffi_struct_vararg_call_trace.lua`, `ffi_promotion_vararg_call_trace.lua`,
  `ffi_pointer_vararg_call_trace.lua`,
  `ffi_large_struct_vararg_call_trace.lua`,
  `ffi_fp_struct_vararg_call_trace.lua`, `ffi_complex_vararg_call_trace.lua`,
  `ffi_fixed_struct_call_trace.lua`, `ffi_calls`, `ffi_cdata`, `mixed_ffi`,
  and `vararg_paths`.
- The post-A3 guardrail pass is also green: `numeric_retrace_probe.lua` remains
  bounded at `n=512`, `calls=200`; `jit_be/numeric_ops.lua`,
  `jit_be/large_immediates.lua`, and the full current `jit_loops` directory
  pass on `kdz1`.
- Checkpoint `206bd094` (`Checkpoint s390x ISA lab gains`) freezes the current
  A1/A2/A3/decimal lab bundle. Two promotion-grade `kdz1` passes completed
  cleanly (`ISA_LAB_VALIDATE_pass1_OK` and `ISA_LAB_VALIDATE_pass2_OK`).
- Same-host A/B against clean `k8ika0s/s390x-bringup-wip` plus the shared lab
  probes classifies the current lanes as:
  - A3: promote for deeper review. The lab passes all focused FFI oracle,
    stack-call, `math.random`, and vararg trace probes; bring-up fails the
    new oracle small-aggregate vararg row and all traced A3 probes. The
    `vararg_paths/sum_loop/hot` row improves from `0.421679s` to `0.008489s`.
  - A1: promote for deeper review, except keep the `add_large` perf row
    non-promotional because the bring-up baseline remains suspiciously near
    zero. Bring-up fails the focused `jit_be/large_immediates.lua` trace check;
    lab passes it and improves the large immediate hot rows that do not have
    the invalid baseline.
  - A1 follow-up: a broad `emit_addptr()` `AGFI` replacement was correct on
    `kdz1` but rejected after same-host A/B because the large-immediate rows
    were neutral-to-mixed. A narrower distinct-operand slice is retained:
    `AGRK`/`SGRK`/`NGRK`/`OGRK`/`XGRK` are used only when `dest != left`, so
    already-coalesced traces keep the old two-address form while missed
    coalesces remove a move. Focused `kdz1` validation is green, `bitops_mix`
    is neutral, and `mixed_noffi/hot` improved from about `0.0424s` to about
    `0.0404s` in the selective A/B read.
  - A1 follow-up 2: the next adjacent distinct-operand slice is retained in
    the lab. Non-guarded small constant add/sub now use `AGHIK` only when
    `dest != left`, preserving the old `AGHI` path for coalesced or guarded
    cases; `BNOT` similarly uses `XGRK` only when it removes a move. Focused
    `kdz1` validation is green. Same-host `mixed_noffi/hot` improved in both
    post-`BNOT` reads (`0.042499s -> 0.040673s` and `0.041098s -> 0.040319s`);
    `bitops_mix` remains noisy/mixed, so keep the claim narrow.
  - A3 follow-up: fixed-call source classification is now instrumented under
    `LUAJIT_S390X_CALL_LOG`, and `ffi_fixed_call_pressure_trace.lua` plus
    `ffi_fixed_call_pressure.lua` cover the call-return-to-call pressure shape.
    The preserve/elision theory did not reproduce in the existing fixed-call
    traces: sources were not live in ABI argument registers. A narrower
    direct-materialization experiment for non-live GPR call args is retained
    as opt-in only via `LUAJIT_S390X_DIRECT_CALL_ARG`; same-host reads were
    mixed and did not clear the promotion gate (`gpr_pressure/hot` was
    neutral-to-slightly-worse in the focused harness, while small rows and some
    broad FFI rows improved). Keep this seam lab-only unless two clean future
    reads show a stable hot-row win.
  - A2: keep the retrace guard as an enablement/correctness fix, but keep the
    numeric helper perf family lab-only for now. The `add` retrace case drops
    from `4096` traces and `12` flushes to `10` traces and no flushes, while
    `abs`, `div`, `min`, and `max` hot rows are slower than bring-up; `sqrt`
    remains the only clear numeric helper speed win.
  - Decimal: keep frozen. The two pass smoke reads remained clean, but there is
    no bring-up A/B claim because the module is lab-only surface area.
- Rebased publish checkpoint:
  - The lab branch was replayed over the current `k8ika0s/s390x-bringup-wip`
    tip after bring-up advanced from the measured baseline. The completed
    same-host matrix remains a valid A/B read against bring-up `5200282f`, with
    lab `34e5bb7f`, `kdz1/gcc/release/jit=on/ffi=on/tuning=baseline`, zero
    failures, and `45` common comparable rows.
  - The rebased code checkpoint before this docs-only note is `9af4fe8b` over
    bring-up `9e38a069`; require a focused restamp before treating the older
    `5200282f` numbers as a final promotion read against current bring-up.
  - Main common-family readout from `isa-lab-rebased-matrix-20260410065312`:
    `dispatch_trace` `26.289x`, `vararg_paths` `3.364x`, `ffi_cdata`
    `2.406x`, `ffi_calls` `1.211x`, and `bitops_mix` `1.136x` geomean speed.
    The known regression checks are `iterator_table` and
    `ffi_cdata:mixed_width_loop`.
  - Next target order after publish: A3 FFI call-lowering promotion review
    first, A1 selective instruction forms second, A2 retrace guard as
    enablement-only, and `LUAJIT_S390X_DIRECT_CALL_ARG` parked as lab-only
    until two clean future hot-row reads justify enabling it.
- Post-publish `ffi_cdata:mixed_width_loop` seam:
  - The regression source was classified as missing s390x assembly support for
    narrow external stores: the mixed-width struct loop records `IR_XSTORE`
    for `uint16_t` and `uint8_t` fields, then default s390x lowering falls
    back with `NYI: cannot assemble IR instruction 78`.
  - A narrow backend experiment now exists behind
    `LUAJIT_S390X_NARROW_XSTORE`. With the flag set, `asm_xstore()` admits
    8/16-bit external stores, emits `STCY`/`STHY`, and the mixed-width loop
    records a real loop trace on `kdz1`. The focused `ffi_cdata_trace.lua`
    probe validates correctness in default mode and requires trace stop/no
    abort only when the flag is set.
  - Same-host `kdz1` readout parks the seam as lab-only: enabling narrow
    `XSTORE` regressed `mixed_width_loop` by about `8-10%` against the guarded
    default (`hot`: `0.031127s` vs `0.028701s`) and also nudged `pair_loop`
    slower. The guarded default stays within about `0-2%` of the pre-probe
    lab read and keeps the old fallback path.
  - Validation: default and flagged focused probes pass on
    `/root/luajit2-s390x-isa/manual-minmax/repo`; driver runs
    `s390x-xstore-guard-default-lab-20260410172009` and
    `s390x-xstore-guard-jitcore-20260410172549` completed with zero failures.
- Post-promotion current-tip text restamp:
  - Rebased the lab branch over bring-up `4b16b7e9` and kept a local safety
    pointer at `k8ika0s/s390x-isa-lab-gains-pre-4b16-rebase-20260410`.
  - Interpreted pure-Lua sanity passed on `kdz1` with wrapper defaults in
    `isa-lab-4b16-rebase-purelua-interp-20260410112849`. A JIT-on pure-Lua
    attempt failed only on fragile `t/isempty.t` trace-link text while output
    stayed correct; do not treat that as a text-lane failure.
  - Same-host current-tip A/B against generic text modes:
    `isa-lab-4b16-text-generic-20260410113254` vs
    `isa-lab-4b16-text-lanes-20260410113735`. Combined `span8+ascii8` stayed
    positive on text workloads (`text_patterns` `1.265x`, `text_mixed`
    `1.241x`, `text_combo` `1.143x`) but repeated small regressions in
    `text_casefold:upper_ascii` and `mixed_noffi`.
  - A reduced repeat confirmed the split:
    `isa-lab-4b16-text-generic-repeat-20260410114319` vs
    `isa-lab-4b16-text-lanes-repeat-20260410114747` kept `text_patterns`
    `1.272x` and `text_mixed` `1.294x`, but moved `text_casefold` to
    `0.976x` geomean with `upper_ascii` as low as `0.835x`.
  - Split-knob reads classify `span8` as the live text candidate and `ascii8`
    as opt-in only for now. `span8`-only
    (`isa-lab-4b16-text-span8only-20260410115313`) kept `text_patterns`
    `1.249x`, `text_mixed` `1.194x`, and `text_combo` `1.197x` against the
    generic control. `ascii8`-only
    (`isa-lab-4b16-text-ascii8only-20260410115812`) was much smaller
    (`text_casefold` `1.015x`, `text_combo` `1.004x`) and still regressed
    `upper_ascii` plus `mixed_noffi` hot/small rows.
  - Policy update: the ISA lab wrapper now defaults only
    `LUAJIT_S390X_TEXT_PATTERN_MODE=span8`; it leaves
    `LUAJIT_S390X_TEXT_TRANSFORM_MODE=generic` unless explicitly overridden.
    `ascii8` is not a promotion candidate until the upper-case path has a
    cleaner current-tip read.
  - Rebased again over bring-up `881440d7` after the A3/A1 promotion landed.
    The rebased wrapper default passed interpreted pure-Lua sanity on `kdz1`
    in `isa-lab-881-purelua-interp-20260410121526`. A focused same-host
    `text_patterns` restamp compared
    `isa-lab-881-text-patterns-generic-20260410121526` against
    `isa-lab-881-text-patterns-span8-default-20260410121526`: `span8`
    remained clean at `1.270x` geomean over 18 common rows, with no row below
    `1.027x` and a max win of `1.962x` on sparse word scanning.

Why third:

- valuable, but riskier than the first two slices
- this touches ABI and FFI-sensitive behavior and should only happen once the
  easier backend wins are exhausted

Qualification:

- `tests/s390x/perf/vararg_paths.lua`
- `tests/s390x/jit_core/ffi_call_trace.lua`
- `tests/s390x/jit_core/ffi_ptr_call_trace.lua`
- `tests/s390x/jit_core/ffi_stack_call_trace.lua`
- `tests/s390x/jit_core/ffi_fixed_complex_call_trace.lua`
- `tests/s390x/jit_core/ffi_fixed_struct_call_trace.lua`
- `tests/s390x/jit_core/ffi_vararg_call_trace.lua`
- `tests/s390x/jit_core/ffi_fp_vararg_call_trace.lua`
- `tests/s390x/jit_core/ffi_mixed_vararg_call_trace.lua`
- `tests/s390x/jit_core/ffi_width_vararg_call_trace.lua`
- `tests/s390x/jit_core/ffi_string_vararg_call_trace.lua`
- `tests/s390x/jit_core/ffi_struct_vararg_call_trace.lua`
- `tests/s390x/jit_core/ffi_promotion_vararg_call_trace.lua`
- `tests/s390x/jit_core/ffi_pointer_vararg_call_trace.lua`
- `tests/s390x/jit_core/ffi_large_struct_vararg_call_trace.lua`
- `tests/s390x/jit_core/ffi_fp_struct_vararg_call_trace.lua`
- `tests/s390x/jit_core/ffi_complex_vararg_call_trace.lua`
- `tests/s390x/jit_core/math_random_trace.lua`
- `tests/s390x/perf/ffi_fixed_struct_calls.lua`
- `tests/s390x/perf/mixed_ffi.lua`

Stop conditions:

- if the first call-lowering patch creates ABI instability or starts requiring
  broader FFI surgery, pause and split a separate FFI ABI plan

## Track B: Decimal Module

Goal:

- create a platform-distinctive opt-in feature that actually exploits s390x
  decimal facilities without perturbing LuaJIT's core binary-number semantics

Decision:

- use a Lua-visible module, not an internal helper lane
- module name: `require("s390x.experimental.decimal")`

Primary files:

- `src/lib_s390x_decimal.c`
- `src/lj_s390x_decimal.c`
- `src/lj_s390x_decimal.h`
- `src/lib_init.c`
- `src/lib_package.c`
- `src/Makefile`
- `src/ljamalg.c`

### B1. Minimal Viable Surface

MVP API:

- `capabilities()`
- `new(str[, format])`
- `tostring(x[, opts])`
- `add(x, y)`
- `sub(x, y)`
- `cmp(x, y)`
- `from_packed(bytes[, scale])`
- `to_packed(x[, digits])`
- `from_zoned(bytes[, scale])`
- `to_zoned(x[, digits])`

Out of scope for MVP:

- `math` integration
- parser or `tonumber()` changes
- FFI-native decimal ctype integration
- JIT recording or lowering
- vector packed-decimal acceleration in the first patch set

### B2. Minimal Implementation Sequence

First patch set:

1. capability detection and module registration
2. opaque decimal object plus `new`, `tostring`, and `cmp`
3. packed and zoned conversion APIs

Current status:

- B1 is started.
- The module skeleton is now implemented as a preload-only opt-in surface:
  `require("s390x.experimental.decimal")`
- The current MVP ships:
  - `capabilities()`
  - `new(str[, format])`
  - `tostring(x)`
  - `add(x, y)`
  - `sub(x, y)`
  - `cmp(x, y)`
  - `from_packed(bytes[, scale])`
  - `to_packed(x[, digits])`
  - `packed_to_string(bytes[, scale])`
  - `string_to_packed(str[, digits])`
  - `packed_rescale(bytes, scale[, digits])`
  - `from_zoned(bytes[, scale])`
  - `to_zoned(x[, digits])`
  - `zoned_to_string(bytes[, scale])`
  - `string_to_zoned(str[, digits])`
- The first cut uses exact software canonicalization and packed/zoned
  conversions. It does not claim DFP or vector packed-decimal acceleration yet.
- Native validation on `kdz1` is green for
  `tests/s390x/pure_lua/decimal_module.lua`, and direct require/use probes
  confirm preload registration works in the lab binary.
- A first arithmetic perf family is now live at
  `tests/s390x/perf/decimal_arith.lua` and runs cleanly on `kdz1`.
- Packed/zoned decode now returns the normalized scale to the caller, which
  fixes canonical string rendering for encoded values with trailing fractional
  zeros.
- `tests/s390x/perf/decimal_convert.lua` is now split enough to separate
  object-backed conversion from direct string/byte helpers. Current `kdz1`
  hot medians show:
  - packed roundtrip: `0.139924s`
  - packed direct: `0.078955s`
  - packed decode direct: `0.105566s`
  - packed encode direct: `0.108253s`
  - packed rescale: `0.112587s`
  - zoned roundtrip: `0.106582s`
  - zoned direct: `0.114266s`
- The decimal string formatter now uses a direct fixed-buffer writer instead of
  `luaL_Buffer`, which improved the packed direct path on `kdz1`:
  - packed direct hot: `0.085018s -> 0.078955s`
  - packed decode direct hot: `0.111043s -> 0.105566s`
- `packed_rescale()` gives a bytes-in/bytes-out packed utility path. It is not
  the hot-path winner yet, but it is the best packed conversion row at the
  medium scale (`0.007863s`) and keeps the packed-only bulk-processing lane
  viable without materializing decimal userdata.
- A direct-nibble `packed_rescale()` rewrite was retained after `kdz1`
  validation. It moved the best observed hot read from `0.112587s` to
  `0.105499s`, and the best medium read from `0.007863s` to `0.007607s`.
- A direct parse-and-emit rewrite for `string_to_packed()` and
  `string_to_zoned()` was tested and backed out. It was correct, but hot
  conversion rows stayed neutral-to-worse (`packed_encode_direct` hovered around
  `0.1067s-0.1081s`; zoned direct also regressed), so that seam is not worth
  carrying without a lower-level instruction-backed plan.
- A same-width identity shortcut for `packed_rescale()` was also tested and
  backed out. It improved medium reads (`~0.0073s`) but repeatedly hurt the hot
  policy row (`~0.1107s-0.1147s`), so the retained path is still the
  direct-nibble canonicalizer without an identity branch.
- Current decimal takeaway:
  - direct packed conversion is the current decimal performance lead
  - both packed direct decode and packed direct encode beat their object-backed
    equivalents on `kdz1`
  - packed bytes-in/bytes-out is promising, but still trails the string-backed
    direct path on the hot policy case
  - direct zoned conversion is roughly neutral right now
  - conversion work should stay focused on packed ingress/egress before adding
    more decimal surface area or revisiting zoned work

Second patch set:

4. `add` and `sub`
5. perf families for conversion and simple arithmetic

Third patch set:

6. consider `mul` and `div`
7. consider vector packed-decimal acceleration for bulk packed/zoned transforms

Qualification:

- new correctness file: `tests/s390x/pure_lua/decimal_module.lua`
- new perf families:
  - `tests/s390x/perf/decimal_convert.lua`
  - `tests/s390x/perf/decimal_arith.lua`
- driver registration in `tools/s390x/driver.py`

Success criteria:

- exact roundtrip correctness for decimal string, packed, and zoned formats
- measurable conversion throughput win relative to pure-Lua or generic C
  fallback paths
- zero change to existing Lua numeric semantics

Stop conditions:

- if toolchain support forces broad inline-asm complexity before the MVP works
- if exact formatting/canonicalization explodes the implementation surface
- if the module cannot be kept fully opt-in

## Track C: Runtime Microprimitives

Goal:

- probe the small part of the runtime that actually has asynchronous or shared
  coordination, without pretending the GC/runtime is already a concurrent
  design

Decision:

- guarded storage remains deferred
- transactional execution remains deferred
- `PLO` remains deferred
- only the profiler path and `gdbjit` lock are worth near-term probing

Primary files:

- `src/lj_profile.c`
- `src/lj_dispatch.c`
- `src/lj_gdbjit.c`

### C1. Async Profiler Probe

First patch set:

- measure the current profiler path under stress
- if it shows real contention or event-loss risk, prototype:
  - atomic increment for profile sample count
  - atomic/latch-style pending bit for profile request
  - keep existing lock around `lj_dispatch_update(g)`

Why this is the only sane first target:

- it already has true cross-thread coordination
- it is narrow enough to measure and revert cleanly

Qualification:

- dedicated profiler stress harness
- event-count correctness
- no lost or duplicated profile toggles
- collateral check on `dispatch_trace`

Stop conditions:

- if there is no measurable contention or sample-loss signal
- if atomic changes destabilize hookmask/dispatch behavior

### C2. GDBJIT Lock Probe

Second patch set:

- only if debug-heavy workloads justify it
- bounded backoff or cleaner CAS/spin handling around `gdbjit_lock`

Stop conditions:

- if debug registration remains niche and unmeasurable

## Deferred Research

Keep these documented, but out of the active execution queue:

- guarded storage for GC
- transactional execution on runtime critical sections
- `PLO` for runtime metadata
- vector-register backend work
- FFI-native decimal types

Reason:

- current GC is incremental mark/sweep with write barriers, not a moving or
  read-barrier design
- transactional execution and `PLO` need a concurrency model the runtime does
  not currently have
- vector backend work is larger than a patch-sized next wave and should wait
  until backend fundamentals stop rejecting useful IR

## Recommended Execution Order

1. Finish A1 immediate-form widening.
2. Keep A2 numeric stubs in qualification; the retrace probe is now bounded,
   but numeric perf still needs same-host A/B and two clean passes before
   promotion.
3. Continue B1/B2 decimal module stabilization without adding more surface area.
4. Resume A3 call-lowering completeness once A1/A2 qualification has a stable
   readout.
5. C1 async profiler measurement probe remains the next runtime-only lane.

## Promotion Rules

- Keep all of this in ISA-lab until each lane has:
  - a correctness suite
  - a dedicated perf family or truth-pack readout
  - two same-host clean passes on `kdz1`
  - no unexplained regression in the carried perf families

- Park a lane if:
  - it cannot beat or match the current baseline after two clean iterations
  - it requires semantic changes outside its track charter
  - it starts depending on facilities not cleanly discoverable at runtime

## Concrete Next Patch Recommendation

If the next hands-on implementation pass starts now, do this first:

1. Keep the current decimal state frozen around the retained direct-nibble
   `packed_rescale()` canonicalizer and the stronger correctness tests.
2. Use `numeric_retrace_probe.lua` as the regression gate for the bounded
   exit-0 side-trace guards.
3. Run same-host A/B for A1/A2, then move the next active backend work to A3
   call-lowering completeness if the qualification passes stay clean.
