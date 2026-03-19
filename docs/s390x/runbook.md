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
  - `remote/bootstrap/*`
  - `remote/steps/<suite>/<variant>/*`
  - `binaries/<variant>/`

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

## Notes

- The harness never edits the remote tree by hand. It always syncs from the
  local repo and collects artifacts back.
- The repo Perl tests now use the local [t/TestLJ.pm](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/t/TestLJ.pm)
  harness and core Perl modules only. Remote bootstrap no longer depends on
  CPAN packages for the existing `t/*.t` coverage.
- If a run is intentionally preserved on the host for manual inspection, use
  `--keep-remote`.
