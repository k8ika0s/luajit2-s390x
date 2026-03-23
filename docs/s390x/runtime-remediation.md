# s390x Runtime Remediation

This note tracks the current runtime remediation loop for the native `s390x`
LuaJIT port as it relates to OpenResty and Kong.

## Current status

- OpenResty on native `s390x` builds and starts against this LuaJIT tree.
- The earlier trace-time `RID_SP` register corruption has been materially
  reduced by backend hardening in the allocator, rename, side-trace, snapshot,
  and temp pseudo-register paths.
- Kong module load on native `s390x` is now stable through
  `require("kong.cmd.init")` and `collectgarbage()`.
- The scripted Kong demo path is now stable with full JIT enabled:
  - staged require probe
  - `prepare`
  - nginx start
  - `GET /status`
  - `GET /demo`
- The old bridge mode remains available as a fallback, but it is no longer the
  default demo path.
- The demo harness also needs a non-LuaJIT fix for `/root`-hosted runtime
  trees: Kong workers normally run as `nobody`, which cannot traverse `/root`.

## Resolved or narrowed finding

- Previous primary failure: JIT traces could use `r15` (`RID_SP`) as a generic
  value register, leading to crashes in generated mcode.
- Impacted area: backend register allocation and inherited register state.
- Current state: this specific failure mode has not reappeared after the recent
  hardening. It remains under regression watch.

## Recent finding

- Current smallest reproducer root:
  `/root/luajit2-s390x/kong-demo-20260322T225753Z`
- Reproducer:
  `resty -e 'require("kong.cmd.init")'`
- Known good probes in that runtime:
  - `require("resty.openssl.version")`
  - `require("ngx.errlog")`
  - `require("kong.tools.dns")`
- Observed earlier failure:
  - `require("kong.cmd.init")` reaches `ok kong.cmd.init`
  - the process can still terminate with `SIGSEGV`
  - the nginx/Kong core dump points at `lj_BC_TGETS`

Updated status after the current remediation:

- `resty -e 'require("kong.cmd.init")'` now completes.
- `resty -e 'require("kong.cmd.init"); collectgarbage()'` now completes.
- The formerly active full-JIT startup crash had moved later:
  - `gc_marktrace()` / trace GC while Kong parses
    `./kong/db/migrations/state.lua` during nginx `init_by_lua`
  - one observed bogus gray-list entry looked like Lua source text rather than
    a valid `GCtrace` or other GC object
- After the new trace guardrails were added, the full-JIT Kong startup demo
  path now passes on `kdz`:
  - `/root/luajit2-s390x/kong-demo-20260323T000006Z`

## Working hypothesis

- The active risk area remains in LuaJIT core, not Kong integration.
- The earlier interpreter/VM issue was real, but it is no longer the front-most
  startup blocker after the `vm_s390x.dasc` containment change.
- The current highest-risk area is trace GC / trace constant handling on
  `s390x` GC64:
  - malformed `IR_KGC` payloads reaching GC traversal
  - malformed saved trace references reaching `gc_marktrace()`
  - trace objects that look committed enough to be traversed, but still carry
    invalid GC roots under startup pressure
- The current branch result is that these guardrails are sufficient for the
  scripted full-JIT Kong demo path, but they should remain under watch until
  they have broader native soak time.

## Current mitigation in tree

- The s390x interpreter now routes the risky string-key/global fast paths
  through the generic metamethod helpers instead of the local fast-path node
  walk.
- This is a deliberate containment step:
  - it preserves correctness while the BE GC64 VM audit continues
  - it is not positioned as a final performance answer
- The Kong demo harness now defaults to the full-JIT path:
  - run `kong prepare`
  - start nginx directly from the Kong source tree
- The old bridge mode is still available:
  - patch the generated nginx init blocks to `require("jit").off()`
- LuaJIT now also has s390x GC64 trace guardrails:
  - validate saved trace roots and `IR_KGC` payloads before trace commit
  - sanitize malformed saved-trace references during GC traversal
  - skip malformed `IR_KGC` payloads during trace traversal instead of
    allowing them to become hard crash candidates
- For runtime trees under `/root`, the demo also patches nginx to run workers
  as `root` so the LMDB path remains accessible. This is demo-only plumbing,
  not a production recommendation.

## Validation sequence

Run these gates in order on native `s390x`:

1. `demo/openresty/run_demo.sh`
2. `demo/kong/run_kong_demo.sh` with runtime build enabled
3. `demo/kong/run_kong_require_probe.sh <remote-root>`
4. `resty -e 'require("kong.cmd.init")'`
5. `resty -e 'require("kong.cmd.init"); collectgarbage()'`
6. `curl -fsS http://127.0.0.1:8001/status`
7. `curl -fsS http://127.0.0.1:8000/demo`
8. `KONG_FORCE_JIT_OFF_IN_NGINX=1 demo/kong/run_kong_demo.sh` only if the
   full-JIT path regresses and you need the fallback bridge mode

Every failing stage should archive:

- exact remote root
- exact command
- key environment
- last successful stage
- `coredumpctl` summary

## Next audit areas

- full-JIT Kong nginx startup rerun with `KONG_FORCE_JIT_OFF_IN_NGINX=0`
- `lj_gc.c` trace traversal and gray-list integrity under startup trace churn
- `lj_trace.c` trace save / commit invariants for `IR_KGC` on `s390x` GC64
- wider native reruns to ensure the new trace guardrails do not mask a second
  correctness issue elsewhere
- `vm_s390x.dasc` interpreter audit remains in scope so the earlier
  string-key/global issue does not regress
- OpenResty request-path trace-observer support, which is still disabled in
  the leadership demo by default; the next staged hardening path is the new
  opt-in `S390X_DEMO_TRACE_OBSERVER=1` worker listener mode so `/__jit` can
  report real observer counters without replacing the stable `jit.util`
  fallback path
- continued backend invariant checks to ensure `RID_SP` cannot re-enter value
  allocation paths
