# Filtered Hotside Promotion Plan

This note records the defended boundary for the scoped hotside promotion work.

## Current Reading

The important conclusion is not the exact old timing table. It is the scope
decision:

- the promoted surface is the narrow `UGET`/looproot seam,
- broader canon/share policy is out of scope,
- iterator and dispatch families are not evidence for this queue,
- the work is about repeated-call clone-ladder collapse, not about solving
  every side-exit or replay floor.

## Mechanism

The useful mechanism is:

- canonicalize hot exits back to an earlier equivalent parent,
- reuse that earlier seam instead of growing a later clone ladder,
- keep the scope limited to the proven hit shape.

The named seam stays:

- `exit=0`,
- `op=BC_UGET`,
- `startop=BC_JMP`,
- loop-like roots such as `BC_FORL` and `BC_FUNCF`.

## First Enable Set

The original promoted slice was the `promotion_core` family. In practice, the
useful examples were:

- `chain_tail_add`
- `chain_tail_store`
- `mix_bits`
- `number_helper_loop`
- `be_pack_loop`
- `direct_abs`
- `stored_abs`

These were the families that best demonstrated the repeated-call seam without
dragging in a different mechanism debt.

## Carry-Forward Only

Some families hit the same seam but were not part of the first enable set:

- `retconst_loop`
- `retlast_loop`
- `mixed_loop`
- `sum_loop`

Keep them as supporting evidence only. They should not silently expand the
scope of the promoted surface.

## Explicitly Out Of Scope

Do not treat these as justification for the same gate:

- `iterator_table`
- `dispatch_trace`
- `mixed_ffi`
- `ffi_cdata`
- `int_add_phi_only`
- `logic_add_phi_noboundary`

Those either depend on a different mechanism or remain dominated by another
cost center.

## Rollout Rule

If this area is restamped or reconsidered:

- validate against the named `promotion_core` slice,
- keep the scope tied to the exact `UGET`/looproot seam,
- do not broaden into global canon/share,
- do not let secondary same-seam families redefine the enable boundary.

The supporting slice builder remains
[`tools/s390x/build_hotside_promotion_slice.py`](../../tools/s390x/build_hotside_promotion_slice.py),
but the underlying policy is more important than any one historical run.
