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
catalog below. Right now it runs only:
- `dispatch_trace.lua`

The remaining microbenchmarks stay in-tree as follow-up probes for known
release-mode crash or wrong-result shapes.
- `dispatch_trace.lua`
  - simple numeric trace
  - side-exit-heavy loop
  - hotexit-heavy loop
  - current `%` boundary:
    - root and simple side-exit modulo traces are green enough for Stream B
      optimization work
    - the aggressive modulo hotexit/stitch stress shape is now correctness-green
      on both native hosts as `tests/s390x/jit_loops/mod_hotexit_stress.lua`
    - that closes the old Stream A blocker and leaves `%` as a pure Stream B
      optimization target again
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
  [artifacts/s390x/20260324T022735.280630Z-p89021](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/20260324T022735.280630Z-p89021)
  - summary:
    [summary.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/20260324T022735.280630Z-p89021/summary.md)
- JIT-off baseline and z13:
  [artifacts/s390x/20260324T023224.602906Z-p91889](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/20260324T023224.602906Z-p91889)
  - summary:
    [summary.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/20260324T023224.602906Z-p91889/summary.md)
- refreshed dispatch-only restamp:
  [artifacts/s390x/20260324T022735.280630Z-p89021](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/20260324T022735.280630Z-p89021)
  - summary:
    [summary.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/20260324T022735.280630Z-p89021/summary.md)
  - perf summary:
    [perf-summary.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/20260324T022735.280630Z-p89021/perf/perf-summary.md)
  - family status:
    [family-status.json](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/20260324T022735.280630Z-p89021/perf/family-status.json)
  - hotspots:
    [hotspots.json](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/20260324T022735.280630Z-p89021/perf/hotspots.json)

Representative median runtimes on `kdz`, `gcc release`, `ffi=on`, `mixed`:

- JIT on, baseline:
  - `numeric_loop/hot`: `0.032174s`
  - `side_exit_loop/hot`: `0.017588s`
  - `hotexit_loop/hot`: `0.009095s`
- JIT on, z13:
  - `numeric_loop/hot`: `0.032158s`
  - `side_exit_loop/hot`: `0.017870s`
  - `hotexit_loop/hot`: `0.009307s`
- JIT off, baseline:
  - `numeric_loop/hot`: `0.002070s`
  - `side_exit_loop/hot`: `0.003668s`
  - `hotexit_loop/hot`: `0.005599s`

Headline ratios from those runs:

- `%` fast path vs pre-fast-path stamped baseline:
  - `numeric_loop/hot`: about `1.37x` faster
  - `side_exit_loop/hot`: about `1.57x` faster
  - `hotexit_loop/hot`: about `1.23x` faster
- `z13` vs baseline, `jit=on`, `numeric_loop/hot`: about `1.00x`
- `z13` vs baseline, `jit=on`, `side_exit_loop/hot`: about `0.98x`
- `jit=on` vs `jit=off`, baseline, `numeric_loop/hot`: about `15.54x` slower
- `jit=on` vs `jit=off`, baseline, `side_exit_loop/hot`: about `4.80x` slower
- `jit=on` vs `jit=off`, baseline, `hotexit_loop/hot`: about `1.62x` slower

Current conclusion:

- the perf harness and native artifact model are working end to end
- the latest closure soak/runtime remediation is correctness-only and keeps the
  branch on track for performance work; it does not change the first measured
  optimization priority
- the first real `%` optimization slice is now in the branch:
  - s390x lowers signed int modulo by positive constant divisors through
    native `dsgr` instead of always calling `lj_vm_modi`
  - the fast path is restamped correct on both hosts for:
    - `tests/s390x/jit_core/mod_int_trace.lua`
    - `tests/s390x/jit_loops/mod_hotexit_stress.lua`
    - `tests/s390x/jit_core/side_exit.lua`
    - `tests/s390x/soak/mixed_stress.lua`
  - `mod_int_trace.lua` now also covers negative dividends to keep the
    signed-remainder correction path pinned down
