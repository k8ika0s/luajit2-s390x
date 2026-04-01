## 2026-04-01 kdz canon+share mechanism

- Host: `kdz`
- Surface: `int_add_phi_only`
- Policy:
  - `LUAJIT_S390X_HOTSIDE_CANON_EQUIV=1`
  - `LUAJIT_S390X_HOTSIDE_SHARE_EQUIV=1`

### Existing late-parent proof from `SHARE_EQUIV` alone

- Focused late seam from the earlier audit:
  - `phase=equiv parent=24 exit=0 cand=6 child=7`
  - `phase=share-done parent=24 exit=0 cand=5 ... target=199`
  - immediately followed by `phase=start parent=24 exit=0 ... snapcount=200`
- That is the old loop-clone flurry:
  - the hotcount walks up to a later equivalent parent
  - `share` primes that late parent to `hotexit - 1`
  - and the same late parent immediately starts another trace

### Combined canon/share read

- Warmed `-jv` focused run:
  - trace lines:
    - `1` root loop
    - `2` root handoff
    - `3..7` early self-loop descendants
    - `8` caller/root return into the early family
  - focused hotside logs during the measured run:
    - `parent=24` hits: `0`
    - `parent=4` hits: `202`
    - final focused start:
      - `phase=start parent=4 exit=0 ... snapcount=200`
- Warmed reduced probe with `FOCUS_PARENT=24`:
  - artifact:
    [combo_focus24.stdout.log](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-mechanism/raw/combo_focus24.stdout.log)
  - `TRACE_START 1`
  - `TRACE_STOP 1`
  - `TRACE_ABORT 0`
  - `TEXIT_COUNT 4021`
  - focused log lines: `0`

### Read

- The exit flurry is not many different hot exits.
- It is one repeated `exit 0` self-loop seam whose hotcount was previously
  walking up an equivalent-parent clone ladder.
- `SHARE_EQUIV` alone still reaches those later parents and primes them into
  immediate new trace starts.
- Under the combined policy, the measured run no longer reaches the late
  `parent=24` seam at all; it keeps returning to the early `parent=4` seam.
- Given the `trace_hotside()` order in
  [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c),
  the most direct reading is:
  - `canon` rewrites the parent to an earlier equivalent trace first
  - then `share` operates on that earlier canonical trace instead of on a late
    clone parent
- That explains the performance result:
  - aggregate exit totals stay flat because the loop still exits once per trip
  - trace population collapses because those exits stop creating fresh later
    equivalent parents
