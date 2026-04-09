# s390x Documentation Index

This directory now has one current-status page, one append-only notebook, and
one performance scoreboard.

The branch should still be treated as having a frozen implementation baseline:

- Lane A is the shipping build and stability floor.
- Lane B is the shipping recorder-side iterator baseline.
- Lane C is parked research and should not leak back into perf work.

## Read This First

- Current project state:
  [state-of-project.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/state-of-project.md)
  - plain-language status
  - latest proven state only
  - current next steps and timeline
- Primary technical notebook:
  [findings.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/findings.md)
  - append-only historical record
  - raw findings, rejects, corrections, and validation notes
- Canonical performance scoreboard:
  [perf.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/perf.md)
  - recurring benchmark table first
  - historical run log below
  - current perf gate and retained baselines
- Validation and sync workflow:
  [runbook.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/runbook.md)
  - canonical local and remote layout
  - authoritative mirror sync entrypoint
  - rebuild and run-output discipline
  - deterministic scale-order perf policy

## Current Reading Order

1. Read
   [state-of-project.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/state-of-project.md)
   for the latest branch state in common language.
2. Read
   [perf.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/perf.md)
   for the recurring benchmark scoreboard and current perf gate.
3. Read
   [runbook.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/runbook.md)
   before syncing, rebuilding, or running anything on `kdz` or `zkd0`.
4. Use
   [findings.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/findings.md)
   when you need the detailed experiment record or the reject pile.

## Focused Technical References

- Signed-int compare repair:
  [gc64-sload-int-repair.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/gc64-sload-int-repair.md)
- Shipping `promotion_core` throughput policy:
  [hotside-uget-looproot-promotion.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/hotside-uget-looproot-promotion.md)
- Static-stop FFI correction:
  [ffi-static-stop-retf-rebase.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/ffi-static-stop-retf-rebase.md)
- Non-`UGET` canon/share reject history:
  [non-uget-canonshare-policy.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/non-uget-canonshare-policy.md)

## Current Freeze Point

- Lane A: build and stability only
  - errno preservation in
    [src/lj_dispatch.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_dispatch.h)
  - `IRSLOAD_KIDX_NUMKEY` in
    [src/lj_ir.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_ir.h)
  - JIT-enabled-by-default s390x clean rebuilds in
    [src/lj_arch.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_arch.h)
- Lane B: promotable iterator perf only
  - retained mixed repair bundle in
    [src/lj_snap.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_snap.c),
    [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c),
    and
    [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc)
    including the retained root-2 hash-bridge floor for `mixed_noffi`
  - deterministic hot-first scale ordering in
    [tests/s390x/perf/benchlib.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/benchlib.lua)
    for carried perf suites
  - active runtime-handoff classification in
    [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c)
  - focused reducer guardrail in
    [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c)
    and
    [src/lj_ffrecord.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_ffrecord.c)
- Lane C: parked bridge and continuation research only

## Documentation Maintenance Rule

- `state-of-project.md` is current-state only and should be updated in place.
- `findings.md` stays append-only and remains the primary technical notebook.
- `perf.md` keeps the canonical recurring benchmark table at the top and the
  chronological log below it.
- `runbook.md` is the authoritative sync, layout, and validation contract.
