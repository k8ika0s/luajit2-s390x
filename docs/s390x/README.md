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
- The downstream Kong proof is now real in staged bridge mode:
  - `demo/kong/run_kong_demo.sh` runs staged `resty` probes first
  - `require("kong.cmd.init")` and `collectgarbage()` are stable
  - the scripted Kong path reaches `prepare`, nginx start, `GET /status`, and
    `GET /demo`
  - the full-JIT startup path now also passes on `kdz`
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

## Current Next Actions

1. Keep `vararg_trace`, the direct-exit `SAVE_L` path, `side_exit.lua`, and
   `thread.exdata()` in the focused regression set so these fixes do not
   silently regress.
2. Finish the current widened `jit_loops` proof wave:
   - `kdz` clang debug
   - `zkd0` gcc debug
3. Carry the same proof pattern into:
   - `jit_loops` gcc release on `kdz`
   - then `jit_be` and `soak` across clang, release, and second-host lanes
4. Once those matrix cuts are stamped, restate the branch as a validated
   matrix/hardening branch rather than an active backend rescue branch.
5. Keep the new full-JIT Kong startup path under regression watch while the
   broader matrix and perf work continues.

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
