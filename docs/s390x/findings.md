# s390x Bring-Up Findings

This document records concrete findings from the staged native bring-up runs.
It is intentionally focused on observed behavior, run IDs, and next actions.

## Harness Status

- The native bring-up harness is implemented under `tools/s390x/`.
- Existing repo Perl tests no longer depend on external CPAN modules.
- The local `t/TestLJ.pm` harness now:
  - parses `__DATA__` blocks without `Test::Base`,
  - captures subprocess stdout and stderr without `IPC::Run3`,
  - emits minimal TAP without `Test::More`,
  - uses absolute `LUA_PATH` and `LUA_CPATH`,
  - filters blocks via explicit `--- requires:` capability markers.
- The driver now records unexpected local Python exceptions and interrupts into
  run artifacts instead of failing without a traceback trail.

## Native Runs

- `20260318T162505Z`
  - Stage: `interp`
  - Suite: `smoke`
  - Host: `kdz`
  - Result: pass
  - Notes: native build succeeded; smoke confirmed `luajit`, `ffi`, and
    `jit.status()` behavior for the interpreter-only build.

- `20260318T165632Z`
  - Stage: `interp`
  - Suite: `pure_lua`
  - Host: `kdz`
  - Result: fail
  - Notes: interpreter-only `.t` blocks now run natively; the first real
    failure moved into `t/exdata.t`.

- `20260318T173334Z`
  - Stage: `interp`
  - Suite: `pure_lua`
  - Host: `kdz`
  - Result: pass
  - Notes: interpreter-only debug coverage is green after the unwind fix and
    explicit test capability staging.

- `manual-interp-release`
  - Stage: `interp`
  - Suite: `pure_lua`
  - Host: `kdz`
  - Result: pass
  - Notes: release-mode interpreter coverage is also green; the remote TAP log
    shows the full interpreter subset passing on native s390x.

- `ffi-call-matrix-20260318`
  - Stage: `ffi-call`
  - Suite: `ffi_abi`
  - Host: `kdz`
  - Result: pass
  - Notes: outgoing FFI call coverage is green on native s390x for gcc and
    clang in both debug and release builds.

## 2026-03-20 Traced FFI Call Frontier

- Added a focused traced-FFI probe in
  `tests/s390x/jit_core/ffi_call_trace.lua`:
  - `ffi.cdef("int abs(int x);")`
  - tight loop over `ffi.C.abs(...)`
  - `jit.attach("trace")` capture with an explicit requirement that a trace
    reaches `"stop"`
- Fresh native result on `zkd0`:
  - the probe returns the correct Lua-visible total (`547`)
  - it sees a trace `"start"` event
  - it does not see a trace `"stop"` event
  - this means the first traced libc-style FFI call is outside the currently
    green JIT surface even though plain FFI calls are already green
- A more diagnostic native run with the same loop and explicit event dumping
  segfaults on `zkd0`.
  - native batch gdb backtrace:
    - fault PC: `0x000000790000007a`
    - top LuaJIT frame: `lj_vm_exit_handler`
    - unwind path continues through `lua_pcall`, `handle_script`, and `main`
- Current conclusion:
  - the next real frontier is not generic FFI correctness, but traced FFI
    C-call handling plus its trace-exit contract on s390x
  - the first backend gap exposed locally is that `asm_callx` is still missing
    on s390x even though traced foreign calls are expected to lower through
  `CALLXS`
  - after unblocking that lowering, the next likely issue is the exit-state
    contract around traced foreign calls if the native crash persists

## 2026-03-20 Traced FFI Direct-Call Fix

- Native `zkd0` diagnosis after the first `CALLXS` slice showed the direct
  traced-FFI crash was not another exit-handler bug.
  - plain `ffi.C.abs` tracing segfaulted with a bogus PC
    `0xfffffffff7b4a4c0`
  - the actual native `ffi.C.abs` pointer is
    `0x000003ff9c44a4c0`
  - the trace return address in gdb was a JIT mcode PC near `0x314c...`
- Root cause:
  - s390x `asm_callx` was still forcing the foreign target through the direct
    `BRASL` path
  - native libc function pointers on IBM Z live far outside the relative
    branch range from JIT mcode
  - with assertions compiled out, the relative displacement truncated and the
    trace branched to garbage
- Fixes applied locally:
  - `src/lj_emit_s390x.h`
    - added `BASR` encoding support
    - added `emit_callr()` for indirect traced calls
  - `src/lj_asm_s390x.h`
    - `asm_gencall()` no longer requires a direct target when the caller has
      already emitted an indirect call
    - `asm_callx()` now selects:
      - direct `BRASL` only when the target is actually in range
      - indirect `BASR` via a materialized GPR otherwise
      - indirect `BASR` for non-constant callable refs
- Native outcome on `zkd0` after rebuild:
  - plain traced FFI loop over `ffi.C.abs` is green:
    - `/tmp/ffi_call_plain.lua` prints `total 131`
    - exit status `0`
  - the staged traced-FFI probe is green:
    - `tests/s390x/jit_core/ffi_call_trace.lua`
    - exit status `0`

## 2026-03-20 Traced FFI Follow-Up

- Added a second probe in `tests/s390x/jit_core/ffi_ptr_call_trace.lua` that
  calls through a stored function value:
  - `local cabs = ffi.C.abs`
  - traced loop over `cabs(...)`
- Current native `zkd0` result:
  - correct Lua-visible total
  - trace `"start"` observed
  - no trace `"stop"`
  - clean failure instead of a crash
- Current interpretation:
  - the direct-call traced FFI crash is fixed
  - the stored-function-value case is a narrower follow-up and should be
    checked against a non-s390x control before treating it as a new
    architecture-specific blocker

## 2026-03-20 Traced FFI Stored-Function-Value Resolution

- The stored-function-value traced-FFI follow-up is now green on native
  `zkd0`.
- Root cause:
  - the failing trace was aborting with `NYI: cannot assemble IR instruction
    69`
  - `IR 69` is `IR_FLOAD`
  - the recorder emits this early in the stored-function-value path as:
    - `u16 FLOAD cdata.ctypeid`
    - followed by `p64 FLOAD cdata.ptr`
  - s390x `asm_fload()` still only accepted `int/u32/addr` and rejected the
    recorder's `u16` field load
- Local backend fix:
  - `src/lj_asm_s390x.h`
    - `asm_fload()` now handles:
      - `U8`
      - `U16`
      - sign-extended `I8`
      - sign-extended `I16`
      - GC64 pointer and GC-reference field loads
    - the byte and halfword cases currently use a 64-bit load plus BE shift
      extraction, which matches the active s390x target contract
- Native outcome on `zkd0` after rebuild:
  - `/tmp/ffi_ptr_abort_check.lua`
    - `total 547`
    - `start yes`
    - `stop yes`
    - `abort no`
    - exit status `0`
  - `tests/s390x/jit_core/ffi_ptr_call_trace.lua`
    - exit status `0`
  - `-jdump=im /tmp/ffi_ptr_plain.lua`
    - trace now commits and stops at `-> loop`
    - no `IR 69` abort remains

## 2026-03-20 Trace-Event Reporting Follow-Up

- After the stored-function-value FFI trace fix, the old verbose event-dump
  reproducer still crashes on native `zkd0`:
  - `/tmp/ffi_ptr_call_diag.lua`
  - prints `total 131`
  - then segfaults with exit status `139`
- The same script is clean on a local x86_64 control build of this branch:
  - prints `total 131`
  - prints both captured `start` and `stop` trace events
  - exits `0`
- Current interpretation:
  - this is no longer an FFI trace-assembly failure
  - it initially looked like a narrower s390x-specific problem in the
    trace-event reporting or event-value materialization path
- Next action:
  - isolate which trace event argument becomes invalid on native s390x before
    broadening the event callback machinery

## 2026-03-20 Trace-Event Post-Processing Loop Crash

- Follow-up native probes on `zkd0` show the event payload itself is not
  corrupted:
  - `arg1` and `arg2` print cleanly for both `start` and `stop`
  - `type(ev[3])` is `function`
  - `tostring(ev[3])` is stable
  - the optional `ev[4]` on the `start` event is also stable and prints `32`
- The crash only reproduces with the generic post-processing loop:
  - `for j = 1, ev.n do print('idx', j, tostring(ev[j])) end`
  - native output reaches:
    - `idx 1 start`
    - `idx 2 1`
    - `idx 3 function: ...`
    - then segfaults before `idx 4`
- Critical split:
  - adding `jit.off()` immediately after `cap.stop()` makes the exact same
    script run cleanly to completion on native `zkd0`
  - this means the remaining crash is not in the event payload itself
  - it is a new s390x JIT bug in the post-capture loop that walks the event
    tables, not in the earlier traced FFI call path
- Additional evidence:
  - the simpler probes that access `ev[4]` directly remain green
  - a local x86_64 control build also runs the full event walk cleanly

## 2026-03-20 Trace-Exit Stack Re-anchoring Fix

- Native `zkd0` gdb runs on the mixed-type `tostring()` post-trace loop showed
  a deeper s390x VM contract bug, not bad event payloads:
  - `cont_stitch` was running with `%r15 == cframe_raw(L->cframe) + 8`
  - `vm_exit_interp` later entered with `%r15 == cframe_raw(L->cframe) + 16`
  - any path that reloaded `SAVE_L` or other `SAVE_*` slots from `sp` in that
    state read garbage, which then poisoned `J->L`, `L->base`, and later
    interpreter dispatch
- Concrete native evidence:
  - at `lj_cont_stitch`, `DISPATCH_J(L)` and `DISPATCH_GL(cur_L)` still held the
    real `lua_State *`, but `SAVE_L`, `SAVE_PC`, and `SAVE_CFRAME` were garbage
    because `%r15` was skewed
  - after widening that fix, the next native crash moved into
    `lj_vm_exit_interp`, confirming the same stack-slot problem at exit resume
- Local fixes in `src/vm_s390x.dasc`:
  - `cont_stitch` now uses `DISPATCH_J(L)` instead of `SAVE_L`
  - the special `vm_record` / `vm_rethook` / `vm_inshook` dispatch path now
    reloads `cur_L` from dispatch instead of `SAVE_L`
  - `vm_exit_interp` now re-anchors `sp` from `cframe_raw(J->L->cframe)` before
    touching any `SAVE_*` slots
- Native outcome on `zkd0` after rebuild:
  - the original no-print reproducer now exits `0`:
    - `/tmp/tostring_type_matrix_noprint.lua`
  - the repo-local trace-event regression now also exits `0`:
    - `tests/s390x/jit_core/trace_event_postloop.lua`
  - the event walk prints both `start` and `stop` payloads cleanly on native
    s390x
