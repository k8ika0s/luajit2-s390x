# s390x Status

This branch carries the in-progress native s390x bring-up for this LuaJIT
tree. The local checkout is the source of truth for code changes. Native IBM Z
validation is authoritative only when it is reproduced from the current remote
sync loop.

## Trustworthy Baseline

- The current authoritative native worktree is on `kdz`:
  - `/root/luajit2-s390x/rsync-loop-20260321`
- Local edits are synced there with selective `rsync`.
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
- Focused matrix spot-checks are also green on the current tree:
  - `zkd0` passes the current hot regression set for `side_exit`, vararg,
    BE helpers, soak, `t/iter.t`, and `t/isarr-jit.t`
  - `kdz` passes a focused clang JIT build plus the same hot regression set
  - `zkd0` also passes the same focused clang JIT spot-check set
  - `kdz` passes the current `-DLUAJIT_DISABLE_FFI` corner for:
    - `t/isarr-jit.t`
    - `t/iter.t`
- The current harness hardening work is no longer about JIT correctness on the
  tested surface. It is about making the driver’s remote sync/collection path
  robust on banner-printing hosts so the structured artifacts match the now
  green manual native matrix.

## Current Next Actions

1. Keep `vararg_trace`, the direct-exit `SAVE_L` path, `side_exit.lua`, and
   `thread.exdata()` in the focused regression set so these fixes do not
   silently regress.
2. Finish hardening the driver transport so repo sync and artifact collection
   work reliably on `kdz`/`zkd0` even with login banners.
3. Once the harness transport is re-stamped, widen matrix coverage only where
   it exercises a meaningfully different surface.
4. Return to the remaining iterator and hot-exit convergence quality work from
   that revalidated matrix baseline instead of reopening the earlier resolved
   blockers.

## Detail Links

- Architecture contract:
  [docs/s390x/contract.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/contract.md)
- Native findings and run history:
  [docs/s390x/findings.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/findings.md)
- Bring-up workflow and artifact guide:
  [docs/s390x/runbook.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/runbook.md)
