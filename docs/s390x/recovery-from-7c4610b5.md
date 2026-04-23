# Recovery From `7c4610b5`

This file defines the canonical recovery path after the April 21 stable anchor.

## Authoritative Source Point

- Trusted baseline: `7c4610b5` (`2026-04-21 09:37 PDT`)
- Last trusted project-status document: [state-of-project.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/state-of-project.md)
- Current branch head is not project truth until revalidated from that anchor.

## Commit Tranches To Audit

Reducer/helper cleanup tranche:
- `f0251248`
- `f21fd5e9`
- `c8342d50`
- `169c8bec`
- `00001578`
- `0efff0b1`
- `68a9aed7`
- `c5830fe2`
- `10e41460`
- `477410c1`
- `1ea6355a`
- `89515ce5`
- `2bbb0d42`
- `48305a0a`
- `3595d13e`
- `d10f41df`

Runtime/backend tranche:
- `4e0a3c1b`
- `a3a1b3f2`
- `4265b194`
- `7e862f75`

## Matrix Files

- Full validation/perf matrix:
  [tests/matrix/upstream_validation_perf_matrix.json](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/matrix/upstream_validation_perf_matrix.json)
- Canonical perf-only matrix:
  [tests/matrix/standard_perf_matrix.json](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/matrix/standard_perf_matrix.json)

The perf-only matrix defines the standard release profiles:
- `jit-off`
- `jit-on`
- `jit-accel-h1e10`
- `jit-accel-h1e1`

## Canonical Commands

Validation/correctness on a remote host:

```bash
python3 tools/upstream_matrix_runner.py run-target \
  --host kdz1 \
  --git-rev 7c4610b5 \
  --matrix tests/matrix/upstream_validation_perf_matrix.json \
  --target-label s390x-kdz1-baseline \
  --output-root artifacts/s390x/recovery-baseline-$(date -u +%Y%m%dT%H%M%SZ)
```

Standard performance run on a remote host:

```bash
python3 tools/upstream_matrix_runner.py run-target \
  --host kdz1 \
  --git-rev 7c4610b5 \
  --matrix tests/matrix/standard_perf_matrix.json \
  --target-label s390x-kdz1-standard-perf \
  --output-root artifacts/s390x/standard-perf-$(date -u +%Y%m%dT%H%M%SZ)
```

Single-target profile comparison from one or more fetched perf artifacts:

```bash
python3 tools/s390x/compare_perf_artifacts.py \
  --profile-label "kdz1 s390x Standard Perf" \
  --profile-artifact artifacts/s390x/standard-perf-.../targets/s390x-kdz1-standard-perf \
  --output-dir artifacts/s390x/profile-compare-...
```

Cross-target comparison in the existing artifact format:

```bash
python3 tools/s390x/compare_perf_artifacts.py \
  --s390x-artifact artifacts/s390x/standard-perf-s390x/... \
  --x86-artifact artifacts/s390x/standard-perf-x86/... \
  --output-dir artifacts/s390x/compare-...
```

## Acceptance Rules

- No post-`7c4610b5` change survives without fetched `kdz1`/`kdz` evidence.
- Validation coverage must never be deleted to make performance runs easier.
- Performance comparison output is authoritative only when emitted as:
  - `combined-comparison.csv`
  - `family-rollup.csv`
  - `summary.json`
  - `combined-comparison.md`
- Ad hoc markdown summaries are not replacements for the canonical report set.
