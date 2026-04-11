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
- A cleaned-driver focused perf pass
  `isa-numeric-ops-clean-20260411051558` completed with zero failures and
  `45` numeric benchmark rows across baseline, z13, and JIT-off variants. The
  result does not promote numeric speed: JIT-on baseline is still about
  `2.09x` slower than JIT-off geomean across `numeric_ops` (`abs` about
  `+76%` to `+82%`, `div` about `+120%`, `min`/`max` about `+123%` to
  `+133%`, `sqrt` about `+88%` to `+91%`). z13 tuning is neutral at about
  `+0.08%` geomean versus baseline. Keep A2 as correctness/churn enablement
  until a later lowering or trace-shape change changes the JIT-on/off posture.
- A2 restamp on `kdz1` after the A1/A3 rejection passes still shows the same
  bounded shape. Current repo at `8914116` reports zero flushes for
  `numeric_retrace_probe.lua` at `n=512`, `calls=200`, with trace counts
  `add=10`, `abs=11`, `min=10`, and `max=12`; `jit_be/numeric_ops.lua`,
  `jit_core/numeric_helpers.lua`, `perf/numeric_ops.lua`, and
  `perf/be_helpers.lua` also completed cleanly in the same pass.
- A2 guard-removal proof keeps the retrace guard classified as
  enablement/correctness, not a speed-only tweak. A throwaway `kdz1` build with
  both duplicate exit-0 guards disabled reproduced the old failure floor for
  all four focused cases at `n=512`, `calls=200`: each case hit the `4096`
  scanned-trace limit and `12` flushes, with roughly `102k` trace starts. The
  split build shows the `rec_loop_jit()` duplicate-loop guard is mandatory:
  disabling only that guard still hit `4096` traces and `12` flushes for
  `add` and `min`. Disabling only the deeper `lj_record_stop()` root-bridge
  guard stayed bounded but noisy (`add=114`, `min=212` traces), so keep it as
  trace-count cleanup unless a later simplification can prove it redundant.
- A later focused `kdz1` perf restamp with both guard disable env vars set,
  `isa-a1-disable-sload-forl-clean-20260411051558`, completed with zero
  failures across `iterator_table` and `mixed_noffi`, but did not support a
  speed-promotion claim for the guards. Against the same current-floor control
  run, the guard-disabled build was about `7.7%` faster geomean on
  `iterator_table` and about `1.5%` faster on `mixed_noffi`. Keep the guards
  classified as correctness/churn enablement based on the trace-count failure
  proof above, not as a broad perf win.
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
- `ffi_fixed_struct_pressure_trace.lua` now covers the repeated fixed-aggregate
  multi-arg family under trace: `take6`/`take7` for `small_u32`,
  `small_u64`, and `struct { double }`. Focused `kdz1` reads with
  `LUAJIT_S390X_CALL_LOG=1` and the new `S390X_CALL_STACK` logging show that
  the current backend already reuses one materialized source across both the
  register bank and overflow stack slots for these repeated fixed-prototype
  shapes. Classification result: park fixed-aggregate overflow as understood,
  not an immediate A3 optimization seam.
- Fixed complex arguments now use the same s390x payload-pointer lowering as
  complex varargs. `ffi_fixed_complex_call_trace.lua` validates scalar-return
  read calls and a mutation guard that proves caller cdata is not aliased.
  It now also covers fixed-prototype complex pressure with
  `take7_complex_sum()`: the traced call uses an FPR seed, fills the GPR bank
  with complex payload pointers, and spills the remaining payload pointers to
  stack overflow slots. Focused `kdz1` call logging shows the intended
  `S390X_CALL_STACK kind=gpr` overflow shape and the guardrail suite remains
  green, but there is no new optimization claim yet. The new
  `ffi_fixed_complex_calls.lua` perf probe is registered as probe-only in the
  driver and provides a measurement gate for this path; the first direct
  `kdz1` read completed with hot medians around `0.012s` for one complex arg,
  `0.018s` for pair, `0.0396s` for seven-complex pressure, and `0.0119s` for
  mutate-copy. A follow-up scratch read of JIT-on versus `-joff` showed this
  is not a speed seam yet: all fixed-complex rows were slower with JIT on
  (`complex_take7_call/hot` about `+10%`, simpler complex rows about
  `+25%` to `+46%`). Keep the complex-pressure probe as ABI/codegen coverage,
  not as a promotion candidate.
