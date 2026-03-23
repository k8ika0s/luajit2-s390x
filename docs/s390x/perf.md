# s390x Performance Validation

## Purpose

This stage turns the s390x bring-up harness into a reproducible native
performance lab. The goal is to measure native IBM Z behavior only after the
matching correctness surface is already green.

The performance loop is split into two tiers:

- Tier 1:
  - build and compiler tuning
  - helper-friendly codegen cleanups
  - low-risk improvements that do not change JIT semantics
- Tier 2:
  - hotspot-driven s390x-specific optimization only after the workload family
    is already correctness-green and measured as a top bottleneck

## Workload Families

The repo-local structured benchmarks live in
[tests/s390x/perf](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf):

The current default `perf_bench` lane is intentionally narrower than the full
catalog below. Right now it runs only `dispatch_trace.lua`, while the
remaining microbenchmarks stay in-tree as follow-up probes for known
release-mode crash or wrong-result shapes.

- `dispatch_trace.lua`
  - simple numeric trace
  - side-exit-heavy loop
  - hotexit-heavy loop
  - current `%` boundary:
    - root and simple side-exit modulo traces are green enough for Stream B
      optimization work
    - the aggressive modulo hotexit/stitch stress shape is tracked separately
      as `tests/s390x/jit_loops/mod_hotexit_stress.lua` and remains a Stream A
      closure item until it is native-release green
    - current native `kdz` threshold split:
      - aggressive `hotloop=2`, `hotexit=2` is still wrong (`6490 -> 6928`)
      - perf-default `hotloop=10`, `hotexit=10` is correct
    - the current reduced closure repro shows the smaller failing family:
      - `% 5` hotexit guard + `% 97` payload
      - current `kdz` result: `4104 -> 4119`
- `bitops_mix.lua`
  - `bit.*`
  - `tobit`
  - mixed overflow-sensitive integer paths
  - currently kept in-tree as a focused follow-up benchmark and temporarily
    excluded from the default `perf_bench` lane until the native s390x
    release-mode crash on this shape is resolved
- `vararg_paths.lua`
  - dynamic `select(i, ...)`
  - vararg reduction
  - return-split path
- `iterator_table.lua`
  - `pairs()`
  - explicit `next()`
  - custom Lua iterator
  - table build and update loop
- `ffi_calls.lua`
  - direct traced `ffi.C.*`
  - stored function-value FFI call
- `ffi_cdata.lua`
  - cdata field load/store loop
  - mixed-width FFI field path
- `be_helpers.lua`
  - number helpers
  - BE-sensitive pack and unpack path
- `mixed_noffi.lua`
  - JIT-heavy mixed workload without FFI
- `mixed_ffi.lua`
  - mixed Lua + FFI workload

Every benchmark validates its final result before and after the measured
section. A fast wrong answer is a failed benchmark, not a performance win.

## Metrics

Each benchmark emits structured JSON records with:

- commit
- host
- compiler
- mode
- jit
- ffi
- build style
- tuning
- family
- workload
- scale
- iterations
- warmup iterations
- median runtime
- p95 runtime
- raw samples
- optional `perf stat` counters for the highest-value release workloads

The primary local artifacts are:

- `artifacts/s390x/<run-id>/perf/benchmarks.json`
- `artifacts/s390x/<run-id>/perf/comparisons.json`
- `artifacts/s390x/<run-id>/perf/family-status.json`
- `artifacts/s390x/<run-id>/perf/hotspots.json`
- `artifacts/s390x/<run-id>/perf/perf-summary.md`

Raw per-benchmark stdout, stderr, and optional `perf stat` outputs stay under
the normal step artifact tree in `remote/steps/perf_bench/...`.

## Closure Gate Rule

Performance is still non-blocking for the branch-level `s390x` support claim,
except where a perf benchmark exposes a correctness problem.

Current closure-stage rule:

- required perf regression check:
  - `dispatch_trace.lua`
  - host `kdz`
  - compiler `gcc`
  - mode `release`
  - `jit=on`
  - tuning `baseline`

The rest of the perf catalog stays in-tree as follow-up probes until each file
is release-stable on native `s390x`.

## Current Stamped Baseline

The first native performance baseline is now stamped for the release-stable
dispatch family on `kdz`.

Authoritative structured runs:

- JIT-on baseline and z13:
  [artifacts/s390x/20260322T145212.814679Z-p29811](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/20260322T145212.814679Z-p29811)
  - summary:
    [summary.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/20260322T145212.814679Z-p29811/summary.md)
- JIT-off baseline and z13:
  [artifacts/s390x/20260322T145555.369124Z-p31961](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/20260322T145555.369124Z-p31961)
  - summary:
    [summary.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/20260322T145555.369124Z-p31961/summary.md)
- refreshed dispatch-only restamp:
  [artifacts/s390x/perf-kdz-20260323a](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/perf-kdz-20260323a)
  - summary:
    [summary.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/perf-kdz-20260323a/summary.md)
  - perf summary:
    [perf-summary.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/perf-kdz-20260323a/perf/perf-summary.md)
  - family status:
    [family-status.json](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/perf-kdz-20260323a/perf/family-status.json)
  - hotspots:
    [hotspots.json](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/perf-kdz-20260323a/perf/hotspots.json)