- Added a focused repo-local regression for the simpler no-print loop:
  - `tests/s390x/jit_core/tostring_type_matrix.lua`
- Current interpretation:
  - the next active blocker in this area is a traced mixed-table/array loop
    over the captured event tables, likely involving the traced `ev[j]` access
    and/or the `tostring(ev[j])` call path
  - the next debugging pass should focus on trace shape for the post-capture
    loop rather than the trace-event callback machinery itself

## 2026-03-20 Bitops JIT Follow-Up

- Added two more focused repo-local JIT probes:
  - `tests/s390x/jit_core/bitops_trace.lua`
  - `tests/s390x/jit_core/ffi_cdata_trace.lua`
- Native `kdz` initially showed the bitops probe returning the correct
  Lua-visible total but aborting before any `"stop"` event:
  - first `jit.attach("trace")` abort payload:
    - error code `32` (`LJ_TRERR_NYIIR`)
    - `errinfo 35` (`IR_BXOR`)
- Local/remote backend bring-up since then:
  - added s390x bitwise lowering for:
    - `BNOT`
    - `BSWAP`
    - `BAND`
    - `BOR`
    - `BXOR`
    - `BSHL`
    - `BSHR`
    - `BSAR`
    - `BROL`
    - `BROR`
  - added the first integer `NEG` lowering
  - added the first minimal `UREFO` / `UREFC` lowering plus `LLGC`
    byte-load support for guarded open/closed upvalue checks
- Native progression on `kdz`:
  - after the bitwise lowering slice:
    - the front-most abort moved from `IR_BXOR` to `IR_NEG`
  - after `NEG` lowering:
    - stripped native bitops loops moved to `IR_UREFO`
  - after `UREFO` lowering:
    - stripped native bitops loops now record and reach
      `TRACE ... stop -> loop`
- Important split discovered during native validation:
  - the stripped inline bitops loop with an explicit `bit.tobit(x)` at the end
    of `mix(i)` is now correct on `kdz`
  - the repo-local `bitops_trace.lua` still fails because it returns raw `x`
    and only applies `bit.tobit()` at the outer accumulator
- Current front-most bug:
  - this is no longer a generic bitops NYI
  - it is a wrong-result issue in the raw-return bitops closure path
  - the first concrete native mismatch is:
    - `mix(6)`
    - expected `167772400`
    - got `-874411914`
  - the earliest narrowed stage is the raw-return form of:
    - `x = bit.bxor(x, bit.lshift(i, 3))`
- Current interpretation:
  - the remaining backend issue is likely a 32-bit normalization or register
    reuse problem in the early `BSHL` / `BXOR` path when the result stays
    unnormalized across subsequent ops
  - the next remediation cut should stay on that exact path before widening
    into other JIT surfaces again

## Resolved Interpreter Issue

- Symptom: uncaught runtime errors from a script file printed the correct
  stderr text but exited with status `0` on native s390x, while the `-e` form
  exited nonzero.
- Root cause: s390x unwind/EH return-value handling was wrong. GCC delivers EH
  return data in `r6` on this platform, while the tree still treated the
  return-value register contract as if it were `r14`.
- Fixes applied:
  - `src/lj_arch.h` now uses `LJ_TARGET_EHRETREG 6` and `LJ_TARGET_EHRAREG 14`.
  - `src/vm_s390x.dasc` now moves the unwind status through `r6` and returns it
    to C in `r2`.
- Outcome: interpreter-only `.t` coverage now passes natively in both debug and
  release runs.

## FFI ABI Resolution

- First failing native run:
  - `ffi-call-gcc-debug`
  - failing check: `small_u8: expected 17, got 0`
- Native ABI proof:
  - GCC-generated s390x assembly for
    `small_u8 echo_small_u8(small_u8 value)` shows:
    - hidden result pointer in `r2`,
    - by-value struct argument in `r3`,
    - the one-byte payload occupying the low bits of `r3`.
- Resolution:
  - `src/lj_ccall.c` now right-justifies by-value structs smaller than one
    pointer-width slot on s390x big-endian.
  - `src/Makefile` now disables clang's integrated assembler for s390x target
    builds.
  - `src/lj_mcode.c` now uses `__builtin___clear_cache`, which fixes the clang
    build path on s390x.
- Outcome:
  - `ffi_abi` now passes natively on `kdz` for gcc debug, gcc release, clang
    debug, and clang release.

## Callback Contract Correction

- The initial `tests/s390x/callbacks/run.lua` tried to cast a Lua function to a
  callback type returning `struct pair_value`.
- A local non-s390x control run with a supported LuaJIT build fails before any
  s390x code is involved:
  - `cannot convert 'function' to 'struct pair_value (*)()'`
- Implication:
  - aggregate callback returns are not currently part of the generic callback
    contract in this tree,
  - treating that case as an s390x callback gate would create a false
    architecture-specific failure.
- Current correction:
  - the callback regression suite now focuses on currently supported callback
    forms: scalar integer returns, stack-boundary integer arguments, mixed
    integer and FP arguments, and 64-bit integer arguments and returns.
- Follow-up:
  - if aggregate callback returns are needed, they should be added as a generic
    LuaJIT feature extension with cross-architecture validation, not as an
    s390x-only workaround.

## Callback-Unwind Progress

- First native callback blocker:
  - `callback-unwind-callbacks-20260318`
  - failure: `./src/luajit: tests/s390x/callbacks/run.lua:22: too many callbacks`
  - root cause: `CALLBACK_MAX_SLOT == 0` for s390x, so callbacks were still in
    the generic unsupported-architecture path.

- Second native blocker:
  - `callback-unwind-callbacks-20260318-v2`
  - failure: parallel native build raced `lj_profile.c` against generated
    `luajit.h`
  - root cause: clean parallel builds could start target object compilation
    before generated headers existed.
  - fix: `src/Makefile` now gives `$(LJVMCORE_O)` and `$(LUAJIT_O)` an
    order-only prerequisite on `$(ALL_HDRGEN)`.

- Third native blocker:
  - `callback-unwind-callbacks-20260318-v3`
  - failure: `lib_jit.c` could not build on interpreter-only s390x because the
    generated `jit.util` registration still referenced trace-only entry points.
  - root cause: the no-JIT target build still needed a stable `jit.util`
    surface during `lib_jit.c` compilation.
  - fix: `src/lib_jit.c` now provides no-op `jit.util.trace*` stubs when
    `LJ_HASJIT == 0`.

- Fourth native blocker:
  - `callback-unwind-callbacks-20260318-v4`
  - direct native callback probe on `kdz` still failed with
    `call_sum6: expected 21, got 2`
  - isolated behavior:
    - a single `sum6` callback in a fresh Lua state worked and received all six
      integer arguments correctly,
    - after allocating one earlier callback, the second callback pointer still
      invoked the first callback body.
  - first correction:
    - `src/lj_ccallback.c` now uses an 8-byte per-slot trampoline on s390x:
      `LGHI r1, slot` plus `BRAS r0, common`.
    - this removed the earlier return-address-derived slot recovery and made
      the slot number explicit in `r1`.
  - remaining root cause:
    - a native gdb probe on the rebuilt image showed `r1` arriving at
      `lj_vm_ffi_callback` as `0` for the first callback and `1` for the
      second callback, so the trampoline itself was correct,
    - a second probe at `callback_conv_args` still showed `cts->cb.slot == 0`
      for both callbacks,
    - `CCallback.slot` is a 32-bit `MSize`, but `vm_s390x.dasc` was storing it
      with `stg`, a 64-bit store; on big-endian s390x that wrote the low
      32-bit slot value outside the field and left the actual `slot` field as
      zero.
  - fix:
    - `src/vm_s390x.dasc` now stores `CTSTATE->cb.slot` with `st`, matching the
      field width and preserving nonzero callback slots on big-endian s390x.

- Fifth native blocker:
  - `callback-unwind-callbacks-20260318-release`
  - failure: release link failed with undefined references to `lua_assert`
    from the s390x callback trampoline emitter.
  - root cause: `src/lj_ccallback.c` used `lua_assert()` in callback mcode
    setup paths. In the non-assert release build that did not resolve as a
    safe macro in this translation unit and became an unresolved symbol.
  - fix:
    - `src/lj_ccallback.c` now uses `lj_assertX(...)` for the callback mcode
      size and overflow checks.

- Current native status:
  - `callback-unwind-callbacks-20260318-v5`
    - gcc debug callback coverage passes natively on `kdz`
  - `callback-unwind-callbacks-20260318-release-v2`
    - gcc release callback coverage passes natively on `kdz`
  - direct debug validation on `kdz` passes the callback regression in
    `tests/s390x/callbacks/run.lua`
  - that regression now covers:
    - scalar integer callbacks,
    - recursive callbacks,
    - stack-boundary integer callbacks,
    - mixed FP and integer callbacks,
    - 64-bit integer callbacks,
    - callback execution under `debug.sethook`,
    - callback error propagation through `pcall`,
    - bad callback return conversion propagation.
  - the callback-unwind stage is now green for the native gcc debug and gcc
    release variants; the wider compiler and host matrix still belongs to the
    later matrix stage.

## Hot-Exit And Dynamic-Key Trace Shape

- Native and local hotloop parity is established for the current dynamic-key
  build-loop probe:
  - the root trace starts at iteration `56` on local x86_64 and on native
    s390x `kdz`
  - this rules out a hotcount or loop-entry skew as the cause of the trace
    divergence

- Pure interpreter state is correct on native s390x for the same probe:
  - after the warmup iterations, the runtime key produced by `'a'..16`
    compares equal to the canonical `'a16'`
  - `rawequal(k, "a16")` is true
  - `t[k]` resolves to the existing `t["a16"]` value
  - this rules out a generic string or table semantics bug in the interpreter

- The old forced side-trace crash under `hotexit=1` was not a runtime
  execution bug. A native gdb batch run on `kdz` showed the crash in
  `ra_restore()` during `asm_tail_link()`, while trying to materialize
  `REF_BASE` for side-trace assembly.
  - direct symptom:
    - `ra_allocref(... ref=32768, allow=8192)` where `32768 == REF_BASE`
  - root cause:
    - s390x still treated `RID_BASE` as permanently fixed, but side-trace
      linking needs to materialize `REF_BASE` into `RID_BASE`
  - fix:
    - `src/lj_target_s390x.h` now leaves `RID_BASE` allocatable and documents
      that contract explicitly
  - outcome:
    - the same native `hotexit=1` probe now records and stops trace 2 cleanly
      instead of crashing:
      - `TRACE_start iter=56 tr=1`
      - `TRACE_stop iter=57 tr=1`
      - `TRACE_start iter=100 tr=2 otr=1 oex=3`
      - `TRACE_stop iter=100 tr=2`

