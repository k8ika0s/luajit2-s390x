# Non-UGET Canon/Share Policy Boundary

Last updated: 2026-04-03 05:09:36 PDT

## Why This Exists

The current shipping throughput default in
[lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c)
is intentionally scoped to the `UGET`/looproot seam.

That is still the correct envless default.

But the localized static-stop `number_helper` subgroup proved one narrower
follow-on fact:

- explicit broad canon/share on non-`UGET` seams can materially reduce the
  cross-call clone ladder
- the same broad policy does not generalize cleanly to the sibling
  `be_pack_literal_stop_real`
- and even where it helps, it still leaves the subgroup slower than `-joff`

So the next honest target is not “turn broad canon/share on everywhere”.
It is a selective non-`UGET` canon/share policy.

## Proven Boundary

Real evidence so far:

- localized static-stop `number_helper`
  - broad opt-in reduces repeated-call trace population and wall time on `kdz`
  - still remains `exit-dominated` in authoritative host-pair truth packs
- localized static-stop `be_pack`
  - barely moves under the same broad opt-in

Reduced post-collapse seam on clean `kdz`:

- broad opt-in env:
  - `LUAJIT_S390X_HOTSIDE_CANON_SHARE_EQUIV=1`
- reduced localized no-helper sibling (`number_helper_local_tobit`, `n=400`)
  still exits at restored `BC_MOV` (`op=18`)
- first exact surviving guard in that reduced lane:
  - `kind=sload_int`
  - `curins=4`
  - `ofs=0`
  - `extra=4`
- later guards in the same cluster include:
  - `curins=3`
  - `kind=sload_type`
  - `curins=2`
  - `ofs=8`
  - `extra=8`

So broad non-`UGET` canon/share collapses the cross-call ladder, but it does
not remove the first stack-visible `SLOAD` guard cluster on the localized
`MOV` seam.

Tighter attribution on the same reduced lane:

- direct `kdz` reduced trace-IR:
  - `TRACEIR tr=1 ins=4 op=SLOAD op1=2 op2=4`
  - `TRACEIR tr=1 ins=2 op=SLOAD op1=3 op2=4`
  - `TRACEIR tr=1 ins=3 op=MULOV op1=1 op2=-6`
- that maps the first marked post-collapse guard to the carried `total` reload,
  not the localized helper slot and not the current loop value
- direct `SLOADMAP` + slot logging on the same seam shows:
  - `curins=4 ref=4 kind=int op1=2 op2=0x4 ofs=0 vofs=4 base=11`
  - restored base is the normal stack base
  - `S390X_SLOT idx=0` is a valid boxed int advancing as expected

Direct carried-`total` guard-skip reject on the same reduced lane:

- artifact:
  [20260402-kdz-number-helper-local-broad-skip-total-guard](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-number-helper-local-broad-skip-total-guard/summary.md)
- env overlay:
  - `LUAJIT_S390X_SKIP_TOTAL_SLOAD_INT_GUARD=1`
- result stays correct:
  - `RESULT -149783296`
- structural counts do not move:
  - `TRACE_START 5`
  - `TRACE_STOP 5`
  - `TRACE_ABORT 0`
  - `TEXIT_COUNT 64001`
- correction:
  - the carried-`total` `sload_int` guard is the first marked branch after the
    ladder collapse
  - it is not the whole floor by itself
  - skipping it just hands the reduced lane to a later unmarked exit path on
    the same restored `BC_MOV` replay family

So the next exact target is no longer the carried-`total` compare/lowering by
itself. It is the later unmarked exit path that survives after that guard is
removed.

Workload-only confirmation on the same reduced lane:

- artifact:
  [20260402-kdz-number-helper-local-broad-skip-total-guard-nopost](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-number-helper-local-broad-skip-total-guard-nopost/summary.md)
- `--no-posthooks` proves the later path is inside the workload, not helper
  traceinfo noise
- reduced `number_helper_loop_local_tobit` still exits every trip:
  - `TRACE_START 6`
  - `TRACE_STOP 5`
  - `TRACE_ABORT 0`
  - `TEXIT_COUNT 64001`
- dominant workload seam remains:
  - `trace 7 exit 0`
  - restored `BC_MOV` (`op=18`)
  - `snapop=18`
  - `snapnent=0`
  - dominant runtime `guardmark=0`
- corrected attribution:
  - the one-off skip was scoped to the no-helper sibling's carried-`total`
    lane (`curins=4`, `op1=2`)
  - on the localized-helper workload, the reduced trace-IR lane layout is:
    - `ins=1 SLOAD op1=6` -> loop bound `n`
    - `ins=3 SLOAD op1=5` -> current numeric-for value
    - `ins=4 SLOAD op1=4` -> localized `tobit`
    - `ins=6 SLOAD op1=3` -> carried `total`
- first surviving `sload_int` in that workload-only cluster is therefore:
  - `curins=6`
  - `op1=3`
  - `ofs=8`
  - `extra=12`

So the next honest target is the later workload-only cluster headed by
the localized-helper carried-`total` lane
`curins=6 / op1=3 / ofs=8 / extra=12`, not the no-helper sibling's skipped
`curins=4 / op1=2` lane.

This means the useful boundary is not “all non-`UGET` seams”. It is narrower.

## Candidate Entry Conditions

Any future selective non-`UGET` policy should require all of:

1. not matched by the shipping `UGET`/looproot gate
2. same-root equivalent side-trace chain exists and is already accepted by the
   broad matcher
3. repeated-call clone ladder is the measured payer, not the within-run replay
   floor
4. localized/static-stop subgroup only, until broader evidence exists

## Hard Non-Goals

Do not treat this as permission to:

- widen the envless default
- reopen the old generic broad hotside experiments
- reopen low32-home or GC64 replay-tag families
- reopen iterator, dispatch, or vararg

## Next Decision

Before any new code family:

- prove the exact repeated-call lane that broad canon/share helps on the real
  localized static-stop `number_helper` shape
- prove that the same policy does not pay for unrelated non-`UGET` seams
- use the reduced `MOV` + `sload_int ofs=0 extra=4` seam as the first exact
  post-collapse target, not the old `UGET` clone-ladder itself

Only then decide whether a dedicated selective policy/gate is justified.

## Rejected Follow-On

Layering signed GC64 integer `SLOAD` extraction onto the same reduced broad
non-`UGET` lane does not move the remaining floor.

- host-backed `kdz` truth pack:
  [20260403-kdz-promotion_core_static_stop-hotside_canon_share-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260403-kdz-promotion_core_static_stop-hotside_canon_share-truth-pack/summary.md)
  - `number_helper_literal_stop_real_local_tobit/hot`
    `0.006251s` vs `-joff 0.001359s` (`4.60x`)
  - focused read stays `TRACE_START 1`, `TRACE_STOP 1`, `TRACE_ABORT 0`,
    `TEXIT_COUNT 63999`
- reduced `kdz` mechanism probe:
  [20260403-kdz-localized-total-signed-broad-probe](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-kdz-localized-total-signed-broad-probe/summary.md)
  - dominant seam is unchanged:
    - `trace 7 exit 0`
    - restored `BC_MOV` (`op=18`)
    - first `sload_int`
      `curins=6 op1=3 op2=4 ofs=8 extra=12`
    - dominant runtime `guardmark=0`

So the old logical-vs-signed GC64 int-tag extraction issue is not the live
post-collapse payer on this localized broad-canon/share lane anymore. The
next honest target is the later unmarked exit path on the same restored
`BC_MOV` replay family, not another carried-`total` compare tweak.