- A tracked-file rerun, `isa-profile-complex-probes-tracked-20260411090000`,
  completed with zero failures after staging the new complex perf probe file
  so the remote runner could sync it. The readout kept the existing
  classification: `ffi_fixed_complex_calls` is ABI/codegen coverage, not speed
  promotion material, with JIT-on about `28.2%` slower geomean than JIT-off
  across the focused complex rows; the `complex_take7_call` pressure row was
  the least bad but still slower by about `7.5%` to `8.4%`.
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
    A current-floor restamp against a throwaway variant with all selective
    A1 forms disabled still favors keeping the bundle overall:
    `mixed_noffi` geomean about `-5.48%`, `be_helpers` about `-0.74%`,
    `bitops_mix` about `-0.35%`, and hot iterator rows improved, while
    `large_immediates` stayed mixed and `add_small` was not a clean promotion
    signal. A follow-up split disabling only `AGHIK` was also rejected:
    no-`AGHIK` helped `mixed_noffi` geomean about `-1.61%`, but regressed
    `bitops_mix` about `+4.22%`, `iterator_table` about `+3.00%`,
    `be_helpers` about `+2.51%`, and `large_immediates` about `+1.31%`.
    Keep the current selective A1 forms; do not broaden the claim beyond the
    mixed/no-FFI and helper controls without another same-host pass.
  - A1 follow-up 3: indexed reference copy-elision before `SLLG` in
    `asm_aref()` and fused `asm_emitfuseahuref()` shapes is rejected for now.
    The allocator distinctness theory is plausible and the focused correctness
    set stayed green on `kdz1`, but the split restamp did not preserve the
    earlier favorable read. The copy-only variant was near-neutral overall:
    `large_immediates` geomean about `-0.36%`, `iterator_table` about
    `+0.64%`, and `mixed_noffi` about `+0.38%`, with `aref_large/hot` only
    about `-2.30%`. That is not enough to promote while iterator controls are
    still noisy, so keep the original pre-copy sequence.
  - A1 follow-up 4: direct narrow-field `FLOAD` selection is rejected for now.
    Swapping the current `LG`+shift sequence in `asm_fload()` for the direct
    `LLGC`/`LLGH`/`LGB`/`LGH` helpers was correctness-clean on `kdz1` for the
    live `IRFL_FUNC_FFID` (`iterator_trace_shape.lua`) and
    `IRFL_CDATA_CTYPEID` (`ffi_cdata_trace.lua`, `ffi_abi/run.lua`) paths, but
    the same-host A/B was materially worse. `ffi_cdata` regressed across every
    row by about `5.2%` to `7.6%`, and `iterator_table/pairs_array_sum/hot`
    regressed about `9.45%`, despite smaller wins elsewhere. Revert this cut
    and treat the current synthesized load+shift sequence as the better choice
    on the present floor.
  - A1 follow-up 5: direct signed-32 `FLOAD` via `LGF` is also rejected for
    now. Replacing the current `LLGF` + `LGFR` sign-extend sequence in the
    `irt_isint()` `asm_fload()` arm was correctness-clean on synced `kdz1`
    (`iterator_trace_shape.lua`, `isarray_root_loop.lua`,
    `string_global_paths.lua`), but same-host A/B did not stay favorable. The
    first serial pass was mixed-to-positive overall, yet the second pass
    regressed `iterator_table/pairs_sum/hot` about `11.45%`,
    `pairs_sum/small` about `9.50%`, `pairs_array_sum/hot` about `2.83%`, and
    `pairs_sum/medium` about `3.12%`, even while `mixed_noffi` stayed about
    `1.9%` to `4.7%` faster. Treat this as another non-promotable backend
    slice and keep the current zero-extend-then-sign-extend sequence.
  - A1 follow-up 6: direct address materialization via `LAY` is rejected for
    now. The broad `asm_tvptr()` plus `IRFL_TAB_ARRAY` `asm_fload()` sweep had
    already regressed helper and iterator controls. The later ref-only cut was
    also split and restamped on `kdz1`: the full ref-only bundle improved
    `large_immediates` geomean about `-1.32%` but still regressed
    `iterator_table/pairs_array_sum/hot` about `+9.38%`; AREF-only reduced the
    damage but still had `pairs_array_sum/hot` about `+5.19%`; const-index
    AREF-only kept strong `aref_large` wins but regressed `iterator_table`
    geomean about `+4.28%` and `be_helpers` about `+2.95%`; copy-only was too
    neutral to keep; and a large-offset-only threshold still regressed
    `iterator_table` about `+3.53%` and `mixed_noffi` about `+6.84%`. Keep the
    existing add/move address materialization paths until a lower-risk
    instruction-backed idea appears.
  - A1 follow-up 7: removing the residual pre-copy in non-`SRLK` 32-bit shift
    lowering is rejected. Dropping the `dest <- left` copy for the immediate
    `SLLG`/`SRAG` path and the variable `SLLK`/`SRAK` path stayed
    correctness-clean on synced `kdz1` (`bitops_trace.lua`,
    `jit_be/numeric_ops.lua`, `jit_be/large_immediates.lua`, and
    `iterator_trace_shape.lua`), but same-host A/B moved the wrong way:
    `bitops_mix` regressed about `0.7%` to `1.7%`, `mixed_noffi` regressed
    about `0.4%` to `0.6%`, and `be_helpers/number_helper_loop/hot` regressed
    about `2.0%`. Keep the existing source copy sequence.
  - A1 follow-up 8: branchless integer min/max via `LOCGR` is also rejected.
    The cut was correctness-clean on synced `kdz1`
    (`jit_be/numeric_ops.lua`; `numeric_retrace_probe.lua` stayed bounded for
    the `min` case), but the valid same-floor A/B against a cloned current lab
    baseline regressed the target family: `min_loop` worsened about `1.4%` on
    `hot`, `2.5%` on `medium`, and `4.3%` on `small`; `max_loop` worsened
    about `1.5%`, `2.4%`, and `3.7%` respectively. Keep the existing compare +
    conditional-branch lowering for now.
  - A1 follow-up 9: low-32 bitop normalization carry is rejected for now.
    The broad opt-in cut skipped `LGFR`/`LLGFR` when a 32-bit bitop result was
    consumed only by `BAND`/`BOR`/`BXOR`; the focused correctness set stayed
    green on `kdz1`, and `BITOP_LOG` confirmed that unrelated
    `large_immediates` and `iterator_table` controls emitted no bitop traces
    under this gate. The same-binary read
    `/tmp/isa-low32home-carry-ab-20260410203415` was still too narrow:
    `logical_chain_tail_add` improved about `-3.74%`, but `bitops_mix` was
    neutral (`-0.04%`) and `logic_add_phi_noboundary` regressed about
    `+0.58%`. A narrower `BSWAP`-only version was worse in
    `/tmp/isa-bswap-low32home-carry-ab-20260410203602`: `bitops_mix`
    regressed about `+0.49%`, `logic_add_phi_noboundary` about `+1.67%`,
    `mixed_noffi` about `+1.57%`, and `be_helpers` about `+0.85%`, with only
    sub-1% logical-chain wins. Keep the current eager 32-bit normalization
    sequence until a lower-risk trace-local proof appears.
  - A1 follow-up 10: double FPR spill load/store now has a narrow
    long-displacement fallback. The old emitter used 12-bit `LD`/`STD` for
    double FPR spill slots and asserted when the offset was outside `0..4095`,
    even though the backend already uses long-displacement `STDY` in other
    FPR stack paths. The lab cut adds `LDY` and uses `LDY`/`STDY` only when
    the offset does not fit the short RX form, preserving the existing short
    encoding for normal spill slots. This is robustness/reachability polish,
    not a speed claim. Focused `kdz1` validation
    `isa-lab-fpr-spill-longdisp-jitcore-20260411103000` passed build plus
    `jit_core` with zero failures.
  - A1/A2 rebase-floor switch restamps:
    `LUAJIT_S390X_AREF_BASE_ALLGPR` is rejected as a promotion candidate on
    current `kdz1`. The opt-in same-binary read
    `/tmp/isa-aref-base-allgpr-ab-20260410201735` helped `bitops_mix`
    geomean about `-1.66%` and `be_helpers` about `-0.34%`, but regressed
    `mixed_noffi` by about `+12.35%` and iterator hot rows by up to
    `+8.97%`. Keep the narrower fused-AREF allocation policy.
    The default-on signed GC64 integer SLOAD and FORL current-compare guards
    were initially favorable in one scratch switch read:
    `/tmp/isa-sload-guard-switch-ab-20260410201810`
    shows disabling signed-int SLOAD regresses `iterator_table` geomean about
    `+6.84%`, disabling the FORL compare fix regresses `iterator_table` about
    `+7.38%` and `mixed_noffi` about `+4.09%`, and disabling both regresses
    `iterator_table` about `+15.63%` plus `numeric_ops` about `+4.82%`. The
    later cleaned current-floor restamp below supersedes this as a speed claim:
    keep the guards justified by the retrace failure proof, not by this scratch
    perf read alone.
    A cleaned-harness current-floor control read
    `isa-a1-current-clean-20260411051558` completed with zero failures and
    `108` benchmark rows across `large_immediates`, `mixed_noffi`,
    `bitops_mix`, `be_helpers`, and `iterator_table`. This is a control read,
    not a new speed claim: JIT-on remains much slower than JIT-off geomean on
    the synthetic loop families (`iterator_table` about `21.64x`,
    `large_immediates` about `7.86x`, `mixed_noffi` about `10.34x`), while
    z13 tuning is only mildly favorable (`bitops_mix` about `-3.12%`,
    `iterator_table` about `-2.86%`, `be_helpers` about `-1.77%`, and
    `large_immediates`/`mixed_noffi` below `-0.5%`). Use this as a clean
    comparison floor for future A1 toggles; do not infer promotion from
    JIT-on/off posture alone.
    A follow-up SLOAD microcut that replaced the signed-int `LJ_TISNUM`
    constant register plus `CGR` with immediate `CGFI` is rejected for now.
    It was correctness-clean on `kdz1`
    (`numeric_ops.lua`, `numeric_helpers.lua`, `iterator_trace_shape.lua`,
    `isarray_root_loop.lua`, `string_global_paths.lua`, and
    `large_immediates.lua`), but same-host reads were order-sensitive:
    `/tmp/isa-sload-cgfi-ab-20260410202146` improved `mixed_noffi` about
    `-5.07%` and `numeric_ops` about `-1.59%` while regressing
    `be_helpers` about `+1.67%` and `iterator_table` about `+1.31%`;
    `/tmp/isa-sload-cgfi-ab2-20260410202252` then flipped the first numeric
    pass to `+3.16%` worse across all rows before a second pass moved back to
    `-2.33%` favorable. Do not retain the immediate-compare rewrite without a
    trace-shape or instruction-count proof that explains the noise.
    The older s390x `DSGR` fast path for signed integer modulo by a positive
    constant now has a dedicated measurement probe,
    `tests/s390x/perf/int_mod.lua`, registered as the probe-only `int_mod`
    family. Use it to qualify modulo lowering directly instead of inferring
    from the broader `dispatch_trace` rows, which mix modulo with side-exit and
    trace-dispatch behavior. Two same-host `kdz1` same-binary A/B rounds using
    the lab-only `LUAJIT_S390X_DISABLE_MODK_DSGR=1` switch do not clear the
    promotion gate. Round 1 favored the existing `DSGR` path by about `0.92%`
    geomean (`isa-int-mod-dsgr-default-20260411090000` versus
    `isa-int-mod-dsgr-disabled-20260411090000`), but round 2 favored disabling
    it by about `1.71%` (`isa-int-mod-dsgr-default2-20260411090000` versus
    `isa-int-mod-dsgr-disabled2-20260411090000`). Combined default/disabled
    geomean was `1.0038x`, or about `0.38%` slower with the existing path.
    Keep the probe and switch for future qualification, but do not promote
    modulo lowering from this signal.
  - A3 follow-up: fixed-call source classification is now instrumented under
    `LUAJIT_S390X_CALL_LOG`, and `ffi_fixed_call_pressure_trace.lua` plus
    `ffi_fixed_call_pressure.lua` cover the call-return-to-call pressure shape.
    The preserve/elision theory did not reproduce in the existing fixed-call
    traces: sources were not live in ABI argument registers. A narrower
    direct-materialization experiment for non-live GPR call args is retained
    as opt-in only via `LUAJIT_S390X_DIRECT_CALL_ARG`; same-host reads were
    mixed and did not clear the promotion gate (`gpr_pressure/hot` was
    neutral-to-slightly-worse in the focused harness, while small rows and some
    broad FFI rows improved). A rebase-floor restamp
    `/tmp/isa-direct-call-arg-rebased-ab-20260410201844` keeps the same
    classification: `ffi_fixed_call_pressure` improved about `-2.35%`,
    `mixed_ffi` about `-0.90%`, and `vararg_paths` about `-1.22%`, but
    `ffi_calls/stored_abs/hot` regressed about `+29.70%` and
    `ffi_fixed_struct_calls` regressed about `+1.29%` geomean with FPR-ish
    struct rows around `+2%` to `+4.5%`. Keep this seam lab-only unless two
    clean future reads show a stable hot-row win without the fixed-call and
    stored-call regressions. A follow-up duplicate-ref preserve slice
    that reused already-assigned GPR arg registers without the broad opt-in was
    correctness-clean on synced `kdz1`
    (`isa-lab-a3-dupref-jitcore-20260410202919`), but two focused same-host
    `ffi_fixed_call_pressure` baseline reads rejected it:
    `isa-lab-a3-dupref-perf-20260410203309` and
    `isa-lab-a3-dupref-perf2-20260410203738` were neutral-to-worse against the
    rebased promotion baseline on every meaningful baseline row
    (`gpr_pressure/hot` `+0.13%` then `+2.51%`, `gpr_pressure/medium` `+0.61%`
    then `+0.90%`, `fpr_pressure/hot` `+0.24%` then `+1.15%`). Do not retain
    the narrow duplicate-ref helpers; move the next A3 work to other fixed-call
    lowering/classification seams instead. A stack-constant GPR variant was
    also rejected: extending constant stack arguments in their materialized
    source register instead of allocating a second scratch was correctness-clean
    (`ffi_stack_call_trace.lua`, `ffi_fixed_struct_pressure_trace.lua`,
    `ffi_abi/run.lua`), but the same-host `kdz1` A/B regressed `ffi_calls`
    geomean about `+1.74%`, `ffi_fixed_struct_calls` about `+1.40%`, and
    `mixed_ffi/hot` about `+1.23%`; the only target win was the narrow
    `ffi_fixed_call_pressure/gpr_pressure/hot` row at about `-0.75%`, which is
    not enough to keep the change.
    A possible FPR-stack preserve seam was also classified without a code
    change. `ffi_fixed_struct_pressure_trace.lua` and
    `ffi_fixed_call_pressure_trace.lua` with `LUAJIT_S390X_CALL_LOG=1` show
    stack-overflow FPR sources in non-argument FPRs (`src=24/26/28`) while the
    live FPR argument targets are `16/18/20/22`; fixed-call pressure likewise
    places overflow GPR stack sources outside the active GPR argument bank.
    No `S390X_CALL_PRESERVE` events are needed for these covered shapes, so do
    not add an FPR analogue of the GPR stack-preserve helper unless a new trace
    shows an overflow source still resident in an argument register.
    A narrower direct-materialization gate for GPR call-result arguments was
    also rejected. Enriched `S390X_CALL_ARG` logging showed the scalar
    `ffi.C.abs` argument path was not touched (`op=41`), fixed-struct GPR args
    were not touched (`op=70`/`op=91`), and the intended target was only
    `ffi_fixed_call_pressure` GPR call-result args (`op=99`). The first
    same-host broad read looked promising for the target family
    (`/tmp/isa-direct-call-result-arg-ab-20260410204021`:
    `ffi_fixed_call_pressure` geomean about `-4.58%`), and a focused rerun
    still showed `gpr_pressure/hot` about `-2.95%`
    (`/tmp/isa-direct-call-result-pressure-ab-20260410204223`). But the
    second broad order-rotated read rejected it
    (`/tmp/isa-direct-call-result-arg-ab2-20260410204334`):
    `ffi_fixed_call_pressure` flipped to about `+0.97%`, `ffi_calls` regressed
    about `+2.54%`, `vararg_paths` about `+2.03%`, and `mixed_ffi` about
    `+1.91%`. Do not retain this gate; keep only the diagnostic op/type call
    logging for future A3 classification.
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
  - Broader same-host carry checks stayed clean with the narrower default
    policy. `text_mixed`
    (`isa-lab-881-text-mixed-generic-20260410122356` vs
    `isa-lab-881-text-mixed-span8-default-20260410122356`) showed `1.243x`
    geomean over 15 common rows with no regression below parity. `text_combo`
    (`isa-lab-881-text-combo-generic-20260410122848` vs
    `isa-lab-881-text-combo-span8-default-20260410122848`) showed `1.171x`
    geomean over 15 common rows with no row below `1.006x`, which supports
    keeping `span8` as the only default-on text helper while `ascii8` stays
    opt-in.
  - `%p`/`%P` punctuation seeking is now covered by `span8` without enabling a
    full punctuation span. The direct-start case is protected in the string
    prefilter because the first seek-only cut improved sparse punctuation rows
    but regressed direct-start punctuation. The final `kdz1` scratch read
    `/tmp/isa-text-punct-final-pP-ab-20260410171854` kept `text_patterns`
    favorable overall (`geomean_factor=0.853`, 30/36 rows faster), with
    punctuation sparse rows around `-32%` to `-34%`, inverse-punctuation sparse
    rows around `-9%`, punctuation seek rows around `-22%`, and
    inverse-punctuation seek rows around `-13%`. The remaining caveat is
    direct-start overhead: `%p+` direct rows were about `+1.4%` to `+3.3%`
    and `%P+` direct rows were about `-1.2%` to `+2.2%`, because the prefilter
    still performs one class check before falling back to the scalar span.
    The post-bring-up-rebase restamp
    `/tmp/isa-text-punct-rebased-ab-20260410201247` kept the same shape on
    `kdz1`: `text_patterns` stayed favorable overall
    (`geomean_factor=0.867`, 30/36 rows faster), `%p` sparse stayed around
    `-32%` to `-33%`, `%p` seek around `-22%`, `%P` sparse around `-7%` to
    `-8%`, and `%P` seek around `-9%` to `-10%`, while direct `%p+`
    regressed about `+3.8%` to `+5.1%` and direct `%P+` hot/medium about
    `+2.3%`. The same run kept `be_helpers` slightly favorable overall
    (`geomean_factor=0.994`) with only `number_helper_loop/hot` above 2%.
    Treat this as a lab candidate, not a promotion claim, until a second pass
    decides whether the sparse wins justify the direct-start cost. Carry
    control in
    `/tmp/isa-text-carry-behelpers-order-20260410171607` shows the unrelated
    `be_helpers` movement from the first generic-then-span8 run was order
    noise: span8 versus generic was `+0.16%` and `+0.28%` geomean on repeated
    order-rotated reads, with no row worse by 2%.
    A narrower gmatch-only punctuation prefilter was also rejected:
    `/tmp/isa-text-punct-gmatchonly-ab-20260410202544` preserved the sparse
    gmatch wins but turned `match_punct_seek` into a `+9%` regression and
    `match_nonpunct_seek` into a `+6%` to `+9%` regression because
    `string.match/find` still paid active-mode overhead without receiving the
    seek benefit. Keep the broader punctuation-seek candidate if this lane is
    revisited.
    Second-pass qualification on `kdz1` kept the broader text candidate alive:
    `/tmp/isa-text-punct-secondpass-ab-20260410220818` showed `span8` versus
    generic at `text_patterns` geomean about `-15.43%`, `text_mixed` about
    `-18.84%`, `text_combo` about `-11.51%`, `string_kernels` about
    `-0.19%`, and `be_helpers` about `-1.72%`. The direct `%p+` caveat was
    still present (`match_punct` about `+2.57%` to `+4.19%`), but sparse and
    seek rows stayed strongly favorable.
    A follow-up full `%p`/`%P` span cut is retained as the current text-lane
    candidate. It allows punctuation classes through the existing span8
    expansion instead of keeping them seek-only. The first same-host read
    `/tmp/isa-text-punct-fullspan-ab-20260410221019` improved the overall
    `text_patterns` geomean to about `-17.02%`, kept `text_mixed` about
    `-18.11%` and `text_combo` about `-11.80%`, and materially improved direct
    `%P+` (`match_nonpunct` about `-7%` to `-9%`). Direct `%p+` is still the
    remaining wart: hot improved to about `+1.02%`, but small/medium stayed
    around `+3%`. Keep this candidate for one more broad validation pass rather
    than promoting it immediately.
    The repeat broad validation pass
    `isa-text-fullspan-generic-repeat-20260411051558` versus
    `isa-text-fullspan-span8-repeat-20260411051558` gave the promotion signal
    needed for review: `text_patterns` geomean was about `-20.01%`,
    `text_mixed` about `-16.14%`, `text_combo` about `-12.39%`, and
    `be_helpers` stayed neutral-to-slightly-favorable at about `-0.43%`.
    The previous direct `%p+` wart did not materially repeat on this pass
    (`match_punct` ranged from flat to about `-0.8%`), while direct `%P+`
    remained favorable around `-8.6%` to `-9.5%`. `string_kernels` moved
    against the candidate by about `+2.51%` geomean despite not using the
    pattern mode; treat that as a noisy guardrail restamp item, not as the
    blocker for the `%p`/`%P` span cut. Both runs captured complete benchmark
    rows; the only driver failure on each side was the same unrelated
    JIT-off soak step after perf rows were already emitted.
    The harness false-failure is fixed in the lab driver: soak files that
    require the JIT are skipped for JIT-off perf comparison variants, matching
    the existing FFI-file skip behavior. Verification run
    `isa-text-patterns-span8-harness-clean-20260411051558` completed the
    `text_patterns` perf stage with all three variants, `108` benchmark rows,
    and zero failures.
    Correctness coverage now includes direct, seek, and sparse `gmatch`
    `%p`/`%P` cases in `tests/s390x/pure_lua/text_patterns.lua`. Local
    `src/luajit` validation passed, and the `kdz1` interpreter-stage gate
    `isa-text-punct-purelua-jitoff-20260411100000` completed with zero
    failures. A separate `--jit on` pure-Lua suite invocation failed later in
    unrelated `t/isempty.t` trace-output expectations, so do not treat that as
    a text-pattern regression.
    A focused guardrail restamp,
    `isa-text-guardrail-generic-20260411090000` versus
    `isa-text-guardrail-span8-20260411090000`, also completed cleanly with
    zero failures and `72` benchmark rows per side across `string_kernels` and
    `be_helpers`. The unaffected guardrail families did not give a clean
    blocker: baseline JIT-on `string_kernels` was still about `+1.06%`
    span8/generic, but JIT-off `string_kernels` was neutral-to-favorable
    (`-0.31%`) and z13 JIT-on moved the other way (`-2.31%`); `be_helpers`
    baseline JIT-on was flat (`+0.14%`). Treat the prior `string_kernels`
    movement as a watch item during promotion review, not as evidence that the
    `%p`/`%P` full-span path changes unrelated string kernels.
    Final full-span text-lane validation completed cleanly on `kdz1`:
    `isa-text-fullspan-generic-final-20260411100000` versus
    `isa-text-fullspan-span8-final-20260411100000` both had zero failures,
    including build, smoke, soak, and perf, with `270` common benchmark rows.
    Across all common rows, `span8/generic` was `0.8807x` (`-11.93%`).
    Family-level geomeans kept the promotion signal in the intended lane:
    `text_patterns` was about `-16.82%` for baseline JIT-on and `-14.82%`
    for z13 JIT-on; `text_mixed` was about `-16.36%` baseline JIT-on and
    `-17.16%` z13 JIT-on; `text_combo` was about `-12.56%` baseline JIT-on
    and `-15.61%` z13 JIT-on. Guardrails were acceptable but still noisy:
    `be_helpers` was about `-1.03%` baseline JIT-on and `-2.37%` z13 JIT-on,
    while `string_kernels` baseline JIT-on was about `+1.28%` even though
    JIT-off was `-1.14%` and z13 JIT-on was `-7.45%`. Keep the promotion
    claim scoped to the `%w`/`%a`/`%d`/`%p`/`%P` text-pattern span8 lane and
    carry `string_kernels` as a watch item, not as a promoted transform claim.
    A range-based ASCII punctuation classifier was rejected. It was
    correctness-clean, including an exhaustive `0..255` `%p` smoke check, but
    `/tmp/isa-text-punct-fastspan-ab-20260410221214` gave back too much of the
    `%P` and sparse gains: `text_patterns` weakened to about `-14.94%`,
    `text_mixed` to about `-15.90%`, `text_combo` to about `-10.66%`, and
    `string_kernels` regressed about `+1.00%`. Keep the table-backed
    `lj_char_ispunct()` classification in the candidate.
  - Split text mode restamp on the rebased floor rejects the remaining
    non-`span8` text knobs for promotion. In
    `/tmp/isa-text-split-modes-ab-20260410202900`, transform `ascii8` kept a
    narrow `text_casefold/lower_ascii` win around `-4%` to `-6%`, but regressed
    `upper_ascii` around `+3%` to `+4%`, `text_combo` geomean about `+2.68%`,
    and `text_mixed` about `+6.83%`. `bswap64` transform regressed
    `text_casefold` about `+10.47%` and `string_kernels` about `+2.81%`;
    `scan2` find regressed `text_combo` about `+3.94%`; libc `memmem` find
    regressed `string_kernels` about `+3.29%`, `text_combo` about `+3.28%`,
    and `text_mixed` about `+2.93%`. A compare-only restamp
    `/tmp/isa-text-compare-ab-20260410202946` improved broad mixed rows but
    regressed the direct `string_kernels/compare_order` target by about
    `+6%` to `+7%`, so keep compare mode generic too. Current promotion
    posture remains: `span8` only.

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