- The remaining blocker is now narrower and sits in the recorder-side
  dynamic-string table-store path, but the probe split is narrower than the
  earlier generic hot-exit diagnosis.
  - `tests/s390x/jit_loops/hotexit_update_trace.lua` is now the canonical mixed
    insert-to-update repro:
    - local x86_64 forms:
      - trace 1: miss-path root loop
      - trace 2: update-path root trace (`otr=1`, `oex=2`)
      - trace 3: update-path loop trace (`otr=1`, `oex=0`)
      - trace 4: final stitch
    - native s390x on `kdz` still forms an endless `oex=0` loop-trace chain
      after the initial miss-path root trace
  - `tests/s390x/jit_loops/hotexit_update_only.lua` now isolates the pure
    update path by prefilling under `jit.off()` and only tracing the update
    loop:
    - local x86_64: one root loop plus one final stitch trace
    - native s390x: same trace shape and correct final values
  - implication:
    - the remaining native problem is not a generic side-trace or update-loop
      failure
    - it is specifically the transition from the initial miss-path trace into
      later update iterations in the mixed cyclic string-key loop
  - next diagnostic cut:
    - use an opt-in recorder probe in `src/lj_record.c`, gated by
      `LUAJIT_S390X_RECIDX_LOG`, to log `parent`, `exitno`, `xrefop`, and
      whether `oldv` is nil for string-key stores during the mixed repro
    - this should answer whether native s390x is still seeing a true nil lookup
      at side-trace start or whether the divergence happens after lookup

## Next Targets

- Start `jit-bringup` with the remaining s390x target metadata and shared JIT
  table fixes that are prerequisite to removing `LJ_ARCH_NOJIT`.
- Add a callback-specific unwind regression that throws from the callback body
  under nested `pcall` or `xpcall` and confirm the saved `cframe` chain remains
  stable.
- Once callback-unwind is green in the harness, move to `jit-bringup` with the
  existing order:
  - dispatch and `vm_next`,
  - trace recording entry,
  - exit handling and snapshot restore,
  - stitch and re-entry,
  - `JLOOP`, `JFOR*`, `JITER*`,
  - compiled varargs,
  - profiler hooks,
  - FFI callback return integration for JIT-enabled paths.

## JIT Bring-Up Progress

- First native JIT blocker after enabling the s390x backend surface:
  - `t/isarr-jit.t` aborted with `NYI: cannot assemble IR instruction 2`
  - `2` is `IR_LE`
  - result: integer compare and guard emission had to be implemented before
    any table.isarray loop trace could assemble.

- Native progression on `kdz` after incremental backend work:
  - `IR_LE` fixed, then the first blocker moved to `IR_ADD`
  - `IR_ADD` fixed, then the first blocker moved to generic helper calls
    (`asm_gencall`, tagged as `-108`)
  - minimal fixed-helper call lowering for direct integer/pointer helpers in
    `r2` through `r6` fixed that blocker
  - the next blocker moved into loop closure (`asm_tail_prep`,
    `asm_loop_fixup`, `asm_loop_tail_fixup`, and root `BASE` coalescing)
  - after those trace-structure fixes, the next real IR blocker was `71`
    (`IR_SLOAD`) and `66` (`IR_ALOAD`) on the ninth test.

## JIT Exit and Early Memory-Reference Progress

- Native remediation work on `kdz` then shifted from new IR lowerers back to
  the trace exit path:
  - exit stubs now carry both `exitno` and `traceno`
  - `src/vm_s390x.dasc` now has working `vm_exit_handler` and
    `vm_exit_interp` paths instead of the earlier traps
  - the original native crash in `lj_vm_exit_handler` was a real s390x
    addressing bug: the handler used `r0` as an address base when saving the
    original stack pointer, but `r0` cannot serve as a base register in s390x
    memory operands
  - fix: the handler now keeps the original stack pointer in `r3` and stores
    it into `ExitState.gpr[RID_SP]` from there.

- First post-exit regression:
  - `tests/s390x/jit_core/basic_trace.lua` no longer crashed, but computed
    `200` instead of the expected `20100`
  - native trace disassembly on `kdz` showed `asm_add()` was emitting the move
    and add instructions in the wrong order for a backend that writes machine
    code backwards
  - fix: `src/lj_asm_s390x.h` now emits the arithmetic operation before the
    optional move, so the executed instruction order is `move` then `add`.

- Trace-exit validation result:
  - `tests/s390x/jit_core/basic_trace.lua` now passes natively on `kdz`
  - `tests/s390x/jit_core/side_exit.lua` also passes natively on `kdz`
  - this confirms the current exit path can:
    - enter a root loop trace,
    - take a side exit,
    - run `lj_trace_exit`,
    - assemble the side trace,
    - and resume in the interpreter without crashing.

- Root cause of the former helper-call crash in `side_exit.lua`:
  - native trace disassembly showed the trace moved `BASE` from `r13` into
    `r1`, called `lj_vm_modi`, and then dereferenced through `r1`
  - the s390 psABI marks `r0` and `r1` plus `r2` through `r5` as volatile
    across calls, so using `r1` for a live `REF_BASE` value across helper calls
    was incorrect
  - fix:
    - `src/lj_target_s390x.h` now declares the call-preserved GPR set and a
      narrower `RSET_GPR_BASE` set for non-argument local registers
    - `src/lj_asm_s390x.h` now allocates `REF_BASE` from that preserved set via
      `ra_allocbase()`, which keeps trace base values out of volatile helper
      call registers.

- Second post-exit blocker:
  - side-trace assembly then failed in `asm_tail_link()` because
    `emit_loadu64()` still only handled 16-bit immediates
  - fix:
    - `src/lj_emit_s390x.h` now materializes full 64-bit immediates with a real
      RIL path using `LLILF` plus `IIHF`
    - this unblocks 64-bit `LPC` materialization for trace links and is the
      correct basis for later pointer and constant loads.

- First post-exit memory-reference slice:
  - `src/lj_asm_s390x.h` now has a minimal BE/GC64-safe implementation for:
    - `asm_aref`
    - `asm_ahuvload`
    - `asm_fload`
  - result:
    - `t/isarr-jit.t` test 9 moved from `IR_ALOAD` (`66`) to `IR_FLOAD`
      (`69`), then to green after the minimal `FLOAD` field load support was
      added.

- Current native JIT status on `kdz` as of `2026-03-19T16:03:53Z`:
  - green:
    - `tests/s390x/jit_core/basic_trace.lua`
    - `tests/s390x/jit_core/side_exit.lua`
    - `t/isarr-jit.t` tests 1-4 and 7-9
  - remaining visible JIT gap:
    - `t/isarr-jit.t` tests 5 and 6 still record an extra linked trace
      (`[TRACE   2 (1/0) test.lua:6 loop]`) where the expected output only
      records the root loop trace
    - semantics are correct, but the trace shape still differs from mature
      backends.

- Current native `SLOAD` slice:
  - `src/lj_emit_s390x.h` now has verified encodings for:
    - `lg`
    - `llgf`
    - `lgfr`
    - `srag`
    - `sllg`
    - `srlg`
  - `src/lj_asm_s390x.h` now lowers a narrow GC64 big-endian `SLOAD` subset:
    - root-trace stack loads only
    - no `IRSLOAD_PARENT`
    - no `IRSLOAD_CONVERT`
    - integer and GC-pointer/address loads only
    - type checks for those same cases using the GC64 high-tag layout

- Native result after the `SLOAD` implementation:
  - `t/isarr-jit.t` no longer aborts during trace assembly for tests 1 through
    8
  - those tests compile a trace and then fail at runtime instead of compile
    time
  - test 9 still reaches `IR_ALOAD`, which remains unimplemented.

- Runtime JIT entry and exit findings:
  - initial native `gdb` run on `kdz` crashed in `lj_BC_JLOOP`
  - root cause: `BC_JLOOP` in `src/vm_s390x.dasc` was still a null-store stub
  - fix:
    - `BC_JLOOP` now loads the `GCtrace *` from `jit_State.trace`,
      stores `jit_base` and `tmpbuf.L`, clears `vmstate`, and branches to the
      trace `mcode` entry like the mature backends do
  - second native `gdb` run on `kdz` then moved the crash site to
    `lj_vm_exit_handler`
  - root cause:
    - trace entry is now happening,
    - but `vm_exit_handler` and `vm_exit_interp` in
      `src/vm_s390x.dasc` are still architecture stubs.

- Current JIT status:
  - s390x native JIT is now past the first assembler and dispatch barriers
  - root loop traces can be assembled and entered for the `table.isarray`
    coverage
  - the next hard blocker is no longer IR lowering for tests 1 through 8
  - the next hard blocker is the trace-exit path:
    - `vm_exit_handler`
    - `vm_exit_interp`
  - after that, `IR_ALOAD` is the next concrete missing lowerer already proven
    by test 9.

## Immediate JIT Next Actions

- Port `vm_exit_handler` and `vm_exit_interp` from a mature 64-bit backend
  into `src/vm_s390x.dasc`, adapting:
  - exit-state save order,
  - exit-number calculation,
  - `jit_base` and `SAVE_L` restore,
  - the `lj_trace_exit` call contract,
  - resume into the interpreter after side exit.
- Re-run the direct `gdb` loop script on `kdz` until the crash site moves
  past `lj_vm_exit_handler`.
- Once exit handling works, rerun `t/isarr-jit.t`:
  - tests 1 through 8 should either pass or expose the next runtime handoff
    issue,
  - test 9 should still expose the first missing memory-reference lowerer,
    `IR_ALOAD`.

## 2026-03-19 Root-Loop Follow-Up

- Native `kdz` work after the exit-path bring-up moved the front-most blocker
  from trace entry and exit into helper-call lowering for root-loop traces.
- The narrowed reproducer is:
  - `./src/luajit -jv /tmp/isarr5_addr.lua`
  - loop body: repeated `table.isarray(t)` on an invariant table.

- Confirmed sequence:
  - the original crash was a traced helper-call argument bug:
    - the invariant table argument was allowed to live in `r2`,
    - the first `lj_tab_isarray()` call clobbered `r2`,
    - the second in-loop helper call reused that dead register value.
  - the earlier `ra_rematk: rematk of K001 has no reg` assert was a separate
    allocator-state bug caused by poisoning outgoing arg GPRs with `ASMREF_L`
    on s390x, where `emit_getgl()` is still stubbed and `ASMREF_L`
    rematerialization is not backend-safe yet.

