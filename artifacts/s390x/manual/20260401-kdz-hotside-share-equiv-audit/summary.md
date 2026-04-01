## 2026-04-01 kdz hotside share-equivalence audit

- Host: `kdz`
- Surface: `int_add_phi_only`
- Goal: explain why `LUAJIT_S390X_HOTSIDE_SHARE_EQUIV=1` lowers exits but raises trace starts

### Focused parent=24 exit=0 proof

- Baseline:
  - `TRACE_START 21`
  - `TEXIT_COUNT 4001`
  - focused logs show the late equivalent-child seam counting up normally
- `SHARE_EQUIV`:
  - `TRACE_START 100`
  - `TEXIT_COUNT 300`
  - focused logs show:
    - `phase=share-done parent=24 exit=0 cand=5 ... target=199`
    - immediately followed by `phase=start ... snapcount=200`

### Trace population proof

- Baseline trace snapshot:
  - `TRACEINFO_COUNT 27`
  - `TRACEINFO_LAST 27 0 stitch`
- `SHARE_EQUIV` trace snapshot:
  - `TRACEINFO_COUNT 106`
  - `TRACEINFO_LAST 106 0 stitch`

### Canon/share combinations on the smallest reproducer

- Baseline:
  - `hot 0.000786`
  - `TRACE_START 21`
  - `TEXIT_COUNT 4001`
- `SHARE_EQUIV`:
  - `hot 0.000659`
  - `TRACE_START 100`
  - `TEXIT_COUNT 300`
- `CANON_EQUIV + SHARE_EQUIV`:
  - `hot 0.000341`
  - `TRACE_START 3`
  - `TEXIT_COUNT 4001`
  - `TRACEINFO_COUNT 9`
- `CANON_CHILD + SHARE_EQUIV`:
  - `hot 0.000407`
  - `TRACE_START 4`
  - `TEXIT_COUNT 4001`
  - `TRACEINFO_COUNT 10`

### Read

- `SHARE_EQUIV` alone improves this family by accelerating new trace formation.
- `CANON_EQUIV + SHARE_EQUIV` wins differently: it collapses actual trace growth while preserving the throughput gain.
- On this family, aggregate `TEXIT_COUNT` is not sufficient to classify the best policy. Trace-population control can dominate even when exit totals stay flat.
