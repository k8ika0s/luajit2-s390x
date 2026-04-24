# GC64 Integer SLOAD Repair Boundary

This note summarizes one of the recurring correctness seams during the s390x
bring-up: integer `SLOAD` handling on GC64, especially when values cross
snapshot, replay, or width-reconstruction boundaries.

## The Real Boundary

The issue is not just "integer loads are wrong on GC64". The fragile seam is
the combination of:

- typed `SLOAD` creation in the recorder,
- rematerialization and replay through snapshots,
- big-endian width reconstruction,
- backend assumptions about where the live integer bits reside.

Any fix that treats only one of those layers in isolation tends to move the bug
instead of removing it.

## Durable Lessons

- Preserve the distinction between the logical IR type and the physical word
  layout used at restore time.
- Be careful when reusing generic GC64 assumptions on big-endian targets.
- Validate fixes with exit-heavy and replay-heavy cases, not just straight-line
  traces.
- Treat cdata width reconstruction and integer `SLOAD` repair as adjacent
  problems.

## Safe Repair Scope

The safest fixes in this area are narrow contract repairs:

- make the recorder or snapshot layer state the intended type precisely,
- restore the correct subword on big-endian paths,
- keep backend lowering aligned with that contract,
- avoid broad relaxations that merely silence a guard or widen an accepted
  pattern.

## Validation Expectations

Changes in this area should be validated with:

- focused GC64 replay and side-exit cases,
- any mixed-width cdata tests that depend on subword restore,
- at least one broader native JIT lane to ensure the repair does not shift a
  different replay path out of alignment.

## Related Docs

- [findings.md](findings.md)
- [state-of-project.md](state-of-project.md)
- [upstream-cleanup.md](upstream-cleanup.md)
