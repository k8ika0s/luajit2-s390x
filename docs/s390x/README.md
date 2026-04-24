# s390x Documentation

This directory contains the public-facing documentation for the s390x porting
and validation work in this repository.

The docs here are intentionally concise. They are meant to describe the current
project state, the generic validation workflow, and the remaining upstream
cleanup work without depending on private hosts, local filesystem layouts, or
branch-lab history.

## Reading Order

1. [state-of-project.md](state-of-project.md)
   Current status, scope, and remaining blockers.
2. [runbook.md](runbook.md)
   Generic native validation workflow and output discipline.
3. [perf.md](perf.md)
   Performance policy, benchmark-oracle rules, and reporting guidance.
4. [upstream-cleanup.md](upstream-cleanup.md)
   Current cleanup priorities for upstream-readiness.

## Technical Notes

- [findings.md](findings.md)
  Condensed engineering themes and recurring failure classes.
- [gc64-sload-int-repair.md](gc64-sload-int-repair.md)
  Note on the GC64 integer `SLOAD` repair boundary.
- [recovery-from-7c4610b5.md](recovery-from-7c4610b5.md)
  Short historical note about a past recovery checkpoint.

## Documentation Policy

- Keep links repo-relative.
- Keep hostnames, private mount points, and personal worktree paths out of the
  docs.
- Put current status in [state-of-project.md](state-of-project.md), not in
  append-only logs.
- Put stable workflow in [runbook.md](runbook.md), not in one-off recovery
  notes.
