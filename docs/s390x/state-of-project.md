# s390x State Of The Project

Last updated: 2026-03-31 22:45:00 PDT

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
- the iterator lane is now frozen at the current checkpoint unless a genuinely
  new seam appears outside the reject pile
- the first dispatch/side-exit loop-clone queue has now also been classified
  and closed on the current mechanism
- the follow-up dispatch-adjacent side-exit pass did not expose a second seam;
  the branchy loops collapse back to the same closed loop-clone ladder
- the next queued performance workstream is broader JIT throughput work
  unless a new helper-boundary storage/materialization seam can be named first
- the broader-throughput queue is now explicit instead of implied:
  - first target:
    [tests/s390x/perf/vararg_paths.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/vararg_paths.lua)
  - second target:
    [tests/s390x/perf/bitops_mix.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/bitops_mix.lua)
  - [tests/s390x/perf/mixed_noffi.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/mixed_noffi.lua)
    stays out of this queue because it would re-entangle iterator behavior via
    `pairs()`
- the branch now has a checked-in broader-throughput helper at
  [tools/s390x/build_throughput_truth_pack.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/build_throughput_truth_pack.py)
  so broader JIT-on families can be restamped under the same tracked-file,
  direct-`src/` rebuild contract instead of ad hoc local runs
- the first native `kdz` pass on that new queue is now enough to name the next
  live family:
  - `vararg_paths` is not just mildly red; it is a real JIT-on cliff,
    especially at `sum_loop/hot`
  - the focused hot trace-count probes on clean `kdz` are not yet stable
    enough to serve as a full truth-pack completion path, so the next work is
    narrowed to traced hot vararg loop behavior, not generic broader-throughput
    sweeping
  - the next exact tasks inside that family are now:
    1. isolate traced hot `sum_loop`
    2. compare it against `retlast_loop` and `retconst_loop`
    3. determine whether the extra payer is:
       - repeated `select()` control,
       - vararg value access/materialization,
       - or exit churn in the traced hot path
- that first comparison is now far enough along to narrow the live seam again:
  - all three vararg cases show the generic loop-clone pattern on `kdz`
  - but only `sum_loop` adds an extra caller-side handoff / return chain on
    top of the inner traced vararg loop
  - so the next target is no longer “generic vararg throughput”
  - it is nested vararg summation plus caller return/handoff around
    `sum(...)`, not the already-shared base loop-clone behavior
