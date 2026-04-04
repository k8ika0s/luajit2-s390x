# s390x State Of The Project

Last updated: 2026-04-03 17:28:00 PDT

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
- the host-pair frozen-family fence is now complete for the envless promoted
  default hotside path:
  - `kdz` iterator and dispatch stay structurally inert
  - `zkd0` iterator improves in raw medians but keeps the same frozen seam
  - `zkd0` dispatch is flat-to-worse and keeps the same frozen seam
  - conclusion:
    - `LUAJIT_S390X_HOTSIDE_CANON_SHARE_UGET_LOOPROOT` remains a scoped
      throughput policy, not a frozen-family promotion
    - that scoped throughput policy should now be treated as the active
      shipping-throughput default for `promotion_core` only:
      - `promotion_secondary` is carry-forward evidence only
      - `same_seam_but_dominated` stays excluded
      - frozen iterator and frozen dispatch stay out of scope
      - explicit baseline / opt-out remains
        `LUAJIT_S390X_DISABLE_HOTSIDE_CANON_SHARE_UGET_LOOPROOT=1`
      - pinned host-pair summary:
        [20260402-hotside-promotion-core-host-pair](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-hotside-promotion-core-host-pair/summary.md)
- the measurement helpers are now hardened enough to close the authoritative
  `zkd0` fence cleanly:
  - tracked-file sync retries once on transient `zkd0` transport failure
  - clean remote build retries once after the flaky clean-build race
  - timed dispatch benches launch via the absolute remote `repo/src/luajit`
    path so `taskset` does not lose the binary on `zkd0`
- the branch now also has a checked-in reduced mechanism helper at
  [tools/s390x/build_core_exit_mechanism_probe.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/build_core_exit_mechanism_probe.py)
  so the promoted-default `promotion_core` families can be classified by
  dominant `trace`/`texit` pair instead of only aggregate `TEXIT_COUNT`
  - that helper now also supports reduced iteration overrides and extra env
    passthrough, so focused runtime exit attribution can be captured without
    reopening the full 64k/80k hot runs
- the exact static-stop FFI literal-stop failure is now directly remediated in
  the recorder baseline:
  - hidden `FORL_STOP` no longer prefers `find_kinit()` on this path; it now
    stays anchored to the runtime stop slot in
    [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c)
  - default opt-out:
    - `LUAJIT_S390X_DISABLE_FORI_FORCE_STOP_SLOT=1`
  - compatibility opt-in alias:
    - `LUAJIT_S390X_FORI_FORCE_STOP_SLOT=1`
  - exact host result on the bad reducer shape:
    - `kdz`: `onecall 338816`, `warm 338816`
    - `zkd0`: `onecall 338816`, `warm 338816`
  - the old bad outputs were:
    - `onecall 26`
    - `warm 7`
  - the checked-in direct guard is now:
    [ffi_literal_stop_same_callsite.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/jit_core/ffi_literal_stop_same_callsite.lua)
  - and the perf harness no longer bootstraps its expected value from the
    traced path:
    [ffi_calls_static_stop.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/ffi_calls_static_stop.lua)
- post-fix `promotion_core` re-quant on clean `kdz` says the active
  throughput floor is unchanged:
  - [be_helpers.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/be_helpers.lua)
    - default:
      - `number_helper_loop/hot 0.008927`
      - `be_pack_loop/hot 0.023920`
    - baseline:
      - `number_helper_loop/hot 0.411136`
      - `be_pack_loop/hot 0.431987`
    - `-joff`:
      - `number_helper_loop/hot 0.002251`
      - `be_pack_loop/hot 0.019077`
  - [ffi_calls.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/ffi_calls.lua)
    - default:
      - `direct_abs/hot 0.018530`
      - `stored_abs/hot 0.013380`
    - baseline:
      - `direct_abs/hot 0.477617`
      - `stored_abs/hot 0.458766`
    - `-joff`:
      - `direct_abs/hot 0.009999`
      - `stored_abs/hot 0.006944`
  - correction:
    - `5e7b09fe` closes the literal-stop FFI correctness seam
    - it does not materially lift the active `promotion_core` throughput floor
    - `be_pack_loop` remains the best payoff sibling because it is still the
      closest live family to `-joff`
    - `number_helper_loop` remains the cleanest control for mechanism work
  - current host-pair caveat:
    - the fresh `zkd0` throughput re-quant is blocked by a broken clean bench
      tree and could not be restamped this pass
- focused post-`5e7b09fe` mechanism rerun on clean `kdz` says the live seam is
  unchanged too:
  - artifact:
    [20260403-131228-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-131228-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism/summary.md)
  - [be_helpers.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/be_helpers.lua)
    - `number_helper_loop`: `TRACE_START 6`, `TRACE_STOP 5`, `TRACE_ABORT 0`,
      `TEXIT_COUNT 64001`
    - dominant seam: `trace 7 exit 0` x `63457`
    - first `sload_int`: `curins 15`, `op1 3`, `op2 4`, `ofs 8`, `extra 12`
    - `be_pack_loop`: `TRACE_START 5`, `TRACE_STOP 5`, `TRACE_ABORT 0`,
      `TEXIT_COUNT 64001`
    - dominant seam: `trace 7 exit 0` x `63457`
    - first `sload_int`: `curins 35`, `op1 3`, `op2 4`, `ofs 8`, `extra 12`
  - correction:
    - `5e7b09fe` closes the exact literal-stop FFI correctness seam
    - it does not move the live `promotion_core` loop floor on `kdz`
    - `number_helper_loop` and `be_pack_loop` still converge on the same
      restored-`BC_UGET` / first-`SLOAD(op1=3, ofs=8)` family
- exact-taken host-pair reruns now close the next question too:
  - `kdz`:
    [20260403-135247-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-135247-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism/summary.md)
  - `zkd0`:
    [20260403-135538-zkd0-hotside_canon_share_uget_looproot_default-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-135538-zkd0-hotside_canon_share_uget_looproot_default-core-exit-mechanism/summary.md)
  - on both hosts, for both `number_helper_loop` and `be_pack_loop`:
    - dominant seam stays `trace 7 exit 0`
    - first literal taken guard stays `curins 3`
    - exact guard stays the inherited current-value lane:
      - `IR=SLOAD`
      - `op1=4`
      - `op2=36`
      - `ofs=16`
      - `extra=20`
  - correction:
    - the live `promotion_core` payer is still the inherited current-value
      replay/typecheck lane
    - it has not shifted to carried-`total`
    - it has not shifted to a later arithmetic/compare consumer
- one new patch family was opened and rejected against that named guard:
  - opt-in experiment:
    - `LUAJIT_S390X_FORL_FASTPATH_VISIBLE_IDX_NOGUARD=1`
  - targeted rule:
    - only on matched `FORL` fastpath reuse (`pc_match=1`, `idx_match=1`)
    - rebuild the inherited current-value lane without the replay typecheck
  - `kdz` artifact:
    [20260403-135832-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-135832-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism/summary.md)
  - result:
    - exact taken guard stayed unchanged on both `number_helper_loop` and
      `be_pack_loop`
    - `guardmark` remained `curins 3`
    - the old `SLOAD op1=4 op2=36` stayed first
    - the patch only grew the loop trace (`nins 28 -> 29`, `71 -> 72`) and
      introduced a later `SLOAD op1=4 op2=32`
  - conclusion:
    - matched-fastpath visible-idx no-guard is a clean reject
- one narrower patch family was then opened and rejected too:
  - opt-in experiment:
    - `LUAJIT_S390X_FORL_ROOT_VISIBLE_IDX_NOGUARD=1`
  - targeted rule:
    - only on root-born `rec_for_loop(init=1)` creation of the visible
      `FORL_IDX` int lane
    - drop `IRSLOAD_TYPECHECK` there and leave all replay-side logic alone
  - `kdz` exact-taken artifacts:
    - [number_helper_loop](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-144356-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism/summary.md)
    - [be_pack_loop](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-144556-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism/summary.md)
  - structural read:
    - the first literal taken guard does move off the inherited current-value
      lane on both live workloads
    - `number_helper_loop`: `curins 15`, `IR=SLOAD`, `op1=3`, `op2=4`,
      `ofs=8`, `extra=12`
    - `be_pack_loop`: `curins 35`, `IR=SLOAD`, `op1=3`, `op2=4`, `ofs=8`,
      `extra=12`
    - so this family redirects the first exact guard to the carried-`total`
      lane
  - throughput read:
    - [20260403-kdz-be_helpers-hotside_canon_share_uget_looproot_default-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260403-kdz-be_helpers-hotside_canon_share_uget_looproot_default-truth-pack/summary.md)
    - `number_helper_loop/hot`: `0.010545` vs current default `0.008927`
    - `be_pack_loop/hot`: `0.025796` vs current default `0.023920`
    - both workloads stay `exit-dominated` with
      `TRACE_START 6`, `TRACE_STOP 5`, `TRACE_ABORT 1`, `TEXIT_COUNT 64001`
  - conclusion:
    - root-only visible-idx no-guard is a clean reject
    - it shifts the first exact guard, but it makes the live payoff family
      slower and does not clear the repeated loop-exit floor
  - that reject also pins the next real issue cleanly:
    - the surviving inherited current-value `FORL_IDX` guard is still the
      first literal payer on the shipping default, but it is not the whole
      floor by itself
  - as soon as that root-born visible-current typecheck is relaxed, the next
    front-most exact guard becomes the loop-carried accumulator lane
  - source-backed identity:
    - the inherited current-value lane is born in
      [rec_for_loop()](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c)
      as `fori_load(... IRSLOAD_INHERIT | IRSLOAD_TYPECHECK | ...)`
    - the carried-`total` lane is not a numeric-for helper lane and not a
      replay-parent artifact; it is the ordinary stack specialization path
      in [sload()](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c)
      reached through `getslot()`
    - on the rejected root-only gate, the visible-current load remains in the
      trace as `op1=4 op2=32`, but the first exact guard shifts to:
      - `number_helper_loop`: `curins 15`, `IR=SLOAD`, `op1=3`, `op2=4`
      - `be_pack_loop`: `curins 35`, `IR=SLOAD`, `op1=3`, `op2=4`
  - implication:
    - the immediate secondary payer is the loop-carried accumulator
      `getslot()->sload()` replay / typecheck / consumer contract
    - but the next honest patch family is no longer another inherited-current
      no-guard tweak
- direct slot attribution on clean `kdz` now closes the stale naming dispute
  around `op1=4 / ofs=16 / extra=20`:
  - artifact:
    [20260403-kdz-visible-lane-slot-attribution](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-kdz-visible-lane-slot-attribution/summary.md)
  - exact-taken runtime maps:
    - `S390X_SLOADMAP curins=3 ref=3 kind=int op1=4 op2=0x24 ofs=16 vofs=20 base=2`
  - repeated exit slots on both representative workloads show:
    - slot `idx=2` advances `3, 4, 5, ...`
    - slot `idx=4` stays constant `1`
    - slot `idx=5` mirrors the advancing current value one slot later
  - correction:
    - `op1=4 / ofs=16` is neither hidden `STEP` nor the visible `FORL_EXT`
      slot
    - it is the hidden `FORL_IDX` slot itself under the live frame layout
    - the visible current-value alias is `FORL_EXT`, but the shipping exact
      guard is still bound to hidden `FORL_IDX`
  - alias-rebind experiment is now closed too:
    - artifact:
      [20260403-kdz-forl-ext-alias-rebind](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-kdz-forl-ext-alias-rebind/summary.md)
    - opt-in family:
      - `LUAJIT_S390X_FORL_REBIND_EXT_ALIAS=1`
    - exact-taken result:
      - the same repeated `trace 7 exit 0` family survives
      - the first exact guard really does move off hidden `FORL_IDX` and onto
        the visible alias:
        - `number_helper_loop`: `curins 3`, `SLOAD op1=7 op2=36`,
          `ofs=40 extra=44`
        - `be_pack_loop`: same `SLOAD op1=7 op2=36` shift on the live loop
          family
    - throughput result:
      - [20260403-kdz-forl-ext-alias-truth](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-kdz-forl-ext-alias-truth/jit-on.jsonl)
      - `number_helper_loop/hot 0.009876` vs default `0.008927`
      - `be_pack_loop/hot 0.024501` vs default `0.023920`
      - smoke stays correct, but both in-scope workloads get slower
  - alias no-guard experiment is now closed as well:
    - artifact:
      [20260403-kdz-forl-ext-alias-noguard](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-kdz-forl-ext-alias-noguard/summary.md)
    - opt-in family:
      - `LUAJIT_S390X_FORL_EXT_ALIAS_NOGUARD=1`
    - exact-taken result:
      - the live loop family stays exit-dominated
      - the first exact guard no longer lands on current-value replay
      - instead both live `kdz` workloads redirect straight back to the
        carried-`total` lane:
        - `number_helper_loop`: `curins 15`, `SLOAD op1=3 op2=4`,
          `ofs=8 extra=12`
        - `be_pack_loop`: `curins 35`, `SLOAD op1=3 op2=4`,
          `ofs=8 extra=12`
    - conclusion:
      - dropping the visible-alias typecheck does not open a new remediation
        lane
      - it just recreates the already-closed carried-accumulator redirect
  - signed GC64 int-tag overlay on the exact live seam is now closed too:
    - mechanism artifact:
      [20260403-kdz-signed-int-current-seam](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-kdz-signed-int-current-seam/summary.md)
    - truth pack:
      [20260403-kdz-be_helpers-hotside_canon_share_uget_looproot_default-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260403-kdz-be_helpers-hotside_canon_share_uget_looproot_default-truth-pack/summary.md)
    - opt-in family:
      - `LUAJIT_S390X_GC64_SIGNED_INT_SLOAD=1`
    - exact-taken result:
      - the old hidden current-value runtime `guardmark curins 3` disappears
      - the repeated `trace 7 exit 0` family survives
      - the first remaining frame `sload_int` is again the carried-`total`
        lane:
        - `number_helper_loop`: `curins 15`, `SLOAD op1=3 op2=4`
        - `be_pack_loop`: `curins 35`, `SLOAD op1=3 op2=4`
      - the dominant runtime guardmark becomes unmarked `curins 0`
    - throughput and smoke result:
      - `number_helper_loop check 13762770`
      - `be_pack_loop check 210`
      - `number_helper_loop/hot 0.008709` vs `-joff 0.002304`
      - `be_pack_loop/hot 0.023758` vs `-joff 0.018765`
    - conclusion:
      - the live payer is not solved by switching this seam to the signed
        GC64 int-tag path
      - this is not a promotable default remediation
  - next exact target:
    - not more slot-selection experiments
    - the remaining live issue is the current-value replay/typecheck contract
      itself, independent of whether the value is sourced from hidden
      `FORL_IDX` or visible `FORL_EXT`
  - compare-truth attribution on the hidden `FORL_IDX` seam is now pinned:
    - control artifact:
      [20260403-kdz-cmptruth-current-seam](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-kdz-cmptruth-current-seam/raw/number_helper_loop.stderr.log)
    - the exact taken lane is a valid boxed int at runtime:
      - `raw=0xfff9000000000013`
      - `itype=-14`
      - payload advances as the live current value
    - but the backend compare constants are wrong on the live signed path:
      - extracted tags:
        - logical `0x1fff2`
        - signed `0xfffffffffffffff2`
      - emitted expected constants:
        - logical `0x1ffff`
        - signed `0xffffffffffffffff`
    - conclusion:
      - this narrows the live payer to backend compare semantics on the
        inherited hidden `FORL_IDX` integer `SLOAD`
      - it is not a replay-materialization or guard-attribution problem
  - first narrow compare-lowering remediation is now classified and closed:
    - control artifact:
      [20260403-kdz-cmptruth-number-helper-fixed](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-kdz-cmptruth-number-helper-fixed/summary.md)
    - payoff crash artifact:
      [20260403-kdz-be-pack-after-int-compare-fix](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-kdz-be-pack-after-int-compare-fix/raw/be_pack_loop.stderr.log)
    - the direct fix was:
      - load the signed int compare constant as `LJ_TISNUM`
      - load the logical fallback constant as the low 17-bit tag form
    - `number_helper_loop` proves the fix is directionally real:
      - `RESULT 1323881804`
      - `TRACE_START 3`
      - `TRACE_STOP 2`
      - `TEXIT_COUNT 202`
      - dominant runtime guardmark becomes `curins 14`
      - the old hidden-current `curins 3` seam disappears
    - but `be_pack_loop` is not promotable under the same fix:
      - remote probe exits `139`
      - the branch is therefore back on baseline compare lowering plus
        debug-only compare-truth instrumentation
    - next exact target:
      - stabilize the post-current-seam `be_pack_loop` failure that appears
        once the hidden `FORL_IDX` compare is corrected
      - not reopen replay/slot-selection families
- short ISA/ABI memo for the current seam:
  [promotion-core-isa-audit.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/promotion-core-isa-audit.md)
  - blind 32-bit opcode swaps remain rejected
  - preserved-register carry remains off-seam while the first literal guard is
    still inherited `SLOAD`
  - predictor hints remain off-seam
  - a fused compare-and-branch experiment only becomes relevant if a later
    cycle moves the first literal taken guard to one stable compare family
- the iterator lane is now frozen at the current checkpoint unless a genuinely
  new seam appears outside the reject pile