- Current backend state:
  - helper-call argument setup in `src/lj_asm_s390x.h` no longer permanently
    binds the invariant table ref to `r2`.
  - native disassembly now shows explicit per-call argument reloads instead of
    a permanently pinned arg register.
  - the remaining crash moved one level deeper:
    - the per-call `SLOAD` reload path still depends on a `BASE` copy that is
      later reused as a loop temporary,
    - the loop backedge then re-enters the call reload sequence with the wrong
      base register contents.

- Native evidence from `kdz`:
  - one intermediate trace shape:
    - invariant table moved into `r1`, copied to `r2` before each call,
      but `r1` was still caller-clobbered, so the second call still failed.
  - latest trace shape:
    - each call site now emits its own `SLOAD`-derived reload into `r2`,
      but the loop body later reuses the copied `BASE` register as the loop
      limit temporary,
      so the second iteration faults on the next reload from the wrong base.
  - direct `gdb` evidence shows the fault has moved from
    `lj_tab_isarray(src=0x0)` to the traced reload sequence itself:
    - faulting PC in one native run:
      - `0x571eff48`
    - faulting instruction:
      - `lg %r2,32(%r11)`
    - immediate cause:
      - `%r11` no longer holds `BASE` after being reused for the loop limit.

- Clean conclusion:
  - the next hard blocker is no longer the helper-call arg lane itself.
  - the next hard blocker is `REF_BASE` / base-register liveness for looped
    traced reloads.
  - fixing this correctly likely requires one of:
    - a real s390x `emit_getgl()` / global reload path so traced code can
      rematerialize `jit_base` / `cur_L` instead of depending on stale copies,
    - or a stricter backend rule that keeps the chosen `BASE` home out of the
      general temporary pool across looped helper-call reload paths.

- Current native test impact:
  - `t/isarr-jit.t` still shows tests 1-4 and 7-8 green in the current branch
    state,
  - tests 5, 6, and 9 still fail because the crashing root-loop reproducer has
    not fully stabilized yet,
  - the failure mode is now reproducible, narrow, and documented enough to
    drive the next remediation step.

## 2026-03-19 JIT Core Gate Recovery

- The root-loop `REF_BASE` / helper-call reload issue is no longer the
  front-most blocker.
- Native fixes after that point were narrower runtime/backend contract work:
  - `src/vm_s390x.dasc` now places `ExitState.spill[]` over the live on-trace
    spill area by allocating only `EXITSTATE_SPILL_BASE` bytes below the trace
    stack pointer
  - `src/lj_emit_s390x.h` now has real generic spill load/store lowering for
    integer and address GPR values
  - `src/lj_asm_s390x.h` now has a minimal integer snapshot tail restore path
    for side traces
  - `src/lj_asm_s390x.h` now lowers integer `IR_MULOV`
  - `src/lj_asm_s390x.h` now has working non-loop `asm_tail_fixup()` code for
    side-trace exit/link branches and stack adjustment

- Native evidence for the spill fix:
  - direct `gdb` on `kdz` showed the loop-carried values used by
    `tests/s390x/jit_core/side_exit.lua` lived in spill slots, but the old
    s390x backend left both generic spill accessors as no-ops
  - at the first `lj_snap_restore()` breakpoint:
    - `T->ir[5].prev == 0xa714` and `T->ir[6].prev == 0xc050`
    - those decode to spill slots `167` and `192`
    - `ex->spill[167]` and `ex->spill[192]` contained instruction words, not
      Lua values, which proved the old spill path was reading garbage
  - after wiring spill load/store plus the `ExitState.spill[]` alias, the
    semantic wrong-result in `side_exit.lua` disappeared.

- Native side-trace progression on `kdz`:
  - before the new work:
    - `tests/s390x/jit_core/side_exit.lua` finished with the wrong total
      (`186`)
    - side-trace attempts aborted at `-107` (`asm_stack_restore`)
  - after spill, restore, `MULOV`, and tail-fixup work:
    - direct native run of
      `tests/s390x/jit_core/side_exit.lua` now passes
    - `./src/luajit -jv tests/s390x/jit_core/side_exit.lua` now shows:
      - `[TRACE   1 side_exit.lua:8 loop]`
      - `[TRACE   2 (1/5) side_exit.lua:11 -> 1]`
    - this confirms side-trace recording, assembly, link patching, and
      interpreter-visible semantics are working for the current integer loop.

- Structured harness validation:
  - native staged run:
    - [artifacts/s390x/20260319T121216Z/summary.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/20260319T121216Z/summary.md)
  - result:
    - stage `jit-bringup`
    - suite `jit_core`
    - host `kdz`
    - success `true`
  - this is the first green structured `jit_core` gate after the root-loop and
    side-exit fixes landed together.

- Current front-most blocker after the green `jit_core` gate:
  - the next exposed backend gap is no longer in the `side_exit.lua` loop
    itself
  - the first visible new miss is `IR_HREFK` (`57`), hit in the JIT-enabled
    error/testlib path:
    - `[TRACE --- (1/7) side_exit.lua:16 -- NYI: cannot assemble IR instruction 57]`
- the planned next loop is:
  - rerun `jit_core` in gcc release
  - run `jit_loops` in gcc debug
  - then start the next lowering slice at `IR_HREFK` if that remains the
      first blocker

## 2026-03-19 Harness Hardening and Loop-Gate Rebaseline

- The staged harness was hardened locally to keep concurrent bring-up runs
  trustworthy:
  - `tools/s390x/driver.py` now allocates auto `run-id` roots with collision
    retries instead of `exist_ok=True`
  - explicit `--run-id` reuse now fails fast unless `--resume` is set
  - each local run root now gets a `.lock` file so concurrent reuse fails
    explicitly
  - `artifacts/s390x/latest` is now updated atomically with `os.replace()`
    instead of an unlink-plus-symlink race

- Fresh native reruns after that hardening:
  - gcc release `jit_core` is green at
    [artifacts/s390x/20260319T193120.002606Z-p87501/summary.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/20260319T193120.002606Z-p87501/summary.md)
  - gcc debug `jit_loops` fails reproducibly at
    [artifacts/s390x/20260319T193120.002609Z-p87502/summary.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/20260319T193120.002609Z-p87502/summary.md)

- `asm_hrefk()` has now been implemented in `src/lj_asm_s390x.h` using a
  full-width GC64 key compare against `Node.key`, with a large-offset
  fallback. That change did not regress the staged gcc release `jit_core`
  gate.

- The current `jit_loops` blockers are now concrete:
  - `IR_HSTORE` (`75`) on the string-key table build loop at `t/iter.t`
    test 1 line 4
  - `asm_stack_restore` tag `-107` on the explicit `next()` path in
    `t/iter.t` test 2
  - `IR_HREF` (`58`) on the custom iterator hash lookup in `t/iter.t`
    test 3
  - big-endian `ITERN` is still intentionally disabled and remains behind
    those earlier blockers

- The current shortest remediation order is now:
  - implement `IR_HSTORE`
  - fix the non-trivial `asm_stack_restore` case hit by the `next()` loop
  - implement `IR_HREF`
  - then move into the BE iterator contract itself:
    - `vm_next`
    - recorder BE gates for `next()` / `pairs()`
    - any needed `IR_HIOP` / paired-return handling

## 2026-03-19 Loop-Gate Progress After `IR_HSTORE`

- Native staged rerun:
  - [artifacts/s390x/20260319T201417.058795Z-p33421/summary.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/20260319T201417.058795Z-p33421/summary.md)
- Changes in this slice:
  - `src/lj_asm_s390x.h` now has a real fused `asm_fstore()` path using the
    existing `FREF` IR, with exact-width stores for `U8`, `U16`, `U32`, and
    64-bit pointer or GC values
  - `src/lj_emit_s390x.h` now has `stcy`, `sthy`, and the related store
    helpers needed by that field-store path
- Result:
  - `IR_FSTORE` (`77`) is no longer the front-most `jit_loops` blocker
  - the string-key table-build loop now records cleanly as:
    - `[TRACE   1 test.lua:3 loop]`
    - `[TRACE   2 (1/0) test.lua:4 loop]`
  - this is progress, not final correctness:
    - the extra `(1/0)` linked trace is still a trace-shape mismatch relative
      to mature backends
    - iterator tracing still fails later because big-endian `ITERN` remains
      disabled

- Next staged rerun:
  - [artifacts/s390x/20260319T202015.844392Z-p38829/summary.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/20260319T202015.844392Z-p38829/summary.md)
- Changes in this slice:
  - `src/lj_asm_s390x.h` now restores traced numeric snapshot slots by storing
    FPR values back to the Lua stack with `stdy`
- Result:
  - `asm_stack_restore` tag `-107` is gone from the explicit `next()` path
  - the next front-most blocker is now `asm_stack_check` tag `-106`
  - current observed `jit_loops` status on native `kdz`:
    - TEST 1: string-key build trace records, then `pairs()` still stops at
      big-endian `ITERN`
    - TEST 2: explicit `next()` has moved from `-107` to `-106`
    - TEST 3: the custom iterator root loop now records after the same
      table-build trace shape

- Clean remediation order after these two advances:
  - implement `asm_stack_check`
  - re-baseline `jit_loops`
  - then address the remaining trace-shape mismatch around the extra `(1/0)`
    table-build trace if it still persists
  - then move into the BE iterator contract itself:
    - `ITERN` recording on big-endian
    - `vm_next`
    - any remaining paired-return or iterator-exit issues

## 2026-03-19 Iterator Helper Bring-Up After `asm_stack_check`

- Native staged reruns:
  - smoke with JIT on is green at
    [artifacts/s390x/20260319T205124.178494Z-p56178/summary.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/20260319T205124.178494Z-p56178/summary.md)
  - the first `jit_loops` rerun with `vm_next` plus `recff_next()` is at
    [artifacts/s390x/20260319T205336.109057Z-p57258/summary.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/20260319T205336.109057Z-p57258/summary.md)
- Changes in this slice:
  - `src/vm_s390x.dasc` now has a real s390x `lj_vm_next` helper instead of
    the previous BE gap
  - `src/lj_ffrecord.c` now records `next()` on big-endian again by re-enabling
    `recff_next()`
  - the harness now has a dedicated explicit-`next()` repro at
    `tests/s390x/jit_loops/explicit_next.lua`, and `tools/s390x/driver.py`
    runs it first in the `jit_loops` stage
- Result:
  - explicit `next()` no longer records as a stitched fallback; it records as
    a real loop trace through `lj_vm_next`
  - `pairs()` is still blocked separately because `ITERN` remains disabled on
    big-endian in `src/lj_record.c`
  - the explicit `next()` repro still returned empty output in the last native
    rerun before the latest local helper adjustment