- that seam is narrower again after the reduced clean-host handoff probes:
  - the extra `sum_loop` red is not a reopened lower-frame return bug
  - the reduced `kdz` `LUAJIT_S390X_RECRET_LOG=1` probes show:
    - `sum_loop`, `retlast_loop`, and `retconst_loop` all return through
      `lua_intrace_return`
    - none of them hit `lua_lower_frame_retf`
    - none of them hit `lua_root_lower_frame_lleave`
  - what singles `sum_loop` out is structural instead:
    - it builds a separate caller handoff trace family (`TRACE 2`, later
      `TRACE 7`) on top of the inner callee loop family
    - `retlast_loop` and `retconst_loop` stay inside the shared single-family
      loop-clone pattern
  - the next exact target is therefore:
    - caller/callee handoff around a traced Lua callee loop
    - not generic return lowering
    - and not another lower-frame return fix family
  - one candidate inside that seam is now also ruled out on clean `kdz`:
    - a new focused `LUAJIT_S390X_FUNCJIT_LOG` pass was silent on the reduced
      `sum_loop` and `retlast_loop` probes
    - so the extra `sum_loop` family is not being born at `rec_func_jit()` /
      compiled-callee entry
    - the next exact target moves later:
      - caller-side re-entry after `lua_intrace_return`
      - before the caller path settles into the separate `TRACE 2` / `TRACE 7`
        family
  - that caller-side re-entry seam is now narrower again after two more clean
    `kdz` classifiers:
    - reduced traceinfo probes show `sum_loop` and `retlast_loop` split before
      any later stitch noise matters:
      - `sum_loop`
        - `trace 1`: callee vararg scan loop in `sum(...)`
        - `trace 2`: root trace at caller setup
        - `trace 7`: later root trace in the same caller family
      - `retlast_loop`
        - `trace 1` onward: caller loop family only
        - later stitch traces exist, but only after that caller loop family is
          already stable
    - a focused clean-host `LUAJIT_S390X_CALLHANDOFF_LOG` classifier was then
      synced and rebuilt on `kdz`
    - that logger stayed silent while `sum_loop` still formed `TRACE 2` /
      `TRACE 7`
    - so the extra `sum_loop` root-family traces are not being born in the
      generic `trace_stop(... BC_CALL/BC_CALLM/BC_ITERC ...)` or
      `lj_trace_stitch()` handoff path either
  - one more corrected clean-host classifier changed the read again:
    - the earlier start-log pass had used the wrong env name
    - the real gate is `LUAJIT_S390X_TRACE_START_LOG`
    - with that corrected gate on clean `kdz`:
      - `sum_loop`
        - `trace 1` starts as a root at the callee loop in `sum(...)`
        - `trace 2` starts as a second root at the caller site
      - `retlast_loop`
        - `trace 1` starts as a root at the caller site
        - later traces are then side growth from that caller root
    - so `sum_loop` is not creating `trace 2` through a hidden post-return
      root-link decision
    - it is splitting into two independently hot root sites:
      - callee vararg scan first
      - then caller arithmetic/call site
  - that leaves one honest live target in this family:
    - why the caller root in `sum_loop` (`TRACE 2`) stops `-> 1` and then
      keeps feeding the inner-loop ladder, instead of settling into the same
      stable caller-loop family shape seen in `retlast_loop`
  - the reduced clean-host caller-root dumps now make that target concrete:
    - `retlast_loop`
      - caller root already contains the outer-loop arithmetic and loop-carried
        PHIs
      - it stops as a loop immediately
    - `sum_loop`
      - caller root stops after callee identity/setup guards
      - it does not yet materialize the caller add or outer-loop carried total
      - so it stops `-> 1` before the caller body becomes a real loop owner
  - the next exact target is therefore:
    - why the caller root for `sum_loop` stops before caller-body
      materialization after the traced callee call, while `retlast_loop`
      reaches the caller add/loop path in the root itself
  - the reduced clean-host recstop logs now narrow that seam one step further:
    - `retlast_loop`
      - caller family reaches `rec_loop_jit()` on the caller loop seam
      - side traces log `S390X_RECLOOP ... ev=2 ...` and stop `-> loop`
    - `sum_loop`
      - caller roots (`TRACE 2`, later `TRACE 7`) stop `-> 1` before any
        `S390X_RECLOOP` appears on the caller path
      - the callee loop family still reaches `rec_loop_jit()`, but the caller
        family does not
    - so the live seam is now earlier than `rec_loop_jit_root`
    - the next exact target is:
      - which recorder/return condition on the traced-callee return path keeps
        `sum_loop` caller roots from reaching the caller loop op at all, while
        `retlast_loop` reaches that loop seam and stabilizes as a caller loop
        family
  - the reduced clean-host return logs and the existing recstop dump correct
    that again:
    - `sum_loop` caller root is not stopping on a post-return branch in
      `lj_record_ret()`
    - the only observed return branch on the focused clean-host probe is
      `lua_intrace_return`, and it belongs to the inner `select()` fastfunc
      work inside the callee loop
    - the decisive stop is earlier:
      - `TRACE 2` starts at the caller site
      - enters `sum(...)`
      - then stops `-> 1` at the callee `JFORI` path, with `pc` already moved
        to the callee loop body start (`GGET`, previous op `JFORI`)
      - so the caller root is linking straight into the already-compiled callee
        loop trace before caller add / outer-loop PHIs ever materialize
    - `retlast_loop` has no nested callee loop seam there, so its caller root
      reaches caller add / loop materialization directly
  - the next exact target is therefore corrected again:
    - explain whether the live vararg cliff is simply the normal root-stop path
      for a caller trace that enters an already-compiled nested callee loop via
      `BC_JFORI`, or whether there is still a narrower recorder ownership seam
      above that boundary
  - code reading now matches the artifact:
    - [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c#L3597)
      handles `BC_JFORI`
    - when `rec_for()` says the loop is entered, the root trace takes:
      `lj_record_stop(J, LJ_TRLINK_ROOT, bc_d(...))`
    - that is exactly the observed `sum_loop` `TRACE 2 ... -> 1` stop with
      `prevop=JFORI`, `pc` moved to the callee loop body start, and `link=1`
  - so the current live question is no longer “which hidden return branch is
    doing this?”
  - it is:
    - whether caller-root ownership across a call into an already-compiled
      nested callee loop is a real open optimization surface on this mechanism,
      or just the normal boundary we should stop fighting here
- that branch decision is now clean enough to move the queue:
  - `sum_loop` still explains the red vararg cliff, but its front-most split is
    the normal root `BC_JFORI -> existing loop` stop into an already-compiled
    nested callee loop
  - that is not the next grounded local optimization target on the current
    mechanism
  - the broader-throughput queue therefore moves forward to
    [tests/s390x/perf/bitops_mix.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/bitops_mix.lua)
- first clean `kdz` truth-pack read for `bitops_mix`:
  - artifact root:
    - [20260331-kdz-bitops_mix-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260331-kdz-bitops_mix-truth-pack)
  - current hot medians:
    - `mix_bits/hot`
      - JIT-on `0.007645`
      - `-joff` `0.002099`
      - gap `+0.005546s`
      - ratio `3.64x`
    - focused hot:
      - JIT-on `0.007662`
      - `-joff` `0.002123`
      - gap `+0.005539s`
      - ratio `3.61x`
  - runtime classification:
    - `TRACE_START 0`
    - `TRACE_STOP 0`
    - `TRACE_ABORT 0`
    - `TEXIT_COUNT 0`
    - `compiled-body-dominated`
  - this is the first clean non-iterator, non-dispatch, non-helper family in
    the current queue that is materially red without any live exit churn
- focused backend audit now names the next exact throughput seam:
  - artifact root:
    - [20260401-kdz-bitop-log-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-bitop-log-audit)
    - [20260401-kdz-bitop-log-audit-v2](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-bitop-log-audit-v2)
  - the hot chain is all integer bitops:
    - `band`, `bxor`, `bor`, shifts, rotates, `bswap`, `bnot`
    - every logged op is `IRT_INT`
    - no helper-call seam and no exit seam show up in this classifier
    - the refined producer log shows the hot chain mostly consumes earlier
      bitop results:
      - `logic` ops repeatedly take prior `logic`, `shiftk`, `brolk`, `bswap`,
        and `bnot` producers as inputs
      - only a small base set comes straight from the original integer source
        or loop-carried arithmetic
  - the s390x backend path is the interesting part:
    - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h#L1428)
      `asm_bitop_logic()`
    - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h#L1443)
      `asm_bitshift()`
    - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h#L1490)
      `asm_brot()`
    - all of those paths currently run through `asm_bnorm32()`
  - that makes the next exact target:
    - prove whether `asm_bnorm32()` is being re-applied across an already
      `IRT_INT` bitop producer chain and is the real compiled-body payer in
      `bitops_mix`
    - and only then decide whether one narrow normalization-state / int32-home
      experiment is justified
  - focused `asm_bnorm32()` audit now says the chain really is paying repeated
    normalization on prior bitop results:
    - artifact root:
      - [20260401-kdz-bnorm-log-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-bnorm-log-audit)
    - clean `kdz` focused run logged `84` `S390X_BNORM` sites
    - the split is exact:
      - `42` sites normalize unary/shift nodes fed directly from the original
        int source or loop-carried arithmetic
      - `42` sites normalize binary chain nodes where both inputs are already
        prior bitops
    - so the next exact target is no longer “is normalization happening?”
    - it is whether one narrow normalization-state experiment can safely avoid
      re-normalizing those already-int32 binary chain nodes
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

Subagent forensics, the mature-control diff, and the direct code read now all
agree on the exact first divergence:

- `lj_record_next()` already has the helper result
- array synthesizes a visible numeric key from the successor index
- non-array/hash intentionally leaves the visible key unloaded
- `rec_itern()` then immediately forks on `if (!tref_isnil(ix.key))`
- so the first-side hash failure is still the same payload-vs-nil /
  unloaded-visible-key family already tested and rejected

That closes this owner-selection line again on the current tree:

- do not reopen it unless there is a cut that is genuinely different from the
  rejected first-side lazy-key classifiers
- otherwise move to a different mechanism or ship the current freeze point

One more four-track measurement pass now closes the current iterator reopening
window on the frozen checkpoint:

- Track 1 restamped the exact `trace 1 exit 1` seam on clean `kdz`
  - hash and array both still attribute that seam to the first loop/leave
    decision after the helper result exists
  - hash still falls into the same closed payload-vs-nil /
    unloaded-visible-key family
- Track 2 showed the `rec_loop_jit_root` death is downstream
  - the `startop=79` root candidate is not a new hash-vs-array seam
  - the real first materially different candidate is still the first-side
    `startop=88` path that was already closed
- Track 3 kept the runtime read exit-heavy
  - `kdz` truth-pack v3:
    - `pairs_sum/hot median=0.062519`
    - `pairs_array_sum/hot median=0.066353`
  - focused micros stay far above same-harness `-joff`:
    - `hash_value/hot 0.061603` vs `0.005682`
    - `hash_key/hot 0.045652` vs `0.003942`
    - `array_value/hot 0.064249` vs `0.004124`
  - `perf stat` is still unsupported on `kdz`, so the active attribution path
    is the runtime fallback:
    - `hash_value` steady exit `1:1`, `64.17ns/texit`
    - `hash_key` steady exit `1:1`, `71.33ns/texit`
    - `array_value` steady exit `5:1`, `66.93ns/texit`
    - all three classify as `exit-dominated`
- Track 4 closed the ABI-aware preserved-GPR audit
  - no proven loop-carried value is being dropped only because current s390x
    reg-home/liveness fails to keep it in a preserved GPR across
    `lj_vm_next(tab, keyindex)`
- `zkd0` stayed a regression gate and came back worse:
  - `pairs_sum/hot median=0.119175`
  - `pairs_array_sum/hot median=0.120802`
  - versus the frozen `zkd0` checkpoint those are `+26.94%` and `+37.36%`

That made the earlier queueing decision explicit:

- no new iterator seam was proven outside the reject pile
- iterator stays frozen at the current Lane A + Lane B checkpoint
- the next queued perf workstream moves to dispatch/side-exit

### Queued Dispatch / Side-Exit Workstream

The branch now has a checked-in dispatch truth-pack helper at
[tools/s390x/build_dispatch_truth_pack.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/build_dispatch_truth_pack.py)
to restamp the next active frontier on the same clean-host contract.

Current `kdz` hot medians from the active dispatch pack are:

- `numeric_loop/hot`
  - JIT-on `0.642036`
  - `-joff` `0.002168`
  - gap `+0.639868s`
  - ratio `296.14x`
- `side_exit_loop/hot`
  - JIT-on `0.155224`
  - `-joff` `0.004739`
  - gap `+0.150485s`
  - ratio `32.75x`
- `hotexit_loop/hot`
  - JIT-on `0.380652`
  - `-joff` `0.005653`
  - gap `+0.374999s`
  - ratio `67.34x`

The important read is the shared runtime shape, not just the medians:

- branch-free `numeric_loop` is already enough to reproduce the same hot-side
  failure mode
- after warmup, `numeric_loop` still shows:
  - `TRACE_START 10`
  - `TRACE_STOP 10`
  - `TRACE_ABORT 0`
  - `TEXIT_COUNT 2001`
- `side_exit_loop` matches the same pattern:
  - `TRACE_START 11`
  - `TRACE_STOP 11`
  - `TRACE_ABORT 0`
  - `TEXIT_COUNT 2001`
- this is not a branch-payload-only issue in the loop body
- the common seam is now the generic dispatch loopback path

The exact seam on the frozen dispatch baseline is now named:

- `trace 1 exit 0` is the hot seam
- on the focused numeric probe:
  - root `trace 1` starts at `BC_FORL` and stops as a loop
  - the hot-side replay lands on the loop-body entry:
    - `pc = BC_MODVN`
    - `prevop = BC_JFORI`
    - `snappc = BC_MODVN`
  - the first side trace enters as:
    - `parent=1 exit=0`
    - `startop = BC_JMP`
    - `parent_startop = BC_FORL`
    - `parent_snapnent = 0`
  - that first side trace does pass the current extra-loop narrow gate:
    - `prev_is_jfori = 1`
    - `fori_target = 1`
    - `target_match = 1`
    - `site=extra_loop_narrow`
  - after `sidecheck`, that first side trace is still on the same bare body
    entry state

That means the active dispatch seam is now:

- generic `FORL` / `JFORI` loop-entry `exit 0`
- not a missed side-trace `JFORI` / `FORL` eligibility check
- the current extra-loop narrow path is firing and still not changing the
  owner/exit shape enough to stop the one-exit-per-iteration ladder
- not iterator lazy-key ownership
- not bridge/continuation machinery
- not late backend instruction shaving

Dispatch-side hot-side classifiers are now split:

- `LUAJIT_S390X_HOTSIDE_CANON_EQUIV=1` on clean `kdz` does not fix the ladder
- instead it collapses the observed exit traffic into one reused site:
  - `7:0=160743`
- that is not a real owner/materialization win and is branch-hostile on z
- `LUAJIT_S390X_HOTSIDE_CANON_CHILD=1` reduces trace churn but leaves the
  real payer in place:
  - `TRACE_START` drops from `10` to `6`
  - `TEXIT_COUNT` stays at `2001`
  - the last trace still absorbs `8:0=858`
- `LUAJIT_S390X_HOTSIDE_SHARE_EQUIV=1` is not safe from the current seam:
  - the focused `numeric_loop` probe timed out after `20s` with no result

A focused `traceinfo` snapshot on the same clean `kdz` seam corrects one part of
the earlier read:

- the dispatch descendants do not fail to become loop owners
- on the focused `numeric_loop` probe:
  - `trace 1`: `link=1`, `linktype=loop`, `nins=18`, `nexit=4`
  - `trace 2`: `link=1`, `linktype=root`, `nins=4`, `nexit=3`
  - `trace 3` through `trace 12`: each is `link=self`, `linktype=loop`,
    `nins=18`, `nexit=4`
  - `trace 13`: `link=0`, `linktype=stitch`, `nins=11`, `nexit=2`
- so the current extra-loop narrow path is firing and does produce self-loop
  loop traces
- the real remaining dispatch red is that execution keeps spawning a chain of
  equivalent self-loop loop traces instead of settling on one reusable owner

The next exact target from there was therefore:

- default `trace_hotside()` reuse/adoption policy on the same seam:
  - by late steady-state (`parent=10 exit=0` in the focused `kdz` probe),
    default policy already sees:
    - `cand=6`
    - `child=7`
  - but with all hotside reuse gates off it still:
    - leaves `J->parent` unchanged
    - increments `snap->count`
    - starts a fresh side trace at `hotexit`
  - so the current loop-clone ladder is not a discovery failure
  - it is the default behavior of `trace_hotside()` when reuse/adoption is not
    explicitly enabled

The next exact target from there was therefore:

- one narrow dispatch-side reuse/adoption experiment, only if it can prove a
  real owner/exit win on this seam instead of just collapsing traffic into one
  reused site

That first narrow reuse/adoption experiment is now rejected:

- `LUAJIT_S390X_HOTSIDE_REUSE_LOOP_CHILD`
  - intended shape:
    - late `exit 0`
    - self-loop `BC_JMP` parent
    - `prevop = BC_JFORI`
    - earlier equivalent candidate plus existing child already found
  - intended action:
    - patch the current parent exit directly to the existing child loop target
    - mark the current hot-side counter done
    - skip recording another equivalent child trace
  - structural gate result on clean `kdz`:
    - focused `numeric_loop_trace.lua`
    - `timeout 20`
    - `REMOTE_RC=124`
  - decision:
    - reject immediately
    - do not keep the gate in the tree
    - the dispatch family is still open, but this direct child-retarget path is
      not safe from the current seam

The next earlier patch-target classifier from that same seam is now also
rejected:

- `LUAJIT_S390X_SIDEEXIT_MCLOOP`
  - exact intended shape:
    - keep the same loop-clone seam and patch parent side exits to the
      existing loop-body target (`T->mcloop`) instead of generic trace entry
  - why it was worth testing:
    - narrower and earlier than direct late child-retarget
    - already existed in-tree
    - directly tested whether the remaining ladder was caused by landing at the
      trace entry prologue rather than the loop body
  - clean `kdz` structural gate:
    - focused `numeric_loop_trace.lua`
    - clean rebuild succeeded
    - the probe then failed immediately with a native segmentation fault before
      any trace-count output was produced
  - code-level autopsy:
    - [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c)
      can force side-exit patching to `J->cur.mcode + T->mcloop`
    - but [src/lj_asm.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm.c)
      only defines `mcloop` as an internal loop-body entry offset
    - and [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc)
      only consumes `mcloop` through owner/resume-controlled VM entry paths
    - so `mcloop` is not a generic safe side-exit landing target on this
      mechanism
  - decision:
    - reject immediately
    - do not widen to `side_exit_loop` or `hotexit_loop`
    - patch-target shape does not open a safe win on the current dispatch
      loop-clone mechanism

That closed the current dispatch loop-clone family:

- default `trace_hotside()` can already see equivalent candidates
- direct late child-retarget is unsafe
- direct side-exit patching to `mcloop` is unsafe
- no safe earlier patch target was exposed by the existing mechanism

The follow-up dispatch-adjacent side-exit pass is now also classified:

- the dispatch truth-pack helper was tightened to use lightweight aggregated
  trace/texit counters so the branchy loops can be observed without the older
  capture overflow path
- clean `kdz` rerun:
  - [20260331-kdz-dispatch-truth-pack-v4](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260331-kdz-dispatch-truth-pack-v4)
- focused runtime result:
  - `numeric_loop`, `side_exit_loop`, and `hotexit_loop` all come back with the
    same trace/exit envelope:
    - `TRACE_START 11`
    - `TRACE_ABORT 0`
    - `TEXIT_COUNT 2001`
    - `TEXIT_HIST 10:0=200,11:0=200,12:0=200,13:0=58,1:0=142,2:0=1,4:0=200,5:0=200,6:0=200,7:0=200,8:0=200,9:0=200`
  - all three focused exit-attribution logs now pin the same practical seam:
    - `pc = BC_MODVN`
    - `prevop = BC_JFORI`
    - `startop = BC_JMP`
    - `site=extra_loop_narrow`
- decision:
  - the branchy dispatch loops do not expose a distinct side-exit payer outside
    the already-closed loop-clone mechanism
  - the current generic dispatch/side-exit line is now closed on this
    mechanism too

The next queued workstream is now:

1. helper-boundary storage/materialization audits where the s390x ABI may help
2. only then broader JIT throughput families

The first helper-boundary follow-up from that queue is now classified:

- clean `kdz` audit surface:
  - [20260331-kdz-href-helper-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260331-kdz-href-helper-audit)
  - target script:
    - [hotexit_update_preinterned.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/jit_loops/hotexit_update_preinterned.lua)
- result:
  - the helper-backed dynamic `HREF` hot-exit surface is structurally healthy
    on the current tree
  - clean `kdz` still shows the expected converged shape:
    - `trace 1`: root loop
    - `trace 2`: update-path root from `parent=1 exit=2`
    - `trace 3`: update-path loop from `parent=1 exit=0`
    - `trace 4`: final stitch
  - the repro terminates cleanly and reports the expected final table state
- decision:
  - this existing helper-backed `HREF` path is not the next broken
    helper-boundary family
  - there is no new helper-boundary storage/materialization seam named from
    this audit

That narrows the queue again:

1. if a new helper-boundary seam is proposed, it must be named first and be
   demonstrably different from the already-closed `lj_vm_next` and dynamic
   `HREF` surfaces
2. otherwise the next live workstream is broader JIT throughput work

That broader-throughput queue is now grounded as:

1. [tests/s390x/perf/vararg_paths.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/vararg_paths.lua)
   first, because it stresses arg-bank, call, return, and `select()` / vararg
   flow without reopening the closed iterator or dispatch mechanisms
2. [tests/s390x/perf/bitops_mix.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/bitops_mix.lua)
   second, as a helper-light compiled-body control that can separate exit-heavy
   red from pure backend throughput red
3. [tests/s390x/perf/mixed_noffi.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/mixed_noffi.lua)
   is intentionally not the next target because its `pairs(map)` loop would
   reintroduce iterator behavior into a queue that is supposed to be outside
   the frozen iterator family

The first authoritative `kdz` pass on that queue is now partially in hand:

- clean-host raw medians from
  [20260331-kdz-vararg_paths-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260331-kdz-vararg_paths-truth-pack)
  already show:
  - `sum_loop/hot`
    - JIT-on `1.109134`
    - `-joff` `0.004543`
    - about `244.14x` slower with JIT on
  - `retlast_loop/hot`
    - JIT-on `0.029367`
    - `-joff` `0.001993`
    - about `14.74x` slower with JIT on
  - `retconst_loop/hot`
    - JIT-on `0.028397`
    - `-joff` `0.000561`
    - about `50.62x` slower with JIT on
- focused hot-only medians keep the same order:
  - `sum_loop/hot`: `0.459993` vs `0.004453`
  - `retlast_loop/hot`: `0.029780` vs `0.002047`
  - `retconst_loop/hot`: `0.027820` vs `0.000574`
- interpretation:
  - this is not a generic branch or iterator seam reopening
  - the next live target is traced hot vararg loop behavior, starting with
    `sum_loop`, because that path is dramatically redder than the other two
    vararg cases

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
abstract” and not “one more backend micro-optimization.”

For iterator specifically, the current answer is now operational:

- do not open another iterator patch family from the current mechanism
- keep the frozen checkpoint as the active shipping state
- only reopen iterator if a future measurement pass proves a genuinely new
  root-trace storage/control seam outside the reject pile
- otherwise spend the next perf effort on dispatch/side-exit, where the branch
  has already shown real measured wins
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
