# s390x State Of Project

This page is the current status summary for the s390x enablement work in this
repository.

## Scope

The remaining work is not just backend bring-up. Upstream-readiness depends on
three things staying aligned:

- runtime and JIT correctness on real s390x hardware,
- performance evidence that is reproducible and not benchmark-shaped, and
- documentation, tests, and tooling that do not depend on private lab context.

## Current Position

- The current branch has a full native validation and performance workflow that
  runs on real s390x hosts from checked-in matrix definitions.
- The current validated line has completed the full upstream validation and
  performance matrix with `0` failures on two real s390x hosts.
- The checked-in docs and reporting flow are being kept generic so the branch
  status can be reviewed without private lab context.
- Future changes are expected to preserve this state rather than reopen
  benchmark-shaped runtime shortcuts.

## Upstream-Readiness Guardrails

### Runtime and JIT

- Keep native matrix rows green on real s390x systems.
- Revalidate focused correctness lanes whenever callback ABI, exit handling,
  cdata lowering, or VM resume paths change.
- Prefer narrow mechanism fixes over benchmark-specific steering.

### Performance

- Keep benchmark oracles interpreter-grounded.
- Separate real regressions from harness artifacts.
- Treat any new s390x-only acceleration as debt unless it is backed by a
  generic contract.

### Tests and Tooling

- Reduce private assumptions in matrix and reporting workflows.
- Keep test expectations independent from JIT-tainted setup code.
- Make the repository's checked-in docs and scripts understandable from a clean
  checkout.

## Reading Map

- [runbook.md](runbook.md): generic native validation workflow
- [perf.md](perf.md): performance and oracle policy
- [upstream-cleanup.md](upstream-cleanup.md): current blocker categories and
  retirement order
- [findings.md](findings.md): condensed engineering themes behind recent fixes

## What This Page Is Not

This page is not an append-only recovery log, artifact ledger, or host-by-host
status board. Those details are intentionally kept out of the public-facing
status summary.