Representative median runtimes on `kdz`, `gcc release`, `ffi=on`, `mixed`:

- JIT on, baseline:
  - `numeric_loop/hot`: `0.044697s`
  - `side_exit_loop/hot`: `0.032459s`
  - `hotexit_loop/hot`: `0.011691s`
- JIT on, z13:
  - `numeric_loop/hot`: `0.043573s`
  - `side_exit_loop/hot`: `0.027554s`
  - `hotexit_loop/hot`: `0.011168s`
- JIT off, baseline:
  - `numeric_loop/hot`: `0.002061s`
  - `side_exit_loop/hot`: `0.003735s`
  - `hotexit_loop/hot`: `0.005610s`

Headline ratios from those runs:

- `z13` vs baseline, `jit=on`, `side_exit_loop/hot`: about `1.18x` faster
- `z13` vs baseline, `jit=on`, `numeric_loop/hot`: about `1.03x` faster
- `jit=on` vs `jit=off`, baseline, `side_exit_loop/hot`: about `8.69x` slower
- `jit=on` vs `jit=off`, baseline, `numeric_loop/hot`: about `21.69x` slower

Current conclusion:

- the perf harness and native artifact model are working end to end
- the latest closure soak/runtime remediation is correctness-only and keeps the
  branch on track for performance work; it does not change the first measured
  optimization priority
- z13 tuning already helps the side-exit-heavy dispatch shape materially
- the dispatch and side-exit family is currently a real optimization hotspot,
  because the present s390x JIT-on path is slower than JIT-off on this family
- `family-status.json` is now the machine-readable promotion queue:
  - `dispatch_trace`: default perf gate
  - `iterator_table`: first promotion candidate
  - remaining families: probe-only until release-stable
- `hotspots.json` is now the machine-readable hotspot backlog for Stream B
- the `%` queue is now intentionally split:
  - Stream B optimization entry point:
    - `tests/s390x/jit_core/mod_int_trace.lua`
  - Stream A closure blocker:
    - `tests/s390x/jit_loops/mod_hotexit_stress.lua`

That is a useful result, not a benchmark failure. It identifies the first
measured Tier 1/Tier 2 optimization target.

## Baselines and Comparison Rules

Primary s390x baseline:

- host: `kdz`
- compiler: `gcc`
- mode: `release`
- jit: `on`
- ffi: `on`
- tuning: `baseline`

Required internal comparisons:

- `jit=on` vs `jit=off`
- `baseline` vs `z13`
- `gcc release` vs `clang release`

Cross-arch control:

- one local workstation control build from the same commit
- informative only
- never a correctness or release gate
- best-effort only; missing local control data must not invalidate native s390x
  perf artifacts
- the local macOS control path now exports `MACOSX_DEPLOYMENT_TARGET`
  automatically; the next perf restamp should confirm whether this removes the
  current local-control skip

Comparison rules:

- absolute timings are meaningful only within a single host
- cross-host and cross-arch results are recorded as normalized ratios
- the headline metric is median runtime
- p95 is the stability metric

## Measurement Discipline

- one warmup pass before measurement
- five measured repetitions by default
- perf benchmarks use moderate JIT thresholds by default:
  - `hotloop=10`
  - `hotexit=10`
- correctness checks before and after the timed region
- one benchmark process per measurement command
- `perf stat` on native s390x release for the top benchmark families:
  - cycles
  - instructions
  - branches
  - branch-misses
  - cache-references
  - cache-misses

Missing or malformed benchmark metric output is treated as a perf-stage
failure.

The original perf helper inherited the bring-up stress settings
`hotloop=1` / `hotexit=2`. That was corrected before treating the dispatch
family as a real optimization target. A direct native rerun on `kdz` with the
moderate thresholds matched the earlier dispatch medians within noise, so the
current dispatch slowdown is considered real enough to guide optimization.

## Execution Order

Run the perf stage in this order:

1. `gcc release`, `jit=on`, `ffi=on`, `baseline` on `kdz`
2. same build with `jit=off`
3. `gcc release`, `jit=on`, `ffi=on`, `z13` on `kdz`
4. `clang release`, `jit=on`, `ffi=on`, `baseline` on `kdz`
5. second-host spot perf on `zkd0` for the top families
6. cross-arch local control for the top families

## Acceptance

A benchmark family is considered fully validated when:

- the measured build passes the matching correctness gate
- median and p95 are stable across repeated runs
- both `jit=off` and `jit=on` measurements exist
- at least one tuned-vs-baseline comparison exists on s390x
- any top regression has an attributed cause or follow-up

A performance optimization iteration is only complete when:

- the targeted workload improves on the primary `kdz` release baseline
- the matching correctness suite still passes
- one mixed soak workload still passes
- `perf-summary.md` and `comparisons.json` are restamped
