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
  - refreshed local closure audit restamp:
    - run `closure-audit-local-20260323b`
    - artifacts:
      [coverage/report.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/closure-audit-local-20260323b/coverage/report.md)
- The closure coverage audit remains the authoritative source-backed inventory
  for remaining candidate gaps. Treat `coverage/report.md` as the source of
  truth instead of relying on older summary lists in this overview.
- The refreshed audit now freezes the remaining backlog by track:
  - `numeric_helpers`
  - `reference_string_barrier`
  - `vm_runtime`
  - `feature_gated_debug`
- The refreshed report also reconciles one stale item:
  - `asm_tobit` is implemented and no longer part of the live stub backlog
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
- The iterator family is now in a split state:
  - structural trace-shape guardrail is green on both native hosts through
    [tests/s390x/jit_loops/iterator_trace_shape.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/jit_loops/iterator_trace_shape.lua)
  - the old unbounded `root -> n` iterator trace churn is no longer the live
    blocker
  - the remaining blocker is steady-state performance on native `kdz`
    release JIT, not basic correctness
- The latest concrete iterator/backend finding is a real Linux/s390x ABI fix:
  - `lj_vm_next` in
    [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc)
    was incorrectly using saved register `r6` as helper scratch state
  - that could clobber a loop-carried integer live range across the iterator
    helper boundary
  - the helper now preserves `r6`, and native iterator probes remain correct
- The remaining iterator performance red is narrower now:
  - safe baseline hotspot still centers on the iterator array family, but the
    current collapsed scratch branch has moved the steady-state payer later
    into the recovered loop family
  - direct `kdz` guard-site probing now confirms the live payer is the later
    in-loop `vload_next_key_int` copy marked `0x527`, not the earlier
    preheader `0x427` copy
  - that hot `trace 8 / curins 7` site reads `tmptv` directly and sees the
    expected end-of-iteration helper result:
    - stale non-nil value side in `tmptv`
    - nil key side in `tmptv`
    - fully nil `tmptv2`
  - so the remaining array-side red is not bad helper data, not bad compare
    logic, and not preheader re-entry on the collapsed branch
  - the live frontier is now the in-loop `CALLL lj_vm_next -> key-lane nil
    split` cluster inside the recovered steady loop body
  - scratch suppression of the duplicated KEYINDEX-side pre-call guard is not
    the landing fix; with the helper ABI fix in place it still only exposes the
    deeper owner at root `exit 2`
  - that surviving owner maps to the post-call carried-total boundary
    (`CALLL -> HIOP -> VLOAD -> SLOAD -> ADDOV`)
  - the latest scratch-only key-lane classifier now proves the bad
    `BC_ADDVV` resume is not the final frontier:
    - a narrow post-call key-lane guard keeps the direct iterator repro
      correct and moves the hot owner to `guardmark=0x427`
    - the exposed hot exits now land at `BC_JLOOP`, not the stale-add resume
      path
    - repeated stop probes keep the carried total sane on that path
    - the next live blocker is the follow-on `TRACE 2 abort otr=9`
      (`LJ_TRERR_LINNER`) after the clean `BC_JLOOP` boundary
  - that key-lane guard remains scratch-only until the follow-on abort is
    understood and the perf story is promotable

## Current Next Actions

1. Use `closure-audit-local-20260323b` as the frozen Stream A backlog source:
   - active closure blockers: `11`
   - `asm_prof` is tracked separately as `feature_gated_debug`
   - `asm_tobit` is reconciled as implemented
2. Keep the new integer-modulo repro hot:
   - `tests/s390x/jit_core/mod_int_trace.lua`
   - native `kdz` release is now green for the root and simple side-exit `%`
     shapes on the current branch head
   - the previously failing aggressive hotexit/stitch `%` stress repro is now
     green on both native hosts as:
     - `tests/s390x/jit_loops/mod_hotexit_stress.lua`
   - the fix was in root-trace restore handling for loop-carried integer state
     on modulo hotexit exits, not in generic `%` lowering
   - keep `mod_int_trace.lua` as the Stream B optimization entry point now
     that the hotexit correctness repro is no longer red
3. Keep iterator work on the clean `BC_JLOOP` follow-on path:
   - keep the `lj_vm_next` `r6` preservation fix
   - keep duplicate-guard diagnostics as scratch-only until the final iterator
     perf fix is proven
   - the current live target is why the semantically clean iterator-end
     boundary still aborts the next trace as `LJ_TRERR_LINNER`
   - the next exact cut is `BC_JLOOP` side-entry / follow-on trace startup,
     not helper return-register mechanics, KEYINDEX restore ownership, or the
     old stale `ADDVV` resume path
4. Keep Stream B anchored on the new structured perf restamp:
   - `perf-kdz-20260323a`
   - `dispatch_trace` remains the only default perf gate
   - `family-status.json` is now the family promotion queue
   - `hotspots.json` is now the machine-readable hotspot list
5. Re-restamp local cross-arch perf control after the new macOS
   `MACOSX_DEPLOYMENT_TARGET` export hardening.
6. Close or explicitly rule out every exercised item still listed in the
   refreshed closure coverage report before widening the support claim beyond
   the current branch-level wording.

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
- The latest structured perf restamp is now:
  - [20260324T022735.280630Z-p89021 summary](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/20260324T022735.280630Z-p89021/summary.md)
  - [perf summary](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/20260324T022735.280630Z-p89021/perf/perf-summary.md)
  - [family status](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/20260324T022735.280630Z-p89021/perf/family-status.json)
  - [hotspots](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/20260324T022735.280630Z-p89021/perf/hotspots.json)
