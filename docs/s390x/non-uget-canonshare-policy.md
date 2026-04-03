# Non-UGET Canon/Share Policy Boundary

Last updated: 2026-04-02 21:43:00 PDT

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
