## 2026-04-01 zkd0 canon+share check

- Host: `zkd0`
- Candidate policy:
  - `LUAJIT_S390X_HOTSIDE_CANON_EQUIV=1`
  - `LUAJIT_S390X_HOTSIDE_SHARE_EQUIV=1`

### Hot medians

- `logical_chain_tail_add`
  - baseline `0.018642`
  - candidate `0.005414`
- `bitops_mix`
  - baseline `0.009459`
  - candidate `0.004000`

### Structural counts after resync + rebuild

- `logical_chain_tail_add`
  - baseline: `TRACE_START 41`, `TEXIT_COUNT 7981`
  - candidate: `TRACE_START 2`, `TEXIT_COUNT 8000`
- `bitops_mix`
  - baseline: `TRACE_START 41`, `TEXIT_COUNT 7981`
  - candidate: `TRACE_START 2`, `TEXIT_COUNT 8000`

### Notes

- The first trace runs returned `REMOTE_RC=1` only because the clean repo still had an older `tests/s390x/helpers/testlib.lua` without `trace_counter_capture_lite()`.
- After tracked-file resync and rebuild, the structural read matched `kdz`.
- This candidate does not look z15-only on the first `z14` screen.
