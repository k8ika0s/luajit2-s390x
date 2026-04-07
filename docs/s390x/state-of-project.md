# s390x State Of The Project

Last updated: 2026-04-07 10:52:47 PDT

This file is the current plain-language status page for the s390x bring-up.
It is intentionally current-state only. Historical experiment detail lives in
[findings.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/findings.md).

## Current State

- The envless first-enable `promotion_core` slice is now on the right side of
  `-joff` on both `kdz` and `zkd0`.
- The active branch-level blocker is still `mixed_noffi`.
- The retained exact `mixed_noffi` bundle is now:
  - `LUAJIT_S390X_DISPATCH_FORL_SKIP_JFORI=1`
  - `LUAJIT_S390X_AREF_BASE_ALLGPR=1`
  - `LUAJIT_S390X_IPAIRS_EXIT1_SKIP_BODY=1`
  - `LUAJIT_S390X_ROOT1_ITERL_REPLAY_TRIPLET=1`
  - `LUAJIT_S390X_ROOT1_ITERL_REPLAY_TRIPLET_LINK_PARENT=1`
  - default-on `SIDETRACE_TYPEINS_DONE`
  - the retained root-2 hash-bridge floor in
    [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc)
- Current authoritative deterministic host-pair restamp:
  - `kdz`: `mixed_noffi/mixed_loop/hot 0.013527` vs `-joff 0.003734`
  - `zkd0`: `mixed_noffi/mixed_loop/hot 0.015202` vs `-joff 0.004387`
- Exactness still holds on both hosts:
  - `/tmp/mixedprobe.lua -> RESULT 553416`
  - `/tmp/hash_value.lua -> HASH_VALUE 3000`
- `dispatch_trace` is exact on both hosts again under the retained dispatch
  gate:
  - `kdz`
    - `numeric_loop/hot 0.000162`
    - `side_exit_loop/hot 0.000391`
    - `hotexit_loop/hot 0.018571`
  - `zkd0`
    - `numeric_loop/hot 0.000469`
    - `side_exit_loop/hot 0.000613`
    - `hotexit_loop/hot 0.030236`
  - `numeric_loop` and `side_exit_loop` are now on the right side of `-joff`
  - `hotexit_loop` is exact again, but still materially red
- Focused mechanism shape on trusted `kdz` moved with the retained bridge win:
  - `TRACE_START 61`
  - `TRACE_STOP 5`
  - `TRACE_ABORT 56`
  - `TEXIT_COUNT 81575`
  - `TEXIT_HIST 1:1 200, 2:1 81375`
- The latest exact recorder-policy branch on that visible root-2 runway is
  closed:
  - `LUAJIT_S390X_ROOT2_NIL_DESC_DONE=1` reduced focused churn to
    `TRACE_START 6`, `TRACE_ABORT 1`
  - but same-host `kdz` perf regressed:
    - candidate `mixed_loop/hot 0.016000`
    - clean control `mixed_loop/hot 0.014724`
- Read:
  - the branch-level mixed floor moved right again on both hosts
  - the active seam is still the retained root-2 VM consume path, but the
    visible payer is now the hot `trace 2 exit 1` runway on the `pairs(map)`
    loop rather than the older `TEXIT_HIST 3:1` wall
  - that visible recorder-side runway is real, but it is not the next
    promotable cut point
  - the older root-1 producer-collapse frontier remains a guardrail, not the
    active blocker

## What Has Been Proven

- Lane A is stable enough to treat as the shipping build and stability floor.
- `promotion_core` is no longer the leading branch-level blocker.
- The root-1 replay-triplet work is a real narrowed reducer win and remains a
  necessary guardrail for `mixed_noffi`, but it is not the current active
  blocker.
- The dispatch `FORL` family is no longer a correctness blocker:
  - exact `BC_FORL` root families in [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c)
    now:
    - skip `JFORI` for:
      - `nsnap=4 nins=32787`
      - `nsnap=7 nins=32791`
      - `nsnap=9 nins=32795`
      - `nsnap=9 nins=32791`
      - `nsnap=8 nins=32793`
    - park the hotexit root set:
      - `nsnap=9 nins=32795`
      - `nsnap=9 nins=32791`
      - `nsnap=8 nins=32793`
  - that restores `dispatch_trace.lua` exactness on both hosts
- The cleaned harness contract is now the floor:
  - canonical nongit mirrors under `.../canon/repo`
  - tracked-file sync only
  - direct `src/` rebuild only
  - deterministic hot-first scale ordering in
    [tests/s390x/perf/benchlib.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/benchlib.lua)
