# FFI Static-Stop RETF Rebase

Status: active design note

Current exact seam on clean `kdz`:

- workload:
  - `direct_abs_literal_stop_same_callsite`
- artifact:
  - [20260403-kdz-ffi-static-stop-same-callsite-recret](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260403-kdz-ffi-static-stop-same-callsite-recret/summary.md)
- continuation trace:
  - `trace 4 start 3/1`
  - first IR lane:
    - `num SLOAD #2 PI`
  - later exact exit:
    - `trace 4 exit 2`
    - `guardmark=0x6`
    - `curins 6`
    - `IR LE`

Pinned contract mismatch:

- [lj_record_ret()](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c#L2016) enters `lua_lower_frame_retf`
  with one live result `TRef`
- `S390X_RECRET_SLOTS` shows that live `TRef` being shifted from the callee
  lane only into the lower-frame call-result destination:
  - pre-shift `idx=0`
  - post-shift `idx=5`
  - `cbase=5`
  - `nresults=1`
- the same-callsite continuation is already recording later at caller `RET1`
  with `prevop=JFORL`, not at the bytecode `MOV` that would normally copy the
  call-result slot into the caller-visible destination/local
- but the continuation snapshot still keeps the inherited caller-visible result
  lane:
  - `slot2=ref1[o=71 t=14 op1=2 op2=33 ...]`
  - `TRACEIR tr=4 ins=1 op=SLOAD op1=2 op2=33`
- [snapshot_slots()](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_snap.c#L103) uses the current `IR_RETF` chain as
  the cutoff for SLOAD restore elimination, so the continuation can keep the
  old inherited result identity even though the lower-frame path only
  materialized the shifted `cbase=5` destination

Current exact reading:

- this is not a generic bad-base replay bug
- this is not an immediate resumed-`MOV` peephole
- it is a caller-visible result-alias mismatch across `IR_RETF`:
  - the lower-frame path materializes the call-result destination
  - the continuation later consumes a caller-visible result alias
  - that alias is still anchored to the pre-`RETF` inherited lane
- [snap_usedef()](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_snap.c#L305) makes the boundary sharper:
  - at caller `RET1`, only the caller-visible return slot is live
  - the shifted call-result destination (`cbase=5`) is not preserved by the
    snapshot on its own
  - if the path reaches `RET1` without recording the intervening `MOV`, the
    only surviving identity for that live return slot is the stale pre-`RETF`
    inherited alias

Bounded remediation target:

- do not treat the caller-loop `LE` as the primary failure
- do not reopen `CALLXS`/`ADDOV` narrowing
- do not reopen promotion-core dynamic-stop work
- do not chase local resumed-`MOV` window patches
- instead:
  - rebase or rematerialize the caller-visible result alias across `IR_RETF`
  - keep the continuation loading a slot identity derived from the shifted
    lower-frame destination, not the pre-`RETF` inherited lane
  - do it before the `RET1`-side snapshot/use-def pass can prune the only
    correct shifted destination slot

Hard stop conditions:

- if the attempted repair only moves the exact exit later without fixing the
  same-callsite correctness failure, reject it
- if the repair broadens lower-frame return behavior outside this tiny FFI
  static-stop lane before proof exists, reject it
