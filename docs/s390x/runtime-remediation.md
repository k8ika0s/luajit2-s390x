# s390x Runtime Remediation

This note summarizes the runtime-facing remediation themes that mattered for
OpenResty and Kong on native s390x.

## Current Status

- OpenResty builds and starts against this LuaJIT tree on native s390x.
- Kong module load and the scripted demo path are stable on the current branch.
- The old fallback bridge that disables JIT during nginx startup remains
  available, but it is no longer the preferred path.

## Important Resolved Or Narrowed Areas

### Stack Pointer Register Safety

An earlier class of crashes came from allowing `RID_SP` to participate as a
generic value register in generated code. The current branch has hardening in
allocation, renaming, snapshot, and side-trace paths to keep that failure mode
contained. It should remain under regression watch.

### GC64 Trace-Root Integrity

Another major runtime seam was trace GC and trace-constant integrity on s390x
GC64. The durable lesson is that malformed saved trace roots or malformed
`IR_KGC` payloads can surface as runtime crashes far away from the original
recording site.

The current branch carries guardrails in that area. They are a containment and
correctness measure, not an argument that the surrounding code needs no further
audit.

### Interpreter Containment

The interpreter also carries containment for risky string-key and global fast
paths by routing them through safer generic helper paths while broader BE GC64
audit work continues.

## Runtime Environment Rule

Do not encode runtime procedures that depend on one machine's directory layout
or worker-user permissions. If a demo or runtime tree is not readable by the
worker user, fix the deployment layout for that run instead of preserving
checked-in instructions that assume a privileged directory.

## Validation Sequence

Run these in order on native s390x:

1. `demo/openresty/run_demo.sh`
2. `demo/kong/run_kong_demo.sh`
3. `demo/kong/run_kong_require_probe.sh <runtime-root>`
4. `resty -e 'require("kong.cmd.init")'`
5. `resty -e 'require("kong.cmd.init"); collectgarbage()'`
6. the demo's HTTP status and request-path checks
7. the old bridge mode only if the full-JIT startup path regresses

Archive enough information to reproduce any failure:

- command,
- environment,
- runtime root,
- last successful stage,
- crash summary if a crash occurred.

## Ongoing Watch Areas

- trace save and commit invariants for GC64 trace roots,
- GC traversal and saved-trace integrity,
- interpreter regressions in the contained fast paths,
- native reruns to ensure runtime guardrails are not hiding a second
  correctness issue elsewhere.

## Related Notes

- [leadership-demo.md](leadership-demo.md)
- [state-of-project.md](state-of-project.md)
- [findings.md](findings.md)
