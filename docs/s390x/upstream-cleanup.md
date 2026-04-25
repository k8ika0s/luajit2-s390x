# s390x Upstream Cleanup

This page tracks the cleanup themes that still matter even when the native
runtime is green on real s390x hardware.

The point is not to preserve every historical tranche. The point is to keep the
current blocker map small and actionable.

## Cleanup Categories To Keep Closed

### 0. Do not add new cleanup debt

Do not add s390x-only recorder substitutions, helper ABIs, or backend peepholes
that exist solely for a benchmark workload. Temporary scaffolding used for
diagnosis must be removed before moving to the next family.

Allowed exceptions need a durable justification such as ABI correctness, a
generic lowering rule, or a reusable backend mechanism. If the explanation is
only "this benchmark gets faster," reject the change instead of carrying new
upstream review debt.

### 1. s390x-only semantic reducers and helper ABIs

The highest-value cleanup remains the removal or narrowing of benchmark-shaped
recorder substitutions and dedicated helper ABIs that exist only to preserve a
local performance floor.

What counts as a blocker:

- recorder matchers for one benchmark family,
- helper ABIs that do not correspond to a reusable lower-level mechanism,
- reducer-driven behavior changes that bypass generic runtime contracts.

### 2. Test and benchmark oracle quality

Performance validation is not upstream-ready if expected values are produced by
the same hot JIT path under test. Oracle cleanup is part of the bring-up exit
criteria, not a later polish task.

### 3. Matrix and report assumptions

Validation claims should come from checked-in matrix definitions and generic
reporting policy, not from private host conventions or ad hoc restamp
workflows.

### 4. Documentation surface

Docs must be readable from a clean checkout. Private hostnames, personal paths,
and stale recovery instructions are blockers because they turn the public
documentation set into a branch-lab notebook.

## Removal Policy

Prefer this order:

1. Remove the smallest reducer or helper that has no unique ABI or runtime
   contract.
2. Validate the generic fallback on real s390x hardware.
3. Remove any now-dead helper ABI entries and documentation references.
4. Re-run the focused family plus one broader comparison row.

Leave the hardest mechanisms for last:

- record-time foreign-call substitution,
- callback ABI special handling,
- contracts that depend on exact exit or resume ownership.

## What Good Looks Like

The s390x port stays upstream-ready when:

- correctness no longer depends on benchmark-shaped reducers,
- perf-family tests use interpreter-grounded oracles,
- matrix definitions and reports are consistent,
- docs explain the current state without private infrastructure.

## Related Docs

- [state-of-project.md](state-of-project.md)
- [runbook.md](runbook.md)
- [perf.md](perf.md)
- [findings.md](findings.md)
