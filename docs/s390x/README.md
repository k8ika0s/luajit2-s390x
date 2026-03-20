# s390x Status

This branch carries the in-progress native s390x bring-up for this LuaJIT
tree. The work is local-first, validated on native IBM Z hosts, and is not yet
ready for upstreaming.

## Current Status

- Interpreter-only native s390x coverage is green.
- Outbound FFI ABI coverage is green on native s390x for gcc and clang.
- Callback and unwind coverage is green in the current native gcc stage gate.
- The staged JIT bring-up has moved past trace entry, exit handling,
  `BC_JLOOP`, the first `SLOAD` / `ALOAD` / `FLOAD` / `VLOAD` slices,
  side-exit spill restore, integer `MULOV`, non-loop tail fixup, explicit
  `next()`, `pairs()`, and the earlier iterator hot-exit crash path.
- Root loop traces assemble, enter, side-exit, and stitch on native s390x for
  the current `jit_core` and `jit_loops` probes.
- The native `jit_core` gcc debug and gcc release gates are green under the
  staged harness.
- Fresh native focused validation on `kdz` is green for:
  - `t/isarr-jit.t`
  - `t/iter.t`
  - `t/table-clone.t`
  - `tests/s390x/jit_core/*.lua`
  - `tests/s390x/jit_loops/*.lua`
  - `tests/s390x/jit_be/*.lua`
  - `tests/s390x/ffi_abi/run.lua`
  - `tests/s390x/callbacks/run.lua`
  - `tests/s390x/soak/mixed_stress.lua`
- A clean native clang JIT build on `kdz` is also green for the same focused
  repo `.t` coverage plus the current `tests/s390x` JIT, BE, FFI, callback,
  and soak sweeps.
- A focused native dynamic-build sweep on `kdz` is green after fixing the ELF
  shared-library packaging path:
  - `src/Makefile` now emits the real soname target
    `libluajit-5.1.so.2` and keeps `libluajit.so` as a symlink
  - `tools/s390x/remote_run.sh` now exports the repo-local `LD_LIBRARY_PATH`
    so staged dynamic runs pick up the freshly built shared library
- A second-host native gcc sweep is green on `zkd0` for the same focused repo
  `.t` coverage plus the current JIT, BE, FFI, callback, and soak probes.
- The focused native dynamic-build sweep is also green on `zkd0`, including
  repo `.t` coverage plus the standalone FFI and callback drivers.
- A second-host native clang sweep is green on `zkd0` for the same focused
  repo `.t`, JIT, BE, FFI, callback, and soak coverage.
- Additional focused native `kdz` probes are now green for:
  - traced floating-point modulo loops
  - post-trace event table walking
  - the current vararg loop trace probe
  - direct complex-value FFI ABI calls
- A clean-tree native gcc build on `kdz` is green after fixing the shared
  library build graph for dynamic objects:
  - `src/Makefile` now gives `$(LJVMCORE_DYNO)` the same generated-header
    ordering fence as the static object set
  - clean parallel mixed builds no longer race `_dyn.o` compilation ahead of
    `lj_bcdef.h` / `lj_ffdef.h`
  - the post-fix clean-tree rerun on `kdz` is green again for the focused
    repo `.t`, JIT, BE, FFI, callback, and soak coverage
- The full repo-local Perl test sweep is now green on native `kdz`:
  - `prove -v t/*.t`
  - the last remaining failures were `t/exdata.t` and `t/exdata2.t`
  - both were fixed by saving the real architectural `r1`/GPR state in
    `vm_exit_handler` so sunk `IR_CNEWI` restore sees the correct pointer
    payload on exit
- The same `vm_exit_handler` fix is mirrored and validated on native `zkd0`:
  - clean rebuild with `XCFLAGS='-DLUAJIT_ENABLE_S390X_JIT'`
  - `prove -v t/exdata.t t/exdata2.t`
  - result: green on the second host as well
- The broader repo-local Perl sweep is now green on `zkd0` too:
  - `prove -v t/*.t`
  - `Files=10, Tests=165`
  - result: PASS on the second host
- The mixed dynamic-key hot-exit miss-chain is fixed:
  - the s390x backend now avoids the fused `HREF + EQ/NE` helper-guard path,
    which was assigning the miss guard to `exit 0` and cloning the loop
  - native `kdz` now matches the expected control shape for the mixed
    preinterned update probe: root trace, update trace, loop trace, then
    stitch