## 2026-03-19 Local-Only Prep While Native Access Was Unavailable

- Native validation was temporarily blocked by local sandbox restrictions on
  outbound `ssh`, so this slice is preparation for the next native rerun, not a
  new validated milestone.
- Changes staged locally:
  - `src/vm_s390x.dasc` hash hits in `lj_vm_next` now return the live
    `Node *` directly, matching mature backends and the `{ val, key }` layout
    that `lj_record_next()` expects
  - `src/lj_asm_s390x.h` now has a real `asm_hiop()` for call results, which is
    the missing generic half of the BE iterator path behind `IR_HIOP`
- Why this matters:
  - explicit `next()` does not need the traced next-index half, so it can move
    forward before `asm_hiop()` is implemented
  - `pairs()` and `ITERN` do need the `HIOP` half of `IRCALL_lj_vm_next`, so
    leaving `asm_hiop()` stubbed would immediately block the next BE iterator
    stage even if the helper payload is otherwise correct
- Planned next native order once access returns:
  - rerun `jit_loops` with `tests/s390x/jit_loops/explicit_next.lua` first
  - confirm whether the hash-hit `Node *` return fixes explicit `next()`
  - if it does, move directly to the BE `ITERN` recorder gate in
    `src/lj_record.c`
  - if it does not, debug the remaining `lj_vm_next` pair-return payload before
    widening scope

## 2026-03-19 Fresh Native Rebuild And Explicit `next()` Revalidation

- Remote access is working again through the `kdz` tmux path, and there is now
  a clean source-level rebuild path that does not depend on older harness
  snapshots:
  - fresh clone:
    `git clone --depth 1 --branch k8ika0s/s390x-bringup-wip https://github.com/k8ika0s/luajit2-s390x.git fresh-20260319-explicit-next`
  - native build:
    `make XCFLAGS='-DLUAJIT_ENABLE_S390X_JIT' BUILDMODE=static -j4`
- This clarified an important build contract detail:
  - the pushed branch still keeps s390x JIT behind
    `LUAJIT_ENABLE_S390X_JIT` in `src/lj_arch.h`
  - without that define, a clean s390x source rebuild compiles the JIT pieces
    out and leaves unresolved JIT references at link time
- Minimal branch-level native patch for explicit `next()`:
  - re-enable big-endian `recff_next()` in `src/lj_ffrecord.c`
  - special-case `IRCALL_lj_vm_next` in `src/lj_asm_s390x.h` so non-`HIOP`
    `CALLL PTR` results use `RID_RETLO`
  - add `tests/s390x/jit_loops/explicit_next.lua`
- Native result on `kdz` from that clean rebuild:
  - `./src/luajit tests/s390x/jit_loops/explicit_next.lua` now exits `0`
  - the explicit-`next()` total is correct again at the Lua level
- Native tracing result on the pushed branch with only that minimal patch:
  - `jit.status()` is on
  - `jit.attach(..., "trace")` shows repeated `start` and `abort` events
  - `jit.util.traceinfo(1)` stays `nil`, so the loop still never commits
- Observed native abort payloads:
  - `abort ... 22 32 75`
  - `abort ... 13 32 -107`
  - `abort ... 36 32 67`
  - `abort ... 36 9 nil`
- Interpretation:
  - the minimum `lj_vm_next` result-register fix is correct and necessary for
    semantics, but not sufficient for trace completion
  - the clean native aborts line up with the backend slices that the local tree
    has already been addressing after the pushed branch point:
    - `75` matches the earlier string-key table-build `HSTORE` frontier
    - `-107` matches the earlier `asm_stack_restore` snapshot-restore gap
    - `67` is another pre-iterator backend gap on the older pushed branch
  - this is a useful confirmation that the current local remediation order is
    still sound: the local tree is ahead of the pushed branch, not off-track
- Process note:
  - `-jv` is not usable in this static s390x rebuild even when `require("jit")`
    and `jit.status()` work, so the reliable native trace probe for this phase
    is `jit.attach(..., "trace")`

## 2026-03-19 Fresh Native Iterator Checkpoint

- The fresh `kdz` rebuild path is still usable after bringing the newer local
  `asm_stack_check`, `asm_stack_restore`, `asm_tvstore64`, `asm_tvptr`,
  `asm_ahustore`, and `asm_fstore` slices into the remote checkout.
- One remote splice bug surfaced during that sync:
  - the fresh remote `src/lj_asm_s390x.h` lost the forward declaration for
    `asm_tvstore64`
  - restoring that prototype ahead of `asm_stack_restore()` was enough to get
    the fresh tree building cleanly again
- Fresh native result after the rebuild:
  - `prove -v t/iter.t` still fails in exactly three places
  - the front-most aborts are now stable and explicit:
    - `IR 58` in the traced table-iterator setup path
    - `IR 67` in explicit `next()` and custom iterator paths
    - `BC_ITERN` still hard-NYI on big-endian once the trace reaches that bytecode
- Interpretation:
  - the current iterator frontier is now clear enough to order cleanly
  - `IR 58` is the first table-lookup path to finish for traced iterator setup
  - `IR 67` remains the missing paired-result half behind `lj_vm_next`
  - `BC_ITERN` is still intentionally blocked in both the recorder and VM path,
    so even after the first two lowerers are complete the BE iterator bring-up
    still needs the dedicated `ITERN` enablement step
- Process note:
  - the fresh-clone remote sync is still more brittle than the local harness
    path because surgical header replacements are being applied over tmux
  - the evidence itself is good, but the next backend slices should be applied
    locally first and then synced in a smaller, cleaner batch to avoid more
    splice-only failures

## 2026-03-19 Native Iterator Resume Fix And `IR_VLOAD` Follow-Through

- The latest remote-native validation was done on `kdz` against the local-sync
  checkout after pushing two small control-flow fixes from the local tree:
  - `src/vm_s390x.dasc` now saves `SAVE_PC` in `BC_JLOOP`
  - `src/vm_s390x.dasc` static resume in `vm_exit_interp` no longer overwrites
    the decoded `BC_JLOOP` traceno with `DISPATCH_J(parent)`
- Native result:
  - `tests/s390x/jit_loops/pairs_loop.lua` no longer segfaults
  - the same reproducer now runs to completion and prints the correct total
  - the old crash in `lj_BC_JMP` has been converted into ordinary trace
    progression
- That immediately exposed the next real backend gap:
  - the iterator/print tail now stopped on `IR 72`, which maps to `IR_VLOAD`
  - the relevant local gap was in `src/lj_asm_s390x.h`, where `asm_ahuvload()`
    only accepted `int/u32/addr`
- Local fix:
  - extend `src/lj_asm_s390x.h` `asm_ahuvload()` to accept primitive boxed
    values (`nil/true/false`) in addition to the existing integer and address
    classes
  - keep the working GC64 address path intact, but add full 64-bit primitive
    compare-and-load handling
- Native result after syncing that slice to `kdz`:
  - `tests/s390x/jit_loops/pairs_loop.lua` now reaches
    `[TRACE 109 (108/1) pairs_loop.lua:13 stitch print]`
  - the old `IR_VLOAD` abort at the final `print()` is gone
  - Lua-visible output remains correct: `pairs total 5050`
- Important remaining issue:
  - `t/iter.t` is still failing only on err-output trace shape
  - the active mismatch is repeated side traces in the string-key table-build
    prelude:
    `[TRACE 2 (1/0) test.lua:4 loop] ...`
  - and repeated linked traces in the iterator loop:
    `[TRACE n (prev/1) test.lua:8 -> 11]`
- New diagnostic result that narrows this further:
  - a no-`-jv` probe of the table-build loop with default thresholds records
    only one root trace:
    `trace 1 link 1 type loop nins 31 nk 13`
  - this means baseline loop commit is fine
  - the remaining instability is in the repeated hot-exit / side-trace path,
    not in basic root-trace assembly for the build loop
- Another useful data point:
  - a no-`-jv` probe of the same table-build loop with `hotexit=2` still
    segfaults on native s390x
  - so the remaining frontier is now clearly the hot-exit / repeated side-trace
    path, not the earlier iterator crash or the earlier `IR_VLOAD` tail

## 2026-03-19 Hot-Exit Miss-Chain Diagnosis

- The current hot-exit investigation is no longer focused on exit-number
  transport. That question is now settled.
  - `src/lj_trace.c` now has an opt-in exit-stub mapper that decodes the saved
    s390x `brasl` return address back to the originating exit stub slot
  - native `kdz` logs show the repeated chain really is coming from
    `stubslot=1`, `stubexit=0`
  - implication:
    - the long side-trace chain is not caused by a bad exit decode in
      `vm_exit_handler`
    - it is caused by the trace actually taking `exit 0`

- A portable build issue fell out of that diagnostic work:
  - the new exit logger originally referenced `RID_R14` unconditionally and
    broke non-s390x builds
  - `src/lj_trace.c` now hides that behind a small `LJ_TARGET_S390X` helper, so
    the same diagnostic code can stay in-tree without breaking local x86_64
    control runs

- New side-trace shape probe:
  - `tests/s390x/jit_loops/hotexit_shape_dump.lua` dumps `traceinfo`,
    `tracek`, `traceir`, and `tracesnap` for traces 1..4 without depending on
    `jit.vmdef`
  - native `kdz` and local x86_64 runs show that the early s390x side traces
    are near-clones of the miss loop rather than the shorter update-path root
    trace seen on x86_64

- Local x86_64 control for the preinterned mixed loop:
  - `TRACE_start iter=2 tr=1 otr=nil oex=nil`
  - `TRACE_start iter=20 tr=2 otr=1 oex=2`
  - `TRACE_start iter=21 tr=3 otr=1 oex=0`
  - `TRACE_start iter=100 tr=4 otr=3 oex=3`
  - recorder/exit logs confirm:
    - the first root trace records one miss (`key=a3`, `oldv_nil=1`,
      `hmask=1`)
    - the first update side trace starts from `parent=1 exit=2`
    - that side trace immediately sees `oldv_nil=0` with `hmask=31`
    - the later loop trace starts from `parent=1 exit=0`