Current readout:

- The probe harness now exists in:
  - `tests/s390x/jit_core/profile_toggle.lua`
  - `tests/s390x/perf/profile_stress.lua`
- The perf harness scale is intentionally bounded (`1.2M`, `1.8M`, `2.7M`)
  because the first large-scale form was too slow for routine probe-matrix use,
  while smaller chunks could fail to receive a SIGPROF callback in
  `sample_toggle/small`.
- Focused `kdz1` correctness is green for both plain sampling mode (`i1`) and
  trace-flush profiling mode (`fi1`).
- The active platform path is confirmed to be `LJ_PROFILE_SIGPROF=1`, so this
  lane is not a pthread-lock contention problem on the current floor. The live
  seam is signal-time coordination in `lj_profile.c` around `ps->samples`,
  `ps->vmstate`, `g->hookmask`, and `lj_dispatch_update(g)`.
- A direct same-host `kdz1` perf read on the persistent
  `/root/luajit2-s390x-isa/manual-minmax/repo` tree shows low overhead and no
  event-loss signal relative to the same-binary `control_loop`:
  - hot: `sample_active -0.20%`, `sample_toggle +0.92%`,
    `trace_active +0.92%`, `trace_toggle +1.19%`
  - medium: `sample_active +0.77%`, `sample_toggle +0.77%`,
    `trace_active +0.78%`, `trace_toggle -0.05%`
  - small: `sample_active +3.08%`, `sample_toggle +3.06%`,
    `trace_active +0.57%`, `trace_toggle +3.08%`