- Authoritative perf run artifacts:
  - JIT on:
    [20260324T022735.280630Z-p89021 summary](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/20260324T022735.280630Z-p89021/summary.md)
  - JIT off:
    [20260324T023224.602906Z-p91889 summary](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/20260324T023224.602906Z-p91889/summary.md)
- Current measured result:
  - the harness is working
  - the first `%` optimization slice is now landed and structurally restamped:
    positive constant divisors no longer fall through `lj_vm_modi` on the
    common traced int path
  - the post-fast-path structured `dispatch_trace` run now shows:
    - `numeric_loop/hot`: `0.032174s`
    - `side_exit_loop/hot`: `0.017588s`
    - `hotexit_loop/hot`: `0.009095s`
  - against the previous stamped baseline, that is:
    - `numeric_loop/hot`: about `1.37x` faster
    - `side_exit_loop/hot`: about `1.57x` faster
    - `hotexit_loop/hot`: about `1.23x` faster
  - first headline ratios:
    - post-fast-path baseline `jit=on` vs `jit=off`, `numeric_loop/hot`:
      about `15.54x` slower
    - post-fast-path baseline `jit=on` vs `jit=off`, `side_exit_loop/hot`:
      about `4.80x` slower
    - post-fast-path baseline `jit=on` vs `jit=off`, `hotexit_loop/hot`:
      about `1.62x` slower
- The broader perf catalog remains in-tree, but only the release-stable subset
  should gate the current perf stage until the remaining families are
  correctness-stable under native release measurement.
- `dispatch_trace` remains the only default structured perf gate.
- `iterator_table` remains the first focused perf-family probe.
- A clean native `kdz` probe from `HEAD` plus two local perf changes:
  - removal of the `BC_IITERL` debug helper call in
    [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc)
  - stopping the forced `hotloop=10,hotexit=10` override in
    [tests/s390x/perf/benchlib.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/benchlib.lua)
  materially improved iterator hot medians while keeping the current
  `%`/side-exit/soak correctness slice green.
- Clean native `kdz` results from
  `kdz:/root/luajit2-s390x/perf-wave-20260324b`:
  - `iterator_table pairs_sum/hot`: `0.371870s` vs older structured
    `0.528441s` (`1.42x` faster)
  - `iterator_table pairs_array_sum/hot`: `0.280803s` vs older structured
    `0.401933s` (`1.43x` faster)
  - `dispatch_trace numeric_loop/hot`: `0.032289s`
  - `dispatch_trace side_exit_loop/hot`: `0.017768s`
  - `dispatch_trace hotexit_loop/hot`: `0.009131s`
- The first `BC_ISNEXT` JLOOP-unpatch port built cleanly but did not materially
  change iterator timings, so it is not part of the active patch set.
- The next coherent native probe on top of that same safe patch set adds one
  more s390x-only runtime tuning change:
  - default `JIT_P_hotexit = 200` in
    [src/lib_jit.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lib_jit.c)
- On `kdz:/root/luajit2-s390x/perf-wave-20260324b`, that drops iterator hot
  medians again while preserving the current modulo/side-exit/soak checks:
  - `iterator_table pairs_sum/hot`: `0.045205s`
  - `iterator_table pairs_array_sum/hot`: `0.046697s`
  - repeated same-process `pairs()` timing falls to roughly
    `0.003175s -> 0.030375s` across warmup, versus the earlier
    `0.066625s -> 0.234235s`
  - dispatch stays near the current `%`-fast-path baseline:
    - `numeric_loop/hot`: `0.032363s`
    - `side_exit_loop/hot`: `0.017595s`
    - `hotexit_loop/hot`: `0.008959s`
- That makes the current best s390x iterator perf story:
  - remove the `BC_IITERL` helper overhead
  - stop forcing `hotexit=10` in perf probes
  - raise the native default `hotexit` threshold to reduce root-linked
    iterator side-trace churn
- The next Stream B target remains iterator / `next()` overhead on traced
  `pairs()` paths, specifically the remaining root-linked child-trace churn.
- As of the latest 2026-03-26 scratch classifier wave, that iterator story is
  split:
  - array-backed `pairs()` now has a coherent classifier path and a plausible
    narrow fix direction
  - hash-backed `pairs()` is still incorrect and is the real blocker for any
    iterator completion commit
  - the active hash owner is root `exit 4` in the second-half post-call
    `lj_vm_next` key-lane cluster, not the earlier array-style `exit 1`
    boundary
  - a newer numeric-key-only descendant split keeps that classifier stable:
    array still takes the deeper payload descendant with hot owner `0x427`,
    while hash falls back to the older `0x509`/`LLEAVE` shape instead of
    crashing
  - no iterator completion patch is currently promotable from this tree
- The newest array-side narrowing is more specific than the older
  `0x427`/descendant discussion:
  - `trace 5` is now proven to be an `LJ_TRLINK_INTERP` bridge, not the first
    real recovered loop child
  - `trace 6` is the first recovered loop child
  - a scratch-only `prime-interp` hotcount classifier can remove that bridge's
    extra `hotexit` budget stage while keeping the direct repro correct
  - that tightens the array family materially, but the steady-state owner is
    still the same legitimate `0x427` end split further down the chain
- The remaining perf families stay probe-only until they are release-stable on
  native `s390x`.
