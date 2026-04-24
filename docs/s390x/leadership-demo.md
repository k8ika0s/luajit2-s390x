# Leadership Demo: OpenResty Gateway on s390x

This note describes the demonstration path that best shows what the LuaJIT
`s390x` port enables for downstream software.

## Demo Goal

The strongest public proof is a recognizable gateway workload running natively
on s390x with Lua policy code and LuaJIT FFI in the live request path.

OpenResty is still the cleanest first demonstration. Kong is the higher-level
follow-on because it exercises the same general OpenResty and LuaJIT substrate
through a more recognizable gateway stack.

## What The Demo Shows

- a request path implemented in Lua,
- LuaJIT JIT activity on native s390x,
- at least one real FFI call in the hot policy path,
- an operational gateway surface rather than a microbenchmark alone.

The OpenResty demo remains the preferred first proof. Kong is useful as a
follow-up demonstration when the environment is already prepared.

## How To Run

From this repository:

```bash
demo/openresty/run_demo.sh
```

Optional variants:

```bash
OPENRESTY_VERSION=1.27.1.2 demo/openresty/run_demo.sh
REMOTE_HTTP_PORT=18080 demo/openresty/run_demo.sh
S390X_DEMO_TRACE_OBSERVER=1 demo/openresty/run_demo.sh
KONG_FORCE_JIT_OFF_IN_NGINX=1 demo/kong/run_kong_demo.sh
```

Use an execution environment where the worker user can read the runtime tree.
Do not rely on `/root`-hosted paths or other machine-local permissions
arrangements in the checked-in workflow.

## Talking Track

Use this framing:

1. Before native s390x support, LuaJIT-dependent gateway software was not a
   viable downstream path on IBM Z.
2. Now that same class of software can run natively, with Lua policy code and
   FFI in the request path.
3. This is a capability proof, not a blanket throughput claim. Performance
   evidence is tracked separately in [perf.md](perf.md).

## Fallback Proof

If the gateway demo is blocked by local packaging or deployment friction, fall
back to the checked-in LuaJIT JIT and FFI trace proofs:

- `tests/s390x/jit_core/ffi_call_trace.lua`
- `tests/s390x/jit_core/ffi_ptr_call_trace.lua`

Those are weaker than the full gateway demo, but they still provide a direct
native s390x proof for JIT and FFI behavior.

## Related Notes

- [runtime-remediation.md](runtime-remediation.md)
- [perf.md](perf.md)
- [state-of-project.md](state-of-project.md)