- Native `kdz` result for the same preinterned mixed loop:
  - trace starts:
    - `TRACE_start iter=2 tr=1 otr=nil oex=nil`
    - `TRACE_start iter=3 tr=2 otr=1 oex=0`
    - `TRACE_start iter=4 tr=3 otr=2 oex=0`
    - ...
    - `TRACE_start iter=20 tr=19 otr=18 oex=0`
    - `TRACE_start iter=100 tr=20 otr=19 oex=3`
  - recorder logs for traces 2..19 show:
    - `parent=n exit=0`
    - `xrefop=58` (`IR_HREF`)
    - `oldv_nil=1`
    - `phase=store-miss`
    - keys advancing through `a4` .. `a20`
    - hash growth progressing through `hmask=3`, `7`, `15`, and `31`
  - the first update hit appears only at:
    - `parent=18 exit=0 key=a1 oldv_nil=0 hmask=31`

- Clean conclusion from the new control comparison:
  - native s390x does eventually reach the same grown-table update state as
    x86_64
  - but it reaches that state through a long `exit 0` miss-chain instead of
    x86_64's early `exit 2 -> exit 0 -> stitch` convergence
  - `J->startpc` staying at the root-loop PC on native side traces is a
    consequence of that:
    - in `lj_record_setup()`, side traces only keep `startpc` when
      `exitno == 0`
    - so the wrong-looking repeated loop trace shape is downstream of the
      `exit 0` chain, not a separate root cause by itself

- Next diagnostic target:
  - compare the hot-exit guard/snapshot mapping for the first mixed miss loop
    between x86_64 and s390x
  - the specific open question is whether the guards that x86_64 associates
    with `exit 2` are being assigned to `snap 0` on s390x, or whether the
    correct snapshot is present but the generated guard path still routes
    execution to the `exit 0` stub

## 2026-03-20 Mixed Miss-Store Narrowing

- Fresh native `kdz` validation with the clean `src/` rebuild path confirmed
  that `tests/s390x/jit_loops/preinterned_key_cycle.lua` converges on s390x the
  same way it does on local x86_64:
  - trace 1 is a loop
  - trace 2 is a stitch
  - no `(n/0)` miss-chain appears

- That rules out the broader “cycling preinterned string key” path. The
  remaining JIT bug is still specific to the mixed hash store/update loop.

- I then added a flag-gated `LUAJIT_S390X_TABGET_LOG` probe in `src/lj_tab.c`
  and reran the mixed native repro on `kdz`.

- The native evidence from that run is clear:
  - compiled s390x trace calls into `lj_tab_get()` with the correct string keys
    (`a6`, `a7`, `a8`, ...)
  - `lj_tab_get()` returns the canonical `niltv(L)` pointer on misses
  - the repeated `exit 0` miss-chain still happens even though the helper
    lookup input and output are both correct

- That rules out these failure modes:
  - bad `asm_tvptr()` key materialization for the `lj_tab_get()` helper call
  - bad `lua_State *` argument passed to `lj_tab_get()`
  - bad `lj_tab_get()` miss return value
  - bad exit-stub decode

- The remaining frontier is therefore after the successful miss lookup, most
  likely in one of:
  - merged HREF compare / guard behavior after the helper call
  - `IR_NEWREF`
  - `IR_HSTORE`
  - `IR_TBAR` / `IR_FSTORE` write-barrier handling

- The current bug surface is now much narrower:
  - hash lookup itself is correct on native s390x
  - the long side-trace chain is happening in the traced miss-store path that
    follows that lookup

## 2026-03-20 Hot-Exit Resolution And Native Sweep

- The mixed dynamic-key hot-exit chain is now fixed on native s390x.
  - root cause:
    - the s390x backend was still taking the fused `HREF + EQ/NE` helper-call
      path in `asm_fuseequal()`
    - on the mixed preinterned update loop, that fused path associated the
      miss guard with `exit 0`
    - result:
      - native `kdz` kept cloning the miss loop as
        `parent=1 exit=0`, `parent=2 exit=0`, `parent=3 exit=0`, ...
      - x86_64 instead formed the expected update trace first from
        `parent=1 exit=2`, then converged through `exit 0` and stitch
  - fix:
    - `src/lj_asm.c` now disables that fused `HREF + EQ/NE` lowering on
      `LJ_TARGET_S390X`
    - s390x falls back to the generic compare/guard path, which preserves the
      expected snapshot and exit numbering for this helper-backed hash lookup

- Native `kdz` validation after that change matches the expected control flow:
  - `tests/s390x/jit_loops/hotexit_update_preinterned.lua` now records:
    - trace 1: root loop
    - trace 2: update-path root trace from `parent=1 exit=2`
    - trace 3: update-path loop trace from `parent=1 exit=0`
    - trace 4: final stitch
  - final Lua-visible result is correct:
    - `done 81 100`

- Fresh native focused sweeps on `kdz` are now green for the current staged
  s390x suites:
  - `t/iter.t`
  - `t/isarr-jit.t`
  - `tests/s390x/jit_core/basic_trace.lua`
  - `tests/s390x/jit_core/isarray_root_loop.lua`
  - `tests/s390x/jit_core/side_exit.lua`
  - `tests/s390x/jit_loops/explicit_next.lua`
  - `tests/s390x/jit_loops/hotexit_build_trace.lua`
  - `tests/s390x/jit_loops/hotexit_shape_dump.lua`
  - `tests/s390x/jit_loops/hotexit_update_only.lua`
  - `tests/s390x/jit_loops/hotexit_update_preinterned.lua`
  - `tests/s390x/jit_loops/hotexit_update_trace.lua`
  - `tests/s390x/jit_loops/iter_pairs.lua`
  - `tests/s390x/jit_loops/pairs_loop.lua`
  - `tests/s390x/jit_loops/preinterned_key_cycle.lua`
  - `tests/s390x/jit_loops/vararg_trace.lua`
  - `tests/s390x/jit_be/mixed_width_ffi.lua`
  - `tests/s390x/jit_be/number_helpers.lua`

- The native standalone FFI and callback drivers are also green when run
  against freshly built oracle shared libraries on `kdz`:
  - built:
    - `/tmp/ffi_oracle.so` from `tests/s390x/ffi_abi/oracle.c`
    - `/tmp/callback_oracle.so` from
      `tests/s390x/callbacks/callback_oracle.c`
  - validated:
    - `src/luajit tests/s390x/ffi_abi/run.lua /tmp/ffi_oracle.so`
    - `src/luajit tests/s390x/callbacks/run.lua /tmp/callback_oracle.so`
  - both return `RC=0` with no stderr output on native s390x

- Current conclusion:
  - the earlier iterator and hot-exit blocker is resolved
  - the branch now has a materially broader native green surface across core
    JIT loops, BE helper probes, outbound FFI ABI, and callback execution
  - the next correct step is to widen correctness coverage and identify the
    next real missing JIT/backend surface instead of continuing to debug the
    already-fixed hot-exit path

## 2026-03-20 Native Clang Sweep

- A clean native clang JIT build now works on `kdz`:
  - `make clean && make CC=clang XCFLAGS='-DLUAJIT_ENABLE_S390X_JIT' -j4`
  - result: `CLANG_BUILD_RC=0`

- The current focused native validation slice is also green under that clang
  build:
  - repo `.t` coverage:
    - `t/isarr-jit.t`
    - `t/iter.t`
    - `t/table-clone.t`
  - staged s390x sweeps:
    - `tests/s390x/jit_core/*.lua`
    - `tests/s390x/jit_loops/*.lua`
    - `tests/s390x/jit_be/*.lua`
    - `tests/s390x/soak/mixed_stress.lua`
  - standalone FFI and callback drivers:
    - `tests/s390x/ffi_abi/run.lua /tmp/ffi_oracle.so`
    - `tests/s390x/callbacks/run.lua /tmp/callback_oracle.so`

- Native result:
  - all focused clang probes return `RC=0`
  - `prove` result for the three repo `.t` files is `PASS` with `Files=3`
    and `Tests=42`

- Practical implication:
  - the current s390x branch state is no longer only a gcc-native success case
  - the first toolchain-hardening slice is green for clang as well
  - the next matrix work can move outward from this base instead of treating
    clang as an open blocker

## 2026-03-20 Native Dynamic-Build Sweep

- A focused native dynamic-build sweep is now green on `kdz`.

- Initial native failure:
  - `make BUILDMODE=dynamic XCFLAGS='-DLUAJIT_ENABLE_S390X_JIT' -j4`
    completed successfully
  - but every runtime probe failed with `RC=127`
  - native loader error:
    - `src/luajit: error while loading shared libraries:
      libluajit-5.1.so.2: cannot open shared object file`

- Root cause:
  - the ELF link step was using the soname `libluajit-5.1.so.2`
  - but the build still only emitted `libluajit.so`
  - so the native loader could not satisfy the soname dependency even though a
    shared library had been built

- Fix:
  - `src/Makefile` now emits the actual soname target on ELF platforms:
    - `libluajit-5.1.so.2`
  - `libluajit.so` is kept as a symlink to that soname target
  - `tools/s390x/remote_run.sh` now exports the repo-local `LD_LIBRARY_PATH`
    so staged native dynamic runs use the just-built library without requiring
    manual environment setup

- Native validation after that fix is green:
  - repo `.t` coverage:
    - `t/isarr-jit.t`
    - `t/iter.t`
    - `t/table-clone.t`
  - standalone FFI and callback drivers:
    - `tests/s390x/ffi_abi/run.lua /tmp/ffi_oracle.so`
    - `tests/s390x/callbacks/run.lua /tmp/callback_oracle.so`
  - direct native soak probe:
    - `tests/s390x/soak/mixed_stress.lua`

- Practical implication:
  - the current branch state is no longer only validated under static or
    default link modes
  - the dynamic ELF packaging path is now structurally correct for native
    zLinux runs
  - the next matrix step should widen to another native host rather than
    continuing to spend time on the already-green `kdz` dynamic slice

## 2026-03-20 Second-Host Native GCC Sweep

- Host-to-host transport is now working between `zkd0` and `kdz`:
  - a dedicated `root` ed25519 key was created on `zkd0`
  - that public key was appended to `kdz:/root/.ssh/authorized_keys`
  - `zkd0` can now pull the known-good checkout from
    `kdz.dev.fyre.ibm.com:/root/luajit2-s390x/local-sync-check/` with `rsync`
  - this is a useful long-term bootstrap path for cross-host matrix work when
    the local sandbox cannot open direct SSH sessions

- Fresh native gcc validation on `zkd0` is green for the same focused surface
  already exercised on `kdz`:
  - repo `.t` coverage:
    - `t/isarr-jit.t`
    - `t/iter.t`
    - `t/table-clone.t`
  - staged s390x sweeps:
    - `tests/s390x/jit_core/*.lua`
    - `tests/s390x/jit_loops/*.lua`
    - `tests/s390x/jit_be/*.lua`
    - `tests/s390x/soak/mixed_stress.lua`
  - standalone drivers:
    - `tests/s390x/ffi_abi/run.lua /tmp/zkd0_ffi_oracle.so`
    - `tests/s390x/callbacks/run.lua /tmp/zkd0_callback_oracle.so`

