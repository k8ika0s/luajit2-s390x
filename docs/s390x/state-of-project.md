# s390x State Of The Project

Last updated: 2026-03-31 16:41:00 PDT

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
- the old root-resume and pre-call-key bridge scaffolding has now been pruned
  out of the active source baseline
- the branch now has a checked-in restamp helper at
  [tools/s390x/restamp_iterator_perf.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/restamp_iterator_perf.py)
  so post-cleanup iterator numbers are captured under one fixed JIT-on contract
- the branch now also has a checked-in truth-pack helper at
  [tools/s390x/build_iterator_truth_pack.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/build_iterator_truth_pack.py)
  so the frozen baseline can be examined with the same clean rebuild path plus
  trace/exit counts, IR+mcode dumps, owner-selection probes, and counter
  availability checks
- a checkpoint branch now exists for the frozen implementation baseline:
  - `k8ika0s/s390x-jit-on-freeze-20260331`
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

### Freeze-point checkpoint and truth pack

The source now also matches the freeze-point docs more closely, and the branch
has one explicit measurement checkpoint:

- the parked root-resume and pre-call-key scaffolding was removed from
  [src/lj_jit.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_jit.h),
  [src/lj_snap.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_snap.c),
  [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c),
  and [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c)
- that cleanup is not a new performance claim
- local host build still succeeds after the cleanup
- the new checked-in restamp helper caught and forced one native build-floor
  fix first:
  - [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c)
    needed the old payload-path `oldpc` local restored after the cleanup
    removed bridge-only branches around it
- the frozen checkpoint branch is:
  - `k8ika0s/s390x-jit-on-freeze-20260331`
- fresh truth-pack and regression-screen numbers on that checkpoint are now:
  - `kdz` truth pack:
    - `pairs_sum/hot median=0.066259`
    - `pairs_array_sum/hot median=0.069155`
    - `-joff pairs_sum/hot median=0.005540`
    - `-joff pairs_array_sum/hot median=0.003716`
  - `zkd0` minimal screen:
    - `pairs_sum/hot median=0.104839`
    - `pairs_array_sum/hot median=0.097884`
- the new important decision from the truth pack is not the exact median twitch;
  it is the runtime shape:
  - steady-state trace and exit activity is still materially nonzero after
    warmup on all three focused loops
  - value-only hash:
    - `TRACE_START 10`
    - `TRACE_ABORT 9`
    - `TEXIT_COUNT 960000`
  - key-using hash:
    - `TRACE_START 10`
    - `TRACE_ABORT 9`
    - `TEXIT_COUNT 640000`
  - array value-only control:
    - `TRACE_START 11`
    - `TRACE_ABORT 10`
    - `TEXIT_COUNT 960000`
- that means the remaining iterator red is not yet just “expensive compiled
  loops”; residual exit behavior is still part of the active frontier

### Non-resume owner-selection read

The next pass after the rejected root-`ITERN` contract attempt was run as a
measurement-only owner-selection pack on clean `kdz`, with a smaller focused
array probe to keep the logs finite. That pass changes the branch direction:

- the root-`ITERN` resume-contract family stays closed from the current tree
- the next frontier is non-resume owner selection and steady-state control
  ownership

What is now mechanically proven:

- value-only hash and key-using hash both spend the hot steady-state seam in:
  - `S390X_JLOOP_EXIT phase=dispatch-original parent=1 exit=1 trace=1`
- on both hash loops, the first materially different owner candidate dies
  before child-link/runtime ownership even matters:
  - `trace 2` is created as a root trace with `startop=79`
  - `rec_loop_jit_root` fires immediately
  - `trace 2` aborts with `err=9` (`inner loop in root trace`)
- so for hash, the first useful owner candidate dies in
  [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c)
  inside `rec_loop_jit()`, not in later child-link promotion and not in
  runtime handoff

Array is different, but not yet good enough:

- array also spends the early hot seam in `dispatch-original` on root `trace 1`
- its first side trace does get farther than hash:
  - `trace 2` starts as a side trace on `parent=1 exit=1 root=1 startop=88`
  - it repeatedly aborts the nil path with `err=8`
  - once the payload path wins, it stops as:
    - `linktype=6`
    - `link=0`
    - `root=1`
- after that, later array descendants do stop, but they are still root-linked:
  - `trace 3`, `trace 4`, `trace 6` all stop with `linktype=1 link=1 root=1`

That means the current answer is:

- hash dies too early, in `rec_loop_jit_root`
- array survives farther, but still first becomes a root-linked descendant
  ladder rather than a stable non-root owner
- the next valid cut, if any, is one narrow non-resume owner-selection change
  that changes that exact outcome

