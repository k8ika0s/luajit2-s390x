# s390x Status

This branch carries the in-progress native s390x bring-up for this LuaJIT
tree. The local checkout is the source of truth for code changes. Native IBM Z
validation is authoritative only when it is reproduced from the current remote
sync loop.

## Trustworthy Baseline

- The current authoritative native worktrees are:
  - `kdz:/root/luajit2-s390x/clean-loop-20260321`
  - `zkd0:/root/luajit2-s390x/spotcheck-20260321`
- Local code changes are still made here first.
- Structured remote validation now syncs only git-tracked files over tar+ssh.
- Older ad hoc or polluted remote trees are not authoritative.

## Revalidated On The Current Native Loop

- clean native rebuild with `XCFLAGS='-DLUAJIT_ENABLE_S390X_JIT -DLUA_USE_ASSERT'`
- non-JIT `ffi` cdata smoke repro:
  - `x4 = 4`
  - `gc-ok`
- forced-hotloop traced `ffi` cdata field-store repro:
  - `xy = 200 201`
  - `gc-ok`
- direct-exit vararg repro:
  - `/tmp/vararg_result.lua`
  - `5650`
  - exit `0`
- focused side-exit repro:
  - `/tmp/side_exit_n4.lua`
  - `10`

## Current Working Position

- The branch is back on a trustworthy remediation loop.
- The closure layer is now wired into the harness:
  - new stage: `closure`
  - new suites:
    - `coverage_audit`
    - `downstream`
  - closure-local source audit artifacts now emit under:
    - `artifacts/s390x/<run-id>/coverage/`
  - first local closure audit restamp:
    - run `closure-audit-local-20260323T005100Z`
    - artifacts:
      [coverage/report.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/closure-audit-local-20260323T005100Z/coverage/report.md)
- The closure coverage audit remains the authoritative source-backed inventory
  for remaining candidate gaps. Treat `coverage/report.md` as the source of
  truth instead of relying on older summary lists in this overview.
- The clean native gcc `prove -v t/*.t` sweep is now green on both:
  - `kdz:/root/luajit2-s390x/clean-loop-20260321`
  - `zkd0:/root/luajit2-s390x/spotcheck-20260321`
- The latest verified vararg fix is in the direct-exit tail path:
  - `SAVE_L` is now rematerialized from `DISPATCH` for no-link exits
  - `tests/s390x/jit_loops/vararg_trace.lua` is green on native `kdz`
  - `tests/s390x/jit_loops/vararg_seq.lua` remains green
  - `tests/s390x/jit_loops/vararg_return_split.lua` remains green
- The staged harness has re-stamped `jit_loops` green on `kdz`:
  - run `20260322T011616.680245Z-p25132`
  - stage `jit-bringup`
  - suite `jit_loops`
- The next wrong-result frontier, `tests/s390x/jit_core/side_exit.lua`, was
  fixed by changing s390x integer snapshot restore to prefer a live register
  over a stale spill when both exist.
- After that `src/lj_snap.c` fix:
  - `tests/s390x/jit_core/side_exit.lua` is green on native `kdz`
  - a direct native sweep of `tests/s390x/jit_core/*.lua` is green on `kdz`
- `jit_core` is now also stamped green under the harness on `kdz`:
  - run `20260322T012714.417104Z-p33648`
  - stage `jit-bringup`
  - suite `jit_core`
- `jit-correctness` is now stamped green under the harness on `kdz`:
  - run `20260322T013015.882691Z-p35735`
  - stage `jit-correctness`
  - suites `smoke`, `jit_core`, `jit_loops`, `jit_be`, and `soak`
- The hardened structured matrix is now green on `kdz` for:
  - `matrix/smoke`
    - run `20260322T034609.025414Z-p15495`
  - `matrix/jit_core` gcc debug
    - run `20260322T050327.377254Z-p44209`
  - `matrix/jit_loops` gcc debug
    - run `20260322T051500.668189Z-p50274`
  - `matrix/trace_tools` gcc release
    - run `trace-tools-kdz-20260323a`
  - `matrix/jit_be` gcc debug
    - run `20260322T051917.451476Z-p52584`
  - `matrix/soak` gcc debug
    - run `20260322T053325.470734Z-p59387`
  - `matrix/jit_core` clang debug
    - run `20260322T055509.960285Z-p68367`
  - `matrix/jit_core` gcc release
    - run `20260322T055912.538970Z-p70902`
