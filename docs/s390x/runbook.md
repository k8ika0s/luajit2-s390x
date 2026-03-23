# s390x Bring-Up Runbook

## Purpose

This runbook explains how to drive the staged native s390x bring-up loop and
how to read the resulting artifacts.

## Primary Workflow

1. Edit locally.
2. Run the smallest remote validation first:
   - `python3 tools/s390x/driver.py --stage <stage> --suite smoke --compiler gcc --mode debug --host auto`
3. If smoke passes, rerun with the stage default suites:
   - `python3 tools/s390x/driver.py --stage <stage> --suite all --compiler gcc --mode debug --host auto`
4. If the stage gate passes, rerun the stage in release mode:
   - `python3 tools/s390x/driver.py --stage <stage> --suite all --compiler gcc --mode release --host auto`
5. For matrix or performance work, use the later stages directly.
6. Keep the current remote worktree disposable. If the tree is contaminated or
   manually edited, throw it away and restamp from a fresh run-id instead of
   repairing it in place.

## Closure Workflow

Use the `closure` stage when the goal is a branch-level native `s390x`
support claim rather than a narrower matrix slice.

Recommended order:

1. Local coverage audit only:
   - `python3 tools/s390x/driver.py --stage closure --suite coverage_audit --compiler gcc --mode debug --jit on`
2. Full closure gate on `kdz`:
   - `python3 tools/s390x/driver.py --stage closure --suite all --compiler gcc --mode release --jit on --host kdz`
3. Second-host closure spot check on `zkd0`:
   - `python3 tools/s390x/driver.py --stage closure --suite all --compiler gcc --mode debug --jit on --host zkd0`

Keep these direct native reduced probes available while closure is being
restamped, since they catch the last soak/runtime regressions much faster than
the full stage:

- `/tmp/s390x_keep_worker.lua`
- `/tmp/s390x_mode0_only.lua`
- `tests/s390x/soak/trace_gc_churn.lua`

The closure stage now includes:

- `smoke`
- `pure_lua`
- `ffi_abi`
- `callbacks`
- `jit_core`
- `jit_loops`
- `jit_be`
- `soak`
- `coverage_audit`
- `downstream`
- `perf_bench`

The `pure_lua` suite in closure mode widens to a full native `prove -v t/*.t`
lane. `coverage_audit` and `downstream` run locally through the driver, while
the existing correctness suites still use native remote execution.

If you need interactive `-jv` or `-jdump` runs from the repo root, remember to
add `src/jit/*.lua` to `LUA_PATH`, for example:

- `LUA_PATH="./src/?.lua;./src/?/init.lua;;" ./src/luajit -jv <script.lua>`

Without that, repo-root `-jv` or `-jdump` probes can look like “no JIT output”
even on a correctly JIT-enabled s390x build.

## Gateway And Kong Demo Workflow

Use these when the goal is product-shaped proof instead of harness matrix work.

1. OpenResty leadership demo:
   - `demo/openresty/run_demo.sh`
2. Kong staged runtime demo:
   - `demo/kong/run_kong_demo.sh`
3. Kong minimal runtime probe against an existing remote root:
   - `demo/kong/run_kong_require_probe.sh`

Important current defaults:

- `demo/kong/run_kong_demo.sh` now defaults to the stronger proof path:
  - `KONG_FORCE_JIT_OFF_IN_NGINX=0`
  - `KONG_NGINX_RUN_AS_ROOT=1`
- Current Kong startup bridge option:
  - `KONG_DELAYED_JIT_ON_IN_NGINX=1`
  - `KONG_DELAYED_JIT_ON_SECS=3`
  - this keeps JIT off through `Kong.init()` and `Kong.init_worker()`, then
    re-enables it from a delayed worker timer after startup settles
- Kong demo runs now also derive deterministic per-run proxy/admin ports and
  stop nginx on exit, which avoids cross-run `8000/8001` collisions during
  closure restamps
- The old bridge mode is still available as a fallback:
  - `KONG_FORCE_JIT_OFF_IN_NGINX=1`
- Use the staged probe before full `kong start` whenever the LuaJIT runtime
  behavior has changed.

## Host Selection

- Primary host: `kdz`
- Automatic fallback: `zkd0`
- `--host auto` prefers `kdz` and records any failover in `manifest.json`.

## Artifact Layout

- Local run root: `artifacts/s390x/<run-id>/`
- Latest symlink: `artifacts/s390x/latest`
- Remote run root: `/root/luajit2-s390x/<run-id>/`
- Important files:
  - `manifest.json`
  - `commands.ndjson`
  - `summary.md`
  - `stage-report.md`
  - `failures.json`
  - `metadata/git-sha.txt`
  - `metadata/dirty.patch`
  - `coverage/*`
  - `downstream/*`
  - `remote/bootstrap/*`
  - `remote/steps/<suite>/<variant>/*`
  - `binaries/<variant>/`