One more corrected `kdz` rerun tightened that read:

- the checked-in finite owner-selection probe now does capture the intended
  recorder/runtime seam for hash
- value-only hash and key-using hash come back with the same first-side
  mechanism
- on both hash loops:
  - root `trace 1` still stops as `ITERN -> loop`
  - hot steady-state still spends `trace 1 exit 1` in `dispatch-original`
  - the first fresh root candidate is still `FORL` (`startop=79`) and still
    dies immediately in `rec_loop_jit_root`
  - the separate `trace 2 start 1/1` attempts still abort as
    `leaving loop in root trace`
- that makes the current hash seam more specific than “non-resume owner
  selection in general”:
  - it is the same first-side nil-descendant / unloaded-visible-key family
    already exposed by the earlier focused `ITERN_FOCUS` probes
  - not a distinct later child-link or runtime-owner family

So the next honest decision point is narrower:

- only reopen this owner-selection line if there is a cut that is genuinely
  different from the already rejected first-side lazy-key classifiers
- otherwise stop reopening this family and move to a different mechanism

## What Has Not Been Proven Yet

The branch is not done with iterator performance, but the open space is now
both smaller and clearer.

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

The refreshed post-cleanup owner map also did not expose a new target:

- value-only hash still shows:
  - hidden `KEYINDEX` load
  - carried-total `SLOAD`
  - helper `VLOAD #0` feeding the visible value lane directly
- key-using hash still adds visible key/type `SLOAD`
- array still carries numeric-key control loads
- shared `addov_rr_int_eq` is still the dominant cross-family payer

So the branch is now in a stricter state than before:

- the measurement contract is better
- the owner map is stable
- and the truth pack says steady-state exit behavior still exists

That means the next justified perf target is not “compiled throughput in the
abstract” and not “one more backend micro-optimization.” The next justified
target is the exact steady-state exit / side-trace ownership on the frozen
baseline, starting with value-only hash.

A fresh focused follow-up on `kdz` narrowed that one step further:

- steady-state value-only hash repeatedly takes `TRACE 1 exit 1`
- the matching `jit.dump=is` root trace shows that seam is in the early root
  snapshot region ahead of the visible value-lane add path
- this is an inference from snapshot ordering, but it points the next work at
  hidden control / root ownership around the `KEYINDEX` / `lj_vm_next` seam,
  not at the visible value lane and not at late backend lowering

The next focused read narrowed it again:

- in the measured hash phase, the one successful extra trace is `trace 2`, but
  it is a `stitch` trace, not the steady-state owner of the hot exit path
- all observed value-only hash texits still stay on `1:1`
- after the stitch trace exists, the next attempted owner is `trace 3`
- that `trace 3` still aborts as `inner loop in root trace`
- array differs in exactly the way that now matters:
  - its `exit 1` path does promote to a live side trace
  - almost all observed array texits are then on `5:1`, not on the root trace

So the next exact target is no longer “what exit is hot?” It is:

- can the first useful hash `exit 1` candidate be made to survive past
  `rec_loop_jit_root` into a materially different non-root owner shape, or is
  this family exhausted on the current mechanism?

The latest focused `kdz` probe narrows that one seam earlier:

- array and hash do not first diverge at `parent=2 exit=1`
- they already diverge at the first side trace from `trace 1 exit 1`
- array `trace 2` commits immediately on the payload path:
  - `S390X_ITERN_FOCUS site=after_next ... key_nil=0`
  - then `site=payload`
  - then `TRACE 2 stop -> loop`
- hash `trace 2` never commits that same first side loop, even when
  `rec_next_types()` reports a non-nil successor:
  - `S390X_ITERN_FOCUS site=after_next ... nextt=4 ... key_nil=1`
  - then immediately `site=nil`
  - then `TRACE 2 abort ... leaving loop in root trace`
- key-using hash behaves the same way on that first side seam:
  - even though the body later needs the visible key, `trace 2` still reaches
    `after_next ... key_nil=1`
  - then `site=nil`
  - then aborts
  - so the first-side decision happens before body demand can force visible-key
    materialization

That means the current live seam is now concrete:

- the hash first-side descendant is still using visible-key state to decide
  payload vs nil on a path where the helper result itself is often non-nil
- array escapes because its numeric visible key is already present
- hash falls into the nil-descendant path because the lazy visible-key policy
  leaves `ix.key` unloaded there

This is a diagnosis, not a new landing direction by itself. Earlier global
attempts to override that payload-vs-nil decision were already tested and
rejected on pinned `kdz`, so the next valid question is narrower:

- is there any semantic-preserving way to change first-side ownership at
  `trace 1 exit 1` without reopening the rejected lazy-key override families?

That question now has one concrete classifier result:

- first-side-only real key materialization on hash is a genuine new seam-local
  cut
- it keeps the root-path lazy-key policy unchanged
- on `kdz`, it turns both:
  - value-only hash `TRACE 2`
  - key-using hash `TRACE 2`
  into real `after_next -> payload -> stop -> loop` side traces

But it still fails the pinned `kdz` perf gate:

- `pairs_sum/hot median=0.068724`
- `pairs_array_sum/hot median=0.071057`

So the cut is structurally real but not promotable. That means the first-side
seam is now understood well enough to state the next constraint clearly:

- even fixing first-side hash ownership honestly is not enough by itself
- any surviving target has to remove additional steady-state cost beyond just
  turning `TRACE 2` into a loop

The newest follow-up narrows that again:

- under the same seam-local classifier, steady-state hash texits still do not
  transfer away from the root trace
- value-only hash on `kdz` still shows:
  - `TRACE_HIST abort:3=9,start:2=1,start:3=9,stop:2=1`
  - `TEXIT_HIST 1:1=960000`
- key-using hash shows the same ownership result:
  - `TRACE_HIST abort:3=9,start:3=9`
  - `TEXIT_HIST 1:1=640000`

So the active question is no longer just “can hash form the first side loop?”
It is now:

- why does steady-state ownership stay on root `1:1` even when the first side
  loop can be recorded?

That question is now mechanically narrower, with one correction:

- the root-child adoption path is still dormant by default on this tree
- [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c)
  only enables the owner-transfer experiments behind:
  - `LUAJIT_S390X_ROOT_PROMOTE_CHILD_LOOP`
  - `LUAJIT_S390X_ROOT_JLOOP_CHILD`
- but the earlier “forced owner path still stops `trace=2` as
  `LJ_TRLINK_INTERP`” read was from an accelerated `hotexit=10` classifier and
  is not the frozen-baseline surface

On the real frozen `hotexit=200` surface:

- array `trace=2` does stop as a loop child:
  - `link=2`
  - `linktype=2` (`LJ_TRLINK_LOOP`)
- `S390X_ROOT_PROMOTE_CHILD` fires and patches the root target from `1` to `2`
- the root now advertises `target_exec=2` and `target_resumechild=2`

But execution still does not hand off cleanly, because the root iterator owner
has no resume contract:

- `target_resumevalid=0`
- `target_resumepc=(nil)`
- `retop` remains the root `ITERN`
- so `JLOOP_EXIT` still takes `phase=dispatch-original` even after promotion

One deliberate root-`ITERN` contract attempt is now also closed:

- the env-gated experiment armed the root successfully:
  - `S390X_ROOT_ITERN_RESUME_ARM trace=1 ... resumevalid=1`
- but `trace 1 exit 1` still stayed in `phase=dispatch-original` long enough
  to promote `trace 2`
- after that, execution did not reach a stable child handoff
- instead it fell into a growing `exit 1` ladder of `BC_JMP` descendants
  (`trace 3`, `trace 4`, ...), then crashed on `kdz`

That corrected an earlier overclaim:

- the runtime already did have a dormant root iterator resume consumer in
  [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c):
  - `retop == BC_ITERN && targetT->root == 0 && targetT->resumevalid`
- so the problem was not “no consumer exists”
- the problem is that the end-to-end root contract still does not produce a
  safe owner handoff on the frozen surface

That makes the next exact target:

- treat the root-`ITERN` contract family as structurally interesting but
  currently rejected, and move back to non-resume owner selection unless a new
  contract design changes the ladder outcome itself

One more focused classifier made that split more concrete:

- the hash or array difference is not purely accidental runtime shape
- in [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c),
  payload descendants behind iterator `exit 1` are only widened automatically
  for numeric-key iterators
- that means array gets an explicit recorder-side exception that hash does not
- with `LUAJIT_S390X_ALLOW_ITER_DESC=1` on the frozen `kdz` baseline, hash does
  stop aborting the `2/1` descendant on `LJ_TRERR_LLEAVE`
- but the result is not a win shape:
  - hash starts producing a ladder of tiny root traces that all stop back into
    the same owner path
  - those new hash roots still link back to `trace 1`, not to the hash loop
    owner
  - array’s promoted roots, by contrast, link back to its loop owner
  - those traces still reload hidden `KEYINDEX`
  - they still call `lj_vm_next(tab, frame_keyindex)`
  - they still feed the carried total through `SLOAD #3` and `ADDOV`

So the current split is now clearer:

- yes, part of the hash/array ownership difference is enforced by recorder
  policy