- The structured second-host restamp is also green on `zkd0` for:
  - `matrix/jit_core` gcc debug
    - run `20260322T055509.960285Z-p68366`
- Focused matrix spot-checks are also green on the current tree:
  - `zkd0` passes the current hot regression set for `side_exit`, vararg,
    BE helpers, soak, `t/iter.t`, and `t/isarr-jit.t`
  - `kdz` passes a focused clang JIT build plus the same hot regression set
  - `zkd0` also passes the same focused clang JIT spot-check set
  - `kdz` passes the current `-DLUAJIT_DISABLE_FFI` corner for:
    - `t/isarr-jit.t`
    - `t/iter.t`
- The current harness hardening work is no longer about first-line JIT
  correctness on the tested surface. It is about making the driver’s remote
  sync/collection path robust on banner-printing hosts so the structured
  artifacts keep pace with the green manual native matrix.
- The downstream gateway proof is now real on native `s390x`:
  - `demo/openresty/run_demo.sh` builds and starts OpenResty against this tree
  - the OpenResty request path uses Lua policy code plus `ffi.C.abs(...)`
  - `GET /__jit` reports enabled JIT and native trace counters
  - the demo now also has an opt-in observer mode:
    - `S390X_DEMO_TRACE_OBSERVER=1`
    - `/__jit` exposes worker-local `jit.attach("trace")` and
      `jit.attach("texit")` counters
    - the latest `kdz` observer-mode restamp is stable and shows attach
      success plus live event counters, but the current request-path workload
      still aborts without a committed `stop`, so this remains a hardening
      surface rather than the default demo mode
- The downstream Kong proof is now real in staged bridge mode:
  - `demo/kong/run_kong_demo.sh` runs staged `resty` probes first
  - `require("kong.cmd.init")` and `collectgarbage()` are stable
  - the raw full-JIT nginx startup path is still unstable on `kdz`
  - the current proven downstream bridge is a delayed startup guard:
    - `KONG_DELAYED_JIT_ON_IN_NGINX=1`
    - `KONG_DELAYED_JIT_ON_SECS=3`
    - JIT stays off through `init_by_lua` and `init_worker_by_lua`
    - JIT is re-enabled from a delayed worker timer after startup settles
  - that guarded path is manually proven on `kdz` for:
    - nginx start
    - `GET /status`
    - `GET /demo`
  - the structured downstream restamp is now green from:
    - `closure-downstream-kdz-20260323d`
    - `closure-downstream-zkd0-20260323b`
  - the Kong demo harness now assigns deterministic per-run proxy/admin ports
    and stops nginx on exit, so closure restamps no longer collide on fixed
    `8000/8001` listeners
  - the remaining demo-only override is worker-as-root for runtime trees under
    `/root`
- The recent product-shaped LuaJIT runtime loop has now:
  - cleared the old interpreter-side `BC_TGETS` startup crash
  - added s390x GC64 trace guardrails around trace commit and trace traversal
  - cleared the current full-JIT Kong startup demo path
- The current harness transport state:
  - repo sync is now tracked-files-only tar-over-ssh
  - macOS metadata is stripped from the tar stream
  - optional binary collection is best-effort and no longer treated as a hard
    failure when a variant does not produce every output
  - the active structured reruns are widening from that hardened baseline
    across compiler, mode, and host axes
- The next support claim gate is no longer the generic matrix alone. It is a
  full green `closure` stage on `kdz`, plus second-host closure spot checks on
  `zkd0`, with the coverage report showing no exercised backend/runtime stub
  left unimplemented.
- The trace-tooling observer lane is now a real staged suite instead of ad hoc
  manual probes:
  - suite: `trace_tools`
  - first native `kdz` restamp:
    - `trace-tools-kdz-20260323a`
  - second-host `zkd0` restamp:
    - `trace-tools-zkd0-20260323c`
  - covered surfaces:
    - `jit.attach("trace")`
    - `jit.attach("texit")`
    - `jit.util.traceinfo()` before and after flush
    - repo-root `require("jit.v")`
    - repo-root `require("jit.dump")`
  - the suite now runs as part of:
    - `jit-correctness`
    - `matrix`
    - `closure`
