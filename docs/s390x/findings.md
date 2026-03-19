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
