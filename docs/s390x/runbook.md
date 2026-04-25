# s390x Validation Runbook

This runbook describes the generic validation workflow for native s390x work in
this repository. It is deliberately host-agnostic: the same rules should apply
whether validation runs locally, on a shared machine, or in automation.

## Purpose

Use this document when you need to:

- rebuild the repository on a real s390x machine,
- run the native validation matrix,
- capture repeatable performance results, or
- restamp previously validated areas after a source change.

For the current project state, read
[state-of-project.md](state-of-project.md) first.

## Workspace Rules

- Build from a clean checkout or a clearly scoped worktree.
- Keep generated output outside tracked source directories.
- Treat native build directories as disposable mirrors of the repository, not
  as long-lived scratchpads.
- Do not mix local experiment debris with the tree used for validation claims.

## Upstream-Readiness Gate

- Do not accept benchmark-shaped backend, recorder, or helper changes whose
  primary justification is one row or one trace shape getting faster.
- Before coding a performance fix, name the reusable mechanism being improved
  and the family-level validation lane that can prove it.
- Treat targeted row wins as diagnostic evidence only. A commit needs either a
  correctness fix or a reusable architecture/runtime improvement.
- The safe rollback checkpoint before the rejected narrow tweaks is
  `1efd7dcc` (`Use NIHF for s390x TValue pointer untag`). Preserve that as the
  known pre-junk landing point unless a newer validated checkpoint supersedes
  it in this document.

## Sync And Build Discipline

- Sync tracked files only.
- Preserve repository-relative paths during any copy or mirror step.
- Rebuild from `src/` after sync instead of copying build products between
  machines.
- Keep validation commands and artifacts reproducible from a fresh checkout.

## Native Validation Flow

1. Sync the source tree to the native s390x environment.
2. Rebuild from `src/`.
3. Confirm the interpreter starts cleanly and JIT is available.
4. Run the targeted validation lane or matrix row.
5. Capture outputs in a timestamped results directory outside the tracked tree.
6. Record only the results needed to justify the current change.

## Minimum Correctness Expectations

Before using a run for support claims, keep these categories green on real
s390x hardware:

- baseline interpreter startup,
- core JIT validation,
- focused backend or runtime repros relevant to the current patch,
- any perf-family correctness oracles touched by the change.

If a change modifies helper ABI, exit handling, callback ABI, or cdata
lowering, rerun the focused lane that exercises that mechanism even if a
broader matrix row already passed.

## Performance Restamps

When restamping performance:

- compare like-for-like builds,
- use the same benchmark file for `jit.on` and `-joff` comparisons,
- keep warmup and sample policy fixed for a given report,
- capture raw logs alongside summarized tables,
- avoid mixing exploratory instrumentation into the numbers used for claims.

See [perf.md](perf.md) for benchmark and oracle policy.

## Output Discipline

- Store generated validation bundles under a dedicated results directory such as
  `artifacts/s390x/` or another ignored output root.
- Keep reports, raw logs, JSON summaries, and trace dumps together.
- Do not treat ad hoc terminal output as the canonical record of a validation
  run.
- Prefer compact summaries that point back to reproducible commands.

## What Not To Encode In Docs

- personal filesystem paths,
- private hostnames,
- one-off recovery trees,
- commands that only make sense in one lab environment,
- dated instructions whose only purpose was to recover a temporary branch
  state.