- The last `kdz` closure blocker was the downstream Kong lane:
  - `closure-kdz-20260323c` was green everywhere except `downstream`
  - `closure-downstream-kdz-20260323d` now restamps that lane green with the
    delayed startup guard
  - `closure-kdz-20260323e` now restamps the full `kdz` closure stage green
- The previously failing closure soak frontier is now materially reduced on the
  current native loop:
  - the reduced fresh-table restore path is fixed in `src/lj_snap.c`
  - the guarded upvalue load alias in `asm_uref` is fixed in
    `src/lj_asm_s390x.h`
  - the fused dynamic array-base preservation fix is in
    `src/lj_asm_s390x.h`
  - direct native `kdz` repros now pass again for:
    - `/tmp/s390x_keep_worker.lua`
    - `/tmp/s390x_mode0_only.lua`
    - `tests/s390x/soak/trace_gc_churn.lua`
  - the full closure restamp is now green as:
    - `closure-kdz-20260323e`

## Current Next Actions

1. Restamp the required second-host closure spot checks on `zkd0` from the
   now-hardened downstream harness:
   - correctness suites are already green in `closure-zkd0-20260323a`
   - downstream lane is green in `closure-downstream-zkd0-20260323b`
2. Commit and push the downstream hardening plus documentation restamp.
   - `trace_tools`
   - `downstream`
3. Keep the reduced soak regressions in the hot native set:
   - `/tmp/s390x_keep_worker.lua`
   - `/tmp/s390x_mode0_only.lua`
   - `tests/s390x/soak/trace_gc_churn.lua`
4. Restamp the required second-host `trace_tools` and closure spot checks on
   `zkd0`.
5. Close or explicitly rule out every exercised item still listed in the
   latest closure coverage report.
6. Only after those gates are green should the branch documentation claim
   branch-level native `s390x` support with JIT as a closure-stamped result.

## Closure Suite Order

The current full closure restamp on `kdz` is driving this order:

- `smoke`
- full `prove -v t/*.t`
- `ffi_abi`
- `callbacks`
- `jit_core`
- `jit_loops`
- `trace_tools`
- `jit_be`
- `soak`
- `coverage_audit`
- `downstream`
- `perf_bench` with the current `dispatch_trace` gate

## Detail Links

- Architecture contract:
  [docs/s390x/contract.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/contract.md)
- Native findings and run history:
  [docs/s390x/findings.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/findings.md)
- Bring-up workflow and artifact guide:
  [docs/s390x/runbook.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/runbook.md)
- Leadership demo operator notes:
  [docs/s390x/leadership-demo.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/leadership-demo.md)
- Runtime remediation tracker:
  [docs/s390x/runtime-remediation.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/runtime-remediation.md)
- Performance validation plan:
  [docs/s390x/perf.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/perf.md)

## Performance Status

- The perf stage is now implemented in the harness and emits structured native
  benchmark artifacts under `artifacts/s390x/<run-id>/perf/`.
- The first stamped native release baseline is the `dispatch_trace` family on
  `kdz`.
- Authoritative perf run artifacts:
  - JIT on:
    [20260322T145212.814679Z-p29811 summary](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/20260322T145212.814679Z-p29811/summary.md)
  - JIT off:
    [20260322T145555.369124Z-p31961 summary](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/20260322T145555.369124Z-p31961/summary.md)
- Current measured result:
  - the harness is working
  - z13 tuning already improves the side-exit-heavy dispatch shape
  - the dispatch/side-exit family is the first proven performance hotspot,
    because current s390x `jit=on` is slower than `jit=off` on that workload
  - first headline ratios:
    - `side_exit_loop/hot`: `z13` is about `1.18x` faster than baseline
    - `numeric_loop/hot`: baseline `jit=on` is about `21.69x` slower than
      `jit=off`
- The broader perf catalog remains in-tree, but only the release-stable subset
  should gate the current perf stage until the remaining families are
  correctness-stable under native release measurement.
