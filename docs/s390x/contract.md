# s390x Architecture Contract

This document is the contract for the staged s390x bring-up. It is the source
of truth for the remote harness gate and the reference used to reconcile the
current partial port before enabling JIT.

## Scope

- Target ABI: Linux s390x ELF psABI on big-endian 64-bit z/Architecture.
- Runtime model: GC64, dual-number mode, native s390x build and execution.
- Bring-up rule: do not remove `LJ_ARCH_NOJIT` until the contract below is
  reflected in code and validated by the staged harness.

## Register and Frame Contract

- General-purpose registers: 16 GPRs, with `r15` as stack pointer and `r14` as
  the architectural return-address register.
- Floating-point registers: 16 FPRs, with outbound FFI FP arguments using the
  even register sequence `f0`, `f2`, `f4`, and `f6`.
- BASE register contract: interpreter and JIT use `r13` as `BASE`. Any s390x
  target metadata must match `vm_s390x.dasc`.
- Callee-save area: the staged bring-up treats the historical 160-byte save
  area assumption as the current unwind and callback baseline unless native
  validation proves otherwise.
- Exit-state contract: `ExitState.gpr[]` must store full pointer-width
  `intptr_t` values. No 32-bit truncation is acceptable in JIT exit state.

## Unwind and Exception Contract

- Unwind return-address contract: the architectural return-address register is
  `r14`, and the DWARF CFA RA register number is `14`.
- CFA adjustment contract: s390x external unwinding must account for the stack
  layout used by the VM prologue, including the 160-byte historical CFA offset
  already encoded in `lj_err.c`.
- JIT unwind contract: s390x must be added to the common JIT unwind constant
  path before JIT is enabled.

## FFI ABI Contract

- Integer argument registers: `r2` through `r6`.
- FP argument registers: `f0`, `f2`, `f4`, `f6`.
- Small integer and enum arguments smaller than 64 bits must be sign- or
  zero-extended to 64 bits before outbound call boundaries.
- Small structs of size 1, 2, 4, or 8 bytes may travel by value in a GPR.
- Small by-value structs narrower than 8 bytes are right-justified in the low
  bits of the GPR or stack slot on big-endian s390x.
- Struct returns use a caller-allocated result buffer passed in `r2`; small
  structs do not bypass the hidden sret pointer on this ABI.
- Complex values and homogeneous FP aggregates must be handled per the psABI,
  not by ad hoc by-reference fallback.
- Callback argument and result marshalling must match the outbound ABI.

## Endianness and Memory Contract

- The port is big-endian only. Low-word and high-word handling must be explicit
  anywhere 32-bit subfields of 64-bit values are read or written.
- Snapshot restore, split IR handling, and FFI mixed-width access must preserve
  big-endian layout rules.
- Unaligned access is permitted, but any helper that assumes little-endian word
  order is incorrect on this target.

## JIT Backend Contract

- Shared JIT constant-table contract: s390x must be wired into the common VM
  exit constant tables before `LJ_ARCH_NOJIT` is removed.
- Target metadata must provide the same level of completeness as other 64-bit
  backends: register sets, fixed registers, scratch policy, spill policy,
  exit-stub spacing, and any required return-pair metadata.
- `vm_record`, `vm_next`, exit handling, stitch and re-entry, loop opcodes,
  profiler hooks, and FFI callback return are required runtime paths for a
  complete JIT port.
- Compiled vararg `BC_JFUNCV` is an explicit peer-parity exception, not an
  s390x-specific completion blocker. The peer VM backends also leave compiled
  vararg functions NYI, and the recorder asserts that `BC_JFUNCV` cannot become
  hot under current hotcall semantics. Keep it visible in coverage as a
  cross-architecture parked NYI unless that shared recorder/VM contract changes.

## Known Contradictions Resolved

- [x] BASE register contract: interpreter and JIT use `r13` as `BASE`.
- [x] FP argument register contract: outbound FFI arguments use `f0`, `f2`, `f4`, and `f6`.
- [x] Exit-state contract: `ExitState.gpr[]` must store full pointer-width `intptr_t` values.
- [x] Unwind return-address contract: the architectural return-address register is `r14`, and the DWARF CFA RA register number is `14`.
- [x] Shared JIT constant-table contract: s390x must be wired into the common VM exit constant tables before `LJ_ARCH_NOJIT` is removed.

## File Map

- `src/lj_arch.h`
- `src/lj_target_s390x.h`
- `src/vm_s390x.dasc`
- `src/lj_ccall.h`
- `src/lj_ccall.c`
- `src/lj_ccallback.c`
- `src/lj_trace.c`
- `src/lj_jit.h`
- `src/lj_err.c`
- `src/lj_frame.h`
- `src/host/buildvm.c`
- `src/host/buildvm_asm.c`
- `dynasm/dasm_s390x.lua`
- `dynasm/dasm_s390x.h`
- `src/jit/dis_s390x.lua`

## Current Code vs Contract

- `src/vm_s390x.dasc` already uses `r13` as `BASE`; `src/lj_target_s390x.h`
  must be brought into alignment.
- `src/lj_ccall.h` comments currently disagree with the `.dasc` FP register map;
  the validated contract is `f0`, `f2`, `f4`, `f6`.
- `src/lj_target_s390x.h` currently models exit-state GPRs too narrowly for a
  64-bit JIT exit path.
- `src/lj_jit.h` and `src/lj_trace.c` still need explicit s390x constant-table
  support before JIT enablement.