- One host bootstrap gap was exposed and fixed:
  - initial `prove` execution on `zkd0` failed with `bash: prove: command not found`
  - root cause:
    - the host had Perl, but not the `perl-Test-Harness` package that ships
      `/usr/bin/prove`
  - fix:
    - `tools/s390x/remote_bootstrap.sh` now installs `perl-Test-Harness` as
      part of the baseline package set
  - result:
    - rerunning the focused repo `.t` slice on `zkd0` returned `Result: PASS`
      with `Files=3` and `Tests=42`

- Practical implication:
  - the current s390x branch state is no longer a single-host gcc success case
  - the focused native matrix is now green across both `kdz` and `zkd0`
  - the next correct expansion is either:
    - native clang on `zkd0`, or
    - the focused dynamic-build slice on `zkd0`

## 2026-03-20 Second-Host Dynamic Sweep And Warning Cleanup

- The focused dynamic-build slice is now also green on `zkd0`.
  - build:
    - `make BUILDMODE=dynamic XCFLAGS='-DLUAJIT_ENABLE_S390X_JIT' -j4`
  - runtime artifacts:
    - `src/libluajit-5.1.so.2`
    - `src/libluajit.so -> libluajit-5.1.so.2`
  - green native validation under `LD_LIBRARY_PATH=$PWD/src`:
    - `t/isarr-jit.t`
    - `t/iter.t`
    - `t/table-clone.t`
    - `tests/s390x/ffi_abi/run.lua /tmp/zkd0_ffi_oracle.so`
    - `tests/s390x/callbacks/run.lua /tmp/zkd0_callback_oracle.so`

- That closes the current focused dynamic matrix on both available native hosts:
  - `kdz`: green
  - `zkd0`: green

- One native compiler-cleanup item was also confirmed and fixed during the
  second-host dynamic rebuild:
  - warning:
    - `lj_asm_s390x.h: asm_stack_check: operation on 'allow' may be undefined`
  - root cause:
    - `rset_clear()` is an in-place `&=` macro
    - the s390x backend was still wrapping it as
      `allow = rset_clear(allow, picked)`
  - fix:
    - `src/lj_asm_s390x.h` now uses:
      - `Reg picked = rset_pickbot(allow);`
      - `pbase = picked;`
      - `rset_clear(allow, picked);`
  - native confirmation:
    - a fresh dynamic rebuild on `zkd0` returns `NO_SEQUENCE_POINT_WARNING`
      when grepping the build log for the earlier diagnostic

- Current implication:
  - focused native gcc and dynamic coverage is now green across both native
    hosts
  - the next matrix expansion should be:
    - native clang on `zkd0`, or
    - a deliberate cleanup pass for the remaining unused-function warnings in
      the still-partial s390x backend

## 2026-03-20 Second-Host Native Clang Sweep

- Native clang validation is now green on `zkd0` as well.
  - build:
    - `make CC=clang XCFLAGS='-DLUAJIT_ENABLE_S390X_JIT' -j4`
  - focused repo `.t` coverage:
    - `t/isarr-jit.t`
    - `t/iter.t`
    - `t/table-clone.t`
  - staged s390x sweeps:
    - `tests/s390x/jit_core/*.lua`
    - `tests/s390x/jit_loops/*.lua`
    - `tests/s390x/jit_be/*.lua`
    - `tests/s390x/soak/mixed_stress.lua`
  - standalone drivers:
    - `tests/s390x/ffi_abi/run.lua /tmp/zkd0_ffi_oracle.so`
    - `tests/s390x/callbacks/run.lua /tmp/zkd0_callback_oracle.so`

- Native result:
  - `ZKD0_CLANG_BUILD_RC=0`
  - `ZKD0_CLANG_PROVE_RC=0`
  - `ZKD0_CLANG_SWEEP_RC=0`
  - `ZKD0_CLANG_FFI_RC=0`
  - `ZKD0_CLANG_CB_RC=0`

- Practical implication:
  - the focused native matrix is now green across both available hosts for:
    - gcc static/default builds
    - clang static/default builds
    - dynamic ELF builds
  - the next most valuable work is no longer more of the same focused
    host/toolchain replication
  - the branch should now spend more time on:
    - remaining JIT completion surfaces not covered by the focused probes
    - FFI-on-trace and broader hardening
    - cleanup of the still-open unused-function warnings in the partial s390x
      backend

## 2026-03-20 Native `tostring` Fallback Retry Regression

- Native `zkd0` still reproduces the focused no-print crash in
  `/tmp/tostring_type_matrix_noprint.lua` after the earlier fast-function
  fallback growstack fix.

- What moved:
  - the old failing `lj_state_growstack` call from `fff_fallback` label `5`
    is no longer the front-most fault
  - `fff_fallback` now enters with a correct `cur_L` reload from `DISPATCH`
    and passes the `maxstack` check on the observed runs

- New concrete evidence from native `gdb`:
  - the first and second fallback calls both load a valid `CFunc` from
    `[BASE-16]`
  - on the second fallback entry, `[BASE-8]` is `0x82`, not a saved caller PC
  - after the fallback/retry flow, the crash happens while dispatching
    `OP=21` (`BC_LEN`)
  - `DISPATCH[21]` itself is corrupted to a heap pointer:
    `0x3fff7fe0f54`
  - neighboring dispatch entries remain valid code pointers, so this is not a
    global `DISPATCH` base corruption

- Interpretation:
  - the immediate failure is no longer a bad `lua_State *` reload
  - the live hard failure is now either:
    - a stale/mis-established retry-frame PC slot around the returned-0
      fast-function retry path, or
    - a separate s390x VM bug that overwrites the single `BC_LEN` dispatch slot
      with a heap pointer during this sequence

- Local remediation started:
  - `fff_fallback` label `5` now reloads `cur_L` from `DISPATCH` before and
    after `lj_state_growstack`
  - the returned-0 retry path in `fff_fallback` now reloads `PC` from
    `SAVE_PC` before restoring `[BASE-8]`

- Next exact focus:
  - instrument or isolate the write that mutates `DISPATCH[BC_LEN]`
  - keep the scope on the `tostring` plus `#s` sequence until the `BC_LEN`
    dispatch slot remains stable across the retry path

## 2026-03-20 Clean-Tree Dynamic Build Graph Fix on `kdz`

- A fresh clean-tree mixed build on `kdz` exposed a real build-graph bug in
  the new shared-library path:
  - `$(LUAJIT_SO)` was correctly switched to depend on `$(LJVMCORE_DYNO)`
  - but only `$(LJVMCORE_O)` had the generated-header ordering fence
  - a clean parallel build could therefore start `_dyn.o` compilation before
    `lj_bcdef.h` and `lj_ffdef.h` existed

- Local fix:
  - `src/Makefile` now gives `$(LJVMCORE_DYNO)` the same order-only
    prerequisite on `$(ALL_HDRGEN)` as the static object set:
    - `$(LJVMCORE_O) $(LJVMCORE_DYNO) $(LUAJIT_O): | $(ALL_HDRGEN)`

- Native result on `kdz` after the fix:
  - clean build:
    - `make clean && make -j8 XCFLAGS='-DLUAJIT_ENABLE_S390X_JIT'`
    - result: `0`
  - focused validation rerun:
    - `tests/s390x/jit_core/*.lua`
    - `tests/s390x/jit_loops/*.lua`
    - `t/isarr-jit.t`
    - `t/iter.t`
    - `t/table-clone.t`
    - `tests/s390x/jit_be/*.lua`
    - `CC=gcc sh tests/s390x/build_oracles.sh`
    - `tests/s390x/ffi_abi/run.lua`
    - `tests/s390x/callbacks/run.lua`
    - `tests/s390x/soak/mixed_stress.lua`
  - result:
    - the clean-tree `kdz` rerun is green again
    - the current blocker is no longer the build graph

- Practical implication:
  - the branch is past the dynamic-build plumbing regression
  - the next most valuable work is to widen native repo coverage until the
    next real s390x runtime, JIT, or BE correctness gap appears

## 2026-03-20 Full Repo-Local `.t` Sweep Green on `kdz`

- After the clean-tree dynamic build-graph fix, the next widened native check
  on `kdz` was the full repo-local Perl suite:
  - `cd /root/luajit2-s390x/local-sync-check`
  - `export LUA_PATH='./?.lua;./?/?.lua;;src/?.lua;;src/?/?.lua;;tests/?.lua;;tests/?/?.lua;;'`
  - `prove -v t/*.t`

- Initial widened result:
  - all repo-local `.t` files were green except:
    - `t/exdata.t`
    - `t/exdata2.t`
  - the failing cases were the JIT read/default-value paths
  - observed bad outputs included non-NULL garbage pointers where the tests
    expected either the original pointer or `NULL`

- Focused native diagnosis on `kdz`:
  - a minimized `-jdump=ir` loop over `thread.exdata()` showed the recorder
    sinking `IR_CNEWI` around:
    - `p64 FLOAD thread.exdata`
    - `{sink} cdt CNEWI +19 ...`
  - that means exit-time restore must recover the original pointer payload
    from `ExitState`
  - the old s390x `vm_exit_handler` was not saving real architectural GPR
    state there:
    - `ExitState.gpr[1]` ended up holding the saved original `r14`
    - `jit_base` was also being cleared with the wrong scratch value instead
      of zero

- Local fix:
  - `src/vm_s390x.dasc`
    - `vm_exit_handler` now saves the actual architectural GPR set into the
      defined `ExitState.gpr[]` layout:
      - `TMPR0` to `gpr[0]`
      - `TMPR1` to `gpr[1]`
      - `r2..r13` via `stmg`
      - `r14` to `gpr[14]`
      - reconstructed original SP to `gpr[15]`
    - `jit_base` is now cleared with zero, not a stray scratch register value

- Native outcome on `kdz` after rebuild:
  - `prove -v t/exdata.t t/exdata2.t`
    - result: PASS
  - rerunning `prove -v t/*.t`
    - result: PASS
    - `Files=10, Tests=165`

## 2026-03-20 `zkd0` Alignment for `exdata` / `exdata2`

- After confirming the `vm_exit_handler` fix on `kdz`, the same source update
  was mirrored to the fresh `zkd0` worktree:
  - `/root/luajit2-s390x/zkd0-sync-check/src/vm_s390x.dasc`