- the dispatch and side-exit family is currently a real optimization hotspot,
  because the present s390x JIT-on path is slower than JIT-off on this family
- the current perf story is materially better than the pre-fast-path baseline,
  but the branch is still leaving large gains on the table on `numeric_loop`
  and `side_exit_loop`
- `family-status.json` is now the machine-readable promotion queue:
  - `dispatch_trace`: default perf gate
  - `iterator_table`: first focused probe family
  - remaining families: probe-only until release-stable
- `hotspots.json` is now the machine-readable hotspot backlog for Stream B
- the `%` queue is now intentionally split:
  - Stream B optimization entry point:
    - `tests/s390x/jit_core/mod_int_trace.lua`
  - former Stream A closure blocker, now green:
    - `tests/s390x/jit_loops/mod_hotexit_stress.lua`
- `tests/s390x/perf/iterator_table.lua` remains the next focused probe family.
  The older structured `kdz` restamp `perf-iterator-kdz-20260323a` exposed the
  iterator cliff:
  - `jit=on baseline pairs_sum/hot`: `0.528441s`
  - `jit=on baseline pairs_array_sum/hot`: `0.401933s`
- The current clean native probe on `kdz`
  (`kdz:/root/luajit2-s390x/perf-wave-20260324b`) keeps `HEAD` plus only two
  local perf changes:
  - remove the `BC_IITERL` debug helper call from
    [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc)
  - stop forcing `hotloop=10,hotexit=10` in
    [tests/s390x/perf/benchlib.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/benchlib.lua)
- On that clean native probe, hot medians improved to:
  - `pairs_sum/hot`: `0.371870s`
  - `pairs_array_sum/hot`: `0.280803s`
- Relative to the older structured iterator restamp, that is:
  - `pairs_sum/hot`: about `1.42x` faster
  - `pairs_array_sum/hot`: about `1.43x` faster
- The same clean native probe kept the current `%`/side-exit/soak correctness
  slice green:
  - `tests/s390x/jit_core/mod_int_trace.lua`
  - `tests/s390x/jit_loops/mod_hotexit_stress.lua`
  - `tests/s390x/jit_core/side_exit.lua`
  - `tests/s390x/soak/mixed_stress.lua`
- A first `BC_ISNEXT` JLOOP-unpatch port on s390x built cleanly but did not
  materially change the iterator timings, so it is not part of the active
  patch set.
- The next coherent native `kdz` probe keeps that safe iterator patch set and
  adds one more s390x-only runtime tuning change:
  - default `JIT_P_hotexit = 200` in
    [src/lib_jit.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lib_jit.c)
- On `kdz:/root/luajit2-s390x/perf-wave-20260324b`, that reduces iterator hot
  medians further to:
  - `pairs_sum/hot`: `0.045205s`
  - `pairs_array_sum/hot`: `0.046697s`
- Relative to the older structured iterator restamp, that is:
  - `pairs_sum/hot`: about `11.69x` faster
  - `pairs_array_sum/hot`: about `8.61x` faster
- Repeated same-process `pairs()` timing on that coherent build is now:
  - `jit.on`: `0.003175`, `0.006357`, `0.010301`, `0.016793`, `0.024084`,
    `0.030375`
  - `jit.off`: about `0.0314`
- That means the current best iterator path on s390x is now at or below
  interpreter cost on the same workload, instead of catastrophically above it.
- The same coherent build kept the current correctness slice green:
  - `tests/s390x/jit_core/mod_int_trace.lua`
  - `tests/s390x/jit_loops/mod_hotexit_stress.lua`
  - `tests/s390x/jit_core/side_exit.lua`
  - `tests/s390x/soak/mixed_stress.lua`
- Dispatch remained near the current `%`-fast-path baseline on that same run:
  - `numeric_loop/hot`: `0.032363s`
  - `side_exit_loop/hot`: `0.017595s`
  - `hotexit_loop/hot`: `0.008959s`

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
