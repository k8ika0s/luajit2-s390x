## 2026-04-01 kdz canon+share family check

- Host: `kdz`
- Candidate policy:
  - `LUAJIT_S390X_HOTSIDE_CANON_EQUIV=1`
  - `LUAJIT_S390X_HOTSIDE_SHARE_EQUIV=1`

### logical_chain_tail_add

- Baseline:
  - `hot 0.008741`
  - `TRACE_START 41`
  - `TRACE_STOP 41`
  - `TRACE_ABORT 0`
  - `TEXIT_COUNT 7981`
- Candidate:
  - `hot 0.002683`
  - `TRACE_START 2`
  - `TRACE_STOP 2`
  - `TRACE_ABORT 0`
  - `TEXIT_COUNT 8000`

### bitops_mix

- Baseline:
  - `hot 0.008902`
  - `TRACE_START 41`
  - `TRACE_STOP 41`
  - `TRACE_ABORT 0`
  - `TEXIT_COUNT 7981`
- Candidate:
  - `hot 0.002968`
  - `TRACE_START 2`
  - `TRACE_STOP 2`
  - `TRACE_ABORT 0`
  - `TEXIT_COUNT 8000`

### Read

- The combined policy scales past the smallest reproducer.
- The win comes with dramatically smaller trace population, not with fewer aggregate exits.
- This is the first generic throughput hotside policy that survives beyond `int_add_phi_only`.
