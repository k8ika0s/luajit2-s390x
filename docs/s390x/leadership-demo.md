# Leadership Demo: OpenResty Gateway on s390x

This demo is the clearest same-day proof of what the LuaJIT `s390x` port
unlocked: a recognizable gateway and plugin surface, running natively on IBM Z,
with Lua policy code and LuaJIT FFI in the live request path.

The current operator stance is:

- OpenResty is the primary proof and should be used first.
- Kong is the next-layer proof and now has a scripted native `s390x` path in
  `demo/kong/run_kong_demo.sh`.
- The fully JIT-enabled Kong nginx startup path now passes on the current
  branch; the remaining demo-only caveat is the `/root` worker permission
  bridge tracked in
  [runtime-remediation.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/runtime-remediation.md).

## Why this demo

- OpenResty embeds LuaJIT into NGINX workers.
- `lua-resty-core` depends on LuaJIT FFI for core APIs.
- Kong Gateway builds its plugin model on top of OpenResty.
- This means a working OpenResty request path on `s390x` is a credible proxy
  for the category of downstream software that was blocked before native
  LuaJIT support existed on the architecture.

## What it shows

- `POST /payments/authorize` accepts a small JSON authorization request.
- A Lua policy engine computes a risk score in the hot path.
- The score uses `ffi.C.abs(...)` to keep a real LuaJIT FFI call in the path.
- The gateway routes the request to mock `approve` or `review` backends.
- `GET /__jit` exposes `jit.status()`, compiled-trace count from `jit.util`,
  policy-path counters, worker PID, and arch.
- The default demo path still uses the conservative metrics mode:
  - `trace_count` comes from `jit.util.traceinfo()`
  - no live observer is attached in the worker by default
- An opt-in observer mode now exists for hardening and tooling work:
  - set `S390X_DEMO_TRACE_OBSERVER=1`
  - `/__jit` then reports worker-local `jit.attach("trace")` and
    `jit.attach("texit")` counters in addition to the `jit.util` trace count
  - this mode is intended for staged validation first, not as the default
    leadership demo setting

## How to run

From this repo:

```bash
demo/openresty/run_demo.sh
```

Defaults:

- primary host: `kdz`
- fallback host: `zkd0`
- remote root: `/root/luajit2-s390x/leadership-demo-<timestamp>`
- OpenResty source tarball:
  `https://openresty.org/download/openresty-1.27.1.2.tar.gz`

Useful overrides:

```bash
OPENRESTY_VERSION=1.27.1.2 demo/openresty/run_demo.sh
S390X_PRIMARY_HOST=zkd0 demo/openresty/run_demo.sh
REMOTE_HTTP_PORT=18080 demo/openresty/run_demo.sh
S390X_DEMO_TRACE_OBSERVER=1 demo/openresty/run_demo.sh
KONG_FORCE_JIT_OFF_IN_NGINX=1 demo/kong/run_kong_demo.sh
```

## Leadership talking track

Use this framing:

1. Before the LuaJIT `s390x` bring-up, the LuaJIT-dependent gateway surface was
   unavailable on IBM Z.
2. Now the same class of software can run natively on `s390x`, including a
   request path that uses Lua policy code and FFI in the hot path.
3. This is a capability proof, not a blanket performance claim. The current
   measured optimization work is still separate and tracked in
   [perf.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/perf.md).

For the current branch, keep one sentence explicit:

- OpenResty is still the cleanest native proof.
- Kong is now also demonstrable with full JIT enabled on native `s390x`; the
  remaining caveat is demo-only worker permissions for `/root`-hosted runtime
  trees.

## Fallback if OpenResty build friction appears

If the OpenResty build path is blocked during the meeting, use the existing
native traced-FFI proofs directly:

```bash
ssh kdz 'cd /root/luajit2-s390x/clean-loop-20260321 && src/luajit tests/s390x/jit_core/ffi_call_trace.lua'
ssh kdz 'cd /root/luajit2-s390x/clean-loop-20260321 && src/luajit tests/s390x/jit_core/ffi_ptr_call_trace.lua'
```

Those are weaker than the gateway demo, but they still prove native LuaJIT JIT
and FFI behavior on `s390x`.

The operator script also does this automatically if the OpenResty request path
crashes during warmup on the current branch. In that case it writes the nginx
error log to the remote log directory and then runs the bundled LuaJIT FFI
trace proofs so the meeting still has a native `s390x` proof point.