- The next active frontier is no longer the original iterator/hot-exit path.
  The current focused matrix is green across both native hosts for gcc, clang,
  and the dynamic ELF path. The next work has shifted into remaining JIT
  completion work for traced FFI C calls, wider loop families, and hardening,
  while also chipping away at the remaining backend warning surface.
- The first focused traced-FFI direct-call probe is now green on native
  `zkd0`:
  - the root cause was `CALLXS` forcing far foreign targets through the direct
    `BRASL` path instead of materializing an indirect call
  - `tests/s390x/jit_core/ffi_call_trace.lua` is now green after adding the
    indirect `BASR` path for out-of-range traced FFI call targets
- The stored-function-value traced-FFI follow-up is also green on native
  `zkd0`:
  - `tests/s390x/jit_core/ffi_ptr_call_trace.lua` now reaches trace `"stop"`
    and exits `0`
  - the missing backend piece was `IR_FLOAD` for the recorder's `u16`
    `cdata.ctypeid` load; s390x `asm_fload()` now handles `U8`, `U16`, sign-
    extended `I8`/`I16`, and GC64 pointer/GC-reference field loads
  - the next narrower blocker in this area is no longer trace assembly
- The traced event post-processing crash on native `zkd0` is now fixed:
  - `tests/s390x/jit_core/trace_event_postloop.lua` is green again
  - the original no-print mixed-type `tostring()` reproducer is also green
  - the root cause was not bad event payload data, but s390x stack
    re-anchoring around trace-exit resume and stitched/helper dispatch paths
  - `cont_stitch`, the special ins-hook/record/rethook dispatch path, and
    `vm_exit_interp` now reload authoritative state from `J->L` /
    `cur_L` and re-anchor from `L->cframe` instead of trusting skewed
    `SAVE_*` slots
- The next active frontier has shifted again:
  - the traced FFI post-loop crash is no longer blocking
  - the clean-tree dynamic build graph is no longer blocking either
  - `kdz` is now green on the full repo-local `.t` suite
  - `zkd0` now matches that full repo-local `.t` result
  - `kdz` is now also green on the full repo-local `.t` suite under clang once
    the worktree is aligned to the current local hardening changes
  - `kdz` is also green on the full repo-local `.t` suite in clang
    `BUILDMODE=dynamic` once the runtime path is exported as an absolute
    `LD_LIBRARY_PATH`
  - `zkd0` now matches that full clang dynamic/full-suite result too
  - the next useful step is to widen that broader repo coverage across the
    remaining matrix corners only if they exercise a meaningfully different
    surface, then return focus to the remaining explicit s390x runtime and JIT
    gaps until the next real correctness blocker appears
- The profiler-hook path is no longer an unconditional s390x crash:
  - `vm_profhook` is now implemented in `src/vm_s390x.dasc`
  - the long native `jit.profile` repro on `kdz` now completes with nonzero
    samples instead of segfaulting
  - the focused profiler probe has been recalibrated to a longer loop so it
    can act as a stable native gate instead of a zero-sample false negative
- The next focused JIT backend slice has moved from NYI to correctness on the
  bitops path:
  - s390x now lowers `NEG` and `UREFO`/`UREFC`
  - stripped native bitops loops on `kdz` now record and reach `TRACE ... stop`
  - the remaining failure is no longer a generic bitops NYI
  - the current front-most bug is a wrong-result issue in the raw-return
    `bitops_trace.lua` shape, where the first native mismatch appears in the
    early `bit.band` / `bit.lshift` / `bit.bxor` chain for `i = 6`
- The detailed findings log below is the authoritative status record for the
  current native bring-up work.

## Detail Links

- Architecture contract:
  [docs/s390x/contract.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/contract.md)
- Native findings and run history:
  [docs/s390x/findings.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/findings.md)
- Bring-up workflow and artifact guide:
  [docs/s390x/runbook.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/runbook.md)
- Harness entrypoint:
  [tools/s390x/driver.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/driver.py)

## Branch Intent

- Preserve the full staged harness, native test assets, and current s390x port
  work in one shareable branch on the fork.
- Keep the native validation trail documented so later upstreaming and CI work
  can start from observed behavior instead of re-discovery.