- The retained mixed improvement is now VM-side:
  - the retained `JLOOP_EXIT` contract still reports
    `dispatch-original -> target=2 -> BC_ITERN`
  - but the visible focused topology has shifted to the hot root-2
    `trace 2 exit 1` runway with repeated `LLEAVE` side-trace attempts
  - the root-2 hash-only consume path in
    [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc)
    is where the last retained gain came from

## What Has Not Been Proven Yet

- `mixed_noffi` is still materially slower than `-joff`.
- `dispatch_trace/hotexit_loop` is still materially slower than `-joff`.
- The current retained root-2 bridge floor is not yet exhausted.
- The remaining room in the active mixed seam is now later than the recorder
  `LLEAVE` policy:
  - the retained hash-bridge tail itself
  - the later runtime handoff that still falls back through
    `dispatch-original -> target=2 -> BC_ITERN`
- The branch has not yet re-ranked the next major blocker after `mixed_noffi`.
  That should happen only after either:
  - the next retained mixed step lands, or
  - the root-2 bridge-tail lane is explicitly exhausted

## What The Freeze Point Means

The current branch should be treated as a shipping baseline plus one active
mixed frontier.

- Lane A: build and stability floor
- Lane B: retained mixed iterator throughput floor
- Lane C: parked research and historical reject pile

From here:

- do not reopen `promotion_core`, compare-fix, low32-home, filtered hotside,
  or other closed throughput defaults
- do not reopen root-1 producer-collapse archaeology as the primary frontier
- do not reopen root-2 replay-shortcut, descendant-chain, self-loop ladder,
  duplicate self-reentry, post-stop duplicate rewrite, or broad hotcount
  priming families
- keep exactly one active mixed probe family at a time

## What Is Parked

- root-1 stale-producer / `slot 13 -> KPRI -> TYPEINS` archaeology
- generic-for no-loop fences
- older bridge and continuation research
- broad recorder-side ownership rewrites that do not target the current
  retained root-2 VM consume path

## Next Steps

1. Keep the cleaned harness and docs aligned with the retained floor.
2. Treat the remote mirrors as disposable nongit mirrors and sync only through
   the tracked-file contract in
   [runbook.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/runbook.md).
3. Keep the active engineering frontier narrow:
  - root-2 bridge tail and later runtime handoff only for `mixed_noffi`
  - treat `dispatch_trace` as exact again and only reopen it for throughput
    cuts, not restore/correctness
  - no reopening of root-1 as a primary target
4. Use `kdz` same-host A/B as the policy signal and `zkd0` only after a real
   `kdz` win.
5. After the next retained `mixed_noffi` outcome, re-rank:
  - `mixed_noffi`
  - `dispatch_trace/hotexit_loop`
  - `vararg_paths/sum_loop`

## Current Baseline Contract

Any future `mixed_noffi` experiment must beat these numbers and preserve their
interpretation.

- retained mixed row:
  - `kdz`: `mixed_noffi/mixed_loop/hot 0.013527`
  - `zkd0`: `mixed_noffi/mixed_loop/hot 0.015202`
- exactness gates:
  - `/tmp/mixedprobe.lua -> RESULT 553416`
  - `/tmp/hash_value.lua -> HASH_VALUE 3000`
- focused retained mechanism guard on `kdz`:
  - `TRACE_START 61`
  - `TRACE_STOP 5`
  - `TRACE_ABORT 56`
  - `TEXIT_COUNT 81575`
  - `TEXIT_HIST 1:1 200, 2:1 81375`
- focused root-1 guardrail on `kdz`:
  - `/tmp/ipairs_only_probe.lua -> RESULT 576000`
  - `TRACE_START 5`
  - `TRACE_ABORT 3`
  - `TEXIT_COUNT 341`

## Updated Timeline

### Now

- `promotion_core` is broadly green and out of the leading slot.
- `mixed_noffi` is the active branch-level blocker.
- the retained floor is the root-2 hash-bridge path
- the next honest target is the later root-2 runtime handoff / bridge-tail
  cost, not a broader branch-wide rewrite

### After The Next Mixed Step

- If the next root-2 bridge-tail family wins on `kdz` and survives `zkd0`,
  restamp the retained matrix and keep burning down `mixed_noffi`.
- If the next root-2 bridge-tail family closes without a win and the lane is
  exhausted, re-rank the remaining red rows in this order:
  1. `mixed_noffi`
  2. `dispatch_trace`
  3. `vararg_paths/sum_loop`
  4. `iterator_table`
  5. `mixed_ffi` and `ffi_cdata`

## Where To Look Next

- Current perf scoreboard:
  [perf.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/perf.md)
- Technical notebook:
  [findings.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/findings.md)
- Validation and sync contract:
  [runbook.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/runbook.md)
