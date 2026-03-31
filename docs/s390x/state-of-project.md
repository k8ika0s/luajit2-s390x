# s390x State Of The Project

Last updated: 2026-03-31 09:12:31 PDT

This file is the current plain-language status page for the s390x bring-up.
It should be updated in place. Older status snapshots should be removed rather
than appended, so this file always reflects the latest known state.

## Current State

The project is no longer in a broad “is s390x fundamentally stable?” phase.
That part is far enough along that the work is now split into three separate
lanes, and the current branch tip should be treated as the implementation
freeze point:

- Lane A: build and stability
- Lane B: promotable recorder-side iterator performance
- Lane C: parked bridge and continuation research

That split matters. The branch spent a long time chasing iterator problems that
turned out to be a mix of real backend bugs, useful classifier work, and
non-causal probe effects. The current state is cleaner:

- the major late crash is now understood as a real s390x ABI and codegen bug
  in dispatch helper errno handling
- the current iterator wins are recorder-side and measurable
- the older bridge and continuation work remains useful research, but it is no
  longer the main performance frontier
- the default branch posture from here is to ship Lane A plus Lane B unless a
  genuinely new root-trace storage/control materialization target appears

## What Has Been Proven

### Lane A: build and stability

The current clean s390x floor is real and validated:

- `ERRNO_SAVE` and `ERRNO_RESTORE` were wrong for s390x and are now hardened in
  [src/lj_dispatch.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_dispatch.h)
- `IRSLOAD_KIDX_NUMKEY` is restored in
  [src/lj_ir.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_ir.h)
- clean s390x rebuilds are JIT-enabled by default again in
  [src/lj_arch.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_arch.h)

That floor was revalidated on both current native hosts:

- `kdz:/root/luajit2-s390x/perf-clean-20260330/repo`
  - machine type `8561` (`z15`)
- `zkd0:/root/luajit2-s390x/perf-clean-20260330/repo`
  - machine type `3906` (`z14`)

Both hosts now come back with `jit.status() == true` on a clean default build,
and the current long-run iterator smoke cases remain green.

### Lane B: recorder-side iterator performance

There is now a real promotable iterator baseline in
[src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c).
It has four parts:

- array visible numeric key comes from successor index:
  - `HIOP(trvk) - 1`
- non-array visible key stays lazy
- hash table live-in is trusted and read-only
- non-array value lane is seeded from `ix.val`

Those changes matter because they removed real steady-state work:

- array no longer redoes the old post-call numeric key-lane materialization
- hash no longer eagerly materializes the visible key when the loop does not
  use it
- hash no longer pays the old table-slot live-in typecheck
- hash root traces no longer frame-source the visible value slot

Current pinned `kdz` hot medians on the frozen baseline are:

- `pairs_sum/hot median=0.059818`
- `pairs_array_sum/hot median=0.061622`

That is still much slower than the `-joff` baseline, but it is materially
better than the older iterator baselines that were dominated by avoidable
recorder-side payers.

## What Has Not Been Proven Yet

The branch is not done with iterator performance, but the open space is now
very small.

The current remaining performance red is:

- shared `addov_rr_int_eq` still dominates as the biggest cross-family payer
- value-only hash still carries the hidden `KEYINDEX` load cluster plus the
  carried-total `SLOAD`
- key-using hash adds a visible key/type `SLOAD`
- array still carries numeric-key control loads

What is now known is that the last allowed accumulator-family pass was tried
and rejected again. A final exact pre-unroll preload attempt did not actually
change the root trace shape: the carried total still came through as
`int SLOAD #3` feeding `ADDOV`. The one allowed backend classifier was also
negative. So there is no justified accumulator-family or simple late-backend
follow-up left from the current mechanism.

## What The Freeze Point Means

The current branch should be operated as a shipping baseline, not as an open
ended lab branch.

- Lane A is done enough to ship as the build and stability floor.
- Lane B is done enough to ship as the promotable recorder-side iterator
  baseline.
- Lane C remains parked and should not leak back into active perf work.

From here, new iterator work only reopens if all of these are true:

- a named remaining payer exists
- there is a direct structural proof target in raw IR or low-noise logs
- the idea is not already in the reject pile
- the candidate can be tested against the frozen pinned `kdz` baseline with
  `zkd0` used only as a regression screen

## What Is Parked

The following work is intentionally parked and should not be mixed back into
the active performance branch unless the clean no-probe baseline regresses
semantically:

- bridge and continuation experiments
- hidden-control carry experiments
- `TRACE 2` churn elimination as a performance proxy
- `KEYINDEX` no-guard variants
- full-lazy iterator collapse
- direct `KEYINDEX` tag-word compare
- backend guard dedup experiments
- accumulator-to-`num` variants that still keep the loop-unroll `int.num`
  check
- exact accumulator preloads that still leave the root trace on `int SLOAD`
  plus `ADDOV`
- backend `AR/SR` and `AGFR/CGFR` rewrite ideas

Those ideas are not merely “unexplored.” They were tried and either broke
semantics, regressed authoritative host performance, or changed trace shape
without producing a promotable win.

## Next Steps

The immediate next steps are operational, not exploratory:

1. Keep the documentation and frozen baseline aligned.
2. Treat Lane A as landed floor work and Lane B as the current promotable
   iterator baseline.
3. Restamp the owner map from the frozen baseline when needed, using:
   - one clean `kdz` perf repo
   - one clean `zkd0` regression repo
   - tracked-file sync only
   - direct `src/` rebuild only
   - same-host pinned `kdz` A/B as the policy signal
4. Keep the remaining perf discussion on root-trace storage/control ownership,
   not bridge work, no-guard ideas, or late backend rewrites.
5. Only reopen perf work if a genuinely new root-trace storage/control
   materialization target is identified.

## Current Baseline Contract

Any future iterator experiment must beat these numbers and preserve their
interpretation.

- `kdz` machine type `8561` (`z15`)
  - `pairs_sum/hot median=0.059818`
  - `pairs_array_sum/hot median=0.061622`
- `zkd0` machine type `3906` (`z14`)
  - `pairs_sum/hot median=0.093881`
  - `pairs_array_sum/hot median=0.087945`

Current owner map contract:

- shared `addov_rr_int_eq` is the dominant cross-family payer
- value-only hash still pays hidden `KEYINDEX` plus carried-total `SLOAD`
- key-using hash adds visible key/type `SLOAD`
- array still pays numeric-key control loads
- hash does not frame-source the visible value lane

## Updated Timeline

### Now

- Lane A is proven and should be treated as stable floor work.
- Lane B has a promotable four-piece recorder baseline.
- Lane C is parked research.

### Next 1-3 work sessions

- keep the docs in sync with the frozen branch state
- preserve the clean `kdz` and `zkd0` validation surfaces
- treat the current Lane A plus Lane B freeze point as the branch shipping
  position unless a genuinely new root-trace ownership idea appears
- avoid reopening any family already closed by the reject pile

### After that

There are only two realistic outcomes:

- a genuinely new root-trace storage/control ownership idea appears and beats
  the frozen baseline, or
- no such idea appears, and the branch moves forward with the proven Lane A
  plus Lane B stack as the current freeze point

## Where To Look Next

- Technical notebook:
  [findings.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/findings.md)
- Current perf map:
  [perf.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/perf.md)
- Validation workflow:
  [runbook.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/runbook.md)
