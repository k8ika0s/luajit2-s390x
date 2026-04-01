## 2026-04-01 zkd0 canon+share single-gate check

- Host: `zkd0`
- Gate: `LUAJIT_S390X_HOTSIDE_CANON_SHARE_EQUIV=1`
- Goal: regression screen for the dedicated canon/share gate on the z14 host

### Results

- `logical_chain_tail_add`
  - hot median: `0.003333`
  - `TRACE_START 2`
  - `TRACE_STOP 2`
  - `TRACE_ABORT 0`
  - `TEXIT_COUNT 8000`
  - check: `CHAIN_TAIL_ADD 281636956`
- `bitops_mix`
  - hot median: `0.004170`
  - `TRACE_START 2`
  - `TRACE_STOP 2`
  - `TRACE_ABORT 0`
  - `TEXIT_COUNT 8000`
  - check: `MIX_BITS 873075307`

### Read

- The dedicated gate keeps the same `2`-trace structural shape on `zkd0`.
- There is no new abort or timeout surface.
- The z14 screen stays materially better than the old baseline and remains
  aligned with the earlier dual-env candidate.