## Current Transport Rules

- The driver no longer relies on raw `rsync` for normal structured runs.
- Repo sync now uses a tracked-files-only tar stream over SSH.
- The tar stream strips macOS metadata and does not include untracked local
  scratch files.
- Artifact collection still uses tar-over-ssh.
- Optional binary collection is best-effort. Missing optional outputs should
  not be treated as the front-most failure if the step itself passed.

## Failure Triage

- Start with `summary.md` and `stage-report.md`.
- Open the failing step directory under `remote/steps/...`.
- Read, in order:
  - `command.txt`
  - `stdout.log`
  - `stderr.log`
  - `metadata.json`
  - `current_test.txt`
  - `diagnostics/*`
- If a crash occurred, inspect:
  - `diagnostics/core-files.txt`
  - `diagnostics/*.gdb.txt`
  - `diagnostics/readelf.txt`
  - `diagnostics/nm.txt`
  - `diagnostics/objdump.txt`
- If a step timed out, inspect `metadata.json` first. The harness uses a default
  remote timeout of 1800 seconds unless `S390X_TIMEOUT_SEC` is overridden.
- For the product demos, also inspect:
  - `logs/stage-current.txt`
  - `logs/stage-history.txt`
  - `logs/require-probe.log`
  - `logs/require-probe-last-ok.txt`
  - `logs/kong-prepare.txt`
  - `logs/kong-nginx-start.txt`
  - `logs/coredumpctl-info.txt`

## Remote Trust Reset

- If the remote validation tree has been touched manually, stop using it as an
  authority immediately.
- Recreate a fresh remote worktree from the local source of truth using a new
  driver run-id whenever possible.
- Do not patch remote source files interactively inside tmux for substantive
  edits unless the normal SSH transport is unavailable.
- The helper
  [tools/s390x/tmux_patch_sync.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/tmux_patch_sync.py)
  emits tmux-safe chunked `printf` commands for environments where direct local
  SSH transport is unavailable.
- Preferred reset sequence:
  1. Start a fresh run with a new run-id, or create a fresh disposable remote
     workdir if you are doing a manual verification pass.
  2. Sync from the local repo source of truth.
  3. Rebuild and rerun the smallest focused native reproducer first.
  4. Only after that passes, widen back to the staged harness gate.
- Current known-good example:
  - host: `kdz`
  - worktree: `/root/luajit2-s390x/clean-loop-20260321`
  - second host spot-check tree: `/root/luajit2-s390x/spotcheck-20260321`

## Stage Rules

- `contract`: the driver checks `docs/s390x/contract.md` before allowing later
  stages to proceed.
- `interp`: keep `LJ_ARCH_NOJIT` in place and focus on boring correctness.
- `ffi-call`: require generated ABI oracle coverage.
- `callback-unwind`: require callback stability and unwind hygiene.
- `jit-bringup`: remove `LJ_ARCH_NOJIT` only after the earlier gates are green.
- `jit-correctness`: expand coverage to the full exercised BE and JIT surface.
- `matrix`: run the wider compiler and build-style matrix.
- `perf`: treat tuning as performance-only, never as correctness.
- `closure`: run the final support-claim gate, including source audit,
  downstream product demos, and the bounded `dispatch_trace` perf regression
  check.

## Performance Stage

- The `perf` stage now includes:
  - `smoke`
  - `soak`
  - `perf_bench`
- `perf_bench` emits structured benchmark JSON plus per-benchmark raw logs.
- The primary perf artifacts are:
  - `perf/benchmarks.json`
  - `perf/comparisons.json`
  - `perf/perf-summary.md`
- Structured perf runs use the same native remote build/test flow as the
  correctness stages.
- Use the `perf` stage only after the matching matrix slice is already green.
- Cross-arch control builds are local and informative only. They are not
  treated as correctness gates.

## Notes

- The harness never edits the remote tree by hand. It always syncs from the
  local repo and collects artifacts back.
- The local worktree may contain untracked scratch files from earlier analysis.
  The current tracked-files-only sync path intentionally excludes them from
  structured remote runs.
- The repo Perl tests now use the local [t/TestLJ.pm](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/t/TestLJ.pm)
  harness and core Perl modules only. Remote bootstrap no longer depends on
  CPAN packages for the existing `t/*.t` coverage.
- If a run is intentionally preserved on the host for manual inspection, use
  `--keep-remote`.
