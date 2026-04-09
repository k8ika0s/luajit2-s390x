# s390x ISA Lab Next-Wave Plan

This document resets the ISA lab after the first text-helper wave and defines
the next ranked tracks, their first implementation slices, and their stop/go
criteria.

## Current Position

- The text lane has produced real wins with `span8` and `ascii8`.
- The broader carry story is positive enough to keep those defaults enabled in
  the ISA lab wrapper, but not clean enough to recommend them outside the lab.
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

Why second:

- the repo already has direct correctness and perf coverage for these
- this is a concrete trace-enablement lane, not speculative cleanup

Qualification:

- `tests/s390x/jit_core/numeric_helpers.lua`
- `tests/s390x/jit_be/number_helpers.lua`
- `tests/s390x/perf/be_helpers.lua`
- `tests/s390x/perf/dispatch_trace.lua`

Stop conditions:

- if correctness becomes unstable or the work expands into broad VM interface
  changes, stop and park `asm_strto` plus deeper numeric work for a later pass

### A3. Call-Lowering Completeness

Third patch set:

- stack-passed call arguments beyond the register bank
- `CCI_VARARG`
- `CCI_CASTU64`

Why third:

- valuable, but riskier than the first two slices
- this touches ABI and FFI-sensitive behavior and should only happen once the
  easier backend wins are exhausted

Qualification:

- `tests/s390x/perf/vararg_paths.lua`
- `tests/s390x/jit_core/ffi_call_trace.lua`
- `tests/s390x/jit_core/ffi_ptr_call_trace.lua`
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

1. A1 immediate-form widening
2. A2 numeric stub closure plus `IRSLOAD_CONVERT`
3. B1 decimal module skeleton plus conversions
4. C1 async profiler measurement probe
5. A3 call-lowering completeness
6. B2 decimal arithmetic expansion

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

1. backend A1 immediate-form widening
2. if that lands cleanly, backend A2 `asm_abs` + `asm_fpdiv`
3. in parallel planning only, scaffold the `s390x.experimental.decimal` module
   surface and test cases, but do not start FFI or parser integration
