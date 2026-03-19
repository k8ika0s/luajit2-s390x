# s390x Status

This branch carries the in-progress native s390x bring-up for this LuaJIT
tree. The work is local-first, validated on native IBM Z hosts, and is not yet
ready for upstreaming.

## Current Status

- Interpreter-only native s390x coverage is green.
- Outbound FFI ABI coverage is green on native s390x for gcc and clang.
- The staged JIT bring-up is active and has moved past initial assembler,
  helper-call, loop-fixup, `SLOAD`, and `BC_JLOOP` barriers.
- Root loop traces can now assemble and enter for the `table.isarray` path.
- The next hard blocker is trace exit handling in
  [vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc):
  `vm_exit_handler` and `vm_exit_interp`.
- After exit handling, the next exposed backend gap is `IR_ALOAD`.

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
