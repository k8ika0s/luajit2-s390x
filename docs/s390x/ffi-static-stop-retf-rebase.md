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
  lane to the lower-frame destination:
  - pre-shift `idx=0`
  - post-shift `idx=5`
  - `cbase=5`
  - `nresults=1`
- but the continuation snapshot still keeps the pre-`RETF` parent result lane:
  - `slot2=ref1[o=71 t=14 op1=2 op2=33 ...]`
  - `TRACEIR tr=4 ins=1 op=SLOAD op1=2 op2=33`
- [snapshot_slots()](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_snap.c#L103) uses the current `IR_RETF` chain as
  the cutoff for SLOAD restore elimination, so this pre-`RETF` inherited lane
  survives into the continuation instead of being re-based to the shifted
  lower-frame destination

Bounded remediation target:

- do not treat the caller-loop `LE` as the primary failure
- do not reopen `CALLXS`/`ADDOV` narrowing
- do not reopen promotion-core dynamic-stop work
- instead:
  - rebase or rematerialize the lower-frame result lane across `IR_RETF`
  - keep the continuation loading the shifted caller destination, not the dead
    pre-shift parent result slot

Hard stop conditions:

- if the attempted repair only moves the exact exit later without fixing the
  same-callsite correctness failure, reject it
- if the repair broadens lower-frame return behavior outside this tiny FFI
  static-stop lane before proof exists, reject it
