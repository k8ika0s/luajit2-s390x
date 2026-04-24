# Same-Callsite Lower-Frame Continuation Rebase

This note records the technical boundary behind an earlier same-callsite
lower-frame continuation failure. The useful lesson remains current even though
the original reproducer trail was tied to one historical FFI lane.

## Core Reading

The failure was not specific to FFI. The FFI-shaped workload only exposed a
more general problem:

- a hot inner loop returns through a lower-frame path,
- continuation recording resumes later at the caller return path,
- the caller-visible result slot can still be anchored to the pre-`IR_RETF`
  inherited lane instead of the shifted lower-frame destination.

That means the continuation can consume the wrong result identity even though
the lower-frame call-result destination itself was materialized correctly.

## Why The Old Local Fixes Failed

The rejected local fixes all had the same weakness: they tried to repair the
value too early or too late.

Too early:

- local rebinding in `lj_record_ret()`
- rematerializing the shifted destination directly after the lower-frame shift

Too late:

- trying to rescue the value during the final snapshot build

Both approaches missed the actual collapse point. The useful conclusion from
the earlier probes is that the identity loss happens at the return-window
boundary while `IR_RETF` is still active, before the final continuation
snapshot becomes observable.

Relevant code areas:

- [`src/lj_record.c`](../../src/lj_record.c)
- [`src/lj_snap.c`](../../src/lj_snap.c)

## Most Likely Real Boundary

The honest boundary is the interaction between:

- lower-frame result shifting,
- active `IR_RETF`,
- return-window liveness in `snap_usedef()`,
- inherited-lane identity carried into later continuation recording.

The important point is not "the caller loop exits on `LE`" or "the failing
trace starts at `RET1`". Those are symptoms. The deeper problem is that the
caller-visible result alias survives with the wrong identity across the
`IR_RETF` transition.

## Safe Next Fix Area

If this family needs further work, the smallest credible fix area is the
snapshot and inherited-lane identity boundary under active `IR_RETF`, not a new
special case in `lj_record_ret()` and not another local `J->base[]` tweak.

That means:

- rebase the caller-visible result identity from the shifted lower-frame
  destination,
- or replace the stale inherited alias before the return-window collapse makes
  it the only surviving live identity.

## What Not To Reopen

- benchmark-specific FFI static-stop workarounds,
- generic `CALLXS` narrowing changes,
- unrelated promotion or dynamic-stop experiments,
- one-off resumed-`MOV` patches without proof that the identity boundary is
  fixed.