- the next live seam inside the promoted-default throughput slice is now pinned
  on both hosts:
  - `be_helpers` and `ffi_calls` converge to the same dominant steady-state
    loop exit:
    - `trace 7 exit 0`
    - `linktype loop`
  - `number_helper_loop` and `be_pack_loop` keep the same `trace 7 exit 0`
    stream (`63457` dominant hits out of `64001` exits); the difference is
    loop-body size (`nins 28` vs `71`)
  - `direct_abs` and `stored_abs` also keep the same `trace 7 exit 0` stream
    (`79457` dominant hits out of `80001` exits); the difference is loop-body
    size (`nins 33` vs `23`), not a different front-most seam
  - focused reduced dump on clean `kdz` corrects the next attribution target:
    - `number_helper_loop` loop trace has no call IR in the hot loop body;
      the front-most remaining seam there is not a helper call boundary
    - `direct_abs` keeps `CALLXS` in the hot loop body, so it stays the
      call-decorated sibling, not the clean first attribution target
  - reduced runtime exit attribution on clean `kdz` now pins the exact steady
    seam for both representative workloads:
    - artifact:
      [20260401-kdz-core-exit-attribution-reduced](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-core-exit-attribution-reduced/summary.md)
    - both `number_helper_loop` and `direct_abs` still converge to the same
      reduced dominant site:
      - `TRACE_START 5`
      - `TEXIT_COUNT 801`
      - dominant `trace 7 exit 0` x `257`
    - existing `lj_trace_exit()` logging proves that repeated site resumes at:
      - `pc op=45`
      - `snapop=45`
      - `snapcount=0`
      - `snapref=32769`
    - `op=45` is `BC_UGET`, matching the filtered hotside seam in
      [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c)
    - the exact `BC_UGET` meaning is now pinned for the clean first target:
      - in
        [tests/s390x/perf/be_helpers.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/be_helpers.lua),
        the loop body is `bit.tobit(total + i * 65537)`
      - trace IR shows the front prefix as:
        - `fun SLOAD #0`
        - `i64 UREFO ... #0`
        - `tab ULOAD`
        - `HREFK "tobit"`
        - `fun HLOAD`
        - `fun EQ ... bit.tobit`
      - so `BC_UGET` is loading the `bit` module upvalue table, not the
        `bit.tobit` callee directly; the callee resolution happens in the
        following `HREFK/HLOAD` identity-guard prefix
    - reduced baseline comparison now closes the mechanism split:
      - artifact:
        [20260401-kdz-core-exit-attribution-baseline-reduced](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-core-exit-attribution-baseline-reduced/summary.md)
      - baseline and promoted default keep the same `BC_UGET` seam
      - what changes is only which equivalent loop clone accumulates the exits:
        - baseline reduced dominant site: `trace 6 exit 0` x `200`
        - promoted-default reduced dominant site: `trace 7 exit 0` x `257`
      - queue correction:
        - the promoted default is collapsing the clone-parent walk
        - it is not changing the actual per-iteration exit seam
        - focused first-clone proof now shows why the seam persists:
          - on the first hot source (`parent=1 exit=0`), there is no
            equivalent candidate and no child yet (`cand=0`, `child=0`)
          - the restored snapshot remains the same header PC:
            - `pc=snappc`
            - `op=snapop=BC_UGET`
            - `startop=BC_JMP`
          - hotside counting simply runs to `hotexit` on that restored header
            and starts the first side trace from the same `UGET` seam
          - that is why the first clone (`trace 4`) has the same loop shape as
            the root (`nins 28`, `nexit 4`) instead of peeling deeper into the
            arithmetic body
        - source-backed mechanism now explains why the first clone must be born
          there on the current generic path:
          - [lj_snap_restore()](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_snap.c#L1196) returns `snap_pc(&map[snap->nent])`
          - [lj_trace_exit()](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c#L3197) passes that restored `pc` straight into
            [trace_hotside()](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c#L2941)
          - [trace_hotside()](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c#L3028) then flips to `LJ_TRACE_START` and calls
            `lj_trace_ins(J, pc)` with that same restored header `pc`
          - [lj_record_setup()](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c#L3833) copies `J->pc` into `J->startpc` for side traces and
            sets `startins=BC_JMP`
          - current `resumepc` machinery in
            [trace_save()](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c#L1902)
            only affects execution of an already-saved child trace; it does not
            provide a generic way to start recording later than the restored
            snapshot PC
        - reduced exit logger now narrows the live red again:
          - [20260401-kdz-core-exit-attribution-reduced](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-core-exit-attribution-reduced/summary.md)
          - dominant `exit 0` is not happening after any snapshot-carried state
            materializes
          - it is a `snapnent=0` exit at the restored `BC_UGET` PC
          - queue correction:
            - the live question is now the exact guard inside the whole
              pre-`SNAP #1` / `SNAP #0` header cluster
            - the restored `BC_UGET` site is only the first marker for the
              original helper form, not proof that the failing guard is the
              helper-lookup chain itself
        - reduced header variants on clean `kdz` now close the old
          helper-specific reading:
          - artifact:
            [20260402-kdz-uget-header-variant-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-uget-header-variant-audit/summary.md)
          - when `bit.tobit` is moved from the outer helper/header form into:
            - a local inside `run()`, or
            - a function argument, or
            - removed from the loop entirely in a pure-add reducer
          - the same reduced `exit 0` clone ladder still survives
          - the restored hot seam just moves with the first op in the reduced
            header:
            - helper form: `BC_UGET` (`45`)
            - local/arg form: `BC_MOV` (`18`)
            - pure-add form: `BC_MULVN` (`24`)
          - queue correction:
            - the remaining promotion-core red is not a helper-only
              `bit -> "tobit"` lookup seam
            - it is a generic `SNAP #0` header-guard family that survives even
              after the helper lookup chain is removed
        - existing `LUAJIT_S390X_GUARD_LOG=1` now narrows the cross-reducer
          candidates further:
          - original helper `snap=0` guard set:
            `curins=17,15,14,13,12,10,8,7,3,2`
          - pure-add reducer `snap=0` guard set:
            `curins=6,5,4,3,2`
          - helper-specific `vload_addr` / lookup guards disappear with the
            reduced forms
          - the surviving shared candidates are now:
            - `sload_int` on the loop-carried header state
            - arithmetic overflow guards as the weaker arithmetic fallback
          - exact reduced attribution is now tight enough to choose the first
            shared live family:
            - the first shared surviving guard after helper-specific lookup
              guards are removed is `sload_int`
            - overflow survives only on the pure-add sibling as the weaker
              arithmetic fallback
            - clean reduced `number_helper_loop` attribution on the promoted
              default now pins the first ordered `sload_int` inside the real
              dominant exit cluster:
              - artifact:
                [20260402-kdz-number-helper-sload-attribution](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-number-helper-sload-attribution/summary.md)
              - dominant runtime seam stays `trace 7 exit 0`
              - dominant guard order is now machine-readably pinned:
                `25,22,20,17,15,14,13,12,10,8,7,3,2`
              - first `sload_int` on the real workload is:
                - `curins=15`
                - `IR=SLOAD`
                - `op1=3`
                - `op2=4`
                - `ofs=8`
                - `extra=12`
              - semantic mapping is now pinned:
                - this target is on a GC64 build (`LJ_FR2=1`)
                - `IR_SLOAD.op1` is `baseslot + slot`
                - `op1=3` therefore maps to top-frame slot `1`
                - on `number_helper_loop`, top-frame slot `1` is the
                  loop-carried `total`
          - exact runtime guard attribution now sharpens the ordering inside
            that header cluster:
            - artifact:
              [20260402-kdz-number-helper-guardmark-attribution-v5](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-number-helper-guardmark-attribution-v5/summary.md)
            - on the real promoted workload, the dominant runtime failure
              inside `trace 7 exit 0` is:
              - `curins=3`
              - `IR=SLOAD`
              - `op1=4`
              - `op2=36`
              - `sload_int ofs=16 extra=20 cc=6`
            - the reduced no-helper sibling keeps that same front-most exact
              failure:
              - artifact:
                [20260402-kdz-number-helper-guardmark-attribution-v4](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-number-helper-guardmark-attribution-v4/raw/pure_add_reducer.stderr.log)
              - first repeated reduced exit cluster lands on
                `guardmark=0x3`
              - matching early guard log on the reduced sibling:
                - `kind=sload_int`
                - `curins=3`
                - `ofs=16`
                - `extra=20`
              - restored header marker shifts to `BC_MULVN`, but the first
                exact runtime failure stays on the same early inherited
                `sload_int`
            - queue correction:
              - the first shared marked header seam on the promoted slice is
                the inherited `sload_int` on `IR=SLOAD #4 TI`
              - corrected slot map artifact:
                [20260402-kdz-number-helper-fori-slot-map](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-number-helper-fori-slot-map/summary.md)
              - for `number_helper_loop`, `FORI/FORL A=2` means:
                - slot `2` = hidden `IDX`
                - slot `3` = hidden `STOP` (`n`)
                - slot `4` = hidden `STEP` (`1`)
                - slot `5` = visible `EXT` (`i`)
              - queue correction:
                - `sload_int ofs=16 extra=20` is hidden `STEP`, not hidden
                  `IDX`
                - `sload_int ofs=8 extra=12` is hidden `STOP`, not carried
                  `total`
                - the promoted-slice front seam is a numeric-for hidden
                  control-slot replay family, but the specific active marker is
                  `STEP`, not `FORL_IDX`
              - recorder/replay correction:
                - the old “root `FORI` constructor” read is now closed
                - [rec_for_loop()](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c#L1086)
                  already uses
                  `fori_arg(... find_kinit(...))` for hidden `STOP` and
                  `STEP` on the `FORL` side-trace path
                - the attempted root-only gate
                  `LUAJIT_S390X_FORI_CONST_INIT=1` only changed
                  [rec_for(..., isforl=0)](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c#L1127),
                  so it was pointed at the wrong constructor
                - clean `kdz` proof under the gate kept the real workload on
                  the same seam:
                  - artifact:
                    [20260402-kdz-fori-const-init-v1](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-fori-const-init-v1/summary.md)
                  - `TRACE_START 5`
                  - `TRACE_STOP 5`
                  - `TRACE_ABORT 0`
                  - `TEXIT_COUNT 64001`
                  - dominant texit still `7:0=63457`
                  - repeated exit still reports `guardmark=0x3`
                - queue correction:
                  - the live promoted-slice seam is not being created by the
                    root `FORI` initializer path
                  - it survives after the `FORL` side path has already had
                    `find_kinit()` available for hidden `STOP`/`STEP`
                  - so the next honest target stays replay/typecheck semantics
                    inside the restored numeric-for header, not more root
                    `FORI` initializer surgery
              - `trace 7 exit 0` is still a `snapnent=0` header exit, so this
                guard is validating live interpreter frame state at restored
                `SNAP #0`, not a later restored snapshot payload
              - the stricter taken-only guardmark path on the real workload
                now closes that ambiguity:
                - artifact:
                  [20260402-kdz-number-helper-guardmark-taken-v1](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-number-helper-guardmark-taken-v1/summary.md)
                - repeated dominant exits on `trace 7 exit 0` still land on
                  `guardmark=0x3`
                - the dominant exact-taken runtime guard is still:
                  - `curins=3`
                  - `IR=SLOAD`
                  - `op1=4`
                  - `op2=36`
                  - `kind=sload_int`
                  - `ofs=16`
                  - `extra=20`
              - queue correction:
                - on the real promoted workload, the first literal taken guard
                  inside the merged restored-`SNAP #0` numeric-`for` header is
                  the inherited hidden `STEP` `sload_int`
                - the later stop-bound `LE` on `n` is still present in the
                  same cluster, but it is no longer the front-most competing
                  failure on the real workload
                - the remaining live question is now narrower:
                  explain why that inherited numeric-for hidden-control
                  `STEP` replay/typecheck contract
                  still fails every trip even though restored slot logging
                  shows the corresponding interpreter slot already int-tagged
              - attempted fix boundary:
                - direct GC64 shifted-tag repair for the inherited integer
                  `SLOAD` compare is now closed as a promotable family
                - reduced no-helper artifact:
                  [20260402-kdz-pure-add-tagfix-v1](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-pure-add-tagfix-v1/summary.md)
                  shows the old flurry almost vanishing:
                  `TRACE_START 8`, `TEXIT_COUNT 6`
                - real helper workload artifact:
                  [20260402-kdz-be_helpers-hotside_canon_share_uget_looproot_default-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260402-kdz-be_helpers-hotside_canon_share_uget_looproot_default-truth-pack/raw/jit-on.stderr.log)
                  fails validation on clean `kdz`:
                  `number_helper_loop/hot: expected 1323881804, got 34304`
                - queue correction:
                  - the raw tag mismatch at `IR=SLOAD #4 TI` is real
                  - but swapping in the exact GC64 shifted int tag is not
                    sufficient to preserve the inherited `FORL_IDX` replay
                    contract on the promoted helper family
                  - treat that direct compare repair as rejected on the current
                    mechanism
              - exact backend mismatch is now source-backed on the real
                workload:
                - artifact:
                  [20260402-kdz-number-helper-sloadmap-v1](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-number-helper-sloadmap-v1/summary.md)
                - env-gated compiler/runtime map in
                  [lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h)
                  now ties the exact-taken guard to the emitted compare regs
                - current s390x integer `SLOAD` typecheck lowering is:
                  - `tmp = slot64`
                  - `tmp >>= 47`
                  - `expected = ((uint32_t)LJ_TISNUM >> 15)`
                  - `CGR tmp, expected`
                - on the real reduced helper workload, the repeated taken
                  values at the live seam are:
                  - live shifted tag: `0x1fff2`
                  - expected constant: `0x1ffff`
                - cross-backend contrast:
                  - [lj_asm_x86.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_x86.h)
                    and
                    [lj_asm_arm64.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_arm64.h)
                    compare against the GC64 high-word int-tag form
                    (`LJ_TISNUM << 15` / upper 32 bits), not the
                    s390x-shifted `0x1ffff` constant
                - queue correction:
                  - the live promoted-slice seam is now narrower than generic
                    numeric-for replay/header wording
                  - it is the s390x GC64 inherited integer-`SLOAD` typecheck
                    on hidden `STEP`
                  - the sharper source correction is that this is a signedness
                    bug in the backend extraction contract:
                    - GC64 `itype()` in
                      [lj_obj.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_obj.h)
                      uses arithmetic shift on signed `it64`
                    - current s390x integer `SLOAD` lowering in
                      [lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h)
                      uses logical `SRLG ... 47`
                    - that is why the live extracted tag is `0x1fff2` instead
                      of the GC64-consistent `0xfffffff2`
                  - design boundary note:
                    [gc64-sload-int-repair.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/gc64-sload-int-repair.md)
                  - the next honest target is a narrow design-first repair for
                    that typecheck contract, not more root-`FORI` surgery and
                    not another raw direct-tag swap
                  - first signed-extraction prototype is now rejected:
                    - artifact:
                      [20260402-kdz-gc64-int-sload-ashift-number-helper-v1](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-gc64-int-sload-ashift-number-helper-v1/summary.md)
                    - reduced no-helper sibling under the same gate stays
                      finite and collapses hard
                    - real helper workload fails with `RC=139` after moving
                      past the old `guardmark=0x3` seam
                    - queue correction:
                      - signed extraction is directionally relevant
                      - but the naive integer-`SLOAD` swap is not semantically
                        safe on the real helper path
                      - the next honest target is the downstream state that
                        goes bad once the inherited hidden-`STEP` typecheck
                        starts passing
            - current queue correction:
              - the next honest family is no longer shared header-state
                stabilization on the carried `total` reload
              - and it is no longer generic in-cluster guard-order
                attribution between the inherited `sload_int` and stop-bound
                `LE`
              - it is exact failure attribution for the inherited hidden
                numeric-for `STEP` replay/typecheck contract on the restored
                `SNAP #0` header
              - not more helper-header rewriting
              - not another backend low32-home reopening
        - next honest target:
          - explain why the inherited hidden `STEP` replay/typecheck contract
            still fails at the restored `SNAP #0` header on the promoted slice:
            - exact-taken guard on the real workload: `IR=SLOAD #4 TI`
            - corrected bytecode map on `number_helper_loop`:
              `IR=SLOAD op1=4` is hidden `STEP`, not hidden `IDX`
            - closed candidate:
              root-`FORI` const-init surgery does not move the seam, because
              `FORL` replay already uses `find_kinit()` for hidden
              `STOP`/`STEP`
            - rejected direct repair:
              exact GC64 shifted-tag compare for that inherited integer
              `SLOAD`
            - competing later `LE` on `n` remains secondary on the real
              workload
          - use `number_helper_loop` as the real workload and the pure-add
            reducer as the no-helper sibling
        - x64 control status is now explicit:
          - there is still no checked-in mature x64 reduced runner for this
            seam
          - ad hoc Rosetta x64 control is conceptually feasible on this
            workstation, but a direct `arch -x86_64 make -C src ...` still
            selects the arm64 VM build and fails in `vm_arm64.dasc`
          - queue correction:
            - treat mature x64 reduced control as a tooling gap for now
            - do not stall the s390x seam read on that missing runner
    - queue correction:
      - the remaining promotion-core red is not a generic helper/call exit
        family
      - the old helper-specific `BC_UGET` reading is now closed:
        - `BC_UGET` is only the first restored marker for the original helper
          form
        - the same reduced exit ladder persists when the header moves to
          `BC_MOV` and then `BC_MULVN`
      - the first shared live guard family across those reduced forms is now
        pinned as `sload_int`, and the ordered first shared signature is now
        pinned one step further as `SLOAD ofs=8 extra=12`
      - `direct_abs` stays the call-decorated sibling, but the clean first
        target is now the generic header-guard family visible in
        `number_helper_loop`
  - queue correction:
    - the remaining red is no longer hotside population churn on this slice
    - the next honest target is direct `trace 7 exit 0` attribution on
      `number_helper_loop`, then recorder/runtime explanation of why that
      `BC_UGET` seam stays live every trip under the promoted default
- the first dispatch/side-exit loop-clone queue has now also been classified
  and closed on the current mechanism
- the follow-up dispatch-adjacent side-exit pass did not expose a second seam;
  the branchy loops collapse back to the same closed loop-clone ladder
- the next queued performance workstream is broader JIT throughput work
  unless a new helper-boundary storage/materialization seam can be named first
- the broader-throughput queue is now explicit instead of implied:
  - active target:
    [tests/s390x/perf/bitops_mix.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/bitops_mix.lua)
  - active seam isolators:
    [tests/s390x/perf/logical_chain_tail_add.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/logical_chain_tail_add.lua)
    and
    [tests/s390x/perf/logical_chain_tail_store.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/logical_chain_tail_store.lua)
  - parked for this cycle:
    [tests/s390x/perf/vararg_paths.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/vararg_paths.lua)
  - [tests/s390x/perf/mixed_noffi.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/mixed_noffi.lua)
    stays out of this queue because it would re-entangle iterator behavior via
    `pairs()`
- the branch now has a checked-in broader-throughput helper at
  [tools/s390x/build_throughput_truth_pack.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/build_throughput_truth_pack.py)
  so broader JIT-on families can be restamped under the same tracked-file,
  direct-`src/` rebuild contract instead of ad hoc local runs
- the latest queue correction is now explicit:
  - `vararg_paths` stays parked on the current mechanism
  - `sum_loop` remains the normal caller-root stop into an already-compiled
    nested callee loop at `BC_JFORI`
  - no narrower recorder seam has been named before that nested-loop entry
  - the broader-throughput helper was underreporting `REMOTE_RC=124` because
    it only parsed `KEY value`, not `KEY=value`
  - the helper now parses `REMOTE_RC=...` correctly and uses a lightweight
    trace/texit counter path in
    [tests/s390x/helpers/testlib.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/helpers/testlib.lua)
    so reduced trace validation no longer wedges on the capture path itself
- the corrected clean-`kdz` read moves the live frontier away from backend
  compiled-body work and back to hot loop-clone / exit behavior:
  - [20260401-kdz-bitops_mix-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-bitops_mix-truth-pack)
    - `mix_bits/hot`: JIT-on `0.008267`, `-joff` `0.002084`, ratio `3.97x`
    - `REMOTE_RC 0`, `TRACE_START 41`, `TRACE_STOP 41`, `TRACE_ABORT 0`,
      `TEXIT_COUNT 7981`
    - classification: `exit-dominated`
  - [20260401-kdz-logical_chain_tail_add-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-logical_chain_tail_add-truth-pack)
    - `chain_tail_add/hot`: JIT-on `0.008269`, `-joff` `0.002123`, ratio
      `3.89x`
    - `REMOTE_RC 0`, `TRACE_START 41`, `TRACE_STOP 41`, `TRACE_ABORT 0`,
      `TEXIT_COUNT 7981`
    - classification: `exit-dominated`
  - [20260401-kdz-logical_chain_tail_store-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-logical_chain_tail_store-truth-pack)
    - `chain_tail_store/hot`: JIT-on `0.006676`, `-joff` `0.002319`, ratio
      `2.88x`
    - `REMOTE_RC 0`, `TRACE_START 42`, `TRACE_STOP 42`, `TRACE_ABORT 0`,
      `TEXIT_COUNT 7983`
    - classification: `exit-dominated`
  - [20260401-kdz-int_add_phi_only-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-int_add_phi_only-truth-pack)
    - `add_phi_only/hot`: JIT-on `0.000672`, `-joff` `0.000020`, ratio
      `33.60x`
    - `REMOTE_RC 0`, `TRACE_START 20`, `TRACE_STOP 20`, `TRACE_ABORT 0`,
      `TEXIT_COUNT 4001`
    - classification: `exit-dominated`
  - [20260401-kdz-logic_add_phi_noboundary-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-logic_add_phi_noboundary-truth-pack)
    - `logic_add_phi_noboundary/hot`: JIT-on `0.003527`, `-joff` `0.002153`,
      ratio `1.64x`
    - `REMOTE_RC 0`, `TRACE_START 23`, `TRACE_STOP 21`, `TRACE_ABORT 2`,
      `TEXIT_COUNT 4001`
    - classification: `exit-dominated`
- two manual bare `-jv` clean-`kdz` probes now close the observability loop:
  - `int_add_phi_only` terminates cleanly and shows a self-loop clone ladder
    after the first root/side formation
  - `logic_add_phi_noboundary` also terminates cleanly and shows the same
    family, with extra “leaving loop in root trace” surfaces inside the logic
    chain
  - that means the earlier reduced-probe timeouts were a capture-path artifact,
    not proof that the underlying loops themselves were non-terminating
- queue correction:
  - the backend low32-home / normalized-result family is no longer the active
    main-line frontier on the current evidence
  - [docs/s390x/low32-home-contract.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/low32-home-contract.md)
    remains as a parked design note, not an active implementation queue
  - do not reopen `W32_HOME` / low32-home prototype work until a finite
    compiled-body family exists again under the corrected validator
  - the current live target is now narrower than “generic hotloop loop-clone /
    exit behavior”
  - the first promotable policy candidate on that family is the existing
    hotside gate pair:
    - `LUAJIT_S390X_HOTSIDE_CANON_EQUIV=1`
    - `LUAJIT_S390X_HOTSIDE_SHARE_EQUIV=1`
  - clean `kdz` proof on the smallest reproducer:
    - artifact:
      [20260401-kdz-hotside-share-equiv-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-share-equiv-audit/summary.md)
    - `int_add_phi_only` baseline:
      - `hot 0.000786`
      - `TRACE_START 21`
      - `TEXIT_COUNT 4001`
      - `TRACEINFO_COUNT 27`
    - `SHARE_EQUIV` alone:
      - `hot 0.000659`
      - `TRACE_START 100`
      - `TEXIT_COUNT 300`
      - `TRACEINFO_COUNT 106`
      - focused logs prove `phase=share-done ... target=199` immediately
        followed by `phase=start ... snapcount=200`
    - `CANON_EQUIV + SHARE_EQUIV`:
      - `hot 0.000341`
      - `TRACE_START 3`
      - `TEXIT_COUNT 4001`
      - `TRACEINFO_COUNT 9`
    - `CANON_CHILD + SHARE_EQUIV`:
      - `hot 0.000407`
      - `TRACE_START 4`
      - `TEXIT_COUNT 4001`
      - `TRACEINFO_COUNT 10`
  - clean `kdz` sibling validation:
    - artifact:
      [20260401-kdz-hotside-canon-share-family-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-family-check/summary.md)
    - `logical_chain_tail_add`:
      - baseline `hot 0.008741`, `TRACE_START 41`, `TEXIT_COUNT 7981`
      - candidate `hot 0.002683`, `TRACE_START 2`, `TEXIT_COUNT 8000`
    - `bitops_mix`:
      - baseline `hot 0.008902`, `TRACE_START 41`, `TEXIT_COUNT 7981`
      - candidate `hot 0.002968`, `TRACE_START 2`, `TEXIT_COUNT 8000`
  - first `zkd0` screen:
    - artifact:
      [20260401-zkd0-hotside-canon-share-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-zkd0-hotside-canon-share-check/summary.md)
    - `logical_chain_tail_add`: baseline `0.018642`, candidate `0.005414`
    - `bitops_mix`: baseline `0.009459`, candidate `0.004000`
    - after tracked-file resync and rebuild, the structural counts match `kdz`:
      `TRACE_START 41 -> 2`, `TEXIT_COUNT 7981 -> 8000`
  - clean `kdz` mechanism proof:
    - artifact:
      [20260401-kdz-hotside-canon-share-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-mechanism/summary.md)
    - `SHARE_EQUIV` alone still hits the late ladder seam:
      - `parent=24 exit=0`
      - `phase=share-done ... target=199`
      - immediate `phase=start ... snapcount=200`
    - the combined canon/share policy no longer reaches that late parent in
      the warmed measured run:
      - `FOCUS_PARENT=24` logs are silent
      - the warmed `-jv` run shows only `parent=4` hotside counting and
        `phase=start parent=4 ... snapcount=200`
  - current queue correction:
    - the exit flurry is now mechanically narrowed:
      - one repeated `exit 0` self-loop seam
      - hotcount migration up an equivalent-parent clone ladder
    - the combined canon/share policy wins because it keeps the measured run
      on the early canonical seam instead of letting that hotcount migrate to
      late parents
    - aggregate exits stay flat because the loop still exits every trip
    - trace population collapses because those exits stop creating fresh later
      equivalent parents
    - the next honest target is no longer “find the cause of the flurry”
    - it is whether to promote this combined policy into one dedicated gate and
      validate it more broadly as the current throughput fix candidate
  - dedicated-gate promotion pass:
    - code:
      [lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c)
    - new gate:
      - `LUAJIT_S390X_HOTSIDE_CANON_SHARE_EQUIV=1`
    - clean `kdz` focused validation:
      - artifact:
        [20260401-kdz-hotside-canon-share-gate-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-gate-check/summary.md)
      - `int_add_phi_only`: `hot 0.000349`, `TRACE_START 3`, `TEXIT_COUNT 4001`
      - `logical_chain_tail_add`: `hot 0.003015`, `TRACE_START 2`, `TEXIT_COUNT 8000`
      - `bitops_mix`: `hot 0.003064`, `TRACE_START 2`, `TEXIT_COUNT 8000`
    - `zkd0` screen:
      - artifact:
        [20260401-zkd0-hotside-canon-share-gate-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-zkd0-hotside-canon-share-gate-check/summary.md)
      - `logical_chain_tail_add`: `hot 0.003333`, `TRACE_START 2`, `TEXIT_COUNT 8000`
      - `bitops_mix`: `hot 0.004170`, `TRACE_START 2`, `TEXIT_COUNT 8000`
    - queue correction:
      - the current throughput candidate is now the single dedicated gate, not
        the old ad hoc env pair
      - helper integration is now complete in
        [tools/s390x/build_throughput_truth_pack.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/build_throughput_truth_pack.py)
        via `--candidate hotside_canon_share`
      - helper-backed clean `kdz` artifacts now match the manual candidate
        surface:
        - [20260401-kdz-int_add_phi_only-hotside_canon_share-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-int_add_phi_only-hotside_canon_share-truth-pack)
          - `add_phi_only/hot`: JIT-on `0.000350`, `-joff` `0.000032`,
            `TRACE_START 3`, `TEXIT_COUNT 4001`
        - [20260401-kdz-logic_add_phi_noboundary-hotside_canon_share-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-logic_add_phi_noboundary-hotside_canon_share-truth-pack)
          - `logic_add_phi_noboundary/hot`: JIT-on `0.002619`, `-joff`
            `0.002159`, `TRACE_START 5`, `TRACE_ABORT 2`, `TEXIT_COUNT 4001`
        - [20260401-kdz-logical_chain_tail_add-hotside_canon_share-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-logical_chain_tail_add-hotside_canon_share-truth-pack)
          - `chain_tail_add/hot`: JIT-on `0.003052`, `-joff` `0.002541`,
            `TRACE_START 2`, `TEXIT_COUNT 8000`
        - [20260401-kdz-logical_chain_tail_store-hotside_canon_share-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-logical_chain_tail_store-hotside_canon_share-truth-pack)
          - `chain_tail_store/hot`: JIT-on `0.002800`, `-joff` `0.002016`,
            `TRACE_START 2`, `TEXIT_COUNT 8000`
        - [20260401-kdz-bitops_mix-hotside_canon_share-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-bitops_mix-hotside_canon_share-truth-pack)
          - `mix_bits/hot`: JIT-on `0.003084`, `-joff` `0.002168`,
            `TRACE_START 2`, `TEXIT_COUNT 8000`
      - helper-backed `zkd0` screen now holds on both larger reduced siblings
        with same-host A/B:
        - [20260401-zkd0-logical_chain_tail_add-baseline-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-zkd0-logical_chain_tail_add-baseline-truth-pack)
          - baseline `chain_tail_add/hot`: JIT-on `0.016437`,
            `TRACE_START 41`, `TEXIT_COUNT 7981`
        - [20260401-zkd0-logical_chain_tail_add-hotside_canon_share-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-zkd0-logical_chain_tail_add-hotside_canon_share-truth-pack)
          - candidate `chain_tail_add/hot`: JIT-on `0.003722`,
            `-joff 0.003899`, `TRACE_START 2`, `TEXIT_COUNT 8000`
        - [20260401-zkd0-bitops_mix-hotside_canon_share-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-zkd0-bitops_mix-hotside_canon_share-truth-pack)
          - `mix_bits/hot`: JIT-on `0.004230`, `-joff` `0.002933`,
            `TRACE_START 2`, `TEXIT_COUNT 8000`
        - [20260401-zkd0-logical_chain_tail_store-baseline-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-zkd0-logical_chain_tail_store-baseline-truth-pack)
          - baseline `chain_tail_store/hot`: JIT-on `0.009260`,
            `TRACE_START 42`, `TEXIT_COUNT 7983`
        - [20260401-zkd0-logical_chain_tail_store-hotside_canon_share-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-zkd0-logical_chain_tail_store-hotside_canon_share-truth-pack)
          - candidate `chain_tail_store/hot`: JIT-on `0.003955`,
            `TRACE_START 2`, `TEXIT_COUNT 8000`
      - current queue correction:
        - the dedicated gate now covers every active reduced throughput
          surface in the current queue with helper-backed evidence
        - broader non-reduced validation is also now in hand:
          - `kdz` broader-suite artifacts:
            - [20260401-kdz-hotside-canon-share-broader-suite-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-broader-suite-check/summary.md)
            - [20260401-kdz-hotside-canon-share-ffi-screen](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-ffi-screen/summary.md)
            - [20260401-kdz-hotside-canon-share-ffi-cdata-rerun](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-ffi-cdata-rerun/summary.md)
            - [20260401-kdz-hotside-canon-share-promotion-scope-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-promotion-scope-check/summary.md)
          - `zkd0` broader-suite artifacts:
            - [20260401-zkd0-hotside-canon-share-broader-suite-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-zkd0-hotside-canon-share-broader-suite-check/summary.md)
            - [20260401-zkd0-hotside-canon-share-promotion-scope-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-zkd0-hotside-canon-share-promotion-scope-check/summary.md)
        - broader-screen read:
          - the earlier `ffi_cdata/mixed_width_loop` caveat washed out on the
            same-host rerun: `0.028053 -> 0.027835` on `kdz`
          - the dedicated gate is strongly positive on broader throughput
            families on both hosts:
            - `kdz`:
              - `dispatch_trace/numeric_loop`: `0.403098 -> 0.008842`
              - `be_helpers/number_helper_loop`: `0.760303 -> 0.007868`
              - `ffi_calls/direct_abs`: `1.186680 -> 0.017131`
              - `vararg_paths/sum_loop`: `1.150344 -> 0.014361`
              - `mixed_ffi/mixed_ffi_loop`: `0.045255 -> 0.013581`
              - `mixed_noffi/mixed_loop`: `0.081497 -> 0.026823`
            - `zkd0`:
              - `dispatch_trace/numeric_loop`: `0.927825 -> 0.017705`
              - `be_helpers/number_helper_loop`: `1.863057 -> 0.009562`
              - `vararg_paths/sum_loop`: `2.688349 -> 0.019945`
              - `mixed_ffi/mixed_ffi_loop`: `0.105319 -> 0.022101`
              - `mixed_noffi/mixed_loop`: `0.145128 -> 0.053974`
          - but it is not a safe global default:
            - `iterator_table` regresses on `kdz`
              - `pairs_sum/hot`: `0.062803 -> 0.066317`
              - `pairs_array_sum/hot`: `0.063466 -> 0.076668`
            - `iterator_table` regresses on `zkd0`
              - `pairs_sum/hot`: `0.093072 -> 0.110394`
              - `pairs_array_sum/hot`: `0.119120 -> 0.129420`
        - current queue correction:
          - the dedicated gate is now a broader throughput promotion candidate
          - it is not promotable as a global s390x default while the frozen
            iterator family regresses on both hosts
          - the first simple selective activation attempts are now rejected:
            - exact loop-clone seam only:
              [20260401-kdz-hotside-canon-share-loop0-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-loop0-check/summary.md)
              - throughput still wins:
                - `add_phi_only/hot`: `0.000714 -> 0.000365`
                - `chain_tail_add/hot`: `0.008402 -> 0.003092`
                - `mix_bits/hot`: `0.008096 -> 0.003168`
              - iterator still regresses:
                - `pairs_sum/hot`: `0.062436 -> 0.068325`
                - `pairs_array_sum/hot`: `0.067516 -> 0.075800`
            - loop-clone seam plus root-`ITERN` exclusion:
              [20260401-kdz-hotside-canon-share-loop0-noitern-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-loop0-noitern-check/summary.md)
              - throughput still wins:
                - `add_phi_only/hot`: `0.000664 -> 0.000503`
                - `chain_tail_add/hot`: `0.007741 -> 0.003107`
                - `mix_bits/hot`: `0.007789 -> 0.003240`
              - iterator still regresses:
                - `pairs_sum/hot`: `0.064846 -> 0.069202`
                - `pairs_array_sum/hot`: `0.068079 -> 0.074660`
            - fast rerun after removing avoidable no-op overhead:
              [20260401-kdz-hotside-canon-share-loop0-noitern-fastcheck](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-loop0-noitern-fastcheck/summary.md)
              - `chain_tail_add/hot`: `0.007930 -> 0.003045`
              - `pairs_sum/hot`: `0.064785 -> 0.072845`
              - `pairs_array_sum/hot`: `0.068692 -> 0.076979`
          - focused iterator proof:
            - hash and array reduced `pairs()` loops both keep their first
              visible hotside seam at `parent=1 exit=1 startop=70` (`BC_ITERN`)
            - artifacts:
              - [20260401-kdz-hotside-iterator-small-focus-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-iterator-small-focus-check/raw/run.stderr.log)
              - [20260401-kdz-hotside-array-iterator-loop0-focus-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-array-iterator-loop0-focus-check/raw/run.stderr.log)
          - deeper selective activation is now proven on the current
            mechanism:
            - exact throughput activation proof on clean `kdz`:
              - old dedicated gate actual hits are concentrated on:
                - `exit=0`
                - `startop=BC_JMP` (`88`)
                - `pcop=snapop=BC_UGET` (`45`)
                - `root_startop=BC_FORL` (`79`) or `BC_FUNCF` (`89`)
              - artifact:
                [20260401-kdz-hotside-throughput-shape-proof](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-throughput-shape-proof/summary.md)
            - exact iterator non-hit proof on clean `kdz`:
              - reduced hash `pairs()`: zero actual canon/share hits
                [20260401-kdz-hotside-canon-share-selective-proof](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-selective-proof/summary.md)
              - reduced array `pairs()`: zero actual canon/share hits
                [20260401-kdz-hotside-canon-share-selective-proof](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-selective-proof/summary.md)
              - full `iterator_table`: zero actual canon/share hits
                [20260401-kdz-hotside-canon-share-iterator-bench-proof](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-iterator-bench-proof/summary.md)
            - first filtered gate:
              - `LUAJIT_S390X_HOTSIDE_CANON_SHARE_UGET_LOOPROOT=1`
              - scope:
                - `exit=0`
                - `startop=BC_JMP`
                - `pcop=snapop=BC_UGET`
                - `root_startop in {BC_FORL, BC_FUNCF}`
              - first inner-only version still left a modest iterator
                regression despite zero iterator matches, so the remaining cost
                was pure no-op check overhead
              - proof:
                [20260401-kdz-hotside-uget-looproot-match-proof/logical_chain_tail_add.summary.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-uget-looproot-match-proof/logical_chain_tail_add.summary.md)
                and
                [20260401-kdz-hotside-uget-looproot-match-proof/iterator_table.summary.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-uget-looproot-match-proof/iterator_table.summary.md)
            - hoisted prefilter retry:
              - the filtered gate now evaluates the exact shape once in
                `trace_hotside()` and skips canon/share entirely when it does
                not match
              - clean `kdz`:
                [20260401-kdz-hotside-canon-share-uget-looproot-perf](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-uget-looproot-perf/summary.md)
                - `logical_chain_tail_add/hot`: `0.003130`
                - `bitops_mix/hot`: `0.003232`
                - `pairs_sum/hot`: `0.059845`
                - `pairs_array_sum/hot`: `0.061876`
              - clean `zkd0`:
                [20260401-zkd0-hotside-canon-share-uget-looproot-perf](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-zkd0-hotside-canon-share-uget-looproot-perf/summary.md)
                - `logical_chain_tail_add/hot`: `0.004477`
                - `bitops_mix/hot`: `0.003607`
                - `pairs_sum/hot`: `0.090623`
                - `pairs_array_sum/hot`: `0.102340`
            - current queue correction:
              - `LUAJIT_S390X_HOTSIDE_CANON_SHARE_UGET_LOOPROOT=1` did clear
                the reduced seam proof and it remains the active selective
                hotside candidate on that exact `UGET`/looproot family
              - helper support now exists in
                [build_throughput_truth_pack.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/build_throughput_truth_pack.py)
                as `--candidate hotside_canon_share_uget_looproot`
              - but the first clean `kdz` broader-suite restamp closes broad
                promotion:
                [20260401-kdz-hotside-uget-looproot-broader-suite-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-uget-looproot-broader-suite-check/summary.md)
                - reduced-family win is still real elsewhere
                - broader families do not carry:
                  - `dispatch_trace/numeric_loop`: `0.341080 -> 0.340611`
                  - `mixed_ffi/mixed_ffi_loop`: `0.059399 -> 0.059215`
                  - `ffi_cdata/mixed_width_loop`: `0.027778 -> 0.027964`
                  - `vararg_paths/sum_loop`: `1.207311 -> 0.675950`
                    but still remains extremely red vs `-joff 0.005150`
                - frozen iterator still regresses:
                  - `pairs_sum/hot`: `0.068483 -> 0.076181`
                  - `pairs_array_sum/hot`: `0.066698 -> 0.084550`
              - the reduced-family host pair is now helper-backed too:
                - `kdz`
                  - `int_add_phi_only`: `0.000664`, effectively inert
                  - `logical_chain_tail_add`: `0.003169`
                  - `logical_chain_tail_store`: `0.002914`
                  - `bitops_mix`: `0.003249`
                - `zkd0`
                  - `logical_chain_tail_add`: `0.004309`
                  - `logical_chain_tail_store`: `0.004228`
                  - `bitops_mix`: `0.004763`
              - exact scope proof on clean `kdz` is now explicit:
                [20260401-kdz-hotside-uget-looproot-scope-proof](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-uget-looproot-scope-proof/summary.md)
                - `int_add_phi_only`: `match_count 0`
                - `logical_chain_tail_add`: `match_count 8792`
                - `logical_chain_tail_store`: `match_count 8792`
                - `bitops_mix`: `match_count 8792`
                - all positive reduced hits stay on:
                  - `exit=0`
                  - `op=BC_UGET`
                  - `startop=BC_JMP`
                  - `root_startop in {BC_FORL, BC_FUNCF}`
              - broader positive-family mechanism proof on clean `kdz` is now
                in hand too:
                [20260401-kdz-hotside-uget-looproot-broader-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-uget-looproot-broader-mechanism/summary.md)
                - `number_helper_loop`: `match_count 63858`
                - `be_pack_loop`: `match_count 63858`
                - `direct_abs`: `match_count 79858`
                - `stored_abs`: `match_count 79858`
                - every positive broader family stayed on:
                  - `exit=0`
                  - `op=BC_UGET`
                  - `startop=BC_JMP`
                  - `root_startop=BC_FORL`
              - representative host-pair confirmation is now in hand too:
                [20260401-zkd0-hotside-uget-looproot-broader-mechanism-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-zkd0-hotside-uget-looproot-broader-mechanism-check/summary.md)
                - `number_helper_loop`: `match_count 63858`
                - `direct_abs`: `match_count 79858`
                - both representative broader winners stayed on:
                  - `exit=0`
                  - `op=BC_UGET`
                  - `startop=BC_JMP`
                  - `root_startop=BC_FORL`
              - broader scope proof now closes the remaining ambiguity on
                `kdz`:
                [20260401-kdz-hotside-uget-looproot-promotion-scope](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-uget-looproot-promotion-scope/summary.md)
                - same-seam positive families:
                  - `retconst_loop`: `match_count 15858`
                  - `retlast_loop`: `match_count 15858`
                  - `sum_loop`: `match_count 15858`
                  - `mixed_loop`: `match_count 15858`
                - zero-hit non-targets:
                  - `mixed_ffi_loop`: `match_count 0`
                  - `pair_loop`: `match_count 0`
                  - `mixed_width_loop`: `match_count 0`
                  - `pairs_sum`: `match_count 0`
                  - `pairs_array_sum`: `match_count 0`
              - representative `zkd0` confirmation now matches that read:
                [20260401-zkd0-hotside-uget-looproot-promotion-scope-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-zkd0-hotside-uget-looproot-promotion-scope-check/summary.md)
                - same-seam positives:
                  - `retconst_loop`: `match_count 15858`
                  - `mixed_loop`: `match_count 15858`
                - zero-hit non-targets:
                  - `mixed_ffi_loop`: `match_count 0`
                  - `pairs_sum`: `match_count 0`
              - promotion scope is now explicit:
                - in-scope candidate slice:
                  - reduced `UGET`/looproot siblings
                  - `be_helpers`
                  - `ffi_calls`
                  - `vararg_paths/retconst_loop`
                  - `vararg_paths/retlast_loop`
                  - `mixed_noffi/mixed_loop`
                - same-seam but not promotion evidence:
                  - `vararg_paths/sum_loop`
                    - it still hits the same seam, but the nested-callee
                      vararg frontier remains dominant and it stays far from
                      `-joff`
                - out of scope on the current mechanism:
                  - `dispatch_trace`
                  - `iterator_table`
                  - `mixed_ffi`
                  - `ffi_cdata`
                  - plain `int_add_phi_only`
              - that scope is now also codified in
                [build_throughput_truth_pack.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/build_throughput_truth_pack.py)
                so helper-backed candidate summaries report:
                - `promotion_core`
                - `promotion_secondary`
                - `same_seam_but_dominated`
                - `out_of_scope`
              - helper-backed host-pair truth packs now cover the full
                non-dominated scoped slice:
                - `promotion_core`
                  - reduced `UGET`/looproot siblings
                  - `be_helpers`
                  - `ffi_calls`
                - `promotion_secondary`
                  - `vararg_paths/retconst_loop`
                  - `vararg_paths/retlast_loop`
                  - `mixed_noffi/mixed_loop`
              - the secondary slice now has enough baseline/candidate A/B
                to stay explicitly secondary:
                - `kdz`
                  - `retlast_loop/hot`: `0.029461 -> 0.003093`
                  - `retconst_loop/hot`: `0.028523 -> 0.001660`
                  - `mixed_loop/hot`: `0.084383 -> 0.036412`
                  - `sum_loop/hot`: `1.123796 -> 0.675104`, still dominated
                - `zkd0`
                  - `retlast_loop/hot`: `0.052306 -> 0.012619`
                  - `retconst_loop/hot`: `0.063160 -> 0.003574`
                  - `mixed_loop/hot`: `0.189387 -> 0.079668`
                  - `sum_loop/hot`: `2.894826 -> 2.985526`, still dominated
                - the `zkd0` vararg baseline rows are already present in the
                  raw baseline logs even though the helper summary has not yet
                  flushed:
                  - [jit-on](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-zkd0-vararg_paths-baseline-truth-pack/raw/jit-on.stdout.log)
                  - [joff](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-zkd0-vararg_paths-baseline-truth-pack/raw/joff.stdout.log)
                - `kdz`
                  - [20260401-kdz-be_helpers-hotside_canon_share_uget_looproot-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-be_helpers-hotside_canon_share_uget_looproot-truth-pack/summary.md)
                    - `number_helper_loop/hot`: `0.008169` vs `-joff 0.002285`
                    - `be_pack_loop/hot`: `0.023346` vs `-joff 0.018789`
                  - [20260401-kdz-ffi_calls-hotside_canon_share_uget_looproot-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-ffi_calls-hotside_canon_share_uget_looproot-truth-pack/summary.md)
                    - `direct_abs/hot`: `0.018044` vs `-joff 0.009995`
                    - `stored_abs/hot`: `0.012581` vs `-joff 0.006919`
                  - [20260401-kdz-vararg_paths-hotside_canon_share_uget_looproot-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-vararg_paths-hotside_canon_share_uget_looproot-truth-pack/summary.md)
                    - `retlast_loop/hot`: `0.003093` vs `-joff 0.002025`
                    - `retconst_loop/hot`: `0.001660` vs `-joff 0.000591`
                    - `sum_loop/hot`: `0.675104` vs `-joff 0.004523`,
                      still dominated
                  - [20260401-kdz-mixed_noffi-hotside_canon_share_uget_looproot-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-mixed_noffi-hotside_canon_share_uget_looproot-truth-pack/summary.md)
                    - `mixed_loop/hot`: `0.036412` vs `-joff 0.003764`
                - `zkd0`
                  - [20260401-zkd0-be_helpers-hotside_canon_share_uget_looproot-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-zkd0-be_helpers-hotside_canon_share_uget_looproot-truth-pack/summary.md)
                    - `number_helper_loop/hot`: `0.015573` vs `-joff 0.003520`
                    - `be_pack_loop/hot`: `0.051882` vs `-joff 0.039644`
                  - [20260401-zkd0-ffi_calls-hotside_canon_share_uget_looproot-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-zkd0-ffi_calls-hotside_canon_share_uget_looproot-truth-pack/summary.md)
                    - `direct_abs/hot`: `0.039104` vs `-joff 0.024293`
                    - `stored_abs/hot`: `0.041040` vs `-joff 0.019167`
                  - [20260401-zkd0-vararg_paths-hotside_canon_share_uget_looproot-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-zkd0-vararg_paths-hotside_canon_share_uget_looproot-truth-pack/summary.md)
                    - `retlast_loop/hot`: `0.012619` vs `-joff 0.003196`
                    - `retconst_loop/hot`: `0.003574` vs `-joff 0.000849`
                    - `sum_loop/hot`: `2.985526` vs `-joff 0.007298`,
                      still dominated
                  - [20260401-zkd0-mixed_noffi-hotside_canon_share_uget_looproot-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-zkd0-mixed_noffi-hotside_canon_share_uget_looproot-truth-pack/summary.md)
                    - `mixed_loop/hot`: `0.079668` vs `-joff 0.006760`
              - promotion decision:
                - keep the envless filtered seam as the active promoted/default
                  scoped throughput surface, with
                  `LUAJIT_S390X_HOTSIDE_CANON_SHARE_UGET_LOOPROOT=1` kept as a
                  compatibility alias, for:
                  - `promotion_core` as the immediate promotion surface
                  - `promotion_secondary` as carry-forward same-seam evidence,
                    not the first promotion bar
                - keep `vararg_paths/sum_loop` out of promotion evidence
                - keep `dispatch_trace`, `iterator_table`, `mixed_ffi`,
                  `ffi_cdata`, plain `int_add_phi_only`, and
                  `logic_add_phi_noboundary` out of this candidate surface
              - so the next honest target is no longer scope discovery,
                helper codification, the promotion call itself, or host-pair
                completion inside this slice
              - it is selective promotion planning from this fully
                helper-backed split: core promotion first, secondary
                same-seam carry-forward second
              - immediate policy consequence:
                - first promotion surface should be `promotion_core` only
                - `promotion_secondary` remains evidence for the same mechanism,
                  but not the first default/enable set
              - promotion note:
                - [hotside-uget-looproot-promotion.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/hotside-uget-looproot-promotion.md)
                  is now the checked-in first-enable boundary for this gate
              - next honest target from this queue:
                - broader rollout criteria from that checked-in boundary, now
                  that the dedicated runner has completed the full host-pair
                  wave on the envless promoted default through
                  [build_hotside_promotion_slice.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/build_hotside_promotion_slice.py),
                  not more slice discovery
              - runner-backed host-pair read:
                - every core family summary now stamps
                  `family scope status: promotion_core`
                - every core family summary now stamps
                  `promotion action: eligible_first_enable_set`
                - all runner-produced reduced trace probes completed with
                  `REMOTE_RC 0` on both `kdz` and `zkd0`
              - helper fence wave:
                - [tools/s390x/build_iterator_truth_pack.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/build_iterator_truth_pack.py)
                  and
                  [tools/s390x/build_dispatch_truth_pack.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/build_dispatch_truth_pack.py)
                  now accept explicit candidate selection:
                  - `baseline` uses
                    `LUAJIT_S390X_DISABLE_HOTSIDE_CANON_SHARE_UGET_LOOPROOT=1`
                  - `hotside_canon_share_uget_looproot_default` uses the
                    envless promoted default
                - clean `kdz` frozen-family read is now pinned from those
                  helper-backed packs:
                  - iterator:
                    [baseline](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-201055-kdz-baseline-iterator-truth-pack/summary.md)
                    vs
                    [promoted default](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-201051-kdz-hotside_canon_share_uget_looproot_default-iterator-truth-pack/summary.md)
                    - `pairs_sum/hot`: `0.062376 -> 0.058992`
                    - `pairs_array_sum/hot`: `0.072711 -> 0.064010`
                    - trace/exit shape unchanged:
                      - `hash_value`: `TRACE_START 10`, `TRACE_ABORT 9`,
                        `TEXIT_COUNT 960000`
                      - `hash_key`: `TRACE_START 10`, `TRACE_ABORT 9`,
                        `TEXIT_COUNT 640000`
                      - `array_value`: `TRACE_START 11`, `TRACE_ABORT 10`,
                        `TEXIT_COUNT 960000`
                    - interpretation:
                      - promoted default is effectively inert on the frozen
                        iterator seam and does not reopen the old regression
                        line on `kdz`
                  - dispatch:
                    [baseline](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-201441-kdz-baseline-dispatch-truth-pack/summary.md)
                    vs
                    [promoted default](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-201748-kdz-hotside_canon_share_uget_looproot_default-dispatch-truth-pack/summary.md)
                    - `numeric_loop/hot`: `0.683002 -> 0.684303`
                    - `side_exit_loop/hot`: `0.373787 -> 0.382921`
                    - `hotexit_loop/hot`: `0.532836 -> 0.536542`
                    - trace/exit shape unchanged:
                      - `numeric_loop`: `TRACE_START 12`, `TRACE_ABORT 0`,
                        `TEXIT_COUNT 2001`
                      - `side_exit_loop`: `TRACE_START 12`, `TRACE_ABORT 0`,
                        `TEXIT_COUNT 2001`
                      - `hotexit_loop`: `TRACE_START 11`, `TRACE_ABORT 0`,
                        `TEXIT_COUNT 2001`
                    - interpretation:
                      - promoted default is also effectively inert on the
                        frozen dispatch seam on `kdz`
                - queue correction:
                  - the host-pair frozen-family fence is now complete
                  - the next honest target is no longer another frozen-family
                    fence pass; it is the next scoped throughput seam outside
                    iterator and dispatch
- that first invariant-driven reduced-probe gate is now rejected on clean
  `kdz`:
  - artifact:
    [20260401-kdz-low32home-add-boundary-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-low32home-add-boundary-check/summary.md)
  - gated surface:
    - keep current 64-bit logical lowering
    - skip producer-side `asm_bnorm32()` only for proven logical-chain carry
      nodes and `ADD`-tail nodes
    - insert explicit normalize at the `ADD` consumer boundary
  - host result:
    - `logical_chain_tail_add`: baseline `0.008265`, gated `0.008807`,
      regression `+0.000542s` (`1.066x`)
    - `logical_chain_tail_store`: baseline `0.006822`, gated `0.007940`,
      regression `+0.001118s` (`1.164x`)
  - structural read:
    - the gate was real:
      - `add-boundary:add-tail`: `46`
      - `skip-bnorm:add-tail`: `46`
      - `skip-bnorm:carry`: `483` on add-tail, `487` on store-tail
      - logged `asm_bnorm32()` totals fell to `439` on add-tail and `489` on
        store-tail
    - but both reduced trace probes timed out with `REMOTE_RC=124`
  - result:
    - source returned to the non-behavior baseline after the host check
    - explicit add-boundary normalization plus carry-skip is closed
    - if `bitops_mix` stays open from here, the next honest target is no
      longer another partial boundary gate
    - it is either:
      - a fuller stateful low32-home / normalized-result contract that avoids
        the timeout/regression shape entirely
      - or closure of this backend family too
- the next clean-host classifier narrows that remaining contract again:
  - artifact:
    [20260401-kdz-addhome-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-addhome-audit/summary.md)
  - `logical_chain_tail_add` does expose a real post-bitop carry seam:
    - `70` plain non-guard integer `ADD` sites matched the low32-home carry
      shape
    - split:
      - `46` where the current bitop result is the only low32-home source
      - `24` where both the carried total and current bitop result are already
        in the same low32-home carry family
    - those `ADD` sites only feed `PHI` / later plain `ADD`, not store or
      guard consumers
  - `logical_chain_tail_store` does not:
    - no `ADD` site matched the same carry shape there
    - the bitop chain still first leaves into `ASTORE`, so store-tail remains
      a hard consumer boundary
  - classifier note:
    - the verbose `LUAJIT_S390X_ADDHOME_LOG=1` `hotloop=1` probes timed out
      with `REMOTE_RC=124`, so this is still a structural classification pass,
      not a perf gate
  - next honest target:
    - if `bitops_mix` stays open, the next backend family is no longer
      “normalize at the `ADD` boundary”
    - it is a fuller stateful low32-home carry across plain non-guard integer
      `ADD` plus `PHI`
    - `ASTORE`, guard/compare (`LE`), helper, and other noncarry consumers stay
      as hard boundaries until proven otherwise
- the first native `kdz` pass on that `ADD`/`PHI` carry gate is now rejected:
  - artifact:
    [20260401-kdz-low32home-addphi-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-low32home-addphi-check/summary.md)
  - clean-host result:
    - `logical_chain_tail_add`: gated median `0.007441`
    - `logical_chain_tail_store`: gated median `0.006737`
    - the proof scripts terminated cleanly:
      - `proof_add`: `REMOTE_RC=0`, `skip_count=2`
      - `proof_store`: `REMOTE_RC=0`, `skip_count=0`
    - but both reduced trace probes timed out:
      - `chain_tail_add`: `REMOTE_RC=124`
      - `chain_tail_store`: `REMOTE_RC=124`
  - result:
    - source returned to the non-behavior baseline after the host check
    - this exact low32-home `ADD`/`PHI` carry gate is closed on the current
      mechanism
    - the remaining backend choice is now narrower:
      - either a fuller stateful low32-home / normalized-result contract that
        keeps the reduced trace probes finite and normalizes before any
        guard/compare, helper/call, store, or snapshot-visible exit boundary
      - or closure of the `bitops_mix` backend family too
  - design read:
    - safe internal carry family on the current lowering surface is only:
      - bitop logic/unary/shift/rotate
      - plain non-guard integer `ADD`
      - loop `PHI` when both incoming arms stay in the same family
    - hard boundaries remain:
      - guard/compare sites
      - helper/call boundaries
      - store consumers such as `ASTORE`
      - snapshot-visible exit/restore paths
- the next clean `kdz` boundary classifier narrows the live backend seam
  again:
  - artifact:
    [20260401-kdz-low32home-boundary-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-low32home-boundary-audit/summary.md)
  - `logical_chain_tail_add`:
    - the extended low32-home family mostly stays internal:
      - `can_carry=1`: `1039`
      - `can_carry=0`: `94`
    - first hard consumer split:
      - `guard`: `65`
      - `other`: `29`
      - `store`: `0`
  - `logical_chain_tail_store`:
    - this is a genuinely different consumer seam:
      - `can_carry=1`: `898`
      - `can_carry=0`: `138`
    - first hard consumer split:
      - `store`: `68`
      - `guard`: `45`
      - `other`: `25`
  - `bitops_mix`:
    - treat the live benchmark family as matching the add-tail seam, not the
      store-tail seam
    - the active compiled-body red is therefore add/guard-boundary dominated,
      not store-boundary dominated
  - classifier note:
    - the reduced trace probes still timed out with `REMOTE_RC=124` under the
      verbose logger, so this is structural attribution only
  - next honest target:
    - if `bitops_mix` stays open, the next backend family is no longer a
      generic low32-home contract and no longer store-tail
    - it is low32-home consumption at the compare/guard boundary on the
      add-tail family
- reduced clean `kdz` compare-boundary check now names the active consumer
  exactly:
  - artifact:
    [20260401-kdz-low32cmp-add-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-low32cmp-add-check/summary.md)
  - reduced add-tail probe result:
    - `BUILD_RC=0`
    - `RUN_RC=0`
    - `RESULT 1746150614`
  - compare summary:
    - total low32-home compare consumers: `19`
    - `intcomp`: `14`
    - `equal`: `5`
    - compare ops:
      - `LE`: `14`
      - `NE`: `5`
    - source shape:
      - left source is always `ADD`: `19`
      - right source is always constant: `19`
      - unsigned compare path is never used: `cmp32u=0` for all `19`
      - signed immediate compare path covers the hot majority:
        - `imm16_signed=1`: `14`
        - `imm16_signed=0`: `5`
  - source read:
    - this matches the current lowering in
      [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h):
      - `asm_intcomp()` uses `CGHI` on the signed-immediate path
      - `asm_equal()` uses `CGR` for the remaining equality guard path
    - [src/lj_emit_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_emit_s390x.h)
      currently exposes only the 64-bit compare forms used here:
      `CGR`, `CLGR`, `CGHI`
    - there is no existing 32-bit compare-consumer surface already wired into
      this backend
  - queue consequence:
    - the live `bitops_mix` boundary is no longer “generic compare/guard”
    - it is specifically the carried-`ADD` into signed immediate `LE` loop
      compare boundary, with constant `NE` equality as the secondary consumer
    - the next honest backend target is compare/guard consumption at that
      exact boundary, not another store-tail or broad skip variant
    - if this family stays open, the next code branch is a real compare
      consumer design with emitter support, not another local normalization
      skip
- the next clean `kdz` proof corrects that compare read one layer deeper:
  - artifact:
    [20260401-kdz-low32cmp-addkind-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-low32cmp-addkind-check/summary.md)
  - compare summary still matches the earlier read numerically:
    - total compare consumers: `19`
    - `LE`: `14`
    - `NE`: `5`
    - left source `ADD`: `19`
    - right source constant: `19`
  - but the add-kind split resolves the ambiguity:
    - left add kind `ctrl_inc`: `19`
    - left add kind `bitop_tail`: `0`
  - reduced clean `kdz` `-jdump=is` proof matches that result:
    - hot `LE` is on the induction increment `i + 1 <= 200`
    - hot `NE` is the zero check on that same induction value in the traced
      `arshift` path
    - the carried value path remains separate:
      - `ADD total, bitop_chain`
      - then `PHI total`
  - queue consequence:
    - the compare/guard frontier is a loop-control seam, not the carried
      bitop value seam
    - close compare-consumer work for `bitops_mix` on the current mechanism
    - the next honest backend target reverts to the carried value path only:
      low32-home through value-tail `ADD` plus loop `PHI`, explicitly
      excluding the control-increment `ADD + 1` compare path
- first exact value-tail `ADD` / `PHI` gate is rejected on clean `kdz`:
  - artifact:
    [20260401-kdz-low32valueaddphi-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-low32valueaddphi-check/summary.md)
  - gate shape:
    - `LUAJIT_S390X_LOW32VALUEADDPHI=1`
    - only plain non-guard integer `ADD`
    - `op2` must be non-constant
    - at least one source must be a real bitop producer
    - carry users restricted to `ADD` / `PHI`
    - control increment `ADD + 1` excluded by construction
  - reduced checks:
    - add-tail reduced probe completed and hit the intended seam:
      - `candidates=10`, `skips=10`
    - store-tail reduced probe completed and produced no hits
  - structural gate:
    - add-tail trace probe: `RUN_RC=124`
    - store-tail trace probe: `RUN_RC=124`
  - result:
    - reject this exact value-tail producer-side skip gate
    - current source baseline keeps only the classifier work
    - if `bitops_mix` stays open, the next honest backend target is deeper
      than another producer-side `asm_bnorm32()` skip
    - it would need a fuller normalized-result / low32-home contract that
      stays finite through real trace formation
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
  - the first exact skip experiment is now rejected:
    - clean `kdz` artifact root:
      - [20260401-kdz-bitop-chain-bnorm-skip-direct-v2](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-bitop-chain-bnorm-skip-direct-v2)
    - scope:
      - skip `asm_bnorm32()` only on binary `band` / `bor` / `bxor` nodes
        whose two operands are already prior bitops
    - structural read:
      - `S390X_BNORM_SKIP` fired heavily on clean `kdz`, so the classifier did
        hit the intended chain nodes
    - perf read:
      - `mix_bits/hot` regressed from frozen `0.007645` to `0.008870`
      - `mix_bits/small` also regressed from `0.000273` to `0.000340`
    - result:
      - the naive chain-node delete path is closed
    - the live question is no longer “can we just skip those normalizations?”
    - it is whether any stateful/int32-home variant can reduce the payer
      without hurting the hot path
  - focused clean-`kdz` mcode dump now pins the hot loop body shape:
    - artifact root:
      - [20260401-kdz-bitops-mcode-audit-v3](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-bitops-mcode-audit-v3)
    - using the repo-local dump module via `LUA_PATH=./src/?.lua;;`, the hot
      loop body shows repeated raw opcode triplets for the binary chain:
      - `b904` (`LGR`) move into the destination
      - `b980` / `b981` / `b982` (`NGR` / `OGR` / `XGR`)
      - `b914` (`LGFR`) post-op renormalization
    - `b91f` (`LRVR`) for `BSWAP` is also followed by `b914`
    - no 32-bit logical register forms appear anywhere in the dumped hot body
  - that narrows the next backend question again:
    - the current s390x emitter is really paying `64-bit logical op + LGFR`
      across the chain
    - but clean source review now shows that this is not a bitops-local contract:
      - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h#L1341)
        `asm_add()` uses the same integer-result pattern
      - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h#L1635)
        `asm_sub()` does too
      - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h#L1710)
        `asm_mul()` also brackets the op with `LGFR`
      - [src/lj_emit_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_emit_s390x.h#L97)
        currently defines only the 64-bit register forms used here:
        `AGR`, `SGR`, `NGR`, `OGR`, `XGR`, `MSGFR`
      - there are no existing 32-bit register `AR` / `SR` / `NR` / `OR` / `XR`
        forms wired into the active emitter path
    - if this family stays open, the next honest target is no longer a
      bitops-only tweak
    - it is whether the s390x backend has a broader 32-bit integer-result
      lowering surface that would have to be added, or whether the current
      `64-bit op + LGFR` contract is fundamental on this backend
  - one full backend-wide 32-bit lowering pass is now classified and rejected:
    - native `kdz` semantics probes showed the candidate 32-bit ops do not
      auto-normalize in 64-bit mode:
      - `AR`, `SR`, `NR`, `OR`, `XR`, `AHI`, `MSR`, and `LR` all preserve the
        stale high 32 bits
      - so the current backend still needs an explicit normalize step after
        those ops
    - first code pass:
      - three-register arithmetic/logical forms plus the existing normalize
        contract
      - clean truth-pack artifact:
        [20260401-kdz-bitops_mix-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-bitops_mix-truth-pack)
      - `mix_bits/hot` regressed from frozen `0.007645` to `0.008957`
    - second code pass:
      - two-register `AR` / `SR` / `NR` / `OR` / `XR`
      - `AHI`
      - direct `CC_OF` guards for int32 `addov` / `subov`
      - selective `LR` where low-32 setup before a later normalize was enough
      - best clean `kdz` rerun improved to `mix_bits/hot 0.007879`, but still
        missed the frozen `0.007645` host bar
    - follow-up shift setup variants were both negative:
      - remove the setup move: `0.008114`
      - use `LR` for the setup move: `0.008079`
    - result:
      - source is back on the frozen baseline
      - the backend-wide opcode-swap family is closed on the current
        normalize-every-result contract
      - if this line reopens, the next honest family is a deeper
        normalized-result / int32-home design, not more local opcode swaps
      - first clean `kdz` classifier for that deeper family is now in:
        [20260401-kdz-bitops-int32home-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-bitops-int32home-audit/summary.md)
      - corrected `asm_bnorm32()` read:
        - `chain-binary`: `1337` total, `1146` pure bitop-to-bitop carry sites,
          `174` tail sites that first leave the chain through `ADD`
        - `source-binary`: `189` total, all still feed later bitops
        - `source-shift`: `763` total, all still feed later bitops
        - `source-unary`: `382` total, all still feed later bitops
      - the only named first non-bitop consumer in the hot probe is op `41`
        (`ADD`), hit `190` times
      - the next honest backend family is therefore narrower than generic
        opcode replacement:
        carry a normalized int32/result-home through the bitop chain and only
        pay the boundary normalize where the chain leaves into integer
        arithmetic
      - first env-gated carry-skip attempt on that seam is now rejected:
        [20260401-kdz-bitops-int32home-gate-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-bitops-int32home-gate-check/summary.md)
      - clean `kdz` result:
        - baseline `mix_bits/hot 0.008095`
        - gated `mix_bits/hot 0.008593`
        - regression `+0.000498s` (`1.062x`)
      - structural read:
        - the gate hit `2392` candidate sites in the focused hot probe
        - the probe still terminated cleanly with `REMOTE_RC=0`
      - conclusion:
        - the named int32-home boundary is real
        - but simple carry-site skip on the current lowering shape is not
          promotable
        - if this family stays open, the next cut has to preserve a real
          int32-home/result state, not just skip `LGFR` at candidate producers
      - first low32-home logical-subchain variant is now also rejected:
        [20260401-kdz-bitops-low32home-subchain-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-bitops-low32home-subchain-check/summary.md)
      - implementation shape:
        - only `band` / `bor` / `bxor`
        - only classifier-proven carry and `ADD`-tail nodes
        - 32-bit `LR` / `NR` / `OR` / `XR` low32-home lowering, no internal
          post-op normalize on those nodes
      - clean `kdz` result:
        - first focused pass was ambiguous:
          - baseline `mix_bits/hot 0.008784`
          - gated `mix_bits/hot 0.008609`
        - but the no-rebuild rerun flipped negative:
          - baseline `mix_bits/hot 0.008312`
          - gated `mix_bits/hot 0.008749`
      - structural read:
        - the reduced gated check terminated cleanly
        - the new path did fire:
          - `logic32carry 21`
          - `logic32tail 2`
        - the focused trace-count script still timed out in both baseline and
          gated forms, so it did not provide a usable structural discriminator
      - result:
        - source is back on the non-behavior baseline
        - this exact low32-home logical-subchain variant is not promotable
        - any remaining backend line here has to be a deeper consumer-side
          normalized-result / int32-home design, or this family should close
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
- the branch now has a reproducible post-cleanup JIT-on measurement path
- the current live non-iterator throughput candidate is
  `LUAJIT_S390X_HOTSIDE_CANON_SHARE_EQUIV=1`
- that candidate is broadly positive on throughput families, but it regresses
  the frozen iterator family on both `kdz` and `zkd0`

### Next 1-3 work sessions

- keep the docs in sync with the measured branch-tip state
- preserve the clean `kdz` and `zkd0` validation surfaces
- treat the current Lane A plus Lane B freeze point as the shipping iterator
  position
- keep the envless filtered hotside path as the shipping throughput default for
  the scoped positive slice only
- do not reopen the reduced `BC_ISF` callback path from the rejected
  signed-extraction prototype; that is probe-hook noise, not the next workload
  seam
- use the new no-counter plus no-posthooks reduced probe path to separate
  future header-repair reruns from probe-side Lua scaffolding
- keep the next live question on replay after the first successful hot helper
  run once the inherited integer `SLOAD` typecheck starts passing
- the current replay seam is no longer just “typecheck passes then something
  later breaks”:
  - direct two-hot host replay shows repeated failure on `trace 1 exit 0`,
    restored `BC_UGET`, exact-taken `guardmark=0xe`
  - on the real workload trace, that mark is `curins 14`, `int MULOV 0003
    +65537`
  - `0003` is `int SLOAD #4 TI`
  - repeated runtime state at that seam is packed numeric-`for` replay
    (`r11=0x8001`, `r12=0xffffffff80018001`, then incrementing), not a plain
    loop index
- source-side contract read:
  - the VM integer `FORI/FORL` fast path in
    [vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc#L4331)
    is explicitly `checkint -> 32-bit add -> setint -> store`
  - so the next live question is not another type-compare tweak
  - it is why replay after the inherited `SLOAD` typecheck still does not
    re-materialize that same cleared 32-bit numeric-for value before the
    header `MULOV`
- tighter entry-path read:
  - `TRACE 1` is the loop trace and starts at `BC_FORL`
  - `TRACE 2` is a tiny `FUNCF` root that only guards `n` and then stops
    `-> 1`
  - that `stop -> 1` shape matches the compiled-loop handoff path in
    [lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c#L3618),
    not the VM `FORI/FORL` materialization path
  - `lj_snap_replay()` only recreates inherited `IR_SLOAD` refs for that
    handoff; it does not run the VM `FORI/FORL` integer materialization path
  - the repeated second-run bad value (`0x8001`, `0x8002`, `0x8003`, ...)
    therefore points to stale inherited numeric-for index/current-value state
    reaching `TRACE 1` before the first body arithmetic use
- next honest target:
  - identify where that function-entry handoff is supposed to rematerialize
    VM-style numeric-for state before linking into `TRACE 1`
  - the current best source-backed candidate is
    [rec_for(..., isforl=0)](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c#L1127):
    it proves loop entry and can stop into the existing loop trace, but it
    does not emit the VM-style `idx = idx + step`, retag/store, and `EXT`
    mirror before that handoff
  - do not reopen low32-home, helper-header lookup, iterator, dispatch, or
    generic hotside population work

- Timestamp: `2026-04-02 14:32:00 PDT`
- The next paired host slice closes one wrong theory and one real blocker
  - correction:
    - `BC_JFORI` does **not** perform the VM `idx += step` update
    - on s390x that update only happens on the `BC_JFORL` / `BC_IFORL` path in
      [vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc#L4331)
    - `BC_JFORI` checks the current integer loop state and stores the current
      visible `FOR_EXT` value before `JLOOP`
  - paired gate under clean `kdz`:
    - `LUAJIT_S390X_GC64_SIGNED_INT_SLOAD=1`
    - `LUAJIT_S390X_JFORI_INTERP_HANDOFF=1`
    - two-hot pure-add and real `number_helper_loop` canaries both return the
      correct first and second hot results:
      [20260402-kdz-gc64-signed-sload-plus-jfori-handoff](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-gc64-signed-sload-plus-jfori-handoff/summary.md)
  - but the focused real-workload counter read is still flat:
    - baseline and paired gate both stay at `TRACE_START 7`, `TRACE_STOP 5`,
      `TRACE_ABORT 1`, `TEXIT_COUNT 64001`
    - the only structural movement is the tiny entry trace:
      - baseline `TRACEINFO 2 1 root 4 6 3`
      - paired gate `TRACEINFO 2 0 interpreter 4 6 3`
  - exact exit read under the pair:
    - repeated seam is still `trace 1 exit 0`
    - restored `pc op=45` / `snapop=45` (`BC_UGET`)
    - exact taken `guardmark` is still `0x3`
    - runtime state now shows the signed compare register already corrected
      (`r4=0xfffffffffffffff2`), while the current loop value still increments
      as plain low integers (`r11=0x3`, `0x4`, `0x5`, ...)
  - queue correction:
    - direct `JFORI` root-linking was a real secondary correctness blocker once
      the inherited integer `SLOAD` typecheck started passing
    - it is not the primary steady-state exit payer on the promoted slice
  - the next live seam is still stack-visible/current-value materialization
      for the inherited visible numeric-for value at restored `SNAP #0`, not
      another `JFORI` population tweak

- Timestamp: `2026-04-02 16:05:00 PDT`
- The next recorder/header slice splits the dynamic numeric-for seam cleanly
  - focused `fori_arg()` proof on clean `kdz`:
    - on the real `number_helper_loop` path, hidden numeric-for `STEP` is
      already constantized during recording
    - hidden numeric-for `STOP` is not; it stays inherited from the runtime
      stop argument `n`
    - so the current dynamic seam is not “constant `STEP` replay failed”
      and not “the recorder forgot a const initializer for both hidden args”
  - literal-stop sibling:
    - a reduced helper variant with `for i = 1, 400 do` constantizes both
      hidden `STOP` and hidden `STEP`
    - the repeated exit flurry still survives there
    - but it shifts off the old dynamic-form mark and onto a later guard:
      `guardmark=0xd`
  - queue correction:
    - inherited hidden `STOP` replay is the front-most dynamic header seam
    - it is not the whole payer by itself
    - stabilizing `STOP`/`STEP` only exposes a later header/body guard, so the
      next live seam is the shifted post-constantization guard family, not a
      direct recorder const-init repair

- Timestamp: `2026-04-02 13:44:40 PDT`
- Clean isolated literal-stop probe corrects the mixed-log reading on the
  shifted seam
  - artifact:
    [20260402-kdz-number-helper-literal-stop-exact-seam](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-number-helper-literal-stop-exact-seam/summary.md)
  - clean `kdz` reduced literal-stop sibling under the promoted default:
    - `TRACE_START 1`
    - `TRACE_STOP 1`
    - `TRACE_ABORT 0`
    - `TEXIT_COUNT 400`
    - dominant texit `7:0=400`
    - dominant exit still restores at `BC_UGET`:
      - `op 45`
      - `snapop 45`
      - `snapnent 0`
  - exact runtime guard on that dominant seam:
    - `curins=13`
    - `IR=SLOAD`
    - `op1=2`
    - `op2=4`
    - `sload_int ofs=0 extra=4`
  - exact semantic mapping from the same trace IR:
    - `TRACE 1` for the isolated sibling shows:
      - `0001 int SLOAD #3 I`
      - `0012 int MULOV 0001 +65537`
      - `0013 int SLOAD #2 T`
      - `0016 int ADD 0013 0012`
    - so the shifted exact runtime failure is the carried `total` reload,
      not a helper-header lookup guard
  - correction:
    - the earlier mixed `guardmark=0xd` / `GGET` read from the broad
      `fori-const-init-v1` artifact was a later alternating family in mixed
      logs, not the steady literal-stop seam
  - queue correction:
    - once hidden `STOP`/`STEP` are constantized, the live shifted seam is
      still restored `SNAP #0` replay/typecheck on stack-visible carried state
    - the next honest target is carried-`total` replay/materialization under
      the promoted default, not helper-header stabilization

- Timestamp: `2026-04-02 13:57:00 PDT`
- Literal-stop slot logging closes the rematerialization-vs-typecheck split
  - artifact:
    [20260402-kdz-number-helper-literal-stop-slotlog](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-number-helper-literal-stop-slotlog/summary.md)
  - focused `kdz` read at the repeated `guardmark=0xd` seam:
    - dominant runtime guard is still:
      - `curins=13`
      - `IR=SLOAD`
      - `op1=2`
      - `op2=4`
      - `sload_int ofs=0 extra=4`
    - slot dump at that same exit shows the restored carried state is already
      int-tagged and numerically sane:
      - `idx=0` `itype=-14` `u64=0xfff9000000030003`
      - that low word is `0x00030003`, which is the correct carried
        `total = 3 * 65537`
      - nearby numeric-for state is also coherent:
        - `idx=1` `u64=...00000003`
        - `idx=2` `u64=...00000190`
        - `idx=3` `u64=...00000001`
  - correction:
    - the shifted seam is no longer best described as missing carried-`total`
      rematerialization
    - the carried `total` is already present in the restored frame as an
      integer TValue
  - queue correction:
    - the live question is now the inherited GC64 integer `SLOAD`
      typecheck/extraction contract on valid restored int slots
    - not helper-header stabilization
    - not another stack-state rematerialization theory

- Timestamp: `2026-04-02 14:08:30 PDT`
- Direct GC64 inherited-int replay repair is now promoted as the active GC64
  default pair on s390x
  - source change:
    - signed/arithmetic GC64 integer `SLOAD` extraction defaults on under
      `LJ_GC64`, with opt-out
      `LUAJIT_S390X_DISABLE_GC64_SIGNED_INT_SLOAD=1`
    - matching `JFORI` interpreter handoff defaults on under `LJ_GC64`, with
      opt-out `LUAJIT_S390X_DISABLE_JFORI_INTERP_HANDOFF=1`
    - old opt-in envs remain as compatibility aliases
  - clean `kdz` literal-stop reducer:
    - artifact:
      [20260402-kdz-literal-stop-paired-default-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-literal-stop-paired-default-check/summary.md)
    - `TRACE_START 1`, `TRACE_STOP 1`, `TRACE_ABORT 0`, `TEXIT_COUNT 399`
    - old carried-`total` inherited-int `SLOAD` seam no longer repeats
    - repeated exits advance into a later `BC_TGETS` family
  - clean `kdz` real helper workload:
    - artifact:
      [20260402-kdz-number-helper-paired-default-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-number-helper-paired-default-check/summary.md)
    - `TRACE_START 6`, `TRACE_STOP 5`, `TRACE_ABORT 0`, `TEXIT_COUNT 64001`
    - no correctness failure under the envless pair
    - front-most inherited-int `SLOAD` failure is gone; the steady flurry is
      now later than that seam
  - clean `zkd0` reduced screen:
    - artifact:
      [20260402-zkd0-number-helper-paired-default-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-zkd0-number-helper-paired-default-check/summary.md)
    - `TRACE_START 6`, `TRACE_STOP 5`, `TRACE_ABORT 0`, `TEXIT_COUNT 64001`
    - no z14 correctness regression on the real workload
  - queue correction:
    - the inherited GC64 integer `SLOAD` replay/typecheck seam is directly
      remediated
    - this is not yet a full perf win because the real helper workload still
      has a later steady header seam
    - exact next-seam attribution remained open

- Timestamp: `2026-04-02 14:20:46 PDT`
- Post-repair exact-taken attribution corrects the next-seam read again
  - real helper workload exact-taken proof:
    [20260402-kdz-number-helper-postrepair-guardmark](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-number-helper-postrepair-guardmark/summary.md)
    - `TRACE_START 6`, `TRACE_STOP 5`, `TRACE_ABORT 0`, `TEXIT_COUNT 64001`
    - dominant seam remains `trace 7 exit 0` at restored `BC_UGET`
    - exact taken guard is now:
      - `curins 3`
      - `IR SLOAD`
      - `op1 4`
      - `op2 36`
      - `sload_int ofs 16 extra 20`
    - so the live post-repair seam on the real helper workload is the
      inherited numeric-for index/current-value `SLOAD`, not a `BC_TGETS`
      guard
  - reduced helper-variant split after the repair:
    [20260402-kdz-postrepair-helper-variant-only](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-postrepair-helper-variant-only/summary.md)
    - original helper form:
      - `number_helper_literal_stop`: `TRACE_START 1`, `TEXIT_COUNT 399`
      - exact taken guard remains the carried-state `SLOAD #2 T`
    - local helper form:
      - `number_helper_local_tobit`: `TRACE_START 1`, `TEXIT_COUNT 0`
    - arg helper form:
      - `number_helper_arg_tobit`: `TRACE_START 2`, `TEXIT_COUNT 0`
    - pure-add sibling under the repaired default is no longer part of this
      helper-header slice; it explodes into trace population and aborts with
      table overflow
  - queue correction:
    - the next honest target is helper-form interaction with the inherited
      numeric-for index/current-value `SLOAD` seam
    - not generic `TGETS`
    - not more inherited-int extraction work

- Timestamp: `2026-04-02 14:31:00 PDT`
- Dynamic helper localization is evidence-only, not a promotable fix
  - reduced `kdz` helper variants still show the important split:
    - original helper literal-stop form keeps exits
    - local/arg reduced forms reach `TEXIT_COUNT 0`
  - but clean `kdz` dynamic local-helper form fails the first real bar:
    - artifact:
      [20260402-kdz-dynamic-helper-localization-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-dynamic-helper-localization-check/raw/number_helper_loop_local_tobit.stderr.log)
    - `RESULT -149783296`
    - `TRACE_START 321`
    - `TRACE_STOP 321`
    - `TRACE_ABORT 0`
    - `TEXIT_COUNT 64001`
    - then `table overflow`
  - queue correction:
    - helper localization is a useful reduced probe
    - it is not a promotable remediation family on the real workload
    - the next honest target remains the dynamic helper-form interaction that
      keeps the inherited numeric-for index/current-value `SLOAD` seam live

- Timestamp: `2026-04-02 15:01:41 PDT`
- Dynamic helper-form interaction is now pinned as stack-visible `BC_MOV`
  replay, not imported-helper `BC_UGET` replay
  - stripped reduced real-workload localization runs on clean `kdz` now agree
    for both dynamic helper forms:
    - local helper:
      [20260402-kdz-dynamic-local-iter400](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-dynamic-local-iter400/summary.md)
    - arg helper:
      [20260402-kdz-dynamic-arg-iter400](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-dynamic-arg-iter400/summary.md)
  - both localized reduced runs are finite and correct:
    - `RESULT 961100104`
    - repeated steady seam at restored `pc op=18`, `snapop=18`
    - repeated exact-taken `guardmark=0x3`
  - recorder setup plus reduced `TRACEIR` now pins the moved inherited lane
    semantically on both localized forms:
    - `baseslot=2`
    - `op1=3` -> carried `total`
    - `op1=4` -> localized `tobit` value
    - `op1=5` -> current numeric-for value feeding `* 65537`
    - `op1=6` -> loop bound `n`
  - both now pin the same exact moved inherited guard:
    - `curins=3`
    - `IR=SLOAD`
    - `op1=5`
    - `op2=36`
    - `kind=sload_int`
    - `ofs=24`
    - `extra=28`
  - source-backed opcode meaning now closes the semantic split:
    - [lj_bc.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_bc.h)
      defines `BC_MOV` as `dst <- var`
    - [lj_parse.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_parse.c#L544)
      emits `BC_MOV` when a non-reloc value must be copied to a different slot
    - [lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c#L3527)
      treats `BC_MOV` as stack-slot movement, not new arithmetic
  - queue correction:
    - the helper-localization evidence is still useful because it proves the
      old imported-helper `BC_UGET` seam is not fundamental
    - but the real replay family survives one step later as stack-visible
      helper/value `BC_MOV` replay on the actual workload
    - the moved seam is no longer just an opcode marker; both localized forms
      converge on the same inherited current numeric-for-value `SLOAD` lane
      behind that `MOV`
    - the next honest target is therefore exact stack-visible helper/value
      replay under the promoted slice at that shifted current-value `SLOAD`

- Timestamp: `2026-04-02 15:03:17 PDT`
- Reduced slot-state follow-up closes the localized rematerialization split
  - artifact:
    [20260402-kdz-dynamic-local-slotlog](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-dynamic-local-slotlog/summary.md)
  - repeated seam stays:
    - `trace 1 exit 0`
    - restored `pc op=18`, `snapop=18`
    - exact taken `guardmark=0x3`
    - `curins=3`, `IR=SLOAD`, `op1=5`, `op2=36`
  - but the replayed loop state at that seam is already coherent:
    - live current value register `r11` advances `0x3`, `0x4`, `0x5`, ...
    - carried `total` dump `r3tv q0` stays a valid boxed GC64 int and advances
      consistently:
      - `0xfff9000000060006`
      - `0xfff90000000a000a`
      - `0xfff90000000f000f`
      - `0xfff9000000150015`
  - queue correction:
    - this is no longer a missing-rematerialization theory
    - the localized helper/value replay state is already live and progressing
    - the next honest target is the inherited integer `SLOAD`
      replay/typecheck contract on that live current-value lane
    - not imported-helper lookup
    - not another helper-localization attempt

- Timestamp: `2026-04-02 15:03:17 PDT`
- Current-`HEAD` slot logger proves the localized seam is firing on the correct
  live current-value slot
  - artifact:
    [20260402-kdz-dynamic-local-slotlog-v2](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-dynamic-local-slotlog-v2/summary.md)
  - `S390X_SLOADMAP` for the repeated seam:
    - `curins=3`
    - `op1=5`
    - `ofs=24`
    - `base=12`
  - the runtime exit dump for the same seam shows `r12 == L->base`
  - the slot logger then proves `op1=5` is reading the right live slot:
    - `baseslot=2`, so `op1=5 -> idx=3`
    - `S390X_SLOT idx=3` is a valid boxed int and advances:
      - `0xfff9000000000003`
      - `0xfff9000000000004`
      - `0xfff9000000000005`
      - ...
  - queue correction:
    - the live localized seam is no longer a stale-slot or missing-store-back
      theory
    - the inherited integer `SLOAD` replay/typecheck is firing on the correct
      live current-value slot
    - the next honest target is the exact compiled typecheck/lowering on that
      lane

- Timestamp: `2026-04-02 15:34:18 PDT`
- The direct signed-expected GC64 repair is now closed as a rejected classifier
  and the branch source remains on baseline
  - reduced classifiers carried:
    - [20260402-kdz-dynamic-local-after-signed-expected-fix](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-dynamic-local-after-signed-expected-fix/summary.md)
    - [20260402-kdz-number-helper-after-signed-expected-fix](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-number-helper-after-signed-expected-fix/summary.md)
  - real helper truth-pack failed:
    - [20260402-kdz-be_helpers-hotside_canon_share_uget_looproot_default-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260402-kdz-be_helpers-hotside_canon_share_uget_looproot_default-truth-pack/raw/jit-on.stderr.log)
    - `number_helper_loop/hot: expected 1323881804, got 34304`
  - current narrowed read:
    - first long hot run is correct
    - second long hot run in the same process is wrong
    - the break only appears at larger reruns
    - direct second-run counters on `kdz` are tiny:
      - `TRACE_START 3`
      - `TRACE_STOP 2`
      - `TRACE_ABORT 1`
      - `TEXIT_COUNT 2`
    - `-jv` two-run proof moves the live seam to:
      - `TRACE 2 (1/0)` overflow side loop
      - then `TRACE 3` fallback/interpreter
      - side-loop body is `num CONV -> num MUL -> int TOBIT -> int ADD`
  - queue correction:
    - the inherited integer `SLOAD` mismatch was real
    - the signed-expected compare fix is directionally right but unsafe
    - the next honest target is the warm-built overflow-side-loop continuation
      seam on the real helper workload, not the old inherited `SLOAD` lane

- Timestamp: `2026-04-02 15:43:56 PDT`
- The warmed post-repair seam is now pinned one step later than the raw
  overflow loop itself
  - artifact:
    [20260402-kdz-signedfix-two-run-noprint-mid](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-signedfix-two-run-noprint-mid/summary.md)
  - two-run no-print-mid result:
    - `WARM 132610`
    - `SECOND 25535`
  - exact trace chain:
    - `TRACE 1`: main int loop
    - `TRACE 2 (1/2)`: overflow side path, `stop -> 1`
    - `TRACE 3 (1/0)`: warmed overflow loop
    - `TRACE 4 (3/3)`: return-side continuation at
      `return bit.tobit(total)`, `stop -> 1`
    - `TRACE 5 (4/0)`: later stitch into `print`
    - exact narrowed return seam:
      - `trace 4 exit 0`
      - `guardmark=0xd`
      - in `TRACE 4` IR that is `curins 13`
      - `0013 > p64 RETF ...`
  - queue correction:
    - the bad second-run result is already born before the console stitch
    - the next honest target is the helper lower-frame return (`RETF`)
      continuation after the warmed overflow loop
    - not the later print path

- Timestamp: `2026-04-02 17:19:00 PDT`
- The post-repair return seam is now split cleanly into one side seam and one
  live caller seam
  - direct return-contract probe on `kdz`:
    - `/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-retf-runtime-contract/raw`
    - `TRACE 4` really does specialize `RETF` to one lower-frame caller PC and
      then re-enter from another:
      - recorder `frame_pc=0x...6b70`
      - taken runtime exit carries `r2=0x...6b70`, `r11=0x...6b7c`
    - local bytecode listing closes the meaning of that `0xc` gap:
      - top-level `warm = run(64000)` call site
      - top-level `second = run(40000)` call site
  - stable-callsite control on `kdz`:
    - `/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-retf-single-callsite-loop/raw`
    - `drive(n, reps)` calls `run(n)` twice from the same caller `FORL` site
    - result is still wrong: `RESULT 25535`
    - but the seam moves past direct `RETF`:
      - `TRACE 4` contains `RETF`
      - then caller-loop state:
        - `SLOAD #6 RI`
        - `SLOAD #5 TI`
        - `ADD`
        - `LE`
      - exact taken exit is `trace 4 exit 2`, restored `pc op=76`,
        `guardmark=0x11`
  - queue correction:
    - polymorphic lower-frame return PCs create a real `RETF` side seam
    - but the stable-callsite control proves the live post-repair bug moves
      into the caller numeric-for header after return
    - the next honest target is the caller `FORI/FORL` state contract after a
      successful lower-frame return, not generic `RETF`

- Timestamp: `2026-04-02 17:42:00 PDT`
- Stable-callsite slot-state probe proves the caller loop state is coherent and
  the wrong value is already in the caller-visible result slot
  - artifact:
    `/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-caller-forl-seam/raw`
  - exact taken exit on the stable-callsite control:
    - `trace 4 exit 2`
    - restored `pc op=76`
    - `guardmark=0x11`
  - local bytecode listing identifies `op=76` as the caller `FORL` in
    `drive(n, reps)`
  - slot dump at that exact exit:
    - caller `out` slot is already wrong:
      - `idx=2 -> 25535`
    - caller loop state is otherwise coherent:
      - `idx=3 -> 2`
      - `idx=4 -> 2`
      - `idx=5 -> 1`
      - `idx=6 -> 2`
  - queue correction:
    - the stable-callsite failure is not bad caller `FORL` state
    - the wrong value is already in the caller-visible result slot when the
      normal loop exit happens
    - the next honest target is return-value handoff from the warmed overflow
      path into the lower frame, not generic caller loop-state replay

- Timestamp: `2026-04-02 16:23:53 PDT`
- The current opt-in GC64 repair pair closes the stale lower-frame return
  theory and is correctness-positive on both hosts
  - active opt-in pair:
    - `LUAJIT_S390X_GC64_SIGNED_INT_SLOAD=1`
    - `LUAJIT_S390X_JFORI_INTERP_HANDOFF=1`
  - `kdz` stable-callsite control:
    - [20260402-kdz-recret-slotlog-signedfix-v1](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-recret-slotlog-signedfix-v1/raw/stdout.log)
    - `RESULT -2050009568`
    - no `lua_lower_frame_retf`; recorder only reports `lua_intrace_return`
  - real helper workload:
    - [20260402-kdz-number-helper-optinpair-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-number-helper-optinpair-check/raw/stdout.log)
    - [20260402-zkd0-number-helper-optinpair-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-zkd0-number-helper-optinpair-check/raw/stdout.log)
    - both hosts return:
      - `WARM -149783296`
      - `SECOND -149783296`
  - authoritative `kdz` helper pack with the pair layered onto the promoted
    default:
    - [20260402-kdz-be_helpers-hotside_canon_share_uget_looproot_default-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260402-kdz-be_helpers-hotside_canon_share_uget_looproot_default-truth-pack/summary.md)
    - `number_helper_loop/hot 0.008248` vs `-joff 0.002282`
    - `be_pack_loop/hot 0.023373` vs `-joff 0.018732`
    - focused runtime remains:
      - `TRACE_START 6`
      - `TRACE_STOP 5`
      - `TRACE_ABORT 1`
      - `TEXIT_COUNT 64001`
  - focused mechanism under the pair:
    - [20260402-kdz-number-helper-optinpair-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-number-helper-optinpair-mechanism/summary.md)
    - dominant seam is still `trace 7 exit 0` at restored `BC_UGET`
    - first surviving `sload_int` stays `curins 15`, `IR=SLOAD`, `op1=3`,
      `ofs=8`, `extra=12`
  - queue correction:
    - the old lower-frame return-value failure is stale for the current pair
    - the pair is correctness-positive but effectively perf-inert on the
      current helper slice
    - the live perf seam remains the same steady header-guard family under the
      promoted default
    - the next honest work is promotion/validation strategy for the pair, not
      more lower-frame return archaeology

- Timestamp: `2026-04-02 18:12:00 PDT`
- Source-backed semantic mapping now closes the “first surviving `SLOAD`”
  ambiguity on the live helper seam
  - focused pair mechanism:
    - [20260402-kdz-number-helper-optinpair-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-number-helper-optinpair-mechanism/summary.md)
    - dominant seam still:
      - `trace 7 exit 0`
      - restored `BC_UGET`
      - first surviving `sload_int`:
        - `curins 15`
        - `IR=SLOAD`
        - `op1=3`
        - `ofs=8`
        - `extra=12`
  - source meaning of that lane:
    - it is the loop-carried `total`
    - it is emitted by the ordinary `getslot() -> sload()` stack
      specialization path in
      [lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c)
    - it stays present on side traces because snapshot replay recreates live
      stack-visible lanes as inherited/parent `IR_SLOAD` in
      [lj_snap.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_snap.c)
  - exact-taken distinction:
    - that first surviving `sload_int` is not the same as the first literal
      taken guard on the live helper path
    - the exact-taken guard remains the later inherited current numeric-for
      value lane:
      - `curins 3`
      - `IR=SLOAD`
      - `op1=4`
      - `op2=36`
      - `ofs=16`
      - `extra=20`
    - artifact:
      [20260402-kdz-number-helper-postrepair-guardmark](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-number-helper-postrepair-guardmark/summary.md)
    - source-backed creation path:
      - this is not a generic stack reload
      - it is the visible numeric-for current-value lane created by
        [rec_for_loop()](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c)
        through:
        - `idx = fori_load(J, ra+FORL_IDX, t, IRSLOAD_INHERIT + tc + ...)`
      - `op2=36` matches exactly:
        - `IRSLOAD_INHERIT`
        - `IRSLOAD_TYPECHECK`
      - the recorder then reuses that lane as the visible current value:
        - `J->base[ra+FORL_IDX] = idx = emitir(IR_ADD, idx, step)`
        - `J->base[ra+FORL_EXT] = idx`
  - backend comparison:
    - the GC64 signed-vs-logical integer-tag extraction difference is
      s390x-specific
    - x86/x64 and arm64 do not carry a matching signed-int-sload repair path
    - queue correction:
      - the GC64 pair fixes one real s390x correctness issue
      - the remaining performance seam is broader snapshot/header replay on
        the inherited visible `FORL_IDX` current-value lane, not a second copy
        of the same extraction bug
      - that lane stays guarded by design on this workload:
        - [rec_for_loop()](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c)
          only drops `IRSLOAD_TYPECHECK` when the narrowed numeric-for state
          is fully compile-time clean
        - `number_helper_loop(n)` keeps hidden `STOP` as runtime `n`, so
          `irref_isk(stop)` is false
        - that leaves `tc = IRSLOAD_TYPECHECK`, and the recorder emits
          `fori_load(... IRSLOAD_INHERIT | IRSLOAD_TYPECHECK ...)`
        - exactly the observed `op2=36`
      - next honest target:
        - determine whether that visible-current-value typecheck can ever be
          safely relaxed after JFORI/FORL integer state is already proven, or
          whether it is the required generic contract for dynamic-stop loops

- Timestamp: `2026-04-02 18:24:00 PDT`
- Direct visible-current-value typecheck relaxation is closed as a structural
  reject
  - rejected gate:
    - `LUAJIT_S390X_FORL_VISIBLE_IDX_NO_TC`
  - intended scope:
    - `FORL` replay only
    - visible current-value lane only
    - integer loop only
  - clean `kdz` screen:
    - remote build completed
    - first reduced setup for `pure_add_reducer` completed
    - no runtime workload log was ever emitted after setup
    - artifact directory stopped at:
      - `build.*`
      - `pure_add_reducer.setup.*`
    - artifact:
      [20260402-kdz-forl-visible-idx-no-tc-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-forl-visible-idx-no-tc-check)
  - queue correction:
    - direct removal of the visible `FORL_IDX` replay/typecheck is not safe
    - the next honest target is the invariant that this guard still protects
      on dynamic-stop loops, not another blind no-typecheck variant

- Timestamp: `2026-04-02 17:33:08 PDT`
- `FORL` fastpath miss/rematerialization is closed as the next helper seam
  - debug-only logger:
    - `LUAJIT_S390X_FORL_FASTPATH_LOG`
  - artifact:
    [20260402-kdz-forl-fastpath-log](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-forl-fastpath-log)
  - decisive read on clean `kdz` helper path:
    - repeated `S390X_FORL_FASTPATH` hits already show:
      - `pc_match=1`
      - `idx_match=1`
    - so the loop is already re-entering through the exact same `fori`
      and the exact same `FORL_IDX` ref identity
  - follow-on exact-taken probe:
    - artifact:
      [20260402-kdz-post-fastpath-guardmark](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-post-fastpath-guardmark/summary.md)
    - still lands at:
      - `TRACE_START 4`
      - `TEXIT_COUNT 401`
      - dominant texit `4:0 x 200`
      - restored `BC_UGET`
      - exact taken `guardmark=0x3`
      - exact inherited current-value lane:
        - `curins 3`
        - `kind=sload_int`
        - `ofs=16`
        - `extra=20`
  - queue correction:
    - the active red is not a `FORL` fastpath miss
    - the next honest target stays the visible current numeric-for value
      typecheck/replay contract itself

- Timestamp: `2026-04-02 18:06:00 PDT`
- Isolated visible-current-value relaxation is closed as a structural reject
  too
  - rejected gate:
    - `LUAJIT_S390X_FORL_VISIBLE_IDX_ONLY_NO_TC`
  - corrected scope:
    - preserve hidden `STOP/STEP` anchoring in `J->base`
    - drop `IRSLOAD_TYPECHECK` only on the visible `FORL_IDX` lane during
      `FORL` replay
  - clean `kdz` artifact:
    [20260402-kdz-forl-visible-idx-only-no-tc-v2](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-forl-visible-idx-only-no-tc-v2)
  - results:
    - `number_helper_loop` still ran:
      - `RESULT 961100104`
      - `TRACE_START 4`
      - `TEXIT_COUNT 401`
    - but no-helper `pure_add_reducer` still failed structurally:
      - `TRACE_START 403`
      - `TRACE_STOP 402`
      - `TEXIT_COUNT 400`
      - final failure:
        - `./src/luajit: /tmp/pure_add_reducer.lua:8: table overflow`
      - late steady seam had already shifted to `BC_LEN` (`op=21`) on a large
        clone ladder (`trace 463`)
  - queue correction:
    - helper interaction is not what makes the current-value typecheck
      required
    - dropping only the visible `FORL_IDX` typecheck is still not safe
    - the next honest target is the generic invariant enforced by that
      dynamic-stop numeric-for current-value lane

- Timestamp: `2026-04-02 18:40:00 PDT`
- Generic `FORL_IDX` contract is now pinned as the next remediation boundary
  - source and backend read:
    - the surviving visible-current-value lane is recorder/runtime-generic,
      not a second s390-only backend bug
    - the narrowed integer `FORL` path requires visible `FORL_IDX` to arrive as
      an int-tagged TValue at replay entry
  - `FORL` fastpath is already succeeding on the live helper seam:
    - `pc_match=1`
    - `idx_match=1`
  - slot-log evidence is noisy but still directionally useful:
    - under the instrumented reduced baseline, the visible current-value slot
      is already boxed-int and increments normally at repeated exits before the
      logger perturbs the reducer
    - artifact:
      [20260402-kdz-visible-idx-slotlog-baseline](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-visible-idx-slotlog-baseline)
  - queue correction:
    - next honest remediation is a once-per-replay visible-current-value latch
      only if replay-entry mismatch can be shown to disappear after the first
      traced update
    - otherwise this visible-current-value line stays closed
  - design note:
    [forl-visible-current-latch.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/forl-visible-current-latch.md)

- Timestamp: `2026-04-02 18:28:00 PDT`
- Visible-current-value latch remediation is now closed on the current
  mechanism
  - generic contract read:
    - the surviving visible `FORL_IDX` current-value lane is recorder/runtime
      policy, not backend-specific lowering
    - snapshot replay preserves slot identity; numeric-for slot meaning does
      not change after replay entry
    - VM numeric-for paths keep `FOR_IDX` as hidden control state and
      `FOR_EXT` as the visible mirror on every iteration
  - new reduced `kdz` cross-checks under the promoted default:
    - [20260402-kdz-direct-abs-current-seam](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-direct-abs-current-seam)
      - `TRACE_START 4`
      - dominant texit `3:0 x 200`
      - exact runtime guard:
        - `curins 3`
        - `sload_int ofs 16 extra 20`
      - reduced dump front lane:
        - `0003 >  int SLOAD  #4    TI`
    - [20260402-kdz-be-pack-current-seam](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-be-pack-current-seam)
      - `TRACE_START 3`
      - dominant texit `4:0 x 200`
      - exact runtime guard:
        - `curins 3`
        - `sload_int ofs 16 extra 20`
      - reduced dump front lane:
        - `0003 >  int SLOAD  #4    TI`
  - queue correction:
    - this is no longer a helper-only or entry-only mismatch theory
    - the same visible-current replay seam is front-most across
      `number_helper_loop`, `be_pack_loop`, and `direct_abs`
    - once-per-replay latch remediation is not an honest next code family on
      the current mechanism
    - the next honest target is whether the remaining promoted-default gap
      should now be treated as generic dynamic-stop numeric-for replay cost

- Timestamp: `2026-04-02 18:31:02 PDT`
- Host-pair seam check matches the same generic floor
  - clean `zkd0` reduced cross-checks under the promoted default:
    - [20260402-zkd0-direct-abs-current-seam](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-zkd0-direct-abs-current-seam)
      - `TRACE_START 4`
      - dominant texit `3:0 x 200`
      - exact runtime guard:
        - `curins 3`
        - `sload_int ofs 16 extra 20`
    - [20260402-zkd0-be-pack-current-seam](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-zkd0-be-pack-current-seam)
      - `TRACE_START 3`
      - dominant texit `4:0 x 200`
      - exact runtime guard:
        - `curins 3`
        - `sload_int ofs 16 extra 20`
  - closure:
    - the same post-promotion replay seam is front-most on both `kdz` and
      `zkd0`
    - there is no remaining host-pair evidence for a z15-only or z14-only
      latch-style remediation on this mechanism
    - the remaining promoted-default gap should now be treated as the current
      shared dynamic-stop numeric-for replay floor unless a different
      mechanism is proven

- Timestamp: `2026-04-02 18:43:05 PDT`
- Route-around split is now pinned on reduced siblings
  - new checked-in reduced probe variants live in:
    - [build_core_exit_mechanism_probe.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/build_core_exit_mechanism_probe.py)
  - clean `kdz` static-stop reads:
    - [20260402-kdz-direct-abs-literal-stop-seam](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-direct-abs-literal-stop-seam)
      - `TRACE_START 4`
      - `TRACE_STOP 3`
      - `TEXIT_COUNT 1`
      - old dynamic-stop replay floor is effectively gone
    - [20260402-kdz-be-pack-literal-stop-seam-nodump](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-be-pack-literal-stop-seam-nodump)
      - `TRACE_START 1`
      - `TRACE_STOP 0`
      - `TEXIT_COUNT 399`
      - dominant texit still hot:
        - `trace 7 exit 0`
        - `op 45`
        - `curins 33`
        - `sload_int ofs 0 extra 4`
  - helper-localized static-stop `be_pack`:
    - [20260402-kdz-be-pack-literal-stop-local-ops](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-be-pack-literal-stop-local-ops)
      - `TRACE_START 1`
      - `TRACE_STOP 1`
      - `TEXIT_COUNT 0`
  - helper-localized dynamic-stop `be_pack`:
    - [20260402-kdz-be-pack-loop-local-ops](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-be-pack-loop-local-ops)
      - `TRACE_START 4`
      - `TRACE_STOP 3`
      - `TEXIT_COUNT 401`
      - dominant texit remains hot, but moves later:
        - `trace 4 exit 0`
        - `op 18`
        - `curins 3`
        - `sload_int ofs 48 extra 52`
  - queue correction:
    - static-stop alone is not the route-around for the whole slice
    - imported helper/header traffic is a real residual floor for `be_pack`
      under static-stop
    - but on the real dynamic-stop shape, localizing helpers only shifts the
      replay seam later; it does not remove the generic exit-dominated floor

- Timestamp: `2026-04-02 19:10:44 PDT`
- Route-around quantification closes the reduced subgroup as a performance
  remediation lane on clean `kdz`
  - new helper-backed family:
    [20260402-kdz-route_around_reducers-baseline-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260402-kdz-route_around_reducers-baseline-truth-pack/summary.md)
    vs
    [20260402-kdz-route_around_reducers-hotside_canon_share_uget_looproot_default-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260402-kdz-route_around_reducers-hotside_canon_share_uget_looproot_default-truth-pack/summary.md)
  - invalid reduced FFI literal-stop legs are now explicitly out:
    - `stored_abs_literal_stop` returns `RESULT 0` on remote `kdz` under plain
      JIT and under the promoted default, so it is not a valid route-around
      workload on this host
  - valid `be_pack` subgroup read:
    - baseline:
      - `be_pack_literal_stop/hot 0.241946` vs `-joff 0.047685`
      - `be_pack_literal_stop_local_ops/hot 0.126152` vs `0.019453`
      - `be_pack_loop_local_ops/hot 0.126122` vs `0.019463`
    - promoted default:
      - `be_pack_literal_stop/hot 0.073034` vs `0.046844`
      - `be_pack_literal_stop_local_ops/hot 0.126189` vs `0.019927`
      - `be_pack_loop_local_ops/hot 0.126296` vs `0.019513`
  - queue correction:
    - the promoted default materially helps the static-stop `be_pack` reducer
      (`0.241946 -> 0.073034`) but still does not cross into `jit.on < -joff`
    - the small-scale structural escape on `be_pack_literal_stop_local_ops`
      does not survive throughput-scale work
    - corrected reduced boundary after fixing the probe helper:
      - one chunk:
        [20260402-kdz-be-pack-local-ops-chunks1-v3](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-be-pack-local-ops-chunks1-v3/summary.md)
        - `TRACE_START 2`
        - `TRACE_STOP 2`
        - `TEXIT_COUNT 1`
      - throughput-sized `run(400)`:
        [raw stdout](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-be-pack-local-ops-chunks400-v3/raw/be_pack_literal_stop_local_ops.stdout.log)
        - `RESULT 32080000`
        - `TRACE_START 367`
        - `TRACE_STOP 367`
        - `TEXIT_COUNT 365`
    - the earlier fake flat sweep was a probe artifact:
      - the old reduced helper ignored `--iterations` for literal-stop
        reducers and reused one shared remote `/tmp/<workload>.lua` name across
        concurrent runs
      - the checked-in probe now honors iteration override for the
        literal-stop family and writes unique remote script names
    - reduced route-around siblings are now evidence only, not the next
      promotable performance lane

- Timestamp: `2026-04-02 20:03:20 PDT`
- Real-shape helper localization is now closed as a promotion-core performance
  family on clean `kdz`
  - new checked-in family:
    [20260402-kdz-be_helpers_localized-baseline-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260402-kdz-be_helpers_localized-baseline-truth-pack/summary.md)
    vs
    [20260402-kdz-be_helpers_localized-hotside_canon_share_uget_looproot_default-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260402-kdz-be_helpers_localized-hotside_canon_share_uget_looproot_default-truth-pack/summary.md)
  - real dynamic-stop localized forms:
    - `number_helper_loop_local_tobit/hot`
      - baseline `0.436932` vs `-joff 0.001442`
      - promoted default `0.621525` vs `0.001435`
    - `be_pack_loop_local_ops_real/hot`
      - baseline `0.300697` vs `-joff 0.007985`
      - promoted default `0.244329` vs `0.008069`
  - focused runtime read:
    - baseline:
      - both workloads `TRACE_START 321`, `TRACE_STOP 321`,
        `TEXIT_COUNT 64001`
    - promoted default:
      - both workloads `TRACE_START 5`, `TRACE_STOP 5`,
        `TEXIT_COUNT 64001`
  - queue correction:
    - real helper localization is not a hidden win on the active slice
    - it is drastically worse than the existing shipping `be_helpers` path
    - it preserves the same dynamic-stop replay floor
    - the next honest target stays on that shared floor, not helper-hoist

- Timestamp: `2026-04-02 20:10:45 PDT`
- Real-shape static-stop quant shows the remaining promotion-core gap is not
  just “dynamic stop” in the simple sense
  - new checked-in family:
    [20260402-kdz-promotion_core_static_stop-baseline-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260402-kdz-promotion_core_static_stop-baseline-truth-pack/summary.md)
    vs
    [20260402-kdz-promotion_core_static_stop-hotside_canon_share_uget_looproot_default-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260402-kdz-promotion_core_static_stop-hotside_canon_share_uget_looproot_default-truth-pack/summary.md)
  - real-shape literal-stop hot read:
    - `number_helper_literal_stop_real`
      - baseline `0.040660` vs `-joff 0.002244`
      - promoted default `0.009910` vs `0.002230`
    - `be_pack_literal_stop_real`
      - baseline `0.097101` vs `-joff 0.019263`
      - promoted default `0.025347` vs `0.018812`
  - focused runtime split:
    - baseline:
      - `compiled-body-dominated`
      - `TEXIT_COUNT 0`
    - promoted default:
      - still `exit-dominated`
      - `TEXIT_COUNT 63999`
  - queue correction:
    - removing dynamic stop does not by itself clear the promoted-default
      steady floor on the real `be_helpers` shapes
    - the promoted default remains a net win even there because its new
      exit-heavy path is still much faster than the compiled-body baseline
    - the next honest target is the exact static-stop seam under the promoted
      default, not helper-hoist and not another dynamic-stop-only theory

- Timestamp: `2026-04-02 20:15:29 PDT`
- Static-stop seam probe narrows the remaining promoted-default floor again
  - new focused artifact:
    [20260402-201327-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-201327-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism/summary.md)
  - `be_pack_literal_stop_real` now pins cleanly:
    - `TRACE_START 2`, `TRACE_STOP 1`, `TEXIT_COUNT 63999`
    - dominant texit:
      - `trace 7 exit 0`
      - restored `op 45`, `snapop 45` = `BC_UGET`
      - first `sload_int`: `curins 33`, `IR=SLOAD`, `op1 2`, `op2 4`
  - `number_helper_literal_stop_real` no longer matches that same front seam:
    - raw exit logs in the same artifact show repeated restored `op 57` /
      `snapop 57` = `BC_TGETS`
    - the exact taken inner guard inside that `TGETS` cluster is not yet
      isolated
  - queue correction:
    - the remaining static-stop cap is no longer one uniform dynamic-stop
      numeric-for replay seam
    - `be_pack` is now front-most at restored `BC_UGET`
    - `number_helper` has already advanced one step later to restored
      `BC_TGETS`

- Timestamp: `2026-04-02 20:39:30 PDT`
- Static-stop helper localization closes the replay lane but not the perf gap
  - direct remote A/B:
    [20260402-kdz-static-stop-local-tobit-direct](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-static-stop-local-tobit-direct/raw/candidate.stdout.log)
  - `number_helper_literal_stop_real_local_tobit/hot`
    - baseline `0.019114s` vs `-joff 0.001359s`
    - promoted default `0.018959s` vs `-joff 0.001359s`
  - focused trace-count check:
    [20260402-kdz-static-stop-local-tobit-trace](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-static-stop-local-tobit-trace/raw/candidate.stdout.log)
    - candidate: `TRACE_START 1`, `TRACE_STOP 1`, `TRACE_ABORT 0`,
      `TEXIT_COUNT 0`
    - baseline: `TRACE_START 1`, `TRACE_STOP 1`, `TRACE_ABORT 0`,
      `TEXIT_COUNT 0`
  - repeated-call check:
    [20260402-kdz-static-stop-local-tobit-postcompile](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-static-stop-local-tobit-postcompile/raw/candidate.stdout.log)
    - `RUN1`: `0.018934s`, `TRACE_START 1`, `TRACE_STOP 1`,
      `TRACE_ABORT 0`, `TEXIT_COUNT 0`
    - `RUN2`: `0.018927s`, `TRACE_START 1`, `TRACE_STOP 0`,
      `TRACE_ABORT 1`, `TEXIT_COUNT 1`
  - `-jv` proof:
    [20260402-kdz-static-stop-local-tobit-postcompile-jv](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-static-stop-local-tobit-postcompile-jv/raw/jv.stderr.log)
    - repeated calls still build a clone ladder:
      `TRACE 1`, `TRACE 2 (1/0)`, ..., `TRACE 102 (101/0)`, then fallback
  - native dump:
    [20260402-kdz-static-stop-local-tobit-dump](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-static-stop-local-tobit-dump/raw/dump.stdout.log)
    - loop body is now just localized `bit.tobit` plus arithmetic:
      `SLOAD #4 I`, `fun SLOAD #3 T`, `MULOV`, `SLOAD #2 T`, `fun EQ bit.tobit`, `ADD`, `LE`
  - queue correction:
    - helper localization plus static stop does remove the replay floor for
      `number_helper`
    - it still does not produce a stable fast JIT lane across repeated calls
    - the next honest target for that subgroup is the cross-call clone/fallback
      behavior, not more replay/header digging inside one run

- Timestamp: `2026-04-02 21:22:00 PDT`
- Explicit broad canon/share now opens a bounded remediation lane for localized
  static-stop `number_helper`
  - artifact:
    [20260402-kdz-static-stop-local-broad-canonshare](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-static-stop-local-broad-canonshare/summary.md)
  - source boundary:
    [lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c)
  - correction:
    - explicit broad canon/share envs were accidentally hard-gated by the
      shipping `UGET`/looproot prefilter in `trace_hotside()`
    - the fix keeps the envless default unchanged, but lets manual broad envs
      reach the equivalence matcher on non-`UGET` seams
  - mechanism proof on clean `kdz`:
    - the localized static-stop lane now reaches `find_equiv_min()`
    - repeated accepts appear on the same root chain:
      - `parent=6 exit=0 root=1 cand=5`
      - `parent=7 exit=0 root=1 cand=6`
    - earlier candidates are rejected only by ordering, not by shape drift
  - repeated-call quant:
    - `number_helper_literal_stop_real_local_tobit`
      - default: `0.027580s`, `0.030517s`, `103` traces
      - broad opt-in: `0.007981s`, `0.007844s`, `7` traces
    - localized static-stop `be_pack` sibling:
      - default: `0.016238s`, `0.016659s`, `7` traces
      - broad opt-in: `0.015817s`, `0.015774s`, `7` traces
  - queue correction:
    - this is a real opt-in remediation lane for the localized static-stop
      `number_helper` subgroup
    - it is not yet a shipping-default widening and not a subgroup-wide win
    - the next honest target is whether this opt-in lane can actually cross
      into `jit.on < -joff`, then whether a selective non-`UGET`
      canon/share policy can be justified

- Timestamp: `2026-04-02 21:31:00 PDT`
- Host-pair truth packs close the broad non-`UGET` localized lane as
  correctness-positive but still slower than `-joff`
  - authoritative packs:
    [20260402-kdz-promotion_core_static_stop-hotside_canon_share_uget_looproot_default-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260402-kdz-promotion_core_static_stop-hotside_canon_share_uget_looproot_default-truth-pack/summary.md)
    [20260402-zkd0-promotion_core_static_stop-hotside_canon_share_uget_looproot_default-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260402-zkd0-promotion_core_static_stop-hotside_canon_share_uget_looproot_default-truth-pack/summary.md)
  - candidate env:
    - `LUAJIT_S390X_HOTSIDE_CANON_SHARE_EQUIV=1`
    - layered on top of the shipping filtered default
  - `number_helper_literal_stop_real_local_tobit/hot`
    - `kdz`: `0.006327s` vs `-joff 0.001361s` (`4.65x`)
    - `zkd0`: `0.013725s` vs `-joff 0.002224s` (`6.17x`)
  - focused runtime read:
    - `kdz`: `TRACE_START 1`, `TRACE_STOP 1`, `TRACE_ABORT 0`,
      `TEXIT_COUNT 63999`
    - `zkd0`: `TRACE_START 1`, `TRACE_STOP 1`, `TRACE_ABORT 0`,
      `TEXIT_COUNT 63999`
    - both stay `exit-dominated`
  - sibling contrast:
    - `be_pack_literal_stop_real` barely moves under the same env and stays
      around `1.3x` slower than `-joff`
  - queue correction:
    - the broad non-`UGET` canon/share lane is real but still not enough to
      make this subgroup fast
    - the next honest target is selective non-`UGET` canon/share policy
      design or a fresh exit seam inside this lane, not widening the default
    - bounded follow-on note:
      [non-uget-canonshare-policy.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/non-uget-canonshare-policy.md)

- Timestamp: `2026-04-02 21:43:00 PDT`
- Reduced localized broad opt-in seam is now pinned after the ladder collapse
  - direct reduced `kdz` probe with:
    - `LUAJIT_S390X_HOTSIDE_CANON_SHARE_EQUIV=1`
    - `LUAJIT_S390X_GUARDMARK_TAKEN=1`
    - `LUAJIT_S390X_GUARD_LOG=1`
  - on the localized no-helper sibling (`number_helper_local_tobit`, `n=400`):
    - restored seam stays `BC_MOV` (`op=18`)
    - exact taken guard is:
      - `kind=sload_int`
      - `curins=4`
      - `ofs=0`
      - `extra=4`
    - later same-cluster guards are:
      - `curins=3`
      - `sload_type curins=2 ofs=8 extra=8`
  - queue correction:
    - broad non-`UGET` canon/share collapses the clone ladder
    - it does not remove the first stack-visible `SLOAD` guard cluster on the
      localized `MOV` seam
    - the next honest target is the exact `MOV`/`sload_int ofs=0 extra=4`
      post-collapse lane, not the old clone ladder itself

- Timestamp: `2026-04-02 22:38:00 PDT`
- Post-collapse localized seam is now pinned as a good-slot carried-`total`
  `SLOAD`, not a bad stack lane
  - direct reduced `kdz` trace-IR on `number_helper_local_tobit` under
    `LUAJIT_S390X_HOTSIDE_CANON_SHARE_EQUIV=1`:
    - `TRACEIR tr=1 ins=4 op=SLOAD op1=2 op2=4`
    - `TRACEIR tr=1 ins=2 op=SLOAD op1=3 op2=4`
    - `TRACEIR tr=1 ins=3 op=MULOV op1=1 op2=-6`
  - so the first marked guard on that lane is the carried `total` reload
  - direct `SLOADMAP` + slot logging on the same seam shows:
    - `S390X_SLOADMAP curins=4 ref=4 kind=int op1=2 op2=0x4 ofs=0 vofs=4 base=11`
    - restored stack base is correct
    - `S390X_SLOT idx=0` is a valid boxed int advancing:
      - `0xfff9000000030003`
      - `0xfff9000000060006`
      - `0xfff90000000a000a`
      - ...
  - queue correction:
    - this is no longer a bad-base or bad-slot theory
    - the next honest target is the `sload_int` compare/lowering path on the
      carried-`total` lane after clone-ladder collapse

- Timestamp: `2026-04-02 22:11:49 PDT`
- Direct carried-`total` guard skip is rejected on the reduced broad
  non-`UGET` lane
  - artifact:
    [20260402-kdz-number-helper-local-broad-skip-total-guard](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-number-helper-local-broad-skip-total-guard/summary.md)
  - env:
    - `LUAJIT_S390X_HOTSIDE_CANON_SHARE_EQUIV=1`
    - `LUAJIT_S390X_SKIP_TOTAL_SLOAD_INT_GUARD=1`
  - reduced localized `number_helper_local_tobit` stays correct:
    - `RESULT -149783296`
  - but the structural floor does not move:
    - `TRACE_START 5`
    - `TRACE_STOP 5`
    - `TRACE_ABORT 0`
    - `TEXIT_COUNT 64001`
  - queue correction:
    - the carried-`total` `sload_int` guard was the first marked branch after
      ladder collapse
    - it is not the whole post-collapse floor
    - skipping it just hands control to a later unmarked exit path on the same
      reduced `BC_MOV` replay family
    - the next honest target is that later unmarked exit path, not this guard
      by itself

- Timestamp: `2026-04-02 22:15:40 PDT`
- The later post-collapse path is now proven to be workload-local, not
  posthook noise
  - artifact:
    [20260402-kdz-number-helper-local-broad-skip-total-guard-nopost](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-number-helper-local-broad-skip-total-guard-nopost/summary.md)
  - same reduced lane, but with post-run `traceinfo/traceir` hooks disabled
  - the workload still stays on:
    - `TRACE_START 6`
    - `TRACE_STOP 5`
    - `TRACE_ABORT 0`
    - `TEXIT_COUNT 64001`
  - dominant workload seam is still:
    - `trace 7 exit 0`
    - restored `BC_MOV` (`op=18`)
    - `snapop=18`
    - `snapnent=0`
    - dominant runtime `guardmark=0`
  - correction:
    - the one-off skip only targeted the no-helper sibling's carried-`total`
      lane (`curins=4`, `op1=2`)
    - the localized-helper workload keeps a shifted lane layout:
      - `op1=6` -> loop bound `n`
      - `op1=5` -> current numeric-for value
      - `op1=4` -> localized `tobit`
      - `op1=3` -> carried `total`
  - first surviving workload-only `sload_int` on the localized-helper lane is
    therefore:
    - `curins=6`
    - `op1=3`
    - `ofs=8`
    - `extra=12`
  - queue correction:
    - the earlier `curins=4` no-helper skip does not explain this workload
    - the live floor is the localized-helper carried-`total` lane itself
    - the next exact target is `curins=6 / op1=3 / ofs=8 / extra=12`
      on the reduced localized lane

- Timestamp: `2026-04-03 05:09:36 PDT`
- Signed GC64 integer `SLOAD` extraction is now closed as a non-remediation on
  the localized broad non-`UGET` lane
  - host-backed `kdz` truth pack:
    [20260403-kdz-promotion_core_static_stop-hotside_canon_share-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260403-kdz-promotion_core_static_stop-hotside_canon_share-truth-pack/summary.md)
    with:
    - `LUAJIT_S390X_HOTSIDE_CANON_SHARE_EQUIV=1`
    - `LUAJIT_S390X_GC64_SIGNED_INT_SLOAD=1`
  - localized static-stop subgroup barely changes:
    - `number_helper_literal_stop_real_local_tobit/hot`
      `0.006251s` vs `-joff 0.001359s` (`4.60x`)
    - focused read stays `TRACE_START 1`, `TRACE_STOP 1`, `TRACE_ABORT 0`,
      `TEXIT_COUNT 63999`
  - reduced mechanism probe:
    [20260403-kdz-localized-total-signed-broad-probe](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-kdz-localized-total-signed-broad-probe/summary.md)
  - same dominant seam survives:
    - `trace 7 exit 0`
    - restored `BC_MOV` (`op=18`)
    - first `sload_int`
      `curins=6 op1=3 op2=4 ofs=8 extra=12`
    - dominant runtime `guardmark=0`
  - queue correction:
    - the localized-helper carried-`total` lane is no longer an honest
      logical-vs-signed GC64 compare target on this mechanism
    - the next honest target is exact taken-guard attribution inside the same
      restored `BC_MOV` replay family

- Timestamp: `2026-04-03 05:13:23 PDT`
- Exact-taken follow-up closes the “later unmarked” theory on the same
  localized broad non-`UGET` lane
  - artifact:
    [20260403-kdz-localized-total-signed-broad-guardmark](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-kdz-localized-total-signed-broad-guardmark/summary.md)
  - env overlay adds:
    - `LUAJIT_S390X_GUARDMARK_TAKEN=1`
  - dominant runtime guard is now explicit:
    - `curins 3`
    - `IR=SLOAD`
    - `op1=5`
    - `op2=36`
    - `sload_int ofs 24 extra 28`
  - correction:
    - the first `sload_int` on this reduced lane is still the carried `total`
      (`curins=6`, `op1=3`)
    - but the exact taken guard is the visible current numeric-for value lane
      (`curins=3`, `op1=5`, `op2=36`)
  - queue correction:
    - the remaining post-collapse floor on this localized shape is back on the
      generic dynamic-stop visible-current replay contract
    - not an unmarked late path
    - not another carried-`total` compare/lowering target

- Timestamp: `2026-04-03 05:28:29 PDT`
- Direct corrected signed-expected follow-up is closed as a dynamic-lane
  reject, not a promotion-core remediation
  - artifacts:
    [20260403-kdz-localized-current-fix-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-kdz-localized-current-fix-check/summary.md)
    and
    [20260403-kdz-localized-current-fix-paired-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-kdz-localized-current-fix-paired-check/summary.md)
  - correcting the signed expected constant does move the exact guard:
    - old visible-current `sload_int` exact guard clears
    - dominant runtime `guardmark` moves to `curins 5`
    - `TRACEIR tr=1 ins=5 op=MULOV op1=3 op2=-6`
  - reduced side-path shape becomes explicit:
    - `trace 4` is `num CONV -> num MUL -> int TOBIT -> int ADD`
  - but the lane is not correctness-safe:
    - `RESULT 1323881804`
    - `TRACE_START 2`
    - `TRACE_STOP 2`
    - `TRACE_ABORT 0`
    - `TEXIT_COUNT 202`
  - pairing `LUAJIT_S390X_JFORI_INTERP_HANDOFF=1` does not change that
  - queue correction:
    - the corrected signed compare is a real bug-fix signal
    - but on the dynamic localized-helper lane it only advances control into a
      later overflow / `TOBIT` continuation bug
    - it is not the next honest performance promotion line

- Timestamp: `2026-04-03 05:34:59 PDT`
- Real static-stop FFI siblings are now split into their own exploratory slice
  and kept out of `promotion_core_static_stop`
  - standalone reducer:
    [ffi_calls_static_stop.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/ffi_calls_static_stop.lua)
  - direct `kdz` probe:
    [20260403-kdz-ffi-static-stop-direct-probe](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-kdz-ffi-static-stop-direct-probe/summary.md)
  - both real static-stop forms are correctness-broken under JIT:
    - `direct_abs_literal_stop_real`: `RUN1 486`, `RUN2 0`, `RUN3 0`
    - `stored_abs_literal_stop_real`: `RUN1 486`, `RUN2 0`, `RUN3 0`
  - repeated-call shape is small and stable, not the old ladder:
    - `TRACE_START 3`
    - `TRACE_STOP 3`

- Timestamp: `2026-04-03 21:00:49 PDT`
- The branch now keeps one real signed-int backend correction and drops the
  latest rejected consumer probes
  - retained code fix:
    - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h)
    - in the generic signed GC64 integer `SLOAD` compare path, expected tag is
      now loaded as direct signed `LJ_TISNUM`
    - this removes the old `-1` expected-constant bug on carried signed-int
      lanes
  - retained validation:
    - with the hidden-current compare fix also enabled, `be_pack_loop` stays
      correct at `2048032000`
  - rejected this cycle:
    - `lj_snap_replay()` replacement / unparent probes
    - `lj_record_ret()` rematerialization probes
    - consumer-side `FLOAD` base masking
    - consumer-side `AHUVLOAD` base masking
  - queue correction:
    - the live blocker after the two compare fixes is a later post-compare
      stable-callsite consumer crash
    - not another signed compare bug
    - not another replay or return-slot rematerialization fix
    - `TRACE_ABORT 0`
    - `TEXIT 401`
    - texit histogram:
      - `1:1=200`
      - `2:1=2`
      - `3:0=199`
  - queue correction:
    - the static-stop FFI subgroup is a real new JIT correctness lane
    - it is not benchmark-ready route-around evidence for `promotion_core`
    - if pursued, the next target is this small repeated-call FFI call seam,
      not the generic dynamic-stop numeric-for floor

- Timestamp: `2026-04-03 10:43:00 PDT`
- Exact-taken probing closes the first wrong FFI static-stop theory
  - direct clean `kdz` rerun with `LUAJIT_S390X_GUARDMARK_TAKEN=1` shows the
    repeated side exit is not the `CALLXS abs -> ADDOV` guard itself
  - dominant runtime seam stays:
    - `trace 1 exit 1`
    - restored `snapop=32`
    - dominant exact `guardmark=0x12`
  - on this branch, `guardmark` values match `curins`, so `0x12` pins the
    first literal failing guard to:
    - `curins 18`
    - `int SLOAD #2 T`
  - queue correction:
    - the first live failure on this lane is the accumulator-slot `SLOAD`
      typecheck immediately before `ADDOV`
    - not the `ADDOV` classifier itself
    - so the failed right-operand normalization experiment was aimed at the
      wrong layer

- Timestamp: `2026-04-03 10:47:00 PDT`
- Numeric accumulator initialization does not rescue the static-stop FFI lane
  - direct clean `kdz` control with:
    - `local total = 0.0`
    - same `ffi.C.abs` loop body
  - still returns the same broken sequence:
    - `RUN1 486`
    - `RUN2 0`
    - `RUN3 0`
  - queue correction:
    - this lane is not explained by integer accumulator initialization alone
    - keep it fenced as a separate FFI/call correctness line rather than
      folding it back into the current promotion-core performance queue

- Timestamp: `2026-04-03 06:28:03 PDT`
- `CALLXS`-fed `ADDOV` narrowing is rejected as a remediation for the static-stop
  FFI lane
  - direct clean `kdz` gated check:
    [20260403-kdz-ffi-static-stop-no-callxs-addov](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-kdz-ffi-static-stop-no-callxs-addov/summary.md)
  - same-callsite clean `kdz` control under the same gate:
    [20260403-kdz-ffi-static-stop-same-callsite-no-callxs-addov](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-kdz-ffi-static-stop-same-callsite-no-callxs-addov/summary.md)
  - the gate does change the root IR in the intended way:
    - `CALLXS -> SLOAD #2 T -> ADDOV`
    - becomes `CALLXS -> SLOAD #2 T -> num CONV -> num ADD`
  - but both real workloads stay wrong:
    - `direct_abs_literal_stop_real`: `RESULT 0`
    - `stored_abs_literal_stop_real`: `RESULT 0`
  - same-callsite also stays wrong:
    - `direct_abs_literal_stop_same_callsite`: `RESULT 0`
    - `stored_abs_literal_stop_same_callsite`: `RESULT 0`
  - queue correction:
    - the remaining bug is not multi-callsite replay noise
    - and it is not fixed by removing `CALLXS`-fed `ADDOV`
    - after the gate, the live seam moves to the later return/caller-loop
      continuation:
      - dominant exact exit `trace 4 exit 2`
      - restored `snapop=76`
      - dominant runtime `guardmark=0x6`
      - exact runtime guard `curins 6`, `IR LE`
    - next honest target is the lower-frame return / caller-loop continuation on
      the num-accumulation path, not more `CALLXS` narrowing work

- Timestamp: `2026-04-03 07:45:41 PDT`
- The static-stop FFI same-callsite lane is now pinned as a lower-frame result
  slot rebasing problem, not a caller-loop `LE` primary seam
  - clean `kdz` `RECRET` artifact:
    [20260403-kdz-ffi-static-stop-same-callsite-recret](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-kdz-ffi-static-stop-same-callsite-recret/summary.md)
  - source-backed read:
    - [lj_record_ret()](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c#L1920) does shift the one live result `TRef`
      during `lua_lower_frame_retf`, but only into the lower-frame call-result
      destination
    - on the first lower-frame handoff into `trace 4`, `S390X_RECRET_SLOTS`
      shows the same live `TRef` moving from pre-shift `idx=0` to post-shift
      `idx=5` (`cbase=5`, `nresults=1`)
    - the same-callsite continuation is already recording later at caller
      `RET1` with `prevop=JFORL`, not at the bytecode `MOV` that would normally
      copy the call-result slot into the caller-visible destination/local
    - but the continuation trace still starts from
      `TRACEIR tr=4 ins=1 op=SLOAD op1=2 op2=33`, i.e. the inherited
      caller-visible result alias `num SLOAD #2 PI`
    - matching exit snapshots still record:
      - `slot2=ref1[o=71 t=14 op1=2 op2=33 ...]`
    - [snapshot_slots()](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_snap.c#L103) explicitly treats the current
      `IR_RETF` chain as the cutoff for SLOAD restore elimination, so the
      continuation can keep the old inherited result identity even though the
      lower-frame path only materialized the shifted `cbase=5` destination
  - queue correction:
    - the later `trace 4 exit 2` / `IR LE` site is secondary
    - this is not a generic bad-base replay bug
    - this is not a resumed-`MOV` window problem
    - the active contract mismatch is a caller-visible result alias crossing
      `IR_RETF` without being re-based to the shifted lower-frame destination
    - [snap_usedef()](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_snap.c#L305) now explains why the seam stays live:
      at caller `RET1`, only the caller-visible return slot is treated as live,
      so if the path reached `RET1` without recording the intervening `MOV`,
      the shifted `cbase=5` destination is not enough by itself
    - direct local repair is now closed:
      - artifact:
        [20260403-075054-kdz-baseline-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-075054-kdz-baseline-core-exit-mechanism/summary.md)
      - explicit `BC_RET1`-side rebinding from `J->base[cbase]` did not change
        `RESULT`, `TRACEIR tr=4 ins=1 op=SLOAD op1=2 op2=33`, or the
        `slot2=ref1[...]` exit snapshot
    - fresh lower-frame destination rematerialization is also closed:
      - artifact:
        [20260403-080443-kdz-baseline-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-080443-kdz-baseline-core-exit-mechanism/summary.md)
      - rematerializing the caller-visible lane from `sload(J, cbase)` inside
        `lua_lower_frame_retf` leaves `RESULT 0`, `TRACEIR tr=4 ins=1 ...`,
        and `slot2=ref1[...]` unchanged
      - queue correction:
        - the active seam is later than the local `lua_lower_frame_retf`
          window
        - `frame_pc(frame)` is still at an earlier caller PC, while the live
          failure is already one bytecode later at caller `RET1`
        - the next honest remediation target is the `IR_RETF` /
          snapshot/use-def identity boundary, not more local
          `lj_record_ret()` slot surgery
    - targeted `snapshot_slots()` logging narrows that boundary again:
      - artifact:
        [20260403-080945-kdz-baseline-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-080945-kdz-baseline-core-exit-mechanism/summary.md)
      - at the actual failing `RET1` snapshot pass, the current frame is
        already collapsed to `baseslot=2`, `maxslot=1`
      - in that final pass, only the stale caller-visible lane is
        considered/kept:
        - `slot=2`, `rel=0`, `op=SLOAD`, `op1=2`, `op2=33`
      - the shifted lower-frame destination is not being explicitly pruned
        there; it is already outside the current `nslots` window
      - queue correction:
        - the live loss happens before the failing `RET1` snapshot build
        - `snapshot_slots()` itself is too late for repair on this lane
        - the next honest target is the earlier frame-window / slot-identity
          collapse between `IR_RETF` and the later caller `RET1`
    - exact `snap_usedef()` logging now closes that earlier target too:
      - artifact:
        [20260403-082040-kdz-baseline-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-082040-kdz-baseline-core-exit-mechanism/summary.md)
      - the first real collapse point is not the later caller `RET1` pass
      - it is the earlier caller `RET0` use/def pass under active `IR_RETF`
      - exact read:
      - `op=75` (`RET0`)
      - `prevop=50` (`UCLO`)

- Timestamp: `2026-04-03 10:05:00 PDT`
- The lower-frame continuation seam is now pinned as generic same-callsite
  return-slot identity loss, not an FFI-only bug
  - checked-in reducer:
    [lower_frame_same_callsite.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/lower_frame_same_callsite.lua)
  - reducer boundary on clean `kdz`:
    - the bench harness stays correct
    - the exact direct same-callsite script still returns `LUA1 0`, `LUA2 0`
    - so the checked-in file is a control surface, not the live reproducer
  - control result:
    - outer same-callsite constant return stays correct
    - pure-Lua inner hot loop under the same outer call/loop/return shape also
      collapses to the same continuation family
  - queue correction:
    - the old static-stop FFI lane is one instance of a generic lower-frame
      continuation / result-slot identity seam after a hot inner loop
    - the next honest replay fix must target that generic continuation family,
      not FFI-specific `CALLXS`/`ADDOV` behavior
        - `baseslot=2`
        - `maxslot=18`
        - `retf=3`
        - only `idx=17` still carries `ref=1`, `type=14`
      - queue correction:
        - the later failing `RET1` snapshot only exposes an identity that has
          already been collapsed by the earlier `BC_RET0` return-window rule
        - `lj_record_ret()` is too early for repair
        - the final `snapshot_slots()` pass is too late
        - the next honest remediation family is the `snap_usedef()` return
          window itself when `IR_RETF` is active
    - first direct remediation attempts on that `snap_usedef()` boundary are
      now rejected:
      - keep-live gate:
        [20260403-082728-kdz-baseline-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-082728-kdz-baseline-core-exit-mechanism/summary.md)
        - `LUAJIT_S390X_RETF_RET0_KEEP_LIVE=1`
        - keeps all currently nonzero lanes live across active `RET0`
        - result is structurally inert:
          - same `RESULT 0`
          - same `TRACEIR tr=4 ins=1 op=SLOAD op1=2 op2=33`
          - same `slot2=ref1[...]`
      - slot0-rebind gate:
        [20260403-082941-kdz-baseline-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-082941-kdz-baseline-core-exit-mechanism/summary.md)
        - `LUAJIT_S390X_RETF_RET0_REBIND_SLOT0=1`
        - if slot `0` is empty and there is exactly one live lane, copy that
          lane into `J->base[0]` before snapshot build
        - result is also structurally inert:
          - same `RESULT 0`
          - same continuation front and same exit snapshot
      - queue correction:
        - the remaining mismatch is deeper than return-window liveness alone
        - it is deeper than local slot `0` rebinding before snapshot build
        - the next honest remediation family is snapshot-map / inherited-lane
          identity replacement at the `IR_RETF` continuation boundary
    - next honest target is lower-frame result-alias rebasing or
      rematerialization across `IR_RETF`, specifically snapshot-map /
      inherited-lane identity replacement, not more `CALLXS` narrowing, not
      more caller-loop attribution, and not more local `MOV` window
      experiments

### After that

There are only two realistic outcomes:

- a selective promotion boundary exists and the dedicated hotside gate moves
  forward as the current throughput candidate without touching frozen iterator
  behavior, or
- no such boundary exists, and the branch keeps the gate as an opt-in
  throughput candidate while iterator remains frozen at the current checkpoint

## Where To Look Next

- Technical notebook:
  [findings.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/findings.md)
- Current perf map:
  [perf.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/perf.md)
- Validation workflow:
  [runbook.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/runbook.md)

## 2026-04-03 15:20 PDT

- `promotion_core` next issue is now pinned more narrowly than “restored
  header carried-total lane”.
- Under the paired control that relaxes the root visible-current guard and
  preserves the first local integer snapshot lane:
  - `LUAJIT_S390X_FORL_ROOT_VISIBLE_IDX_NOGUARD=1`
  - `LUAJIT_S390X_KEEP_FIRST_LOCAL_SLOAD_SNAP=1`
  the first exact taken guard is still the carried accumulator lane:
  - `curins 15`
  - `IR=SLOAD`
  - `op1=3`
  - `op2=4`
  - `ofs=8`
  - `extra=12`
- widened birth logging now proves where that lane comes from:
  - visible current-value lanes are born by `sloadt()` in
    [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c)
    while recording `BC_UGET`
  - the carried accumulator lane is born later by plain `sload()` while
    recording `BC_ADDVV`
  - exact birth:
    - `pcop=32`
    - `baseslot=2`
    - `slot=1`
    - `abs=3`
    - `mode=4`
    - `ref=15`
- direct restored-header rematerialization is rejected:
  - seeding `J->base[1]` from the preserved local integer lane at the header
    leaves the exact guard and exit counts unchanged
- next honest remediation target:
  - the `BC_ADDVV` accumulator operand specialization path
  - not more restored-header rematerialization
  - not more visible-current `FORL_IDX` no-guard variants

## 2026-04-03 16:11 PDT

- The direct `BC_ADDVV` accumulator specialization family is now classified.
- On the carried-`total` control, `BC_ADDVV` entry logging shows:
  - `pcop=32`
  - `ra=8`, `rb=1`, `rc=8`
  - `baseslot=2`
  - `J->base[1] == 0`
  - `J->slot[3] == 0`
- So the accumulator lane is genuinely absent before consumer-side
  specialization begins.
- The direct patch family:
  - `LUAJIT_S390X_ADDVV_ACCUM_INT_NOGUARD=1`
  - only on `BC_ADDVV`
  - only for `rb==1`
  - seeds the operand with `sloadt(... IRT_INT, 0)` instead of
    `getslot()->sload()`
- What it buys:
  - under paired root visible-current relax, the carried-`total` lane is no
    longer the first payer
  - `curins 15 / SLOAD op1=3` remains in the loop body, but now as `op2=0`
- What it does not buy:
  - the first exact taken guard simply snaps back to the visible current-value
    lane on both control and payoff sibling:
    - `curins 3`
    - `IR=SLOAD`
    - `op1=4`
    - `op2=36`
  - throughput stays near the current default and remains exit-dominated:
    - `number_helper_loop/hot 0.008697`
    - `be_pack_loop/hot 0.023731`
- Conclusion:
  - the `BC_ADDVV` accumulator family is a closed sub-remediation
  - it removes the carried accumulator typecheck, but only re-exposes the old
    visible-current seam
  - next honest target remains the visible current-value replay/typecheck
    contract itself, with this `BC_ADDVV` result kept as carry-forward
    evidence for what sits underneath it

## 2026-04-03 16:22 PDT

- The combined recorder family is now closed:
  - `LUAJIT_S390X_FORL_ROOT_VISIBLE_IDX_NOGUARD=1`
  - `LUAJIT_S390X_ADDVV_ACCUM_INT_NOGUARD=1`
- On the mechanism control
  [20260403-161644-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-161644-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism/summary.md),
  the first exact taken guard finally moves off replay/header `SLOAD`:
  - `RESULT 1323881804`
  - `trace 1 exit 0`
  - runtime `guardmark curins 14`
  - exact front-most consumer:
    - `TRACEIR tr=1 ins=14 op=MULOV`
    - `op1=3`
    - `op2=-12`
  - the carried accumulator lane remains but is already unguarded:
    - `TRACEIR tr=1 ins=15 op=SLOAD op1=3 op2=0`
- On the payoff sibling
  [20260403-161839-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-161839-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism/summary.md),
  the family does not preserve the live seam:
  - `TRACE_START 1`
  - `TRACE_STOP 1`
  - `TEXIT_COUNT 2`
  - dominant exit shifts to `trace 2 exit 0`
  - dominant runtime `guardmark curins 1`
- Conclusion:
  - this family is not promotable
  - it is correctness-bad on `number_helper_loop`
  - and not seam-preserving on `be_pack_loop`
  - but it identifies the next real target cleanly:
    the first loop-body `MULOV` consumer that becomes front-most once both
    stack `SLOAD` lanes are relaxed
  - the arithmetic audit closes the obvious follow-up:
    `bit.tobit()` is not enough to justify stripping `MULOV`, because
    [src/lj_opt_narrow.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_opt_narrow.c)
    explicitly treats wrapped `MULOV` as non-strip-safe due to precision
    widening

## 2026-04-03 16:42 PDT

- The replay-side-only visible-current rebuild family is now closed.
- Opt-in experiment:
  - `LUAJIT_S390X_FORL_REPLAY_VISIBLE_IDX_NOGUARD=1`
- Targeted rule:
  - only on `rec_for_loop(init=0)` replay-side rebuild of the visible current
    `FORL_IDX` lane
  - root-born emission and matched-fastpath reuse left unchanged
- Exact-taken control:
  [20260403-164012-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-164012-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism/summary.md)
- Result:
  - structurally inert on `number_helper_loop`
  - same `trace 7 exit 0`
  - same runtime `guardmark curins 3`
  - same exact guard:
    - `IR=SLOAD`
    - `op1=4`
    - `op2=36`
    - `ofs=16`
    - `extra=20`
  - same first carried accumulator lane:
    - `curins 15`
    - `IR=SLOAD`
    - `op1=3`
    - `op2=4`
- Conclusion:
  - the narrow replay-side `rec_for_loop(init=0)` hook is not the active owner
    of the shipping seam
  - relaxing it does not move the live payer at all
  - this closes the cleanest replay-only emitter theory

## 2026-04-03 16:55 PDT

- The backend-only visible-current `SLOAD` family is now classified and
  rejected.
- Opt-in experiment:
  - `LUAJIT_S390X_ASM_VISIBLE_IDX_SLOAD_NOGUARD=1`
- Targeted rule:
  - only in [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h)
  - skip the asm-side integer typecheck compare for inherited
    `SLOAD op1=4` with `IRSLOAD_INHERIT|IRSLOAD_TYPECHECK`
  - recorder and replay emission unchanged
- Exact control:
  [20260403-164918-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-164918-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism/summary.md)
  - `number_helper_loop` stays on the same dominant `trace 7 exit 0` family
  - exact first guard moves from the visible-current lane to the carried
    accumulator lane:
    - old: `curins 3 / SLOAD op1=4 op2=36`
    - new: `curins 15 / SLOAD op1=3 op2=4`
- Payoff sibling mirror:
  [20260403-165053-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-165053-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism/summary.md)
  - `be_pack_loop` matches the same displacement:
    - same dominant `trace 7 exit 0`
    - exact guard becomes `curins 35 / SLOAD op1=3 op2=4`
- Throughput and smoke:
  [20260403-kdz-be_helpers-hotside_canon_share_uget_looproot_default-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260403-kdz-be_helpers-hotside_canon_share_uget_looproot_default-truth-pack/summary.md)
  - smoke is wrong:
    - `number_helper_loop check: 13762770`
    - `be_pack_loop check: 210`
  - hot medians regress:
    - `number_helper_loop/hot 0.010473`
    - `be_pack_loop/hot 0.027104`
- Conclusion:
  - this is useful ownership evidence for the asm-side compare
  - but not a promotable remediation family
  - it fails correctness and makes the payoff sibling slower

## 2026-04-03 18:36 PDT

- The next post-current-seam `be_pack_loop` issue is now pinned exactly.
- Discovery surface:
  - keep the shipping default frozen
  - use opt-in hidden-current compare repair only as a discovery aid:
    - `LUAJIT_S390X_FORL_CURRENT_COMPARE_FIX=1`
  - widen debug compare-truth logging to the next carried lane:
    - `SLOAD op1=3 op2=4`
- Carried-lane artifact:
  [20260403-kdz-be-pack-carried-cmptruth](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-kdz-be-pack-carried-cmptruth/summary.md)
- Read:
  - clean direct rerun on synced `kdz` no longer reproduces the old exit `139`
  - stripped result-only confirmation returns the correct hot value:
    - `2048032000`
  - exact taken guard still reports the carried accumulator lane:
    - `guardmark=35`
    - `curins=35`
    - `IR=SLOAD`
    - `op1=3`
    - `op2=0x4`
    - `ofs=8`
    - `extra=12`
  - but compare-truth now shows that lane is semantically clean:
    - boxed int raw slot
    - `logical_eq=1`
    - `signed_eq=1`
  - nearby guard cluster under the same restored-header seam is:
    - `curins 37`
    - `curins 35`
    - `curins 33`
  - nearby IR window is:
    - `ins 33`: `EQ`
    - `ins 35`: `SLOAD`
    - `ins 37`: `UGT`
- Conclusion:
  - the carried accumulator `SLOAD` compare is not the next broken compare
  - the next real issue is adjacent guard ownership/consumer attribution in
    the same restored-header cluster
  - the next remediation target should start at `UGT curins 37`, not another
    `SLOAD` compare patch

## 2026-04-04 00:42 PDT

- The cleaned compare-fix checkpoint now holds on both `kdz` and `zkd0`.
- Checkpoint artifact:
  [20260404-hostpair-comparefix-phi-checkpoint](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260404-hostpair-comparefix-phi-checkpoint/summary.md)
- Active experimental surface:
  - `LUAJIT_S390X_FORL_CURRENT_COMPARE_FIX=1`
  - `LUAJIT_S390X_GC64_SIGNED_INT_SLOAD=1`
- Retained code changes:
  - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h)
    - generic signed GC64 int `SLOAD` compare constant correction
    - `asm_tobit()` rewrite using the VM-style `LGDR`/`LGFR` extraction path
  - [src/lj_asm.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm.c)
    - duplicate the right PHI input into scratch on the exact
      `ref18/ref23 -> phi27` handoff instead of birthing the PHI directly in
      `r12`
- Host-pair validation:
  - exact `32768` oracle:
    - `kdz`: `JIT 1610629120`, `INTERP 1610629120`
    - `zkd0`: `JIT 1610629120`, `INTERP 1610629120`
  - reduced helper validators:
    - `number_helper_loop(64000)` matches on both hosts:
      `-149783296 == -149783296`
    - reduced `be_pack_loop(64000)` matches on both hosts:
      `-32000 == -32000`
  - focused `kdz` perf under the same gates:
    - `number_helper_loop/hot median=0.000092`
    - `be_pack_loop/hot median=0.000270`
- Classification:
  - the cleaned checkpoint is stable enough to preserve
  - this is still experimental and env-gated, not a promoted default policy
  - the next step should be deciding whether to promote the `asm_phi()` handoff
    change under the compare-fix family or keep it as a narrower diagnostic
    checkpoint
