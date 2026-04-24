# Non-UGET Canon/Share Policy Boundary

This note captures the boundary from earlier broad canon/share experiments on
non-`UGET` seams.

## Durable Conclusion

The default scoped hotside policy should remain tied to the `UGET`/looproot
family. Earlier experiments showed that broadening canon/share onto
non-`UGET` seams can help one localized static-stop subgroup, but that result
does not generalize cleanly and it does not remove the real replay floor.

In other words:

- broad non-`UGET` canon/share can collapse a repeated-call clone ladder,
- but the next surviving payer is still inside the workload's own replay path,
- so "make canon/share broader" is not the right default policy.

## What The Earlier Probes Proved

The earlier reduced probes narrowed the useful reading to this:

- the initial cross-call ladder can be reduced,
- the first surviving floor is then a stack-visible `SLOAD`/`MOV` replay seam,
- once that seam moves, the next visible payer becomes an overflow or `TOBIT`
  continuation family,
- so the apparent policy win is really a handoff from one mechanism debt to
  another.

That is why this doc exists: to prevent the old broad opt-in result from being
misread as evidence for a wider default.

## Current Policy

Keep these boundaries:

- the scoped `UGET`/looproot surface is the defended default,
- localized non-`UGET` experiments stay research-only unless they can prove a
  reusable mechanism,
- do not widen policy based on one static-stop subgroup,
- do not mix this queue with GC64 replay, iterator, dispatch, or vararg work.

## Candidate Entry Conditions

Any future selective non-`UGET` policy should require all of:

1. the path is not already covered by the `UGET`/looproot family,
2. the repeated-call clone ladder is the measured payer,
3. the remaining replay floor is understood after the ladder collapses,
4. the gain is not confined to one benchmark-shaped subgroup.

## Practical Reading

If a future change seems to justify broader canon/share, use this note as the
checklist for rejecting overreach. The burden is to show a real reusable
mechanism, not just an isolated reduction in trace population.
