# s390x Performance Policy

This document describes how performance results for the s390x port should be
measured, interpreted, and reported.

It intentionally avoids machine-specific scoreboards. The current goal is to
keep the performance workflow upstream-ready: reproducible, comparable across
runs, and separated from correctness claims.

## Purpose

Use this document to decide:

- which benchmark results are safe to cite,
- how to generate non-tainted expected values,
- which performance families need focused restamps after a change, and
- what evidence is required before claiming a retained optimization is still
  justified.

## Core Rules

- Correctness comes first. A benchmark result is not meaningful if the oracle
  is JIT-tainted or the workload has no interpreter-grounded expected value.
- Performance claims must be reproducible from a clean native build.
- Compare only like-for-like configurations.
- Keep benchmark harness changes separate from backend or runtime changes where
  possible.

## Performance Acceptance Gate

- Do not commit benchmark-specific micro-optimizations or special cases just
  because one row, profile, or narrow family improves.
- Focused performance runs are diagnostic. Acceptance requires correctness
  validation plus evidence that a reusable mechanism or broader family-level
  path improved without hiding regressions.
- Put raw correctness and perf logs under the matching run directory in
  `artifacts/s390x/correctness/`, `artifacts/s390x/perf/`, or
  `artifacts/s390x/investigate/`.
- Put retained comparison documents only under
  `artifacts/s390x/compare/<timestamp>-<target>-vs-ka0s01-full/combined-comparison.md`.
  Do not leave comparison docs in individual perf run directories.
- A change that cannot be explained beyond "this benchmark got faster" is not
  upstream-ready performance work.

## Oracle Rules

Expected values used by perf-family tests must not be derived from the same hot
JIT-compiled path under test.

Safe patterns:

- compute the expected value under `jit.off()`,
- use a closed-form mathematical result,
- use a small checked interpreter helper,
- validate against a stable external oracle when the workload is explicitly an
  FFI ABI test.

Unsafe patterns:

- computing `expected = hot_function(n)` at module load time,
- reusing a JIT-produced warmup value as the benchmark oracle,
- treating a previously generated artifact as authoritative when its own oracle
  path is uncertain.

## Scale And Profile Terms

- `small`, `medium`, `hot`, and any extra labels such as `xhot` are
  family-local workload buckets. They are not globally comparable sizes across
  different benchmark families.
- `jit-off` is the interpreter baseline for a row.
- `jit-on` is the default JIT-enabled row for the same compiler/build mode.
- `h1e10` means `hotloop=1 hotexit=10`.
- `h1e1` means `hotloop=1 hotexit=1`.

Any retained comparison doc should define those terms explicitly so the report
can be reviewed without out-of-band lab knowledge.

## Required Reporting Fields

Each retained performance report should make these details easy to recover:

- benchmark family or matrix row,
- build mode and compiler,
- whether JIT was enabled,
- warmup and sample policy,
- scale-label meaning and any non-default JIT hotloop/hotexit profile,
- native architecture and target environment,
- output directory containing raw logs.

The report itself should stay compact. Long chronological journals belong in
artifacts, not in this document.

## Minimum Perf Families To Restamp

The exact set depends on the change, but these categories are the usual
high-signal lanes:

- dispatch and side-exit sensitive families,
- iterator-heavy families,
- callback and FFI ABI families,
- cdata width-conversion families,
- families previously accelerated by s390x-only reducers or helper ABIs.

If a change removes a reducer or helper, rerun the focused family that used it
and at least one broader comparison row to ensure the fallback path is both
correct and not catastrophically slower.

## How To Read A Regression

- First decide whether the result is a correctness issue, an oracle issue, or a
  real throughput regression.
- If only one focused family moves, inspect the owning mechanism before
  widening the search.
- If a broad set of unrelated families move together, suspect harness policy,
  code generation, or trace-admission changes before assuming a single helper
  regression.

## Relationship To Other Docs

- [runbook.md](runbook.md) defines the generic native validation flow.
- [state-of-project.md](state-of-project.md) records current status and major
  blockers.
- [upstream-cleanup.md](upstream-cleanup.md) tracks cleanup work needed before
  benchmark results can be treated as upstream-ready.