- Interpretation:
  - the short-scale movement looks like timer granularity noise
  - hot and medium runs do not show a strong separation between plain sampling
    (`i1`) and trace-flush mode (`fi1`)
  - the probe is not a JIT-vs-interpreter speed claim; a scratch JIT-on versus
    `-joff` read showed the control loop itself is slower with JIT on, so the
    useful signal is profiler overhead relative to the same-mode control row
  - this is not a promising next promotion lane unless a future workload shows
    real sample loss or materially larger profiler overhead
- Promotion posture: park C1 as measured-but-not-promotable. Keep the harness
  for future regression checks, but do not spend runtime-atomic work here
  right now.
- The tracked-file rerun `isa-profile-complex-probes-tracked-20260411090000`
  stayed consistent with that posture. The focused profile rows completed with
  zero failures; baseline JIT-on hot overhead relative to same-variant
  `control_loop` was about `+0.05%` to `+0.06%`, z13 hot overhead about
  `+0.60%`, and the smaller rows were dominated by timer granularity noise.

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
5. C1 async profiler measurement is complete and parked; the next runtime-only
   work should be re-ranked only if a new shared-state hotspot appears.

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
4. Keep the async-profiler harness as a regression probe only; do not open the
   atomic pending-bit/sample-count experiment without a stronger overhead or
   event-loss signal.