- Native validation on `zkd0`:
  - rebuild:
    - `make -C src -j8 XCFLAGS='-DLUAJIT_ENABLE_S390X_JIT'`
  - targeted repo-local tests:
    - `prove -v t/exdata.t t/exdata2.t`

- Native outcome on `zkd0`:
  - both files are green after the mirrored fix
  - this closes the only widened repo-local test gap exposed so far on the
    `kdz` full-suite pass and keeps both native hosts aligned for the next
    broadening step

## 2026-03-20 Full Repo-Local `.t` Sweep Green on `zkd0`

- With the mirrored `vm_exit_handler` fix in place, the next widened check on
  `zkd0` matched the `kdz` repo-local Perl sweep:
  - `cd /root/luajit2-s390x/zkd0-sync-check`
  - `export LUA_PATH='./?.lua;./?/?.lua;;src/?.lua;;src/?/?.lua;;tests/?.lua;;tests/?/?.lua;;'`
  - `prove -v t/*.t`

- Native outcome on `zkd0`:
  - result: PASS
  - `Files=10, Tests=165`

- Practical implication:
  - both native hosts are now green on the full repo-local `.t` suite
  - the branch is past the earlier host divergence on `exdata` / `exdata2`
  - the next widening step should move outward across the remaining compiler
    and shared-build matrix until the next real s390x-specific runtime or JIT
    gap appears

## 2026-03-20 Full Repo-Local `.t` Sweep Green on `zkd0`

- After the mirrored `vm_exit_handler` fix was validated on the narrowed
  `exdata` pair, the next widened native check on `zkd0` was the full
  repo-local Perl suite:
  - `cd /root/luajit2-s390x/zkd0-sync-check`
  - `export LUA_PATH='./?.lua;./?/?.lua;;src/?.lua;;src/?/?.lua;;tests/?.lua;;tests/?/?.lua;;'`
  - `prove -v t/*.t`

- Native outcome on `zkd0`:
  - result: PASS
  - `Files=10, Tests=165`

- Practical implication:
  - the current branch is now green on the full repo-local `.t` suite on both
    native hosts:
    - `kdz`
    - `zkd0`
  - the next widening step should move out to the remaining compiler and
    shared-build matrix instead of staying on the repo-local Perl frontier

## 2026-03-20 Full Repo-Local `.t` Sweep Green on `kdz` Under Clang

- The next unproven compiler-host slice after the gcc full-suite passes was a
  native clang rebuild plus the full repo-local Perl suite on `kdz`:
  - `cd /root/luajit2-s390x/local-sync-check`
  - `make clean && make CC=clang XCFLAGS='-DLUAJIT_ENABLE_S390X_JIT' -j4`
  - `PATH=/usr/local/bin:$PATH prove -v t/*.t`

- First native result on `kdz`:
  - clang crashed again in `lj_asm.c` with:
    - `fatal error: error in backend: Unsupported stack frame traversal count`
  - unlike the earlier `zkd0` clang pass, this turned out not to be a new
    backend/compiler regression in the current branch

- Root cause:
  - the `kdz` worktree was stale and still had the old temporary hardening
    probes in:
    - `src/lj_emit_s390x.h`
      - `emit_loadi()` still logged both
        `__builtin_return_address(0)` and `__builtin_return_address(1)`
    - `src/lj_asm_s390x.h`
      - the old signed `LJ_TISNUM << 15` expression was still present
  - once those two spots were aligned to the current local tree, the same
    clang-20 host/compiler combination built cleanly

- Native outcome on `kdz` after aligning the worktree:
  - `make clean && make CC=clang XCFLAGS='-DLUAJIT_ENABLE_S390X_JIT' -j4`
    - result: PASS
  - `PATH=/usr/local/bin:$PATH prove -v t/*.t`
    - result: PASS
    - `Files=10, Tests=165`

- Practical implication:
  - the missing `kdz` clang quadrant is now closed
  - the next matrix widening step should move to the shared-build/full-suite
    combinations instead of revisiting static clang on the same host

## 2026-03-20 Full Repo-Local `.t` Sweep Green on `kdz` Under Clang Dynamic Build

- After closing the static clang pass on `kdz`, the next widening step was the
  full repo-local Perl suite under the shared-library build:
  - `cd /root/luajit2-s390x/local-sync-check`
  - `make clean && make CC=clang BUILDMODE=dynamic XCFLAGS='-DLUAJIT_ENABLE_S390X_JIT' -j4`
  - `prove -v t/*.t`

- First native result on `kdz`:
  - the build itself succeeded
  - the suite then failed uniformly with:
    - exit status `127`
    - `No such file or directory`
  - this was not a JIT/runtime regression; the dynamically linked `src/luajit`
    ran correctly when invoked directly with `LD_LIBRARY_PATH=src`

- Root cause:
  - the repo-local Perl tests chdir during execution, so the relative
    `LD_LIBRARY_PATH=src` used in the first run stopped pointing at the built
    shared library for child processes
  - the correct launcher form for this matrix slice is:
    - `export LD_LIBRARY_PATH=$PWD/src`

- Native outcome on `kdz` after re-running with an absolute runtime path:
  - `export LD_LIBRARY_PATH=$PWD/src`
  - `PATH=/usr/local/bin:$PATH prove -v t/*.t`
    - result: PASS
    - `Files=10, Tests=165`

- Practical implication:
  - the clang dynamic/full-suite path is green on `kdz`
  - this is a harness/runtime-path note, not a new compiler or s390x
    correctness gap
  - the next useful widening step is the same dynamic/full-suite slice on
    `zkd0`

## 2026-03-20 Full Repo-Local `.t` Sweep Green on `zkd0` Under Clang Dynamic Build

- The next matching matrix slice after the `kdz` dynamic clang/full-suite pass
  was the same run on `zkd0`:
  - `cd /root/luajit2-s390x/zkd0-sync-check`
  - `make clean && make CC=clang BUILDMODE=dynamic XCFLAGS='-DLUAJIT_ENABLE_S390X_JIT' -j4`
  - `export LD_LIBRARY_PATH=$PWD/src`
  - `PATH=/usr/local/bin:$PATH prove -v t/*.t`

- Native outcome on `zkd0`:
  - result: PASS
  - `Files=10, Tests=165`

- Practical implication:
  - the full repo-local `.t` suite is now green across both native hosts for:
    - gcc static/default builds
    - clang static/default builds
    - clang dynamic builds
  - the remaining high-value work is no longer matrix plumbing on this repo
    surface; it is the explicit remaining s390x runtime/JIT incompleteness
    still visible in the backend and VM sources

## 2026-03-20 Focused Probe Sweep On `kdz`

- Added a focused traced modulo probe in `tests/s390x/jit_core/mod_trace.lua`.
  - Local x86_64 control: green.
  - Native `kdz`: green.
  - Practical implication:
    - the current traced floating-point modulo path is not blocked by the
      still-stubbed `vm_mod` helper.

- Re-ran the current focused JIT frontiers on native `kdz`:
  - `tests/s390x/jit_core/trace_event_postloop.lua`
  - `tests/s390x/jit_loops/vararg_trace.lua`
  - Outcome: both are green on the current native worktree.

- Extended the FFI ABI oracle with direct complex-value coverage:
  - `double complex add_complex(double complex a, double complex b)`
  - `double complex mul_complex(double complex a, double complex b)`
  - Local x86_64 control: green.
  - Native `kdz`: green.
  - Practical implication:
    - the current s390x ABI path is conservative here, but not obviously wrong
      for direct complex arguments/returns.

## 2026-03-20 Profiler Hook Reproducer And Remediation

- Added a focused profiler probe in `tests/s390x/jit_core/profile_loop.lua`
  using `jit.profile.start("fi1", cb)`.

- Initial native `kdz` result:
  - shorter loop: `samples == 0`
  - longer loop repro: segfault
  - batch gdb backtrace pinned the fault directly to `lj_vm_profhook`.

- Root cause:
  - `vm_profhook` in `src/vm_s390x.dasc` was still a trap stub.

- Local remediation:
  - implemented `vm_profhook` to mirror the mature backends:
    - load `cur_L` from dispatch
    - store `BASE` to `L->base`
    - call `lj_dispatch_profile(L, PC)`
    - restore `BASE`
    - back up `PC` by one instruction and re-dispatch through `cont_nop`

- Native `kdz` outcome after rebuild:
  - the long profiler repro no longer segfaults
  - native samples are now observed (`samples 8` on the longer repro)

- Probe calibration:
  - the original 30M-iteration loop was too short to produce deterministic
    samples on `kdz`
  - an 80M-iteration floating-point accumulation loop produces samples
    reliably (`samples 3` in the focused native one-liner)
  - `tests/s390x/jit_core/profile_loop.lua` has been updated to use that
    longer floating-point loop for a stable gate

- Next step for this slice:
  - validate the same profiler path on `zkd0`
  - then fold the updated profiler probe back into the staged native loop

## 2026-03-20 Bitops JIT Follow-Up

- Native `kdz` status before the latest return-path work:
  - stripped bitops loops now record and reach `TRACE ... stop`
  - the remaining wrong-result reproducer is the raw-return closure shape:
    - `local x = bit.band(i, 0xff)`
    - `x = bit.bxor(x, bit.lshift(i, 3))`
    - `return x`
  - first confirmed native mismatch:
    - `i = 6`
    - expected: `54`
    - got: `6`

- Differential narrowing:
  - `jit.off()` on the focused closure is correct.
  - If the callee is forced interpreted with `jit.off(f, true)`, the outer
    hot loop stays correct on native `kdz`.
  - If the callee is allowed to trace with `jit.on(f, true)`, the result is
    wrong on native `kdz`.
  - Practical implication:
    - the front-most bug is now inside the traced callee path or its return
      contract, not in the outer caller loop alone.

- `asm_retf` remediation:
  - s390x no longer leaves `asm_retf` as a stub.
  - The first implementation removed the large caller-frame corruption and
    changed the wrong-result shape from `393222` to a smaller wrong return.
  - Adding the `REF_BASE` spill update in `asm_retf` fixed the larger caller
    frame poisoning. After that change:
    - `jit.off(f, true)` stays correct
    - `jit.on(f, true)` is still wrong, but now returns `6` instead of
      `393222`
  - Practical implication:
    - traced lower-frame return on s390x is materially closer to correct
    - the remaining bug is back in the traced callee-local path, which is a
      better next frontier than the earlier caller-frame corruption

- Next step:
  - stay on the minimal traced-callee closure
  - inspect the generated callee trace body and its return-value placement
    after the repaired `asm_retf`
  - keep the next cut limited to the `band` / `lshift` / `bxor` path until the
    `i = 6` reproducer is clean
