# s390x Findings

This document is a condensed engineering notebook for recurring s390x bring-up
themes. It is not an append-only artifact journal.

The goal is to preserve the technical lessons that still matter while dropping
machine-local paths, one-off recovery stories, and stale branch-lab evidence.

## Recurring Failure Classes

### Exit and Resume Contracts

Several hard bugs in the bring-up cycle came from mismatches between recorded
trace assumptions and the VM state rebuilt at exits or resumes. The recurring
lesson is:

- root and side-trace resume contracts must be explicit,
- iterator and numeric-loop state must be reconstructed according to the owning
  bytecode family,
- dispatch shortcuts are only safe when the live slot contract is the same on
  both sides of the handoff.

### GC64 and Snapshot Restore

Another repeated seam was snapshot restoration on 64-bit big-endian systems.
When values were narrowed, widened, sunk, or replayed through snapshots, the
exact source word and width mattered. The durable lesson is to treat:

- big-endian subword restore,
- sunk object replay,
- cdata width reconstruction,
- `SLOAD` typing and rematerialization

as one connected correctness area rather than as isolated backend issues.

### Callback And FFI ABI Boundaries

Callback bugs were usually ABI-contract bugs, not Lua-level logic bugs. The
most important patterns were:

- preserve the platform's callee-saved and argument registers in trampolines,
- keep callback entry contracts minimal and explicit,
- validate both single-call and repeated-call cases,
- treat record-time foreign-call substitution as a semantic change that needs
  extra scrutiny.

### Reducer Debt

Many earlier performance wins came from recorder-side semantic reducers or
s390x-only helper ABIs. These can be useful during bring-up, but they become
upstream debt if they:

- recognize benchmark-shaped bytecode bodies,
- skip generic runtime or recorder mechanisms,
- require dedicated helper ABIs for one family,
- or depend on private validation stories to justify their existence.

The current policy is to remove or narrow them unless they can be defended as a
generic mechanism.

### Harness And Oracle Integrity

Several apparent backend regressions turned out to be test or benchmark
problems:

- expected values computed by hot JIT code,
- matrix rows whose ownership or scope was unclear,
- reports that treated one machine's local workflow as canonical.

That class of issue is now considered an upstream-readiness blocker in its own
right.

## Working Principles

- Prefer exact mechanism explanations over broad bring-up narratives.
- Keep current state in [state-of-project.md](state-of-project.md).
- Keep workflow in [runbook.md](runbook.md).
- Keep performance policy in [perf.md](perf.md).
- Use this page only for durable engineering lessons that still help explain
  the current codebase.
