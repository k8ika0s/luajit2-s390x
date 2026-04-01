## 2026-04-01 kdz canon+share single-gate check

- Host: `kdz`
- Gate: `LUAJIT_S390X_HOTSIDE_CANON_SHARE_EQUIV=1`
- Goal: validate that one dedicated gate reproduces the already-proven
  `CANON_EQUIV + SHARE_EQUIV` throughput candidate

### Results

- `int_add_phi_only`
  - hot median: `0.000349`
  - `TRACE_START 3`
  - `TRACE_STOP 3`
  - `TRACE_ABORT 0`
  - `TEXIT_COUNT 4001`
  - check: `ADD_PHI_ONLY 414000`
- `logical_chain_tail_add`
  - hot median: `0.003015`
  - `TRACE_START 2`
  - `TRACE_STOP 2`
  - `TRACE_ABORT 0`
  - `TEXIT_COUNT 8000`
  - check: `CHAIN_TAIL_ADD 281636956`
- `bitops_mix`
  - hot median: `0.003064`
  - `TRACE_START 2`
  - `TRACE_STOP 2`
  - `TRACE_ABORT 0`
  - `TEXIT_COUNT 8000`
  - check: `MIX_BITS 873075307`

### Read

- The single gate preserves the same tiny-trace-set structural shape as the
  old dual-env pair.
- The repeated exit seam is unchanged:
  - `4001` exits on the smallest reproducer
  - `8000` exits on the larger siblings
- The promoted surface is still the same mechanism:
  - keep the run on the early canonical seam
  - stop hotcount migration up the later equivalent-parent ladder

