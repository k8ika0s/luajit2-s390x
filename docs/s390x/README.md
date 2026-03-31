# s390x Documentation Index

This directory now has one canonical current-status page and three supporting
technical references.

The branch should now be treated as having a frozen implementation baseline:

- Lane A is the shipping build and stability floor
- Lane B is the shipping recorder-side iterator baseline
- Lane C is parked research and should not leak back into perf work

## Read This First

- Current project state:
  [state-of-project.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/state-of-project.md)
  - plain-language status
  - latest proven state only
  - current next steps and timeline
- Technical findings notebook:
  [findings.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/findings.md)
  - append-only lab notebook
  - raw findings, rejects, and validation notes
- Current perf status:
  [perf.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/perf.md)
  - frozen iterator baseline
  - current owner map
  - current perf gate
- Current validation workflow:
  [runbook.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/runbook.md)
  - authoritative worktrees
  - rebuild and restamp rules
  - checked-in helper:
    [tools/s390x/restamp_iterator_perf.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/restamp_iterator_perf.py)
  - low-noise validation discipline

## Current Reading Order

1. Read
   [state-of-project.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/state-of-project.md)
   for the latest branch state in common language.
2. Read
   [perf.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/perf.md)
   for the current iterator performance baseline and remaining gate.
3. Read
   [runbook.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/runbook.md)
   before running new host validation.
4. Use
   [findings.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/findings.md)
   when you need the detailed experimental record or the reject pile.

## Current Freeze Point

- Lane A: build and stability only
  - errno preservation in
    [src/lj_dispatch.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_dispatch.h)
  - `IRSLOAD_KIDX_NUMKEY` in
    [src/lj_ir.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_ir.h)
  - JIT-enabled-by-default s390x clean rebuilds in
    [src/lj_arch.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_arch.h)
- Lane B: promotable recorder-side iterator perf only
  - four-piece split in
    [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c)
- Lane C: parked bridge and continuation research only

## Current Default

The default from here is to ship the frozen Lane A plus Lane B stack unless a
genuinely new root-trace storage/control materialization target appears.

Any future perf idea must clear three gates before code starts:

1. name the remaining payer
2. define the structural proof target
3. explain why the idea is not already in the reject pile

## Documentation Maintenance Rule

- `state-of-project.md` should be updated in place and keep only the latest
  state.
- `findings.md` stays append-only and keeps the historical experiment record.
