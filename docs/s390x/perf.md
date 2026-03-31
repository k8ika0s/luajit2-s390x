# s390x Performance Status

Last updated: 2026-03-31 08:25:08 PDT

## Scope

This page tracks the current native s390x performance state after the branch
was frozen into three lanes:

- Lane A: build and stability only
- Lane B: promotable recorder-side iterator perf only
- Lane C: parked bridge and continuation research only

The current performance frontier is Lane B only. The bridge and continuation
line is parked unless the clean no-probe iterator baseline regresses
semantically again.

## Authoritative Validation Surfaces

- Primary perf host:
  - `kdz:/root/luajit2-s390x/perf-clean-20260330/repo`
- Regression screen host:
  - `zkd0:/root/luajit2-s390x/perf-clean-20260330/repo`

Validation rules:

- tracked-file sync only
- direct `src/` rebuild only
- same-host pinned `kdz` A/B is the policy signal
- `zkd0` is regression-only
- low-noise manual logs or debugger only
- no dirty-tree `iterator_probe.py` runs for perf decisions

## Frozen Iterator Baseline

The current promotable iterator perf baseline is the four-piece recorder split
in [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c):

- array visible numeric key comes from successor index:
  - `HIOP(trvk) - 1`
- non-array visible key stays lazy
- hash table live-in is trusted and read-only in `rec_isnext()` / `rec_itern()`
- non-array value lane is seeded from `ix.val` so the hash root trace no longer
  frame-source the visible value slot

This is a split policy, not a full-lazy collapse.

## Current Pinned Baseline

Pinned `kdz` baseline on the frozen four-piece split:

- `pairs_sum/hot median=0.056362`
- `pairs_array_sum/hot median=0.061370`

`zkd0` regression screen on the same baseline:

- `HASH_VALUE 3000`
- `HASH_KEY 1320`
- `ARRAY_VALUE 3000`
- `pairs_sum/hot median=0.098189`
- `pairs_array_sum/hot median=0.097454`

These are the numbers new iterator perf work must beat.

## What The Current Baseline Proved

- The branch is no longer blocked on the old late crash in dispatch helper
  errno handling.
- Array and hash do not pay the same owners.
- Array-side post-call numeric key-lane waste was reduced by deriving the
  visible numeric key from the successor index instead of rereading the helper
  tuple key lane.
- Hash-side eager visible-key and table-slot costs were both removed.
- Hash root traces no longer frame-source the visible value lane.
  - helper `VLOAD #0` now feeds the hash add path directly
  - the old extra frame value `SLOAD` is gone

## Current Owner Map

Low-noise manual logging on the frozen baseline shows:

- Value-only hash:
  - dominant shared payer is still `addov_rr_int_eq`
  - main non-value cluster is still the hidden `KEYINDEX` load
  - only other frame `SLOAD` is the carried total slot
- Key-using hash:
  - still pays shared `addov_rr_int_eq`
  - still pays the hidden `KEYINDEX` load
  - adds a visible key/type `SLOAD`
- Array value-only control:
  - still pays shared `addov_rr_int_eq`
  - still pays numeric-key control loads

Current read:

- shared `addov_rr_int_eq` is now the dominant cross-family payer
- hash still carries the hidden `KEYINDEX` load cluster
- array still carries numeric-key control loads

## What Is Rejected

These are not active perf candidates anymore:

- full-lazy collapse
- any `KEYINDEX` no-guard path
- direct `KEYINDEX` tag-word compare
- backend dedup of `KEYINDEX` guard generation
- hidden-control carry through the unused visible-key slot
- body-scan loopback overrides as landing policy
- `TRACE 2` churn elimination as a perf proxy
- bridge-local producer and consumer fusion
- accumulator-to-`num` cuts that still keep the loop-unroll `int.num` check
- backend `AR/SR` overflow rewrites
- backend `AGFR/CGFR` equality-guard rewrites

The common failure modes were:

- semantic breakage
- cross-host regression
- same-host pinned `kdz` regression
- or real structural change with no promotable hot-loop win

## One Remaining Perf Gate

Only one accumulator-family pass is still worth consideration, and only after
restamping from the frozen baseline:

- instrument iterator roots around
  [src/lj_opt_loop.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_opt_loop.c)
- prove whether the original carried total is still born as `int` before
  `loop_unroll()` sees it
- only continue if a new cut can make the original carried slot `num` before
  loop unroll, so the back-edge `IR_CONV int.num check` never materializes

Immediate stop rule:

- if the back-edge `int.num` check survives, reject that whole family again
- if the check disappears but pinned `kdz` does not beat this frozen baseline,
  reject it and stop

## Benchmark And Logging Commands

From the clean remote repo:

```sh
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
make -C src clean && make -C src -j4
taskset -c 0 ./src/luajit tests/s390x/perf/iterator_table.lua
```

Focused low-noise owner mapping:

```sh
LUAJIT_S390X_ADD_LOG=1 LUAJIT_S390X_SLOAD_LOG=1 ./src/luajit /tmp/hash_value.lua
LUAJIT_S390X_ADD_LOG=1 LUAJIT_S390X_SLOAD_LOG=1 ./src/luajit /tmp/hash_key.lua
LUAJIT_S390X_ADD_LOG=1 LUAJIT_S390X_SLOAD_LOG=1 ./src/luajit /tmp/array_value.lua
```

## Relationship To Other Docs

- High-level status:
  [state-of-project.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/state-of-project.md)
- Detailed findings and reject pile:
  [findings.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/findings.md)
- Validation discipline:
  [runbook.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/runbook.md)