- no, simply lifting that policy does not solve the steady-state cost
- the next justified target is therefore narrower still:
  - explain what prevents hash from promoting into a materially different owner
    body and owner link, rather than just cloning the same root work into a
    descendant ladder

One more source pass sharpened that seam again:

- the first owner link is chosen in
  [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c)
  by `rec_loop_jit()`, before the later child-link promotion logic in
  [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c)
  runs
- so the observed `link=1` vs `link=2` split is not caused only by later
  runtime child-link retargeting
- a focused direct probe with `LUAJIT_S390X_ALLOW_ITER_DESC=1` also shows that
  descendant permission by itself does not recreate the earlier array promoted
  owner behavior; both hash and array can collapse to the same `link=1`
  root-ladder shape under that classifier

That means the live question has shifted again:

- why does the default array path reach a different `rec_loop_jit()` stop
  target and trace family than hash?

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
- the candidate can be tested against the measured branch-tip `kdz` baseline with
  `zkd0` used only as a regression screen

The current measured branch-tip baseline is therefore the new operational
contract, even though it is worse than the older freeze-point reference.

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

1. Keep the documentation and measured branch-tip baseline aligned.
2. Treat Lane A as landed floor work and Lane B as the current promotable
   iterator baseline in source, while measuring against the new post-cleanup
   host restamp.
3. Use
   [tools/s390x/restamp_iterator_perf.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/restamp_iterator_perf.py)
   for any future authoritative iterator restamp, using:
   - one clean `kdz` perf repo
   - one clean `zkd0` regression repo
   - tracked-file sync only
   - direct `src/` rebuild only
   - same-host pinned `kdz` A/B as the policy signal
4. Keep the remaining perf discussion on root-trace and side-trace ownership,
   not bridge work, no-guard ideas, or late backend rewrites.
5. Use the truth pack and focused follow-up probes to answer the next exact
   question before any new code:
   - why does hash `exit 1` remain root-owned while array `exit 1` promotes to
     a live side trace?
6. Do not open a new perf patch family until either:
   - the current post-cleanup drift is explained, or
   - a genuinely new root-trace or side-trace storage/control materialization
     target is identified outside the reject pile

## Current Baseline Contract

Any future iterator experiment must beat these numbers and preserve their
interpretation.

- `kdz` machine type `8561` (`z15`)
  - checkpoint truth-pack `pairs_sum/hot median=0.060779`
  - checkpoint truth-pack `pairs_array_sum/hot median=0.066737`
- `zkd0` machine type `3906` (`z14`)
  - checkpoint screen `pairs_sum/hot median=0.132097`
  - checkpoint screen `pairs_array_sum/hot median=0.124149`

Same-harness `-joff` comparator on `kdz`:

- `pairs_sum/hot median=0.005574`
- `pairs_array_sum/hot median=0.004126`

Current distance to that comparator on `kdz`:

- hash hot: `0.060779` vs `0.005574`
  - `10.90x` slower
  - `+0.055205s`
- array hot: `0.066737` vs `0.004126`
  - `16.17x` slower
  - `+0.062611s`

Current delivery ladder on `kdz`:

- Restamp bar:
  - checkpoint still does not beat the older freeze-point reference
  - hash is `+1.61%` slower than `0.059818`
  - array is `+8.30%` slower than `0.061622`
- Recovery bar:
  - hash target `<= 0.056341`, current gap `+0.004438s`
  - array target `<= 0.059806`, current gap `+0.006931s`
- First real-results bar:
  - hash target `<= 0.050000`, current gap `+0.010779s`
  - array target `<= 0.055000`, current gap `+0.011737s`

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
- the branch now has a reproducible post-cleanup JIT-on measurement path, and
  that path says the current tip is slower than the older freeze-point
  reference

### Next 1-3 work sessions

- keep the docs in sync with the measured branch-tip state
- preserve the clean `kdz` and `zkd0` validation surfaces
- treat the current Lane A plus Lane B freeze point as the branch shipping
  position in source unless a genuinely new root-trace ownership idea appears
- decide whether the post-cleanup drift is real branch cost or a measurement
  artifact before opening another perf family
- avoid reopening any family already closed by the reject pile

### After that

There are only two realistic outcomes:

- a genuinely new root-trace storage/control ownership idea appears and beats
  the measured branch-tip baseline, or
- no such idea appears, and the branch moves forward with the proven Lane A
  plus Lane B stack as the current freeze point

## Where To Look Next

- Technical notebook:
  [findings.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/findings.md)
- Current perf map:
  [perf.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/perf.md)
- Validation workflow:
  [runbook.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/runbook.md)
