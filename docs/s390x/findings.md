# s390x Bring-Up Findings

This document records concrete findings from the staged native bring-up runs.
It is intentionally focused on observed behavior, run IDs, and next actions.

For the current project state in plain language, use
[state-of-project.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/state-of-project.md).
This file is the append-only technical notebook. New entries should be
added at the end in chronological order.

## Harness Status

- The native bring-up harness is implemented under `tools/s390x/`.
- Existing repo Perl tests no longer depend on external CPAN modules.
- The local `t/TestLJ.pm` harness now:
  - parses `__DATA__` blocks without `Test::Base`,
  - captures subprocess stdout and stderr without `IPC::Run3`,
  - emits minimal TAP without `Test::More`,
  - uses absolute `LUA_PATH` and `LUA_CPATH`,
  - filters blocks via explicit `--- requires:` capability markers.
- The driver now records unexpected local Python exceptions and interrupts into
  run artifacts instead of failing without a traceback trail.
- The hardened tracked-files-only tar-over-ssh transport is now the
  authoritative structured sync path for native remote runs.
- The harness now has a final `closure` stage with two new suites:
  - `coverage_audit`
  - `downstream`
- `coverage_audit` runs locally and derives the closure inventory from source
  files, not memory.
- `downstream` runs the existing OpenResty and Kong native demos as standard
  driver-managed gates instead of leaving them as side-only scripts.

## Native Runs

- `iter-chain-handoff-root-itern-precall-tab-layout-fix-20260330g`
  - Stage: focused native iterator probe
  - Surface: `iter-tiny`
  - Host: `kdz`
  - Result: enabling fix
  - Notes: the first raw-`TValue` pre-call table preserve attempt added new
    scratch fields to `jit_State` in
    [src/lj_jit.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_jit.h).
    That immediately made the remote rebuild fail at `BUILDVM lj_vm.S` with
    `DASM error 11001e7f`.
  - This turned out not to be host skew. The remote tree matched the local
    source, and manual DynASM preprocessing still succeeded. The failure was
    later, inside `host/buildvm`, when encoding
    `la DISPATCH, GG_G2DISP(RB)` in
    [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc).
    Growing `jit_State` also grows `GG_G2DISP`, and on s390x that offset is
    still consumed through a 12-bit `la` displacement.
  - Fix: keep the raw-table preserve experiment, but do not grow `jit_State`.
    The scratch table `TValue` is now preserved through the existing
    `J->errinfo` slot, and the table slot is derived from the already-saved
    root key slot (`slot = rootslot-1`). The corresponding restore-time
    override in
    [src/lj_snap.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_snap.c)
    now copies from `errinfo` only when that exact root/bridge match holds.
  - Consequence:
    - the branch is buildable again on `kdz`
    - the raw-table preserve experiment can now be classified at runtime
      without reopening the VM offset/range issue

- `iter-chain-handoff-root-itern-precall-tab-iitern-entry-20260330g`
  - Stage: focused native iterator probe
  - Surface: `iter-tiny`
  - Host: `kdz`
  - Result: promising classifier
  - Notes: with the repaired raw-table preserve branch enabled:
    - `LUAJIT_S390X_ROOT_ITERN_SETUP_ITERN=1`
    - `LUAJIT_S390X_ROOT_RESUME_PRECALL_TAB=1`
    - `LUAJIT_S390X_LOOPDESC_BRIDGE_PRECALL_TAB=1`
    - plus the current safe continuation bundle
    the tiny reproducer no longer falls over immediately. It times out
    cleanly instead of reproducing the earlier `lj_vm_IITERN+24` segfault.
  - A focused `gdb` stop on `lj_vm_IITERN` now shows the resumed producer entry
    with the correct local `A` field:
    - `r4 = 9`
    - `r13 = 0x...2ce8`
    - `x/12gx $r13+32` includes:
      - `0xfffb83fff7fd4e80`
      - `0xfffa03fff7fd3c38`
      - `0xfffe7fff00000000`
      - followed by `nil`
  - This is materially different from the earlier reject. Previously the slot
    that should have held the iterator table arrived at `lj_vm_IITERN` as an
    int-like raw value (`0xfff90000ffff95b0`); now it is a tagged GC pointer
    again, and the control-var lane is also present.
  - Consequence:
    - the raw-table preserve is restoring the producer-side table lane
      correctly enough to reach `lj_vm_IITERN`
    - the live seam is now later than “wrong table lane at `IITERN` entry”
    - the next question is whether this branch now loops with a valid producer
      contract or still stalls before a real iterator-state update becomes
      visible

- `iter-chain-handoff-root-itern-precall-tab-iitern-hit2-20260330g`
  - Stage: focused native iterator probe
  - Surface: `iter-tiny`
  - Host: `kdz`
  - Result: classification only
  - Notes: the next exact question on the repaired raw-table branch was
    whether the producer body (`lj_vm_IITERN`) was still the hot tight replay
    loop. It is not.
  - A focused `gdb` run with `ignore 1 1` reaches the second `lj_vm_IITERN`
    hit and shows the same producer-side entry state as hit `1`:
    - `r4 = 9`
    - `r6 = 0x303`
    - `r11 = -14`
    - `BASE+0x20 .. +0x48` still holds:
      - two numeric lanes at `1`
      - two tagged GC pointers
      - a control/result bundle headed by `0xfffe7fff00000002`
      - followed by numeric `1`, numeric `1`, and `nil`
  - But the producer is no longer the tight hot loop. A matching `gdb` run
    with `ignore 1 49` did not reach hit `50` within a 40 second timeout.
  - Consequence:
    - the repaired branch does re-enter `lj_vm_IITERN`
    - but `lj_vm_IITERN` is no longer the dominant infinite replay surface
    - the hot stall has moved later or lower-frequency, so the next seam is
      not “bad table lane at producer entry” anymore

- `iter-chain-handoff-root-itern-precall-tab-parent3-loop-20260330g`
  - Stage: focused native iterator probe
  - Surface: `iter-tiny`
  - Host: `kdz`
  - Result: classification only
  - Notes: the next structural question was whether the repaired raw-table
    branch still reached the familiar post-`trace 4` bridge seam. It does not.
  - A focused `JLOOP_EXIT` run on `parent=3 exit=0` shows a stable earlier
    loop:
    - `trace=3`
    - `target=1`
    - `target_exec=2`
    - `target_startop=BC_ITERN` (`70`)
    - `exec_startop=BC_JMP` (`88`)
    - `exec_resumepc=0x...6b0`
    - `exec_resumeop=BC_ITERL` (`82`)
    - `retop=BC_LOOP` (`85`)
    - repeated `phase=resume-linked`
  - There are no `loopdesc-child-query`, `loopdesc-bridge-child-reenter`, or
    `trace 4` bridge logs on this branch under the same focused run. The
    repaired producer/table path has therefore changed the structural regime,
    not just the entry payload.
  - Consequence:
    - this branch is no longer sitting on the old `trace 4` bridge seam
    - the current behavior is an earlier `parent=3 exit=0 -> BC_LOOP`
      resume-linked spin
    - so the raw-table branch is a real classifier, but not yet a landing fix

- `iter-chain-handoff-root-itern-ab-and-lownoise-20260330g`
  - Stage: focused native iterator probe
  - Surface: `iter-tiny`
  - Host: `kdz`
  - Result: classification only
  - Notes: the next question was whether the earlier `parent=3 exit=0` focused
    loop was the real structural fork, or just the first hot surface before
    the later bridge still forms.
  - The A/B split showed:
    - `LUAJIT_S390X_ROOT_ITERN_SETUP_ITERN=1` by itself is enough to produce
      the earlier repeated
      `target=1 target_exec=2 retop=BC_LOOP phase=resume-linked`
      surface under focused `JLOOP_EXIT` logging
    - the raw-table preserve envs by themselves are not safe on this branch
      (`RC=139`)
  - But that focused result was not the real fork. A clean low-noise rerun
    with only `RECSTOP` and `TRACE_META` after gating the raw-table save in
    [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c)
    shows that both variants still reach the same later bridge stop within
    20 seconds:
    - stable baseline, new knobs off:
      - `S390X_RECSTOP trace=4 parent=3 exit=0`
      - `S390X_TRACE_META phase=stop trace=4 ... startop=88 link=3 linktype=1 nins=32770 mcloop=0`
    - combined producer branch:
      - `S390X_RECSTOP trace=4 parent=3 exit=0`
      - `S390X_TRACE_META phase=stop trace=4 ... startop=88 link=3 linktype=1 nins=32770 mcloop=0`
  - Consequence:
    - the earlier `parent=3 exit=0 -> BC_LOOP` loop is a transient hot
      surface, not the decisive structural split
    - the real seam remains later, at the same `trace 4` bridge stop
    - the raw-table / producer work must now prove value on the post-`trace 4`
      bridge path, not by merely changing the first focused `JLOOP_EXIT`
      pattern
  - A follow-up `gdb` run on the combined producer branch with a breakpoint on
    `lj_vm_IITERN` and `ignore 1 5` did not reach the sixth hit within a 40
    second timeout. That matches the earlier “hit 2 exists, hit 50 does not”
    classification: even on the repaired producer branch, `lj_vm_IITERN` is
    not the dominant hot replay loop.

- `iter-chain-handoff-post-trace4-no-dispatch-yet-20260330g`
  - Stage: focused native iterator probe
  - Surface: `iter-tiny`, `iter-chain-handoff`
  - Host: `kdz`
  - Result: classification only
  - Notes: the next exact question was whether the repaired producer branch
    changes anything on the real post-`trace 4` seam. It does not, at least
    not within the first bridge-latency window.
  - Paired low-noise runs on `iter-tiny`, with only:
    - `LUAJIT_S390X_RECSTOP_LOG=1`
    - `LUAJIT_S390X_TRACE_META_LOG=1`
    - `LUAJIT_S390X_VM_BRIDGE_DISPATCH_LOG=1`
    - `LUAJIT_S390X_VM_ITERL_LOG=1`
    show the same result for both the stable baseline and the combined
    producer branch:
    - `S390X_RECSTOP trace=4 parent=3 exit=0`
    - `S390X_TRACE_META phase=stop trace=4 ... startop=88 link=3 linktype=1 nins=32770 mcloop=0`
    - but no `S390X_VM_BRIDGE_DISPATCH`
    - and no `S390X_VM_ITERL`
    within 60 seconds.
  - Repeating the same narrow probe on the full authoritative
    `iter-chain-handoff.lua` surface gives the same answer:
    - the run reaches `trace 4`
    - but still shows no logged bridge-consumed interpreter dispatch within
      60 seconds
  - Consequence:
    - the live seam is now even narrower than “post-bridge consumption”
    - the active gap is between `trace 4` stop formation and the first actual
      bridge-consumed interpreter dispatch
    - the next exact target is a debugger or trace-exit stop on that gap,
      not more producer-lane work and not more broad bridge logging

- `iter-chain-handoff-post-trace4-debug-window-20260330g`
  - Stage: focused native iterator probe
  - Surface: `iter-tiny`, `iter-chain-handoff`
  - Host: `kdz`
  - Result: classifier rejects
  - Notes: the next attempt was to observe the gap directly under `gdb`.
  - Two later breakpoints in
    [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c)
    were tested on the safe baseline:
    - the exact `loopdesc-bridge-child-reenter` return
    - the earlier fully-classified `JLOOP_EXIT` site where `target`,
      `execno`, and `retop` are already computed
  - Under `gdb`, neither breakpoint was reached in a reasonable window:
    - `iter-tiny` still did not hit `loopdesc-bridge-child-reenter`
      within 120 seconds
    - it also did not reach the earlier classified `JLOOP_EXIT` site
      within 60 seconds
    - the full `iter-chain-handoff.lua` surface likewise did not reach the
      later bridge-child breakpoint within 40 seconds
  - A more aggressive debugger-only micro repro with:
    - `hotloop=1`
    - `hotexit=1`
    - `run(1)`, `run(2)`, `run(3)`
    was also tested as a faster seam trigger. That is not a valid reproducer:
    it segfaults early (`RC=139`) before it can be used as a bridge classifier.
  - One VM-side static-dispatch probe was also tried and rejected. Injecting a
    helper call at the static-dispatch entry in
    [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc)
    was too invasive for this path: even a metadata-only call perturbed the
    root path and crashed before the real seam.
  - Consequence:
    - the active branch must stay on the safe runtime baseline
    - the next debugger target needs an earlier, cheaper stop than the
      bridge-child return itself
    - `hotloop=1/hotexit=1` is not a trustworthy debug-time substitute for the
      current tiny reproducer

- `iter-chain-handoff-bridge-prev-jloop-reject-20260330f`
  - Stage: focused native iterator probe
  - Surface: `iter-tiny`
  - Host: `kdz`
  - Result: reject
  - Notes: the next exact bridge-target experiment was to dispatch the live
    bytecode carrier immediately before the replayed `ITERL` seam instead of
    replaying `IITERL` again. The strengthened bridge log in
    [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c)
    proves the repeated post-bridge neighborhood is:
    - `prev2 = 0x010a0120` (`BC_ADDVV`)
    - `prev1 = 0x00040b57` (`BC_JLOOP 4`)
    - `pc = 0x7ffd0952` (`BC_ITERL`, `A=9`, `D=32765`)
    - `next1 = 0x7ff8024f` (`BC_FORL`)
    So the bridge is definitely resuming in the local `ADDVV ; JLOOP ; ITERL ;
    FORL` tail, and the replayed `ITERL` itself is not carrying the wrong
    A-field anymore.
  - In
    [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc),
    the exact bridge-resumed `BC_ITERL` path was patched to dispatch the
    preceding live `BC_JLOOP` carrier instead of forcing `BC_IITERL`.
  - On `kdz`, that is also a reject. The tiny reproducer fails early with
    `RUN_EXIT=132` / `SIGILL` before the replay seam advances.
  - Consequence:
    - the active bridge bug is not simply “resume at `JLOOP` instead of
      `ITERL`”
    - the missing refresh target is now narrower:
      - not the replayed `ITERL` tail
      - not the raw local `JLOOP` carrier
    - the next seam is the preserved pre-tail refresh contract that should run
      before this local `ADDVV ; JLOOP ; ITERL ; FORL` tail, not either of the
      two live bytecodes currently sitting in front of us

- `iter-chain-handoff-bridge-dispatch-neighborhood-20260330f`
  - Stage: focused native iterator probe
  - Surface: `iter-tiny`
  - Host: `kdz`
  - Result: classification only
  - Notes: the strengthened `S390X_VM_BRIDGE_DISPATCH` log in
    [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c)
    now captures the exact bytecode neighborhood around the repeated
    bridge-resumed seam. On the stable branch:
    - `trace=3`
    - `startpc = 0x...6ac`
    - `resumepc = 0x...6ac`
    - bridge `pc = 0x...6b0`
    - `op = BC_ITERL`
    - `ra = 9`
    - `rd = 32765`
    - `prev2op = BC_ADDVV`
    - `prev1op = BC_JLOOP`
    - `next1op = BC_FORL`
  - This closes the earlier ambiguity about stale decode-state. The bridge is
    not replaying `ITERL` with the wrong local `A` field anymore. It is
    re-entering the tail at the right `ITERL` register contract, but after the
    state-refreshing producer body has already been replaced by the local
    `BC_JLOOP` carrier.

- `iter-chain-handoff-bridge-iitern-refresh-reject-20260330f`
  - Stage: focused native iterator probe
  - Surface: `iter-tiny`
  - Host: `kdz`
  - Result: reject
  - Notes: forcing the exact bridge-resumed `BC_ITERL` seam to jump directly
    into `lj_vm_IITERN` is not a valid landing path. The idea was coherent:
    the stable branch was replaying a frozen `IITERL` tail, so the next test
    was to re-enter the iterator producer body instead of consuming the same
    carried result bundle again. In
    [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc),
    the exact bridge branch was patched to detect the local `BC_JLOOP`
    carrier, rewind `PC`, decode `RA`, and jump to `->vm_IITERN`.
  - `gdb` shows the bridge lands in `lj_vm_IITERN+24` with the wrong frame
    contract:
    - `RA = 11`
    - `r4 = 0x58` after the internal `sllg`
    - `r7 = 0x2`
    - `r5 = 0x3`
    - crash at `llgf %r1,48(%r7)`
  - The live frame around `BASE` confirms why:
    - `BASE+48` still holds the iterator function
    - `BASE+56` holds integer `0`
    - `BASE+64` holds the special control-var sentinel
    - `BASE+72` holds boxed integer `2`
    - `BASE+80` holds boxed integer `3`
    So with `RA=11`, the `-16(RA, BASE)` slot that `IITERN` expects to be the
    iterator table actually resolves to the frozen key lane (`2`), not a table.
  - A save/restore-side attempt to preserve one extra pre-call table slot did
    not fix this. The failure is not “one missing lane”; it is that the bridge
    is not re-entering the iterator producer through the original bytecode
    frame shape at all.
  - Consequence:
    - do not force `vm_IITERN` from the bridge carrier path
    - keep the stable `BC_ITERL -> BC_IITERL` bridge dispatch as the baseline
    - the next seam remains the pre-tail refresh contract: the exact resumed
      bytecode target/state that should precede the replayed `JLOOP/ITERL/FORL`
      tail

- `iter-chain-handoff-bridge-iiterl-stable-timeout-20260330e`
  - Stage: focused native iterator probe
  - Surface: `iter-chain-handoff`
  - Host: `kdz`
  - Result: classification only
  - Notes: the exact post-bridge VM seam moved again, and this time the change
    is a real stabilization. In
    [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc),
    the bridge-resumed static decode now special-cases `BC_ITERL` to dispatch
    as `BC_IITERL` instead of running the `BC_ITERL` hotloop prologue. The
    first cut missed because the `BC_JMP` fast-path branch jumped around the
    rewrite; moving the `BC_ITERL -> BC_IITERL` rewrite ahead of that branch
    fixed the control flow. On the authoritative `kdz` baseline, the run no
    longer segfaults. It now times out cleanly, still repeatedly hitting the
    exact bridge seam. So the active blocker is no longer the bridge crash
    itself; it is forward progress after the stabilized bridge dispatch.

- `iter-chain-handoff-bridge-iiterl-replay-state-20260330e`
  - Stage: focused native iterator probe
  - Surface: `iter-chain-handoff`
  - Host: `kdz`
  - Result: classification only
  - Notes: after the `BC_ITERL -> BC_IITERL` bridge fix, the exact bridge seam
    is stable enough to observe directly in `gdb`. A breakpoint on
    `lj_BC_IITERL` for the bridge-resumed `RD=32765` path shows:
    - hit `n=1`: `vm_pc=...cc9c`, `RA=11`, `RD=32765`
    - hit `n=50`: same `vm_pc`, same `RA/RD`
    - hit `n=200`: same `vm_pc`, same `RA/RD`
    and the carried slots around `BASE+80` / `BASE+104` stabilize after the
    first hit and then stop changing:
    - one lane materializes from the initial odd value to boxed `2`
    - the surrounding iterator/result lanes remain fixed (`2`, `3`, GC refs)
    This means the post-bridge path is no longer crashing, but it is replaying
    the same `IITERL` state instead of making iterator progress. The next seam
    is therefore not another VM crash contract; it is why the resumed bridge
    path keeps re-entering `IITERL` with the same carried result bundle.

- `iter-chain-handoff-bridge-resume-itern-reject-20260330e`
  - Stage: focused native iterator probe
  - Surface: `iter-chain-handoff`
  - Host: `kdz`
  - Result: reject
  - Notes: shifting the exact bridge resume site one instruction earlier, from
    bridge `BC_ITERL` to the preceding `BC_ITERN`, is not a safe fix. The idea
    was coherent: post-bridge runs were replaying `IITERL` without re-hitting
    `ITERN`, so resuming from the iterator call looked like the next boundary
    to test. In
    [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc),
    the exact bridge branch was patched to detect a preceding `BC_ITERN` and
    dispatch from there instead of from the bridge `BC_ITERL`. On the
    authoritative `kdz` baseline, that regressed immediately to a hard
    segfault (`RUN_EXIT=139`). So the bridge must not simply rewind to the
    iterator call site wholesale. The stable branch remains the exact
    `BC_ITERL -> BC_IITERL` bridge dispatch, which times out cleanly and keeps
    the seam on post-bridge iterator progress instead of another VM crash.

- `iter-chain-handoff-bridge-iterl-hotloop-jiterl-20260330e`
  - Stage: focused native iterator probe
  - Surface: `iter-chain-handoff`
  - Host: `kdz`
  - Result: classification only
  - Notes: a focused `gdb` stop on the fixed post-bridge branch proved the
    next crash was not another ownership issue. The exact bridge-resumed
    `BC_ITERL` at `trace3.resumepc+4` was being hotloop-patched in-place into
    `BC_JITERL`:
    - original bridge resume ins: `0x7ffd0b52` (`BC_ITERL`)
    - patched live bytecode: `0x7ffd0b54` (`BC_JITERL`)
    - preceding bytecode: `0x00040d57` (`BC_JLOOP 4`)
    That patched `BC_JITERL` then jumped to `lj_BC_JLOOP` with `RD=32765`
    (`0x7ffd`), which is still the iterator target field, not a trace number,
    and crashed in the trace table lookup. This is why the correct bridge fix
    is to skip the hotloop prologue on this exact resumed `ITERL` path, not to
    mutate ownership or resume metadata again.

- `iter-chain-handoff-bridge-helper-reg-preserve-20260330e`
  - Stage: focused native iterator probe
  - Surface: `iter-chain-handoff`
  - Host: `kdz`
  - Result: classification only
  - Notes: preserving the bridge helper’s `resumepc` and `resumeins` across
    the C logging call in
    [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc)
    was a real fix. Before that change, the post-bridge resumed decode-state
    was bogus at `lj_vmeta_istype` entry:
    - `PC = 0x27`
    - `RA = 0`
    - `RD = 0`
    After saving `resumepc` in `RB` and `resumeins` in `ITYPE` across the
    helper call, the seam moved back onto an honest interpreter-side loop path:
    repeated bridge hits followed by `lj_BC_JLOOP`, with real `PC`, `BASE`,
    and live bytecode. That proved the earlier `ISTYPE` state corruption was
    helper-call register clobbering, not the underlying bridge contract.

- `iter-chain-handoff-partial-sync-false-regression-20260330d`
  - Stage: focused native iterator probe
  - Surface: `iter-chain-handoff`
  - Host: `kdz`
  - Result: classification only
  - Notes: a partial source sync back to `kdz` created a false regression
    surface. Syncing only
    [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c)
    and a few nearby files, while leaving older
    [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c),
    [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h),
    and related continuation files on the remote host, collapsed the runtime
    back onto the old `trace 3` illegal-instruction surface. A full sync of the
    active continuation files is required before treating any `kdz` result as
    authoritative.

- `iter-chain-handoff-trace3-mcloop-baseline-20260330d`
  - Stage: focused native iterator probe
  - Surface: `iter-chain-handoff`
  - Host: `kdz`
  - Result: classification only
  - Notes: on the fully synced branch, the current safe `trace 3` baseline is
    narrower than previously documented. The two env-gated `mcloop` controls:
    - `LUAJIT_S390X_JLOOP_EXEC_SKIP_MCLOOP=1`
    - `LUAJIT_S390X_VM_CHILD_SKIP_MCLOOP=1`
    are both required to keep the branch off the old raw `trace 3` child-entry
    crash. With those enabled, `trace 3` saves with:
    - `resumeop=BC_JLOOP`
    - `mcloop=20`
    - `ownerop=0`
    and the authoritative `kdz` harness again reaches:
    - `trace 4`
    - `parent=3 exit=0`
    - `root=1`
    - `startop=BC_JMP`
    - `nins=32770`
    - `mcloop=0`
    So those `mcloop` gates are part of the current valid debug baseline on
    this branch, not optional extras.

- `iter-chain-handoff-vm-exit-child-entry-r6-valid-20260330d`
  - Stage: focused native iterator probe
  - Surface: `iter-chain-handoff`
  - Host: `kdz`
  - Result: classification only
  - Notes: a focused `gdb` stop on the fully synced branch shows the
    `lj_vm_exit_interp` child-entry path is not failing because `TRACE:RD` is
    garbage. At the `SIGILL` stop:
    - `r6` points at a real trace object
    - that trace is the live `trace 3` loop child
    - the `mcode` field at offset `+104` is valid runtime mcode
      (`0x...fc0c`)
    - the process still eventually dies at `pc=0x3b`
    So the current `0x3b` illegal-instruction is later than trace lookup and
    later than the raw `mcode` load itself. A simple reload of `TRACE->mcode`
    after the helper call in
    [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc)
    is not sufficient by itself. The remaining seam on that surface is still
    inside the effective `trace 3` child-entry/entry-state path, unless the
    `mcloop` gates are enabled to suppress it.

- `iter-chain-handoff-safe-baseline-trace4-timeout-20260330d`
  - Stage: focused native iterator probe
  - Surface: `iter-chain-handoff`
  - Host: `kdz`
  - Result: classification only
  - Notes: with the fully synced branch and the two `mcloop` gates enabled:
    - `LUAJIT_S390X_JLOOP_EXEC_SKIP_MCLOOP=1`
    - `LUAJIT_S390X_VM_CHILD_SKIP_MCLOOP=1`
    the recovered `kdz` baseline no longer dies at the old raw `trace 3`
    child-entry fault. It settles into the expected pre-bridge shape:
    - repeated `parent=3 exit=0`
    - `target=2`
    - `target_exec=3`
    - `retop=87`
    - `phase=loopdesc-child-query ... child=0`
    - `phase=resume-linked`
    and then records:
    - `S390X_BCJMP_STOP trace=4 parent=3 exit=0 ... nins=32770`
    before timing out. So the valid current seam is again the post-`trace 4`
    bridge/continuation path, not the earlier raw `trace 3` child-entry crash.

- `iter-chain-handoff-interp-resume-sigill-20260330c`
  - Stage: focused native iterator probe
  - Surface: `iter-chain-handoff`
  - Host: `kdz`
  - Result: reject
  - Notes: widening the existing
    `LUAJIT_S390X_BCJMP_LOOPDESC_RESUME` save-time setup in
    [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c)
    so it also covered the first non-stub `LJ_TRLINK_INTERP` bridge shape
    (`root=1`, `startop=BC_JMP`, `nins=32770`) is not a safe landing path.
    On `kdz`, the authoritative `iter-chain-handoff.lua` surface fails
    immediately with `SIGILL` before the new focused `S390X_BRIDGE_META` line
    can fire. So the first real interpreter bridge cannot simply inherit the
    existing loop-desc resume setup wholesale.

- `iter-chain-handoff-vm-bridge-helper-sigill-20260330c`
  - Stage: focused native iterator probe
  - Surface: `iter-chain-handoff`
  - Host: `kdz`
  - Result: reject
  - Notes: a direct VM-side helper-call probe is also too intrusive on this
    seam. Adding a logging-only helper call in
    [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc)
    at the exact static-resume consumption point, wired to
    [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c)
    and gated by `LUAJIT_S390X_VM_BRIDGE_RESUME_LOG=1`, fails immediately with
    `SIGILL` before any `S390X_VM_BRIDGE_RESUME` line can print. So the next
    bridge probe must avoid introducing a helper call in the VM fast path and
    instead use debugger stops or an even narrower non-call-side observation
    technique.

- `iter-chain-handoff-kdz-jit-plumbing-20260330b`
  - Stage: focused native iterator probe
  - Surface: `iter-chain-handoff`
  - Host: `kdz`
  - Result: classification only
  - Notes: `kdz` had regressed into a misleading non-JIT runtime surface even
    after successful top-level repo rebuilds. The decisive proof came from the
    temporary init log in
    [src/lib_jit.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lib_jit.c):
    repo-root rebuilds still printed:
    - `S390X_JIT_LIB phase=luaopen_jit after_init flags=nojit`
    - `jit.on()` then failed with `no JIT compiler for this architecture (yet)`
    Rebuilding directly in
    `/root/luajit2-s390x/perf-iterator-kdz-20260325u/repo/src` with:
    - `make XCFLAGS=-DLUAJIT_ENABLE_S390X_JIT -j4`
    restored the real surface:
    - `S390X_JIT_LIB phase=jit_init flags=0x3ff0001`
    - `jit.status() == true`
    - `jit.on()` succeeds
    So the active bridge work on `kdz` must use a direct `src/` rebuild; the
    top-level repo rebuild path was not propagating the s390x JIT define into
    all compilation units reliably enough for focused bridge work.

- `iter-chain-handoff-trace102-restore-live-slot13-20260330b`
  - Stage: focused native iterator probe
  - Surface: `iter-chain-handoff`
  - Host: `kdz`
  - Result: classification only
  - Notes: the authoritative remote harness on `kdz` is the repo copy:
    [iter-chain-handoff.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/iter-chain-handoff.lua)
    with its built-in:
    - `require("jit.opt")`
    - `hotloop=2`
    - `hotexit=10`
    - `minstitch=1`
    On the recovered continuation bundle, that exact surface again reproduces
    the full bridge ladder:
    - `trace 1 -> 2 -> 3`
    - then the pure-loop family through `trace 101`
    - then the first non-stub bridge:
      - `trace 102`
      - `parent=101 exit=0`
      - `linktype=6`
      - `nins=32770`
      - `mcloop=0`
    The restore-time probe in
    [src/lj_snap.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_snap.c)
    now proves the hidden control-var lane is still live well past the old
    early seams:
    - slot `13` is a GC reference on `trace 1 exit=4`
    - slot `13` is still a GC reference on `trace 3 exit=0`
    - slot `13` is still a GC reference on `trace 101 exit=0` immediately
      before `trace 102` forms
    So the nil/compare failure found earlier is not caused by snapshot
    restore. The hidden lane is being lost later, during or after the resumed
    post-`trace 102` bridge/static-dispatch path.

- `iter-chain-handoff-vm-bridge-nil-number-20260330a`
  - Stage: focused native iterator probe
  - Surface: `iter-chain-short`
  - Host: `zkd0`
  - Result: classification only
  - Notes: the VM-side bridge-dispatch experiment in
    [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc)
    no longer fails by raw-entering the `trace 4` bridge stub or by crashing in
    `lj_BC_ISNEXT`. With the exact continuation baseline, the failing run now
    reaches normal compare error handling:
    - `lj_vmeta_comp`
    - `lj_meta_comp`
    - `lj_err_comp`
    and reports:
    - `attempt to compare nil with number`
    A focused `gdb` stop on `lj_err_comp` shows the operands are:
    - `o1 = L->base + 13`
    - `o1->u64 = 0xffffffffffffffff` (`nil`)
    - `o2->u64 = 0x0` (`number 0`)
    So the remaining bridge bug is now value-level and specific: the hidden
    control-var lane at slot `13` is still nil by the time the bridge path
    reaches the resumed compare site. This rules out another ownership mistake
    as the immediate blocker and points at bridge consumption / hidden slot
    materialization instead.

- `iter-chain-handoff-exec-self-reenter-fired-20260329m`
  - Stage: focused native iterator probe
  - Surface: `iter-chain-handoff`
  - Host: `kdz`
  - Result: classification only
  - Notes: the exact `parent=3 exit=0` owner/self branch in
    [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c)
    is now proven live. With the recovered baseline plus:
    - `LUAJIT_S390X_JLOOP_EXEC_SELF_REENTER=1`
    - `LUAJIT_S390X_JLOOP_EXEC_SELF_PRED_LOG=1`
    the focused `3/0` run shows:
    - `use_resume=1`
    - `idle=1`
    - `parent_is_trace=1`
    - `exec_is_trace=1`
    - `is_jloop=1`
    - initial `bcd=2`, then repeated `bcd=3`
    - repeated `phase=exec-self-reenter`
    So the old “branch never fires” fork is closed. The handoff logic now
    really retargets the current `BC_JLOOP` from owner stub `2` onto owner
    `3`. The remaining problem is after transfer, not before it.

- `iter-chain-handoff-trace3-mcode-crash-20260329n`
  - Stage: focused native iterator probe
  - Surface: `iter-chain-handoff`
  - Host: `kdz`
  - Result: classification only
  - Notes: once the `3/0` exec-self-reenter branch is live, the short harness
    no longer merely times out. It segfaults. A focused `gdb` run on the exact
    recovered env shows:
    - `S390X_VM_CHILD_ENTRY ... trace=3 ... mcode=0x331bfcb4 mcloop=20`
    - repeated `phase=exec-self-reenter`
    - crash at `pc = 0x331bfd00`, which is `trace 3` mcode `+0x4c`
    The disassembly window around the trace shows the trace body begins at
    `0x331bfcb4`, the mcloop entry is at `0x331bfcc8`, and the failing PC is
    later inside the same trace body. This means the live seam has moved below
    owner selection and transfer logic: the active bug is now the `trace 3`
    self-entry / execution contract itself, likely its stack or entry-state
    ABI, not the old `4 -> 5` owner candidate.

- `iter-chain-handoff-vm-child-entry-trace3-20260329k`
  - Stage: focused native iterator probe
  - Surface: `iter-chain-handoff`
  - Host: `kdz`
  - Result: classification only
  - Notes: after a clean archive rebuild on `kdz` (`rm -f libluajit.a luajit
    lj_trace.o` before `make`), the recovered baseline with
    `LUAJIT_S390X_EMPTY_LOOP_FALLTHROUGH=1` plus the continuation envs can now
    reproducibly reach real VM child entry for the loop-child owner:
    - `S390X_VM_CHILD_ENTRY ... trace=3 ... resumeop=87 ... mcloop=20`
    On the same run, `parent=3 exit=0` repeatedly logs:
    - `target=2`
    - `target_exec=3`
    - `retop=87`
    - `exec_resumeop=87`
    but still only takes `phase=resume-linked`, never any visible
    `parent=4/5` execution transfer. `trace 4` still stops later as:
    - `S390X_BCJMP_STOP trace=4 parent=3 exit=0 ...`
    So the recovered path is now deeper than the earlier root/trace-2 churn:
    VM child entry for `trace 3` is real, but the post-entry self/owner
    re-entry path at `parent=3 exit=0` still does not hand execution forward.

- `iter-chain-handoff-owner-static-reenter-miss-20260329l`
  - Stage: focused native iterator probe
  - Surface: `iter-chain-handoff`
  - Host: `kdz`
  - Result: classification only
  - Notes: a narrow `lj_trace_exit()` experiment that forced `-17` static
    re-entry for the exact stabilized loop-desc owner shape (`trace 5`
    family) did change runtime behavior, but not at the expected seam. On the
    recovered long harness it produced early `S390X_VM_CHILD_ENTRY trace=3`,
    but still no `parent=5`, no `trace=6`, and no final `RESULT`. On the short
    handoff harness it did not even reach the old `4 -> 5` seam; it remained
    stuck in:
    - root `exit 4`
    - root `exit 1 -> target=2`
    - `parent=3 exit=0 -> target=2 target_exec=3 retop=87`
    Therefore the missing transfer is earlier than the exact `trace 5`
    experiment. The live seam has moved to the `parent=3 exit=0` owner/self
    re-entry path, not the old `4 -> 5` candidate alone.

- `iter-chain-handoff-fallthrough-recovery-20260329i`
  - Stage: focused native iterator probe
  - Surface: `iter-chain-handoff`
  - Host: `kdz`
  - Result: classification only
  - Notes: the apparent `trace 3` regression was partly a bad baseline. The
    dedicated handoff harness had dropped `LUAJIT_S390X_EMPTY_LOOP_FALLTHROUGH=1`,
    which reintroduced the old pure-loop self-hang before the deeper
    continuation seam. With that env restored:
    - the harness again gets past `trace 3`
    - the recovered chain logs
      `S390X_BCJMP_STOP trace=4 parent=3 exit=0 ... link=3 linktype=1 nins=32770`
    - so the deeper continuation seam is still reachable on the current code
      line
    With the same recovered baseline and `parent=4 exit=0` focus, the plain
    runtime now segfaults quickly instead of merely timing out. So the active
    seam is once again the post-`trace 4` continuation boundary, not the old
    pure-loop `trace 3` hang.

- `iter-chain-handoff-root-stuck-on-2-20260329j`
  - Stage: focused native iterator probe
  - Surface: `iter-chain-handoff`
  - Host: `kdz`
  - Result: classification only
  - Notes: on the recovered baseline (`LUAJIT_S390X_EMPTY_LOOP_FALLTHROUGH=1`
    restored, `LUAJIT_S390X_JLOOP_CURRENT_SELF_RESUME` off), root `exit 1`
    does not immediately adopt the newly promoted loop child. The focused
    `parent=1 exit=1` log shows repeated:
    - `target=2`
    - `target_exec=2`
    - `retop=82`
    - `target_resumechild=0`
    even after `trace 3` promotion has begun. Only later in the same run does
    the chain advance enough to save:
    - `S390X_BCJMP_STOP trace=4 parent=3 exit=0 ...`
    So the recovered deeper seam is real, but the root-side handoff still
    spends measurable time stuck on continuation stub `2` before `3/0` is even
    allowed to form. That makes the next precise question: why the `2 -> 3`
    runtime-owner adoption is delayed on this harness, and whether that delay
    is what eventually feeds the crashing `4/0` path.

- `iter-chain-handoff-trace3-self-resume-mask-20260329h`
  - Stage: focused native iterator probe
  - Surface: `iter-chain-handoff`
  - Host: `kdz`
  - Result: classification only
  - Notes: on the current continuation branch, `trace 3` is now the active
    masking seam. With the full continuation env set, the harness stalls after
    `trace 3` promotion and never reaches any `parent=4 exit=0` activity. When
    only `LUAJIT_S390X_JLOOP_CURRENT_SELF_RESUME` is removed, the same harness
    immediately segfaults instead of timing out. So the current-self-resume
    gate is no longer just a late `4 -> 5` policy tweak on this surface; it is
    masking an earlier `trace 3 exit=0` failure. The live bug on this harness
    has moved back up to the `trace 3` self-resume / exit-0 path.

- `iter-chain-handoff-trace3-regression-20260329g`
  - Stage: focused native iterator probe
  - Surface: `iter-chain-handoff`
  - Host: `kdz`
  - Result: classification only
  - Notes: the dedicated `iter-chain-handoff.lua` harness currently no longer
    reaches the old `4 -> 5` seam on the active continuation branch. With the
    previously working continuation env set, it now stalls immediately after:
    - `trace 2` promotion from `parent=1 exit=4`
    - `trace 3` promotion from `parent=1 exit=1`
    and emits only the seven root/child promotion lines before timing out.
    This remains true both with and without
    `LUAJIT_S390X_LOOPDESC_OWNER_DIRECT_ENTRY=1`, so the exact-owner scratch is
    not the only cause. The current chain-handoff harness is therefore acting
    as a higher-level regression surface: before any more `4 -> 5` transfer
    work, the deeper continuation seam needs to be re-established on the exact
    workload/configuration that previously reached it.

- `iter-array-loopdesc-owner-patch-resumepc-jloop-20260329f`
  - Stage: focused native iterator probe
  - Surface: `iter-chain-handoff`
  - Host: `kdz`
  - Result: classification only
  - Notes: one-dispatch patching of the exact stabilized owner’s
    `resume_bcpc` to `BC_JLOOP self` is not enough to force real owner entry.
    The exact branch fires repeatedly:
    - `phase=patch-resumepc-jloop`
    - `parent=4`
    - `exit=0`
    - `target=5`
    - `resumepc=<target startpc>`
    but still never produces:
    - any `S390X_VM_CHILD_ENTRY`
    - any `parent=5`
    - any `trace=6`
    - any final `RESULT`
    So the remaining blocker is now below owner selection, owner marking, and
    even one-dispatch resume-site patching. The actual post-exit PC/dispatch
    consumer is still not handing execution onto the selected owner body.

- `iter-array-loopdesc-owner-self-jloop-20260329e`
  - Stage: focused native iterator probe
  - Surface: `iter-chain-handoff`
  - Host: `kdz`
  - Result: classification only
  - Notes: the exact stabilized owner shape (`trace 5`) can be moved onto the
    same self-`BC_JLOOP` resume contract used by the pure-loop family:
    - `target=5`
    - `target_exec=5`
    - `target_resumeop=87`
    - `target_resumechild=5`
    - `target_ownerop=85`
    and the steady-state root-side seam changes accordingly:
    - `retop=87`
    instead of the earlier `retop=82`. But this still does not produce:
    - any `parent=5`
    - any `trace=6`
    - any `S390X_VM_CHILD_ENTRY`
    - any final `RESULT`
    So the remaining blocker is not just “wrong resume opcode family” for the
    stabilized owner. Even when `trace 5` is self-`JLOOP` resumable, the
    runtime still does not visibly transfer execution onto it.

- `iter-array-loopdesc-owner-force-static-20260329d`
  - Stage: focused native iterator probe
  - Surface: `iter-chain-handoff`
  - Host: `kdz`
  - Result: classification only
  - Notes: forcing `lj_trace_exit()` to return `-17` for the exact stabilized
    owner shape does not make the VM consume that owner through the existing
    static `BC_JLOOP` child-entry path. The run repeatedly reports:
    - `phase=force-static-owner`
    - `target=5`
    - `target_exec=5`
    - `retop=82`
    but still shows no:
    - `S390X_VM_CHILD_ENTRY`
    - `parent=5`
    - `trace=6`
    This means the missing transfer is not solved by simply forcing the
    selected owner onto the `-17` static-dispatch path.

- `iter-array-loopdesc-owner-direct-shape-20260329c`
  - Stage: focused native iterator probe
  - Surface: `iter-chain-handoff`
  - Host: `kdz`
  - Result: classification only
  - Notes: marking the exact stabilized owner candidate with:
    - `resumechild=self`
    - `ownerop=BC_LOOP`
    changes the saved owner metadata exactly as intended:
    - `target=5`
    - `target_exec=5`
    - `target_resumechild=5`
    - `target_ownerop=85`
    but it does not trigger actual owner entry under the current runtime
    path. The steady-state exit still remains:
    - `retop=82`
    with no `S390X_VM_CHILD_ENTRY`, `parent=5`, `trace=6`, or `RESULT`.
    So selecting and marking the owner is not enough by itself; the runtime
    still does not transfer onto that selected owner body.

- `iter-array-loopdesc-owner-gated-self-resume-20260329b`
  - Stage: focused native iterator probe
  - Surface: `iter-chain-handoff`
  - Host: `kdz`
  - Result: classification only
  - Notes: the `4 -> 5` stall was not just “owner 5 never selected”. The
    current-trace self-resume path in `trace_exit()` was still winning first.
    On the authoritative baseline, repeated `parent=4 exit=0` showed:
    - `target=5`
    - `target_exec=5`
    - but `retop=87`
    which means the path was still returning through trace `4`'s own saved
    `BC_JLOOP` contract.
    Narrowing `LUAJIT_S390X_JLOOP_CURRENT_SELF_RESUME` so it only applies
    when the current trace is itself the computed runtime owner changes that
    decisively:
    - `target=5`
    - `target_exec=5`
    - `retop=82`
    - `target_resumechild=0`
    So the selected owner contract is finally being consumed from trace `5`,
    not from trace `4`.
    But it is still not a landing fix. The run still does not produce:
    - any `parent=5`
    - any `trace=6`
    - any `S390X_VM_CHILD_ENTRY`
    - any final `RESULT`
    That means the remaining blocker has moved again: it is no longer the
    `trace 4` self-resume override. It is the VM-side/static-resume
    consumption of trace `5`'s `BC_ITERL` resume contract.

- `iter-array-loopdesc-self-owner-stop-20260329a`
  - Stage: focused native iterator probe
  - Surface: `iter-chain-handoff`
  - Host: `kdz`
  - Result: classification only
  - Notes: stopping the `parent>=3 exit=0` pure-loop family against the
    already self-`JLOOP`-capable owner materially changes the shape.
    With:
    - `LUAJIT_S390X_LOOPDESC_SELF_OWNER_STOP=1`
    - plus the existing loop-desc self-`JLOOP` resume baseline
    `rec_loop_jit()` now hits:
    - `S390X_RECLOOP ... loopdesc_self_owner_stop=1 root=1 lnk=4`
    on `trace 5 parent=4 exit=0`
    and the new descendant saves as:
    - `trace=5`
    - `link=4`
    - `linktype=2`
    - `nins=32770`
    - `szmcode=40`
    - `mcloop=0`
    This is the first time the endless pure-loop `BC_JMP` ladder has been cut
    off before the old `trace 102 / LJ_TRLINK_INTERP` bridge. The remaining
    blocker is now the handoff after owner `4`: runtime still stalls before
    execution visibly transfers onto the new `trace 5` descendant.

- `iter-array-loopdesc-targetexec4-20260328d`
  - Stage: focused native iterator probe
  - Surface: `pairs_array_sum`
  - Host: `kdz`
  - Result: classification only
  - Notes: suppressing `resumechild` on the first non-stub loop-desc
    candidate is a real improvement. With the current safe branch:
    - `trace 4` still saves as a root-owned `BC_JMP` continuation trace with
      a valid resume contract
    - but `target_exec` now stays on `4`, not `3`
    - the steady-state root-side seam becomes:
      - `target=4`
      - `target_exec=4`
      - `target_resumeop=82`
      - `target_resumechild=0`
    This proves the first non-stub descendant can stand as its own execution
    candidate instead of being forced backward into the previous tiny
    pure-loop stub. It is not a landing fix yet: the chain still times out
    with `retop=88`, so the remaining blocker is the continuation contract
    after selecting `trace 4`, not child selection anymore.

- `iter-array-loopdesc-record-orig-20260328d`
  - Stage: focused native iterator probe
  - Surface: `pairs_array_sum`
  - Host: `kdz`
  - Result: reject
  - Notes: a loop-desc-only variant of the old `dispatch-resume-bc` idea is
    not safe. Trying to patch the `parent>=3 exit=0` seam through the selected
    loop-desc stub’s saved `resumepc/resumeins` contract caused an immediate
    crash. This means the supported next step is not “record through `trace 4`
    by rewriting the current `BC_JLOOP` to its resume bytecode”.

- `iter-array-loopdesc-nonstub-looplink-20260328d`
  - Stage: focused native iterator probe
  - Surface: `pairs_array_sum`
  - Host: `kdz`
  - Result: reject
  - Notes: changing `rec_loop_jit()` so the first non-stub loop-desc child
    stays loop-linked to its parent regresses the chain. Instead of
    stabilizing `trace 3 -> trace 4`, the runtime falls back to selecting the
    earlier continuation stub:
    - `parent=3 exit=0`
    - `target=2`
    - `target_exec=3`
    So the first non-stub descendant should not simply be forced into the
    ordinary loop-link path at record time.

- `iter-array-exec-self-jloop-20260328c`
  - Stage: focused native iterator probe
  - Surface: `pairs_array_sum`
  - Host: `kdz`
  - Result: classification only
  - Notes: the `trace 4` loop-desc stub is no longer the wrong execution
    owner by accident. With:
    - `LUAJIT_S390X_BCJMP_LOOPDESC_RESUME=1`
    - `LUAJIT_S390X_BCJMP_LOOPDESC_RESUME_CHILD=1`
    - `LUAJIT_S390X_BCJMP_SELF_JLOOP_RESUME=1`
    - `LUAJIT_S390X_JLOOP_EXEC_RESUME=1`
    the runtime now proves all of:
    - `trace 4` remains the selected target stub
    - `target_exec=3`
    - `trace 3` carries a saved self-`JLOOP` resume contract
      - `exec_resumeop=87`
      - `exec_mcloop=20`
      - `exec_ownerop=85`
    - `trace_exit()` now really returns through that exec-side contract:
      - `phase=resume-linked`
      - `retop=87`
    This is the strongest handoff proof so far, but it is still not a
    landing fix. The workload still times out. So the remaining blocker is no
    longer “wrong stub contract” for `trace 4`, and no longer “cannot reach
    trace 3’s saved loop contract”. The new result says `trace 3` itself is
    still only a tiny pure-loop stub, not the final forward-progress owner.

- `iter-array-pureloop-ladder-first-body-20260328c`
  - Stage: focused native iterator probe
  - Surface: `pairs_array_sum`
  - Host: `kdz`
  - Result: classification only
  - Notes: with the newer exec-side handoff active but
    `LUAJIT_S390X_LINK_LOOP_DESC` disabled, the old pure-loop ladder comes
    back and exposes the first non-stub owner later in the chain. The run
    saves:
    - a long sequence of pure-loop `BC_JMP` stubs
      - `parent=N exit=0`
      - `startop=88`
      - `nins=32772`
      - `szmcode=24`
      - `mcloop=20`
    - followed by the first non-stub continuation trace:
      - `trace=102`
      - `parent=101 exit=0`
      - `startop=88`
      - `nins=32770`
      - `szmcode=76`
      - `mcloop=0`
      - `linktype=6`
    This is the strongest evidence so far that the current
    `LUAJIT_S390X_LINK_LOOP_DESC` collapse is happening too early. `trace 4`
    is not the real stabilized execution owner. The chain needs to be allowed
    to advance past the tiny pure-loop stubs until the first non-stub
    continuation trace appears, and only then should ownership be stabilized.

- `iter-array-bcjmp-mcloop-entry-20260328c`
  - Stage: focused native iterator probe
  - Surface: `pairs_array_sum`
  - Host: `kdz`
  - Result: classification only
  - Notes: direct `mcloop` entry for the self-loop `BC_JMP` stub is not a
    safe landing path. Marking the pure-loop stub (`trace 3`) as directly
    `mcloop`-enterable changes behavior immediately, but under the full chain
    baseline it crashes instead of stabilizing. So the supported fix path is
    not “treat the `BC_JMP` pure-loop stub as a raw external mcode entry ABI”.

- `iter-array-loopdesc-skip-self-20260328c`
  - Stage: focused native iterator probe
  - Surface: `pairs_array_sum`
  - Host: `kdz`
  - Result: classification only
  - Notes: the `loopdesc-child` self-retarget guard is now authoritative on
    `kdz`. The live loop-desc seam repeatedly shows:
    - `target=4`
    - `target_exec=3`
    - `child=4`
    - `phase=loopdesc-child-skip-self`
    - `phase=resume-linked`
    This proves the old self-patch bounce is gone. The continuation still
    times out, but it is no longer because `trace_exit()` keeps rewriting the
    current `BC_JLOOP` back to the same `trace 4` stub.

- `iter-array-loopdesc-resume-contract-20260328b`
  - Stage: focused native iterator probe
  - Surface: `pairs_array_sum`
  - Host: `kdz`
  - Result: classification only
  - Notes: the loop-desc `BC_JMP` stub can now be given a real static-resume
    contract without crashing immediately. With:
    - `LUAJIT_S390X_SKIP_PATCHEXIT_BCJMP_LOOPDESC=1`
    - `LUAJIT_S390X_BCJMP_LOOPDESC_RESUME=1`
    the first stabilized descendant (`trace 4`) saves as:
    - `startop=88` (`BC_JMP`)
    - `resumevalid=1`
    - `resumeop=82` (`BC_ITERL`)
    and root-side `JLOOP` logs then report steady-state selection of:
    - `target=4`
    - `target_resumevalid=1`
    - `target_resumeop=82`
    This removes the old static-`BC_JMP` decode crash, but it is not a
    landing fix. The runtime still times out while repeatedly selecting the
    same `trace 4` stub. So the remaining blocker is no longer “`trace 4`
    lacks a valid resume contract.” It is the handoff after that contract is
    in place.

- `iter-array-recloop-focus-manual-20260328a`
  - Stage: focused native iterator probe
  - Surface: `pairs_array_sum`
  - Host: `kdz`
  - Result: classification only
  - Notes: after the empty-loop backend fix, the pure-`LOOP` ladder is now
    proven to be created by the normal `rec_loop_jit()` decision path, not by
    a bypass around it. In the active continuation family:
    - `trace 3` is the first tiny pure-`LOOP` descendant
    - later descendants (`trace 4`, `5`, `6`, ...) are all recorded from
      `parent>=3 exit=0`
    - the focused log shows the deciding state is stable:
      - `pc == startpc`
      - `startop=88` (`BC_JMP`)
      - `root=1`
      - `framedepth + retdepth == 0`
      - `iter_restart=0`
      - `payload_desc=0`
    That means the ladder is being driven by the generic
    `J->pc == J->startpc` “form extra loop” branch in
    [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c),
    not by iterator-specific restart classifiers.

- `iter-array-link-loop-desc-stoplog-20260328a`
  - Stage: focused native iterator probe
  - Surface: `pairs_array_sum`
  - Host: `kdz`
  - Result: classification only
  - Notes: the existing `LUAJIT_S390X_LINK_LOOP_DESC` hook is now proven to
    fire on the first pure-`LOOP` descendant. For `trace 4` from
    `parent=3 exit=0`:
    - `S390X_RECLOOP ... link_loop_desc=1`
    - `S390X_RECSTOP ... linktype=1 link=1`
    - `S390X_TRACE_META ... linktype=1`
    So the hook really does convert that first descendant from a loop-linked
    extra loop into a root-linked stabilization trace. But that alone is not a
    landing fix:
    - execution still stays on `trace 3`
    - `trace 4` is recorded, but it is not adopted as the runtime owner
    - the branch still times out without `RESULT`
    This narrows the remaining issue again: once the first pure-`LOOP`
    descendant is root-linked correctly, the next blocker is continuation
    ownership/stitching, not `rec_loop_jit()` classification by itself.

- `iter-root-owner-split-kdz-20260328a`
  - Stage: focused native iterator probe
  - Surface: `pairs_array_sum`
  - Host: `kdz`
  - Result: classification only
  - Notes: the root-owner split is now proven on a real JIT-enabled `kdz`
    binary. `trace_exit()` no longer has to fall through
    `phase=dispatch-original` on the recovered root seam:
    - root trace `1` still saves `target_startop=70` (`BC_ITERN`)
    - scratch `ownerop` can be split to `85` (`BC_LOOP`)
    - `S390X_JLOOP_EXIT` then flips from `dispatch-original` to
      `resume-linked`
    That is the first coherent proof that root ownership, not the `0x527`
    guard, is the remaining iterator frontier. But it is not a landing fix:
    resumed root `trace 1` still spins immediately with identical state at
    `parent=1 exit=1`.

- `iter-root-freeze-kdz-20260328a`
  - Stage: focused native iterator probe
  - Surface: `pairs_array_sum`
  - Host: `kdz`
  - Result: classification only
  - Notes: root-freeze logging in
    [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c)
    and
    [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c)
    now proves where the root start contract is lost. On root `trace 1`:
    - `root_before_rec_setup_root` starts at `startins=70` (`BC_ITERN`)
    - early retries in `rec_setup_root()` can normalize to post-`ITERN`
      follow ops (`82..88`)
    - but by `trace_stop_pre_rootpatch` and `trace_save_pre_memcpy`, the
      winning root attempt is back at `startins=70`
    So the iterator root is not failing because birth-time normalization never
    happens. It is failing because the successful root attempt still persists
    the old `ITERN` contract.

- `iter-root-owner-buildgate-kdz-20260328a`
  - Stage: focused native iterator probe
  - Surface: remote build path validation
  - Host: `kdz`
  - Result: classification only
  - Notes: the recent silent `kdz` runs were not iterator evidence. They were
    no-JIT builds. Native s390x JIT requires:
    - `XCFLAGS='-DLUAJIT_ENABLE_S390X_JIT'`
    - an explicit static relink of `src/luajit`
    The authoritative rebuild loop on `kdz` is now:
    - sync the touched source and header files
    - `cd .../repo/src`
    - `make clean`
    - `make XCFLAGS='-DLUAJIT_ENABLE_S390X_JIT' BUILDMODE=static luajit`
    This is now the only trustworthy remote path for the iterator endgame.

- `iter-root-itern-follow-kdz-20260328a`
  - Stage: focused native iterator probe
  - Surface: `pairs_array_sum`
  - Host: `kdz`
  - Result: classification only
  - Notes: the post-`ITERN` follow op used by the winning root trace is not
    stable. Across retries, root `trace 1` sees:
    - `82` `BC_ITERL`
    - `83` `BC_IITERL`
    - `84` `BC_JITERL`
    - `85` `BC_LOOP`
    - `86` `BC_ILOOP`
    - `87` `BC_JLOOP`
    - `88` `BC_JMP`
    - `89` `BC_FUNCF`
    That is why the naive recorder-side birth hook keeps missing the final
    saved root: it was following transient patched bytecode, not a stable root
    ownership contract.

- `iter-array-vload-next-guard-direct-20260327a`
  - Stage: focused native iterator probe
  - Surface: `pairs_array_sum`
  - Host: `kdz`
  - Result: classification only
  - Notes: the direct guard-site probe now observes the real steady-state
    recovered-loop seam on the current array scratch baseline. The hot exit is
    `trace 8 exit 1` with `guardmark=0x527`, and the corresponding
    `vload_next_key_int` site is `trace 8 / curins 7` in the in-loop copy, not
    the earlier preheader copy. The probe fires directly from that site and
    shows:
    - the base pointer is `gl_tmptv`
    - the loaded key qword is `0xffffffffffffffff`
    - the compared low-word lane is `0xffffffff`
    - `tmptv.q0` remains stale non-nil data
    - `tmptv.q1` is nil
    - `tmptv2` is fully nil
    That matches the end-of-iteration contract in
    [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc):
    array-end returns `&tmptv`, leaves the value side stale, and nils the key
    side separately. So the current live array red is no longer bad data,
    preheader re-entry, or a broken compare. It is the in-loop
    `CALLL lj_vm_next -> key-lane nil split` cluster itself.

- `iter-array-loophead-liveness-20260327a`
  - Stage: focused native iterator probe
  - Surface: `pairs_array_sum`
  - Host: `kdz`
  - Result: classification only
  - Notes: the loop-head spill/liveness pass showed the recovered loop anchor
    is not a standalone external entry ABI. Common loop setup in
    [src/lj_asm.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm.c)
    spill-backs the carried helper/index lane and total lane, but the carried
    VLOAD key/value lanes are still register-only at loop entry. That explains
    why direct `sideexit -> mcloop` entry was initially unsafe, and why making
    it safe still did not improve the current collapsed branch: the hot steady
    seam on that branch is the later in-loop `0x527` copy, not the preheader.

- `iter-array-collapsed-guardmark-split-20260327a`
  - Stage: focused native iterator probe
  - Surface: `pairs_array_sum`
  - Host: `kdz`
  - Result: classification only
  - Notes: splitting the two `vload_next_key_int` copies with distinct guard
    markers proved the current steady-state payer on the collapsed array branch
    is the later in-loop copy. The first/preheader copy keeps the `0x427`
    family and the later copy emits the `0x527` family; current hot exits on
    `kdz` report only `0x527`. That rules out preheader re-entry as the main
    remaining bottleneck on the collapsed branch.

- `iterator-r6-preserve-20260325a`
  - Stage: focused native iterator probe
  - Surface: `pairs_array_sum`
  - Host: `kdz`
  - Result: pass
  - Notes: this probe confirmed one real Linux/s390x ABI bug in
    [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc):
    `lj_vm_next` was using saved register `r6` as `NEXT_ARR` and clobbering a
    loop-carried integer live range across the helper boundary. The helper now
    keeps that scratch state off `r6`, and the native probe completed cleanly
    with the expected result again. This is a real backend fix, but it does
    not clear the remaining iterator performance issue by itself.

- `iterator-r6-plus-keyindex-ab-20260325a`
  - Stage: focused native iterator probe
  - Surface: `pairs_array_sum`
  - Host: `kdz`
  - Result: classification only
  - Notes: rerunning the scratch KEYINDEX-only `asm_gencall_sload()`
    suppression after the `r6` helper fix still shifts churn from the old
    pre-call `guardmark=0x508` path to root `exit 2` with `guardmark=0x101`.
    That means the duplicated pre-call KEYINDEX guard is not the landing fix.
    The remaining steady-state red is still on the post-call carried-total
    boundary (`CALLL -> HIOP -> VLOAD -> SLOAD -> ADDOV`), not in KEYINDEX
    restore ownership or helper return-register selection.

- `20260323-openresty-observer-demo`
  - Stage: `product-demo`
  - Surface: `openresty`
  - Host: `kdz`
  - Result: pass with observer mode
  - Notes: the opt-in worker observer path is now wired through
    `S390X_DEMO_TRACE_OBSERVER=1`. `/__jit` reports:
    - `observer_enabled=true`
    - `observer_attach_ok=true`
    - live `trace_start`, `trace_abort`, and `texit` counters
    The latest request-path restamp stayed stable and shut down cleanly, but
    it still showed `start/abort` activity without a committed `stop`, so this
    is a tooling-hardening proof, not a proof that the current OpenResty
    request path commits traces under observer mode on every run.

- `trace-tools-zkd0-20260323c`
  - Stage: `closure`
  - Suite: `trace_tools`
  - Host: `zkd0`
  - Result: pass
  - Notes: second-host debug restamp is green after simplifying the
    `traceinfo_lifecycle` workload away from the older modulo-heavy shape.
    Current authoritative trace-tooling stamps are now:
    - `trace-tools-kdz-20260323a`
    - `trace-tools-zkd0-20260323c`

- `trace-tools-kdz-20260323a`
  - Stage: `closure`
  - Suite: `trace_tools`
  - Host: `kdz`
  - Result: pass
  - Notes: first native staged observer/tooling restamp is green in release
    mode. The lane now validates:
    - `jit.attach("trace")` root-loop stop events
    - `jit.attach("texit")` hot-exit observation
    - `jit.util.traceinfo()` visibility before full flush and disappearance
      after `jit.flush()`
    - repo-root `require("jit.v")`
    - repo-root `require("jit.dump")`
    The local helper layer in `tests/s390x/helpers/testlib.lua` now provides:
    - safe attach/detach wrappers
    - `traceinfo_snapshot()`
    - `assert_trace_stop()`
    - repo-root JIT module path enablement
    This suite is now wired into the driver stage defaults for
    `jit-correctness`, `matrix`, and `closure`.

- `20260323-direct-soak-restamp`
  - Stage: `closure-remediation`
  - Suite: reduced native repros plus `trace_gc_churn`
  - Host: `kdz`
  - Result: pass
  - Notes: the previously failing closure soak surface is green again on the
    authoritative native tree after three targeted fixes:
    - `src/lj_snap.c`
      - restore and replay sunk fresh allocations structurally from store
        edges instead of depending only on `RID_SUNK`
    - `src/lj_asm_s390x.h`
      - guarded `asm_uref` now keeps the closed-upvalue test in a separate
        scratch register instead of clobbering the live upvalue pointer
    - `src/lj_asm_s390x.h`
      - fused dynamic `AREF` base allocation now prefers preserved GPRs so
        live array bases do not get lost across helper `%` calls under
        register pressure
    Direct native `kdz` proofs now pass for:
    - `/tmp/s390x_keep_worker.lua`
    - `/tmp/s390x_mode0_only.lua`
    - `tests/s390x/soak/trace_gc_churn.lua`
    The next full closure restamp is running as `closure-kdz-20260323c`.

- `closure-audit-local-20260323T005100Z`
  - Stage: `closure`
  - Suite: `coverage_audit`
  - Host: local
  - Result: pass
  - Notes: first local closure inventory restamp is green and now emits:
    - `coverage/remaining-stubs.json`
    - `coverage/bc-opcodes.json`
    - `coverage/ir-ops.json`
    - `coverage/vm-handlers.json`
    - `coverage/helper-calls.json`
    - `coverage/report.md`
    The current report explicitly keeps the remaining s390x asm stubs and VM
    NYIs visible so closure is gated on source-backed evidence rather than
    optimism.

- `20260322-openresty-leadership-demo`
  - Stage: `product-demo`
  - Surface: `openresty`
  - Host: `kdz`
  - Result: pass
  - Notes: OpenResty now builds and starts natively on `s390x` against this
    LuaJIT tree; the gateway request path uses Lua policy code plus
    `ffi.C.abs(...)`, and `/__jit` reports enabled JIT with native trace
    activity.

- `20260322-kong-require-probe`
  - Stage: `product-demo`
  - Surface: `kong`
  - Host: `kdz`
  - Result: pass
  - Notes: the earlier `require("kong.cmd.init")` startup crash was narrowed
    and then cleared; staged probes for `resty.openssl.version`,
    `ngx.errlog`, `kong.tools.dns`, `kong.cmd.init`, and
    `kong.cmd.init + collectgarbage()` now pass natively on `s390x`.

- `20260322T234217Z`
  - Stage: `product-demo`
  - Surface: `kong`
  - Host: `kdz`
  - Result: pass with bridge mode
  - Notes: scripted Kong startup now reaches `prepare`, direct nginx start,
    `GET /status`, and `GET /demo` on native `s390x`. The current stable demo
    mode patches nginx init blocks with `require("jit").off()` and runs demo
    workers as `root` because the runtime tree lives under `/root`.

- `20260322-full-jit-kong-startup`
  - Stage: `product-demo`
  - Surface: `kong`
  - Host: `kdz`
  - Result: pass
  - Notes: after the s390x GC64 trace guardrails were added, the full-JIT
    Kong nginx startup path now completes on the current branch. Latest known
    good root:
    `/root/luajit2-s390x/kong-demo-20260323T000006Z`

- `20260318T162505Z`
  - Stage: `interp`
  - Suite: `smoke`
  - Host: `kdz`
  - Result: pass
  - Notes: native build succeeded; smoke confirmed `luajit`, `ffi`, and
    `jit.status()` behavior for the interpreter-only build.

- `20260318T165632Z`
  - Stage: `interp`
  - Suite: `pure_lua`
  - Host: `kdz`
  - Result: fail
  - Notes: interpreter-only `.t` blocks now run natively; the first real
    failure moved into `t/exdata.t`.

- `20260318T173334Z`
  - Stage: `interp`
  - Suite: `pure_lua`
  - Host: `kdz`
  - Result: pass
  - Notes: interpreter-only debug coverage is green after the unwind fix and
    explicit test capability staging.

- `manual-interp-release`
  - Stage: `interp`
  - Suite: `pure_lua`
  - Host: `kdz`
  - Result: pass
  - Notes: release-mode interpreter coverage is also green; the remote TAP log
    shows the full interpreter subset passing on native s390x.

- `ffi-call-matrix-20260318`
  - Stage: `ffi-call`
  - Suite: `ffi_abi`
  - Host: `kdz`
  - Result: pass
  - Notes: outgoing FFI call coverage is green on native s390x for gcc and
    clang in both debug and release builds.

- `20260322T034609.025414Z-p15495`
  - Stage: `matrix`
  - Suite: `smoke`
  - Host: `kdz`
  - Result: pass
  - Notes: hardened tracked-file tar-over-ssh transport restamp; all 8 gcc
    debug smoke variants pass across `jit on/off`, `ffi on/off`, and
    `static/dynamic`.

- `20260322T050327.377254Z-p44209`
  - Stage: `matrix`
  - Suite: `jit_core`
  - Host: `kdz`
  - Result: pass
  - Notes: first widened structured matrix slice is green on gcc debug with
    all 4 `jit on`, `ffi on/off`, `static/dynamic` lanes.

- `20260322T051500.668189Z-p50274`
  - Stage: `matrix`
  - Suite: `jit_loops`
  - Host: `kdz`
  - Result: pass
  - Notes: full gcc debug structured `jit_loops` matrix is green across
    `ffi on/off` and `static/dynamic`.

- `20260322T051917.451476Z-p52584`
  - Stage: `matrix`
  - Suite: `jit_be`
  - Host: `kdz`
  - Result: pass
  - Notes: full gcc debug structured `jit_be` matrix is green across
    `ffi on/off` and `static/dynamic`.

- `20260322T053325.470734Z-p59387`
  - Stage: `matrix`
  - Suite: `soak`
  - Host: `kdz`
  - Result: pass
  - Notes: full gcc debug structured `soak` matrix is green across
    `ffi on/off` and `static/dynamic`.

- `20260322T055509.960285Z-p68367`
  - Stage: `matrix`
  - Suite: `jit_core`
  - Host: `kdz`
  - Result: pass
  - Notes: full clang debug structured `jit_core` matrix is green across
    `ffi on/off` and `static/dynamic`.

- `20260322T055509.960285Z-p68366`
  - Stage: `matrix`
  - Suite: `jit_core`
  - Host: `zkd0`
  - Result: pass
  - Notes: second-host gcc debug structured `jit_core` restamp is green
    across `ffi on/off` and `static/dynamic`.

- `20260322T055912.538970Z-p70902`
  - Stage: `matrix`
  - Suite: `jit_core`
  - Host: `kdz`
  - Result: pass
  - Notes: full gcc release structured `jit_core` matrix is green across
    `ffi on/off` and `static/dynamic`.

- `20260322T060703.342079Z-p75778`
  - Stage: `matrix`
  - Suite: `jit_loops`
  - Host: `zkd0`
  - Result: in progress
  - Notes: second-host gcc debug structured `jit_loops` restamp is the
    current next proof lane.

- `20260322T060703.342267Z-p75779`
  - Stage: `matrix`
  - Suite: `jit_loops`
  - Host: `kdz`
  - Result: in progress
  - Notes: clang debug structured `jit_loops` restamp is running in parallel
    with the second-host lane.

## 2026-03-20 Traced FFI Call Frontier

- Added a focused traced-FFI probe in
  `tests/s390x/jit_core/ffi_call_trace.lua`:
  - `ffi.cdef("int abs(int x);")`
  - tight loop over `ffi.C.abs(...)`
  - `jit.attach("trace")` capture with an explicit requirement that a trace
    reaches `"stop"`
- Fresh native result on `zkd0`:
  - the probe returns the correct Lua-visible total (`547`)
  - it sees a trace `"start"` event
  - it does not see a trace `"stop"` event
  - this means the first traced libc-style FFI call is outside the currently
    green JIT surface even though plain FFI calls are already green
- A more diagnostic native run with the same loop and explicit event dumping
  segfaults on `zkd0`.
  - native batch gdb backtrace:
    - fault PC: `0x000000790000007a`
    - top LuaJIT frame: `lj_vm_exit_handler`
    - unwind path continues through `lua_pcall`, `handle_script`, and `main`
- Current conclusion:
  - the next real frontier is not generic FFI correctness, but traced FFI
    C-call handling plus its trace-exit contract on s390x
  - the first backend gap exposed locally is that `asm_callx` is still missing
    on s390x even though traced foreign calls are expected to lower through
  `CALLXS`
  - after unblocking that lowering, the next likely issue is the exit-state
    contract around traced foreign calls if the native crash persists

## 2026-03-22 Kong Runtime Remediation Loop

- The product-shaped runtime work split into two distinct problems:
  - a JIT backend `RID_SP` leak that let `r15` act like a value register
  - a later startup-time interpreter crash during Kong module load
- The `RID_SP` problem was materially reduced by hardening:
  - allocator selection
  - rename/inheritance paths
  - snapshot restore
  - temp pseudo-register handling
- The interpreter-side startup crash was then contained by routing the risky
  GC64 big-endian string-key/global fast paths through the generic helpers in
  `vm_s390x.dasc`.
- After that change:
  - `require("kong.cmd.init")` is stable
  - `require("kong.cmd.init"); collectgarbage()` is stable
- The remaining full-JIT startup frontier is now later in the runtime:
  - a startup-time trace GC crash while Kong nginx `init_by_lua` is running
  - one observed symptom was bogus gray-list entries that looked like Lua
    source text rather than real `GCtrace` or other GC objects
- Current in-tree mitigation:
  - validate trace GC roots and `IR_KGC` payloads before committing a trace on
    `s390x` GC64
  - defensively sanitize malformed trace references and malformed `IR_KGC`
    objects during GC traversal instead of allowing a crash path to proceed
- Current result after the rerun:
  - the full-JIT Kong startup demo path now passes on `kdz`
  - these trace guardrails should remain under regression watch while wider
    matrix and product demos continue

## 2026-03-23 Closure Soak Remediation

- The last known `closure/soak` blocker was narrowed away from the original raw
  `ASTORE` crash into three separate correctness bugs:
  - fresh-table unsink and replay on exit
  - guarded upvalue load aliasing in `asm_uref`
  - volatile fused-array-base loss across `%` helper calls under pressure
- The reduced repro chain on native `kdz` was:
  - fresh table store only:
    - green after the earlier `asm_tvstore64x()` scratch exclusion fix
  - `keep + item.value`:
    - green after the `src/lj_snap.c` sunk-allocation restore/replay fix
  - `keep + worker(item.value)`:
    - green after the guarded `asm_uref` alias fix
  - mixed `{ tag = ..., value = ... }` table plus stored worker call:
    - green after preferring preserved registers for the fused dynamic array
      base path in `src/lj_asm_s390x.h`
- The direct native `trace_gc_churn.lua` restamp is now green again on the
  authoritative `kdz` worktree.
- Closure remains gated on the full structured `closure` rerun and the required
  `zkd0` spot check, but the front-most soak runtime crash is no longer open.

## 2026-03-20 Traced FFI Direct-Call Fix

- Native `zkd0` diagnosis after the first `CALLXS` slice showed the direct
  traced-FFI crash was not another exit-handler bug.
  - plain `ffi.C.abs` tracing segfaulted with a bogus PC
    `0xfffffffff7b4a4c0`
  - the actual native `ffi.C.abs` pointer is
    `0x000003ff9c44a4c0`
  - the trace return address in gdb was a JIT mcode PC near `0x314c...`
- Root cause:
  - s390x `asm_callx` was still forcing the foreign target through the direct
    `BRASL` path
  - native libc function pointers on IBM Z live far outside the relative
    branch range from JIT mcode
  - with assertions compiled out, the relative displacement truncated and the
    trace branched to garbage
- Fixes applied locally:
  - `src/lj_emit_s390x.h`
    - added `BASR` encoding support
    - added `emit_callr()` for indirect traced calls
  - `src/lj_asm_s390x.h`
    - `asm_gencall()` no longer requires a direct target when the caller has
      already emitted an indirect call
    - `asm_callx()` now selects:
      - direct `BRASL` only when the target is actually in range
      - indirect `BASR` via a materialized GPR otherwise
      - indirect `BASR` for non-constant callable refs
- Native outcome on `zkd0` after rebuild:
  - plain traced FFI loop over `ffi.C.abs` is green:
    - `/tmp/ffi_call_plain.lua` prints `total 131`
    - exit status `0`
  - the staged traced-FFI probe is green:
    - `tests/s390x/jit_core/ffi_call_trace.lua`
    - exit status `0`

## 2026-03-20 Traced FFI Follow-Up

- Added a second probe in `tests/s390x/jit_core/ffi_ptr_call_trace.lua` that
  calls through a stored function value:
  - `local cabs = ffi.C.abs`
  - traced loop over `cabs(...)`
- Current native `zkd0` result:
  - correct Lua-visible total
  - trace `"start"` observed
  - no trace `"stop"`
  - clean failure instead of a crash
- Current interpretation:
  - the direct-call traced FFI crash is fixed
  - the stored-function-value case is a narrower follow-up and should be
    checked against a non-s390x control before treating it as a new
    architecture-specific blocker

## 2026-03-20 Traced FFI Stored-Function-Value Resolution

- The stored-function-value traced-FFI follow-up is now green on native
  `zkd0`.
- Root cause:
  - the failing trace was aborting with `NYI: cannot assemble IR instruction
    69`
  - `IR 69` is `IR_FLOAD`
  - the recorder emits this early in the stored-function-value path as:
    - `u16 FLOAD cdata.ctypeid`
    - followed by `p64 FLOAD cdata.ptr`
  - s390x `asm_fload()` still only accepted `int/u32/addr` and rejected the
    recorder's `u16` field load
- Local backend fix:
  - `src/lj_asm_s390x.h`
    - `asm_fload()` now handles:
      - `U8`
      - `U16`
      - sign-extended `I8`
      - sign-extended `I16`
      - GC64 pointer and GC-reference field loads
    - the byte and halfword cases currently use a 64-bit load plus BE shift
      extraction, which matches the active s390x target contract
- Native outcome on `zkd0` after rebuild:
  - `/tmp/ffi_ptr_abort_check.lua`
    - `total 547`
    - `start yes`
    - `stop yes`
    - `abort no`
    - exit status `0`
  - `tests/s390x/jit_core/ffi_ptr_call_trace.lua`
    - exit status `0`
  - `-jdump=im /tmp/ffi_ptr_plain.lua`
    - trace now commits and stops at `-> loop`
    - no `IR 69` abort remains

## 2026-03-20 Trace-Event Reporting Follow-Up

- After the stored-function-value FFI trace fix, the old verbose event-dump
  reproducer still crashes on native `zkd0`:
  - `/tmp/ffi_ptr_call_diag.lua`
  - prints `total 131`
  - then segfaults with exit status `139`
- The same script is clean on a local x86_64 control build of this branch:
  - prints `total 131`
  - prints both captured `start` and `stop` trace events
  - exits `0`
- Current interpretation:
  - this is no longer an FFI trace-assembly failure
  - it initially looked like a narrower s390x-specific problem in the
    trace-event reporting or event-value materialization path
- Next action:
  - isolate which trace event argument becomes invalid on native s390x before
    broadening the event callback machinery

## 2026-03-20 Trace-Event Post-Processing Loop Crash

- Follow-up native probes on `zkd0` show the event payload itself is not
  corrupted:
  - `arg1` and `arg2` print cleanly for both `start` and `stop`
  - `type(ev[3])` is `function`
  - `tostring(ev[3])` is stable
  - the optional `ev[4]` on the `start` event is also stable and prints `32`
- The crash only reproduces with the generic post-processing loop:
  - `for j = 1, ev.n do print('idx', j, tostring(ev[j])) end`
  - native output reaches:
    - `idx 1 start`
    - `idx 2 1`
    - `idx 3 function: ...`
    - then segfaults before `idx 4`
- Critical split:
  - adding `jit.off()` immediately after `cap.stop()` makes the exact same
    script run cleanly to completion on native `zkd0`
  - this means the remaining crash is not in the event payload itself
  - it is a new s390x JIT bug in the post-capture loop that walks the event
    tables, not in the earlier traced FFI call path
- Additional evidence:
  - the simpler probes that access `ev[4]` directly remain green
  - a local x86_64 control build also runs the full event walk cleanly

## 2026-03-20 Trace-Exit Stack Re-anchoring Fix

- Native `zkd0` gdb runs on the mixed-type `tostring()` post-trace loop showed
  a deeper s390x VM contract bug, not bad event payloads:
  - `cont_stitch` was running with `%r15 == cframe_raw(L->cframe) + 8`
  - `vm_exit_interp` later entered with `%r15 == cframe_raw(L->cframe) + 16`
  - any path that reloaded `SAVE_L` or other `SAVE_*` slots from `sp` in that
    state read garbage, which then poisoned `J->L`, `L->base`, and later
    interpreter dispatch
- Concrete native evidence:
  - at `lj_cont_stitch`, `DISPATCH_J(L)` and `DISPATCH_GL(cur_L)` still held the
    real `lua_State *`, but `SAVE_L`, `SAVE_PC`, and `SAVE_CFRAME` were garbage
    because `%r15` was skewed
  - after widening that fix, the next native crash moved into
    `lj_vm_exit_interp`, confirming the same stack-slot problem at exit resume
- Local fixes in `src/vm_s390x.dasc`:
  - `cont_stitch` now uses `DISPATCH_J(L)` instead of `SAVE_L`
  - the special `vm_record` / `vm_rethook` / `vm_inshook` dispatch path now
    reloads `cur_L` from dispatch instead of `SAVE_L`
  - `vm_exit_interp` now re-anchors `sp` from `cframe_raw(J->L->cframe)` before
    touching any `SAVE_*` slots
- Native outcome on `zkd0` after rebuild:
  - the original no-print reproducer now exits `0`:
    - `/tmp/tostring_type_matrix_noprint.lua`
  - the repo-local trace-event regression now also exits `0`:
    - `tests/s390x/jit_core/trace_event_postloop.lua`
  - the event walk prints both `start` and `stop` payloads cleanly on native
    s390x
- Added a focused repo-local regression for the simpler no-print loop:
  - `tests/s390x/jit_core/tostring_type_matrix.lua`
- Current interpretation:
  - the next active blocker in this area is a traced mixed-table/array loop
    over the captured event tables, likely involving the traced `ev[j]` access
    and/or the `tostring(ev[j])` call path
  - the next debugging pass should focus on trace shape for the post-capture
    loop rather than the trace-event callback machinery itself

## 2026-03-20 Bitops JIT Follow-Up

- Added two more focused repo-local JIT probes:
  - `tests/s390x/jit_core/bitops_trace.lua`
  - `tests/s390x/jit_core/ffi_cdata_trace.lua`
- Native `kdz` initially showed the bitops probe returning the correct
  Lua-visible total but aborting before any `"stop"` event:
  - first `jit.attach("trace")` abort payload:
    - error code `32` (`LJ_TRERR_NYIIR`)
    - `errinfo 35` (`IR_BXOR`)
- Local/remote backend bring-up since then:
  - added s390x bitwise lowering for:
    - `BNOT`
    - `BSWAP`
    - `BAND`
    - `BOR`
    - `BXOR`
    - `BSHL`
    - `BSHR`
    - `BSAR`
    - `BROL`
    - `BROR`
  - added the first integer `NEG` lowering
  - added the first minimal `UREFO` / `UREFC` lowering plus `LLGC`
    byte-load support for guarded open/closed upvalue checks
- Native progression on `kdz`:
  - after the bitwise lowering slice:
    - the front-most abort moved from `IR_BXOR` to `IR_NEG`
  - after `NEG` lowering:
    - stripped native bitops loops moved to `IR_UREFO`
  - after `UREFO` lowering:
    - stripped native bitops loops now record and reach
      `TRACE ... stop -> loop`
- Important split discovered during native validation:
  - the stripped inline bitops loop with an explicit `bit.tobit(x)` at the end
    of `mix(i)` is now correct on `kdz`
  - the repo-local `bitops_trace.lua` still fails because it returns raw `x`
    and only applies `bit.tobit()` at the outer accumulator
- Current front-most bug:
  - this is no longer a generic bitops NYI
  - it is a wrong-result issue in the raw-return bitops closure path
  - the first concrete native mismatch is:
    - `mix(6)`
    - expected `167772400`
    - got `-874411914`
  - the earliest narrowed stage is the raw-return form of:
    - `x = bit.bxor(x, bit.lshift(i, 3))`
- Current interpretation:
  - the remaining backend issue is likely a 32-bit normalization or register
    reuse problem in the early `BSHL` / `BXOR` path when the result stays
    unnormalized across subsequent ops
  - the next remediation cut should stay on that exact path before widening
    into other JIT surfaces again

## Resolved Interpreter Issue

- Symptom: uncaught runtime errors from a script file printed the correct
  stderr text but exited with status `0` on native s390x, while the `-e` form
  exited nonzero.
- Root cause: s390x unwind/EH return-value handling was wrong. GCC delivers EH
  return data in `r6` on this platform, while the tree still treated the
  return-value register contract as if it were `r14`.
- Fixes applied:
  - `src/lj_arch.h` now uses `LJ_TARGET_EHRETREG 6` and `LJ_TARGET_EHRAREG 14`.
  - `src/vm_s390x.dasc` now moves the unwind status through `r6` and returns it
    to C in `r2`.
- Outcome: interpreter-only `.t` coverage now passes natively in both debug and
  release runs.

## FFI ABI Resolution

- First failing native run:
  - `ffi-call-gcc-debug`
  - failing check: `small_u8: expected 17, got 0`
- Native ABI proof:
  - GCC-generated s390x assembly for
    `small_u8 echo_small_u8(small_u8 value)` shows:
    - hidden result pointer in `r2`,
    - by-value struct argument in `r3`,
    - the one-byte payload occupying the low bits of `r3`.
- Resolution:
  - `src/lj_ccall.c` now right-justifies by-value structs smaller than one
    pointer-width slot on s390x big-endian.
  - `src/Makefile` now disables clang's integrated assembler for s390x target
    builds.
  - `src/lj_mcode.c` now uses `__builtin___clear_cache`, which fixes the clang
    build path on s390x.
- Outcome:
  - `ffi_abi` now passes natively on `kdz` for gcc debug, gcc release, clang
    debug, and clang release.

## Callback Contract Correction

- The initial `tests/s390x/callbacks/run.lua` tried to cast a Lua function to a
  callback type returning `struct pair_value`.
- A local non-s390x control run with a supported LuaJIT build fails before any
  s390x code is involved:
  - `cannot convert 'function' to 'struct pair_value (*)()'`
- Implication:
  - aggregate callback returns are not currently part of the generic callback
    contract in this tree,
  - treating that case as an s390x callback gate would create a false
    architecture-specific failure.
- Current correction:
  - the callback regression suite now focuses on currently supported callback
    forms: scalar integer returns, stack-boundary integer arguments, mixed
    integer and FP arguments, and 64-bit integer arguments and returns.
- Follow-up:
  - if aggregate callback returns are needed, they should be added as a generic
    LuaJIT feature extension with cross-architecture validation, not as an
    s390x-only workaround.

## Callback-Unwind Progress

- First native callback blocker:
  - `callback-unwind-callbacks-20260318`
  - failure: `./src/luajit: tests/s390x/callbacks/run.lua:22: too many callbacks`
  - root cause: `CALLBACK_MAX_SLOT == 0` for s390x, so callbacks were still in
    the generic unsupported-architecture path.

- Second native blocker:
  - `callback-unwind-callbacks-20260318-v2`
  - failure: parallel native build raced `lj_profile.c` against generated
    `luajit.h`
  - root cause: clean parallel builds could start target object compilation
    before generated headers existed.
  - fix: `src/Makefile` now gives `$(LJVMCORE_O)` and `$(LUAJIT_O)` an
    order-only prerequisite on `$(ALL_HDRGEN)`.

- Third native blocker:
  - `callback-unwind-callbacks-20260318-v3`
  - failure: `lib_jit.c` could not build on interpreter-only s390x because the
    generated `jit.util` registration still referenced trace-only entry points.
  - root cause: the no-JIT target build still needed a stable `jit.util`
    surface during `lib_jit.c` compilation.
  - fix: `src/lib_jit.c` now provides no-op `jit.util.trace*` stubs when
    `LJ_HASJIT == 0`.

- Fourth native blocker:
  - `callback-unwind-callbacks-20260318-v4`
  - direct native callback probe on `kdz` still failed with
    `call_sum6: expected 21, got 2`
  - isolated behavior:
    - a single `sum6` callback in a fresh Lua state worked and received all six
      integer arguments correctly,
    - after allocating one earlier callback, the second callback pointer still
      invoked the first callback body.
  - first correction:
    - `src/lj_ccallback.c` now uses an 8-byte per-slot trampoline on s390x:
      `LGHI r1, slot` plus `BRAS r0, common`.
    - this removed the earlier return-address-derived slot recovery and made
      the slot number explicit in `r1`.
  - remaining root cause:
    - a native gdb probe on the rebuilt image showed `r1` arriving at
      `lj_vm_ffi_callback` as `0` for the first callback and `1` for the
      second callback, so the trampoline itself was correct,
    - a second probe at `callback_conv_args` still showed `cts->cb.slot == 0`
      for both callbacks,
    - `CCallback.slot` is a 32-bit `MSize`, but `vm_s390x.dasc` was storing it
      with `stg`, a 64-bit store; on big-endian s390x that wrote the low
      32-bit slot value outside the field and left the actual `slot` field as
      zero.
  - fix:
    - `src/vm_s390x.dasc` now stores `CTSTATE->cb.slot` with `st`, matching the
      field width and preserving nonzero callback slots on big-endian s390x.

- Fifth native blocker:
  - `callback-unwind-callbacks-20260318-release`
  - failure: release link failed with undefined references to `lua_assert`
    from the s390x callback trampoline emitter.
  - root cause: `src/lj_ccallback.c` used `lua_assert()` in callback mcode
    setup paths. In the non-assert release build that did not resolve as a
    safe macro in this translation unit and became an unresolved symbol.
  - fix:
    - `src/lj_ccallback.c` now uses `lj_assertX(...)` for the callback mcode
      size and overflow checks.

- Current native status:
  - `callback-unwind-callbacks-20260318-v5`
    - gcc debug callback coverage passes natively on `kdz`
  - `callback-unwind-callbacks-20260318-release-v2`
    - gcc release callback coverage passes natively on `kdz`
  - direct debug validation on `kdz` passes the callback regression in
    `tests/s390x/callbacks/run.lua`
  - that regression now covers:
    - scalar integer callbacks,
    - recursive callbacks,
    - stack-boundary integer callbacks,
    - mixed FP and integer callbacks,
    - 64-bit integer callbacks,
    - callback execution under `debug.sethook`,
    - callback error propagation through `pcall`,
    - bad callback return conversion propagation.
  - the callback-unwind stage is now green for the native gcc debug and gcc
    release variants; the wider compiler and host matrix still belongs to the
    later matrix stage.

## Hot-Exit And Dynamic-Key Trace Shape

- Native and local hotloop parity is established for the current dynamic-key
  build-loop probe:
  - the root trace starts at iteration `56` on local x86_64 and on native
    s390x `kdz`
  - this rules out a hotcount or loop-entry skew as the cause of the trace
    divergence

- Pure interpreter state is correct on native s390x for the same probe:
  - after the warmup iterations, the runtime key produced by `'a'..16`
    compares equal to the canonical `'a16'`
  - `rawequal(k, "a16")` is true
  - `t[k]` resolves to the existing `t["a16"]` value
  - this rules out a generic string or table semantics bug in the interpreter

- The old forced side-trace crash under `hotexit=1` was not a runtime
  execution bug. A native gdb batch run on `kdz` showed the crash in
  `ra_restore()` during `asm_tail_link()`, while trying to materialize
  `REF_BASE` for side-trace assembly.
  - direct symptom:
    - `ra_allocref(... ref=32768, allow=8192)` where `32768 == REF_BASE`
  - root cause:
    - s390x still treated `RID_BASE` as permanently fixed, but side-trace
      linking needs to materialize `REF_BASE` into `RID_BASE`
  - fix:
    - `src/lj_target_s390x.h` now leaves `RID_BASE` allocatable and documents
      that contract explicitly
  - outcome:
    - the same native `hotexit=1` probe now records and stops trace 2 cleanly
      instead of crashing:
      - `TRACE_start iter=56 tr=1`
      - `TRACE_stop iter=57 tr=1`
      - `TRACE_start iter=100 tr=2 otr=1 oex=3`
      - `TRACE_stop iter=100 tr=2`

- The remaining blocker is now narrower and sits in the recorder-side
  dynamic-string table-store path, but the probe split is narrower than the
  earlier generic hot-exit diagnosis.
  - `tests/s390x/jit_loops/hotexit_update_trace.lua` is now the canonical mixed
    insert-to-update repro:
    - local x86_64 forms:
      - trace 1: miss-path root loop
      - trace 2: update-path root trace (`otr=1`, `oex=2`)
      - trace 3: update-path loop trace (`otr=1`, `oex=0`)
      - trace 4: final stitch
    - native s390x on `kdz` still forms an endless `oex=0` loop-trace chain
      after the initial miss-path root trace
  - `tests/s390x/jit_loops/hotexit_update_only.lua` now isolates the pure
    update path by prefilling under `jit.off()` and only tracing the update
    loop:
    - local x86_64: one root loop plus one final stitch trace
    - native s390x: same trace shape and correct final values
  - implication:
    - the remaining native problem is not a generic side-trace or update-loop
      failure
    - it is specifically the transition from the initial miss-path trace into
      later update iterations in the mixed cyclic string-key loop
  - next diagnostic cut:
    - use an opt-in recorder probe in `src/lj_record.c`, gated by
      `LUAJIT_S390X_RECIDX_LOG`, to log `parent`, `exitno`, `xrefop`, and
      whether `oldv` is nil for string-key stores during the mixed repro
    - this should answer whether native s390x is still seeing a true nil lookup
      at side-trace start or whether the divergence happens after lookup

## Next Targets

- Start `jit-bringup` with the remaining s390x target metadata and shared JIT
  table fixes that are prerequisite to removing `LJ_ARCH_NOJIT`.
- Add a callback-specific unwind regression that throws from the callback body
  under nested `pcall` or `xpcall` and confirm the saved `cframe` chain remains
  stable.
- Once callback-unwind is green in the harness, move to `jit-bringup` with the
  existing order:
  - dispatch and `vm_next`,
  - trace recording entry,
  - exit handling and snapshot restore,
  - stitch and re-entry,
  - `JLOOP`, `JFOR*`, `JITER*`,
  - compiled varargs,
  - profiler hooks,
  - FFI callback return integration for JIT-enabled paths.

## JIT Bring-Up Progress

- First native JIT blocker after enabling the s390x backend surface:
  - `t/isarr-jit.t` aborted with `NYI: cannot assemble IR instruction 2`
  - `2` is `IR_LE`
  - result: integer compare and guard emission had to be implemented before
    any table.isarray loop trace could assemble.

- Native progression on `kdz` after incremental backend work:
  - `IR_LE` fixed, then the first blocker moved to `IR_ADD`
  - `IR_ADD` fixed, then the first blocker moved to generic helper calls
    (`asm_gencall`, tagged as `-108`)
  - minimal fixed-helper call lowering for direct integer/pointer helpers in
    `r2` through `r6` fixed that blocker
  - the next blocker moved into loop closure (`asm_tail_prep`,
    `asm_loop_fixup`, `asm_loop_tail_fixup`, and root `BASE` coalescing)
  - after those trace-structure fixes, the next real IR blocker was `71`
    (`IR_SLOAD`) and `66` (`IR_ALOAD`) on the ninth test.

## JIT Exit and Early Memory-Reference Progress

- Native remediation work on `kdz` then shifted from new IR lowerers back to
  the trace exit path:
  - exit stubs now carry both `exitno` and `traceno`
  - `src/vm_s390x.dasc` now has working `vm_exit_handler` and
    `vm_exit_interp` paths instead of the earlier traps
  - the original native crash in `lj_vm_exit_handler` was a real s390x
    addressing bug: the handler used `r0` as an address base when saving the
    original stack pointer, but `r0` cannot serve as a base register in s390x
    memory operands
  - fix: the handler now keeps the original stack pointer in `r3` and stores
    it into `ExitState.gpr[RID_SP]` from there.

- First post-exit regression:
  - `tests/s390x/jit_core/basic_trace.lua` no longer crashed, but computed
    `200` instead of the expected `20100`
  - native trace disassembly on `kdz` showed `asm_add()` was emitting the move
    and add instructions in the wrong order for a backend that writes machine
    code backwards
  - fix: `src/lj_asm_s390x.h` now emits the arithmetic operation before the
    optional move, so the executed instruction order is `move` then `add`.

- Trace-exit validation result:
  - `tests/s390x/jit_core/basic_trace.lua` now passes natively on `kdz`
  - `tests/s390x/jit_core/side_exit.lua` also passes natively on `kdz`
  - this confirms the current exit path can:
    - enter a root loop trace,
    - take a side exit,
    - run `lj_trace_exit`,
    - assemble the side trace,
    - and resume in the interpreter without crashing.

- Root cause of the former helper-call crash in `side_exit.lua`:
  - native trace disassembly showed the trace moved `BASE` from `r13` into
    `r1`, called `lj_vm_modi`, and then dereferenced through `r1`
  - the s390 psABI marks `r0` and `r1` plus `r2` through `r5` as volatile
    across calls, so using `r1` for a live `REF_BASE` value across helper calls
    was incorrect
  - fix:
    - `src/lj_target_s390x.h` now declares the call-preserved GPR set and a
      narrower `RSET_GPR_BASE` set for non-argument local registers
    - `src/lj_asm_s390x.h` now allocates `REF_BASE` from that preserved set via
      `ra_allocbase()`, which keeps trace base values out of volatile helper
      call registers.

- Second post-exit blocker:
  - side-trace assembly then failed in `asm_tail_link()` because
    `emit_loadu64()` still only handled 16-bit immediates
  - fix:
    - `src/lj_emit_s390x.h` now materializes full 64-bit immediates with a real
      RIL path using `LLILF` plus `IIHF`
    - this unblocks 64-bit `LPC` materialization for trace links and is the
      correct basis for later pointer and constant loads.

- First post-exit memory-reference slice:
  - `src/lj_asm_s390x.h` now has a minimal BE/GC64-safe implementation for:
    - `asm_aref`
    - `asm_ahuvload`
    - `asm_fload`
  - result:
    - `t/isarr-jit.t` test 9 moved from `IR_ALOAD` (`66`) to `IR_FLOAD`
      (`69`), then to green after the minimal `FLOAD` field load support was
      added.

- Current native JIT status on `kdz` as of `2026-03-19T16:03:53Z`:
  - green:
    - `tests/s390x/jit_core/basic_trace.lua`
    - `tests/s390x/jit_core/side_exit.lua`
    - `t/isarr-jit.t` tests 1-4 and 7-9
  - remaining visible JIT gap:
    - `t/isarr-jit.t` tests 5 and 6 still record an extra linked trace
      (`[TRACE   2 (1/0) test.lua:6 loop]`) where the expected output only
      records the root loop trace
    - semantics are correct, but the trace shape still differs from mature
      backends.

- Current native `SLOAD` slice:
  - `src/lj_emit_s390x.h` now has verified encodings for:
    - `lg`
    - `llgf`
    - `lgfr`
    - `srag`
    - `sllg`
    - `srlg`
  - `src/lj_asm_s390x.h` now lowers a narrow GC64 big-endian `SLOAD` subset:
    - root-trace stack loads only
    - no `IRSLOAD_PARENT`
    - no `IRSLOAD_CONVERT`
    - integer and GC-pointer/address loads only
    - type checks for those same cases using the GC64 high-tag layout

- Native result after the `SLOAD` implementation:
  - `t/isarr-jit.t` no longer aborts during trace assembly for tests 1 through
    8
  - those tests compile a trace and then fail at runtime instead of compile
    time
  - test 9 still reaches `IR_ALOAD`, which remains unimplemented.

- Runtime JIT entry and exit findings:
  - initial native `gdb` run on `kdz` crashed in `lj_BC_JLOOP`
  - root cause: `BC_JLOOP` in `src/vm_s390x.dasc` was still a null-store stub
  - fix:
    - `BC_JLOOP` now loads the `GCtrace *` from `jit_State.trace`,
      stores `jit_base` and `tmpbuf.L`, clears `vmstate`, and branches to the
      trace `mcode` entry like the mature backends do
  - second native `gdb` run on `kdz` then moved the crash site to
    `lj_vm_exit_handler`
  - root cause:
    - trace entry is now happening,
    - but `vm_exit_handler` and `vm_exit_interp` in
      `src/vm_s390x.dasc` are still architecture stubs.

- Current JIT status:
  - s390x native JIT is now past the first assembler and dispatch barriers
  - root loop traces can be assembled and entered for the `table.isarray`
    coverage
  - the next hard blocker is no longer IR lowering for tests 1 through 8
  - the next hard blocker is the trace-exit path:
    - `vm_exit_handler`
    - `vm_exit_interp`
  - after that, `IR_ALOAD` is the next concrete missing lowerer already proven
    by test 9.

## Immediate JIT Next Actions

- Port `vm_exit_handler` and `vm_exit_interp` from a mature 64-bit backend
  into `src/vm_s390x.dasc`, adapting:
  - exit-state save order,
  - exit-number calculation,
  - `jit_base` and `SAVE_L` restore,
  - the `lj_trace_exit` call contract,
  - resume into the interpreter after side exit.
- Re-run the direct `gdb` loop script on `kdz` until the crash site moves
  past `lj_vm_exit_handler`.
- Once exit handling works, rerun `t/isarr-jit.t`:
  - tests 1 through 8 should either pass or expose the next runtime handoff
    issue,
  - test 9 should still expose the first missing memory-reference lowerer,
    `IR_ALOAD`.

## 2026-03-19 Root-Loop Follow-Up

- Native `kdz` work after the exit-path bring-up moved the front-most blocker
  from trace entry and exit into helper-call lowering for root-loop traces.
- The narrowed reproducer is:
  - `./src/luajit -jv /tmp/isarr5_addr.lua`
  - loop body: repeated `table.isarray(t)` on an invariant table.

- Confirmed sequence:
  - the original crash was a traced helper-call argument bug:
    - the invariant table argument was allowed to live in `r2`,
    - the first `lj_tab_isarray()` call clobbered `r2`,
    - the second in-loop helper call reused that dead register value.
  - the earlier `ra_rematk: rematk of K001 has no reg` assert was a separate
    allocator-state bug caused by poisoning outgoing arg GPRs with `ASMREF_L`
    on s390x, where `emit_getgl()` is still stubbed and `ASMREF_L`
    rematerialization is not backend-safe yet.

- Current backend state:
  - helper-call argument setup in `src/lj_asm_s390x.h` no longer permanently
    binds the invariant table ref to `r2`.
  - native disassembly now shows explicit per-call argument reloads instead of
    a permanently pinned arg register.
  - the remaining crash moved one level deeper:
    - the per-call `SLOAD` reload path still depends on a `BASE` copy that is
      later reused as a loop temporary,
    - the loop backedge then re-enters the call reload sequence with the wrong
      base register contents.

- Native evidence from `kdz`:
  - one intermediate trace shape:
    - invariant table moved into `r1`, copied to `r2` before each call,
      but `r1` was still caller-clobbered, so the second call still failed.
  - latest trace shape:
    - each call site now emits its own `SLOAD`-derived reload into `r2`,
      but the loop body later reuses the copied `BASE` register as the loop
      limit temporary,
      so the second iteration faults on the next reload from the wrong base.
  - direct `gdb` evidence shows the fault has moved from
    `lj_tab_isarray(src=0x0)` to the traced reload sequence itself:
    - faulting PC in one native run:
      - `0x571eff48`
    - faulting instruction:
      - `lg %r2,32(%r11)`
    - immediate cause:
      - `%r11` no longer holds `BASE` after being reused for the loop limit.

- Clean conclusion:
  - the next hard blocker is no longer the helper-call arg lane itself.
  - the next hard blocker is `REF_BASE` / base-register liveness for looped
    traced reloads.
  - fixing this correctly likely requires one of:
    - a real s390x `emit_getgl()` / global reload path so traced code can
      rematerialize `jit_base` / `cur_L` instead of depending on stale copies,
    - or a stricter backend rule that keeps the chosen `BASE` home out of the
      general temporary pool across looped helper-call reload paths.

- Current native test impact:
  - `t/isarr-jit.t` still shows tests 1-4 and 7-8 green in the current branch
    state,
  - tests 5, 6, and 9 still fail because the crashing root-loop reproducer has
    not fully stabilized yet,
  - the failure mode is now reproducible, narrow, and documented enough to
    drive the next remediation step.

## 2026-03-19 JIT Core Gate Recovery

- The root-loop `REF_BASE` / helper-call reload issue is no longer the
  front-most blocker.
- Native fixes after that point were narrower runtime/backend contract work:
  - `src/vm_s390x.dasc` now places `ExitState.spill[]` over the live on-trace
    spill area by allocating only `EXITSTATE_SPILL_BASE` bytes below the trace
    stack pointer
  - `src/lj_emit_s390x.h` now has real generic spill load/store lowering for
    integer and address GPR values
  - `src/lj_asm_s390x.h` now has a minimal integer snapshot tail restore path
    for side traces
  - `src/lj_asm_s390x.h` now lowers integer `IR_MULOV`
  - `src/lj_asm_s390x.h` now has working non-loop `asm_tail_fixup()` code for
    side-trace exit/link branches and stack adjustment

- Native evidence for the spill fix:
  - direct `gdb` on `kdz` showed the loop-carried values used by
    `tests/s390x/jit_core/side_exit.lua` lived in spill slots, but the old
    s390x backend left both generic spill accessors as no-ops
  - at the first `lj_snap_restore()` breakpoint:
    - `T->ir[5].prev == 0xa714` and `T->ir[6].prev == 0xc050`
    - those decode to spill slots `167` and `192`
    - `ex->spill[167]` and `ex->spill[192]` contained instruction words, not
      Lua values, which proved the old spill path was reading garbage
  - after wiring spill load/store plus the `ExitState.spill[]` alias, the
    semantic wrong-result in `side_exit.lua` disappeared.

- Native side-trace progression on `kdz`:
  - before the new work:
    - `tests/s390x/jit_core/side_exit.lua` finished with the wrong total
      (`186`)
    - side-trace attempts aborted at `-107` (`asm_stack_restore`)
  - after spill, restore, `MULOV`, and tail-fixup work:
    - direct native run of
      `tests/s390x/jit_core/side_exit.lua` now passes
    - `./src/luajit -jv tests/s390x/jit_core/side_exit.lua` now shows:
      - `[TRACE   1 side_exit.lua:8 loop]`
      - `[TRACE   2 (1/5) side_exit.lua:11 -> 1]`
    - this confirms side-trace recording, assembly, link patching, and
      interpreter-visible semantics are working for the current integer loop.

- Structured harness validation:
  - native staged run:
    - [artifacts/s390x/20260319T121216Z/summary.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/20260319T121216Z/summary.md)
  - result:
    - stage `jit-bringup`
    - suite `jit_core`
    - host `kdz`
    - success `true`
  - this is the first green structured `jit_core` gate after the root-loop and
    side-exit fixes landed together.

- Current front-most blocker after the green `jit_core` gate:
  - the next exposed backend gap is no longer in the `side_exit.lua` loop
    itself
  - the first visible new miss is `IR_HREFK` (`57`), hit in the JIT-enabled
    error/testlib path:
    - `[TRACE --- (1/7) side_exit.lua:16 -- NYI: cannot assemble IR instruction 57]`
- the planned next loop is:
  - rerun `jit_core` in gcc release
  - run `jit_loops` in gcc debug
  - then start the next lowering slice at `IR_HREFK` if that remains the
      first blocker

## 2026-03-19 Harness Hardening and Loop-Gate Rebaseline

- The staged harness was hardened locally to keep concurrent bring-up runs
  trustworthy:
  - `tools/s390x/driver.py` now allocates auto `run-id` roots with collision
    retries instead of `exist_ok=True`
  - explicit `--run-id` reuse now fails fast unless `--resume` is set
  - each local run root now gets a `.lock` file so concurrent reuse fails
    explicitly
  - `artifacts/s390x/latest` is now updated atomically with `os.replace()`
    instead of an unlink-plus-symlink race

- Fresh native reruns after that hardening:
  - gcc release `jit_core` is green at
    [artifacts/s390x/20260319T193120.002606Z-p87501/summary.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/20260319T193120.002606Z-p87501/summary.md)
  - gcc debug `jit_loops` fails reproducibly at
    [artifacts/s390x/20260319T193120.002609Z-p87502/summary.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/20260319T193120.002609Z-p87502/summary.md)

- `asm_hrefk()` has now been implemented in `src/lj_asm_s390x.h` using a
  full-width GC64 key compare against `Node.key`, with a large-offset
  fallback. That change did not regress the staged gcc release `jit_core`
  gate.

- The current `jit_loops` blockers are now concrete:
  - `IR_HSTORE` (`75`) on the string-key table build loop at `t/iter.t`
    test 1 line 4
  - `asm_stack_restore` tag `-107` on the explicit `next()` path in
    `t/iter.t` test 2
  - `IR_HREF` (`58`) on the custom iterator hash lookup in `t/iter.t`
    test 3
  - big-endian `ITERN` is still intentionally disabled and remains behind
    those earlier blockers

- The current shortest remediation order is now:
  - implement `IR_HSTORE`
  - fix the non-trivial `asm_stack_restore` case hit by the `next()` loop
  - implement `IR_HREF`
  - then move into the BE iterator contract itself:
    - `vm_next`
    - recorder BE gates for `next()` / `pairs()`
    - any needed `IR_HIOP` / paired-return handling

## 2026-03-19 Loop-Gate Progress After `IR_HSTORE`

- Native staged rerun:
  - [artifacts/s390x/20260319T201417.058795Z-p33421/summary.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/20260319T201417.058795Z-p33421/summary.md)
- Changes in this slice:
  - `src/lj_asm_s390x.h` now has a real fused `asm_fstore()` path using the
    existing `FREF` IR, with exact-width stores for `U8`, `U16`, `U32`, and
    64-bit pointer or GC values
  - `src/lj_emit_s390x.h` now has `stcy`, `sthy`, and the related store
    helpers needed by that field-store path
- Result:
  - `IR_FSTORE` (`77`) is no longer the front-most `jit_loops` blocker
  - the string-key table-build loop now records cleanly as:
    - `[TRACE   1 test.lua:3 loop]`
    - `[TRACE   2 (1/0) test.lua:4 loop]`
  - this is progress, not final correctness:
    - the extra `(1/0)` linked trace is still a trace-shape mismatch relative
      to mature backends
    - iterator tracing still fails later because big-endian `ITERN` remains
      disabled

- Next staged rerun:
  - [artifacts/s390x/20260319T202015.844392Z-p38829/summary.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/20260319T202015.844392Z-p38829/summary.md)
- Changes in this slice:
  - `src/lj_asm_s390x.h` now restores traced numeric snapshot slots by storing
    FPR values back to the Lua stack with `stdy`
- Result:
  - `asm_stack_restore` tag `-107` is gone from the explicit `next()` path
  - the next front-most blocker is now `asm_stack_check` tag `-106`
  - current observed `jit_loops` status on native `kdz`:
    - TEST 1: string-key build trace records, then `pairs()` still stops at
      big-endian `ITERN`
    - TEST 2: explicit `next()` has moved from `-107` to `-106`
    - TEST 3: the custom iterator root loop now records after the same
      table-build trace shape

- Clean remediation order after these two advances:
  - implement `asm_stack_check`
  - re-baseline `jit_loops`
  - then address the remaining trace-shape mismatch around the extra `(1/0)`
    table-build trace if it still persists
  - then move into the BE iterator contract itself:
    - `ITERN` recording on big-endian
    - `vm_next`
    - any remaining paired-return or iterator-exit issues

## 2026-03-19 Iterator Helper Bring-Up After `asm_stack_check`

- Native staged reruns:
  - smoke with JIT on is green at
    [artifacts/s390x/20260319T205124.178494Z-p56178/summary.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/20260319T205124.178494Z-p56178/summary.md)
  - the first `jit_loops` rerun with `vm_next` plus `recff_next()` is at
    [artifacts/s390x/20260319T205336.109057Z-p57258/summary.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/20260319T205336.109057Z-p57258/summary.md)
- Changes in this slice:
  - `src/vm_s390x.dasc` now has a real s390x `lj_vm_next` helper instead of
    the previous BE gap
  - `src/lj_ffrecord.c` now records `next()` on big-endian again by re-enabling
    `recff_next()`
  - the harness now has a dedicated explicit-`next()` repro at
    `tests/s390x/jit_loops/explicit_next.lua`, and `tools/s390x/driver.py`
    runs it first in the `jit_loops` stage
- Result:
  - explicit `next()` no longer records as a stitched fallback; it records as
    a real loop trace through `lj_vm_next`
  - `pairs()` is still blocked separately because `ITERN` remains disabled on
    big-endian in `src/lj_record.c`
  - the explicit `next()` repro still returned empty output in the last native
    rerun before the latest local helper adjustment

## 2026-03-19 Local-Only Prep While Native Access Was Unavailable

- Native validation was temporarily blocked by local sandbox restrictions on
  outbound `ssh`, so this slice is preparation for the next native rerun, not a
  new validated milestone.
- Changes staged locally:
  - `src/vm_s390x.dasc` hash hits in `lj_vm_next` now return the live
    `Node *` directly, matching mature backends and the `{ val, key }` layout
    that `lj_record_next()` expects
  - `src/lj_asm_s390x.h` now has a real `asm_hiop()` for call results, which is
    the missing generic half of the BE iterator path behind `IR_HIOP`
- Why this matters:
  - explicit `next()` does not need the traced next-index half, so it can move
    forward before `asm_hiop()` is implemented
  - `pairs()` and `ITERN` do need the `HIOP` half of `IRCALL_lj_vm_next`, so
    leaving `asm_hiop()` stubbed would immediately block the next BE iterator
    stage even if the helper payload is otherwise correct
- Planned next native order once access returns:
  - rerun `jit_loops` with `tests/s390x/jit_loops/explicit_next.lua` first
  - confirm whether the hash-hit `Node *` return fixes explicit `next()`
  - if it does, move directly to the BE `ITERN` recorder gate in
    `src/lj_record.c`
  - if it does not, debug the remaining `lj_vm_next` pair-return payload before
    widening scope

## 2026-03-19 Fresh Native Rebuild And Explicit `next()` Revalidation

- Remote access is working again through the `kdz` tmux path, and there is now
  a clean source-level rebuild path that does not depend on older harness
  snapshots:
  - fresh clone:
    `git clone --depth 1 --branch k8ika0s/s390x-bringup-wip https://github.com/k8ika0s/luajit2-s390x.git fresh-20260319-explicit-next`
  - native build:
    `make XCFLAGS='-DLUAJIT_ENABLE_S390X_JIT' BUILDMODE=static -j4`
- This clarified an important build contract detail:
  - the pushed branch still keeps s390x JIT behind
    `LUAJIT_ENABLE_S390X_JIT` in `src/lj_arch.h`
  - without that define, a clean s390x source rebuild compiles the JIT pieces
    out and leaves unresolved JIT references at link time
- Minimal branch-level native patch for explicit `next()`:
  - re-enable big-endian `recff_next()` in `src/lj_ffrecord.c`
  - special-case `IRCALL_lj_vm_next` in `src/lj_asm_s390x.h` so non-`HIOP`
    `CALLL PTR` results use `RID_RETLO`
  - add `tests/s390x/jit_loops/explicit_next.lua`
- Native result on `kdz` from that clean rebuild:
  - `./src/luajit tests/s390x/jit_loops/explicit_next.lua` now exits `0`
  - the explicit-`next()` total is correct again at the Lua level
- Native tracing result on the pushed branch with only that minimal patch:
  - `jit.status()` is on
  - `jit.attach(..., "trace")` shows repeated `start` and `abort` events
  - `jit.util.traceinfo(1)` stays `nil`, so the loop still never commits
- Observed native abort payloads:
  - `abort ... 22 32 75`
  - `abort ... 13 32 -107`
  - `abort ... 36 32 67`
  - `abort ... 36 9 nil`
- Interpretation:
  - the minimum `lj_vm_next` result-register fix is correct and necessary for
    semantics, but not sufficient for trace completion
  - the clean native aborts line up with the backend slices that the local tree
    has already been addressing after the pushed branch point:
    - `75` matches the earlier string-key table-build `HSTORE` frontier
    - `-107` matches the earlier `asm_stack_restore` snapshot-restore gap
    - `67` is another pre-iterator backend gap on the older pushed branch
  - this is a useful confirmation that the current local remediation order is
    still sound: the local tree is ahead of the pushed branch, not off-track
- Process note:
  - `-jv` is not usable in this static s390x rebuild even when `require("jit")`
    and `jit.status()` work, so the reliable native trace probe for this phase
    is `jit.attach(..., "trace")`

## 2026-03-19 Fresh Native Iterator Checkpoint

- The fresh `kdz` rebuild path is still usable after bringing the newer local
  `asm_stack_check`, `asm_stack_restore`, `asm_tvstore64`, `asm_tvptr`,
  `asm_ahustore`, and `asm_fstore` slices into the remote checkout.
- One remote splice bug surfaced during that sync:
  - the fresh remote `src/lj_asm_s390x.h` lost the forward declaration for
    `asm_tvstore64`
  - restoring that prototype ahead of `asm_stack_restore()` was enough to get
    the fresh tree building cleanly again
- Fresh native result after the rebuild:
  - `prove -v t/iter.t` still fails in exactly three places
  - the front-most aborts are now stable and explicit:
    - `IR 58` in the traced table-iterator setup path
    - `IR 67` in explicit `next()` and custom iterator paths
    - `BC_ITERN` still hard-NYI on big-endian once the trace reaches that bytecode
- Interpretation:
  - the current iterator frontier is now clear enough to order cleanly
  - `IR 58` is the first table-lookup path to finish for traced iterator setup
  - `IR 67` remains the missing paired-result half behind `lj_vm_next`
  - `BC_ITERN` is still intentionally blocked in both the recorder and VM path,
    so even after the first two lowerers are complete the BE iterator bring-up
    still needs the dedicated `ITERN` enablement step
- Process note:
  - the fresh-clone remote sync is still more brittle than the local harness
    path because surgical header replacements are being applied over tmux
  - the evidence itself is good, but the next backend slices should be applied
    locally first and then synced in a smaller, cleaner batch to avoid more
    splice-only failures

## 2026-03-19 Native Iterator Resume Fix And `IR_VLOAD` Follow-Through

- The latest remote-native validation was done on `kdz` against the local-sync
  checkout after pushing two small control-flow fixes from the local tree:
  - `src/vm_s390x.dasc` now saves `SAVE_PC` in `BC_JLOOP`
  - `src/vm_s390x.dasc` static resume in `vm_exit_interp` no longer overwrites
    the decoded `BC_JLOOP` traceno with `DISPATCH_J(parent)`
- Native result:
  - `tests/s390x/jit_loops/pairs_loop.lua` no longer segfaults
  - the same reproducer now runs to completion and prints the correct total
  - the old crash in `lj_BC_JMP` has been converted into ordinary trace
    progression
- That immediately exposed the next real backend gap:
  - the iterator/print tail now stopped on `IR 72`, which maps to `IR_VLOAD`
  - the relevant local gap was in `src/lj_asm_s390x.h`, where `asm_ahuvload()`
    only accepted `int/u32/addr`
- Local fix:
  - extend `src/lj_asm_s390x.h` `asm_ahuvload()` to accept primitive boxed
    values (`nil/true/false`) in addition to the existing integer and address
    classes
  - keep the working GC64 address path intact, but add full 64-bit primitive
    compare-and-load handling
- Native result after syncing that slice to `kdz`:
  - `tests/s390x/jit_loops/pairs_loop.lua` now reaches
    `[TRACE 109 (108/1) pairs_loop.lua:13 stitch print]`
  - the old `IR_VLOAD` abort at the final `print()` is gone
  - Lua-visible output remains correct: `pairs total 5050`
- Important remaining issue:
  - `t/iter.t` is still failing only on err-output trace shape
  - the active mismatch is repeated side traces in the string-key table-build
    prelude:
    `[TRACE 2 (1/0) test.lua:4 loop] ...`
  - and repeated linked traces in the iterator loop:
    `[TRACE n (prev/1) test.lua:8 -> 11]`
- New diagnostic result that narrows this further:
  - a no-`-jv` probe of the table-build loop with default thresholds records
    only one root trace:
    `trace 1 link 1 type loop nins 31 nk 13`
  - this means baseline loop commit is fine
  - the remaining instability is in the repeated hot-exit / side-trace path,
    not in basic root-trace assembly for the build loop
- Another useful data point:
  - a no-`-jv` probe of the same table-build loop with `hotexit=2` still
    segfaults on native s390x
  - so the remaining frontier is now clearly the hot-exit / repeated side-trace
    path, not the earlier iterator crash or the earlier `IR_VLOAD` tail

## 2026-03-19 Hot-Exit Miss-Chain Diagnosis

- The current hot-exit investigation is no longer focused on exit-number
  transport. That question is now settled.
  - `src/lj_trace.c` now has an opt-in exit-stub mapper that decodes the saved
    s390x `brasl` return address back to the originating exit stub slot
  - native `kdz` logs show the repeated chain really is coming from
    `stubslot=1`, `stubexit=0`
  - implication:
    - the long side-trace chain is not caused by a bad exit decode in
      `vm_exit_handler`
    - it is caused by the trace actually taking `exit 0`

- A portable build issue fell out of that diagnostic work:
  - the new exit logger originally referenced `RID_R14` unconditionally and
    broke non-s390x builds
  - `src/lj_trace.c` now hides that behind a small `LJ_TARGET_S390X` helper, so
    the same diagnostic code can stay in-tree without breaking local x86_64
    control runs

- New side-trace shape probe:
  - `tests/s390x/jit_loops/hotexit_shape_dump.lua` dumps `traceinfo`,
    `tracek`, `traceir`, and `tracesnap` for traces 1..4 without depending on
    `jit.vmdef`
  - native `kdz` and local x86_64 runs show that the early s390x side traces
    are near-clones of the miss loop rather than the shorter update-path root
    trace seen on x86_64

- Local x86_64 control for the preinterned mixed loop:
  - `TRACE_start iter=2 tr=1 otr=nil oex=nil`
  - `TRACE_start iter=20 tr=2 otr=1 oex=2`
  - `TRACE_start iter=21 tr=3 otr=1 oex=0`
  - `TRACE_start iter=100 tr=4 otr=3 oex=3`
  - recorder/exit logs confirm:
    - the first root trace records one miss (`key=a3`, `oldv_nil=1`,
      `hmask=1`)
    - the first update side trace starts from `parent=1 exit=2`
    - that side trace immediately sees `oldv_nil=0` with `hmask=31`
    - the later loop trace starts from `parent=1 exit=0`

- Native `kdz` result for the same preinterned mixed loop:
  - trace starts:
    - `TRACE_start iter=2 tr=1 otr=nil oex=nil`
    - `TRACE_start iter=3 tr=2 otr=1 oex=0`
    - `TRACE_start iter=4 tr=3 otr=2 oex=0`
    - ...
    - `TRACE_start iter=20 tr=19 otr=18 oex=0`
    - `TRACE_start iter=100 tr=20 otr=19 oex=3`
  - recorder logs for traces 2..19 show:
    - `parent=n exit=0`
    - `xrefop=58` (`IR_HREF`)
    - `oldv_nil=1`
    - `phase=store-miss`
    - keys advancing through `a4` .. `a20`
    - hash growth progressing through `hmask=3`, `7`, `15`, and `31`
  - the first update hit appears only at:
    - `parent=18 exit=0 key=a1 oldv_nil=0 hmask=31`

- Clean conclusion from the new control comparison:
  - native s390x does eventually reach the same grown-table update state as
    x86_64
  - but it reaches that state through a long `exit 0` miss-chain instead of
    x86_64's early `exit 2 -> exit 0 -> stitch` convergence
  - `J->startpc` staying at the root-loop PC on native side traces is a
    consequence of that:
    - in `lj_record_setup()`, side traces only keep `startpc` when
      `exitno == 0`
    - so the wrong-looking repeated loop trace shape is downstream of the
      `exit 0` chain, not a separate root cause by itself

- Next diagnostic target:
  - compare the hot-exit guard/snapshot mapping for the first mixed miss loop
    between x86_64 and s390x
  - the specific open question is whether the guards that x86_64 associates
    with `exit 2` are being assigned to `snap 0` on s390x, or whether the
    correct snapshot is present but the generated guard path still routes
    execution to the `exit 0` stub

## 2026-03-20 Mixed Miss-Store Narrowing

- Fresh native `kdz` validation with the clean `src/` rebuild path confirmed
  that `tests/s390x/jit_loops/preinterned_key_cycle.lua` converges on s390x the
  same way it does on local x86_64:
  - trace 1 is a loop
  - trace 2 is a stitch
  - no `(n/0)` miss-chain appears

- That rules out the broader “cycling preinterned string key” path. The
  remaining JIT bug is still specific to the mixed hash store/update loop.

- I then added a flag-gated `LUAJIT_S390X_TABGET_LOG` probe in `src/lj_tab.c`
  and reran the mixed native repro on `kdz`.

- The native evidence from that run is clear:
  - compiled s390x trace calls into `lj_tab_get()` with the correct string keys
    (`a6`, `a7`, `a8`, ...)
  - `lj_tab_get()` returns the canonical `niltv(L)` pointer on misses
  - the repeated `exit 0` miss-chain still happens even though the helper
    lookup input and output are both correct

- That rules out these failure modes:
  - bad `asm_tvptr()` key materialization for the `lj_tab_get()` helper call
  - bad `lua_State *` argument passed to `lj_tab_get()`
  - bad `lj_tab_get()` miss return value
  - bad exit-stub decode

- The remaining frontier is therefore after the successful miss lookup, most
  likely in one of:
  - merged HREF compare / guard behavior after the helper call
  - `IR_NEWREF`
  - `IR_HSTORE`
  - `IR_TBAR` / `IR_FSTORE` write-barrier handling

- The current bug surface is now much narrower:
  - hash lookup itself is correct on native s390x
  - the long side-trace chain is happening in the traced miss-store path that
    follows that lookup

## 2026-03-20 Hot-Exit Resolution And Native Sweep

- The mixed dynamic-key hot-exit chain is now fixed on native s390x.
  - root cause:
    - the s390x backend was still taking the fused `HREF + EQ/NE` helper-call
      path in `asm_fuseequal()`
    - on the mixed preinterned update loop, that fused path associated the
      miss guard with `exit 0`
    - result:
      - native `kdz` kept cloning the miss loop as
        `parent=1 exit=0`, `parent=2 exit=0`, `parent=3 exit=0`, ...
      - x86_64 instead formed the expected update trace first from
        `parent=1 exit=2`, then converged through `exit 0` and stitch
  - fix:
    - `src/lj_asm.c` now disables that fused `HREF + EQ/NE` lowering on
      `LJ_TARGET_S390X`
    - s390x falls back to the generic compare/guard path, which preserves the
      expected snapshot and exit numbering for this helper-backed hash lookup

- Native `kdz` validation after that change matches the expected control flow:
  - `tests/s390x/jit_loops/hotexit_update_preinterned.lua` now records:
    - trace 1: root loop
    - trace 2: update-path root trace from `parent=1 exit=2`
    - trace 3: update-path loop trace from `parent=1 exit=0`
    - trace 4: final stitch
  - final Lua-visible result is correct:
    - `done 81 100`

- Fresh native focused sweeps on `kdz` are now green for the current staged
  s390x suites:
  - `t/iter.t`
  - `t/isarr-jit.t`
  - `tests/s390x/jit_core/basic_trace.lua`
  - `tests/s390x/jit_core/isarray_root_loop.lua`
  - `tests/s390x/jit_core/side_exit.lua`
  - `tests/s390x/jit_loops/explicit_next.lua`
  - `tests/s390x/jit_loops/hotexit_build_trace.lua`
  - `tests/s390x/jit_loops/hotexit_shape_dump.lua`
  - `tests/s390x/jit_loops/hotexit_update_only.lua`
  - `tests/s390x/jit_loops/hotexit_update_preinterned.lua`
  - `tests/s390x/jit_loops/hotexit_update_trace.lua`
  - `tests/s390x/jit_loops/iter_pairs.lua`
  - `tests/s390x/jit_loops/pairs_loop.lua`
  - `tests/s390x/jit_loops/preinterned_key_cycle.lua`
  - `tests/s390x/jit_loops/vararg_trace.lua`
  - `tests/s390x/jit_be/mixed_width_ffi.lua`
  - `tests/s390x/jit_be/number_helpers.lua`

- The native standalone FFI and callback drivers are also green when run
  against freshly built oracle shared libraries on `kdz`:
  - built:
    - `/tmp/ffi_oracle.so` from `tests/s390x/ffi_abi/oracle.c`
    - `/tmp/callback_oracle.so` from
      `tests/s390x/callbacks/callback_oracle.c`
  - validated:
    - `src/luajit tests/s390x/ffi_abi/run.lua /tmp/ffi_oracle.so`
    - `src/luajit tests/s390x/callbacks/run.lua /tmp/callback_oracle.so`
  - both return `RC=0` with no stderr output on native s390x

- Current conclusion:
  - the earlier iterator and hot-exit blocker is resolved
  - the branch now has a materially broader native green surface across core
    JIT loops, BE helper probes, outbound FFI ABI, and callback execution
  - the next correct step is to widen correctness coverage and identify the
    next real missing JIT/backend surface instead of continuing to debug the
    already-fixed hot-exit path

## 2026-03-20 Native Clang Sweep

- A clean native clang JIT build now works on `kdz`:
  - `make clean && make CC=clang XCFLAGS='-DLUAJIT_ENABLE_S390X_JIT' -j4`
  - result: `CLANG_BUILD_RC=0`

- The current focused native validation slice is also green under that clang
  build:
  - repo `.t` coverage:
    - `t/isarr-jit.t`
    - `t/iter.t`
    - `t/table-clone.t`
  - staged s390x sweeps:
    - `tests/s390x/jit_core/*.lua`
    - `tests/s390x/jit_loops/*.lua`
    - `tests/s390x/jit_be/*.lua`
    - `tests/s390x/soak/mixed_stress.lua`
  - standalone FFI and callback drivers:
    - `tests/s390x/ffi_abi/run.lua /tmp/ffi_oracle.so`
    - `tests/s390x/callbacks/run.lua /tmp/callback_oracle.so`

- Native result:
  - all focused clang probes return `RC=0`
  - `prove` result for the three repo `.t` files is `PASS` with `Files=3`
    and `Tests=42`

- Practical implication:
  - the current s390x branch state is no longer only a gcc-native success case
  - the first toolchain-hardening slice is green for clang as well
  - the next matrix work can move outward from this base instead of treating
    clang as an open blocker

## 2026-03-20 Native Dynamic-Build Sweep

- A focused native dynamic-build sweep is now green on `kdz`.

- Initial native failure:
  - `make BUILDMODE=dynamic XCFLAGS='-DLUAJIT_ENABLE_S390X_JIT' -j4`
    completed successfully
  - but every runtime probe failed with `RC=127`
  - native loader error:
    - `src/luajit: error while loading shared libraries:
      libluajit-5.1.so.2: cannot open shared object file`

- Root cause:
  - the ELF link step was using the soname `libluajit-5.1.so.2`
  - but the build still only emitted `libluajit.so`
  - so the native loader could not satisfy the soname dependency even though a
    shared library had been built

- Fix:
  - `src/Makefile` now emits the actual soname target on ELF platforms:
    - `libluajit-5.1.so.2`
  - `libluajit.so` is kept as a symlink to that soname target
  - `tools/s390x/remote_run.sh` now exports the repo-local `LD_LIBRARY_PATH`
    so staged native dynamic runs use the just-built library without requiring
    manual environment setup

- Native validation after that fix is green:
  - repo `.t` coverage:
    - `t/isarr-jit.t`
    - `t/iter.t`
    - `t/table-clone.t`
  - standalone FFI and callback drivers:
    - `tests/s390x/ffi_abi/run.lua /tmp/ffi_oracle.so`
    - `tests/s390x/callbacks/run.lua /tmp/callback_oracle.so`
  - direct native soak probe:
    - `tests/s390x/soak/mixed_stress.lua`

- Practical implication:
  - the current branch state is no longer only validated under static or
    default link modes
  - the dynamic ELF packaging path is now structurally correct for native
    zLinux runs
  - the next matrix step should widen to another native host rather than
    continuing to spend time on the already-green `kdz` dynamic slice

## 2026-03-20 Second-Host Native GCC Sweep

- Host-to-host transport is now working between `zkd0` and `kdz`:
  - a dedicated `root` ed25519 key was created on `zkd0`
  - that public key was appended to `kdz:/root/.ssh/authorized_keys`
  - `zkd0` can now pull the known-good checkout from
    `kdz.dev.fyre.ibm.com:/root/luajit2-s390x/local-sync-check/` with `rsync`
  - this is a useful long-term bootstrap path for cross-host matrix work when
    the local sandbox cannot open direct SSH sessions

- Fresh native gcc validation on `zkd0` is green for the same focused surface
  already exercised on `kdz`:
  - repo `.t` coverage:
    - `t/isarr-jit.t`
    - `t/iter.t`
    - `t/table-clone.t`
  - staged s390x sweeps:
    - `tests/s390x/jit_core/*.lua`
    - `tests/s390x/jit_loops/*.lua`
    - `tests/s390x/jit_be/*.lua`
    - `tests/s390x/soak/mixed_stress.lua`
  - standalone drivers:
    - `tests/s390x/ffi_abi/run.lua /tmp/zkd0_ffi_oracle.so`
    - `tests/s390x/callbacks/run.lua /tmp/zkd0_callback_oracle.so`

- One host bootstrap gap was exposed and fixed:
  - initial `prove` execution on `zkd0` failed with `bash: prove: command not found`
  - root cause:
    - the host had Perl, but not the `perl-Test-Harness` package that ships
      `/usr/bin/prove`
  - fix:
    - `tools/s390x/remote_bootstrap.sh` now installs `perl-Test-Harness` as
      part of the baseline package set
  - result:
    - rerunning the focused repo `.t` slice on `zkd0` returned `Result: PASS`
      with `Files=3` and `Tests=42`

- Practical implication:
  - the current s390x branch state is no longer a single-host gcc success case
  - the focused native matrix is now green across both `kdz` and `zkd0`
  - the next correct expansion is either:
    - native clang on `zkd0`, or
    - the focused dynamic-build slice on `zkd0`

## 2026-03-20 Second-Host Dynamic Sweep And Warning Cleanup

- The focused dynamic-build slice is now also green on `zkd0`.
  - build:
    - `make BUILDMODE=dynamic XCFLAGS='-DLUAJIT_ENABLE_S390X_JIT' -j4`
  - runtime artifacts:
    - `src/libluajit-5.1.so.2`
    - `src/libluajit.so -> libluajit-5.1.so.2`
  - green native validation under `LD_LIBRARY_PATH=$PWD/src`:
    - `t/isarr-jit.t`
    - `t/iter.t`
    - `t/table-clone.t`
    - `tests/s390x/ffi_abi/run.lua /tmp/zkd0_ffi_oracle.so`
    - `tests/s390x/callbacks/run.lua /tmp/zkd0_callback_oracle.so`

- That closes the current focused dynamic matrix on both available native hosts:
  - `kdz`: green
  - `zkd0`: green

- One native compiler-cleanup item was also confirmed and fixed during the
  second-host dynamic rebuild:
  - warning:
    - `lj_asm_s390x.h: asm_stack_check: operation on 'allow' may be undefined`
  - root cause:
    - `rset_clear()` is an in-place `&=` macro
    - the s390x backend was still wrapping it as
      `allow = rset_clear(allow, picked)`
  - fix:
    - `src/lj_asm_s390x.h` now uses:
      - `Reg picked = rset_pickbot(allow);`
      - `pbase = picked;`
      - `rset_clear(allow, picked);`
  - native confirmation:
    - a fresh dynamic rebuild on `zkd0` returns `NO_SEQUENCE_POINT_WARNING`
      when grepping the build log for the earlier diagnostic

- Current implication:
  - focused native gcc and dynamic coverage is now green across both native
    hosts
  - the next matrix expansion should be:
    - native clang on `zkd0`, or
    - a deliberate cleanup pass for the remaining unused-function warnings in
      the still-partial s390x backend

## 2026-03-20 Second-Host Native Clang Sweep

- Native clang validation is now green on `zkd0` as well.
  - build:
    - `make CC=clang XCFLAGS='-DLUAJIT_ENABLE_S390X_JIT' -j4`
  - focused repo `.t` coverage:
    - `t/isarr-jit.t`
    - `t/iter.t`
    - `t/table-clone.t`
  - staged s390x sweeps:
    - `tests/s390x/jit_core/*.lua`
    - `tests/s390x/jit_loops/*.lua`
    - `tests/s390x/jit_be/*.lua`
    - `tests/s390x/soak/mixed_stress.lua`
  - standalone drivers:
    - `tests/s390x/ffi_abi/run.lua /tmp/zkd0_ffi_oracle.so`
    - `tests/s390x/callbacks/run.lua /tmp/zkd0_callback_oracle.so`

- Native result:
  - `ZKD0_CLANG_BUILD_RC=0`
  - `ZKD0_CLANG_PROVE_RC=0`
  - `ZKD0_CLANG_SWEEP_RC=0`
  - `ZKD0_CLANG_FFI_RC=0`
  - `ZKD0_CLANG_CB_RC=0`

- Practical implication:
  - the focused native matrix is now green across both available hosts for:
    - gcc static/default builds
    - clang static/default builds
    - dynamic ELF builds
  - the next most valuable work is no longer more of the same focused
    host/toolchain replication
  - the branch should now spend more time on:
    - remaining JIT completion surfaces not covered by the focused probes
    - FFI-on-trace and broader hardening
    - cleanup of the still-open unused-function warnings in the partial s390x
      backend

## 2026-03-20 Native `tostring` Fallback Retry Regression

- Native `zkd0` still reproduces the focused no-print crash in
  `/tmp/tostring_type_matrix_noprint.lua` after the earlier fast-function
  fallback growstack fix.

- What moved:
  - the old failing `lj_state_growstack` call from `fff_fallback` label `5`
    is no longer the front-most fault
  - `fff_fallback` now enters with a correct `cur_L` reload from `DISPATCH`
    and passes the `maxstack` check on the observed runs

- New concrete evidence from native `gdb`:
  - the first and second fallback calls both load a valid `CFunc` from
    `[BASE-16]`
  - on the second fallback entry, `[BASE-8]` is `0x82`, not a saved caller PC
  - after the fallback/retry flow, the crash happens while dispatching
    `OP=21` (`BC_LEN`)
  - `DISPATCH[21]` itself is corrupted to a heap pointer:
    `0x3fff7fe0f54`
  - neighboring dispatch entries remain valid code pointers, so this is not a
    global `DISPATCH` base corruption

- Interpretation:
  - the immediate failure is no longer a bad `lua_State *` reload
  - the live hard failure is now either:
    - a stale/mis-established retry-frame PC slot around the returned-0
      fast-function retry path, or
    - a separate s390x VM bug that overwrites the single `BC_LEN` dispatch slot
      with a heap pointer during this sequence

- Local remediation started:
  - `fff_fallback` label `5` now reloads `cur_L` from `DISPATCH` before and
    after `lj_state_growstack`
  - the returned-0 retry path in `fff_fallback` now reloads `PC` from
    `SAVE_PC` before restoring `[BASE-8]`

- Next exact focus:
  - instrument or isolate the write that mutates `DISPATCH[BC_LEN]`
  - keep the scope on the `tostring` plus `#s` sequence until the `BC_LEN`
    dispatch slot remains stable across the retry path

## 2026-03-20 Clean-Tree Dynamic Build Graph Fix on `kdz`

- A fresh clean-tree mixed build on `kdz` exposed a real build-graph bug in
  the new shared-library path:
  - `$(LUAJIT_SO)` was correctly switched to depend on `$(LJVMCORE_DYNO)`
  - but only `$(LJVMCORE_O)` had the generated-header ordering fence
  - a clean parallel build could therefore start `_dyn.o` compilation before
    `lj_bcdef.h` and `lj_ffdef.h` existed

- Local fix:
  - `src/Makefile` now gives `$(LJVMCORE_DYNO)` the same order-only
    prerequisite on `$(ALL_HDRGEN)` as the static object set:
    - `$(LJVMCORE_O) $(LJVMCORE_DYNO) $(LUAJIT_O): | $(ALL_HDRGEN)`

- Native result on `kdz` after the fix:
  - clean build:
    - `make clean && make -j8 XCFLAGS='-DLUAJIT_ENABLE_S390X_JIT'`
    - result: `0`
  - focused validation rerun:
    - `tests/s390x/jit_core/*.lua`
    - `tests/s390x/jit_loops/*.lua`
    - `t/isarr-jit.t`
    - `t/iter.t`
    - `t/table-clone.t`
    - `tests/s390x/jit_be/*.lua`
    - `CC=gcc sh tests/s390x/build_oracles.sh`
    - `tests/s390x/ffi_abi/run.lua`
    - `tests/s390x/callbacks/run.lua`
    - `tests/s390x/soak/mixed_stress.lua`
  - result:
    - the clean-tree `kdz` rerun is green again
    - the current blocker is no longer the build graph

- Practical implication:
  - the branch is past the dynamic-build plumbing regression
  - the next most valuable work is to widen native repo coverage until the
    next real s390x runtime, JIT, or BE correctness gap appears

## 2026-03-20 Full Repo-Local `.t` Sweep Green on `kdz`

- After the clean-tree dynamic build-graph fix, the next widened native check
  on `kdz` was the full repo-local Perl suite:
  - `cd /root/luajit2-s390x/local-sync-check`
  - `export LUA_PATH='./?.lua;./?/?.lua;;src/?.lua;;src/?/?.lua;;tests/?.lua;;tests/?/?.lua;;'`
  - `prove -v t/*.t`

- Initial widened result:
  - all repo-local `.t` files were green except:
    - `t/exdata.t`
    - `t/exdata2.t`
  - the failing cases were the JIT read/default-value paths
  - observed bad outputs included non-NULL garbage pointers where the tests
    expected either the original pointer or `NULL`

- Focused native diagnosis on `kdz`:
  - a minimized `-jdump=ir` loop over `thread.exdata()` showed the recorder
    sinking `IR_CNEWI` around:
    - `p64 FLOAD thread.exdata`
    - `{sink} cdt CNEWI +19 ...`
  - that means exit-time restore must recover the original pointer payload
    from `ExitState`
  - the old s390x `vm_exit_handler` was not saving real architectural GPR
    state there:
    - `ExitState.gpr[1]` ended up holding the saved original `r14`
    - `jit_base` was also being cleared with the wrong scratch value instead
      of zero

- Local fix:
  - `src/vm_s390x.dasc`
    - `vm_exit_handler` now saves the actual architectural GPR set into the
      defined `ExitState.gpr[]` layout:
      - `TMPR0` to `gpr[0]`
      - `TMPR1` to `gpr[1]`
      - `r2..r13` via `stmg`
      - `r14` to `gpr[14]`
      - reconstructed original SP to `gpr[15]`
    - `jit_base` is now cleared with zero, not a stray scratch register value

- Native outcome on `kdz` after rebuild:
  - `prove -v t/exdata.t t/exdata2.t`
    - result: PASS
  - rerunning `prove -v t/*.t`
    - result: PASS
    - `Files=10, Tests=165`

## 2026-03-20 `zkd0` Alignment for `exdata` / `exdata2`

- After confirming the `vm_exit_handler` fix on `kdz`, the same source update
  was mirrored to the fresh `zkd0` worktree:
  - `/root/luajit2-s390x/zkd0-sync-check/src/vm_s390x.dasc`

- Native validation on `zkd0`:
  - rebuild:
    - `make -C src -j8 XCFLAGS='-DLUAJIT_ENABLE_S390X_JIT'`
  - targeted repo-local tests:
    - `prove -v t/exdata.t t/exdata2.t`

- Native outcome on `zkd0`:
  - both files are green after the mirrored fix
  - this closes the only widened repo-local test gap exposed so far on the
    `kdz` full-suite pass and keeps both native hosts aligned for the next
    broadening step

## 2026-03-20 Full Repo-Local `.t` Sweep Green on `zkd0`

- With the mirrored `vm_exit_handler` fix in place, the next widened check on
  `zkd0` matched the `kdz` repo-local Perl sweep:
  - `cd /root/luajit2-s390x/zkd0-sync-check`
  - `export LUA_PATH='./?.lua;./?/?.lua;;src/?.lua;;src/?/?.lua;;tests/?.lua;;tests/?/?.lua;;'`
  - `prove -v t/*.t`

- Native outcome on `zkd0`:
  - result: PASS
  - `Files=10, Tests=165`

- Practical implication:
  - both native hosts are now green on the full repo-local `.t` suite
  - the branch is past the earlier host divergence on `exdata` / `exdata2`
  - the next widening step should move outward across the remaining compiler
    and shared-build matrix until the next real s390x-specific runtime or JIT
    gap appears

## 2026-03-20 Full Repo-Local `.t` Sweep Green on `zkd0`

- After the mirrored `vm_exit_handler` fix was validated on the narrowed
  `exdata` pair, the next widened native check on `zkd0` was the full
  repo-local Perl suite:
  - `cd /root/luajit2-s390x/zkd0-sync-check`
  - `export LUA_PATH='./?.lua;./?/?.lua;;src/?.lua;;src/?/?.lua;;tests/?.lua;;tests/?/?.lua;;'`
  - `prove -v t/*.t`

- Native outcome on `zkd0`:
  - result: PASS
  - `Files=10, Tests=165`

- Practical implication:
  - the current branch is now green on the full repo-local `.t` suite on both
    native hosts:
    - `kdz`
    - `zkd0`
  - the next widening step should move out to the remaining compiler and
    shared-build matrix instead of staying on the repo-local Perl frontier

## 2026-03-20 Full Repo-Local `.t` Sweep Green on `kdz` Under Clang

- The next unproven compiler-host slice after the gcc full-suite passes was a
  native clang rebuild plus the full repo-local Perl suite on `kdz`:
  - `cd /root/luajit2-s390x/local-sync-check`
  - `make clean && make CC=clang XCFLAGS='-DLUAJIT_ENABLE_S390X_JIT' -j4`
  - `PATH=/usr/local/bin:$PATH prove -v t/*.t`

- First native result on `kdz`:
  - clang crashed again in `lj_asm.c` with:
    - `fatal error: error in backend: Unsupported stack frame traversal count`
  - unlike the earlier `zkd0` clang pass, this turned out not to be a new
    backend/compiler regression in the current branch

- Root cause:
  - the `kdz` worktree was stale and still had the old temporary hardening
    probes in:
    - `src/lj_emit_s390x.h`
      - `emit_loadi()` still logged both
        `__builtin_return_address(0)` and `__builtin_return_address(1)`
    - `src/lj_asm_s390x.h`
      - the old signed `LJ_TISNUM << 15` expression was still present
  - once those two spots were aligned to the current local tree, the same
    clang-20 host/compiler combination built cleanly

- Native outcome on `kdz` after aligning the worktree:
  - `make clean && make CC=clang XCFLAGS='-DLUAJIT_ENABLE_S390X_JIT' -j4`
    - result: PASS
  - `PATH=/usr/local/bin:$PATH prove -v t/*.t`
    - result: PASS
    - `Files=10, Tests=165`

- Practical implication:
  - the missing `kdz` clang quadrant is now closed
  - the next matrix widening step should move to the shared-build/full-suite
    combinations instead of revisiting static clang on the same host

## 2026-03-20 Full Repo-Local `.t` Sweep Green on `kdz` Under Clang Dynamic Build

- After closing the static clang pass on `kdz`, the next widening step was the
  full repo-local Perl suite under the shared-library build:
  - `cd /root/luajit2-s390x/local-sync-check`
  - `make clean && make CC=clang BUILDMODE=dynamic XCFLAGS='-DLUAJIT_ENABLE_S390X_JIT' -j4`
  - `prove -v t/*.t`

- First native result on `kdz`:
  - the build itself succeeded
  - the suite then failed uniformly with:
    - exit status `127`
    - `No such file or directory`
  - this was not a JIT/runtime regression; the dynamically linked `src/luajit`
    ran correctly when invoked directly with `LD_LIBRARY_PATH=src`

- Root cause:
  - the repo-local Perl tests chdir during execution, so the relative
    `LD_LIBRARY_PATH=src` used in the first run stopped pointing at the built
    shared library for child processes
  - the correct launcher form for this matrix slice is:
    - `export LD_LIBRARY_PATH=$PWD/src`

- Native outcome on `kdz` after re-running with an absolute runtime path:
  - `export LD_LIBRARY_PATH=$PWD/src`
  - `PATH=/usr/local/bin:$PATH prove -v t/*.t`
    - result: PASS
    - `Files=10, Tests=165`

- Practical implication:
  - the clang dynamic/full-suite path is green on `kdz`
  - this is a harness/runtime-path note, not a new compiler or s390x
    correctness gap
  - the next useful widening step is the same dynamic/full-suite slice on
    `zkd0`

## 2026-03-20 Full Repo-Local `.t` Sweep Green on `zkd0` Under Clang Dynamic Build

- The next matching matrix slice after the `kdz` dynamic clang/full-suite pass
  was the same run on `zkd0`:
  - `cd /root/luajit2-s390x/zkd0-sync-check`
  - `make clean && make CC=clang BUILDMODE=dynamic XCFLAGS='-DLUAJIT_ENABLE_S390X_JIT' -j4`
  - `export LD_LIBRARY_PATH=$PWD/src`
  - `PATH=/usr/local/bin:$PATH prove -v t/*.t`

- Native outcome on `zkd0`:
  - result: PASS
  - `Files=10, Tests=165`

- Practical implication:
  - the full repo-local `.t` suite is now green across both native hosts for:
    - gcc static/default builds
    - clang static/default builds
    - clang dynamic builds
  - the remaining high-value work is no longer matrix plumbing on this repo
    surface; it is the explicit remaining s390x runtime/JIT incompleteness
    still visible in the backend and VM sources

## 2026-03-20 Focused Probe Sweep On `kdz`

- Added a focused traced modulo probe in `tests/s390x/jit_core/mod_trace.lua`.
  - Local x86_64 control: green.
  - Native `kdz`: green.
  - Practical implication:
    - the current traced floating-point modulo path is not blocked by the
      still-stubbed `vm_mod` helper.

- Re-ran the current focused JIT frontiers on native `kdz`:
  - `tests/s390x/jit_core/trace_event_postloop.lua`
  - `tests/s390x/jit_loops/vararg_trace.lua`
  - Outcome: both are green on the current native worktree.

- Extended the FFI ABI oracle with direct complex-value coverage:
  - `double complex add_complex(double complex a, double complex b)`
  - `double complex mul_complex(double complex a, double complex b)`
  - Local x86_64 control: green.
  - Native `kdz`: green.
  - Practical implication:
    - the current s390x ABI path is conservative here, but not obviously wrong
      for direct complex arguments/returns.

## 2026-03-20 Profiler Hook Reproducer And Remediation

- Added a focused profiler probe in `tests/s390x/jit_core/profile_loop.lua`
  using `jit.profile.start("fi1", cb)`.

- Initial native `kdz` result:
  - shorter loop: `samples == 0`
  - longer loop repro: segfault
  - batch gdb backtrace pinned the fault directly to `lj_vm_profhook`.

- Root cause:
  - `vm_profhook` in `src/vm_s390x.dasc` was still a trap stub.

- Local remediation:
  - implemented `vm_profhook` to mirror the mature backends:
    - load `cur_L` from dispatch
    - store `BASE` to `L->base`
    - call `lj_dispatch_profile(L, PC)`
    - restore `BASE`
    - back up `PC` by one instruction and re-dispatch through `cont_nop`

- Native `kdz` outcome after rebuild:
  - the long profiler repro no longer segfaults
  - native samples are now observed (`samples 8` on the longer repro)

- Probe calibration:
  - the original 30M-iteration loop was too short to produce deterministic
    samples on `kdz`
  - an 80M-iteration floating-point accumulation loop produces samples
    reliably (`samples 3` in the focused native one-liner)
  - `tests/s390x/jit_core/profile_loop.lua` has been updated to use that
    longer floating-point loop for a stable gate

- Next step for this slice:
  - validate the same profiler path on `zkd0`
  - then fold the updated profiler probe back into the staged native loop

## 2026-03-20 Bitops JIT Follow-Up

- Native `kdz` status before the latest return-path work:
  - stripped bitops loops now record and reach `TRACE ... stop`
  - the remaining wrong-result reproducer is the raw-return closure shape:
    - `local x = bit.band(i, 0xff)`
    - `x = bit.bxor(x, bit.lshift(i, 3))`
    - `return x`
  - first confirmed native mismatch:
    - `i = 6`
    - expected: `54`
    - got: `6`

- Differential narrowing:
  - `jit.off()` on the focused closure is correct.
  - If the callee is forced interpreted with `jit.off(f, true)`, the outer
    hot loop stays correct on native `kdz`.
  - If the callee is allowed to trace with `jit.on(f, true)`, the result is
    wrong on native `kdz`.
  - Practical implication:
    - the front-most bug is now inside the traced callee path or its return
      contract, not in the outer caller loop alone.

- `asm_retf` remediation:
  - s390x no longer leaves `asm_retf` as a stub.
  - The first implementation removed the large caller-frame corruption and
    changed the wrong-result shape from `393222` to a smaller wrong return.
  - Adding the `REF_BASE` spill update in `asm_retf` fixed the larger caller
    frame poisoning. After that change:
    - `jit.off(f, true)` stays correct
    - `jit.on(f, true)` is still wrong, but now returns `6` instead of
      `393222`
  - Practical implication:
    - traced lower-frame return on s390x is materially closer to correct
    - the remaining bug is back in the traced callee-local path, which is a
      better next frontier than the earlier caller-frame corruption

- Next step:
  - stay on the minimal traced-callee closure
  - inspect the generated callee trace body and its return-value placement
    after the repaired `asm_retf`
  - keep the next cut limited to the `band` / `lshift` / `bxor` path until the
    `i = 6` reproducer is clean

## 2026-03-20 Bitops Rotate Slice And Current `BNOT` Frontier

- Native `kdz` tracing of the focused callee-only bitops repro showed the
  next concrete backend bug was the constant rotate path, not the earlier
  `BXOR` or return scaffold:
  - `bit.band`
  - `bit.lshift`
  - `bit.bxor`
  - `bit.bor`
  - `bit.bsar`
  - `bit.brol`
  were enough to reproduce the wrong traced result before the last `BNOT`
  stage was even added.

- Root cause for the rotate slice:
  - the s390x constant `BROL` / `BROR` lowering was still materializing the
    rotate count in a temp register
  - on native `kdz` that temp-based path produced the first traced mismatch in
    the prefix probe even though the non-rotate prefix stayed correct

- Local remediation:
  - `src/lj_asm_s390x.h`
    - constant `asm_brot()` now lowers through the immediate `RLL` form
      directly
    - the failed `ra_left_nobase` experiment was dropped instead of being
      carried forward

- Native `kdz` outcome after the rotate fix:
  - the focused prefix probe is now green through op7
  - the first remaining mismatch is op8, which adds `bit.bnot(i)`
  - repo-local `tests/s390x/jit_core/bitops_trace.lua` still fails only
    because of that final step

- Current working hypothesis for `BNOT`:
  - `asm_bnot()` was still grabbing a fixed temp for the all-ones mask without
    excluding the already-allocated source register first
  - that can alias and clobber the traced input before the XOR

- Current next step:
  - keep the now-good immediate rotate lowering intact
  - validate the narrowed `asm_bnot()` allocator fix on native `kdz`
  - if the prefix probe turns fully green, re-run
    `tests/s390x/jit_core/bitops_trace.lua` and then re-baseline the broader
    `jit_core` bitops coverage

## 2026-03-20 Bitops File-Entry Rebaseline

- The first narrowed `asm_bnot()` allocator hardening did not change the
  native prefix result by itself:
  - traced `bit.bnot(i)` was already correct
  - traced `bit.bxor(x, bit.bnot(i))` was also already correct

- Native `kdz` narrowing after that point showed the real split was structural,
  not a raw `BNOT` arithmetic failure:
  - the full op8 prefix chain was green in focused one-liners
  - the exact helper/capture/mix logic was green in `-e` and `dofile(...)`
    forms
  - the only bad shape left was the direct file-entry path of
    `tests/s390x/jit_core/bitops_trace.lua`

- Further focused native probes pinned the remaining bad combination to:
  - direct file entry
  - active `jit.attach(..., "trace")`
  - traced `mix()` as a callee
  - Removing any one of those factors made the focused repro green.

- Final native rebaseline for this slice:
  - after rerunning the repo-local file repro on the current build,
    `tests/s390x/jit_core/bitops_trace.lua` is green again on `kdz`
  - the current local `asm_bnot()` allocator hardening is kept because it is
    the correct register-allocation contract for that path even though it was
    not the only factor in the earlier mismatch trail

- Current next step:
  - re-baseline the broader `tests/s390x/jit_core/*.lua` sweep on native s390x
  - take the next front-most failure from that wider sweep instead of staying
    on the bitops branch after the repo-local repro has gone green

## 2026-03-20 FFI Cdata Trace After `IR_XSTORE`

- Added the first direct-address s390x `asm_xload()` / `asm_xstore()` slice in
  `src/lj_asm_s390x.h` for the currently exercised integer and pointer widths.
  This clears the front-most `IR_XSTORE` NYI on the `pair_t[1]` cdata field
  loop.

- Native `kdz` behavior is now different in an important way:
  - the focused `ffi_cdata` event dump reaches a real `start` / `stop`
  - the old repeated `abort ... NYIIR 78` is gone

- That change does **not** make the cdata loop correct yet.

- Bare traced cdata loop on native `kdz`:
  - script shape:
    - `box[0].x = i`
    - `box[0].y = box[0].y + box[0].x`
  - observed result:
    - `x = 3`
    - `y = 6`
  - expected after 200 iterations:
    - `x = 200`
    - `y = 20100`
  - a plain `collectgarbage("collect")` after the loop then segfaults

- Additional narrowing from focused native probes:
  - store-only repro:
    - `for i=1,200 do box[0].x = i end`
    - observed: `x = 3`
  - practical implication:
    - this is not just the `y = y + x` arithmetic path
    - a traced cdata field store by itself is not being committed correctly on
      native s390x once the loop goes hot

- Important differential narrowing:
  - an empty `jit.attach(..., "trace")` handler does **not** change the bad
    `x = 3`, `y = 6` result
  - the generic post-trace event walk is still good:
    - `tests/s390x/jit_core/trace_event_postloop.lua` is green on the same build
  - practical implication:
    - this is not the generic trace-event postloop bug again
    - it is still a cdata-trace execution or resume problem

- `gdb` on native `kdz` for `tests/s390x/jit_core/ffi_cdata_trace.lua`:
  - the process later faults in `gc_sweep()` during `lua_gc(LUA_GCCOLLECT)`
  - this is consistent with heap corruption after the bad traced cdata loop

- Recorder-shape review in `src/lj_crecord.c`:
  - for struct field access, the recorder folds `sizeof(GCcdata) + field_ofs`
    into the pointer before emitting `IR_XLOAD` / `IR_XSTORE`
  - practical implication:
    - the current `ofs = 0` lowering in the new s390x `asm_xload()` /
      `asm_xstore()` is structurally consistent with the recorder contract
    - the remaining bug is not simply “forgot the `GCcdata` header offset”

- Current interpretation:
  - `IR_XSTORE` is no longer the front-most blocker
  - the next real bug is a bad first compiled execution of the cdata loop or
    the immediate resume path after it
  - the `x = 3`, `y = 6` result strongly suggests the compiled cdata loop is
    not running through the full iteration space correctly on native s390x

- Next step:
  - keep the current `asm_xload()` / `asm_xstore()` slice in place
  - compare this bad cdata loop execution path against a known-good traced loop
  - focus the next remediation on compiled loop execution or exit/resume state
    for the cdata trace rather than broadening more FFI field lowering

## 2026-03-20 FFI Cdata Root-Compare Narrowing

- Local x86_64 control for the minimal store-only loop shows the expected IR
  shape:
  - root path:
    - `XSTORE box.x = i`
    - `ADD i + 1`
    - `LE ... +4`
  - loop path:
    - `XSTORE box.x = i`
    - `ADD i + 1`
    - `LE ... +4`
    - `PHI`

- Native `kdz` `jit.dump` mcode for the same 4-iteration repro confirms:
  - the loop backedge is patched to the loop-body label, not one instruction
    late
  - the compiled loop body does contain the expected first instruction store
  - practical implication:
    - the old “backedge skips the loop-body `XSTORE`” theory is not supported
      by the native mcode dump

- The same native run still exits as:
  - `parent=1 exit=1`
  - observed result after the trace:
    - `x4 = 3`
  - practical implication:
    - the bad result is more likely coming from the root compare / carried
      integer value path before the loop-body store is reached on that final
      transition

- Strong current hypothesis:
  - the s390x backend is still not normalizing some integer-width values after
    traced `ADD`/`SUB`
  - this would allow the low 32 bits to look correct for `XSTORE`, while the
    full 64-bit register value remains wrong for the subsequent compare/guard

- A local remediation candidate is in progress:
  - normalize `IRT_INT` / `IRT_U32` results in `asm_add()` and `asm_sub()`
    using the same `asm_bnorm32()` contract already used in the newer `mul` and
    `neg` lowering
  - local syntax checks are green

- Validation status for that candidate:
  - the native remote confirmation has not completed yet
  - the current `kdz` disposable worktree was polluted by repeated ad hoc tmux
    patch attempts, so the next native rerun needs a clean file sync before the
    result is trustworthy

## 2026-03-21 Remote Trust Reset

- The local repo remains the authoritative source of truth for the active
  cdata-loop narrowing slice.
- The old ad hoc `kdz` worktree is no longer a trustworthy validation surface:
  - repeated tmux-side edits polluted `src/lj_asm_s390x.h`
  - remote compile failures there are now transport noise, not signal
- A clean committed remote source tree is available on `kdz` at:
  - `/root/luajit2-s390x/fresh-20260319-explicit-next`
- That remote tree is on an older commit than the current local branch, so the
  correct workflow is now:
  - fresh remote clone from that committed source
  - replay the exact local patch in one shot
  - rebuild
  - rerun the smallest focused cdata repro before widening back out
- Added a local helper for this transport path:
  - `tools/s390x/tmux_patch_sync.py`
  - purpose: emit chunked tmux-safe `printf` commands that reconstruct and
    optionally `git apply` a local patch on the remote host
- Next trust-restoring action:
  - use the clean remote source plus the generated patch replay
  - confirm the local `asm_add()` / `asm_sub()` normalization candidate against
    the 4-iteration `ffi_cdata_small_trace.lua` repro

## 2026-03-21 Clean Native Baseline Re-established

- A fresh native validation tree was created on `kdz` at:
  - `/root/luajit2-s390x/clean-loop-20260321`
- That tree was reset from the published fork branch and then patched with the
  current local-only code delta instead of continuing to mutate the polluted
  ad hoc worktree.
- The preferred tmux replay pane for this clean loop is:
  - session `luajit2s390x`
  - pane `%111`

- Clean native rebuild result:
  - `make -j4 XCFLAGS='-DLUAJIT_ENABLE_S390X_JIT'`
  - result: green on `kdz`

- Two focused native repros were rerun immediately on the clean build:
  - `/tmp/ffi_cdata_small_trace.lua`
    - observed:
      - `x4 = 4`
      - `gc-ok`
  - `/tmp/ffi_cdata_trace_small.lua`
    - observed:
      - `xy = 200 201`
      - `gc-ok`

- Practical implication:
  - the earlier “traced `ffi` cdata store corruption” report from the polluted
    remote tree is not trustworthy
  - the clean native rebuild does **not** reproduce that corruption
  - the remediation loop is back on a stable footing

- Updated working rule:
  - treat the clean remote tree plus one-shot local patch replay as the only
    authoritative native surface for current work
  - do not carry forward blocker claims that have not been reproduced on that
    clean surface

- Next step:
  - rebaseline the next JIT-facing probe from the clean remote tree
  - take the next blocker only from clean native evidence

## 2026-03-21 First Trustworthy Rebaseline Result

- After the clean-tree trust reset, a focused native `jit_core` sweep was run
  directly from:
  - `/root/luajit2-s390x/clean-loop-20260321`
- Result:
  - the first trustworthy failing path is now
    `tests/s390x/jit_core/bitops_trace.lua`

- Native `kdz` failure:
  - observed:
    - `expected 873075307, got -1951285677`
  - the failure is accompanied by repeated native exit-0/hotside logging on
    the same hot path

- Practical implication:
  - the earlier `ffi` cdata corruption report from the polluted worktree should
    not drive the current priority order anymore
  - the clean native loop has restored a concrete and trustworthy next blocker
  - the immediate next validation target is the local bitops-related backend
    delta in `src/lj_asm_s390x.h`

- Current next step:
  - replay the current local code delta into the clean remote tree
  - rebuild
  - rerun `tests/s390x/jit_core/bitops_trace.lua` before widening back out

## 2026-03-21 Vararg Trace Narrowing

- The stable native baseline on `kdz` is preserved:
  - `tests/s390x/jit_loops/vararg_trace.lua` still fails as:
    - expected `5650`
    - got `601`
  - no new broad regression was accepted into the tree

- The vararg loop has now been split into cleaner native probes:
  - `vararg_count.lua`
    - result: correct
    - observed:
      - `CNT 1 4`
      - `CNT 2 4`
  - practical implication:
    - `select("#", ...)`
    - loop index progression
    - and the traced loop bound / compare path
    are all behaving correctly on the current native build

- The remaining bug is in the traced value-fetch path for `select(i, ...)`,
  not in the traced count path.

- Native value probes:
  - `vararg_return_split.lua`
    - `retconst(...)` returns `42` correctly on both calls
    - `retlast(...)` returns:
      - `LAST 1 1`
      - `LAST 2 1`
  - practical implication:
    - traced return itself is not the front-most bug
    - the fetched vararg value is already wrong before return

- Native sequence probe:
  - `vararg_seq.lua`
  - observed:
    - `SEQ 1 1241`
    - `SEQ 2 141`
  - expected:
    - `SEQ 1 1231`
    - `SEQ 2 1232`
  - practical implication:
    - the value-fetch path is drifting into the wrong stack/control slots while
      the loop count stays correct

- Diagnostic code slices tried in this loop:
  - extra PHI / RA / slot / IR logging
  - a conservative non-aliased `AREF` destination
  - saved-register preference for pointer-like `ADD` / `SUB` results
  - a first ad hoc fused `VLOAD(AREF(...))` path

- Result of those attempts:
  - the non-aliased `AREF` change did not move the native result
  - the saved-register preference for pointer-like results did not move the
    native result
  - the first ad hoc fused `VLOAD(AREF(...))` path was directionally relevant
    but unsafe:
    - it changed the failure mode from wrong result to native trace-code
      segfault
    - that experiment was backed out to restore the stable wrong-result
      baseline

- Current best interpretation:
  - the next real fix should be a proper s390x port of the mature
    `asm_fuseahuref()` / fused `VLOAD(AREF(...))` path used by established
    backends, not another local one-off fusion attempt
  - the active frontier is now tightly scoped to traced vararg value address
    formation and use

## 2026-03-21 Vararg Runtime Layout And Exit Probe

- The current clean native focus moved from generic vararg value fetch to a
  smaller repro:
  - `/tmp/vararg_looplast.lua`
  - loop body:
    - `for i = 1, select("#", ...) do`
    - `v = select(i, ...)`
  - native result:
    - `LAST 1 1`
    - `LAST 2 1`
    - then `select()` eventually errors on the hot path with
      `bad argument #1 to 'select' (index out of range)`

- This repro is better than the original sum loop because it removes the outer
  accumulation noise while preserving the same traced `select(i, ...)` shape.

- Native IR / snapshot evidence for `looplast`:
  - root trace 1 records:
    - `0018 i64 SUB 0000 0011`
    - `0019 p64 ADD 0018 +3`
    - `0020 p64 AREF 0019 0015`
    - `0021 int VLOAD 0020 #0`
  - loop snapshot is:
    - `SNAP #3 [ ---- ---- 0027 ]`
  - hot exits come through:
    - `parent=1 exit=3`
  - practical implication:
    - the front-most failure is not a wrong exit number
    - the loop result register is already wrong at the point of exit

- Native s390x exit-state dump:
  - exit 3 repeatedly shows:
    - `r11=0x5`
    - `r5=0x3`
    - `r12=0x1`
  - practical implication:
    - loop-carried control state is plausible
    - the loaded value register is the bad state
    - this is upstream of generic snapshot-number decode

- Native traced-address dump:
  - the computed traced AREF register is stable:
    - `r4 = r2 + r5*8`
  - but the live bytes at the traced AREF window are not the expected vararg
    sequence for the current call
  - observed on exit 3:
    - `arefbias +1 = 1`
    - neighboring slot-biased values:
      - `-24:1`
      - `-16:3`
      - `-8:4`
      - `0:1`
      - `8:3`
      - `16:2`
      - `24:3`
  - practical implication:
    - the current `+1` big-endian lane read is reading a real integer lane
    - but it is reading from the wrong slot window
    - this points back to traced vararg pseudo-base mapping, not to the final
      integer lane bias itself

- Interpreter-side runtime ground truth was captured by logging
  `LJLIB_CF(select)` under `LUAJIT_S390X_SELECT_LOG=1`.
  For the non-JIT `looplast(1, 2, 3, 2)` call:
  - `select("#", ...)` sees:
    - `base[0] = "#"`
    - `base[1] = 1`
    - `base[2] = 2`
    - `base[3] = 3`
    - `base[4] = 2`
  - `select(i, ...)` sees the dynamic call shape:
    - `base[0] = i`
    - `base[1] = 1`
    - `base[2] = 2`
    - `base[3] = 3`
    - `base[4] = 2`
  - practical implication:
    - the interpreter-side `select` argument vector is clean and contiguous
    - the traced AREF window captured on exit does not line up with that real
      runtime argument layout

- Current best interpretation:
  - the current s390x traced dynamic-vararg pseudo-base points into the wrong
    stack-slot window for the looped `select(i, ...)` path
  - this is narrower than the earlier generic `VLOAD` / lane-bias theory
  - the next correct remediation target is the s390x mapping of:
    - `REF_BASE - fr`
    - plus the `kintpgc(frofs - (8<<LJ_FR2))` adjustment
    in the dynamic `select(i, ...)` recorder/lowering contract

- Follow-up offset probes:
  - a temporary env-gated byte override was added for traced vararg `VLOAD`
    offsets
  - global slot-bias sweep for `/tmp/vararg_looplast.lua`:
    - `0`   -> `LAST 1 1`, `LAST 2 1`
    - `8`   -> `LAST 1 3`, `LAST 2 1`
    - `16`  -> `LAST 1 2`, `LAST 2 1`
    - `24`  -> `LAST 1 3`, `LAST 2 2`
    - `32`  -> `LAST 1 1`, `LAST 2 3`
  - practical implication:
    - the failure is offset-sensitive
    - but no single static slot shift fixes both hot executions

- Root-vs-loop split probe:
  - with:
    - `LUAJIT_S390X_VARG_SLOT_BIAS_ROOT=16`
    - `LUAJIT_S390X_VARG_SLOT_BIAS_LOOP=24`
  - native result:
    - `LAST 1 2`
    - `LAST 2 1`
    - `vararg_trace.lua` improves from `601` to `702`, but is still wrong
  - loop-bias sweep with root fixed at `16`:
    - changing loop bias between `0`, `8`, `16`, `24`, and `32`
      did not move `LAST 2`
  - practical implication:
    - the first hot execution is controlled by the root dynamic-vararg fetch
      mapping
    - the later hot-loop failure is not controlled by the same simple loop-side
      `VLOAD` offset knob
    - the next frontier is now split:
      - root traced dynamic-select pseudo-base mapping
      - then later hot-loop side-exit / re-entry behavior on the same path

## 2026-03-21 Clean Two-Host Rebaseline

- The branch has been revalidated on two separate native s390x hosts:
  - `kdz`
  - `zkd0`
- The `zkd0` spot-check run now matches the clean `kdz` baseline.
- Native build on `zkd0` completed successfully with:
  - `XCFLAGS='-DLUAJIT_ENABLE_S390X_JIT'`
- Green native spot-check set on `zkd0`:
  - `prove -v t/isarr-jit.t`
  - `prove -v t/iter.t`
  - `prove -v t/exdata.t`
  - `tests/s390x/ffi_abi/run.lua`
  - `tests/s390x/callbacks/run.lua`
  - all current `tests/s390x/jit_core/*.lua`
  - all current `tests/s390x/jit_loops/*.lua`
  - all current `tests/s390x/jit_be/*.lua`
  - `tests/s390x/soak/mixed_stress.lua`
- Important behavioral confirmation from the `zkd0` run:
  - the earlier bitops, traced-FFI, iterator, and vararg blockers in this
    branch cycle are all green on the current source state
  - repo-local vararg probes now print the expected values:
    - `vararg_return_split.lua`: `LAST 1 1`, `LAST 2 2`
    - `vararg_seq.lua`: `SEQ 1 1231`, `SEQ 2 1232`
  - `pairs_loop.lua` is semantically correct and prints:
    - `pairs total 5050`
- Current frontier after the clean two-host rebaseline:
  - correctness is green on the current staged spot-check set
  - the next remaining gap is trace-shape quality, especially longer-than-
    expected side-trace chains on iterator / hot-exit paths
  - this is now a convergence-quality investigation, not the front-most crash
    or wrong-result blocker

## 2026-03-21 Iterator / Hot-Exit Quality Focus

- Native `zkd0` still shows a longer side-trace chain than ideal in the
  iterator / hot-exit diagnostics:
  - `hotexit_shape_dump.lua`
  - `hotexit_update_trace.lua`
  - `hotexit_update_preinterned.lua`
- Those runs are nevertheless semantically correct and converge to a valid end
  state:
  - `done 81 100`
  - or the expected final accumulator for the reduced probe
- Current interpretation:
  - the exit transport and stack re-anchoring bugs are no longer the
    front-most issue
  - the remaining work in this area is about loop-link convergence and trace
    shape on s390x, not basic correctness
- Next action:
  - keep the current two-host green baseline fixed
  - probe the remaining `pairs()` / iterator side-trace chain against mature
    64-bit backend behavior
  - cut only the smallest backend change that shortens or eliminates the extra
    s390x trace chain without regressing the green baseline

## 2026-03-21 Dynamic HREF Probe

- The exact `t/iter.t` `pairs()` body still shows the longer side-trace ladder
  on the clean native surface when run directly with `-jv`:
  - `TRACE 1` table-build loop
  - `TRACE 2` iterator loop
  - repeated `(n/1) iter_test1.lua:8 -> 2` side traces after that
- A focused IR dump confirms the repeated side traces sit on:
  - `CALLL lj_vm_next`
  - `VLOAD`
  - dynamic `HREF`
  - `HLOAD`
- A first local attempt to inline the dynamic string-key `HREF` path in
  `src/lj_asm_s390x.h` did hit the right JIT surface, but it did not shorten
  the side-trace ladder and it regressed dynamic string lookup correctness.
- That experiment was reverted locally and on the clean native tree after
  revalidation:
  - direct dynamic string lookup loop returned to `total 5050`
  - `prove -v t/iter.t` returned to green
- Current interpretation:
  - the remaining iterator quality issue is real
  - but the first dynamic-string `HREF` inline port was not yet a valid fix
  - keep the branch on the restored green baseline and continue narrowing from
    there

## 2026-03-21 Broad Native Matrix Rebaseline

- The restored green baseline has now been widened beyond the targeted s390x
  suites.
- Native `prove -v t/*.t` is green for the current branch on:
  - `kdz` with gcc
  - `kdz` with clang
  - `zkd0` with gcc
  - `zkd0` with clang
- That broad repo-local TAP sweep covers:
  - `Files=10`
  - `Tests=165`
- Combined with the existing s390x-focused suites, the current branch state is
  now revalidated across:
  - two native s390x hosts
  - both gcc and clang
  - repo-local Perl/TAP coverage
  - the repo-local `tests/s390x` JIT, FFI, callback, BE, and soak suites
- Current frontier after the widened matrix:
  - no new correctness blocker was exposed by the broader native matrix
  - the remaining known issue is still the iterator / hot-exit convergence
    shape visible in direct `-jv` stress repros

## 2026-03-21 Exact Update-Ref Iterator Restore Fix

- The remaining wrong-result failure on the clean native `kdz` loop was reduced
  to the custom iterator repro in `/tmp/iter_custom_trace2only.lua`:
  - expected: `total = 5050`
  - stable bad baseline: `total = 682`
  - intermediate mixed-rename state: `total = 4950`

- The decisive runtime evidence came from exact-ref restore logging:
  - failing exit: `trace=1 exit=4`
  - live exit registers already held the correct carried sum:
    - `r4 = 0x13ba`
    - `r12 = 0x13ba`
  - stale restore-visible state still existed for exact refs:
    - `ref18`
    - `ref9`

- The post-flush custom iterator trace IR showed the carried loop state that
  matters here:
  - `0018 >+ int ADDOV 0016 0003`
  - `0028    int PHI    0003 0018`
  - `0029    int PHI    0016 0027`
  - `0030    int PHI    0009 0020`
  - snapshot `#4` restores `0018` and `0009`

- Root cause:
  - two rename mechanisms were interacting badly on s390x:
    1. explicit carried update-ref canonicalization in `asm_phi_fixup()`
    2. stale left-PHI renames emitted implicitly by `ra_rename()` from
       `asm_phi_shuffle()`
  - that mixed state let the carried update ref move in the right direction
    while an old left-PHI shuffle rename still polluted snapshot restore

- Local remediation in `src/lj_asm.c`:
  - keep explicit loop-snapshot renames only for carried update refs in
    `asm_phi_fixup()`
  - target those renames at the update ref's own final allocated register
    (`IR(phi->op2)->r`) instead of the left-PHI destination register
  - split `ra_rename()` into:
    - normal rename with snapshot-visible `IR_RENAME`
    - `ra_rename_nosnap()` for pure PHI-shuffle register moves
  - switch `asm_phi_shuffle()` to `ra_rename_nosnap()` so left-PHI shuffles no
    longer create stale restore-visible renames

- Native `kdz` result after the fix:
  - reduced repro:
    - `total = 5050`
  - exact restore log:
    - `ref18` now restores from live `reg12` with `0x13ba`
    - stale left-PHI rename entries for `ref9` / `ref3` are gone
  - full repo iterator test:
    - `prove -v t/iter.t`
    - result: PASS (`1..9`, all green)

- Current interpretation:
  - this was not a generic iterator semantics problem
  - it was an exact update-ref canonicalization bug in loop-snapshot restore,
    exposed most clearly by the custom iterator hot-exit path on s390x
  - the branch is back on a quality/convergence frontier, not a current
    iterator wrong-result frontier

## 2026-03-21 Direct Return SAVE_L Fix

- After the exact update-ref iterator fix, the next clean native `kdz`
  `jit_loops` blocker was:
  - `tests/s390x/jit_loops/vararg_trace.lua`
  - crash site:
    - `lj_vm_exit_interp+10`
    - `stg %r13,32(%r7)`
  - fault cause:
    - `SAVE_L` loaded from `256(sp)` was `0x0f`
    - the live on-trace `lua_State *` in `r8` was still valid

- The minimal trace shape for the failure was:
  - `TRACE 1`: vararg loop
  - `TRACE 2`: caller side trace back to `TRACE 1`
  - `TRACE 3`: `(1/3) ... stop -> return`
  - the crash happened immediately after the return trace committed

- Key narrowing results:
  - removing the extra `SPS_FIXED` term from the s390x `link=0` tail did not
    change the fault signature
  - the decisive runtime proof was that the direct return path was reaching
    `vm_exit_interp` without seeding `SAVE_L`
  - the problem was therefore not generic vararg fetch anymore, and not the
    older exit-number / snapshot mismatch theory

- Local remediation in `src/lj_asm_s390x.h`:
  - widen non-loop `lnk==0` tail reservation so the direct return tail has
    room for an extra in-place store
  - on the s390x direct `link=0` tail, store the live `RID_LREG` value into
    `SAVE_L` before branching to `lj_vm_exit_interp`
  - keep the rest of the return-path contract unchanged

- Native `kdz` result after the fix:
  - `./src/luajit tests/s390x/jit_loops/vararg_trace.lua`
    - `RC=0`
  - `./src/luajit -jv tests/s390x/jit_loops/vararg_trace.lua`
    - `TRACE 1`
    - `TRACE 2`
    - `TRACE 3 (1/3) ... stop -> return`
    - `RC=0`
  - clean focused `jit_loops` sweep on `/root/luajit2-s390x/clean-loop-20260321`
    is now fully green:
    - `tests/s390x/jit_loops/*.lua`

- Current next step:
  - widen into the next native build-mode slice for `jit_loops`
  - keep the remaining focus on iterator / hot-exit convergence quality, not
    a current loop correctness crash

## 2026-03-21 Debug `vararg_trace` return handoff

- The wider assert-enabled `jit_loops` sweep on clean native `kdz` no longer
  stops in snapshot replay. The next blocker is:
  - `tests/s390x/jit_loops/vararg_trace.lua`
  - reduced native repro:
    - `/tmp/vararg_noprint.lua` with `n=12`

- New native narrowing:
  - `n=11` is green
  - `n=12` segfaults
  - trace sequence on the assert build:
    - `TRACE 1`: loop
    - `TRACE 2`: root
    - `TRACE 3`: `link=0 type=return`

- Important compile/runtime facts:
  - `TRACE 3` exits to interpreter as `pcop=RET1`, `baseslot=0`,
    `gotframe=0`, `mres=0`
  - `TRACE 3` does **not** call `asm_retf()`
  - an old s390x-specific `asm_retf()` bug was still found and fixed locally:
    the compare path loaded `base[-8]` back into the base register and then
    reused that clobbered base for later updates
  - that bug was real, but it is not the front-most cause of this crash,
    because `TRACE 3` never reaches `asm_retf()`

- `gdb` state at `lj_vm_exit_interp` entry for the failing `TRACE 3` is sane:
  - `BASE = 0x...ce20`
  - `-16(BASE)` still holds the callee function object
  - `0(BASE)` holds the return value `18`

- The later `gdb` crash in `lj_cont_dispatch` shows corrupted resumed state:
  - `BASE = 0x...cdf0`
  - `-16(BASE) = 18`
  - the surrounding stack window shows this base is no longer a valid frame
    base for continuation dispatch

- Current interpretation:
  - the failing frontier is now the no-link return-to-interpreter handoff
  - corruption is introduced after entering `vm_exit_interp`, in the
    interpreter-side `RET1` / continuation resume path
  - this is no longer a snapshot replay problem and no longer a direct
    `asm_retf()` problem

- Current next step:
  - instrument or narrow the `vm_exit_interp` `RET1` resume path for this
  return trace shape
  - determine whether the return trace is resuming the wrong PC/base contract
    for `RET1`, or whether later continuation resume is consuming the correct
    state incorrectly

## 2026-03-21 Fresh rsync-loop assert rebaseline for `vararg_trace`

- The remediation loop is trustworthy again from a direct local `rsync` into:
  - `kdz:/root/luajit2-s390x/rsync-loop-20260321`
- Fresh native assert rebuild:
  - `make -j4 XCFLAGS='-DLUAJIT_ENABLE_S390X_JIT -DLUA_USE_ASSERT'`

- Fresh native result on the reduced repro:
  - `/tmp/vararg_noprint.lua`
  - `n=11` and `n=12` both segfault on this current local tree
  - trace sequence with `jit.v` is still:
    - `TRACE 1`: loop
    - `TRACE 2`: `-> 1`
    - `TRACE 3`: `(1/3) ... stop -> return`

- The restore-boundary logs remain stable:
  - repeated `trace=1 exit=3`
  - only `slot=2 ref=30` is snapshot-restored on that loop exit
  - the changing restored value still tracks the loop total, so this is no
    longer pointing at a generic bad multi-slot replay

- New hard `gdb` proof for the post-exit path:
  - after the second `lj_vm_exit_interp` hit, the interpreter executes
    `lj_BC_RET1`
  - on that path, `RET1` first takes the expected vararg relocation branch:
    - raw return marker: `PC = 0x33`
    - relocated base: `BASE = ...cd60`
    - reloaded caller PC pointer from `-8(BASE)`:
      - `0x...3570`
  - the raw caller instruction bytes at that reloaded PC are:
    - `01 05 07 42`
    - opcode `0x42`, which decodes to `BC_CALL`
  - the later crash is in:
    - `lj_vm_returnp`
    - `lj_cont_dispatch`
  - at the crashing `lj_vm_returnp` entry:
    - `PC = 0x32`
    - `BASE = ...cd90`
  - at the crashing `lj_cont_dispatch` entry:
    - `BASE` still names the active Lua frame
    - `-32(meta_base)` / `-24(meta_base)` contain ordinary frame data, not a
      valid continuation function and continuation PC

- Strong current interpretation:
  - this is now a no-link return-to-interpreter handoff bug on a vararg return
    path
  - the current failure is after the trace exit and after the first vararg
    relocation step
  - the active frontier is no longer snapshot replay, no longer the older
    `SAVE_L` issue, and no longer a generic exit-number problem
  - the next likely fix surface is the s390x `vm_exit_interp` / `BC_RET1` /
    `vm_returnp` contract for this return-trace shape

- Important recorder-side clue:
  - the `TRACE 3` dump still contains no visible `IR_RETF`
  - a temporary env-gated recorder log (`LUAJIT_S390X_RECRET_LOG`) is now in
    the local tree to help distinguish normal lower-Lua-frame returns from
    continuation-style returns during the next pass

- Current next step:
  - compare the s390x no-link vararg return handoff against the mature x64
    path
  - keep the focus on the post-exit `RET1` / `vm_returnp` sequence, not on the
    already-cleared trace-1 replay side

## 2026-03-21 Vararg return handoff moved forward, next crash is caller-state after return

- A focused s390x fix landed in [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc) for wrapped C fast-function returns that see a live `FRAME_VARG` marker.
- Native `kdz` results after that patch:
  - `/tmp/vararg_noprint.lua 12` now exits `0`
  - [tests/s390x/jit_loops/vararg_seq.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/jit_loops/vararg_seq.lua) is green again
  - [tests/s390x/jit_loops/vararg_return_split.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/jit_loops/vararg_return_split.lua) now prints the correct `LAST 1 1` / `LAST 2 2`
- The full [tests/s390x/jit_loops/vararg_trace.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/jit_loops/vararg_trace.lua) still segfaults, but the failure frontier changed:
  - a file-shaped reproducer with the same hot vararg loop plus a final `print(result)` also segfaults
  - the no-print file repro stays green
  - `jit.v` still shows the expected trace sequence:
    - `TRACE 1`: loop
    - `TRACE 2`: `-> 1`
    - `TRACE 3`: `(1/3) ... return`
- New hard `gdb` proof on `kdz`:
  - the new crash is in `lj_cf_print()`
  - `lj_cf_print()` receives a bad `lua_State *`
  - the first bad user-visible operation after the repaired vararg return is the next ordinary C fast-function call from the caller chunk
- Current interpretation:
  - the traced vararg return itself is now correct enough to finish the loop
  - the next caller-side C call still sees corrupted interpreter state
  - the next remediation target is post-return caller-state restoration, most likely `SAVE_L` / `cur_L` or closely related caller-frame state after the traced vararg return

## 2026-03-21 Direct-exit `SAVE_L` corruption fixed on the vararg loop

- The next focused native `kdz` pass proved the remaining `print(result)` crash
  was not in the VM slow path anymore.
  - a hardware watchpoint on the active interpreter-frame `SAVE_L` slot showed
    the bad overwrite came from JIT mcode, not from `vm_exit_handler`
  - the exact corrupting instruction sequence was:
    - `aghi %r15,8`
    - `stg %r8,256(%r15)`
    - direct branch to `lj_vm_exit_interp`
  - that store came from the s390x tail fixup path in
    [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h)

- Root cause:
  - the s390x backend was emitting direct trace exits that stored `RID_LREG`
    (`r8`) into `SAVE_L`
  - `BC_JLOOP` does not actually seed `RID_LREG` for this path
  - on the hot vararg loop, `r8` held a stack-adjacent stale pointer instead of
    the authoritative `lua_State *`

- Local remediation:
  - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h)
    - `asm_tail_fixup()` no longer stores `RID_LREG` into `SAVE_L` for direct
      exits
    - it now rematerializes `cur_L` from `DISPATCH` and stores that value into
      the interpreter-frame `SAVE_L` slot
    - tail reservation for the no-link exit path was widened to cover the extra
      load/store pair

- Native `kdz` results after the fix:
  - `/tmp/vararg_result.lua`:
    - prints `5650`
    - exits `0`
  - [tests/s390x/jit_loops/vararg_trace.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/jit_loops/vararg_trace.lua):
    - exits `0`
  - [tests/s390x/jit_loops/vararg_seq.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/jit_loops/vararg_seq.lua):
    - still green
  - [tests/s390x/jit_loops/vararg_return_split.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/jit_loops/vararg_return_split.lua):
    - still green
  - `prove -v t/iter.t`:
    - green again on the same clean native loop
  - direct sweep of `tests/s390x/jit_loops/*.lua`:
    - green on native `kdz`

- Current interpretation:
  - the old vararg caller-state crash is no longer the front-most blocker
  - the direct-exit tail path now matches the real runtime contract for `L`
  - the next step is to re-stamp the now-green `jit_loops` surface under the
    staged harness and then widen back out to the next JIT gate

## 2026-03-22 `side_exit` wrong-result fixed by restore preference

- After the direct-exit vararg fix, the next front-most `kdz` regression was
  [tests/s390x/jit_core/side_exit.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/jit_core/side_exit.lua):
  - expected `25784`
  - got `26786`
- The smallest native repro was `/tmp/side_exit_n4.lua`:
  - before the fix it printed `0`
  - `jit.v` showed one root loop and a side exit
  - restore logging showed the loop-carried accumulator ref had both a live
    register and a spill, but the restore path used the stale spill slot
- Root cause:
  - on s390x, integer restore for this exit shape preferred the spill identity
    even when the authoritative value was still live in a register
  - the bad restore state propagated back into interpreter execution and
    produced the wrong total
- Local remediation:
  - [src/lj_snap.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_snap.c)
    - fixed the s390x restore-prefer-register override so it emits a real
      register-backed `RegSP`, not a broken hint-only encoding
    - made that s390x integer restore preference default-on unless explicitly
      disabled by `LUAJIT_S390X_RESTORE_PREF_REG=0`
- Native `kdz` results after rebuild:
  - `/tmp/side_exit_n4.lua`:
    - prints `10`
  - [tests/s390x/jit_core/side_exit.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/jit_core/side_exit.lua):
    - green
  - direct native sweep of `tests/s390x/jit_core/*.lua`:
    - green on `kdz`
- Structured runner state:
  - `jit_loops` is already stamped green by run
    `20260322T011616.680245Z-p25132`
  - the first attempted `jit_core` harness rerun,
    `20260322T012310.485271Z-p31132`, never progressed past bootstrap and is
    not authoritative
- Current next step:
  - rerun `jit_core` cleanly under the harness
  - then widen into `jit-correctness` from the structured loop

## 2026-03-22 Structured `jit_core` and `jit-correctness` restamped green

- The clean rerun of the staged `jit_core` gate on `kdz` is now authoritative:
  - run `20260322T012714.417104Z-p33648`
  - stage `jit-bringup`
  - suites executed:
    - `build`
    - `jit_core`
  - result:
    - success
    - no recorded failures
- The earlier `jit_core` attempt `20260322T012310.485271Z-p31132` remains
  non-authoritative because it never progressed beyond bootstrap.

- The next structured widening step is also now green on `kdz`:
  - run `20260322T013015.882691Z-p35735`
  - stage `jit-correctness`
  - suites executed:
    - `build`
    - `smoke`
    - `jit_core`
    - `jit_loops`
    - `jit_be`
    - `soak`
  - result:
    - success
    - no recorded failures

- Manual and spot-check widening from the same branch state also succeeded:
  - native `kdz` manual checks:
    - `tests/s390x/jit_be/*.lua`
    - `tests/s390x/soak/*.lua`
  - native `zkd0` spot-check set:
    - `tests/s390x/jit_core/side_exit.lua`
    - `tests/s390x/jit_core/bitops_trace.lua`
    - `tests/s390x/jit_core/ffi_cdata_trace.lua`
    - `tests/s390x/jit_loops/vararg_trace.lua`
    - `tests/s390x/jit_be/number_helpers.lua`
    - `tests/s390x/soak/mixed_stress.lua`
    - `prove -v t/iter.t`
    - `prove -v t/isarr-jit.t`
  - focused `kdz` clang JIT cut:
    - assert build succeeds
    - the current hot regression set is green after the clang build

- Current interpretation:
  - the branch is no longer blocked at the `jit-bringup` or
    `jit-correctness` stage gates for the current focused surface
  - the next useful work is to widen matrix coverage only where it exercises a
    meaningfully different surface, then continue with the remaining iterator
    and hot-exit quality work from that revalidated baseline

## 2026-03-22 Matrix restamp and harness transport hardening

- Clean native full repo-local Perl sweeps are now green on both active hosts:
  - `kdz:/root/luajit2-s390x/clean-loop-20260321`
  - `zkd0:/root/luajit2-s390x/spotcheck-20260321`
  - command:
    - `prove -v t/*.t`
  - result:
    - `Files=10, Tests=165`
    - `Result: PASS`

- Focused clang spot-checks are also green on both hosts:
  - `prove -v t/exdata.t t/iter.t t/isarr-jit.t`
  - `tests/s390x/jit_core/side_exit.lua`
  - result:
    - PASS on `kdz`
    - PASS on `zkd0`

- The `-DLUAJIT_DISABLE_FFI` matrix corner is green on native `kdz`:
  - build:
    - `XCFLAGS='-DLUAJIT_ENABLE_S390X_JIT -DLUA_USE_ASSERT -DLUAJIT_DISABLE_FFI'`
  - focused repo coverage:
    - `prove -v t/isarr-jit.t t/iter.t`
  - result:
    - PASS

- `thread.exdata()` and the reduced side-exit/nloop hot-side regressions remain
  fixed on the clean native loop:
  - `prove -v t/exdata.t`
  - `/tmp/nloop.lua 7`
  - `tests/s390x/jit_core/side_exit.lua`
  - result:
    - green on `kdz`
    - current spot-checks green on `zkd0`

- New process-level finding from the structured runner:
  - the manual native matrix is ahead of the harness again, but for a process
    reason rather than a new backend bug
  - the local driver’s `rsync`-based remote sync path hangs or degrades on the
    IBM Z hosts because the remote login banners interfere with that transport
  - the first authoritative harness failure from this phase,
    `20260322T025707.788600Z-p87271`, also exposed a real hardening gap:
    `vm_s390x.dasc` now calls `lj_trace_s390x_iter_log`, but that helper had
    been added as a plain C symbol rather than a normal exported trace helper
  - local remediation now in progress:
    - `src/lj_trace.h`
      - added declarations for:
        - `lj_trace_s390x_varg_probe`
        - `lj_trace_s390x_iter_log`
    - `src/lj_trace.c`
      - both helpers now use `LJ_FUNC`
    - `tools/s390x/driver.py`
      - transport is being converted away from raw `rsync` to banner-tolerant
        tar-over-ssh / ssh-cat paths

- Current interpretation:
  - the active correctness surface for the tested repo-local and focused JIT
    coverage is green on both native hosts
  - the immediate next work is harness hardening, not another backend rescue
  - once the driver transport and symbol-export issue are restamped under the
    structured runner, the next meaningful frontier returns to remaining
    iterator / hot-exit convergence quality work and the broader matrix

## 2026-03-22 Transport cleanup follow-up

- The harness transport hardening moved from “works, but noisy” to
  “structurally correct and branch-worthy”.

- Branch commits pushed during this pass:
  - `14b0150e`
    - `Harden s390x matrix transport and restamp native state`
  - `352e29eb`
    - `Sync only tracked files for s390x remote runs`
  - `e1d2f1ac`
    - `Quiet tracked-file s390x repo sync`

- New driver behavior in `tools/s390x/driver.py`:
  - repo sync no longer streams the whole working tree
  - sync now uses `git ls-files -z` as the authoritative file list
  - the tar command now disables copyfile/macOS metadata emission
  - untracked local scratch files are no longer copied into remote runs
  - the raw `git ls-files -z` payload is no longer printed to the console
  - optional binary collection remains best-effort

- Evidence from the transport restamp:
  - older run `20260322T033608.425913Z-p8440`
    - proved that the old `jit=off` link failure is gone
    - `gcc debug jit=off ffi=on static`
      - build: PASS
      - smoke: PASS
    - `gcc debug jit=off ffi=on dynamic`
      - build: PASS
      - smoke: PASS
    - `gcc debug jit=off ffi=off static`
      - build: PASS
      - smoke: PASS
    - this run still carried pre-hardening transport noise and remote junk-file
      contamination, so it is informative but not the final structured
      baseline for the new transport path
  - fresh reruns:
    - `20260322T034317.579291Z-p13051`
    - `20260322T034609.025414Z-p15495`
    - both are transport restamp runs from the pushed tracked-file sync path
    - these are not yet authoritative stage stamps until they complete and
      write final `summary.md` / `stage-report.md`

- Hard result from the tracked-file sync check:
  - remote staging under
    `kdz:/root/luajit2-s390x/20260322T034317.579291Z-p13051/repo`
    no longer contains the accidental untracked junk files that polluted the
    previous tar-over-ssh runs
  - that confirms the current transport direction is correct

- Current interpretation:
  - correctness is still ahead of the harness, not behind it
  - the structured runner is now being restamped from the same clean
    assumptions as the manual native loop
  - once one clean `matrix/smoke` run completes from the `e1d2f1ac` baseline,
    the next useful work is to widen structured matrix coverage instead of
    reopening any of the resolved JIT bugs
# 2026-03-22 Perf Baseline

- The first native performance stage is now real and artifact-producing via
  [tools/s390x/driver.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/driver.py).
- The current release-stable benchmark subset is intentionally narrow:
  `tests/s390x/perf/dispatch_trace.lua`.
- Authoritative native runs:
  - JIT on baseline and z13:
    [artifacts/s390x/20260324T022735.280630Z-p89021](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/20260324T022735.280630Z-p89021)
  - JIT off baseline and z13:
    [artifacts/s390x/20260324T023224.602906Z-p91889](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/20260324T023224.602906Z-p91889)
- Representative hot-scale medians on `kdz`, `gcc release`:
  - JIT on baseline:
    - `numeric_loop`: `0.032174s`
    - `side_exit_loop`: `0.017588s`
    - `hotexit_loop`: `0.009095s`
  - JIT on z13:
    - `numeric_loop`: `0.032158s`
    - `side_exit_loop`: `0.017870s`
    - `hotexit_loop`: `0.009307s`
  - JIT off baseline:
    - `numeric_loop`: `0.002070s`
    - `side_exit_loop`: `0.003668s`
    - `hotexit_loop`: `0.005599s`
- The first headline performance ratios from those runs are:
  - `%` fast path vs the pre-fast-path stamped baseline:
    - `numeric_loop/hot`: about `1.37x` faster
    - `side_exit_loop/hot`: about `1.57x` faster
    - `hotexit_loop/hot`: about `1.23x` faster
  - `numeric_loop/hot`: `z13` is about `1.00x` vs baseline
  - `side_exit_loop/hot`: `z13` is about `0.98x` vs baseline
  - baseline `jit=on` vs `jit=off`:
    - `numeric_loop/hot`: about `15.54x` slower
    - `side_exit_loop/hot`: about `4.80x` slower
    - `hotexit_loop/hot`: about `1.62x` slower
- Current interpretation:
  - the perf harness is working
  - the first `%` fast path produced a real measured win on the dispatch
    family, but the branch is still leaving substantial speed on the table
  - dispatch and side-exit overhead are now proven optimization hotspots for
    the s390x JIT path
- Follow-up validation:
  - the perf helper was corrected to use moderate JIT thresholds
    (`hotloop=10`, `hotexit=10`) instead of the old bring-up stress settings
  - a direct native rerun on `kdz` matched the dispatch medians within normal
    noise, so the hotspot conclusion still stands
- The next optimization loop should stay on that hotspot before widening the
  default perf gate to the unstable `bitops`, `vararg`, iterator-table update,
  or traced FFI perf families.

## 2026-03-23 Downstream Kong Startup Narrowing

- `closure-kdz-20260323c` is green for every closure suite except
  `downstream`.
- The failing downstream edge is now narrowed on `kdz` to Kong nginx startup
  with raw JIT enabled in the generated `init_by_lua` / `init_worker_by_lua`
  blocks.
- Fresh native `gdb` startup repro does not match the older coarse coredump
  summary. The live crash is:
  - `lj_tab_getstr()` / `lj_tab_get()` / `lj_meta_tget()`
  - while requiring `kong.tools.string` through nested `package.require`
  - during `ngx_http_lua_init_by_inline`
- The worker follow-on crash after a naive startup-only guard is the same class
  of failure:
  - worker cores resolve to `libluajit + 0x150ec/+0x15118`
  - `addr2line` maps those back into `hashmask` / `lj_tab_getstr` /
    `lj_tab_get` / `lj_tab_set`
- Full `jit.off()` in Kong nginx is a valid fallback and brings the runtime up
  cleanly, but that disables request-path JIT for the whole worker and is too
  blunt for the main downstream gate.
- The first downstream bridge that works end to end on native `kdz` is the
  delayed startup guard:
  - `init_by_lua_block`: `jit.off(); Kong = require 'kong'; Kong.init()`
  - `init_worker_by_lua_block`:
    `jit.off(); Kong.init_worker(); ngx.timer.at(3, function() require("jit").on() end)`
  - result:
    - nginx start succeeds
    - `GET /status` returns `200`
    - `GET /demo` returns `200`
- That guard is now wired into `demo/kong/run_kong_demo.sh` behind:
  - `KONG_DELAYED_JIT_ON_IN_NGINX=1`
  - `KONG_DELAYED_JIT_ON_SECS=3`
- The structured `downstream` restamp from that state is now green:
  - `closure-downstream-kdz-20260323d`
  - OpenResty lane: green
  - Kong lane: green through staged require probe, `prepare`, nginx start,
    `GET /status`, `GET /demo`, and post-start `run_kong_require_probe.sh`
- The first full `kdz` closure rerun from that state failed for a harness-only
  reason, not a runtime regression:
  - `closure-kdz-20260323d`
  - Kong tried to bind fixed `127.0.0.1:8000/8001` while an older demo worker
    was still listening there
  - the collision came from the demo harness leaving Kong nginx up after a
    successful run
- The downstream harness is now hardened in `demo/kong/run_kong_demo.sh`:
  - deterministic per-run proxy/admin ports derived from the run label
  - best-effort runtime teardown on exit
- The fresh full `kdz` closure rerun from that hardened state is now green:
  - `closure-kdz-20260323e`
- The old `zkd0` full closure restamp cleared every correctness suite and only
  failed in the stale downstream wrapper after the remote Kong demo had
  already passed:
  - `closure-zkd0-20260323a`
- The patched `zkd0` downstream restamp is now green too:
  - `closure-downstream-zkd0-20260323b`

## 2026-03-23 Closure Backlog Refresh and Perf Queue Hardening

- The closure inventory is now refreshed from current source state as:
  - `closure-audit-local-20260323b`
- The refreshed report corrects one stale closure conclusion immediately:
  - `asm_tobit` is implemented in `src/lj_asm_s390x.h`
  - it is no longer part of the live stub backlog
- The refreshed closure report now freezes the remaining backlog by track:
  - `numeric_helpers`
  - `reference_string_barrier`
  - `vm_runtime`
  - `feature_gated_debug`
- Current active closure backlog from that report:
  - `11` blocking items
  - `asm_prof` is tracked separately as a feature-gated/debug surface
- A new targeted native traced-integer-modulo repro is now in-tree:
  - `tests/s390x/jit_core/mod_int_trace.lua`
- The modulo queue is now split into two native surfaces:
  - green optimization lane:
    - `tests/s390x/jit_core/mod_int_trace.lua`
    - fresh native `kdz` proof on branch head is green for the root and simple
      side-exit `%` shapes
  - former closure lane, now remediated:
    - `tests/s390x/jit_loops/mod_hotexit_stress.lua`
    - the repro was reduced to the smallest failing native shape:
      - one hotexit guard: `i % 5 == 0`
      - one payload modulo: `total = total + (i % 97)`
      - `else total = total + 1`
    - the failure was specific to the low-threshold hotexit/stitch regime:
      - guard-only `% 5` with constant payload: green
      - `% 5` guard with plain `+i` payload: green
      - `% 5` guard with `% 97` payload: wrong
    - the front-most bug was not generic modulo lowering. It was root-trace
      restore of loop-carried integer state on hot exits:
      - `exit=1` duplicated an inherited `SLOAD` snapshot ref
      - `exit=5` restored the carried total from a stale seed ref instead of
        the current loop-carried state
    - the remediation combined three s390x-specific fixes:
      - duplicate `SNAP_NORESTORE` ref reuse in `lj_snap_restore()`
      - narrower register-preference rules for the bad root exits in
        `snap_restoreval()`
      - integer loop-state PHI canonicalization during root exit restore
    - fresh native proof is now green on both hosts:
      - `kdz`
      - `zkd0`
- `mod_int_trace.lua` stays intentionally out of the default `jit_core` lane
  until the modulo work is ready for promotion, but it is now the tracked
  Stream B entry point rather than a blended Stream A/B repro.
- `mod_hotexit_stress.lua` is no longer a red closure blocker on the current
  branch head and can move out of Stream A.
- The latest structured native perf restamp is now:
  - `20260324T022735.280630Z-p89021`
  - green on `kdz`
- That perf run adds two new machine-readable artifacts:
  - `perf/family-status.json`
  - `perf/hotspots.json`
- Current Stream B state from those artifacts:
  - `dispatch_trace` remains the default perf gate
  - `iterator_table` remains the first focused probe family
  - the remaining perf families stay probe-only until release-stable
- The local cross-arch control path also exposed a host-side tooling gap:
  - macOS local control builds were failing because
    `MACOSX_DEPLOYMENT_TARGET` was not exported
  - the driver now sets that automatically for local control builds
  - the refreshed run now emits cross-arch ratios again, but they remain
    diagnostic only and not a support gate

2026-03-23: first native `%` fast path landed

- Commit `26ee6d1b` fixed the remaining low-threshold `%` hotexit correctness
  bug by repairing root-trace restore/canonicalization; that moved `%` fully
  into Stream B performance work.
- The next cut now lands the first real s390x modulo optimization:
  - `IR_MOD` on signed ints with positive constant divisors no longer falls
    straight through `IRCALL_lj_vm_modi`
  - the new path uses native `dsgr`, with signed-remainder correction for
    negative dividends
- Native correctness restamp after that change is green on both hosts for:
  - `tests/s390x/jit_core/mod_int_trace.lua`
  - `tests/s390x/jit_loops/mod_hotexit_stress.lua`
  - `tests/s390x/jit_core/side_exit.lua`
- Additional safety restamp on `kdz` is green:
  - `tests/s390x/soak/mixed_stress.lua`
- `mod_int_trace.lua` now includes negative-dividend coverage so the signed
  correction path stays pinned down under JIT.
- Fresh structured native runs now restamp that same `%` win with current
  branch artifacts:
  - JIT on:
    - `20260324T022735.280630Z-p89021`
  - JIT off:
    - `20260324T023224.602906Z-p91889`
  - against the older stamped dispatch baseline, the current branch is now:
    - `numeric_loop/hot`: about `1.37x` faster
    - `side_exit_loop/hot`: about `1.57x` faster
    - `hotexit_loop/hot`: about `1.23x` faster
  - but the branch is still slower than interpreter-only execution on that
    family:
    - `numeric_loop/hot`: about `15.54x`
    - `side_exit_loop/hot`: about `4.80x`
    - `hotexit_loop/hot`: about `1.62x`
- First perf-family widening beyond `dispatch_trace` is also partially proved:
  - older structured `kdz` restamp `perf-iterator-kdz-20260323a` exposed the
    iterator cliff:
    - `jit=on baseline pairs_sum/hot`: `0.528441s`
    - `jit=on baseline pairs_array_sum/hot`: `0.401933s`
  - the current clean native probe on `kdz` (`perf-wave-20260324b`) keeps
    `HEAD` plus only two local perf changes:
    - remove the `BC_IITERL` debug helper call in `vm_s390x.dasc`
    - stop forcing `hotloop=10,hotexit=10` in `tests/s390x/perf/benchlib.lua`
  - that improved hot medians to:
    - `pairs_sum/hot`: `0.371870s`
    - `pairs_array_sum/hot`: `0.280803s`
  - relative to the older structured restamp, that is:
    - `pairs_sum/hot`: about `1.42x` faster
    - `pairs_array_sum/hot`: about `1.43x` faster
  - the same clean probe kept the current correctness slice green on `kdz`:
    - `tests/s390x/jit_core/mod_int_trace.lua`
    - `tests/s390x/jit_loops/mod_hotexit_stress.lua`
    - `tests/s390x/jit_core/side_exit.lua`
    - `tests/s390x/soak/mixed_stress.lua`
- That changes the Stream B priority order:
  - `dispatch_trace` remains a default gate and still shows real wins from the
    `%` fast path
  - `iterator_table` remains the next focused probe, not a default gate yet
  - the next major optimization target is still traced iterator / `next()`
    overhead, not generic modulo correctness
  - a first `BC_ISNEXT` JLOOP-unpatch port on s390x built cleanly but did not
    materially move the iterator numbers, so it is not part of the active
    patch set

2026-03-24: coherent iterator perf tuning wave

- The earlier `hotexit=200` default experiment is now revalidated on a
  coherent source tree instead of a stale mixed worktree.
- Safe patch set used for the clean native `kdz` probe:
  - remove the `BC_IITERL` debug helper call in `vm_s390x.dasc`
  - stop forcing `hotloop=10,hotexit=10` in `tests/s390x/perf/benchlib.lua`
  - set s390x default `JIT_P_hotexit = 200` in `src/lib_jit.c`
- Clean native run root:
  - `kdz:/root/luajit2-s390x/perf-wave-20260324b`
- Measured iterator medians on that coherent build:
  - `pairs_sum/hot`: `0.045205s`
  - `pairs_array_sum/hot`: `0.046697s`
- Relative to the older structured iterator restamp:
  - `pairs_sum/hot`: `0.528441s -> 0.045205s` (`11.69x` faster)
  - `pairs_array_sum/hot`: `0.401933s -> 0.046697s` (`8.61x` faster)
- Repeated same-process `pairs()` timing on that same coherent build:
  - `jit.on` runs: `0.003175`, `0.006357`, `0.010301`, `0.016793`,
    `0.024084`, `0.030375`
  - `jit.off` runs: about `0.0314`
  - so current `jit.on` iterator execution is now materially below
    interpreter cost on that path instead of catastrophically above it
- Dispatch remains near the current `%`-fast-path baseline on the same build:
  - `numeric_loop/hot`: `0.032363s`
  - `side_exit_loop/hot`: `0.017595s`
  - `hotexit_loop/hot`: `0.008959s`
- Current correctness slice stays green on the same build:
  - `tests/s390x/jit_core/mod_int_trace.lua`
  - `tests/s390x/jit_loops/mod_hotexit_stress.lua`
  - `tests/s390x/jit_core/side_exit.lua`
  - `tests/s390x/soak/mixed_stress.lua`
- So the current best iterator performance explanation is now:
  - helper removal fixed one direct VM-side cost
  - perf harness stopped forcing the worst threshold pair
  - a higher default `hotexit` meaningfully reduces root-linked iterator
    side-trace churn on s390x

2026-03-26: iterator boundary classifier narrowed the last semantic red

- The remaining iterator investigation is now past the older
  `root -> n` trace explosion and the earlier `trace 2 exit 1` ownership
  debate. The current frontier is a scratch-only semantic classifier on the
  post-call `lj_vm_next` tuple boundary.
- `iterator-exit2-twostep-20260326a` proved the old exposed root `exit 2`
  path was genuinely wrong:
  - first `trace 1 exit 2` resumed with a clean boxed carried total
  - second `trace 1 exit 2` already had the carried total polluted by the
    stale value-lane low word from the iterator-end tuple
  - that pinned the old red on the root `CALLL -> VLOAD #0 -> ADDOV` boundary,
    not on helper ABI, KEYINDEX restore, or child-trace replay
- A new scratch-only s390x classifier in
  [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h)
  now guards the post-call `lj_vm_next` key lane before the carried-total add
  path can consume the iterator-end tuple.
- `iterator-keylane-guard-20260326a` showed the first clean semantic split:
  - `pairs_array_sum:20` returned the expected result on native `kdz`
  - the hot owner moved from the broken `BC_ADDVV` resume path to
    `guardmark=0x427`
  - hot exits now land at `pc op=87`, i.e. `BC_JLOOP`, instead of re-entering
    the body add with a stale value lane
- `iterator-keylane-stop-20260326a` then showed that the new hot `exit 1`
  path is semantically clean:
  - repeated exits keep slot `0` as a valid boxed integer (`4`, then `9`)
  - outer-loop state remains sane
  - the remaining red is no longer stale payload corruption at the exit
    boundary
- The next blocker exposed by that cleaner boundary is trace formation, not
  wrong arithmetic:
  - `iterator-keylane-guard-20260326a` shows `TRACE 2 start` followed by
    `TRACE 2 abort otr=9`
  - `otr=9` is `LJ_TRERR_LINNER`
  - so the remaining problem is now the follow-on trace path after a correct
    iterator-end `BC_JLOOP` exit, not the old root `exit 2` corruption
- This key-lane guard is still classification-only:
  - it is not committed
  - it is not performance-restamped
  - it is useful because it converts the remaining iterator question from
    “why is the carried total corrupted?” into
    “why does the clean `BC_JLOOP` follow-on still abort as `LINNER`?”

2026-03-26: iterator classifier split cleanly into array and hash fronts

- The current scratch candidate pair is now proven to be array-specific, not a
  general iterator completion:
  - skip the local `asm_gencall_sload()` type guard for `IRSLOAD_KEYINDEX`
  - guard the post-call `lj_vm_next` numeric key lane on the array path
  - allow the direct payload descendant for the exposed
    `parent==root, exit==1, BC_JMP -> BC_ADDVV` side-entry shape
- Direct native classifier runs on `kdz` show the array path is now
  semantically clean under that pair:
  - `pairs_array_sum hot50`: correct after the payload descendant wins
  - `pairs_array_sum hot100`: correct with a true `parent=1 exit=1` side trace
  - `pairs_array_sum hot200`: correct with only the root fallback path
- The same promoted scratch pair is not shippable because it breaks the hash
  iterator family:
  - `tests/s390x/perf/iterator_table.lua` fails immediately on native `kdz`
  - `pairs_sum/small` returns `60000` instead of the expected `56871`
  - tiny direct `pairs_sum` classifiers also stay wrong (`actual=6`, `8`,
    `158`, etc. vs expected `300`)
- The hash path is now classified much more tightly than before:
  - the active root owner is no longer the array-style numeric key guard
  - root `trace 1 exit 4` is the second-half post-call `vload_addr` owner in
    the repeated `lj_vm_next` result cluster
  - the first true side trace on the hash path comes from `parent=1 exit=4`,
    not from `parent=1 exit=1`
- The new first-stop native probes show that hash `exit 4` is not firing on
  random garbage:
  - the first stopped root `exit 4` already has a sane carried total
    (`slot0 = 8`)
  - the `lj_vm_next` value-lane probe reads real table values (`2`, `3`, `4`)
  - the new address-lane probe reads real boxed string-key qwords before the
    failing boundary, then sees the expected nil/end qword from `tmptv`
    (`0xffffffffffffffff`)
- That changes the remaining hash diagnosis:
  - helper ABI is not the blocker
  - KEYINDEX restore ownership is not the blocker
  - recorder-side nil-descendant suppression on `parent=1 exit=4` is not the
    blocker; it was tested and cleanly reverted
  - the remaining red is the hash restart/resume path after a real
    end-of-iteration post-call key-lane exit, not another pre-call guard issue
- So the current iterator state is:
  - array side: classifier is coherent enough to guide a real fix
  - hash side: still incorrect, still the reason no iterator completion patch
    has been committed
  - no promotion or perf restamp is justified until the hash `exit 4` restart
    boundary is fixed

2026-03-26: numeric-key-only descendant widening keeps the split stable

- The next scratch narrowing keeps the deeper payload-descendant allowance only
  for numeric-key iterator families:
  - the direct `parent==root, exit==1, BC_JMP -> BC_ADDVV` payload child is
    still allowed for all iterator roots
  - deeper same-root payload descendants are now allowed only when the control
    var `SLOAD` is tagged with `IRSLOAD_KIDX_NUMKEY`
  - this uses the existing recorder-side `rec_next_types()` classification and
    does not widen the policy for hash iterators
- Native `kdz` hot50 classifiers show the split is now stable instead of
  crashing:
  - `iterator-array-desc2-numkeyonly-hot50-20260326d`
    - `pairs_array_sum:20` stays correct (`500/500`)
    - the array path still builds through `trace 4`
    - the hot owner remains `guardmark=0x427`
  - `iterator-hash-desc2-numkeyonly-hot50-20260326d`
    - `pairs_sum:20` stays correct (`300/300`)
    - the hash path falls back to the older `trace 3 start otr=2 oex=1 ->
      abort otr=8` shape instead of crashing
    - the hot owner remains the pre-call `guardmark=0x509`
- This is useful because it proves the deeper descendant policy is not
  inherently unsafe:
  - array-backed iterators do need the deeper descendant to get past the old
    `trace 3 parent=2 exit=1` `LLEAVE` barrier
  - hash-backed iterators are not ready for the same widening and should stay
    on the older policy until the hash-side restart boundary is fixed
- The current non-promotable state is now:
 - array side: structurally improved, still performance-red
  - hash side: structurally safe again, still blocked on the old
    pre-call `0x509` owner
  - the next real optimization target is no longer “one iterator rule for
    everything”; it is separate array and hash completion work under a stable
    split

2026-03-26: recovered array payload family narrowed to an interpreter bridge seam

- The current array-only scratch path now has a much tighter structural model:
  - `trace 4 exit 1 / guardmark=0x427` is still a legitimate iterator-end split
  - the first recovered payload child is `trace 5`
  - `trace 5` is not equivalent to `trace 6`
  - `trace 5` is an `LJ_TRLINK_INTERP` bridge, while `trace 6` is the first
    real recovered payload loop child
- The field-level `trace 5 -> 6` diff is now explicit:
  - `trace 5`
    - `linktype=6` (`LJ_TRLINK_INTERP`)
    - `nsnap=3`
    - `nins=32778`
    - stop anchored at payload `BC_ADDVV` (`pc ...7b0`, `op=32`)
  - `trace 6`
    - `linktype=2` (`LJ_TRLINK_LOOP`)
    - `nsnap=4`
    - `nins=32782`
    - stop anchored at compiled `BC_JLOOP` (`pc ...7c4`, `op=87`)
- That proves the remaining `5 -> 6` jump is a real trace-shape transition,
  not just another equivalent recovered payload child being missed by hotside
  equivalence.
- The causal reason for that bridge is now also explicit in
  [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c):
  - after the bounded nil window on `parent=4 exit=1`, the parent exit has
    already spent the generic `hotexit + tryside` budget
  - the first recovered payload child therefore hits the generic
    `sidecheck_interp` path and closes as `LJ_TRLINK_INTERP`
  - only the next side trace (`trace 6`) becomes the first real loop child
- A scratch-only bypass of that `sidecheck_interp` cutoff was tested and
  reverted immediately:
  - it was a clean miss
  - instead of producing the first loop child sooner, it made `trace 5`
    re-enter and abort repeatedly on the nil path while `trace 4` stayed hot
- The first useful hotcount-side experiment on this seam is now
  `prime-interp`, a scratch-only classifier in
  [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c):
  - when the active recovered child is exactly that `LJ_TRLINK_INTERP` bridge,
    its `exit 1` hotcount is pre-biased so the next real loop child can form
    on the next hit
  - direct native `kdz` proof:
    - result stays correct (`500/500`)
    - the old collapsed-shape histogram
      `5/1=10, 6/1=10, 7/1=1, 8/1=1, 9/1=111`
      tightens to
      `5/1=1, 6/1=10, 7/1=1, 8/1=120`
    - so one full hotcount stage is removed instead of merely renamed
- This is still not a finish-line fix:
 - the hot owner remains the same legitimate `0x427` iterator-end split
  - the family is merely tighter than before
  - the next live seam is now `trace 6 -> 8`, not `trace 5 -> 6`

2026-03-28: array root continuation chain narrowed from an endless loop ladder to a single post-`trace 4` stall

- The root/continuation ownership model is now structurally explicit:
  - `trace 1` remains the root owner
  - `trace 2` is the continuation stub from `parent=1 exit=4`
  - `trace 3` is the first loop child from `parent=1 exit=1`
- The s390x backend bug in empty pure-`LOOP` stubs is real and separately
  proven in [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h):
  - with `LUAJIT_S390X_EMPTY_LOOP_FALLTHROUGH=1`, the old self-branching
    `mcloop` fixup is corrected
  - this removed the earlier hard hang in `trace 3` and exposed the true
    runtime continuation problem
- The next structural bug was then proven in
  [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c):
  - the `trace 3 -> 4 -> 5 -> 6...` ladder is born in `rec_loop_jit()`
  - focused `kdz` logs showed every descendant hitting:
    - `parent>=3`
    - `exit=0`
    - `ev=2`
    - `samepc=1`
    - `startop=88` (`BC_JMP`)
    - `lnk=1`
  - so the stock `J->pc == J->startpc` rule kept choosing “form extra loop”
    instead of stabilizing onto an existing owner
- The continuation-chain mismatch was also made concrete in
  [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c):
  - `trace 3` was being recorded from `parent=1 exit=1`
  - the preserved continuation owner was still `trace 2`
  - the old guard therefore logged `S390X_CHILD_LINK_SKIP owner=2 ... actual_parent=1`
    and never attached `2 -> 3`
- A narrow root-promotion fix now attaches that child explicitly:
  - `S390X_CHILD_LINK_CHILD owner=2 newlink=3 newlinktype=2 root=1 actual_parent=1`
  - after that, runtime owner resolution changes from `target_exec=2` to
    `target_exec=3`
- A matching recorder-side scratch path now lets the first `3/0` continuation
  link against the loop owner instead of blindly re-rooting to `trace 1`:
  - `S390X_RECLOOP trace=4 parent=3 exit=0 ... link_loop_desc=1 ... lnk=3`
  - `S390X_RECSTOP trace=4 parent=3 exit=0 ... linktype=1 link=3 ...`
- This materially changes the failure mode:
  - before: unbounded pure-`LOOP` ladder (`trace 4`, `5`, `6`, ...)
  - now: the ladder is cut off at `trace 4`
  - the remaining array-side blocker is a single post-`trace 4` execution
    stall, not continuation-family proliferation
- The first post-fix trace dump makes that new stall shape concrete:
  - `trace 3` remains the 24-byte pure-`LOOP` child with `loop=20`
  - `trace 4` is not another identical descendant
  - instead, `trace 4` saves as a one-IR, 40-byte, root-linked stub:
    - `TRACEINFO tr=4 link=3 type=root nins=1 nexit=2`
    - `TRACEMC tr=4 ... loop=0 size=40`

2026-03-28: the first non-stub loop-desc descendant is still an interpreter bridge,
not a reusable loop owner

- Turning the loop-desc stabilization off entirely now gives a clean reference
  chain for the continuation family:
  - traces `4..101` are still tiny pure-loop descendants
  - each one saves with:
    - `parent=prev`
    - `exit=0`
    - `root=1`
    - `startop=88` (`BC_JMP`)
    - `nins=32772`
    - `szmcode=24`
    - `mcloop=20`
    - `linktype=2` (`LJ_TRLINK_LOOP`)
- The first descendant that is *not* another 24-byte pure-loop stub appears
  much later:
  - `trace=102`
  - `parent=101 exit=0`
  - `root=1`
  - `startop=88` (`BC_JMP`)
  - `nins=32770`
  - `szmcode=76`
  - `mcloop=0`
  - `linktype=6` (`LJ_TRLINK_INTERP`)
- So the current continuation family does **not** naturally discover a
  directly-enterable loop owner just by descending past the early stubs.
- The most likely reason is the generic side-trace cutoff in
  [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c):
  - once the long `exit=0` stub chain spends the root side-trace budget, the
    next non-stub continuation candidate falls through `sidecheck_interp` and
    closes as `LJ_TRLINK_INTERP`
- That means “first non-stub descendant” is still not a sufficient ownership
  rule on this seam.

2026-03-28: bypassing the loop-desc `sidecheck_interp` cutoff proves the chain
still has no natural owner

- A narrow recorder-side bypass at the `sidecheck_interp` gate in
  [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c)
  was used only for the `parent>=3, exit=0, root=1, startop=BC_JMP,
  pcop=BC_JLOOP` continuation family.
- With that bypass enabled, the earlier `trace 102 / LJ_TRLINK_INTERP` bridge
  disappears, but the chain does not discover a usable owner behind it.
- Instead it just keeps minting the same tiny pure-loop descendants:
  - `startop=88` (`BC_JMP`)
  - `linktype=2` (`LJ_TRLINK_LOOP`)
  - `nins=32772`
  - `mcloop=20`
- In other words, the earlier `trace 102` bridge was only the generic cutoff.
  It was not hiding a later directly-enterable loop owner.

2026-03-28: giving the pure-loop loop-desc stubs a self-`BC_JLOOP` resume
contract changes execution, but still does not land the chain

- In [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c),
  the pure-loop loop-desc stubs now save:
  - `resumepc = startpc`
  - `resumeins = BC_JLOOP`
- That change is real at runtime:
  - `S390X_CHILD_RESUME_LOOPDESC ... resumeins=87 ... purestub=1`
  - the active `parent>=3 exit=0` stubs stop resuming with `retop=88`
  - they now resume with `retop=87`
- But the chain still does not stabilize:
  - it still grows through the pure-loop descendants
  - it still reaches `trace 102`
  - `trace 102` is still the first `LJ_TRLINK_INTERP` bridge on `exit=1`
- So the missing fix is no longer “the loop-desc stubs need a real resume
  contract.” They now have one. The remaining bug is the bridge/handoff that
  follows that pure-loop family.
  - runtime `texit` output stops at `trace 3 ex=0` once `trace 4` forms, so
    the live problem has moved from “keeps building new traces” to “hangs
    after entering the first stabilized post-`trace 4` continuation”
- So the live seam is no longer iterator data, no longer root ownership
  metadata, and no longer endless descendant formation. It is the execution
  contract after `trace 4` has been saved against loop owner `3`.
- The next s390x-specific VM bug on that seam is now also proven:
  - in [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc),
    static `BC_JLOOP` child selection originally loaded `resumechild` via
    `0(r0, base)`, but s390 ignores `r0` as an index register
  - that meant the child-trace table lookup silently reloaded the base trace
    instead of the selected child
  - switching that indexed load to a nonzero register fixed the first
    continuation-selection crash and let the real post-selection seam appear
- With that fixed, the runtime facts are now:
  - `trace 2` remains the continuation stub with `resumevalid=1`
  - `trace 3` is now explicitly saved as a non-stub loop child with
    `resumevalid=0`
  - `trace 4` still forms as the first stabilized `parent=3 exit=0`
    descendant
- The important new negative results are also now clear:
  - skipping the stop-time raw patchexit into `trace 4` is not enough
    (`trace 3` just keeps re-entering the `trace 2` stub path)
  - one-dispatch retarget from `trace 3` straight to execution owner
    `trace 3` is unsafe and segfaults
  - one-dispatch retarget from `trace 3` to side child `trace 4` is also
    unsafe unless the child carries a real resume contract
- The crash reason for that child-4 static path is now concrete:
  - side traces are born in
    [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c)
    with `J->cur.startins = BCINS_AD(BC_JMP, 0, 0)`
  - so when the VM tries to drive `trace 4` through the static `BC_JMP`
    resume path, it decodes `OP=BC_JMP` with `D=0` and dies in
    `lj_vm_exit_interp` at the `branchPC` decode path
  - that proves `trace 4` cannot be used through the raw `startins` static
    `BC_JMP` path as currently saved
- So the seam narrowed again:
  - `trace 4` either needs a valid saved `resumepc/resumeins` continuation
    contract, or it must remain a raw-mcode-only side entry
  - any further work on `trace 4` has to respect that distinction instead of
    treating its placeholder `startins` as a real branch instruction

2026-03-29: collapsing the pure-loop family to `trace 5` makes owner
selection real, but still does not transfer execution

- A narrow recorder-side stop rule in
  [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c)
  now cuts the old endless `parent>=3 exit=0` pure-loop family off early:
  - `trace 5` forms from `parent=4 exit=0`
  - it saves as `startop=88` (`BC_JMP`), `link=4`, `linktype=1`
- With the existing `LUAJIT_S390X_JLOOP_EXEC_CHILD` path enabled in
  [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c),
  runtime owner selection now really advances:
  - repeated `S390X_JLOOP_EXIT ... parent=4 exit=0 ... target=5 target_exec=5`
- But there are still zero `parent=5` or `trace=6` events in the focused run.
  That proves the old blocker is gone:
  - runtime is no longer stuck on selecting owner `4`
  - it is now selecting owner `5`
  - but execution still does not transfer into the selected owner
- The immediate control-flow reason is now explicit:
  - the `BC_JLOOP` case in
    [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c)
    still ends that `target=5 target_exec=5` path with `return 0`
  - `vm_exit_interp` only takes the static patched-dispatch path for
    `BC_JLOOP` when `trace_exit()` returns `-17`
  - so owner selection alone is not enough to enter `trace 5`

2026-03-29: forcing the patched-dispatch path for the selected loop-desc owner
is not a safe fix

- A scratch experiment in
  [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c)
  tried to force the existing `-17` patched-dispatch path when a selected
  root-owned `BC_JMP` trace had a valid resume contract.
- Two cuts were tried:
  - a broad version, which misfired earlier on the `1 -> 2` root-child seam
    and crashed after `phase=dispatch-exec-resume-bc parent=1 exit=1 target=2`
  - a narrowed `parent>=3 exit=0` loop-desc-only version, which still crashed
    before the `4 -> 5` seam could validate
- So the remaining bug is not just “return `-17` instead of `0`”.
  The patched-static-dispatch contract itself is not valid for this selected
  loop-desc owner path as currently shaped.
- The live seam is now narrower again:
  - `trace 5` is a real selected execution owner
  - but neither plain `return 0` nor forced patched-dispatch currently enters
    it safely
 - the next fix has to be in how `vm_exit_interp` and the `BC_JLOOP`
    continuation path consume the selected owner contract, not in recorder
    stop policy or owner discovery

2026-03-29: the exact `parent=3 exit=0` owner/self branch is real, and the
old `trace 3` transfer question is closed

- A focused branch in
  [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c)
  now handles only the exact current-owner seam:
  - `parent == trace`
  - `exit == 0`
  - `execno == traceno`
  - `use_resume_contract == 1`
  - current opcode `BC_JLOOP`
- On `kdz`, that branch really fires:
  - `S390X_JLOOP_EXIT phase=exec-self-pred ... use_resume=1 idle=1 parent_is_trace=1 exec_is_trace=1 is_jloop=1`
  - followed by `phase=exec-self-reenter`
  - and the current `BC_JLOOP` retargets from `bcd=2` to `bcd=3`
- That closes the old fork:
  - the runtime is no longer failing because owner/self re-entry is not being
    selected
  - the branch selection is real
- The first remaining crash on that branch was inside `trace 3` mcode at
  `mcode + 0x4c`, which narrowed the bug from selection to post-transfer
  execution

2026-03-29: `trace 3` was being forced into direct owner entry too early; the
save-time owner bit had to be cleared on the real birth path

- The first attempt to skip `mcloop` entry for `trace 3` was a miss because it
  targeted `parent>=3 exit=0`, but `trace 3` is actually born from
  `parent=1 exit=1`
- Updating the exact save-time predicate in
  [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c)
  to match the real `trace 3` birth path changed the runtime shape
  materially:
  - `trace 3` keeps `resumeop=87` (`BC_JLOOP`)
  - but now saves with `ownerop=0`
  - so `vm_exit_interp` no longer treats it as directly enterable owner mcode
- This removed the old `trace 3` VM-child-entry crash surface:
  - the corrected run no longer emits `S390X_VM_CHILD_ENTRY trace=3`
  - instead, it repeatedly takes the exact `phase=exec-self-reenter` path
    under `parent=3 exit=0`
- That is the strongest current proof that the old `trace 3` crash was not
  the true endgame bug. It was caused by forcing direct owner-entry on a trace
  that still needed to be consumed through the static self-`JLOOP` path.

2026-03-29: with the corrected `trace 3` save path, the seam advances cleanly
to `trace 4`

- On the corrected baseline, the focused `kdz` run now gets past the old
  `trace 3` entry failure and forms:
  - `S390X_BCJMP_STOP trace=4 parent=3 exit=0 root=1 startop=88 link=2 linktype=1 nins=32770`
  - `S390X_TRACE_META phase=stop trace=4 ... mcode=... szmcode=40 mcloop=0`
- The run still segfaults afterward, but the fault site has moved past the old
  `trace 3` transfer seam:
  - `trace 3` no longer crashes on entry
  - the remaining failure is now later, after `trace 4` has been formed
- So the active seam has advanced again:
  - no more work is needed on `trace 3` owner selection itself
  - the next fix target is the post-`trace 4` continuation/entry contract on
    top of this corrected `trace 3` baseline

2026-03-30: the recovered `trace 4` baseline is only authoritative with
`SKIP_PATCHEXIT`, and that exposed a real `BC_JLOOP` resume-contract fault

- The current valid `kdz` baseline needs all of:
  - `LUAJIT_S390X_JLOOP_EXEC_SKIP_MCLOOP=1`
  - `LUAJIT_S390X_VM_CHILD_SKIP_MCLOOP=1`
  - `LUAJIT_S390X_SKIP_PATCHEXIT_BCJMP_LOOPDESC=1`
- Without the `SKIP_PATCHEXIT` gate, the branch can still appear to “stall at
  `trace 4`”, but that result is not authoritative for the real bridge seam.
- Re-running the 60s low-noise classifier on `kdz` with the full safe
  baseline changed the result materially:
  - highest trace still `4`
  - latest non-stub still `trace 4`
  - but the run now died with `SIGSEGV` instead of timing out cleanly
- A focused `gdb` stop on that exact surface showed the first real fault at:
  - `lj_BC_JLOOP+12`
  - instruction `lg %r6,0(%r6,%r1)`
  - `r6 = 0x3ffe8`
- That means the post-`trace 4` bridge path was reaching interpreter-side
  `BC_JLOOP` static dispatch with a garbage trace operand. The live bug had
  moved below ownership and below `trace 4` formation, into the exact resume
  contract consumed after the bridge stop.

2026-03-30: consuming the exact `trace 4` bridge through the linked trace in
`vm_exit_interp` removes the `BC_JLOOP` crash, but still does not advance
beyond `trace 4`

- In
  [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc),
  the exact bridge-stub path in `vm_exit_interp` now resolves the selected
  `trace 4` bridge through its linked trace before static dispatch, instead of
  consuming `trace 4`'s own saved continuation contract directly.
- That changed the seam again in the right direction:
  - the `lj_BC_JLOOP` garbage-`RD` crash is gone on the full safe baseline
  - the authoritative 60s classifier is back to a clean timeout
  - highest trace remains `4`
  - latest non-stub remains `trace 4`
- So the VM-side linked-trace bridge consumption is directionally correct and
  removes a real bridge-contract fault, but it is not yet sufficient to carry
  execution past the real `trace 4` seam.

2026-03-30: the exact `trace 4` bridge-child reenter path is now proven live
on the full safe baseline

- On `kdz`, with the full safe baseline including:
  - `LUAJIT_S390X_JLOOP_EXEC_SKIP_MCLOOP=1`
  - `LUAJIT_S390X_VM_CHILD_SKIP_MCLOOP=1`
  - `LUAJIT_S390X_SKIP_PATCHEXIT_BCJMP_LOOPDESC=1`
  the focused `parent=3 exit=0` run now repeatedly reaches:
  - `target=4`
  - `target_exec=3`
  - `phase=loopdesc-child-query ... child=4`
  - `phase=loopdesc-bridge-child-reenter`
- That closes the old uncertainty around the exact bridge handoff. The
  `trace 4` path is no longer merely forming and timing out; the runtime is
  actively selecting it and repeatedly taking the exact bridge-child reenter
  branch.

2026-03-30: after removing the last C-side resume overrides, the `trace 4`
bridge now exposes a pure static-dispatch seam

- Narrowing
  [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c)
  so the exact bridge stub no longer inherits:
  - the current trace's self-`JLOOP` resume contract
  - the linked exec trace's self-`JLOOP` resume contract
  changed the focused `parent=3 exit=0` logs again.
- The live bridge seam now shows:
  - `target=4`
  - `target_exec=3`
  - `phase=loopdesc-bridge-child-reenter`
  - `retop=88` (`BC_JMP`)
  - while `target 4` still advertises `target_resumeop=82` (`BC_ITERL`)
- So the remaining blocker is now narrower again:
  - the bridge-child handoff itself is real
  - the old C-side `retop=87` override is gone
  - the runtime is now stalling on the pure static `BC_JMP` bridge path
    after `loopdesc-bridge-child-reenter`
- That makes the next exact seam VM-side: how `vm_exit_interp` consumes the
  selected `trace 4` bridge once all of the earlier C-side resume overrides
  have been stripped away.

2026-03-30: the exact `trace 4` bridge handoff is live, but the remaining
stall is now below the C-side `JLOOP_EXIT` view

- On `kdz`, the focused `parent=3 exit=0` run now repeatedly shows the full
  bridge handoff:
  - `target=4`
  - `target_exec=3`
  - `phase=loopdesc-child-query ... child=4`
  - `phase=loopdesc-bridge-child-reenter`
- Narrowing the bridge case so it no longer inherits:
  - the current trace's self-`JLOOP` resume contract
  - the linked exec trace's self-`JLOOP` resume contract
  removed the last visible C-side resume override.
- The seam now reports:
  - `retop=88`
  - while `target 4` still advertises `target_resumeop=82`
- So the bridge-child handoff itself is no longer hypothetical. The remaining
  bug is now below the C-side `JLOOP_EXIT` reporting layer, in the exact VM
  static-dispatch consumption that happens after `return -17`.

2026-03-30: a mixed VM bridge contract did not change the visible seam

- In
  [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc),
  a follow-up experiment tried a more precise bridge consumption rule:
  - keep the linked trace for bridge context
  - but dispatch using the bridge stub's own `resumepc/resumeins`
- On the same focused `kdz` surface, that did not change the visible C-side
  handoff:
  - the run still times out
  - `phase=loopdesc-bridge-child-reenter` still repeats
  - `retop` still presents as `88` in `JLOOP_EXIT`
- So the next useful probe is no longer another C-side retop rewrite. It is a
  VM-side bridge-consumption probe that logs or inspects the post-`-17`
  static opcode choice directly.

2026-03-30: the VM bridge probe shows the exact post-`-17` static contract
now being consumed

- A narrow VM-side probe in
  [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc)
  and
  [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c)
  now logs the static opcode and PC actually chosen after the exact
  `loopdesc-bridge-child-reenter` handoff.
- On `kdz`, the post-`-17` bridge dispatch is now explicit:
  - linked trace context is `trace 3`
  - static `pc = trace4.resumepc = ...cc78`
  - static opcode is `BC_ITERL` (`op=82`)
  - encoded instruction is `0x7ffd0b52`
- So the remaining seam is no longer “which resume contract does the bridge
  use”. That question is now answered:
  - the exact bridge handoff is consuming `trace 4`'s own `BC_ITERL`
    continuation contract
  - and it is doing so under linked `trace 3` context
- The visible C-side `retop=88` line is now known to be stale for this seam.
  The real post-`-17` VM choice is `BC_ITERL`.
- That moves the live blocker down one more level:
  - no longer bridge ownership
  - no longer bridge-child handoff
  - no longer resume-contract selection
  - specifically the resumed `BC_ITERL` path after the exact `trace 4`
    bridge dispatch

2026-03-30: the stable bridge seam does not re-enter the `IITERL` body

- On the recovered `kdz` baseline, the stable VM probes now give one more
  exact result:
  - `S390X_VM_BRIDGE_DISPATCH` repeats after
    `phase=loopdesc-bridge-child-reenter`
  - the VM repeatedly chooses linked-trace context `trace=3`
  - and static `pc = trace4.resumepc` with `op=82` (`BC_ITERL`)
- But the existing `S390X_VM_ITERL` helper in the `BC_IITERL` body fires only
  once, and it happens before the bridge loop starts:
  - `pc=...cc7c`
  - `op=79`
  - then root promotion to `trace 2` and `trace 3`
- So the current bridge seam is not repeatedly executing the `IITERL` body.
  It is repeatedly resolving to a static `BC_ITERL` dispatch state and
  stalling before a real `IITERL` body cycle happens again.
- I also tried two deeper `ITERL`-front-edge classification cuts and rejected
  both:
  - adding pre/post-`hotloop` helper calls at `BC_ITERL` destabilized the VM
    and segfaulted before the bridge seam; that probe is too intrusive to
    trust
  - forcing `-Ohotloop=1000000` on the same branch also failed too early, with
    zero bridge-dispatch hits and zero `VM_ITERL` hits; that is not a valid
    classification surface for this seam
- Consequence:
 - the trustworthy result is still the stable one: post-`-17` bridge
    dispatch resolves to `BC_ITERL`, but the resumed `ITERL/IITERL` body is
    not actually re-entered in a stable way
  - next work should move to debugger-level observation on the stable branch,
    not more intrusive `BC_ITERL` helper surgery or broad `hotloop`
    overrides

2026-03-30: the exact `trace 4` bridge bug in `vm_exit_interp` was a wrong
branch target, and fixing it moves the seam beyond the bridge

- Debugger work on `kdz` finally showed why the exact `trace 4` bridge never
  reached `BC_ITERL`/`BC_IITERL` even though the VM bridge probe reported
  `op=82` at `trace4.resumepc`:
  - after `lj_trace_s390x_vm_bridge_dispatch_log` returned, control resumed at
    `lj_vm_exit_interp+422`
  - the bridge branch used `j >6`
  - in the current local-label layout, that `>6` lands in the child-entry
    block at `lj_vm_exit_interp+552`, not in the later static decode path
  - the bridge therefore kept re-entering child-entry machinery instead of
    dispatching the saved static opcode
- I patched the exact bridge branch in
  [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc)
  to inline the static opcode decode and dispatch directly, instead of
  jumping through the ambiguous local label.
- That changed the runtime surface immediately:
  - the endless bridge loop is gone
  - the branch now gets past the bridge
  - the first new fault is later, not at `trace 4`

2026-03-30: the first honest post-bridge fault is now in
`lj_vmeta_istype -> lj_meta_istype`

- After the bridge-target fix, a clean `gdb` run on `kdz` reaches a new crash:
  - `SIGSEGV` in `lj_meta_istype` at `lj_obj_itypename[tp]`
  - caller is `lj_vmeta_istype`
- The current crash shape:
  - `r9 = 0x27` at `lj_vmeta_istype`
  - `lj_meta_istype` is trying to index the type-name table with that invalid
    type lane
  - this is later than the old bridge seam and later than the old `IITERL`
    suspicion
- I also confirmed one intermediate crash was self-inflicted instrumentation:
  - the temporary `lj_trace_s390x_vm_iterl_log` call inside `BC_IITERL`
    clobbered `%r4` across the C call and crashed at `lg %r7,0(%r4)`
  - that probe has been removed from
    [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc)
- Current state:
  - the `trace 4` bridge handoff is no longer the active blocker
  - the live seam is now the resumed interpreter path after the fixed bridge,
    specifically the bad `vmeta_istype/meta_istype` argument lane

2026-03-30: `lj_vmeta_istype` is entered with bogus decode-state registers

- A focused `gdb` breakpoint on `lj_vmeta_istype` on the cleaned post-bridge
  branch shows the new failure is not a normal `ISTYPE/ISNUM` fallback with
  sane operands.
- At entry to `lj_vmeta_istype`, the live register state is already wrong:
  - `r4 = 0`
  - `r6 = 0`
  - `r9 = 0x27`
  - `r13 = 0x...2cb8` (base is real)
- The entry sequence is:
  - `llgfr %r3,%r4`
  - `llgfr %r4,%r6`
  - `stg %r9,168(%r15)`
  - `brasl ... lj_meta_istype`
- So the later `lj_meta_istype` crash is downstream of already-corrupted
  resumed decode state. This is not just a bad C helper call or a bad
  type-name table lookup in isolation.
- Current consequence:
  - the bridge handoff itself is now working far enough to expose the next
    seam
  - the next exact target is resumed interpreter decode-state reconstruction
    after the fixed bridge, not bridge ownership or `trace 4` dispatch

2026-03-30: a residual root-owned `BC_ITERN` resumepc/resumeins restart was
still live in `vm_exit_interp`, and removing it clears the tiny early crash

- A minimal iterator reproducer (`run(1)`, `run(5)`, `run(20)`) on the
  authoritative `kdz` baseline exposed an earlier fault than the long
  handoff harness:
  - only `trace 2` and `trace 3` promotion logs appeared
  - then the process segfaulted in `lj_vm_exit_interp+444`
- `gdb` plus `objdump` on the rebuilt remote binary mapped that crash to the
  static dispatch path in
  [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc),
  not to the exact bridge branch:
  - the faulting block was still trying to restart certain root-owned traces
    from `resumepc/resumeins`
  - that path still carried the previously rejected `BC_ITERN` rewind logic
- I removed that exact fallback locally:
  - root-owned `BC_JMP` traces may still use `resumepc/resumeins`
  - root-owned `BC_ITERN` traces now stay on `startpc/startins`
- Result on `kdz` after rebuild:
  - the tiny reproducer no longer segfaults in `lj_vm_exit_interp+444`
  - it now rebuilds cleanly and times out instead
- Consequence:
  - the rejected `BC_ITERN` rewind was not fully backed out before
  - the current branch is cleaner: the early tiny-script crash is gone, and
    the next work should classify where the tiny reproducer now stalls rather
    than revisiting that restart experiment

2026-03-30: the cleaned tiny reproducer reaches the same `BC_IITERL` replay
surface as the long handoff harness

- After removing the residual `BC_ITERN` rewind, a no-log `gdb` stop on
  `lj_BC_IITERL` confirms the tiny reproducer is not on a different early
  seam:
  - it reaches `trace 2` and `trace 3` promotion first
  - then it stops in `lj_BC_IITERL`
  - by hit `50`, the live iterator-tail state is still:
    - `RD = 32765`
    - `BASE = r13 = 0x...2ce8`
    - `BASE+80 = 0xfff9000000000003` (boxed integer `3`)
    - the surrounding carried lanes are already `nil`
- So the tiny reproducer is now converging with the long harness instead of
  exposing a separate earlier crash:
  - same `BC_IITERL` replay surface
  - same “iterator tail is being re-entered without fresh progress” pattern
- Consequence:
  - the current fast reproducer is trustworthy for the next seam
  - next work should stay on the no-log branch and inspect why post-bridge
    `IITERL` is replaying a frozen carried bundle instead of returning to a
    fresh iterator-state update

2026-03-30: the tiny reproducer's replayed `IITERL` bundle is a frozen
`[2, 2, 3, nil, ...]` operand lane set

- A second `gdb` stop on the cleaned tiny reproducer at `lj_BC_IITERL` hit
  `50` adds the missing lane detail:
  - `r13 = BASE = 0x...2ce8`
  - `BASE+64 = 0xfff9000000000002`
  - `BASE+72 = 0xfff9000000000002`
  - `BASE+80 = 0xfff9000000000003`
  - `BASE+88` and later nearby lanes are `nil`
- So the replay is not just “same PC, same RD”. The actual operand bundle is
  frozen too:
  - a stable boxed `2`
  - a second stable boxed `2`
  - then boxed `3`
  - then `nil`
- This fits the VM source shape in
  [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc):
  - `BC_IITERL` only consumes/stores the already-produced iterator result
  - real iterator progress happens earlier in `BC_ITERN`, which updates the
    control var and value slots
- Consequence:
  - the post-bridge replay seam is now specific
  - next work should explain why the cleaned branch keeps re-entering
    `BC_IITERL` with that frozen `[2, 2, 3, nil, ...]` bundle instead of
    returning to a fresh `ITERN` update

2026-03-30: the repeated tiny `IITERL` stop sits in a `JLOOP/ITERL/FORL`
tail region, not next to `ISNEXT/ITERN`

- A focused `gdb` stop at tiny `lj_BC_IITERL` hit `50` shows the live
  bytecode words around `vm_pc`:
  - `0x00040b57`
  - `0x7ffd0952`
  - `0x7ff8024f`
  - `0x00010236`
- Decoded semantically, the active local neighborhood is:
  - `BC_JLOOP`
  - `BC_ITERL`
  - `BC_FORL`
  - then later body bytecode
- So by the time the replay seam is hot, the resumed interpreter is no
  longer adjacent to `ISNEXT/ITERN`. It is running in a tail-only
  `JLOOP/ITERL/FORL` region.
- This matches the asymmetry from the debugger:
  - `lj_BC_IITERL` reaches hit `50` quickly
  - `lj_BC_ITERN` does not
- Consequence:
  - the frozen `[2, 2, 3, nil, ...]` bundle is not surprising anymore
  - the resumed path is bypassing the state-refreshing iterator body in the
    local bytecode stream
  - next work should focus on how the bridge/root continuation hands control
    back into this tail-only region, and what exact pre-tail refresh step is
    missing

2026-03-30: the exact `trace 4` bridge is born with a consumer-side `ITERL`
contract, while the linked trace saves a different producer-side `JLOOP`
contract

- A safe `S390X_BRIDGE_META` dump at `trace 4` formation on the authoritative
  `kdz` tiny repro now gives the exact contract split:
  - `trace=4 parent=3 exit=0 root=1 link=3 linktype=LJ_TRLINK_ROOT`
  - `resumepc = startpc + 4`
  - `resumeins = 0x7ffd0952`
  - `resumeop = BC_ITERL`
  - local resumed neighborhood:
    - `prev2 = 0x010a0120` (`BC_ADDVV`)
    - `prev1 = 0x00010b57` (`BC_JLOOP`, target `1`)
    - `next1 = 0x7ff8024f` (`BC_FORL`)
  - linked trace `3` saves a different contract:
    - `link_resumepc = link_startpc`
    - `link_resumeins = 0x00030057`
    - `link_resumeop = BC_JLOOP`
- So the bridge has three distinct candidate contracts in play:
  - the bridge's own saved consumer contract: `BC_ITERL`
  - the live local preceding carrier: `BC_JLOOP` to trace `1`
  - the linked trace's saved producer contract: `BC_JLOOP` to trace `3`
- This is the clearest proof yet that the missing step is between those
  contracts, not identical to any one of them:
  - bridge `ITERL` is too late
  - live local `JLOOP 1` is the wrong carrier
  - linked saved `JLOOP 3` is a different, more plausible producer-side
    contract

2026-03-30: exact VM bridge dispatch through the linked trace's saved
`JLOOP 3` contract rewinds too far and suppresses `trace 4`

- I tested one exact VM-side experiment in
  [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc):
  - for the exact `trace 4` bridge family only
  - after selecting the bridge stub
  - consume the linked trace's saved `resumepc/resumeins`
    (`0x00030057`, `BC_JLOOP`) instead of the bridge's saved
    `BC_ITERL` contract
- This is a reject.
- On `kdz`, the tiny repro no longer converges on the later `trace 4`
  bridge seam. The focused `parent=3 exit=0` run rewinds to the earlier loop:
  - `trace=3`
  - `target=1`
  - `target_exec=2`
  - `retop=BC_LOOP`
  - repeated `phase=resume-linked`
- A low-noise 20s follow-up also shows no `S390X_RECSTOP trace=4` at all.
- So the linked trace's saved `JLOOP 3` contract is too early. It rewinds the
  continuation before the actual bridge seam instead of materializing the
  missing refresh step immediately before the tail.
- Consequence:
  - the correct post-bridge fix is not “use bridge `ITERL` as-is”
  - and not “use the linked trace's saved `JLOOP 3` contract verbatim”
  - the remaining missing step lies between those two contracts
  - the next target should stay narrow:
    - identify the exact producer-side refresh step between
      `link_resumeins = BC_JLOOP 3`
      and
      `trace4.resumeins = BC_ITERL`
    - likely an explicit producer refresh or a later producer-owned resume
      contract, not a full rewind to the linked trace's saved `JLOOP`

2026-03-30: the live `exec=2` continuation stub is also consumer-side, so the
missing refresh step is not present in any saved trace resume contract seen so
far

- On the restored stable branch, a focused `parent=3 exit=0` run on `kdz`
  shows the earlier live handoff again before `trace 4` forms:
  - `target=1`
  - `target_exec=2`
  - `retop=BC_LOOP`
  - repeated `phase=resume-linked`
- The same run also prints the active exec-trace contract:
  - `trace=3 exec=2`
  - `exec_startop=BC_JMP`
  - `exec_resumepc = startpc + 4`
  - `exec_resumeop = BC_ITERL`
  - `exec_resumechild = 3`
- So trace `2` is not a hidden producer-side resume target. It is another
  continuation stub whose saved contract is also consumer-side `BC_ITERL`.
- Combined with the earlier bridge dump:
  - `trace4.resumeins = BC_ITERL` is too late
  - live local `prev1 = BC_JLOOP 1` is the wrong carrier
  - linked `trace3.resumeins = BC_JLOOP 3` is too early
  - exec `trace2.resumeins = BC_ITERL` is also too late
- Consequence:
  - the missing pre-tail refresh step is not currently materialized as any of
    the saved trace resume contracts in the live `1 -> 2 -> 3 -> 4` family
  - the next target should therefore shift from “pick the right saved
    resumepc/resumeins pair” to “identify or synthesize the producer-side
    refresh step immediately before the `ITERL` tail”

2026-03-30: exact VM bridge dispatch through root trace `1`'s original
`BC_ITERN` start contract also rewinds too far

- After the bridge-meta dump proved the local preceding carrier had become
  `BC_JLOOP 1`, I tested one narrower producer-side cut in
  [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc):
  - only for the exact `trace 4` bridge family
  - if `trace4.resumepc - 4` is local `BC_JLOOP 1`
  - dispatch root trace `1`'s original `startpc/startins`
    (`BC_ITERN`) instead of the bridge's saved `BC_ITERL` tail
- This is also a reject.
- On `kdz`, the tiny repro stays stable but the focused `parent=3 exit=0` seam
  rewinds to the same earlier loop as the other “too early” producer cuts:
  - `target=1`
  - `target_exec=2`
  - `retop=BC_LOOP`
  - repeated `phase=resume-linked`
  - `exec=2` still advertising its own consumer-side
    `exec_resumeop=BC_ITERL`
- So root trace `1`'s original `BC_ITERN` contract is still too early when
  consumed verbatim from the bridge path. It does not bridge into the missing
  refresh step; it simply collapses the run back into the earlier
  `1/2/3` continuation regime.
- Combined with the earlier rejects:
  - bridge `trace4.resumeins = BC_ITERL` is too late
  - local `prev1 = BC_JLOOP 1` carrier is not itself a safe landing target
  - linked `trace3.resumeins = BC_JLOOP 3` is too early
  - root `trace1.startins = BC_ITERN` is also too early when consumed
    directly from the bridge
- Consequence:
  - the missing producer-side refresh step is still not represented by any
    existing saved or live contract in the family
  - the next target should be narrower than “pick another saved trace
    contract”:
    - inspect or synthesize the exact refresh state that `BC_ITERN` needs
      immediately before the tail, without rewinding all the way back to the
      root-owner continuation loop

2026-03-30: even a synthetic current-frame `BC_ITERN` immediately before the
bridge tail rewinds into the earlier `BC_LOOP` regime

- I tested one narrower VM-side cut in
  [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc):
  - only for the exact `trace 4` bridge family
  - keep the current bridge frame
  - replace the bridge's saved `BC_ITERL` decode with a synthetic `BC_ITERN`
    instruction that reuses the live `A=9` field
  - and place `PC` one slot earlier so the synthetic producer runs
    immediately before the existing `ITERL` tail
- This is also a reject.
- On the stable safe bundle, the tiny repro remains stable but rewinds into
  the earlier continuation regime instead of landing the bridge seam:
  - repeated `parent=3 exit=0`
  - `target=1`
  - `target_exec=2`
  - `retop=BC_LOOP`
  - `phase=resume-linked`
- Re-running the same synthetic producer cut with the repaired producer/table
  envs:
  - `LUAJIT_S390X_ROOT_ITERN_SETUP_ITERN=1`
  - `LUAJIT_S390X_ROOT_RESUME_PRECALL_TAB=1`
  - `LUAJIT_S390X_LOOPDESC_BRIDGE_PRECALL_TAB=1`
  still does not land. It shows the same earlier `BC_LOOP` replay surface,
  while `trace 4` can still form later as a separate classifier.
- Consequence:
  - the missing producer-side refresh step is not fixed by swapping in any
    existing or synthetic `BC_ITERN` contract wholesale
  - the remaining seam is now below “which producer instruction to resume”
    and closer to “which exact producer inputs or frame lanes must be
    materialized before the tail”

2026-03-30: direct bridge-local `vm_IITERN` body entry with the original
producer `A` field is also too coarse

- I tested a narrower VM-only cut in
  [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc):
  - keep the exact `trace 4` bridge-local `PC`
  - keep the bridge-selected runtime context
  - do not consume any saved `resumepc/resumeins` contract verbatim
  - instead, jump directly into `->vm_IITERN`
  - with `RA` taken from root trace `1`'s original `BC_ITERN` instruction,
    so the producer body uses the original iterator base operand rather than
    the live patched `BC_JLOOP` carrier
- This is also a reject.
- On `kdz`, the tiny repro stays stable, but the focused seam still collapses
  into the earlier loop:
  - repeated `parent=3 exit=0`
  - `target=1`
  - `target_exec=2`
  - `retop=BC_LOOP`
  - `phase=resume-linked`
- `trace 4` can still form later as a separate classifier, but the direct
  `vm_IITERN` body entry does not reach a new post-bridge producer-refresh
  surface.
- Consequence:
 - the missing step is not any saved carrier contract
  - and it is not the whole reusable `vm_IITERN` body either
  - the remaining seam is narrower still: some producer-owned state or
    fallthrough contract that sits below full `vm_IITERN` entry and above the
    replayed `ITERL` tail

2026-03-30: strengthened `loopdesc-bridge-child-reenter` handoff logging shows
the earlier `resume-linked` loop is still unpatched

- I extended the exact `loopdesc-bridge-child-reenter` log in
  [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c)
  to dump the concrete handoff state:
  - live `pc`
  - current `ins`
  - `prev1` / `next1`
  - `patchpc` / `patchins`
  - `resume_bcpc`
  - `target_resumeins`
- A focused `parent=3 exit=0` run on `kdz` with that broader `JLOOP_EXIT`
  logging still does not reach `loopdesc-bridge-child-reenter`. It remains in
  the earlier loop:
  - repeated `target=1`
  - `target_exec=2`
  - `retop=BC_LOOP`
  - `phase=resume-linked`
- The important new constraint is that every one of those repeated earlier
  passes still shows:
  - `patchpc=(nil)`
  - `retpc=0x...b960`
  - live `pc=0x...cc74`
  - `op=BC_JLOOP`
- Consequence:
 - when the run is still sitting in the earlier `1/2/3` continuation regime,
    it is not inheriting a stale bytecode patch from the C side
  - so the missing post-`trace 4` handoff is not being masked by a leftover
    `patchpc/patchins` pair during the earlier loop
  - the next useful cut should stay low-noise and isolate the exact
    `loopdesc-bridge-child-reenter` branch without turning on the full
    per-exit `parent=3 exit=0` logger

2026-03-30: low-noise bridge query/reenter gates prove the gap is still before
any bridge-family child selection

- I added two low-noise scratch gates in
  [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c):
  - `LUAJIT_S390X_BRIDGE_CHILD_QUERY_LOG=1`
  - `LUAJIT_S390X_BRIDGE_CHILD_REENTER_LOG=1`
- On `kdz`, with the safe baseline plus only:
  - `RECSTOP`
  - `TRACE_META`
  - `VM_BRIDGE_DISPATCH`
  - `BRIDGE_CHILD_QUERY`
  - `BRIDGE_CHILD_REENTER`
  the authoritative `iter-chain-handoff.lua` run still reaches:
  - `S390X_RECSTOP trace=4 parent=3 exit=0`
  - `S390X_TRACE_META phase=stop trace=4 ... startop=88 link=3 linktype=1 nins=32770 mcloop=0`
- But within the same 60 second window it shows:
  - no `loopdesc-child-query`
  - no `loopdesc-bridge-child-reenter`
  - no `S390X_VM_BRIDGE_DISPATCH`
- Consequence:
  - the live gap is now even earlier than the bridge-family child selection
  - after `trace 4` is formed, control still disappears before the exact
    bridge `JLOOP_EXIT` child-query path is reached
  - the next useful target should therefore move earlier than
    `loopdesc-child-query`, not later than it

2026-03-30: exact `target=4` `JLOOP_EXIT` logging is too invasive for this seam

- I tried one narrower scratch probe in
  [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c):
  log only `BC_JLOOP` exits whose computed `targetT->traceno == 4`, without
  enabling the broad focused `parent=3 exit=0` stream.
- That probe is not trustworthy. On `kdz`, the run regressed immediately:
  - it segfaulted during the first 60 second window
  - the resulting log never progressed beyond `trace 1`
  - there were still no `target=4`, `loopdesc-child-query`,
    `loopdesc-bridge-child-reenter`, or `VM_BRIDGE_DISPATCH` lines
- Consequence:
 - even very narrow additional `lj_trace_exit()` logging is now perturbing
    the seam enough to become self-invalidating
  - the correct next move is debugger-level observation from an earlier
    stable point, not more logging added to the target-4 `JLOOP_EXIT` path

2026-03-30: debugger proves the first post-`trace 4` exit is still the local
`JLOOP 1` carrier

- A focused `gdb` pass on `kdz` now breaks correctly on:
  - `trace_stop()` for `trace 4`
  - then the first `lj_trace_exit()` site after that stop
- The key result is:
  - `TRACE4_STOP parent=3 exit=0 startins=0x00000058 link=3 linktype=1 root=1`
  - `FIRST_EXIT_AFTER4 parent=3 exit=0 pc=0x...cca4 ins=0x00010d57 prev=0x030c0320 next=0x7ffd0b52`
- Interpreting the live bytecode words:
  - `prev = 0x030c0320` is the local `ADDVV`
  - `ins = 0x00010d57` is still `BC_JLOOP 1`
  - `next = 0x7ffd0b52` is the local `BC_ITERL`
- Consequence:
  - after `trace 4` is born, the first real post-stop exit still comes
    straight back to the same local `ADDVV ; JLOOP 1 ; ITERL` carrier
  - bridge-family child selection is not even being attempted yet
  - this explains why the low-noise bridge runs showed:
    - `trace 4` stop formation
    - but no `loopdesc-child-query`
    - no `loopdesc-bridge-child-reenter`
    - and no `VM_BRIDGE_DISPATCH`

2026-03-30: exact stop-time retarget of the live carrier to `trace 4` unlocks
the bridge family again

- I tested one exact scratch rule in
  [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c),
  behind `LUAJIT_S390X_STOP_RETARGET_LOOPDESC=1`:
  - only for the exact `trace 4` bridge stop shape that already matches
    `SKIP_PATCHEXIT_BCJMP_LOOPDESC`
  - if the live site is still `BC_JLOOP`, rewrite that live carrier from its
    old target to the just-formed bridge trace number at `trace_stop()`
- On `kdz`, this immediately changes the low-noise runtime shape:
  - `S390X_STOP_RETARGET trace=4 ... oldins=0x00010d57 newins=0x00040d57`
  - then repeated:
    - `phase=loopdesc-child-query ... target=4 exec=3 child=4`
    - `phase=loopdesc-bridge-child-reenter ... ins=0x00040d57`
    - `S390X_VM_BRIDGE_DISPATCH ... pc=trace4.resumepc op=82`
- This is the first proof on the stable low-noise baseline that the missing
  gap really was the live carrier, not bridge discovery:
  - once the live `JLOOP` is retargeted from `1` to `4`
  - the exact bridge child-query and bridge dispatch paths light up again
- The first dispatch payloads are also informative:
  - `raw_tab` is a tagged GC pointer
  - `raw_key = 2`
  - `raw_val = 3`
  - `raw_ctl` is initially odd on the first dispatch
    (`0xfffe7fff00000057`)
    but quickly stabilizes to numeric `2`
- Consequence:
  - the post-`trace 4` gap was not “VM bridge code never reachable”
  - it was “the live bytecode carrier never retargeted to the bridge”
  - with stop-time retarget enabled, the live seam moves back to the repeated
    bridge-dispatch / `BC_ITERL` replay surface, now on a structurally correct
    carrier path

2026-03-30: bridge-resumed `IITERL` re-enters with normal-looking tail decode
state; the remaining seam is below bridge decode setup

- On `kdz`, with the stable bridge baseline plus
  `LUAJIT_S390X_STOP_RETARGET_LOOPDESC=1`, I stopped in `gdb` at
  `lj_BC_IITERL` on hit `1` and hit `50`.
- Both hits land with the same bridge-resumed interpreter state:
  - `r4/RA = 9`
  - `r6/RD = 0x7ffd`
  - `r8/KBASE = 0x...36e8`
  - `r9/PC = 0x...36b4`
  - `r13/BASE = 0x...2ce8`
  - `SAVE_PC = 0x...36ac`
  - `SAVE_L = 0x...1380`
- The live bytecode neighborhood at both hits is:
  - `0x...36a4 = 0x80010948`
  - `0x...36a8 = 0x010a0120` (`ADDVV`)
  - `0x...36ac = 0x00040b57` (`BC_JLOOP 4`)
  - `0x...36b0 = 0x7ffd0952` (`BC_ITERL A=9 D=32765`)
  - `0x...36b4 = 0x7ff8024f` (`BC_FORL`)
  - so `PC` is already advanced past the resumed `ITERL`, exactly like normal
    `ins_NEXT` entry into the static opcode handler
- The bridge-resumed operand bundle at `BASE+56` is:
  - hit `1`:
    - tagged GC table at slot `A-2`
    - odd control lane at slot `A-1`
      (`0xfffe7fff00000057`)
    - numeric key `2`
    - numeric value `3`
  - hit `50`:
    - same tagged GC table
    - control lane stabilized to numeric `2`
    - same key `2`
    - same value `3`
- Source-side comparison with
  [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc)
  now matters more than the old contract search:
  - the bridge path decodes `OP/RA/RD` from `resumeins`
  - advances `PC` by `4`
  - and then dispatches through the static table
  - `BC_IITERL` itself only consumes the already-produced bundle; it does not
    refresh iterator state
- Consequence:
  - on the corrected `JLOOP 4` carrier path, the bridge is no longer obviously
    mis-decoding `ITERL` or entering with a bad `PC/RA/RD/KBASE/BASE`
    contract
  - the remaining replay seam is now narrower:
    - the bridge resumes into a structurally normal consumer tail
    - but the producer-owned iterator lanes are still frozen
  - the next useful target is no longer “pick another saved trace contract”
    or “fix bridge decode”
  - it is the producer-side materialization step that should have refreshed the
    table/control/key/value bundle before this `JLOOP 4 ; ITERL ; FORL` tail

2026-03-30: first two bridge-resumed `IITERL` hits are back-to-back with the
same tail state; only the control lane normalizes once

- After cleaning `kdz`, I reran a short `gdb` cycle probe on the stable bridge
  baseline plus `LUAJIT_S390X_STOP_RETARGET_LOOPDESC=1`, stopping on:
  - `lj_BC_IITERL` hit `1`
  - `lj_BC_FORL` hit `1`
  - `lj_BC_IITERL` hit `2`
- The useful result is that the first two `IITERL` hits do arrive cleanly, but
  there is still no visible `FORL` stop between them in this short cycle.
- Both `IITERL` hits are effectively the same replay:
  - `RA = 9`
  - `RD = 0x7ffd`
  - `PC = 0x...36b4`
  - `BASE = 0x...2ce8`
  - `SAVE_PC = 0x...36ac`
  - bytecode neighborhood:
    - `ADDVV`
    - `BC_JLOOP 4`
    - `BC_ITERL`
    - `BC_FORL`
- The operand lanes at `BASE + 56` change only once:
  - hit `1`:
    - tagged table
    - odd control lane `0xfffe7fff00000057`
    - key `2`
    - value `3`
  - hit `2`:
    - same tagged table
    - control lane normalized to numeric `2`
    - same key `2`
    - same value `3`
- Consequence:
  - the replay loop is now even tighter than “tail region repeats”
  - at least across the first two bridge-resumed consumer hits:
    - key/value do not advance
    - `PC` does not move
    - only the control-var lane normalizes once
  - the missing next question is whether the replay path is actually:
    - `IITERL -> JLOOP 4 -> bridge dispatch -> IITERL`
    - rather than a normal `IITERL -> FORL -> ...` progression

2026-03-30: repeated bridge-resumed `IITERL` hits do not show an intervening
local `FORL` or post-retarget interpreter `JLOOP`

- I ran one more short `gdb` cycle probe on `kdz` with breakpoints on:
  - `lj_BC_JLOOP` (first three hits)
  - `lj_BC_IITERL` (first two hits)
- The three visible `JLOOP` hits are all still the old pre-bridge carrier:
  - `RA = 0xb`
  - `RD = 0x1`
  - live bytecode word `0x00010b57` (`BC_JLOOP 1`)
  - this happens before `trace 4` is formed and before the
    `S390X_STOP_RETARGET ... newins=0x00040b57` log
- After `trace 4` forms and the stop-time retarget fires, the probe then sees:
  - `IITERL_HIT 1 RA=0x9 RD=0x7ffd PC=0x...36b4 SAVE_PC=0x...36ac`
  - `IITERL_HIT 2 RA=0x9 RD=0x7ffd PC=0x...36b4 SAVE_PC=0x...36ac`
- There is still no visible post-retarget local `FORL` stop or post-retarget
  interpreter `JLOOP` stop between those first two bridge-resumed consumer
  hits.
- Consequence:
  - the repeated `IITERL` replay is not behaving like a simple in-interpreter
    `ITERL -> FORL -> JLOOP` tail cycle
  - at least on this seam, the repeated consumer hits look more like
    bridge/static re-entry into the same `IITERL` state than ordinary local
    bytecode fallthrough
  - the next useful target should therefore treat the replay as a
    bridge-dispatch re-entry loop, not as a plain local tail-progression bug

2026-03-30: the bridge-resumed `ITERL` contract can only loop through
`ADDVV ; JLOOP 4`; it cannot reach a producer refresh by itself

- The current
  [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc)
  `branchPC` macro is:
  - `PC = PC + 4*RD - 4*BCBIAS_J`
  - and `BCBIAS_J = 0x8000`
- On the live bridge-resumed consumer hit:
  - `PC = 0x...36b4`
  - `RD = 0x7ffd`
  - therefore `branchPC RD` rewrites `PC` to:
    - `0x...36b4 + 4*0x7ffd - 4*0x8000 = 0x...36a8`
- `0x...36a8` is exactly the local `ADDVV` word in the live neighborhood:
  - `0x...36a8 = ADDVV`
  - `0x...36ac = BC_JLOOP 4`
  - `0x...36b0 = BC_ITERL`
  - `0x...36b4 = BC_FORL`
- That explains the replay loop mechanically:
  - bridge dispatch resumes at `BC_ITERL`
  - `BC_IITERL` consumes the already-produced bundle
  - `branchPC RD` jumps back to local `ADDVV`
  - `ADDVV` runs
  - local `BC_JLOOP 4` re-enters the bridge path
  - and the same bridge dispatch resumes `BC_ITERL` again
- Consequence:
  - the current bridge resume site is now proven to be intrinsically
    consumer-only
  - even on the corrected `JLOOP 4` carrier path, `trace4.resumepc =
    BC_ITERL` can never reach a producer refresh by itself
  - this is stronger than the earlier “frozen bundle” observation:
    - the bridge is not merely missing progress accidentally
    - its saved consumer contract structurally loops on
      `IITERL -> ADDVV -> JLOOP 4 -> bridge dispatch -> IITERL`
  - the next fix surface is therefore not bridge decode invariants anymore
  - it is a producer-side refresh contract or explicit producer-state
    rematerialization before the bridge is allowed to resume at this local tail

2026-03-30: the only intact local producer anchor left in the bytecode is
`BC_ISNEXT`

- I decoded the local opcode numbers directly from
  [src/lj_bc.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_bc.h):
  - `BC_ADDVV = 32`
  - `BC_ITERN = 70`
  - `BC_ISNEXT = 72`
  - `BC_ITERL = 82`
  - `BC_JLOOP = 87`
  - `BC_JMP = 88`
- That identifies the live neighborhood around the bridge seam precisely:
  - `0x...36a4 = 0x80010948 = BC_ISNEXT`
  - `0x...36a8 = 0x010a0120 = BC_ADDVV`
  - `0x...36ac = 0x00010b57 / 0x00040b57 = patched BC_JLOOP`
  - `0x...36b0 = 0x7ffd0952 = BC_ITERL`
  - `0x...36b4 = 0x7ff8024f = BC_FORL`
- Parser-side generic `for` layout in
  [src/lj_parse.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_parse.c)
  also matches this family:
  - carrier at loop head: `BC_ISNEXT` or `BC_JMP`
  - body
  - producer call: `BC_ITERN` or `BC_ITERC`
  - consumer tail: `BC_ITERL`
- VM-side `BC_ISNEXT` in
  [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc)
  is the only intact local opcode here that still performs producer-side setup:
  - checks `next`
  - initializes the hidden control var to `0xfffe7fff00000057`
  - branches to the iterator producer path
- Consequence:
  - the bridge replay currently resumes too late at `BC_ITERL`
  - the patched `JLOOP` site at `0x...36ac` is a trap, not a surviving
    producer contract
  - but the local `BC_ISNEXT` at `0x...36a4` is still a real producer-side
    semantic anchor in the bytecode stream
 - the next coherent experiment is therefore bridge-local and exact-family:
    - use the local `BC_ISNEXT` anchor or its producer-side effect
    - not another saved `trace1/2/3/4` resume contract

2026-03-30: bridge-local entry below full `vm_IITERN` is the first cut that
restores real producer motion

- I changed the exact bridge `BC_ITERL` path in
  [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc)
  again, but this time below full `vm_IITERN` entry:
  - add a local `->vm_IITERN_bridge` label immediately after `vm_IITERN`'s
    `ins_A` decode
  - on the exact bridge-resumed `BC_ITERL` path:
    - keep the already-decoded bridge `A` field
    - back `PC` up by 4 to the patched predecessor slot
    - jump directly into `->vm_IITERN_bridge`
- This matters because the earlier direct `vm_IITERN` experiment was still
  decoding the patched `BC_JLOOP` word at the producer slot and therefore
  entered with the wrong base operand.
- On `kdz`, with the corrected carrier path still enabled via
  `LUAJIT_S390X_STOP_RETARGET_LOOPDESC=1`, this is the first bridge cut that
  no longer freezes the bridge payload.
- The bridge-dispatch payload sequence now moves as follows:
  - `n=0`: `raw_ctl = 0xfffe7fff00000057`, `raw_key = 2`, `raw_val = 3`
  - `n=1`: `raw_ctl = 0`, `raw_key = 2`, `raw_val = 3`
  - `n=2`: `raw_ctl = 2`, `raw_key = 1`, `raw_val = 1`
  - `n=3`: `raw_ctl = 3`, `raw_key = 2`, `raw_val = 3`
  - `n=4`: `raw_ctl = 4`, `raw_key = 3`, `raw_val = 5`
  - `n=5`: `raw_ctl = 5`, `raw_key = 4`, `raw_val = 7`
  - `n=6`: `raw_ctl = 6`, `raw_key = 5`, `raw_val = 9`
- After that, the bridge payload restarts a new cycle with the same table and
  a reset control lane, then the same `1,1 -> 2,3 -> 3,5 -> 4,7 -> 5,9`
  progression repeats again.
- Structural result:
  - the missing step really was below full saved-contract selection and below
    full `vm_IITERN` entry
  - the bridge-local producer body is now alive
  - the replay is no longer a frozen consumer-only `IITERL` loop

2026-03-30: the bridge-local producer-body cut crosses the old replay wall, but
the recurrence is still wrong

- A clean `kdz` control run on the same branch, with the bridge logs disabled
  but `LUAJIT_S390X_STOP_RETARGET_LOOPDESC=1` still enabled, no longer times
  out at the old replay seam.
- It now prints a real final line:
  - `RESULT -1679162313`
- That is still wrong for this harness. The expected total for:
  - `for i = 1, 200000 do`
  - `for _, v in pairs({1,3,5,7,9}) do`
  - `total = total + v`
  is:
  - `5000000`
- So this is not the landing fix yet.
- But the seam has moved in a useful way:
  - before: no producer progress, only repeated bridge replay
  - now: real producer-side key/value motion exists, and the branch reaches a
    final `RESULT`
  - the remaining bug is therefore a wrong recurrence / restart / termination
    contract, not the earlier “missing producer refresh” wall
- The bridge payload also suggests where that next bug lives:
  - after the correct `1..5 / 1,3,5,7,9` array progression, the cycle restarts
  - so the next exact target is the bridge-local cycle boundary
  - specifically the `BC_ISNEXT`-style restart/setup semantics for the next
    outer trip, not another saved trace resume contract and not another frozen
    consumer-tail diagnosis

2026-03-30: the new bridge cut probably revives the producer body without the
normal `IITERL` consumer writeback

- The moving bridge payload now has a sharper shape than the old frozen replay:
  - control lane: `2, 3, 4, 5, 6`
  - key/value lanes: `1,1 -> 2,3 -> 3,5 -> 4,7 -> 5,9`
- That is strong evidence the exact bridge-local producer body is running.
- It also suggests the current bridge-local cut is now *too* low-level:
  - the producer side is alive
  - but the normal local `BC_IITERL` consumer-side writeback is probably not
    happening on this path anymore
  - otherwise the observed control lane would be expected to track the returned
    key more closely instead of staying on the producer-side `index+1`
    progression
- So the next fix should not go back to saved trace contracts.
- It should fuse two exact local pieces on the bridge path:
  - just enough producer work to refresh key/value
  - then just enough local `ITERL/IITERL` consumer semantics to rejoin the tail
    correctly
- That would preserve the real producer motion now visible on `kdz` while
  avoiding the wrong recurrence now showing up as the bad final `RESULT`

2026-03-30: naïvely fusing local `IITERL` writeback back into the bridge body is
too strong; it re-freezes the seam

- I tested one narrower follow-up in
  [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc):
  - keep the bridge-local `vm_IITERN_bridge` producer body
  - but tag bridge entry and force the success path to perform the local
    `IITERL`-style control-var writeback before `ins_next`
- That is a reject.
- On `kdz`, the first bridge payloads collapse immediately to:
  - `raw_ctl = 1`
  - `raw_key = 1`
  - `raw_val = 1`
  - repeating
- So that fused cut is too strong or ordered incorrectly:
  - it destroys the newly restored producer progression
  - and re-freezes the seam almost immediately
- I backed that experiment out locally and rebuilt `kdz` back to the previous
  producer-body-only bridge baseline.
- Current best branch is therefore still:
  - exact `trace 4` stop-time carrier retarget
  - exact bridge-local jump below full `vm_IITERN` entry
  - real producer-side key/value motion
  - wrong final recurrence / result
- The next exact target is no longer “just add `IITERL` writeback back in”.
- It has to be a narrower producer/consumer handoff than that.

2026-03-30: backing `PC` up to the patched predecessor slot is also too
strong as a direct bridge change

- I tested the minimal `PC`-context correction on the producer-body branch in
  [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc):
  - change the exact bridge path from `aghi PC, -4` to `aghi PC, -8`
  - keep the rest of the bridge-local `vm_IITERN_bridge` producer-body cut the
    same
- This was meant to anchor the bridge micro-step at the patched predecessor
  slot so the producer body's built-in `PC+4` would land on the local
  `BC_ITERL` slot instead of the following `BC_FORL`.
- That is also a reject.
- On `kdz`, the branch now segfaults before any `S390X_VM_BRIDGE_DISPATCH` log
  appears:
  - bridge dispatch count stays `0`
  - the run dies before the corrected bridge payload can even be observed
- I backed that change out immediately and rebuilt `kdz` back to the previous
  producer-body-only bridge baseline.
- So the next fix still has to stay narrower than:
  - full local `IITERN` + `IITERL` fusion
  - and narrower than simply moving the bridge `PC` anchor one slot earlier

2026-03-30: even the narrower slot-based key commit is too strong as a direct
bridge add-on

- I tested the exact cut suggested by the new payload shape:
  - keep the producer-body-only `vm_IITERN_bridge` path
  - on the array-success path only, copy the produced key TValue from
    `0(RA, BASE)` back to the hidden control-var slot `-8(RA, BASE)`
  - leave the existing control flow alone
- This was meant to preserve the revived producer motion while committing only
  the visible key for the next producer trip.
- That is also a reject.
- On `kdz`, the branch now segfaults before any `S390X_VM_BRIDGE_DISPATCH`
  entry appears:
  - bridge dispatch count stays `0`
  - so this cut destabilizes the seam before the bridge payload can even be
    classified
- I backed it out immediately and rebuilt `kdz` back to the previous
  producer-body-only bridge baseline.
- So the remaining bridge-local fix must be narrower still than:
  - full `IITERN + IITERL` fusion
  - simple `PC` rewind to the patched predecessor slot
  - direct slot-based key commit inserted into the producer array-success path

2026-03-30: duplicating the key-commit path as a bridge-only producer variant
still re-freezes the seam

- I tested a safer variant of the slot-based key commit in
  [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc):
  - keep the normal `vm_IITERN` and `vm_IITERN_bridge` body untouched
  - add a separate `vm_IITERN_bridge_commit` entry used only by the exact
    bridge-resumed `BC_ITERL` path
  - on that bridge-only variant, perform the key-slot commit after
    `branchPC RD` and before `ins_next`
- That is also a reject.
- It does not segfault early, but it immediately collapses the bridge payload
  back to the same frozen shape as the earlier over-strong fusion:
  - `raw_ctl = 1`
  - `raw_key = 1`
  - `raw_val = 1`
  - repeating
- I backed that out immediately and rebuilt `kdz` back to the previous
  producer-body-only bridge baseline.
- So the remaining good state is still:
  - exact carrier retarget at `trace_stop()`
  - exact bridge-local jump below full `vm_IITERN` entry
  - real producer-side key/value progression
  - wrong final recurrence / result
- And the remaining bridge-local fix is now proven narrower than:
  - direct writeback fusion in the producer body
  - direct slot-based key commit in the producer body
  - and a bridge-only duplicate that performs that commit after `branchPC`

2026-03-30: the revived bridge producer now clearly feeds the local `ADDVV`
tail, but the bug only starts once the bridge family is active

- I extended the exact bridge log in
  [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c)
  again so `S390X_VM_BRIDGE_DISPATCH` also dumps the local `prev2=BC_ADDVV`
  operands and slots:
  - `prev2a`, `prev2b`, `prev2c`
  - `raw_add_a`, `raw_add_b`, `raw_add_c`
- On `kdz`, the first bridge-resumed payloads on the stable
  producer-body-only branch now show:
  - `prev2 = 0x030c0320`
  - `prev2op = BC_ADDVV`
  - `prev2a = 3`, `prev2b = 3`, `prev2c = 12`
  - `raw_add_c` matches the produced value lane exactly:
    - `1, 3, 5, 7, 9`
  - `raw_add_a` and `raw_add_b` are the same slot and their low-word delta
    walks by those same odd values across the bridge cycle
- That proves the revived bridge producer is no longer isolated from the local
  arithmetic tail:
  - the bridge now feeds the local `ADDVV`
  - the local `ADDVV` is consuming the produced value lane
  - so the remaining bug is not “producer never reaches the sum tail” anymore
- But the one-shot classifier matters just as much:
  - with the same stable producer-body bridge branch plus
    `LUAJIT_S390X_STOP_RETARGET_LOOPDESC=1`
  - `/tmp/oneshot_iter.lua` now returns correct totals for small counts:
    - `n=1 -> RESULT 25`
    - `n=5 -> RESULT 125`
  - the branch only fails once the bridge family becomes active:
    - `n=20` reaches `trace=4` retarget and then segfaults
    - `n=500 -> RESULT -1439826190`
    - `n=2000 -> RESULT -1252093586`
- So the new `ADDVV` evidence does **not** support a generic pre-JIT
  accumulator-initialization bug.
- The current failure is still bridge-era and post-`trace4`:
  - before the bridge family forms, results are correct
  - after `trace4` becomes active, the revived producer feeds the local sum
    tail, but the recurrence is still wrong and eventually corrupts the result
    or crashes
- That narrows the next target again:
  - keep the current producer-body bridge baseline
  - use the smallest `n` that first reaches `trace4` as the classifier
  - debug the first post-`trace4` bridge-fed `ADDVV`/cycle boundary, not the
    pre-JIT accumulator setup

2026-03-30: the smallest failing classifier confirms the live bug is in the
bridge-era `ADDVV` recurrence, not before JIT takeover

- I reran the current stable producer-body bridge branch on `kdz` with a tiny
  one-shot harness:
  - `for _ = 1, n do`
  - `for _, v in pairs({1,3,5,7,9}) do`
  - `total = total + v`
- Current results on that exact branch are:
  - `n=1 -> RESULT 25`
  - `n=5 -> RESULT 125`
  - `n=20 -> RESULT -1496461294` when the bridge logs are enabled
  - `n=500 -> RESULT -1439826190`
  - `n=2000 -> RESULT -1252093586`
- So this branch is still correct before the bridge family is active, and the
  wrong total only appears once the `trace4` bridge path is live.
- The `n=20` bridge dump is the cleanest classifier so far:
  - `prev2 = BC_ADDVV`
  - `prev2a = 1`, `prev2b = 1`, `prev2c = 11`
  - `next1 = BC_FORL`, with `next1a = 3`
  - `raw_for_idx/raw_for_ext` advance cleanly:
    - first bridge cycle starts with outer index `10`
    - later cycles show `11`, `12`, `13`, `14`
  - but the `ADDVV` left-hand slot is already wrong on the very first bridge
    dispatch:
    - `raw_add_a = raw_add_b = 0xfff90000a6cdcf18`
    - then it walks by the produced odd values:
      - `...cf19`, `...cf1c`, `...cf21`, `...cf28`, `...cf31`, ...
    - while `raw_add_c` matches the produced value lane exactly:
      - `1, 3, 5, 7, 9`
- That means the revived bridge producer is feeding the local `ADDVV` tail
  correctly, and the outer `FORL` state is advancing correctly too.
- The remaining corruption is now narrower:
  - the bridge-local accumulator/LHS slot consumed by `ADDVV` is already bad
    by the first bridge dispatch
  - the local tail then adds the correct odd values on top of that bad seed
  - so the wrong total is no longer explained by outer restart or by a missing
    producer refresh
- I also tried one more exact probe at this seam:
  - extend `loopdesc-bridge-child-reenter` in
    [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c)
    to read and print the live `ADDVV` slots from `J->L->base` before
    returning `-17`
- That probe is a reject:
  - on `kdz`, the smallest `n=20` classifier regresses to an early segfault
    before any bridge payload log appears
  - so this seam is still too probe-sensitive for extra C-side base-slot reads
- The next exact target is therefore:
  - keep the stable producer-body bridge baseline
  - treat the first bridge-dispatch `ADDVV` LHS seed as the live bug
  - inspect where that accumulator slot should be materialized or restored
    before the bridge-fed local tail runs

2026-03-30: the first post-bridge `ADDVV` write proves the accumulator slot is
already stale before the local arithmetic tail runs

- I took the narrower debugger route on `kdz`:
  - keep the stable producer-body bridge baseline
  - use the smallest failing classifier: `/tmp/oneshot_iter.lua 20`
  - break on the first
    `lj_trace_s390x_vm_bridge_dispatch_log()` call
  - set a hardware watchpoint on the bridge-fed `ADDVV` LHS slot:
    - `slot1 = &base[1].u64`
- That produced the first decisive provenance result for the stale total seed:
  - first bridge entry:
    - `base = 0x3fff7fd2cc0`
    - `pc = 0x3fff7fd3730`
    - `ins = 0x7ffd0a52`
    - `slot1 = 0x3fff7fd2cc8`
    - `val = 0xfff90000f7fdcf48`
  - first watchpoint hit:
    - old value: `0xfff90000f7fdcf48`
    - new value: `0xfff90000f7fdcf49`
    - current PC: `lj_BC_ADDVV`
- That is the key narrowing:
  - the bridge is **not** writing the first bad accumulator seed immediately
    before the local tail
  - the first observed write is simply `lj_BC_ADDVV` adding the produced
    `1` onto an already-stale carried total
  - so the live bug is now strictly before the local `ADDVV` execution
- Combined with the earlier `n=20` bridge dump, the picture is now coherent:
  - the bridge producer is alive
  - the produced value lane is correct
  - outer `FORL` state is correct
  - the first bridge-fed `ADDVV` just consumes a stale boxed total
- So the next exact target is no longer “find the writer of the bad value.”
- It is:
  - determine why the carried total slot is not restored or materialized before
    the first bridge-fed local arithmetic tail runs
  - most likely in bridge-era carried-state selection / restore-source
    canonicalization, not in the local `ADDVV` tail itself

2026-03-30: the exact `trace 4` bridge stop is built with no normal stack-slot
snapshot entries

- I switched from runtime-side dump attempts to a non-invasive `gdb` stop on
  `trace_stop()` for the exact bridge-stop shape:
  - `J->parent == 3`
  - `J->exitno == 0`
  - `J->cur.root == 1`
  - `bc_op(J->cur.startins) == BC_JMP`
  - `J->cur.link != 0`
- Dumping the live temp trace buffers (`J->cur.snap` / `J->cur.snapmap`) at
  that stop finally answered the accumulator-liveness question without
  perturbing the runtime seam:
  - `traceno = 4`
  - `link = 3`
  - `linktype = 1`
  - `nsnap = 2`
  - `nsnapmap = 4`
  - `nins = 32770`
  - `startins = 0x58`
- Raw snapshot header words for the two live snapshots are:
  - snap0:
    - `mapofs = 0`
    - `ref = 0x8001`
    - `mcofs = 0`
    - header word `0x0c0e0000`
  - snap1:
    - `mapofs = 2`
    - `ref = 0x8002`
    - `mcofs = 0x0016`
    - header word `0x0c0e00ff`
- Interpreted with the `SnapShot` layout on s390x big-endian, both snapshots
  have:
  - `nslots = 12`
  - `topslot = 14`
  - `nent = 0`
- That is the decisive new finding:
  - the exact `trace 4` bridge stop is being built with **no normal stack-slot
    snapshot entries at all**
  - so the carried total consumed by the bridge-fed local `BC_ADDVV` is not in
    `trace 4`’s saved/live snapshot map in the first place
- This matches the previous watchpoint result perfectly:
  - first bridge-fed `ADDVV` writes `old + 1`
  - because the accumulator slot was already stale before the local arithmetic
    tail ran
  - and now we know why: the bridge trace did not capture that slot
- This moves the target again, in a useful way:
  - not outer restart
  - not producer refresh
  - not local `ADDVV`
  - specifically snapshot completeness / carried-state liveness for the exact
    `trace 4` bridge stop
- The next exact move should be to inspect why this bridge-stop family reaches
  `nent=0` and whether the live accumulator slot can be made part of the bridge
  trace’s carried state without reopening the earlier saved-contract failures

2026-03-30: the carried total is not merely omitted from the bridge snapshot;
it has no TRef in the bridge trace at all

- I followed the empty-snapshot result by stopping again at the exact
  `trace_stop()` entry for the live `trace 4` bridge shape and inspecting the
  recorder-side slot map directly.
- For the local arithmetic tail:
  - `prev2 = BC_ADDVV`
  - `prev2a = 1`
  - current `baseslot = 2`
  - so the carried total consumed by the local `ADDVV` corresponds to
    `J->slot[baseslot + 1] = J->slot[3]`
- At that exact bridge stop, `gdb` shows:
  - `TRACE4_SLOT3 s=3 tr=0x0 ref=0x0 baseslot=2 maxslot=10`
- That is stronger than the earlier empty-snapshot result:
  - the carried total is not being dropped only by snapshot compression
  - it is not an inherited `SLOAD` that later gets elided as “unchanged”
  - the bridge trace has **no TRef at all** for the accumulator slot by the
    time `trace_stop()` runs
- Combined with the first bridge-dispatch watchpoint:
  - runtime `ADDVV` later reads a stale boxed total from the Lua stack slot
  - because the bridge trace never materialized or carried that total in its
    recorder-side slot map
- So the live target tightens again:
  - not restore-source selection
  - not snapshot-entry compression alone
  - specifically why the exact `trace 4` bridge recorder state leaves the
    carried total slot dead (`J->slot[3] == 0`) even though the local tail
    immediately uses it after bridge activation

2026-03-30: `lj_snap_replay()` leaves the exact `trace 4` recorder slot window
completely dead

- I moved one phase earlier than `trace_stop()` and stopped immediately after
  `lj_snap_replay(J, T)` in side-trace setup (`lj_record.c:3962`) for the exact
  `parent=3 exit=0 root=1` bridge family.
- Result on `kdz`:
  - `AFTER_REPLAY traceno=4 parent=3 exit=0 root=1 baseslot=2 maxslot=10`
  - `pc=0x...372c op=BC_JLOOP startop=BC_JMP`
  - `slot3=0x0 ref3=0x0 tr3=0x0`
  - parent trace `T` still has `snap0.nent=0`, `snap1.nent=0`
  - dumped `J->slot[0..10]` is all zero
- So the carried total is not being lost late during `trace_stop()`
  compaction. On the exact bridge family, side-trace setup zeros the slot
  window and `lj_snap_replay()` repopulates none of it because the parent
  snapshots are empty.

2026-03-30: first bridge-frame slot dump shows no nearby correct carried total

- I extended `S390X_VM_BRIDGE_DISPATCH` to dump the nearby live frame slots
  `raw_base0..raw_base5` on the stable producer-body bridge baseline.
- `kdz`, `/tmp/oneshot_iter.lua 20`, first bridge dispatch:
  - `raw_base0=20`
  - `raw_base1=0xfff90000b33dcf18`
  - `raw_base2=<tab ptr>`
  - `raw_base3=10`
  - `raw_base4=20`
  - `raw_base5=1`
  - while the local tail still sees:
    - `raw_add_a/raw_add_b = raw_base1`
    - `raw_add_c = 1,3,5,7,9`
    - `raw_for_idx/raw_for_ext = 10,11,...`
- So there is no obvious correct carried total hiding in nearby bridge frame
  slots. The live arithmetic tail is reading slot `1`, and slot `1` is already
  a stale boxed integer-like word before the first bridge-fed `ADDVV`.

2026-03-30: no write repairs the stale total between exact `trace 4` stop and
the first bridge-fed `ADDVV`

- `kdz`, `/tmp/oneshot_iter.lua 20`
- I broke at exact `trace4` `trace_stop()` and set a hardware watchpoint on
  `&J->L->base[1].u64`.
- Stop-time value:
  - `TRACE4_STOP_WATCH addr=0x...2cc8 val=0xfff90000f7fdcf48`
- First write after continuing:
  - watchpoint hits in `lj_BC_ADDVV`
  - old `0xfff90000f7fdcf48`
  - new `0xfff90000f7fdcf49`
- So nothing between exact `trace4` stop and the first bridge-fed local tail
  materializes or repairs the carried total slot. The first observed write is
  just `ADDVV` consuming the stale seed.

2026-03-30: the stale carried-total seed already exists at exact `trace 3` stop

- I moved the same slot watchpoint earlier, to `trace_stop()` for
  `trace=3 parent=1 exit=1`.
- On `kdz`, the same live interpreter slot already contains the stale value:
  - `TRACE3_STOP_WATCH addr=0x...2cc8 val=0xfff90000f7fdcf48`
- I also dumped the recorder-side slot map at that same exact `trace3` stop:
  - `baseslot=2 maxslot=10 nsnap=2 nins=32772`
  - `slot3=0x0 ref3=0x0 tr3=0x0`
  - `J->slot[0..10]` is all zero
- That is the same boxed integer later seen:
  - at exact `trace4` stop
  - and on the first bridge-fed `ADDVV`
- This moves the seam earlier again:
  - the bridge does not create the bad accumulator seed
  - `trace4` does not create it either
  - the carried total is already absent from the recorder-side slot map by
    exact `trace3` stop
  - the bridge is only the first place that consumes a stack slot which was
    never materialized with the carried total
- Next exact target:
  - inspect where the running total lives on the `trace3` path if it is not in
    `J->slot[3]`
  - determine why the `trace3 -> trace4` path never materializes that carried
    total back to a live interpreter stack TValue in slot `1`

2026-03-30: the `trace3` accumulator live-in replay idea is a reject, and the
long-run recovery from the probe build was non-causal

- I implemented the exact recorder-side experiment in
  [src/lj_snap.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_snap.c):
  - env gate: `LUAJIT_S390X_TRACE3_ACC_LIVEIN=1`
  - replay-time injection:
    `J->slot[3] = emitir_raw(IRT(IR_SLOAD, IRT_INT), 3, IRSLOAD_INHERIT|IRSLOAD_PARENT)`
  - intended scope: the `parent=1 exit=1 root=1 startop=BC_JMP` continuation
    family that later feeds the `trace3 -> trace4` bridge
- Initial structural checks looked promising:
  - exact `trace3`/`trace4` replay and stop probes showed `slot3` nonzero with
    the gate enabled
  - short one-shot cases such as `n=20 -> RESULT 500` also passed
- But the next runtime classifier disproved the causal story:
  - with the gate enabled, `n=50` regressed from the correct `1250` to
    `RESULT 0`
  - a focused replay log showed why:
    - the injection fired on an earlier continuation stub, not the intended
      bridge family:
      - `curlink=0`
      - `curlinktype=0`
      - `maxslot=0`
      - `pc=...372c`
      - `ins=BC_JLOOP 1`
- I tightened the gate with `J->maxslot >= 10`, which removed that bad early
  hit and restored the small one-shot results:
  - `n=20 -> 500`
  - `n=50 -> 1250`
  - `n=500 -> 12500`
  - `n=2000 -> 50000`
- But the crucial control result rejected the whole live-in theory:
  - on the probe build, `n=2000` and even `n=200000 -> 5000000` recovered with
    `LUAJIT_S390X_TRACE3_ACC_LIVEIN` **off**
  - a remote-only control replacing `src/lj_snap.c` with `HEAD` still produced
    `n=2000 -> 50000`
  - after syncing the cleaned local `src/lj_snap.c` back to `kdz` and
    rebuilding, the long run regressed again:
    - `n=2000 -> 50000`
    - `n=200000` segfaulted
- So the recorder-side live-in injection is not the landing fix:
  - the broad form is wrong and corrupts an earlier continuation stub
  - the narrowed form is effectively dormant
  - the apparent long-run recovery on the probe build was a layout/timing
    effect in `lj_snap.c`, not a semantic proof that `slot3` rebinding solved
    the bridge-era accumulator problem
- Current conclusion:
  - keep the accumulator-live-in hypothesis rejected for now
  - do not land `LUAJIT_S390X_TRACE3_ACC_LIVEIN`
  - treat the `lj_snap.c` probe build as a Heisenbug classifier, not a fix

2026-03-30: the long-run flip is reproducibly `lj_snap_replay()` code-shape
sensitive

- I backed the accumulator-live-in path out of the active source again and
  reran the clean branch on `kdz`.
- Clean `src/lj_snap.c` result:
  - `n=2000 -> RESULT 50000`
  - `n=200000` segfaults again
- Then I added one deliberately non-semantic probe in
  [src/lj_snap.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_snap.c):
  - new env gate: `LUAJIT_S390X_SNAP_LAYOUT_PROBE`
  - new helper `snap_s390x_layout_probe()`
  - a single disabled branch at the top of `lj_snap_replay()`
  - with the env **unset**, so the probe never actually runs
- That alone flips the long run back:
  - `n=200000 -> RESULT 5000000`
- So the active seam is now much clearer:
  - not the rejected accumulator-live-in semantics
  - not a bridge-side writeback
  - specifically the generated code shape of `lj_snap_replay()` on this s390x
    build
- I then compared `lj_snap_replay` object code for the clean and probe builds
  on `kdz`:
  - clean object:
    - `lj_snap_replay` size `0x0c50`
    - frame allocation `lay %r15,-288(%r15)`
  - probe object:
    - `lj_snap_replay` size `0x0cba`
    - frame allocation `lay %r15,-296(%r15)`
  - the disassembly also diverges throughout the function after the new helper
    is introduced
- This is the first reproducible, minimal Heisenbug classifier on the current
  branch:
  - an inert `lj_snap_replay()` layout perturbation is enough to move the
    long-run result from `SIGSEGV` to the correct `5000000`
- Current conclusion:
  - the branch is now sensitive to `lj_snap_replay()` code generation / stack
    layout on s390x
  - treat the no-op probe as a diagnostic reproducer, not a promotable fix
  - the next target is compiler/code-shape isolation inside
    `lj_snap_replay()`, not more accumulator slot rebinding

2026-03-30: the late `200000` crash is an `ERRNO_RESTORE` register-clobber bug
in `lj_dispatch_ins`, and forcing stack-backed errno saves fixes it

- After the `lj_snap_replay()` layout digression, I checked repeatability on the
  current `kdz` binary instead of source edits:
  - same binary, same env, same input:
    - run 1: `SIGSEGV`
    - run 2: `RESULT 5000000`
    - run 3: `RESULT 5000000`
- I then looped the same binary until failure and captured the persisted core
  from `systemd-coredump`.
- The stable failing crash site is:
  - `lj_dispatch_ins`
  - `pc = 0x100bd24`
  - instruction:
    - `ste %f10,0(%r10)`
- Core state from the failing run:
  - `L = 0x3ffad351380`
  - bytecode `pc = 0x...3730`
  - live local bytecode neighborhood:
    - `... BC_JLOOP 1 ; BC_ITERL ; BC_FORL ; BC_IFORL`
  - `%r10 = 0x8002ad3716b8`
- Disassembly of the old `lj_dispatch_ins` made the bug explicit:
  - after `__errno_location@plt`, the function cached the errno pointer in
    caller-saved `%r10`
  - it also cached the saved errno value in `%f10`
  - then it called helpers like:
    - `cur_topslot`
    - `lj_trace_ins`
    - `callhook`
    - `lj_debug_line`
  - and finally restored errno with:
    - `ste %f10,0(%r10)`
- That is not a bridge or snapshot bug. It is an ABI bug:
  - `%r10` is caller-saved on s390x
  - `lj_dispatch_ins` was assuming the saved errno pointer would survive those
    helper calls
  - when `%r10` was clobbered, `ERRNO_RESTORE` wrote through a bad pointer and
    crashed nondeterministically late in the run
- I fixed this at the macro layer in
  [src/lj_dispatch.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_dispatch.h):
  - non-Windows:
    - `ERRNO_SAVE` changed from `int olderr = errno;` to
      `volatile int olderr = errno;`
  - Windows:
    - both `olderr` and `oldwerr` are now `volatile`
- On `kdz`, the new generated `lj_dispatch_ins` no longer uses `%f10` to carry
  the saved errno value across helper calls:
  - old shape:
    - `lde %f10,0(%r10)` ... helper calls ... `ste %f10,0(%r10)`
  - new shape:
    - `l %r5,0(%r10)`
    - `st %r5,172(%r15)`
    - ... helper calls ...
    - `l %r1,172(%r15)`
    - `st %r1,0(%r10)`
- That is the right fix surface: force stack-backed errno state instead of a
  caller-saved register/TLS-pointer pair.
- Validation on the clean source (with the inert `lj_snap.c` layout probe
  removed again):
  - `oneshot_iter.lua 200000`:
    - run 1: `RESULT 5000000`
    - run 2: `RESULT 5000000`
    - run 3: `RESULT 5000000`
  - `./luajit -joff /tmp/oneshot_iter.lua 200000`:
    - `RESULT 5000000`
  - full [iter-chain-handoff.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/iter-chain-handoff.lua):
    - `RESULT 5000000`
- Current conclusion:
  - the late `200000` crash was not a lingering bridge replay bug
  - it was an s390x ABI bug in `ERRNO_RESTORE` codegen for dispatch/helper paths
  - forcing stack-backed errno saves appears to resolve the long-run crash on
    the clean branch

2026-03-30: clean post-fix validation confirms the errno fix, but also exposed
one missing clean-build dependency and a still-red iterator perf surface

- A clean source rooted at `0a76ac86` did **not** build by itself on either
  host until one additional committed dependency was restored:
  - `lj_record.c` and `lj_asm_s390x.h` already reference
    `IRSLOAD_KIDX_NUMKEY`
  - clean `lj_ir.h` did not define it
  - adding
    `#define IRSLOAD_KIDX_NUMKEY 0x80 /* KEYINDEX expected to produce numeric keys. */`
    was required to make the clean validation source self-consistent
- With that one-line dependency present and no `lj_snap.c`/bridge probe noise,
  the `ERRNO_SAVE` fix validates as a real ABI/codegen fix:
  - `kdz`, release `gcc`:
    - `oneshot_iter.lua 20 -> RESULT 500`
    - `oneshot_iter.lua 2000 -> RESULT 50000`
    - `oneshot_iter.lua 200000`:
      - run 1: `RESULT 5000000`
      - run 2: `RESULT 5000000`
      - run 3: `RESULT 5000000`
    - `./luajit -joff /tmp/oneshot_iter.lua 200000 -> RESULT 5000000`
    - `iter-chain-handoff.lua -> RESULT 5000000`
    - `lj_dispatch_ins` shows stack-backed errno state again:
      - `st %r5,172(%r15)`
      - later `l %r1,172(%r15)` / `st %r1,0(%r10)`
      - no old `%f10` restore path
  - `kdz`, debug `gcc`:
    - `oneshot_iter.lua 200000 -> RESULT 5000000`
    - `lj_dispatch_ins` still spills saved errno to the stack
      (`st %r1,180(%r15)` in this build)
  - `kdz`, release `clang`:
    - `oneshot_iter.lua 200000 -> RESULT 5000000`
    - `lj_dispatch_ins` still spills saved errno to the stack
      (`st %r0,284(%r15)` in this build)
  - `kdz`, debug `clang`:
    - reject as a matrix blocker, but not because of errno restore
    - build fails later in `lj_opt_fold_dyn.o` with undeclared
      `fold_hashkey` / `fold_hash` / `fold_func`
    - so this quadrant currently points at an unrelated debug+clang build
      surface, not a regression of the errno fix
  - `zkd0`, release `gcc`:
    - `oneshot_iter.lua 200000 -> RESULT 5000000`
    - `./luajit -joff /tmp/oneshot_iter.lua 200000 -> RESULT 5000000`
    - `iter-chain-handoff.lua -> RESULT 5000000`
    - `lj_dispatch_ins` matches the stack-backed release `gcc` shape from `kdz`
  - `zkd0`, release `clang`:
    - `oneshot_iter.lua 200000 -> RESULT 5000000`
    - `lj_dispatch_ins` again uses a stack spill instead of the old floating
      restore shape
- So the late long-run blocker is now cleanly split from the remaining iterator
  work:
  - the `ERRNO_RESTORE` crash is fixed at the right layer
  - the clean successful quadrants agree on the result
  - inert `lj_snap.c` probe layout changes are no longer needed to keep the run
    green
- The clean perf gate on `kdz` remains decisively red even after the crash fix:
  - JIT on:
    - `pairs_sum/small median=0.003633`
    - `pairs_array_sum/small median=0.003640`
    - `pairs_sum/medium median=0.018218`
    - `pairs_array_sum/medium median=0.018210`
    - `pairs_sum/hot median=0.072996`
    - `pairs_array_sum/hot median=0.083461`
  - `-joff`:
    - `pairs_sum/small median=0.000218`
    - `pairs_array_sum/small median=0.000184`
    - `pairs_sum/medium median=0.001089`
    - `pairs_array_sum/medium median=0.000919`
    - `pairs_sum/hot median=0.004353`
    - `pairs_array_sum/hot median=0.003692`
- That means the post-fix go-forward target is no longer bridge crash
  stability. It is back to the steady-state hot owners:
  - array post-call key-lane owner `0x427`
  - hash pre-call KEYINDEX owner `0x509`

2026-03-30: low-noise post-validation owner checks still point at the same
array/hash payer split

- The first attempt to reuse the generic
  [tools/s390x/iterator_probe.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/iterator_probe.py)
  wrapper on the current dirty scratch tree was too invasive for this seam:
  - even a single-host `pairs_array_sum:20` run with only the backend-side
    key-lane guard requested still inherited the wrapper's broader
    `CALL`/`ADD`/`EXIT`/`RECSTOP` logging bundle
  - that build completed, but the probe run segfaulted after trace-snap dumps
  - so the wrapper result is not trustworthy as an owner classifier on the
    current scratch tree
- A manual low-noise rerun against the already-built `kdz` scratch repo did
  give a clean semantic split again:
  - `pairs_array_sum:20` with
    `LUAJIT_S390X_VLOAD_NEXT_KEY_NIL_GUARD=1`,
    `LUAJIT_S390X_GUARD_LOG=1`,
    `LUAJIT_S390X_GUARD_MARK_LOG=1`,
    `LUAJIT_S390X_EMPTY_LOOP_FALLTHROUGH=1`
    returned the correct `RESULT actual=500 expected=500`
  - the repeated live guard cluster is still the post-call array key-lane
    family:
    - `vload_next_key_int`
    - `vload_next_key_nil`
    - `gencall_sload_type` on the table slot
    - then the carried-total `addov_rr_int_eq`
  - `pairs_sum:20` with the same low-noise setup minus the array-only nil guard
    returned the correct `RESULT actual=300 expected=300`
  - the repeated live hash guard cluster is still the pre-call/restart family:
    - `vload_addr`
    - duplicate `gencall_sload_type`
    - `sload_keyindex`
    - then the carried-total `addov_rr_int_eq`
- So the post-errno-fix pivot is now restamped locally:
  - array side is still paying in the post-call numeric key-lane cluster
  - hash side is still paying in the pre-call KEYINDEX/address-lane cluster
  - the bridge/continuation crash work is no longer the primary iterator
    target

2026-03-30: clean array-only result-path derivation removes the old post-call
array key-lane payer

- I moved the next perf pass onto a clean detached worktree at `81fd03d1` and
  left the dirty bridge/snapshot scratch out of the experiment.
- The array-only change is in
  [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c):
  - extend `rec_next_types()` to report whether the next visible item comes
    from the array part
  - in `lj_record_next()`, when the path is array-only and the visible key is
    numeric, derive that visible key from the returned successor index
    (`HIOP(trvk) - 1`) instead of reloading `VLOAD #1` from the `lj_vm_next`
    tuple
- This stays exact-path-only:
  - numeric-key array traversal only
  - no hash behavior changes
  - no new descendant/bridge policy changes
- Clean `kdz` correctness stayed green on that build with the same safe env
  bundle:
  - `/tmp/oneshot_iter.lua 20 -> RESULT 500`
  - `/tmp/oneshot_iter.lua 2000 -> RESULT 50000`
  - `/tmp/oneshot_iter.lua 200000 -> RESULT 5000000`
  - `./luajit -joff /tmp/oneshot_iter.lua 200000 -> RESULT 5000000`
- The low-noise manual `pairs_array_sum:20` classifier changed exactly where
  expected:
  - `RESULT actual=500 expected=500`
  - the old repeated post-call array key-lane cluster is gone:
    - `vload_next_key_int`
    - `vload_next_key_nil`
  - the remaining repeated guards are now the carried-total/slot-load side of
    the loop, not the helper-tuple key-lane reload
- The clean perf gate on `kdz` improved on the array hot case while leaving the
  hash case essentially where it was:
  - JIT on:
    - `pairs_sum/hot median=0.072109`
    - `pairs_array_sum/hot median=0.071600`
  - `-joff`:
    - `pairs_sum/hot median=0.004267`
    - `pairs_array_sum/hot median=0.004011`
  - compared to the previous clean baseline:
    - `pairs_sum/hot` is effectively unchanged (`0.072996 -> 0.072109`)
    - `pairs_array_sum/hot` improves materially (`0.083461 -> 0.071600`)
- The low-noise manual `pairs_sum:20` hash classifier remained on the same
  pre-call/restart owner family:
  - `vload_addr`
  - duplicate guard site at the table/key restart boundary
  - `sload_keyindex`
  - `sload_type`
  - then the carried-total `addov_rr_int_eq`
- So the next split is now cleaner than before:
  - array has a concrete result-path win that removes the old post-call
    numeric key-lane payer
  - hash is still the remaining iterator perf target and stays on the
    pre-call KEYINDEX/address-lane side

2026-03-30: clean hash-side lazy visible-key materialization removes the eager
helper-tuple `vload_addr` payer

- The next clean worktree experiment stayed recorder-side and built on top of
  the array-only result-path change in
  [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c).
- Instead of eagerly materializing the visible hash key from `lj_vm_next`'s
  returned tuple, `lj_record_next()` now leaves the visible key slot unloaded
  on the non-array path and lets the loop body `SLOAD` it later if needed.
- This is not the same as dropping hash keys entirely:
  - on `for _, v in pairs(t)` the body never asks for the visible key, so the
    old `vload_addr` payer disappears
  - on `for k, v in pairs(t)` the body later touches the key slot and the trace
    naturally reloads it with `SLOAD`, which stayed semantically correct in the
    small key-using classifier
- Clean `kdz` results with the same safe env bundle:
  - low-noise `pairs_sum:20` stayed correct: `RESULT actual=500 expected=500`
  - the old repeated `vload_addr` hash payer disappeared from the guard log
  - the remaining repeated hash cluster became:
    - `sload_keyindex`
    - `sload_type`
    - carried-total integer slot/add guards
- A key-using hash classifier also stayed correct:
  - `for k, v in pairs(t) do total = total + v + #k end`
  - `run(20) -> 600`
  - IR confirmed the visible key now comes from `str SLOAD #12` on demand
    instead of the old eager helper-tuple `str VLOAD #1`
- The clean perf gate on `kdz` improved again on the hash hot case:
  - JIT on:
    - `pairs_sum/hot median=0.068346`
    - `pairs_array_sum/hot median=0.072085`
  - compared to the clean post-array baseline:
    - `pairs_sum/hot` improves materially (`0.072109 -> 0.068346`)
    - `pairs_array_sum/hot` stays in the same band (`0.071600 -> 0.072085`)
- So the recorder-side split now looks much cleaner:
  - array: derive the numeric visible key directly from the successor index
  - hash: lazily materialize the visible key only if the loop body actually
    asks for it

2026-03-30: same-host pinned A/B shows the `zkd0` full-lazy hash delta is not
just host-speed skew, but it also is not a direct hash-policy effect

- To separate machine-profile differences from policy effects, I reran the
  split-vs-full-lazy A/B on the same pinned core (`taskset -c 0`) with the
  same safe env bundle and `S390X_PERF_SAMPLES=9`.
- `kdz` same-host repeated medians:
  - split:
    - `pairs_sum/hot`: `0.078863`, `0.079172`, `0.077700`
    - `pairs_array_sum/hot`: `0.079935`, `0.080002`, `0.080002`
  - full-lazy:
    - `pairs_sum/hot`: `0.079261`, `0.080554`, `0.079099`
    - `pairs_array_sum/hot`: `0.080667`, `0.079288`, `0.079789`
  - median-of-medians:
    - hash: full-lazy is only about `0.5%` slower
    - array: full-lazy is about `0.27%` faster
- `zkd0` same-host repeated medians:
  - split:
    - `pairs_sum/hot`: `0.145481`, `0.137792`, `0.136099`
    - `pairs_array_sum/hot`: `0.142606`, `0.126713`, `0.142824`
  - full-lazy:
    - `pairs_sum/hot`: `0.156157`, `0.156250`, `0.131984`
    - `pairs_array_sum/hot`: `0.133959`, `0.130575`, `0.127103`
  - median-of-medians:
    - hash: full-lazy is about `13.33%` slower
    - array: full-lazy is about `8.44%` faster
- So the earlier cross-host read really was misleading:
  - `zkd0` is slower overall than `kdz`
  - absolute host-to-host timings are not directly comparable
  - the only trustworthy comparison is split-vs-full-lazy on the same host
- But the source diff gives the more important structural constraint:
  - split vs full-lazy differs only in the array-success path inside
    `lj_record_next()`
  - the non-array success path already uses the same lazy visible-key rule in
    both variants
  - so the `zkd0` hash delta cannot be a direct consequence of different hash
    recorder logic
- The practical read is:
  - the big `zkd0` full-lazy hash loss is either measurement variance or a
    secondary whole-binary / whole-suite effect
  - it is not a clean argument for changing the intended hash policy
  - the promotable next patch should therefore stay on the split recorder
    shape, not the simpler full-lazy collapse

2026-03-30: `zkd0` fresh-process A/B stays too noisy for policy choice; direct
`KEYINDEX` tag-word compare is an immediate reject

- I followed up the same-host suite A/B with fresh-process isolated hash-only
  and array-only runs on `zkd0` to remove suite-order contamination.
- Those isolated `zkd0` runs did not preserve the earlier clean split:
  - host state during the run was already noisy:
    - split block started around `loadavg 6.73 5.19 3.68`
    - lazy block started around `loadavg 7.35 5.47 3.81`
    - `nproc=8`
  - isolated hash medians:
    - split: `0.143018`, `0.139432`, `0.161071`
    - full-lazy: `0.124187`, `0.152878`, `0.129796`
  - isolated array medians:
    - split: `0.133113`, `0.139894`, `0.132421`
    - full-lazy: `0.127625`, `0.159798`, `0.159999`
- So `zkd0` is still too noisy to pick recorder policy from the benchmark
  numbers alone:
  - the isolated hash run no longer shows the earlier clean “full-lazy loses”
    pattern
  - the isolated array run swings hard enough that its direction is unstable
  - the trustworthy policy read is still:
    - use same-host `kdz` as the primary signal
    - use the source diff itself, which proves split vs full-lazy differs only
      in the array-success path
- I then tested a backend-side hash candidate in the clean split worktree:
  - replace the `IRSLOAD_KEYINDEX` s390x check with a direct high-word compare
    against `LJ_KEYINDEX` instead of the old 64-bit load-plus-shift path
  - the same direct compare was also tried in `asm_gencall_sload()`
- That backend cut is a reject:
  - after rebuilding the clean split repo on `kdz`, even
    `/tmp/oneshot_iter.lua 20` stopped making forward progress
  - the run hung at `n=20` instead of returning the normal `RESULT 500`
  - restoring the previous backend file immediately brought the clean split
    repo back to `RESULT 500`
- So the next target remains the same:
  - keep the split recorder patch as the promotable candidate
  - do not collapse to full-lazy
  - do not pursue the direct `KEYINDEX` tag-word compare path further

2026-03-30: hash `KEYINDEX` no-guard is a semantic reject; hash table live-in
shaping is the real next win

- I tested a recorder-side hash-only `KEYINDEX` classifier first:
  - drop `IRSLOAD_TYPECHECK` from the hidden control-var `KEYINDEX` load on the
    non-array `ITERN` path
  - keep array numeric-key handling unchanged
- That cut is a reject:
  - value-only iterators stayed green:
    - `oneshot_iter.lua 20 -> 500`
    - `oneshot_iter.lua 2000 -> 50000`
    - `oneshot_iter.lua 200000 -> 5000000`
  - but a key-using hash loop stopped making progress
  - so the hidden hash `KEYINDEX` guard is not redundant; it carries real hash
    iteration semantics
- The more useful follow-up was narrower:
  - keep the `KEYINDEX` path unchanged
  - only reshape the `pairs()` table live-in on the hash side
  - in `rec_isnext()` and `rec_itern()`, use a read-only trusted table ref for
    hash iteration instead of a generic typechecked table `SLOAD`
  - array keeps the existing path
- That hash table live-in cut held up on `kdz`:
  - correctness:
    - `oneshot_iter.lua 20 -> 500`
    - `oneshot_iter.lua 2000 -> 50000`
    - `oneshot_iter.lua 200000 -> 5000000`
    - key-using hash loop
      `for k,v in pairs({aa=1,bbb=2,c=3,d=4,e=5}) do total = total + v + #k end`
      still returned `460` for `run(20)`
  - low-noise hash `SLOAD` log:
    - before: hot pre-call hash trace showed table slot `#9` and hidden
      `KEYINDEX` slot `#10`
    - after: the table slot `SLOAD` disappeared while the hidden `KEYINDEX`
      `SLOAD` remained
    - so the hash side now keeps the real semantic guard and drops the table
      live-in typecheck
- Clean `kdz` perf from the detached split worktree improved materially:
  - prior split baseline:
    - `pairs_sum/hot median=0.068346`
    - `pairs_array_sum/hot median=0.072085`
  - hash table live-in cut:
    - run 1:
      - `pairs_sum/hot median=0.058412`
      - `pairs_array_sum/hot median=0.060240`
    - run 2:
      - `pairs_sum/hot median=0.061722`
      - `pairs_array_sum/hot median=0.065274`
    - unconditional clean build:
      - `pairs_sum/hot median=0.060262`
      - `pairs_array_sum/hot median=0.061562`
- `zkd0` remains noisier, so only same-host A/B is trustworthy:
  - baseline split on the same clean repo:
    - `pairs_sum/hot median=0.172180`
    - `pairs_array_sum/hot median=0.133606`
  - hash table live-in cut on the same clean repo:
    - `pairs_sum/hot median=0.171461`
    - `pairs_array_sum/hot median=0.125585`
  - so `zkd0` does not show a regression signal; if anything it trends slightly
    better, but not strongly enough to outweigh the cleaner `kdz` signal
- Practical read:
  - `KEYINDEX` no-guard is rejected
  - hash table live-in shaping is the current promotable hash-side follow-up to
    the split recorder patch
  - the remaining hash cost is not the visible-key tuple load anymore, and not
    the hidden control-var guard either; the next removable payer was the hash
    table slot typecheck

2026-03-30: hash traversal-index keepalive does not change the loop contract

- I tested one narrower follow-up in the clean detached split repo:
  - keep the hash table live-in cut
  - add a hash-only no-op use of the returned traversal index
    (`ix.mobj = ADD ix.mobj, 0`) before leaving the visible key unloaded
  - goal: see whether a trivial explicit use would make the loop preserve the
    returned `HIOP` as a carried value for the next `lj_vm_next()` call
- That classifier is a reject:
  - the hash IR was unchanged
  - it still records:
    - `tab SLOAD #9 R`
    - `int SLOAD #10 TK`
    - `CALLL lj_vm_next (0002 0003)`
    - loop `CALLL lj_vm_next (0002 0003)` again
  - array still differs in the expected way:
    - loop `CALLL lj_vm_next (0002 0006)`
    - with `HIOP` / `PHI` carried around the loop
- So the remaining hash difference is deeper than a dead-code/liveness issue on
  the returned traversal index:
  - a dummy keepalive is not enough to make hash reuse the returned `HIOP` as
    the next loop-carried control input
  - after the table live-in fix, the remaining hash-specific contract is still
    the frame `KEYINDEX` path itself

2026-03-30: value-only hash `KEYINDEX` no-guard by loop-body slot scan is also a
reject

- I tested one more narrow hash-side classifier in the clean detached split
  repo:
  - keep the split recorder patch and the hash table live-in win
  - add an env-gated hash-only fast path
    `LUAJIT_S390X_HASH_KEY_UNUSED_NOGUARD=1`
  - conservatively scan the local loop body from `ITERN` back to its body entry
    and drop `IRSLOAD_TYPECHECK` from the hidden hash `KEYINDEX` load only when
    the visible key slot appears unused
- This is structurally narrower than the earlier global `KEYINDEX` no-guard:
  - key-using loops keep the guard
  - only value-only hash loops are eligible
- Small correctness classifiers stayed green:
  - `/tmp/oneshot_iter.lua 20 -> RESULT 500`
  - `/tmp/oneshot_iter.lua 2000 -> RESULT 50000`
  - `/tmp/oneshot_iter.lua 200000 -> RESULT 5000000`
  - key-using hash loop
    `for k,v in pairs({aa=1,bbb=2,c=3,d=4,e=5}) do total = total + v + #k end`
    still returned `43` for `f(20)`
- But it is not promotable:
  - on an idle `kdz`, a pinned same-binary A/B using the hot hash and array
    loops showed the baseline half finishing in the normal band:
    - `pairs_sum_hot 0.059565`
    - `pairs_array_sum_hot 0.060925`
  - the env-on half then fell into a pathological long-running shape instead of
    finishing in the same band
  - stale host contention was ruled out first by explicitly killing the old
    stray `HASH_KEYINDEX_NOGUARD`, `iterator_table.lua`, and prior bench jobs
- So this value-only keyed no-guard path is also a reject:
  - it is not just “unclear perf”
  - it actively destabilizes the hot hash/array loop surface on clean `kdz`
  - the remaining safe target is still the hash `KEYINDEX` live-in contract
    itself, without dropping the hidden control-var guard

2026-03-30: recorder-side hash control-input carry via unused visible-key slot
is not promotable

- I tested one narrower recorder-only follow-up under
  `LUAJIT_S390X_HASH_KEY_CARRY=1`:
  - keep the promoted split recorder baseline unchanged
  - only on non-array loops where the visible key slot is provably unused,
    cache the returned traversal index in that unused visible-key slot
  - feed the next `lj_vm_next()` call from that carried ref while keeping the
    hidden `KEYINDEX` slot and guard intact
- The experiment rebuilt cleanly and stayed semantically correct on `kdz`:
  - `oneshot_iter.lua 20 -> 500`
  - `oneshot_iter.lua 2000 -> 50000`
  - `oneshot_iter.lua 200000 -> 5000000`
  - key-using hash loop still returned `43` for `f(20)`
- Same-binary pinned A/B on clean `kdz` looked promising:
  - baseline:
    - `pairs_sum_hot 0.059960`
    - `pairs_array_sum_hot 0.062132`
  - env on:
    - `pairs_sum_hot 0.057555`
    - `pairs_array_sum_hot 0.059757`
- But it is not promotable:
  - the low-noise `SLOAD` proxy did not give a clean structural proof that the
    hash loop stopped re-sourcing its second `lj_vm_next()` input from the
    frame `KEYINDEX` path
  - and the `zkd0` same-binary regression screen failed:
    - baseline:
      - `pairs_sum_hot 0.105885`
      - `pairs_array_sum_hot 0.124655`
    - env on:
      - `pairs_sum_hot 0.140418`
      - `pairs_array_sum_hot 0.134604`
- So this carry path is a reject for now:
  - it is a real directional classifier on `kdz`
  - but it is not stable enough across hosts to land
  - the branch stays on the promoted recorder split baseline only

2026-03-30: backend dedup of duplicate hash `KEYINDEX` gencall/typecheck is a
crash reject

- I tested one narrower backend-side follow-up for the remaining hash payer:
  - keep the promoted recorder split baseline unchanged
  - in `asm_sload()`, when a hidden `IRSLOAD_KEYINDEX` ref has no direct uses
    and the same ref was already typechecked by `asm_gencall_sload()` while
    being prepared as the `lj_vm_next()` argument, skip the second local
    `sload_keyindex` typecheck
  - gate it behind `LUAJIT_S390X_DEDUP_KEYINDEX_GENCALL_SLOAD=1`
- This required first syncing the clean detached split repo with the current
  s390x assembler state from the main workspace:
  - the clean repo did not yet carry the `ASMState` gencall-tracking fields in
    `lj_asm.c`
  - without that sync the header-only experiment was not even self-consistent
- Once the clean detached repo was made self-consistent, the experiment
  rejected immediately on `kdz`:
  - build completed successfully
  - `env LUAJIT_S390X_DEDUP_KEYINDEX_GENCALL_SLOAD=1 ./luajit /tmp/oneshot_iter.lua 200000`
    crashed with `SIGSEGV`
- So this dedup path is not promotable:
 - it is not just “no perf win”
  - it breaks the long authoritative iterator surface outright
  - the remaining safe target stays recorder/live-in side, not another attempt
    to delete the hidden hash `KEYINDEX` guard path in backend code

2026-03-30: clean s390x rebuilds were still disabling JIT by default

- The validation floor failure on `kdz` was source-level, not host noise:
  - `make -pn` showed `TARGET_LJARCH = s390x`
  - but the same `TARGET_TESTARCH` expansion still contained:
    - `LJ_TARGET_S390X 1`
    - `LJ_ARCH_NOJIT 1`
    - `LJ_HASJIT 0`
  - so the clean build was correctly identifying s390x, then disabling JIT in
    `src/lj_arch.h`
- The cause was the still-active opt-in guard in `src/lj_arch.h`:
  - s390x kept:
    - `#if !defined(LUAJIT_ENABLE_S390X_JIT)`
    - `#define LJ_ARCH_NOJIT 1 /* NYI */`
  - which meant every clean native rebuild without ad hoc `XCFLAGS` produced a
    no-JIT binary, even though the branch now relies on default-clean JIT
    rebuilds for validation
- I removed that guard locally and restamped it on a clean `kdz` repo:
  - patch one clean repo’s `src/lj_arch.h`
  - `make clean && make -j4`
  - then `./luajit -e 'print(jit and jit.status())'`
- Result:
  - clean rebuild returned `true`
  - `jit.on()` worked again without any `XCFLAGS=-DLUAJIT_ENABLE_S390X_JIT`
- This is not a perf change by itself, but it fixes the validation floor:
  - clean s390x builds are JIT-capable again by default
  - later recorder-side A/B work can be trusted without hidden build flags

2026-03-30: `rec_itern()` `nextt != IRT_NIL` loopback decision is a mixed
reject

- With the clean-build floor repaired, I retested the narrow recorder theory in
  `src/lj_record.c`:
  - keep the promoted recorder split baseline unchanged
  - change the `rec_itern()` loopback test from:
    - `if (!tref_isnil(ix.key))`
  - to:
    - `if ((nextt & 0xff) != IRT_NIL)`
- Why this was worth testing:
  - the lazy non-array path intentionally sets `ix->key = 0`
  - so the old test conflates:
    - “visible key intentionally unloaded”
    - and “iterator actually returned nil”
  - that made it the tightest recorder-side suspicion for the remaining hash
    control asymmetry
- The experiment stayed semantically correct on clean `kdz`:
  - `oneshot_iter.lua 20 -> 500`
  - `oneshot_iter.lua 2000 -> 50000`
  - key-using hash loop still returned the expected `740` for `run(20)`
- On the repaired clean repo, that one-line change does not buy a distinct
  structural win on its own:
  - after reverting it, the same value-only hash IR still looped as:
    - first call `CALLL lj_vm_next (0002 0003)`
    - loop call `CALLL lj_vm_next (0002 0006)`
  - so the earlier “it caused the control input carry” read was not stable
    enough to treat as causal
  - the useful result remains the pinned same-repo perf A/B, not the stale
    structural interpretation
- But same-repo pinned `kdz` A/B says it is not the right landing policy:
  - candidate:
    - `pairs_sum/hot 0.062256`
    - `pairs_array_sum/hot 0.059013`
  - reverted baseline on the same repo:
    - `pairs_sum/hot 0.059822`
    - `pairs_array_sum/hot 0.062837`
- So the change trades hash down for array up:
  - array improves materially
  - hash regresses materially
  - that makes it a mixed policy, not a promotable hash fix
- Status:
  - reject for the current branch
  - branch stays on the last validated recorder split baseline
  - the remaining hash target is still narrower than this loop/leave rewrite

2026-03-30: value-only hash body-scan loopback override is a structural win but
still a perf reject

- After repairing the clean-build floor and syncing a clean `kdz` repo to the
  actual local recorder baseline, I restamped the real value-only hash root
  shape:
  - no visible-key `VLOAD #1` on the baseline
  - lazy non-array key is already working
  - but the root trace still loops as:
    - `tab SLOAD #9 R`
    - hidden `KEYINDEX SLOAD #10 TK`
    - `CALLL lj_vm_next (0002 0003)`
    - loop `CALLL lj_vm_next (0002 0003)` again
- The focused `ITERN` log on that synced baseline made the remaining mismatch
  explicit:
  - `site=after_next`:
    - `key_nil=1`
    - `key_ref=-32768`
    - `s_key=0`
  - then it immediately falls to `site=nil`
  - so the baseline still conflates:
    - “visible key intentionally unloaded”
    - and “iterator returned nil”
- A first narrower retry using `idxchain` as the discriminator is a dead end:
  - value-only hash loops still show `idxchain=0`
  - bytecode dumps show why:
    - both `for _, v in pairs(t)` and `for k, v in pairs(t)` compile to
      `ITERN 9 3 3`
  - so neither `rb` nor `idxchain` distinguishes “key unused”
- I then tested a more exact recorder-side discriminator:
  - add a conservative bytecode body scan between the `ITERN` payload entry and
    the next iterator step
  - only on non-array loops with lazy visible key and non-nil result, override
    the nil/loopback decision when slot `ra` is never read in that body
- That cut is semantically safe and does the structural thing we wanted:
  - `oneshot_iter.lua 20 -> 500`
  - `oneshot_iter.lua 2000 -> 50000`
  - key-using hash loop still returned `740`
  - the focus log flips from `site=nil` to `site=payload`
  - root-trace hash IR then becomes:
    - first call `CALLL lj_vm_next (0002 0003)`
    - loop call `CALLL lj_vm_next (0002 0006)`
    - i.e. hidden control input is carried through prior-result `HIOP`
- But same-host pinned `kdz` perf still rejects it:
  - candidate:
    - `pairs_sum/hot 0.061420`
    - `pairs_array_sum/hot 0.063508`
  - synced baseline just before the patch:
    - `pairs_sum/hot 0.060340`
    - `pairs_array_sum/hot 0.062151`
- So even the correct value-only body-use discriminator is not the landing fix:
  - it gets the structural carry we wanted
  - but it still makes the hot loop slower on the authoritative host
- Status:
  - reject for the current branch
  - branch stays on the synced recorder split baseline plus the clean-build
    `lj_arch.h` floor fix
  - the remaining hash payer is now narrower than:
    - lazy visible-key loading
    - table live-in shaping
    - or loopback-control carrying by body-use discrimination

2026-03-30: hash `TRACE 2` abort churn is real, but removing it is still not a
perf win

- With the synced clean `kdz` baseline restored, the next asymmetry is easy to
  restamp:
  - array value-only loop:
    - `TRACE 1` forms
    - `TRACE 2` immediately forms and stops to loop
  - hash value-only loop:
    - `TRACE 1` forms
    - then `TRACE 2 start 1/1 ... abort ... leaving loop in root trace`
      repeats over and over
- The focused recorder logs on that exact surface (`parent=1`, `exit=1`) show
  why hash churns:
  - root `trace=1`:
    - `oldop=BC_ITERN`
    - `newop=BC_FORL`
    - `key_nil=1`
  - repeated side trace `trace=2 parent=1 exit=1`:
    - `site=after_next`
    - `nextt=4`
    - `key_nil=1`
    - `key_ref=-32768`
    - `s_key=0`
  - then it immediately falls to `site=nil`
  - so the side-trace hash churn is the same lazy-key/nil conflation already
    seen at root level
- I tested the exact surgical fix that should only touch that churn:
  - keep root trace behavior unchanged
  - only for side traces (`parent != 0 && exitno == 1`)
  - only for non-array loops with lazy visible key and non-nil `nextt`
  - use the conservative bytecode body scan to prove slot `ra` is unused
  - then override `site=nil` to `site=payload`
- That cut is semantically safe and does exactly what it should structurally:
  - `oneshot_iter.lua 20 -> 500`
  - `oneshot_iter.lua 2000 -> 50000`
  - key-using hash loop still returned `740`
  - focused log on `trace=2 parent=1 exit=1` flips from:
    - `site=after_next -> site=nil`
  - to:
    - `site=after_next -> site=payload`
  - `TRACE 2` now forms and stops to loop instead of repeatedly aborting
  - `TRACE 2 IR` becomes:
    - first call `CALLL lj_vm_next (0002 0003)`
    - loop call `CALLL lj_vm_next (0002 0006)`
    - i.e. carried hidden control via prior-result `HIOP`
- But same-host pinned `kdz` A/B still rejects it:
  - side-trace-only candidate single run:
    - `pairs_sum/hot 0.062196`
    - `pairs_array_sum/hot 0.063217`
  - repeated candidate hot medians:
    - hash: `0.061813`, `0.057118`, `0.060274`
    - array: `0.061538`, `0.062793`, `0.064080`
  - repeated synced-baseline hot medians right after restore:
    - hash: `0.056385`, `0.058654`, `0.060260`
    - array: `0.059320`, `0.062397`, `0.061621`
- So the conclusion changed again:
  - hash `TRACE 2` abort churn is real and recorder-caused
  - but eliminating that churn alone is not enough to win the hot benchmark
  - the remaining major hash payer is below the `TRACE 2 start/abort` seam
- One more useful structural comparison from the same clean baseline:
  - array `TRACE 1 IR`:
    - current iteration value comes from helper result `VLOAD #0`
    - loop call already uses carried `HIOP`
  - hash `TRACE 1 IR`:
    - still starts with:
      - `tab SLOAD #9 R`
      - hidden `KEYINDEX SLOAD #10 TK`
      - `CALLL lj_vm_next (0002 0003)`
      - `int VLOAD #0`
      - extra frame `int SLOAD #12 T`
    - and loops as:
      - `CALLL lj_vm_next (0002 0003)` again
  - so the remaining hash payer now looks more like root-trace steady-state
    value/control ownership than side-trace churn

2026-03-30: landing split summary

- Treat the branch as three lanes:
  - Lane A: stability/build-floor
  - Lane B: promotable recorder-side iterator perf
  - Lane C: parked bridge/continuation research
- Operational baseline for further work:
  - one authoritative clean `kdz` worktree
  - one `zkd0` regression worktree
  - full tracked-file sync only
  - direct `src/` rebuild only
  - same-host pinned `kdz` A/B is the policy signal
  - low-noise manual logging or debugger only

Lane A

- The clean-build floor was still wrong:
  - s390x clean rebuilds were disabling JIT by default in `src/lj_arch.h`
  - removing the `LUAJIT_ENABLE_S390X_JIT` opt-in guard restores
    default-clean `jit.status() == true`
- The stability/build-floor stack is:
  - `0a76ac86` `ERRNO_SAVE` / `ERRNO_RESTORE` hardening in
    `src/lj_dispatch.h`
  - `a48c6214` `IRSLOAD_KIDX_NUMKEY` restore in `src/lj_ir.h`
  - local `src/lj_arch.h` JIT-default fix

Lane B

- The promotable recorder baseline in `src/lj_record.c` is:
  - array visible numeric key from successor index `HIOP(trvk) - 1`
  - lazy non-array visible key
  - trusted read-only hash table live-in shaping in `rec_isnext()` /
    `rec_itern()`
- This removed real hot payers on clean `kdz`:
  - `pairs_array_sum/hot` improved from `0.083461` to `0.071600`
  - `pairs_sum/hot` improved from `0.072109` to `0.068346`
  - hash table live-in shaping brought clean medians down again to about
    `0.060262` for `pairs_sum/hot` and `0.061562` for
    `pairs_array_sum/hot`
- Keep this as a split patch, not a full-lazy collapse.

Reject pile

- Do not reopen:
  - full-lazy collapse
  - any `KEYINDEX` no-guard path
  - direct `KEYINDEX` tag-word compare
  - backend `KEYINDEX` guard dedup
  - recorder-side hash carry through unused visible-key slot
  - body-scan loopback overrides as landing policy
  - `TRACE 2` churn elimination as a perf proxy
  - `trace3` accumulator live-in injection
  - bridge-local producer/consumer fusion

Next hash target

- The remaining major hash cost is now below visible-key laziness, below hash
  table live-in shaping, and below `TRACE 2` churn.
- Next task:
  - map root-trace `SLOAD #12 T` on the clean split baseline for value-only
    hash, key-using hash, and array control loops
  - only then decide the next root-trace steady-state patch

2026-03-30: hash root trace was still frame-sourcing the value lane

- Clean `kdz` root-trace mapping on the split baseline showed:
  - value-only hash: extra frame `int SLOAD #13 T`
  - key-using hash: `str SLOAD #12 T` is the visible key and `int SLOAD #13 T`
    is still the value lane
  - array value-only: no extra frame value `SLOAD`; it uses helper `VLOAD #0`
    directly
- That narrowed the remaining hash payer again:
  - the current hash root trace was not just carrying a stale control input
  - it was also failing to seed the current value slot from the helper result
    on the lazy-key non-array path
- Recorder-side candidate:
  - keep lazy non-array key behavior unchanged
  - keep the hidden `KEYINDEX` guard unchanged
  - but seed `J->base[ra+1]` from `ix.val` on the non-array, non-nil,
    lazy-key path before the loop/leave decision
- Structural result on clean `kdz`:
  - value-only hash root trace changed from
    - frame `SLOAD #13 T` plus unused helper `VLOAD #0`
  - to
    - direct helper `VLOAD #0` feeding the add
    - no extra frame value `SLOAD`
- Validation:
  - `kdz` correctness stayed green:
    - value-only hash `300`
    - key-using hash `460`
    - array control `500`
  - same-host pinned `kdz` A/B:
    - candidate:
      - `pairs_sum/hot median=0.061129`
      - `pairs_array_sum/hot median=0.061913`
    - restored split baseline:
      - `pairs_sum/hot median=0.064761`
      - `pairs_array_sum/hot median=0.063512`
  - `zkd0` regression screen stayed green:
    - value-only hash `300`
    - key-using hash `460`
    - array control `500`
    - `pairs_sum/hot median=0.133049`
    - `pairs_array_sum/hot median=0.124357`
- So this is the first post-split hash root-trace steady-state win that:
  - removes a real frame-materialization payer
  - keeps key-using hash semantics intact
  - improves the authoritative `kdz` hot loop
  - and passes the `zkd0` regression screen

2026-03-30: pre-seeding the hidden hash control slot from `ix.mobj` is a reject

- On the current baseline after the value-lane fix, clean `kdz` root focus logs
  show:
  - array root goes `site=after_next -> site=payload`
  - hash root still goes `site=after_next -> site=nil`
- Narrow experiment:
  - after `lj_record_next()`, but before the `payload` / `nil` split in
    `rec_itern()`, pre-seed `J->base[ra-1] = ix.mobj | TREF_KEYINDEX` for
    non-array successful `next()` results
  - keep lazy visible-key behavior unchanged
  - keep the semantic hidden `KEYINDEX` guard unchanged
- Structural result on clean `kdz`:
  - value-only hash root trace changed from loop
    - `CALLL lj_vm_next (0002 0003)`
  - to loop
    - `CALLL lj_vm_next (0002 0006)`
    - plus carried `HIOP` / `PHI` for the second helper arg
- Validation:
  - `kdz` correctness stayed green:
    - `HASH_VALUE 300`
    - `HASH_KEY 460`
    - `ARRAY_VALUE 500`
  - same-host pinned `kdz` A/B/A was mixed:
    - candidate 1:
      - `pairs_sum/hot median=0.055925`
      - `pairs_array_sum/hot median=0.060627`
    - restored baseline:
      - `pairs_sum/hot median=0.056608`
      - `pairs_array_sum/hot median=0.058633`
    - candidate 2:
      - `pairs_sum/hot median=0.055678`
      - `pairs_array_sum/hot median=0.058943`
  - `zkd0` regression screen failed:
    - candidate:
      - `pairs_sum/hot median=0.120925`
      - `pairs_array_sum/hot median=0.121979`
    - restored baseline:
      - `pairs_sum/hot median=0.116330`
      - `pairs_array_sum/hot median=0.108804`
- Conclusion:
  - the structural carry is real
  - but this is not promotable because it does not beat the synced split
    baseline cleanly on `kdz` and it regresses both hot cases on `zkd0`

2026-03-30: value-only-only hidden-control carry is also a reject

- Narrow follow-up:
  - keep key-using hash on the old root path
  - only pre-seed the hidden `KEYINDEX` control slot from `ix.mobj` when a
    conservative bytecode body scan shows the visible key slot is not read in
    the loop body
- Structural result:
  - value-only hash still flips to loop
    - `CALLL lj_vm_next (0002 0006)`
  - key-using hash stays on
    - `CALLL lj_vm_next (0002 0003)`
- Validation on pinned `kdz`:
  - candidate:
    - `pairs_sum/hot median=0.057171`
    - `pairs_array_sum/hot median=0.058124`
  - restored baseline:
    - `pairs_sum/hot median=0.056608`
    - `pairs_array_sum/hot median=0.058633`
- Conclusion:
  - narrowing the carry to value-only hash is still not enough
  - the hidden-control carry family should be considered exhausted for the
    current branch

2026-03-30: native `AR` / `SR` overflow lowering is a backend reject

- Hardware check on `kdz` showed:
  - `AR` / `SR` set the expected overflow condition directly
  - `LGFR` sign-extends the 32-bit result and preserves that condition code
  - so a narrower s390x backend cut was viable in principle
- Backend experiment:
  - add local `AR` / `SR` opcodes to `src/lj_emit_s390x.h`
  - replace only the rr guarded integer add/sub paths in
    `src/lj_asm_s390x.h`
  - old path:
    - `LGFR`
    - `AGR` / `SGR`
    - `LGFR`
    - `CGR`
    - guard on `CC_NE`
  - candidate path:
    - `AR` / `SR`
    - `LGFR`
    - guard on `CC_OF`
- Structural result:
  - low-noise guard mix changed as intended
  - old `addov_rr_int_eq` disappeared
  - new `addov_rr_int_of` became the dominant shared add guard
- Validation:
  - `kdz` correctness stayed green
  - same-host pinned `kdz` was mixed:
    - candidate 1:
      - `pairs_sum/hot median=0.056099`
      - `pairs_array_sum/hot median=0.057465`
    - restored baseline:
      - `pairs_sum/hot median=0.056091`
      - `pairs_array_sum/hot median=0.058564`
    - candidate 2:
      - `pairs_sum/hot median=0.058984`
      - `pairs_array_sum/hot median=0.057309`
  - `zkd0` regression screen failed badly:
    - candidate:
      - `pairs_sum/hot median=0.148769`
      - `pairs_array_sum/hot median=0.200513`
- Conclusion:
  - the backend simplification is real and hits the intended payer
  - but it is not promotable because it does not hold up cross-host and
    regresses the `zkd0` hot loops sharply

2026-03-30: hash-only numeric accumulator `SLOAD(CONVERT)` is also a reject

- Goal:
  - replace the local custom `num ADD` override with a real numeric reload of
    the accumulator slot on the exact tagged value-only hash `ADDVV`
  - if that held the accumulator slot as `num`, it could remove both the hash
    root `ADDOV` pair and the loop-back `int.num` check without widening policy
- Recorder/backend experiment:
  - add temporary s390x scratch state in `jit_State` to tag the value-only hash
    payload `ADDVV`
  - on the tagged `BC_ADDVV`, reload the non-value operand with
    `sloadt(..., IRT_GUARD|IRT_NUM, IRSLOAD_TYPECHECK|IRSLOAD_CONVERT)`
    instead of forcing a custom `num ADD`
  - add a minimal s390x-only `IRSLOAD_CONVERT` path in `asm_sload()` for the
    `num <- int` case
- Structural result on clean `kdz`:
  - hash root trace changed from
    - `int VLOAD #0`
    - `int SLOAD #3 T`
    - `num CONV value`
    - `num CONV total`
    - `num ADD`
    - loop-back `int CONV prev_num int.num check`
  - to
    - `int VLOAD #0`
    - `int SLOAD #3 T`
    - `num SLOAD #3 TC`
    - `num CONV value`
    - `num ADD`
    - loop-back `int CONV prev_num int.num check`
  - so the true numeric reload landed, but the loop still paid the back-edge
    integer conversion
- Validation:
  - `kdz` correctness stayed green for the value-only hash micro:
    - `HASH_VALUE 300`
  - pinned `kdz` perf was mixed:
    - candidate:
      - `pairs_sum/hot median=0.055438`
      - `pairs_array_sum/hot median=0.062029`
    - restored baseline:
      - `pairs_sum/hot median=0.056341`
      - `pairs_array_sum/hot median=0.059806`
  - `zkd0` correctness stayed green:
    - `HASH_VALUE 300`
    - `HASH_KEY 310`
    - `ARRAY_VALUE 500`
    - `RESULT 5000000`
  - but the `zkd0` regression screen failed hard:
    - candidate:
      - `pairs_sum/hot median=0.141693`
      - `pairs_array_sum/hot median=0.203562`
    - restored baseline:
      - `pairs_sum/hot median=0.209249`
      - `pairs_array_sum/hot median=0.254719`
- Conclusion:
  - this is not a clean cross-host reject in the old sense, because it helped
    `kdz` hash and also improved the current `zkd0` baseline
  - but it remains a reject for the current branch because it materially
    regresses the array hot case on both hosts and does not remove the
    loop-back `int.num` check that motivated the experiment
  - the next live target is still the shared backend `addov_rr_int_eq` guard
    path, not more recorder-side hash carry or convert tagging

2026-03-30: `AGFR`/`CGFR` equality-guard lowering is a backend reject

- Instruction-floor check on `kdz`:
  - gas accepts the signed-32 register forms directly:
    - `agfr %r2,%r3`
    - `cgfr %r2,%r3`
    - `sgfr %r2,%r3`
  - so there is a real instruction-level alternative to the current
    `LGFR tmp,dest` + `CGR dest,tmp` sequence for guarded integer add/sub
- Backend experiment:
  - keep the existing equality-guard semantics
    - overflow test remains `sum64 == sext32(sum64)`
  - replace only the int-guarded add/sub equality path in
    `src/lj_asm_s390x.h`
  - old rr shape:
    - `LGFR dest,dest`
    - `AGR` / `SGR`
    - `LGFR tmp,dest`
    - `CGR dest,tmp`
    - guard on `CC_NE`
  - candidate rr shape:
    - `LGFR dest,dest`
    - `AGFR` / `SGFR`
    - `CGFR dest,dest`
    - guard on `CC_NE`
  - constant-path `AGHI` cases were cut the same way:
    - `LGFR dest,dest`
    - `AGHI`
    - `CGFR dest,dest`
    - guard on `CC_NE`
- Structural result:
  - low-noise add/guard logs still hit the intended payer:
    - `addov_rr_int_eq`
  - but the temporary compare register is gone from the backend shape
- Validation:
  - `kdz` correctness stayed green:
    - `HASH_VALUE 300`
    - `HASH_KEY 310`
    - `ARRAY_VALUE 500`
    - `RESULT 5000000`
  - pinned `kdz` perf lost to the restored baseline:
    - candidate:
      - `pairs_sum/hot median=0.058440`
      - `pairs_array_sum/hot median=0.060322`
    - restored baseline:
      - `pairs_sum/hot median=0.056341`
      - `pairs_array_sum/hot median=0.059806`
- Conclusion:
  - this is a real backend simplification, not a no-op
  - but it is not promotable because it still loses on the authoritative
    pinned `kdz` baseline
  - so the remaining target is no longer “find a smaller equality-guard
    sequence”; it is to remove or avoid the hot `IR_ADDOV` earlier than final
    machine lowering

2026-03-30: generalized iterator payload `ADDVV` numeric reload is a reject

- Goal:
  - target the actual hot root-trace owner directly
  - for exact iterator bodies whose first payload instruction is `BC_ADDVV`
    consuming the helper-returned value slot, reload the other operand as
    `num` with `IRSLOAD_CONVERT` and let normal arithmetic lowering run
  - unlike the earlier hash-only cut, widen this exact payload tag to both
    array and hash value-only loops
- Recorder/backend experiment:
  - add temporary iterator-payload scratch fields in `jit_State`
  - in `rec_itern()`, tag the exact payload pc and value slot whenever
    `lj_record_next()` returned a non-nil value
  - in `lj_record_ins()`, on the exact tagged `BC_ADDVV`, reload the
    non-value operand with
    `sloadt(..., IRT_GUARD|IRT_NUM, IRSLOAD_TYPECHECK|IRSLOAD_CONVERT)`
  - add the minimal s390x `IRSLOAD_CONVERT` support in `asm_sload()` for the
    `num <- int` case
- Structural result on clean `kdz`:
  - hash root trace changed from
    - `int VLOAD #0`
    - `int SLOAD #3 T`
    - `int ADDOV`
  - to
    - `int VLOAD #0`
    - `num SLOAD #3 TC`
    - `num CONV value`
    - `num ADD`
    - loop-back `int CONV prev_num int.num check`
  - array root trace flipped the same way:
    - old array root used `int ADD` for the visible numeric key and
      `int ADDOV` for the carried total
    - candidate array root kept the numeric-key path intact but replaced the
      carried total with `num SLOAD #3 TC`, `num ADD`, and the same back-edge
      `int.num` check
- Correctness:
  - `kdz` stayed green on the exact value-only and key-using micros:
    - `HASH_VALUE_MICRO 300`
    - `HASH_KEY 310`
    - `ARRAY_VALUE_MICRO 500`
- Validation on pinned `kdz`:
  - candidate:
    - `pairs_sum/hot median=0.056274`
    - `pairs_array_sum/hot median=0.063843`
  - restored baseline:
    - `pairs_sum/hot median=0.056341`
    - `pairs_array_sum/hot median=0.059806`
- Conclusion:
  - this proves the dominant root-trace `ADDOV` can be displaced structurally
    for both iterator families
  - but the generalized numeric-reload policy is not promotable because array
    gets materially worse while hash is effectively flat
  - so the remaining problem is not “turn value-only iterator accumulation into
    num everywhere”; the next target has to explain why array loses when the
    root `ADDOV` is removed and why hash does not win enough to justify it

2026-03-31: hash-only accumulator preseed-to-num is also a reject

- Goal:
  - avoid the rejected payload-local reload path and try a narrower recorder cut
  - on the exact non-array path, if the first payload instruction is
    `BC_ADDVV` consuming the helper-returned value slot, preseed the other
    payload operand slot in `J->base` as `num` before the payload runs
  - if that worked, hash could avoid both the hot root `ADDOV` and the frame
    value reload without touching array policy or adding a custom backend path
- Recorder experiment:
  - in `rec_itern()`, inspect the exact payload pc
  - only for `!nextisarray` and exact first-op `BC_ADDVV` using `ra+1`
  - preseed the other operand slot as `num` in `J->base`
    - use `sloadt(..., IRT_GUARD|IRT_NUM, IRSLOAD_TYPECHECK|IRSLOAD_CONVERT)`
      if the slot was still unloaded and the runtime TValue was integer
    - otherwise convert an already-loaded integer ref with `IR_CONV num.int`
- Structural result on clean `kdz`:
  - hash root trace changed to a true numeric accumulator chain:
    - `CALLL lj_vm_next (0002 0003)`
    - `num CONV value`
    - `num CONV total`
    - `num ADD`
    - loop `CALLL lj_vm_next (0002 0003)` again
    - back-edge `int CONV prev_num int.num check`
    - `num PHI`
  - so this cut did remove the old frame value reload and the plain root
    `ADDOV`, but it still kept the loop-back `int.num` check
- Correctness:
  - `kdz` stayed green on the standard checks:
    - `RESULT 5000000`
    - hash value micro `300`
    - hash key micro `600`
    - array value micro `500`
    - array key micro `800`
- Validation on pinned `kdz`:
  - first candidate pass:
    - `pairs_sum/hot median=0.057114`
    - `pairs_array_sum/hot median=0.061318`
  - restored baseline immediately after:
    - `pairs_sum/hot median=0.057555`
    - `pairs_array_sum/hot median=0.061202`
  - repeated same-host A/B remained too close and too noisy to show a clear
    hash win:
    - candidate runs:
      - `pairs_sum/hot median=0.057561`, `pairs_array_sum/hot median=0.060429`
      - `pairs_sum/hot median=0.055838`, `pairs_array_sum/hot median=0.068660`
      - `pairs_sum/hot median=0.058882`, `pairs_array_sum/hot median=0.056667`
    - restored baseline runs:
      - `pairs_sum/hot median=0.057236`, `pairs_array_sum/hot median=0.061361`
      - `pairs_sum/hot median=0.057604`, `pairs_array_sum/hot median=0.058329`
      - `pairs_sum/hot median=0.059835`, `pairs_array_sum/hot median=0.057742`
- Conclusion:
  - this is a real structural change, not a no-op:
    - hash accumulator became `num`
    - root `ADDOV` disappeared
    - the frame value reload also disappeared from the hot root trace
  - the surviving back-edge `int.num` check is now explained:
    - `src/lj_opt_loop.c` inserts it during loop unroll type reconciliation
    - exact site:
      - when a copied loop-carried dependency changes from original `int` to
        new `num`, `loop_unroll()` hits
        `irt_isnum(irr->t) && irt_isinteger(t)` and emits
        `IR_CONV int.num check`
    - that means this family will always keep the back-edge check unless the
      original carried slot type is already `num` before loop unroll sees it
  - but it still did not produce a convincing same-host `kdz` win
  - because the policy is hash-only (`!nextisarray`), the array variance in the
    repeated runs is not a policy signal and should be treated as noise
  - the remaining live seam in this family is now even narrower:
    - the back-edge `int.num` check that survives after the root `ADDOV`
      and frame value reload are gone
  - until that surviving check can be removed or explained, this preseed-to-num
    cut is not promotable

2026-03-31: exact operand-decode early-num load is also a reject

- Goal:
  - move the same hash-only accumulator family one step earlier than the
    preseed-to-num cut
  - tag the exact non-array payload `BC_ADDVV` in `rec_itern()`
  - then, in `lj_record_ins()` operand decode, load the accumulator slot as
    `num` with `IRSLOAD_TYPECHECK|IRSLOAD_CONVERT` before any plain int
    `getslot()` can record the original slot ref
  - if that worked, it would be the first version that could plausibly avoid
    the old int-origin slot contract before the payload arithmetic is recorded
- Recorder/backend experiment:
  - add temporary exact-payload scratch fields in `jit_State`
  - tag only the exact non-array `BC_ADDVV` payload slot in `rec_itern()`
  - in `lj_record_ins()`, override the tagged operand decode to use
    `sloadt(..., IRT_GUARD|IRT_NUM, IRSLOAD_TYPECHECK|IRSLOAD_CONVERT)`
  - keep the minimal s390x `IRSLOAD_CONVERT` support in `asm_sload()`
- Structural result on clean `kdz`:
  - the hot slot load really changed:
    - `S390X_SLOAD ref=8 op1=3 type=19 op2=0xc`
  - the old backend add log did not appear:
    - no `S390X_ADD kind=addov_rr_int_eq`
  - so this is a real earlier cut than the plain payload-local convert path
- Correctness:
  - `kdz` stayed green on the focused micros:
    - `HASH_VALUE 3000`
    - `HASH_KEY 1320`
    - `ARRAY_VALUE 3000`
- Validation on pinned `kdz`:
  - candidate:
    - `pairs_sum/hot median=0.059046`
    - `pairs_array_sum/hot median=0.058509`
  - restored pushed baseline immediately after:
    - `pairs_sum/hot median=0.058474`
    - `pairs_array_sum/hot median=0.059654`
- Conclusion:
  - this is another real structural change, not a no-op
  - but it still fails the authoritative same-host `kdz` bar:
    - hash got slightly worse
    - array got slightly better
  - that means even the earliest exact payload-slot numeric load tested so far
    is not a promotable iterator perf win
  - the accumulator-to-num family should stay in the reject pile unless a new
    cut can both remove the surviving loop-unroll `int.num` check and beat the
    synced split baseline on same-host `kdz`

2026-03-31: Lane A floor and four-piece Lane B baseline are the new freeze point

- Lane split:
  - Lane A is build/stability only
  - Lane B is promotable recorder-side iterator perf only
  - Lane C is parked bridge/continuation research only
  - the current branch should stop cross-contaminating those three lines

- Lane A restamp on clean default s390x rebuilds:
  - `kdz` clean rebuild in `src/` came back JIT-enabled by default:
    - `jit.status() => true fold cse`
  - `zkd0` clean rebuild in `src/` also came back JIT-enabled by default:
    - `jit.status() => true fold cse`
  - long-run correctness stayed green on the clean default builds:
    - `kdz`:
      - `/tmp/oneshot_iter.lua 20 => RESULT 500`
      - `/tmp/oneshot_iter.lua 2000 => RESULT 50000`
      - `/tmp/oneshot_iter.lua 200000 => RESULT 5000000`
      - `-joff /tmp/oneshot_iter.lua 200000 => RESULT 5000000`
    - `zkd0`:
      - `/tmp/oneshot_iter.lua 200000 => RESULT 5000000`
      - `-joff /tmp/oneshot_iter.lua 200000 => RESULT 5000000`
  - conclusion:
    - the validated Lane A floor is now:
      - `ERRNO_SAVE` / `ERRNO_RESTORE` hardening
      - `IRSLOAD_KIDX_NUMKEY`
      - s390x JIT enabled by default without `LUAJIT_ENABLE_S390X_JIT`

- Lane B freeze point:
  - keep the four-piece recorder split only:
    - array visible numeric key from successor index
    - lazy non-array visible key
    - trusted read-only hash table live-in shaping
    - non-array value-lane seeding from `ix.val`
  - do not collapse to full-lazy
  - do not reopen hidden-control carry or accumulator-to-`num` families unless
    a new cut beats this exact baseline on pinned `kdz`

- Current pinned-host perf baseline:
  - `kdz` same-host pinned:
    - `pairs_sum/hot median=0.056362`
    - `pairs_array_sum/hot median=0.061370`
  - `zkd0` regression screen:
    - `HASH_VALUE 3000`
    - `HASH_KEY 1320`
    - `ARRAY_VALUE 3000`
    - `pairs_sum/hot median=0.098189`
    - `pairs_array_sum/hot median=0.097454`

- Current low-noise owner map on the frozen baseline:
  - value-only hash:
    - shared payer is still `addov_rr_int_eq`
    - non-value cluster is still the hidden `KEYINDEX` load:
      - `S390X_SLOAD ... op1=10 ... op2=0x44`
    - the only other frame `SLOAD` is the carried total slot:
      - `S390X_SLOAD ... op1=3 ... op2=0x4`
    - there is no extra visible value-lane frame `SLOAD`
  - key-using hash:
    - still pays shared `addov_rr_int_eq`
    - keeps the hidden `KEYINDEX` load
    - adds a visible key/type `SLOAD`:
      - `S390X_SLOAD ... op1=11 ... type=4 ... op2=0x4`
  - array value-only control:
    - still pays shared `addov_rr_int_eq`
    - keeps numeric-key array control loads:
      - `S390X_SLOAD ... op1=10 ... op2=0xc4`
      - `S390X_SLOAD ... op1=9 ... type=11 ... op2=0x4`

- Minimal proof that hash root no longer frame-sources the value lane:
  - value-only hash bytecode payload is:
    - `ADDVV 1 1 10`
  - on the frozen baseline, low-noise hash compile logs show only:
    - the carried total slot `SLOAD`
    - the hidden `KEYINDEX` `SLOAD`
  - there is no third frame `SLOAD` for the helper-returned visible value lane
  - conclusion:
    - helper `VLOAD #0` now feeds the hash add path directly on the frozen
      baseline, and the old extra frame value `SLOAD` is gone

- Decision from the restamp:
  - shared `addov_rr_int_eq` is now the dominant cross-family payer
  - hash root still carries the hidden `KEYINDEX` load cluster
  - the next live target is not another carry/no-guard/full-lazy experiment
  - if one more accumulator-family pass is attempted at all, it must target
    the original carried-total type before `loop_unroll()` sees it, and it
    must be rejected immediately if the back-edge `int.num` check survives

2026-03-31: fresh freeze-point restamp holds, backend classifier is negative, and the final exact accumulator preload is a reject

- Fresh host capture on the authoritative clean repos:
  - `kdz:/root/luajit2-s390x/perf-clean-20260330/repo`
    - machine type `8561` (`z15`)
  - `zkd0:/root/luajit2-s390x/perf-clean-20260330/repo`
    - machine type `3906` (`z14`)
  - that host model capture now travels with the frozen baseline so future
    32-bit vs 64-bit accumulator experiments are not read without hardware
    context

- Fresh baseline restamp:
  - `kdz` clean default rebuild stayed green:
    - `jit.status() => true fold cse`
    - `/tmp/oneshot_iter.lua 20 => RESULT 500`
    - `/tmp/oneshot_iter.lua 2000 => RESULT 50000`
    - `/tmp/oneshot_iter.lua 200000 => RESULT 5000000`
    - `-joff /tmp/oneshot_iter.lua 200000 => RESULT 5000000`
    - pinned `iterator_table.lua`:
      - `pairs_sum/hot median=0.059818`
      - `pairs_array_sum/hot median=0.061622`
  - `zkd0` clean default rebuild stayed green:
    - `jit.status() => true fold cse`
    - `/tmp/oneshot_iter.lua 200000 => RESULT 5000000`
    - `-joff /tmp/oneshot_iter.lua 200000 => RESULT 5000000`
    - pinned `iterator_table.lua`:
      - `pairs_sum/hot median=0.093881`
      - `pairs_array_sum/hot median=0.087945`

- Fresh low-noise owner restamp on `kdz`:
  - value-only hash is still:
    - shared `addov_rr_int_eq`
    - carried-total `SLOAD #3`
    - hidden `KEYINDEX SLOAD #10 TK`
    - no extra visible value-lane `SLOAD`
  - key-using hash is still:
    - shared `addov_rr_int_eq`
    - carried-total `SLOAD #3`
    - hidden `KEYINDEX SLOAD #10 TK`
    - visible key/type `SLOAD #11`
  - array value-only control is still:
    - shared `addov_rr_int_eq`
    - carried-total `SLOAD #3`
    - numeric-key control loads `#10` and `#9`

- Minimal proof that hash root still does not frame-source the visible value:
  - raw `-jdump` on `/tmp/hash_value.lua` still shows:
    - `int SLOAD #10 TK`
    - `int VLOAD #0`
    - `int SLOAD #3 T`
    - `int ADDOV`
  - there is no third frame `SLOAD` for the helper-returned visible value lane

- Narrow backend classifier result:
  - the surviving hot hash non-value cluster is not a plain load +
    compare + branch triplet
  - in `asm_sload()` the current `sload_keyindex` / `sload_type` lowering is:
    - load tag word
    - shift to extract tag bits
    - compare against expected tag or type
    - branch on the resulting condition code
  - that means there is no obvious semantic-preserving load-test or
    compare-and-branch fusion target left at the current lowering seam
  - conclusion:
    - stop backend exploration here
    - keep the remaining work on root-trace storage/control ownership only

- Final exact accumulator preload experiment:
  - Goal:
    - satisfy the last allowed accumulator-family condition by making the
      original carried total slot be born as `num` before `loop_unroll()`
      sees it
  - Exact cut:
    - in `rec_itern()`, when the payload is the exact
      `total = total + iterator_value` form, preload the accumulator slot as a
      `num` `SLOAD`
    - add the minimal `IRSLOAD_CONVERT` support in `asm_sload()` for that
      `num-from-int` slot load
  - Correctness:
    - stayed green on `kdz` for:
      - `RESULT 5000000`
      - `HASH_VALUE 3000`
      - `HASH_KEY 1320`
      - `ARRAY_VALUE 3000`
  - First perf passes:
    - looked promising:
      - `pairs_sum/hot median=0.055187`
      - `pairs_array_sum/hot median=0.060284`
    - rerun:
      - `pairs_sum/hot median=0.056218`
      - `pairs_array_sum/hot median=0.062896`
  - But the stop-rule structural proof failed immediately:
    - raw `-jdump` still showed the old root trace:
      - `int SLOAD #3`
      - `int ADDOV`
    - low-noise `SLOAD`/`ADD` logs were also unchanged:
      - `S390X_SLOAD curins=8 ref=8 op1=3 ... type=19 op2=0x4`
      - `S390X_ADD kind=addov_rr_int_eq`
    - there was no real pre-unroll `num` carried slot, so the intended
      `int.num` back-edge removal never even became live
  - Conclusion:
    - reject immediately
    - the small perf movement is non-causal because the structural gate failed
    - the accumulator-to-`num` family is exhausted again on the current tree

- Updated freeze-point decision:
  - Lane A plus the four-piece Lane B recorder baseline remains the branch
    freeze point
  - there is no remaining justified accumulator-family pass from the current
    mechanism
  - only reopen if a future cut can prove the original carried slot becomes
    `num` before `loop_unroll()` sees it, not merely later in the trace

## Timestamped Notes

- Timestamp: `2026-03-31 09:12:31 PDT`
- The branch is now being operated from a frozen implementation baseline:
  - Lane A is the shipping build and stability floor
  - Lane B is the shipping four-piece recorder-side iterator baseline
  - Lane C remains parked
- Fresh freeze-point restamp:
  - `kdz` machine type `8561` (`z15`)
    - `pairs_sum/hot median=0.059818`
    - `pairs_array_sum/hot median=0.061622`
  - `zkd0` machine type `3906` (`z14`)
    - `pairs_sum/hot median=0.093881`
    - `pairs_array_sum/hot median=0.087945`
- Current owner map on the frozen baseline:
  - shared `addov_rr_int_eq` remains the dominant cross-family payer
  - value-only hash still pays hidden `KEYINDEX` plus carried-total `SLOAD`
  - key-using hash adds visible key/type `SLOAD`
  - array still pays numeric-key control loads
  - hash does not frame-source the visible value lane
- Closure result:
  - the last allowed accumulator-family pass failed its structural gate
  - the one allowed narrow backend classifier was also negative
  - no justified follow-up is open from those two families
- Operating rule from here:
  - do not reopen perf work unless a genuinely new root-trace
    storage/control materialization target is identified and can be proven
    against the frozen `kdz` baseline

- Timestamp: `2026-03-31 10:14:33 PDT`
- Source cleanup only:
  - removed the parked root-resume and pre-call-key scaffolding from
    [src/lj_jit.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_jit.h),
    [src/lj_snap.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_snap.c),
    [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c),
    and [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c)
  - intended effect: make the active source match the documented Lane A/Lane B
    freeze point more closely by removing dormant Lane C root-resume hooks
  - local host build still succeeds after the cleanup
  - no new s390x perf claim is attached to this change
  - native `kdz` / `zkd0` validation is still pending for this slice

- Timestamp: `2026-03-31 10:44:29 PDT`
- A checked-in iterator restamp helper now exists:
  - [tools/s390x/restamp_iterator_perf.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/restamp_iterator_perf.py)
  - it syncs tracked files only into the clean native repo, rebuilds in
    `src/`, runs both `jit.on` and `-joff` on
    [tests/s390x/perf/iterator_table.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/iterator_table.lua),
    and captures raw micro, owner-log, and IR artifacts
- The first helper-driven post-cleanup restamp forced one real branch-tip fix:
  - [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c)
    still referenced `oldpc` in the payload-path stop log after the cleanup
    removed the now-unused bridge-only branch around it
  - restoring the local `oldpc` and dropping the dead `entrypc` local made the
    clean native rebuild work again on `kdz`
- Fresh post-cleanup restamp from the current branch tip:
  - `kdz` machine type `8561` (`z15`)
    - `pairs_sum/hot median=0.061851`
    - `pairs_array_sum/hot median=0.063845`
  - `zkd0` machine type `3906` (`z14`)
    - `pairs_sum/hot median=0.156370`
    - `pairs_array_sum/hot median=0.154843`
- Relative to the earlier freeze-point reference:
  - `kdz` is `+3.40%` slower on hash and `+3.61%` slower on array
  - `zkd0` is `+66.56%` slower on hash and `+76.07%` slower on array
- The refreshed owner map did not expose a new target:
  - value-only hash still shows hidden `KEYINDEX`, carried-total `SLOAD`, and
    helper `VLOAD #0`
  - key-using hash still adds visible key/type `SLOAD`
  - array still carries numeric-key control loads
  - shared `addov_rr_int_eq` remains dominant
- Immediate operating rule:
  - do not open a new perf patch family from this restamp alone
  - either explain the post-cleanup drift first or identify a genuinely new
    root-trace storage/control materialization target outside the reject pile

- Timestamp: `2026-03-31 11:35:00 PDT`
- A frozen-baseline checkpoint branch now exists:
  - `k8ika0s/s390x-jit-on-freeze-20260331`
- A checked-in truth-pack helper now exists:
  - [tools/s390x/build_iterator_truth_pack.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/build_iterator_truth_pack.py)
  - it reuses the tracked-files-only sync and direct `src/` rebuild path from
    the restamp helper, then adds focused hot medians, `-jdump=im` IR+mcode,
    low-noise owner logs, `jit.attach("trace")` / `jit.attach("texit")`
    counts after warmup, and `perf stat` capture when available
- Fresh frozen-baseline truth pack on `kdz`:
  - machine type `8561` (`z15`)
  - `pairs_sum/hot median=0.060779`
  - `pairs_array_sum/hot median=0.066737`
  - `-joff pairs_sum/hot median=0.005574`
  - `-joff pairs_array_sum/hot median=0.004126`
- Fresh frozen-baseline minimum screen on `zkd0`:
  - machine type `3906` (`z14`)
  - `pairs_sum/hot median=0.132097`
  - `pairs_array_sum/hot median=0.124149`
- The new decisive result is runtime shape, not the exact median twitch:
  - value-only hash after warmup:
    - `TRACE_START 10`
    - `TRACE_ABORT 9`
    - `TEXIT_COUNT 960000`
  - key-using hash after warmup:
    - `TRACE_START 10`
    - `TRACE_ABORT 9`
    - `TEXIT_COUNT 640000`
  - array value-only control after warmup:
    - `TRACE_START 12`
    - `TRACE_ABORT 10`
    - `TEXIT_COUNT 960000`
- Decision:
  - steady-state exits are still materially nonzero on the frozen baseline
  - the next justified target is not “compiled throughput only”
  - the next justified target is the exact steady-state exit / side-trace
    ownership site, starting with value-only hash
  - do not reopen bridge work, no-guard ideas, hidden-control carry, or late
    backend micro-surgery from this result

- Timestamp: `2026-03-31 11:48:00 PDT`
- Focused follow-up on the frozen `kdz` baseline after the truth pack:
  - a post-warmup `jit.dump` `texit` probe on value-only hash shows repeated
    `TRACE 1 exit 1`
  - the matching `jit.dump=is` root trace still records:
    - hidden `KEYINDEX SLOAD #10`
    - helper `CALLL lj_vm_next (0002 0003)`
    - helper `VLOAD #0`
    - carried-total `SLOAD #3`
    - `ADDOV`
  - snapshot ordering on that same root trace is:
    - `SNAP #0`
    - `SNAP #1`
    - hidden `KEYINDEX` / helper-call path
    - `SNAP #2`
    - carried-total `SLOAD`
    - `ADDOV`
  - inference:
    - the repeated `TRACE 1 exit 1` seam is in the early root snapshot region,
      ahead of the visible value-lane add path
    - the next justified target is therefore early hidden-control /
      root-ownership around `KEYINDEX` / `lj_vm_next`, not the visible value
      lane and not late backend lowering

- Timestamp: `2026-03-31 12:02:00 PDT`
- Truth-pack helper now records per-trace and per-exit histograms in the
  focused warmup-after capture:
  - [tools/s390x/build_iterator_truth_pack.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/build_iterator_truth_pack.py)
- Refreshed histogram run on `kdz`:
  - value-only hash:
    - `trace histogram abort:3=9,start:2=1,start:3=9,stop:2=1`
    - `texit histogram 1:1=960000`
  - key-using hash:
    - `trace histogram abort:3=9,start:2=1,start:3=9,stop:2=1`
    - `texit histogram 1:1=640000`
  - array value-only control:
    - `trace histogram abort:6=10,start:5=1,start:6=10,stop:5=1`
    - `texit histogram 4:1=87,5:1=959913`
- Focused follow-up classification on `kdz`:
  - value-only hash `trace 2` exists, but `traceinfo(2)` reports:
    - `linktype=stitch`
    - `nins=9`
    - `nexit=2`
  - after that stitch trace exists, a dump started in the measured phase shows:
    - repeated `TRACE 1 exit 1`
    - then `TRACE 3 start ...`
    - then `TRACE 3 abort ... -- inner loop in root trace`
- Decision:
  - the next target is not generic “exit 1 is hot”
  - the next target is why hash `exit 1` remains root-owned while array
    `exit 1` promotes to a live side trace

- Timestamp: `2026-03-31 12:38:00 PDT`
- Focused `kdz` classifier against the frozen baseline:
  - hash `trace 2` body can now be captured directly under `jit.dump`
  - without any descendant widening, the key shape is:
    - `trace 1`: loop root with hidden `KEYINDEX`, `CALLL lj_vm_next(tab, frame_keyindex)`,
      helper `VLOAD #0`, carried-total `SLOAD #3`, `ADDOV`
    - `trace 2`: loop trace on the same body
    - `trace 3`: repeated `2/1` aborts as `leaving loop in root trace`
  - array captured in the same style differs immediately:
    - its loop trace already carries the helper-result successor index in the
      second `lj_vm_next()` argument
    - later `exit 1` children do record and stop successfully
- Recorder-policy classifier:
  - [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c)
    explicitly widens iterator payload descendants for numeric-key iterators,
    but not for hash iterators
  - with `LUAJIT_S390X_ALLOW_ITER_DESC=1` on `kdz`, hash immediately stops
    failing the old `2/1` seam on `LJ_TRERR_LLEAVE`
  - resulting trace family:
    - `trace 1`: loop
    - `trace 2`: loop
    - `trace 3`, `4`, `5`, `7`, `8`, ...: `linktype=root`, tiny clones that
      all stop back to `1`
    - `trace 6`, `10`, ...: nil-path roots that also stop back to `1`
  - contrast against the array capture in the same style:
    - array child roots stop back to `2`, its loop owner
    - hash child roots stop back to `1`, the original root owner
- Decision:
  - the current hash/array ownership split is partly recorder policy, not
    purely accidental runtime behavior
  - but simply lifting descendant suppression is not a win mechanism:
    - it creates a root-ladder of the same expensive body
    - it does not produce a materially different steady-state owner or owner
      link
  - next target stays narrow:
    - explain why hash cannot promote into a materially different owner body
      and owner link on `exit 1`, instead of reopening generic descendant
      widening

- Timestamp: `2026-03-31 13:05:00 PDT`
- Source diagnosis for the owner-link seam:
  - the first stop target for iterator descendants is chosen in
    [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c)
    by `rec_loop_jit()`, before the later child-link promotion logic in
    [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c)
    runs
  - specifically:
    - `rec_loop_jit(J, rc, ...)` receives the compiled-loop target as `lnk`
    - it then decides between:
      - `lj_record_stop(..., LJ_TRLINK_LOOP, J->cur.traceno)` for a self-loop
      - `lj_record_stop(..., LJ_TRLINK_ROOT, lnk)` for a root-linked child
  - that means the observed `link=1` vs `link=2` split is not created by the
    later `S390X_ROOT_PROMOTE_CHILD_*` machinery alone; it is already present
    in the initial stop decision
- Focused classifier with `LUAJIT_S390X_ALLOW_ITER_DESC=1`:
  - on a stripped direct probe, both hash and array collapse to the same shape:
    - `trace 1`: loop
    - `trace 2`: loop
    - later descendants: `link=1`, `linktype=root`
  - so descendant permission alone is not enough to reproduce the earlier
    array promoted-owner behavior
- Decision:
  - the next target is now even narrower:
    - explain why the default array path reaches a different `rec_loop_jit()`
      stop target / trace family than hash, rather than treating child-link
      promotion as the first cause

- Timestamp: `2026-03-31 14:02:00 PDT`
- Focused `kdz` first-side ownership classifier:
  - the earlier “array vs hash diverges at `parent=2 exit=1`” read was too
    late
  - the first concrete divergence is already at `trace 1 exit 1`, i.e. the
    first `TRACE 2 start 1/1`
- Direct `ITERN_FOCUS` proof on the same binary:
  - array value-only control:
    - `TRACE 2 start 1/1`
    - `site=after_next ... nextt=19 ... key_nil=0`
    - `site=payload ...`
    - `TRACE 2 stop -> loop`
  - value-only hash:
    - `TRACE 2 start 1/1`
    - `site=after_next ... nextt=4 ... key_nil=1`
    - `site=nil ...`
    - `TRACE 2 abort ... leaving loop in root trace`
  - key-using hash:
    - same first-side outcome as value-only hash
    - `TRACE 2 start 1/1`
    - `site=after_next ... nextt=4 ... key_nil=1`
    - `site=nil ...`
    - `TRACE 2 abort ... leaving loop in root trace`
- Meaning:
  - on the first side seam, hash is still deciding payload vs nil from visible
    key state, not purely from helper-result non-nil status
  - array escapes because its numeric visible key is already present
  - hash falls into the nil-descendant path because the lazy visible-key
    policy leaves `ix.key` unloaded there even when `nextt` is non-nil
  - key-using hash confirms the decision happens before loop-body key demand can
    force visible-key materialization
- Decision:
  - the next exact target is now:
    - determine whether there is any semantic-preserving first-side ownership
      cut at `trace 1 exit 1` that does not just re-open the already rejected
      lazy-key override family
  - do not treat this as permission to revive the earlier global
    `rec_itern()` payload-vs-nil overrides; those were already tested and
    rejected against pinned `kdz`

- Timestamp: `2026-03-31 14:26:00 PDT`
- First-side-only real hash key materialization classifier:
  - implementation shape:
    - keep the root-path hash lazy-key policy unchanged
    - only on non-array `parent == root`, `exit == 1`, materialize the real
      visible key in `lj_record_next()` instead of leaving `ix.key = 0`
    - intended scope: the first `TRACE 2 start 1/1` hash seam only
- Structural result on `kdz`:
  - value-only hash:
    - `site=after_next ... key_nil=0`
    - `site=payload`
    - `TRACE 2 stop -> loop`
  - key-using hash:
    - same first-side result
    - `site=after_next ... key_nil=0`
    - `site=payload`
    - `TRACE 2 stop -> loop`
  - this proves the seam-local cut is genuinely narrower than the earlier
    rejected global payload-vs-nil overrides
- Pinned `kdz` perf result:
  - `pairs_sum/hot median=0.068724`
  - `pairs_array_sum/hot median=0.071057`
- Steady-state ownership follow-up on the same classifier:
  - value-only hash:
    - `TRACE_HIST abort:3=9,start:2=1,start:3=9,stop:2=1`
    - `TEXIT_HIST 1:1=960000`
  - key-using hash:
    - `TRACE_HIST abort:3=9,start:3=9`
    - `TEXIT_HIST 1:1=640000`
- Decision:
  - reject
  - the structural fix is real, but it is not promotable because it loses
    badly against the frozen `kdz` baseline
  - implication:
    - first-side hash ownership is part of the mechanism
    - but fixing it alone is still not enough to recover native JIT-on
      iterator performance
    - more specifically, steady-state ownership still stays on root `1:1`
      even after the first side loop can be recorded
    - and the ownership transfer path is narrower than it first looked:
      - the child-owner machinery is still opt-in
      - but on the real frozen `hotexit=200` surface, array `trace=2` does
        reach `LJ_TRLINK_LOOP` and `S390X_ROOT_PROMOTE_CHILD`
      - the remaining blocker is later:
        - root owner selection knows `target_exec=2`
        - but the root iterator trace still has `target_resumevalid=0`
        - so `JLOOP_EXIT` falls back to `dispatch-original` and the promoted
          child is not used as a direct execution owner

- Timestamp: `2026-03-31 15:05:00 PDT`
- Root iterator resume-contract classification:
  - corrected later by the 16:05 PDT contract attempt above
  - keep only the narrower lasting point from this pass:
    - root iterator traces are still special at the recorder boundary via
      `LJ_TRACE_RECORD_1ST` and `rec_itern()` ownership of the first-ins loop
      handoff
    - so any future contract work here is still an end-to-end design problem,
      not a tiny local field fill-in

- Timestamp: `2026-03-31 16:00:38 PDT`
- Non-resume owner-selection truth-pack pass on the frozen baseline:
  - validation surfaces:
    - `kdz` truth pack:
      - `pairs_sum/hot median=0.066259`
      - `pairs_array_sum/hot median=0.069155`
      - `TRACE_START/TRACE_ABORT/TEXIT_COUNT`
        - hash value: `10 / 9 / 960000`
        - hash key: `10 / 9 / 640000`
        - array value: `11 / 10 / 960000`
    - `zkd0` minimal screen:
      - `pairs_sum/hot median=0.104839`
      - `pairs_array_sum/hot median=0.097884`
  - helper update:
    - [tools/s390x/build_iterator_truth_pack.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/build_iterator_truth_pack.py)
      now has a checked-in smaller owner-selection probe path
    - reason:
      - the first owner-selection logging attempt used the full `80000`
        trace-count scripts and produced gigabyte-scale logs
      - the helper now uses short finite probe scripts for this surface
  - exact non-resume owner-selection read on `kdz`:
    - value-only hash:
      - root `trace 1`:
        - `link=1`
        - `linktype=2`
        - `startop=70`
      - hot seam:
        - repeated `S390X_JLOOP_EXIT phase=dispatch-original parent=1 exit=1 trace=1`
      - first materially different candidate:
        - `trace 2` starts as root with `startop=79`
        - dies immediately at `S390X_LINNER site=rec_loop_jit_root`
        - aborts with `err=9`
    - key-using hash:
      - same owner-selection outcome as value-only hash
      - first candidate dies in `rec_loop_jit_root` before child-link/runtime
        ownership matters
    - array value-only control:
      - root `trace 1` also spends the early seam in `dispatch-original`
      - first side trace:
        - `trace 2 parent=1 exit=1 root=1 startop=88`
        - repeated nil-path aborts with `err=8`
        - eventual stop:
          - `linktype=6`
          - `link=0`
          - `root=1`
      - later descendants:
        - `trace 3`, `trace 4`, `trace 6` stop as:
          - `linktype=1`
          - `link=1`
          - `root=1`
  - decision:
    - close the root-`ITERN` contract family again on the current mechanism
    - the next open seam is non-resume owner selection only
    - hash dies too early, in
      [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c)
      inside `rec_loop_jit()`
    - array survives farther, but still first lands in interpreter/root-linked
      ownership rather than a stable non-root owner
    - next valid target:
      - one narrow non-resume owner-selection cut only if it can move hash
        past `rec_loop_jit_root` into a materially different owner shape
      - otherwise stop reopening this family

- Timestamp: `2026-03-31 16:05:00 PDT`
- Env-gated root-`ITERN` resume contract attempt:
  - tested under:
    - `LUAJIT_S390X_ROOT_ITERN_RESUME_CONTRACT=1`
    - `LUAJIT_S390X_ROOT_PROMOTE_CHILD_LOOP=1`
    - `LUAJIT_S390X_ROOT_JLOOP_CHILD=1`
  - structural facts on clean `kdz`:
    - root `trace 1` does arm:
      - `S390X_ROOT_ITERN_RESUME_ARM trace=1 ... resumevalid=1`
    - `trace 1 exit 1` still spends a long stretch in:
      - `S390X_JLOOP_EXIT phase=dispatch-original parent=1 exit=1 trace=1`
    - child promotion still happens later:
      - `S390X_ROOT_PROMOTE_CHILD trace=2 root=1 ... newtarget=2`
    - after promotion, execution does not settle into a direct child handoff
    - it falls into a growing `exit 1` ladder of `BC_JMP` descendants:
      - `trace 3`, `trace 4`, ... `trace 103+`
    - the finite array probe then crashes on `kdz`
  - decision:
    - reject at the structural gate
    - the contract is real enough to arm the root, but it does not create a
      safe stable owner handoff
    - this family is still not promotable
  - correction to the prior read:
    - the runtime did already have a dormant root iterator resume consumer:
      - `retop == BC_ITERN && targetT->root == 0 && targetT->resumevalid`
    - so the failure is not “missing consumer”
    - the failure is the end-to-end contract shape itself

- Timestamp: `2026-03-31 16:41:00 PDT`
- Corrected finite owner-selection rerun on `kdz` after the helper fix:
  - purpose:
    - verify that the checked-in smaller owner-selection probe is capturing the
      intended recorder/runtime seam rather than the old smoke outputs
  - fresh corrected artifacts:
    - value-only hash:
      - [hash_value.stdout.log](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260331-kdz-nonresume-owner-selection-v3/raw/owner-selection/hash_value.stdout.log)
      - [hash_value.stderr.log](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260331-kdz-nonresume-owner-selection-v3/raw/owner-selection/hash_value.stderr.log)
    - key-using hash:
      - [hash_key.stdout.log](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260331-kdz-nonresume-owner-selection-v3/raw/owner-selection/hash_key.stdout.log)
      - [hash_key.stderr.log](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260331-kdz-nonresume-owner-selection-v3/raw/owner-selection/hash_key.stderr.log)
  - focused results:
    - value-only hash:
      - `RESULT 37500`
      - `TRACE_START 3`
      - `TRACE_STOP 1`
      - `TRACE_ABORT 2`
      - `TEXIT_COUNT 3000`
    - key-using hash:
      - `RESULT 16500`
      - `TRACE_START 3`
      - `TRACE_STOP 1`
      - `TRACE_ABORT 2`
      - `TEXIT_COUNT 2000`
  - exact seam read from the corrected rerun:
    - both hash loops share the same first-side mechanism
    - root `trace 1` still starts at `ITERN` (`startop=70`) and stops as a
      loop
    - hot steady-state still spends `trace 1 exit 1` in
      `S390X_JLOOP_EXIT phase=dispatch-original`
    - the first fresh root candidate is still:
      - `trace 2 startop=79`
      - `S390X_LINNER site=rec_loop_jit_root`
      - `S390X_TRACE_ABORT ... err=9`
    - the separate side attempts still show:
      - `TRACE 2 start 1/1`
      - `abort ... leaving loop in root trace`
  - decision:
    - this corrected rerun does not expose a new non-resume owner family
    - it re-shows the same first-side nil-descendant / unloaded-visible-key
      seam already found by the earlier focused `ITERN_FOCUS` probes
    - therefore the next valid cut, if any, must be demonstrably different
      from the already rejected first-side lazy-key classifiers
    - otherwise this family should be closed again

- Timestamp: `2026-03-31 17:05:15 PDT`
- Three-track closure on the first-side owner seam:
  - inputs:
    - exact first-side owner-seam forensics against the frozen baseline
    - mature-control structural diff against the recorded x86_64 control note
    - microarchitecture gatekeeper filter against the current reject pile
  - converged result:
    - the first differing decision is still in `rec_itern()`, after
      `lj_record_next()` already has the helper result
    - array reaches the payload path because `lj_record_next()` synthesizes a
      visible numeric key from the successor index
    - non-array/hash reaches the nil path because the visible key is
      intentionally left unloaded
    - `rec_itern()` immediately forks on `if (!tref_isnil(ix.key))`, so the
      first-side hash failure is still the same payload-vs-nil /
      unloaded-visible-key family
  - supporting code points:
    - non-array/hash leaves `ix->key = 0` on success:
      - [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c#L2567)
    - array synthesizes the visible key from the successor index:
      - [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c#L2573)
    - `rec_itern()` payload-vs-nil fork:
      - [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c#L1402)
      - [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c#L1476)
  - mature-control read:
    - the first real divergence is still payload-vs-nil, not a later
      stop-target or runtime-owner adoption difference
  - gatekeeper read:
    - this is not a new owner-selection target
    - it is the already rejected first-side lazy-key family in different
      clothing
  - decision:
    - close the first-side owner-selection family again on the current tree
    - do not code another override here unless a future cut is demonstrably
      different from the rejected first-side lazy-key classifiers

- Timestamp: `2026-03-31 17:46:25 PDT`
- Four-track frozen-baseline iterator restamp:
  - scope:
    - Track 1 exact `trace 1 exit 1` seam attribution
    - Track 2 `rec_loop_jit_root` stop-target autopsy
    - Track 3 exit-cost vs compiled-body-cost attribution
    - Track 4 ABI-aware preserved-GPR opportunity audit
  - authoritative `kdz` bundle:
    - [summary.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260331-kdz-frozen-baseline-v3/summary.md)
  - `kdz` frozen-baseline medians:
    - `pairs_sum/hot 0.062519`
    - `pairs_array_sum/hot 0.066353`
  - focused same-harness `-joff` gaps:
    - `hash_value 0.061603` vs `0.005682` (`10.84x`)
    - `hash_key 0.045652` vs `0.003942` (`11.58x`)
    - `array_value 0.064249` vs `0.004124` (`15.58x`)
  - Track 1 result:
    - both hash and array still attribute `trace 1 exit 1` to the first
      loop/leave decision after the helper result exists
    - hash stays on the same closed payload-vs-nil / unloaded-visible-key
      family
  - Track 2 result:
    - `rec_loop_jit_root` is still downstream
    - the `startop=79` root candidate is not the real hash-vs-array split
    - the first materially different candidate is still the first-side
      `startop=88` path that was already closed
  - Track 3 result:
    - `perf stat` is still unavailable on `kdz`:
      - `cycles`, `instructions`, `branches`, `branch-misses`
        all report `<not supported>`
    - runtime fallback now carries the exit/body attribution:
      - `hash_value`: steady exit `1:1`, `64.17ns/texit`
      - `hash_key`: steady exit `1:1`, `71.33ns/texit`
      - `array_value`: steady exit `5:1`, `66.93ns/texit`
    - all three focused loops classify as `exit-dominated`
  - Track 4 result:
    - no proven preserved-GPR opportunity on the current tree
    - the surviving loop-carried/control loads are not currently shown to
      exist only because s390x reg-home/liveness drops a value across
      `lj_vm_next(tab, keyindex)`
  - `zkd0` regression screen:
    - [summary.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/restamps/20260331-zkd0-post-tracks-screen/summary.md)
    - `pairs_sum/hot 0.119175` (`+26.94%` vs frozen)
    - `pairs_array_sum/hot 0.120802` (`+37.36%` vs frozen)
  - decision:
    - no new iterator seam was proven outside the reject pile
    - freeze iterator at the current Lane A + Lane B checkpoint
    - move the next queued perf workstream to dispatch/side-exit

- Timestamp: `2026-03-31 18:23:33 PDT`
- Dispatch/side-exit queue, first exact seam attribution:
  - checked in a dispatch truth-pack helper:
    - [tools/s390x/build_dispatch_truth_pack.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/build_dispatch_truth_pack.py)
  - active `kdz` dispatch hot medians from the current branch tip:
    - `numeric_loop/hot 0.642036` vs `-joff 0.002168` (`296.14x`)
    - `side_exit_loop/hot 0.155224` vs `-joff 0.004739` (`32.75x`)
    - `hotexit_loop/hot 0.380652` vs `-joff 0.005653` (`67.34x`)
  - focused runtime read:
    - branch-free `numeric_loop` already reproduces the failure
    - after warmup it still shows:
      - `TRACE_START 10`
      - `TRACE_STOP 10`
      - `TRACE_ABORT 0`
      - `TEXIT_COUNT 2001`
    - `side_exit_loop` shows the same exit-heavy shape:
      - `TRACE_START 11`
      - `TRACE_STOP 11`
      - `TRACE_ABORT 0`
      - `TEXIT_COUNT 2001`
  - exact seam on the focused numeric probe:
    - root `trace 1` starts at `BC_FORL` and stops as a loop
    - the hot seam is `trace 1 exit 0`
    - `trace_hotside()` sees the replay at:
      - `pc = BC_MODVN`
      - `prevop = BC_JFORI`
      - `snappc = BC_MODVN`
      - `parent_startop = BC_FORL`
    - recorder side setup then enters the first side trace as:
      - `parent=1 exit=0`
      - `startop = BC_JMP`
      - `startpc == pc == snappc`
      - `parent_snapnent = 0`
    - after `sidecheck`, that first side trace is still on the same bare
      body-entry state
  - named seam:
    - `loop-body-entry-after-JFORI`
  - classifier result:
    - `LUAJIT_S390X_HOTSIDE_CANON_EQUIV=1` is not a fix
    - on the same numeric probe it collapses observed exit traffic into one
      reused site:
      - `7:0=160743`
    - that is not a real owner/materialization win
  - decision:
    - the active frontier is now generic dispatch `FORL` / `JFORI`
      loop-entry `exit 0`
    - do not reopen iterator-only work from this evidence
    - next exact target is to explain why the first side trace stays a bare
      `BC_JMP` side entry at the same body PC instead of becoming a materially
      different owner

- Timestamp: `2026-03-31 18:44:04 PDT`
- Dispatch/side-exit queue, current exact dispatch read:
  - focused `kdz` root-seam rerun on `numeric_loop` kept the same named seam:
    - `loop-body-entry-after-JFORI`
  - the important new proof is that the first side trace is not missing the
    current side-trace `JFORI` / `FORL` narrow gate:
    - `trace=4 parent=1 exit=0`
    - `prev_is_jfori = 1`
    - `fori_target = 1`
    - `target_match = 1`
    - `site=extra_loop_narrow`
  - that same proof repeats through the whole hot-side ladder:
    - `trace=5 parent=4 exit=0`
    - `trace=6 parent=5 exit=0`
    - ... through later descendants
  - despite the narrow path firing, recorder side setup still leaves each
    trace on the same body-entry state:
    - `pc = BC_MODVN`
    - `prevop = BC_JFORI`
    - `startop = BC_JMP`
    - `after_sidecheck` still unchanged
  - classifier results on the same focused numeric probe:
    - `LUAJIT_S390X_HOTSIDE_CANON_CHILD=1`
      - reduces `TRACE_START` from `10` to `6`
      - leaves `TEXIT_COUNT` at `2001`
      - last trace still absorbs `8:0=858`
      - not enough by itself
    - `LUAJIT_S390X_HOTSIDE_SHARE_EQUIV=1`
      - timed out after `20s` on the `2000`-iteration focused probe
      - produced no result before timeout
      - reject as unsafe from the current seam
  - decision:
    - the live dispatch problem is no longer “failure to qualify for the
      current extra-loop narrow path”
    - the live problem is that the current narrow path still does not produce
      a materially different owner/body shape and still leaves the per-iteration
      `exit 0` ladder in place
    - next exact target is to inspect the side-trace path after
      `rec_for_loop(..., init=1)` and explain why that narrow setup still
      emerges as the same `BC_JMP` body-entry ladder

- Timestamp: `2026-03-31 18:44:04 PDT`
- Dispatch/side-exit queue, corrected owner read from focused `traceinfo`:
  - the same clean `kdz` `numeric_loop` seam now has an explicit trace snapshot:
    - `trace 1`: `link=1`, `linktype=loop`, `nins=18`, `nk=7`, `nexit=4`
    - `trace 2`: `link=1`, `linktype=root`, `nins=4`, `nk=6`, `nexit=3`
    - `trace 3` through `trace 12`: each is `link=self`, `linktype=loop`,
      `nins=18`, `nk=7`, `nexit=4`
    - `trace 13`: `link=0`, `linktype=stitch`, `nins=11`, `nk=19`, `nexit=2`
  - that corrects the previous interpretation:
    - the dispatch descendants are not failing to become loop owners
    - they are already becoming self-loop loop traces
  - combined with the focused recorder logs:
    - `site=extra_loop_narrow` fires at `trace=4 parent=1 exit=0`
    - and then again through later descendants
  - current read:
    - the live dispatch problem is now churn/reuse, not basic owner formation
    - equivalent self-loop loop traces keep getting cloned on the same
      `FORL` / `JFORI` `exit 0` seam instead of reusing a stable earlier owner
  - classifier context:
    - `LUAJIT_S390X_HOTSIDE_CANON_CHILD=1`
      - reduces `TRACE_START` from `10` to `6`
      - leaves `TEXIT_COUNT` at `2001`
      - not enough by itself
    - `LUAJIT_S390X_HOTSIDE_SHARE_EQUIV=1`
      - timed out after `20s` on the focused probe
      - unsafe from the current seam
  - decision:
    - next exact target is `trace_hotside()` equivalence/reuse policy for this
      `exit 0` loop-clone family
    - inspect why default policy keeps cloning equivalent self-loop loop traces
      instead of reusing or adopting an earlier equivalent loop owner

- Timestamp: `2026-03-31 19:22:40 PDT`
- Dispatch/side-exit queue, default hot-side policy is now mechanically pinned:
  - static read from [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c):
    - `trace_hotside()` always computes equivalent candidates for focused logs
    - actual adoption only happens through the env-gated helpers:
      - `LUAJIT_S390X_HOTSIDE_CANON_EQUIV`
      - `LUAJIT_S390X_HOTSIDE_CANON_CHILD`
      - `LUAJIT_S390X_HOTSIDE_SHARE_EQUIV`
    - with those gates off, the default path just increments `snap->count` and
      starts a new side trace once `hotexit` is reached
  - late steady-state proof from the focused `kdz` dispatch artifact:
    - `parent=10 exit=0`
    - `phase=equiv`
    - `cand=6`
    - `child=7`
    - followed immediately by repeated `phase=before ... snapcount=...`
    - and finally `phase=start ... snapcount=200`
  - combined read:
    - the current loop-clone ladder is not because the runtime cannot see an
      equivalent owner
    - it is because default `trace_hotside()` is still choosing “count to
      hotexit and start another trace” on this seam
  - decision:
    - the next exact dispatch target is no longer seam discovery
    - it is one narrow reuse/adoption experiment on this seam only, and it must
      prove a real owner/exit win rather than just collapsing traffic into one
      reused site the way `CANON_EQUIV` already did

- Timestamp: `2026-03-31 19:42:10 PDT`
- Dispatch/side-exit queue, first narrow reuse/adoption experiment was tried:
  - code surface:
    - [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c)
  - env gate:
    - `LUAJIT_S390X_HOTSIDE_REUSE_LOOP_CHILD`
  - exact intended shape:
    - late steady-state `exit 0`
    - self-loop `BC_JMP` parent on the `FORL` / `JFORI` dispatch seam
    - earlier equivalent candidate already visible
    - existing child under that equivalent candidate already visible
  - action:
    - patch the current parent exit directly to the existing child loop target
    - mark the current hot-side count done
    - do not start recording another equivalent side trace
  - why this was different from older classifiers:
    - not `CANON_EQUIV` parent substitution
    - not `CANON_CHILD` reparent-before-record
    - not `SHARE_EQUIV` hotcount transfer
    - direct exit retarget to an already existing equivalent child
  - local validation:
    - local build succeeded
    - local `luajit -e 'print(\"ok\")'` succeeded
    - local hotloop smoke with the gate enabled terminated cleanly
  - clean `kdz` structural gate:
    - focused `numeric_loop_trace.lua`
    - executed under `timeout 20`
    - completed with `REMOTE_RC=124`
    - no trace-count stdout was produced before timeout
  - decision:
    - reject immediately before perf
    - do not keep the gate in the tree
    - from the current seam, direct child-retarget is not safe enough to be a
      live dispatch family

- Timestamp: `2026-03-31 19:45:12 PDT`
- Dispatch/side-exit queue, earlier patch-target classifier is also rejected:
  - exact gate:
    - `LUAJIT_S390X_SIDEEXIT_MCLOOP`
  - exact intended question:
    - whether the loop-clone ladder exists because parent side exits are
      patched to generic trace entry instead of the loop-body target
      (`T->mcloop`)
  - clean `kdz` structural gate:
    - authoritative repo:
      - `kdz:/root/luajit2-s390x/perf-clean-20260330/repo`
    - focused surface:
      - `numeric_loop_trace.lua`
    - result:
      - clean rebuild succeeded
      - the native probe then failed immediately with:
        - `Segmentation fault (core dumped)`
      - no trace-count stdout was produced before the crash
    - artifact bundle:
      - [20260331-kdz-sideexit-mcloop-numeric](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260331-kdz-sideexit-mcloop-numeric)
  - code-level autopsy:
    - [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c)
      can force `trace_stop()` to patch side exits to
      `J->cur.mcode + T->mcloop`
    - [src/lj_asm.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm.c)
      only defines `mcloop` as an internal loop-body entry offset
    - [src/vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc)
      only consumes `mcloop` through owner/resume-gated VM entry paths
    - that makes `mcloop` an internal controlled entry target, not a generic
      safe landing site for arbitrary patched side exits
  - decision:
    - reject `SIDEEXIT_MCLOOP` immediately
    - do not widen to `side_exit_loop` or `hotexit_loop`
    - close the current dispatch loop-clone mechanism:
      - default hot-side policy already sees equivalent candidates
      - direct late child-retarget is unsafe
      - direct `sideexit -> mcloop` patch-target is unsafe
      - no safe earlier patch target was exposed on this mechanism
    - next queued redirect:
      - dispatch-adjacent side-exit cost surfaces outside the loop-clone seam
      - helper-boundary storage/materialization audits where the ABI may help
      - only then broader JIT throughput families

- Timestamp: `2026-03-31 20:08:12 PDT`
- Dispatch/side-exit queue, branchy follow-up surfaces collapse to the same seam:
  - helper/workflow change:
    - [tests/s390x/helpers/testlib.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/helpers/testlib.lua)
      now has lightweight aggregated trace/texit counters
    - [tools/s390x/build_dispatch_truth_pack.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/build_dispatch_truth_pack.py)
      now uses those counters for dispatch trace-count runs so the branchy loops
      can be observed without the earlier capture-overflow failure
  - clean `kdz` rerun:
    - [20260331-kdz-dispatch-truth-pack-v4](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260331-kdz-dispatch-truth-pack-v4)
  - latest clean dispatch medians:
    - `numeric_loop/hot`
      - JIT-on `0.350184`
      - `-joff` `0.002168`
      - ratio `161.52x`
    - `side_exit_loop/hot`
      - JIT-on `0.535201`
      - `-joff` `0.004667`
      - ratio `114.68x`
    - `hotexit_loop/hot`
      - JIT-on `0.622022`
      - `-joff` `0.005627`
      - ratio `110.55x`
  - focused runtime result:
    - all three loops now capture cleanly and come back with the same shape:
      - `TRACE_START 11`
      - `TRACE_ABORT 0`
      - `TEXIT_COUNT 2001`
      - `TEXIT_HIST 10:0=200,11:0=200,12:0=200,13:0=58,1:0=142,2:0=1,4:0=200,5:0=200,6:0=200,7:0=200,8:0=200,9:0=200`
    - all three focused exit logs show the same practical side-entry seam:
      - `pc = BC_MODVN`
      - `prevop = BC_JFORI`
      - `startop = BC_JMP`
      - `site=extra_loop_narrow`
  - decision:
    - the branchy dispatch surfaces do not expose a distinct side-exit payer
      outside the already-closed loop-clone mechanism
    - the current generic dispatch/side-exit line is now closed on this
      mechanism
    - next queued redirect:
      - helper-boundary storage/materialization audits where the ABI may help
      - only then broader JIT throughput families

- Timestamp: `2026-03-31 20:20:45 PDT`
- Helper-boundary follow-up, existing dynamic `HREF` hot-exit surface is structurally green:
  - authoritative clean-host audit:
    - [20260331-kdz-href-helper-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260331-kdz-href-helper-audit)
  - target script:
    - [hotexit_update_preinterned.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/jit_loops/hotexit_update_preinterned.lua)
  - why this was the first helper-boundary follow-up:
    - dynamic `HREF` is still helper-backed on s390x in
      [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h)
    - older native findings showed real hot-exit path sensitivity around the
      helper-backed mixed preinterned update loop
  - clean `kdz` result on the current branch tip:
    - the run terminates cleanly (`rc=0`)
    - it reports the expected converged trace chain:
      - `TRACE_start iter=2 tr=1`
      - `TRACE_stop iter=3 tr=1`
      - `TRACE_start iter=21 tr=2 otr=1 oex=2`
      - `TRACE_stop iter=20 tr=2`
      - `TRACE_start iter=20 tr=3 otr=1 oex=0`
      - `TRACE_stop iter=21 tr=3`
      - `TRACE_start iter=101 tr=4 otr=3 oex=3`
      - `TRACE_stop iter=101 tr=4`
    - final traceinfo also matches the expected converged shape:
      - `tr=1 link=1 type=loop`
      - `tr=2 link=1 type=root`
      - `tr=3 link=3 type=loop`
      - `tr=4 link=0 type=stitch`
    - final result is stable:
      - `done 81 100`
  - decision:
    - the current helper-backed dynamic `HREF` update path is not a newly
      broken family
    - this audit does not expose a new helper-boundary storage/materialization
      seam worth opening
    - queueing rule from here:
      - any new helper-boundary work must name a fresh seam first
      - otherwise move to broader JIT throughput families

- Timestamp: `2026-03-31 21:05:00 PDT`
- Broader-throughput queue is now explicit and has a checked-in restamp path:
  - new helper:
    - [tools/s390x/build_throughput_truth_pack.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/build_throughput_truth_pack.py)
  - current supported families:
    - [tests/s390x/perf/vararg_paths.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/vararg_paths.lua)
    - [tests/s390x/perf/bitops_mix.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/bitops_mix.lua)
  - why this is the next queue:
    - iterator is frozen at the Lane A + Lane B checkpoint
    - the dispatch loop-clone mechanism is closed
    - the first helper-boundary follow-up did not name a new seam
  - target order:
    - `vararg_paths` first because it stresses arg-bank, call, return, and
      `select()` / vararg flow without reopening the closed iterator or
      dispatch families
    - `bitops_mix` second as a helper-light compiled-body control
  - explicit non-target:
    - [tests/s390x/perf/mixed_noffi.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/mixed_noffi.lua)
      is not the next family because its `pairs(map)` loop would drag iterator
      behavior back into a queue that is supposed to sit outside the frozen
      iterator mechanism
  - scope of the new helper:
    - reuses tracked-file sync only
    - reuses direct `src/` rebuild only
    - captures full-family JIT-on and `-joff` medians
    - captures focused hot-only medians, trace/texit counts, and `perf stat`
      when available
  - current status:
    - helper added locally and validated for syntax/smoke only
    - no new authoritative `kdz` or `zkd0` family restamp is claimed yet from
      this note

- Timestamp: `2026-03-31 22:05:00 PDT`
- First broader-throughput `kdz` pass names the next live family: traced hot vararg loops
  - first native queue target:
    - [tests/s390x/perf/vararg_paths.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/vararg_paths.lua)
  - artifact root:
    - [20260331-kdz-vararg_paths-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260331-kdz-vararg_paths-truth-pack)
  - authoritative clean-host raw medians already captured:
    - `sum_loop/hot`
      - JIT-on `1.109134`
      - `-joff` `0.004543`
      - about `244.14x` slower with JIT on
    - `retlast_loop/hot`
      - JIT-on `0.029367`
      - `-joff` `0.001993`
      - about `14.74x` slower with JIT on
    - `retconst_loop/hot`
      - JIT-on `0.028397`
      - `-joff` `0.000561`
      - about `50.62x` slower with JIT on
  - focused hot-only medians keep the same ordering:
    - `sum_loop/hot`
      - JIT-on `0.459993`
      - `-joff` `0.004453`
      - about `103.30x`
    - `retlast_loop/hot`
      - JIT-on `0.029780`
      - `-joff` `0.002047`
      - about `14.55x`
    - `retconst_loop/hot`
      - JIT-on `0.027820`
      - `-joff` `0.000574`
      - about `48.47x`
  - clean `kdz` smoke is not the issue:
    - plain remote per-case checks still return the expected values
      - `SUM_LOOP 279`
      - `RETLAST_LOOP 159`
      - `RETCONST_LOOP 840`
  - the first follow-up structural read is narrower:
    - the full-family and focused median passes complete
    - but the focused per-workload hot trace-count path is not yet stable on
      this family and was hanging the helper before timeout hardening
    - the checked-in broader-throughput helper now wraps those per-workload
      probes in a fixed timeout instead of wedging the whole run
  - decision:
    - do not widen to `bitops_mix` yet
    - the next live target is traced hot vararg loop behavior, starting with
      `sum_loop`
    - this is now the front-most broader-throughput family because it is
      dramatically redder than the other two vararg paths and is cleanly
      outside the closed iterator and dispatch mechanisms

- Timestamp: `2026-03-31 22:20:00 PDT`
- Next exact task queue inside the new vararg family is now fixed:
  1. isolate traced hot `sum_loop` on clean `kdz`
  2. compare it directly against `retlast_loop` and `retconst_loop`
  3. name which payer class is actually extra on `sum_loop`:
     - repeated `select()` control
     - vararg value access/materialization
     - or exit-heavy traced hot flow
  - queue rule:
    - do not widen to `bitops_mix` until one of those three is named first

- Timestamp: `2026-03-31 22:45:00 PDT`
- First structural split inside the new vararg family is now pinned on `kdz`
  - local `-jdump=im` compare:
    - `sum_loop` traces the inner `sum(...)` vararg scan loop directly
    - `retlast_loop` and `retconst_loop` do not; they stay on the simpler
      caller loop shape
  - clean `kdz` `-jv` compare:
    - `sum_loop`
      - starts with an inner loop trace in `sum(...)`
      - then adds caller-side handoff traces back into the outer loop
      - then keeps cloning the inner loop seam
    - `retlast_loop`
      - shows the base loop-clone pattern only
    - `retconst_loop`
      - also shows the base loop-clone pattern only
  - decision:
    - the already-shared loop-clone behavior is not enough by itself to
      explain the `sum_loop` cliff
    - the new live seam is the nested vararg summation path plus caller
      return/handoff around `sum(...)`
    - next exact target:
      - explain why the `sum(...)` inner vararg loop plus outer caller handoff
        forms that extra ladder on `kdz`

- Timestamp: `2026-03-31 23:35:00 PDT`
- Reduced clean-host handoff probes narrowed the vararg seam again
  - artifact bundle:
    - [20260331-kdz-vararg-handoff-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260331-kdz-vararg-handoff-audit)
  - reduced `kdz` probes run with:
    - `jit.opt.start("hotloop=1")`
    - `LUAJIT_S390X_RECRET_LOG=1`
    - `-jv`
    - `n=2000`
  - `sum_loop`
    - still starts with the inner callee loop
    - then forms a separate caller handoff trace family:
      - `TRACE 2 ... -> 1`
      - later `TRACE 7 (2/0) ... -> 1`
    - `lj_record_ret()` logs on those traces show:
      - `site=lua_intrace_return`
      - no `site=lua_lower_frame_retf`
      - no `site=lua_root_lower_frame_lleave`
    - the `TRACE 2` dump is dominated by callee re-entry setup:
      - caller-side `tobit`
      - guard on the `sum` function object
      - fresh `select` env lookup / identity guard
      - then stop `-> 1`
  - `retlast_loop`
    - shows the shared base loop-clone ladder only
    - repeated return logging is still `site=lua_intrace_return`
    - no separate handoff-family root is formed
  - `retconst_loop`
    - shows the same shared base loop-clone ladder only
    - repeated return logging is also `site=lua_intrace_return`
    - no lower-frame return path appears here either
  - decision:
    - the extra `sum_loop` red is not a generic lower-frame return bug
    - the live seam is now caller/callee handoff around a traced Lua callee
      loop, layered over the already-known base loop-clone pattern
    - next exact target:
      - explain why that caller handoff path keeps materializing as a second
        trace family instead of staying inside one stable owner family

- Timestamp: `2026-03-31 23:58:00 PDT`
- Focused `rec_func_jit()` classifier came back negative on clean `kdz`
  - artifact bundle:
    - [20260331-kdz-vararg-funcjit-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260331-kdz-vararg-funcjit-audit)
  - one new focused log gate was added:
    - `LUAJIT_S390X_FUNCJIT_LOG`
    - it logs entry / continue / stop decisions in `rec_func_jit()`
  - clean `kdz` reduced probes for:
    - `sum_loop`
    - `retlast_loop`
  - result:
    - no `S390X_FUNCJIT` lines at all on either reduced probe
    - `sum_loop` still shows:
      - `TRACE 2 ... -> 1`
      - later `TRACE 7 (2/0) ... -> 1`
      - repeated `site=lua_intrace_return`
    - `retlast_loop` still shows only the shared loop-clone ladder
  - decision:
    - the extra `sum_loop` handoff family is not being born at
      `rec_func_jit()` / compiled-callee entry
    - the next exact target moves later:
      - caller-side re-entry after `lua_intrace_return`
      - before that path settles into the separate caller handoff family

- Timestamp: `2026-04-01 00:36:00 PDT`
- Reduced traceinfo and call-handoff classifiers narrowed the vararg seam again
  - focused artifact bundles:
    - [20260331-kdz-vararg-rootstart-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260331-kdz-vararg-rootstart-audit)
    - [20260331-kdz-vararg-callhandoff-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260331-kdz-vararg-callhandoff-audit)
  - reduced traceinfo read on clean `kdz`:
    - `sum_loop`
      - `trace 1`: `link=1`, `type=loop`, callee vararg scan loop in `sum(...)`
      - `trace 2`: `link=1`, `type=root`, caller-side root trace
      - `trace 7`: `link=1`, `type=root`, later caller-side root trace in the
        same family
    - `retlast_loop`
      - `trace 1` through `trace 10`: caller loop family only
      - later `trace 11` and `trace 12`: stitch traces for the reporting tail,
        not the unique hot-path payer
  - important contrast:
    - `sum_loop` uniquely creates fresh caller root-family traces after the
      callee loop already exists
    - `retlast_loop` does not
  - one more focused negative classifier is now closed:
    - a synced-and-rebuilt `LUAJIT_S390X_CALLHANDOFF_LOG` pass on clean `kdz`
      stayed completely silent
    - `sum_loop` still formed:
      - `TRACE 2 ... -> 1`
      - `TRACE 7 (2/0) ... -> 1`
    - so those extra caller-family roots are not being born in the generic
      `trace_stop(... BC_CALL/BC_CALLM/BC_ITERC ...)` plus `lj_trace_stitch()`
      handoff path
  - decision:
    - the live vararg seam moves earlier again
    - the next exact target is recorder-side root-link selection after
      `lua_intrace_return`, before generic stitch machinery matters

- Timestamp: `2026-04-01 01:08:00 PDT`
- Corrected clean-host trace-start classifier changed the vararg read again
  - focused artifact bundle:
    - [20260331-kdz-vararg-tracestart-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260331-kdz-vararg-tracestart-audit)
  - correction:
    - the earlier start-log pass used the wrong env name
    - the actual gate is `LUAJIT_S390X_TRACE_START_LOG`
  - clean `kdz` result with the corrected gate:
    - `sum_loop`
      - `S390X_TRACE_START ... op=79 parent=0 exit=0` at the callee loop
      - then a second `S390X_TRACE_START ... op=79 parent=0 exit=0` at the
        caller site before `TRACE 2`
      - so `TRACE 2` is a normal second root trace started by hotcount, not a
        hidden post-return root-link creation
    - `retlast_loop`
      - only one root start appears in the reduced probe
      - that root starts directly at the caller site
      - later traces are then side growth from that caller root
  - implication:
    - `sum_loop` is structurally split across two independently hot root sites:
      - callee vararg scan loop first
      - then caller arithmetic/call site
    - the generic call/stitch path is still not the birth point of the extra
      caller family
  - decision:
    - the live target is no longer “who creates the extra caller root?”
    - the live target is:
      - why the caller root in `sum_loop` stops `-> 1` and keeps feeding the
        inner-loop ladder instead of converging into the stable caller-loop
        family shape seen in `retlast_loop`

- Timestamp: `2026-04-01 01:20:00 PDT`
- Reduced caller-root dumps make the vararg stop-point explicit
  - focused artifact bundle:
    - [20260331-kdz-vararg-rootdump-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260331-kdz-vararg-rootdump-audit)
  - `retlast_loop`
    - caller root (`TRACE 1`) already contains:
      - the caller add
      - the outer-loop increment/check
      - outer-loop PHIs
    - it stops as a loop immediately
  - `sum_loop`
    - callee loop (`TRACE 1`) is separate
    - caller root (`TRACE 2`) contains:
      - caller-side `tobit` setup
      - callee function identity guard
      - `select` env / identity guards
    - but it does not yet contain:
      - the caller add of callee result into the outer total
      - the outer-loop carried total / PHIs
    - it stops `-> 1` before the caller body becomes a real loop owner
  - important contrast:
    - the live seam is not generic “two roots are bad”
    - it is specifically that traced-callee return to caller in `sum_loop`
      stops before caller-body materialization, while `retlast_loop` reaches
      caller add/loop formation in the caller root itself
  - next exact target:
    - explain which recorder/return condition prevents `sum_loop` caller root
      from materializing the caller add and outer-loop PHIs after the traced
      callee call

- Timestamp: `2026-04-01 01:34:00 PDT`
- Reduced recstop logs narrow the vararg seam one step further
  - focused artifact bundle:
    - [20260401-kdz-vararg-recstop-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-vararg-recstop-audit)
  - `sum_loop`
    - `TRACE 1` is still the callee vararg loop and stops `-> loop`
    - `TRACE 2` starts as a second root at the caller site
    - `TRACE 2` stops back to `1` with:
      - `pc=...d824`
      - `op=54`
      - `prevop=78`
      - `startop=79`
      - `linktype=1`
      - `link=1`
      - `framedepth=2`
    - later caller trace `TRACE 7 (2/0)` repeats the same stop shape with
      `startop=88`, `linktype=1`, `link=1`, and `framedepth=2`
    - crucially, neither caller trace logs `S390X_RECLOOP` before stopping
      `-> 1`
  - `retlast_loop`
    - caller root still forms first and stops `-> loop`
    - caller side trace `TRACE 2 (1/0)` logs:
      - `S390X_RECLOOP ... op=57 startop=88 ev=2 lnk=1`
      - then `S390X_RECSTOP ... linktype=2 link=2 lnkop=88`
      - and stops `-> loop`
    - the same pattern then repeats through the rest of the caller loop family
  - implication:
    - the live vararg seam is now earlier than `rec_loop_jit_root`
    - `sum_loop` caller roots are not dying inside `rec_loop_jit()`
    - they are stopping before they ever reach the caller loop seam that
      `retlast_loop` reaches and turns into a loop family
  - next exact target:
    - explain which recorder/return condition on the traced-callee return path
      prevents `sum_loop` caller roots from reaching the caller loop op at all,
      while `retlast_loop` reaches that seam and stabilizes as a loop family

- Timestamp: `2026-04-01 01:52:00 PDT`
- Reduced return logs correct the live vararg seam again
  - focused artifact bundle:
    - [20260401-kdz-vararg-recret-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-vararg-recret-audit)
  - clean-host result:
    - `sum_loop` does not show a divergent post-return branch in
      `lj_record_ret()`
    - the only observed return branch is `lua_intrace_return`
    - that branch belongs to the inner `select()` fastfunc work in the callee
      loop, not to the outer caller add / return path
  - combined with the existing recstop dump:
    - `TRACE 2` starts at the caller site
    - enters `sum(...)`
    - then stops `-> 1` with:
      - `pc=...d824`
      - `op=54` (`GGET`)
      - `prevop=78` (`JFORI`)
      - `linktype=1`
      - `link=1`
      - `framedepth=2`
    - that is the callee loop-body start after the nested `JFORI` path has
      already moved `pc` into the callee loop body
  - implication:
    - the caller root is not being cut off after traced-callee return
    - it is being cut off earlier, by linking directly into the already-
      compiled callee loop trace when it reaches the nested `BC_JFORI` seam
    - `retlast_loop` does not have that nested callee loop boundary, so its
      caller root reaches caller add / loop formation directly
  - next exact target:
    - decide whether the `sum_loop` cliff is simply the normal root-stop path
      for a caller trace that enters an already-compiled nested callee loop via
      `BC_JFORI`, or whether there is still a narrower recorder ownership seam
      above that boundary

- Timestamp: `2026-04-01 02:02:00 PDT`
- Recorder code confirms the corrected `sum_loop` stop mechanism
  - relevant code path:
    - [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c#L3597)
    - `case BC_JFORI:` calls `rec_for(J, pc, 0)`
    - if the loop is entered, it immediately does:
      `lj_record_stop(J, LJ_TRLINK_ROOT, bc_d(...))`
  - this matches the clean-host `sum_loop` artifact exactly:
    - [20260401-kdz-vararg-recstop-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-vararg-recstop-audit)
    - `TRACE 2` starts at the caller site
    - then stops `-> 1` with:
      - `prevop=78` (`JFORI`)
      - `pc=...d824`, `op=54` (`GGET`) at the callee loop-body start
      - `linktype=1`, `link=1`
  - implication:
    - this is not a hidden post-return split anymore
    - it is the normal root-stop path for a caller trace that enters an
      already-compiled nested callee loop
    - `retlast_loop` avoids this because it has no nested callee loop there
  - next exact target:
    - decide whether caller-root ownership across a call into an already-
      compiled nested callee loop is a real optimization surface on the current
      mechanism, or whether that boundary should be treated as closed and the
      broader vararg queue should move elsewhere

- Timestamp: `2026-04-01 02:18:00 PDT`
- `bitops_mix` is now the live broader-throughput family
  - artifact root:
    - [20260331-kdz-bitops_mix-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260331-kdz-bitops_mix-truth-pack)
  - clean `kdz` medians:
    - `mix_bits/small`: JIT-on `0.000273`, `-joff` `0.000105`, ratio `2.60x`
    - `mix_bits/medium`: JIT-on `0.002093`, `-joff` `0.000525`, ratio `3.99x`
    - `mix_bits/hot`: JIT-on `0.007645`, `-joff` `0.002099`, ratio `3.64x`
    - focused hot: JIT-on `0.007662`, `-joff` `0.002123`, ratio `3.61x`
  - runtime read:
    - `TRACE_START 0`
    - `TRACE_STOP 0`
    - `TRACE_ABORT 0`
    - `TEXIT_COUNT 0`
    - classification: `compiled-body-dominated`
  - implication:
    - this is the first clean non-iterator, non-dispatch, non-helper family in
      the current queue that is materially red without any live exit churn
    - the queue should move off the vararg nested-callee-loop boundary and onto
      compiled-body lowering work

- Timestamp: `2026-04-01 02:28:00 PDT`
- Focused backend log names the next `bitops_mix` seam
  - relevant source:
    - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h#L78)
      new `LUAJIT_S390X_BITOP_LOG` gate
    - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h#L1428)
      `asm_bitop_logic()`
    - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h#L1443)
      `asm_bitshift()`
    - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h#L1490)
      `asm_brot()`
  - focused artifact bundle:
    - [20260401-kdz-bitop-log-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-bitop-log-audit)
  - clean `kdz` result:
    - the hot chain is exactly the workload shape:
      - `band`, `bxor`, `bor`, shifts, rotates, `bswap`, `bnot`
    - every logged hot op is `IRT_INT`
    - no helper-call seam and no exit seam appear in this classifier
    - the s390x backend path is the interesting part:
      - all of those lowering paths currently run through `asm_bnorm32()`
  - implication:
    - the next exact target is no longer ownership or exits
    - it is whether repeated per-op 32-bit normalization / extend work in the
      s390x bitop lowering is the real compiled-body payer in `bitops_mix`
  - next exact target:
    - prove whether one narrow backend normalization-hoist or int32-home
      experiment is justified before opening any optimization patch

- Timestamp: `2026-04-01 02:40:00 PDT`
- Refined `bitops_mix` backend log narrows the live compiled-body payer
  - relevant source:
    - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h#L110)
      refined `LUAJIT_S390X_BITOP_LOG` producer logging
  - focused artifact bundle:
    - [20260401-kdz-bitop-log-audit-v2](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-bitop-log-audit-v2)
  - clean `kdz` result:
    - the hot chain is still entirely `IRT_INT`
    - the refined producer refs show later bitops are usually consuming earlier
      bitop results, not fresh source values:
      - `logic` repeatedly takes prior `logic`, `shiftk`, `brolk`, `bswap`,
        and `bnot` producers
      - only a small base set comes straight from the original source integer
        or loop-carried arithmetic
    - no helper-call seam and no exit seam appear in this classifier
  - implication:
    - the live question is no longer “is the chain widening out of int32?”
    - it is whether s390x is paying `asm_bnorm32()` over and over on an
      already-int32 producer chain
  - next exact target:
    - prove whether one narrow normalization-state / int32-home experiment is
      justified before opening any backend optimization patch

- Timestamp: `2026-04-01 05:00:00 PDT`
- Focused `asm_bnorm32()` audit confirms repeated normalization on the hot
  `bitops_mix` producer chain
  - relevant source:
    - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h#L77)
      new `LUAJIT_S390X_BNORM_LOG` gate
    - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h#L1474)
      `asm_bnorm32()`
  - focused artifact bundle:
    - [20260401-kdz-bnorm-log-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-bnorm-log-audit)
  - clean `kdz` result:
    - the focused hot run logged `84` `S390X_BNORM` sites
    - exact split:
      - `42` sites normalize unary/shift nodes fed directly from the original
        source integer or loop-carried arithmetic
      - `42` sites normalize binary chain nodes where both operands are already
        prior bitops
    - all logged sites remain `IRT_INT`; this is not a 64-bit widening seam
  - implication:
    - the live backend question is now narrow and concrete
    - the s390x backend is demonstrably re-normalizing an already-int32 chain
      in the binary half of the hot path
  - next exact target:
    - prove whether one narrow normalization-state or int32-home experiment can
      safely skip some of those chain-node `asm_bnorm32()` calls before opening
      any optimization patch

- Timestamp: `2026-04-01 05:05:00 PDT`
- Binary-chain `asm_bnorm32()` skip experiment is rejected on clean `kdz`
  - relevant source:
    - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h#L1422)
      `asm_bnorm32()`
  - focused artifact bundle:
    - [20260401-kdz-bitop-chain-bnorm-skip-direct-v2](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-bitop-chain-bnorm-skip-direct-v2)
  - clean `kdz` result:
    - the gate fired on the intended seam:
      - `S390X_BNORM_SKIP` appears throughout the binary `band` / `bor` /
        `bxor` chain nodes
    - but the hot median regressed:
      - frozen `mix_bits/hot`: `0.007645`
      - gated `mix_bits/hot`: `0.008870`
    - `small` also regressed:
      - frozen `0.000273`
      - gated `0.000340`
  - implication:
    - repeated normalization on chain nodes is a real surface
    - but simply deleting those `asm_bnorm32()` calls is the wrong fix
  - next exact target:
    - if this family stays open, it must be a stricter normalization-state or
      int32-home experiment, not a plain chain-node skip

- Timestamp: `2026-04-01 05:15:00 PDT`
- Clean `kdz` mcode dump pins the current bitop hot-loop lowering shape
  - relevant source:
    - [src/lj_emit_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_emit_s390x.h#L120)
      `S390XI_OGR`
    - [src/lj_emit_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_emit_s390x.h#L122)
      `S390XI_XGR`
    - [src/lj_emit_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_emit_s390x.h#L123)
      `S390XI_NGR`
    - [src/lj_emit_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_emit_s390x.h#L97)
      `S390XI_LGFR`
  - focused artifact bundle:
    - [20260401-kdz-bitops-mcode-audit-v3](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-bitops-mcode-audit-v3)
  - clean `kdz` result:
    - after enabling the repo-local dump module via `LUA_PATH=./src/?.lua;;`,
      the hot loop body shows repeated raw opcode triplets:
      - `b904` (`LGR`)
      - `b980` / `b981` / `b982` (`NGR` / `OGR` / `XGR`)
      - `b914` (`LGFR`)
    - `BSWAP` also shows `b91f` (`LRVR`) followed by `b914`
    - no 32-bit logical register forms appear in the dumped hot body
  - implication:
    - the backend is explicitly materializing `64-bit logical op + post-op
      sign-extend` across the bitop chain
    - the remaining open backend question is now narrower than
      normalization-state alone
  - next exact target:
    - determine whether the s390x backend has a valid 32-bit logical lowering
      surface at all; if not, this compiled-body family is close to closure

- Timestamp: `2026-04-01 05:28:40 PDT`
- Source review shows the `bitops_mix` lowering seam is part of a broader
  backend-wide integer-result contract
  - relevant source:
    - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h#L1341)
      `asm_add()`
    - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h#L1635)
      `asm_sub()`
    - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h#L1710)
      `asm_mul()`
    - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h#L1482)
      `asm_bnorm32()`
  - source result:
    - integer add and sub already follow the same result contract seen in the
      dumped bitop chain:
      - normalize or sign-extend the destination with `LGFR` / `LLGFR`
      - perform the 64-bit integer op
      - then reassert the 32-bit result shape again on guarded paths
    - integer multiply is even more explicit:
      - `MSGFR` is bracketed by `LGFR` in both guarded and non-guarded forms
    - so the current `NGR` / `OGR` / `XGR` plus `LGFR` read in `bitops_mix`
      is not an isolated backend quirk
    - it is one instance of a broader s390x integer-result lowering contract
  - implication:
    - the next honest family is no longer “optimize bitops normalization”
    - it is “does the backend have any broader 32-bit integer-result lowering
      surface at all?”
    - if the answer is no, `bitops_mix` should be closed as a local family
      instead of continuing with narrower bitops-only patches
  - next exact target:
    - audit whether any valid backend-wide 32-bit ALU/logical lowering path
      exists for `int` results on s390x before opening another code
      experiment

- Timestamp: `2026-04-01 05:30:28 PDT`
- Emitter audit says the broader integer-result contract does not currently
  have an existing 32-bit register-op escape hatch
  - relevant source:
    - [src/lj_emit_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_emit_s390x.h#L97)
      active opcode definitions
    - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h#L1491)
      `asm_bitop_logic()`
    - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h#L1341)
      `asm_add()`
    - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h#L1635)
      `asm_sub()`
    - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h#L1710)
      `asm_mul()`
  - source result:
    - the active emitter definitions include only the 64-bit register forms
      used in the hot paths:
      - `AGR`, `SGR`, `NGR`, `OGR`, `XGR`, `MSGFR`
    - there are no wired 32-bit register `AR` / `SR` / `NR` / `OR` / `XR`
      forms sitting unused behind the current assembler selection logic
    - so there is no remaining honest “pick a better existing opcode” move on
      this backend surface
  - implication:
    - if this family stays open, it is no longer a bitops-local or opcode-swap
      experiment
    - it becomes a broader backend capability question: whether to introduce a
      real 32-bit integer-result lowering surface at all
    - if that is out of scope for the current line, `bitops_mix` should be
      closed as a local family instead of taking more narrow experiments
  - next exact target:
    - decide whether to open one explicit backend-wide 32-bit lowering design
      family, or close `bitops_mix` and redirect again

- Timestamp: `2026-04-01 06:17:48 PDT`
- Backend-wide 32-bit integer-result lowering is now a measured reject on
  clean `kdz`
  - native semantics probes closed the easy path first:
    - `AR`, `SR`, `NR`, `OR`, `XR`, `AHI`, `MSR`, and `LR` all preserve stale
      upper 32 bits in 64-bit mode
    - so a valid int32 lowering still needs explicit normalization under the
      current backend contract
  - first code pass:
    - add the missing 32-bit register arithmetic/logical forms to the emitter
    - lower through three-register arithmetic/logical forms plus the existing
      `asm_bnorm32()` / `LGFR` contract
    - authoritative clean-host artifact:
      - [20260401-kdz-bitops_mix-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-bitops_mix-truth-pack)
    - perf result:
      - `mix_bits/hot` regressed from frozen `0.007645` to `0.008957`
  - second code pass:
    - drop the three-register forms
    - keep the explicit normalize contract
    - use two-register `AR` / `SR` / `NR` / `OR` / `XR`
    - use `AHI`
    - use direct `CC_OF` guards for int32 `addov` / `subov`
    - use `LR` only where low-32 setup before a later normalize was enough
    - best clean `kdz` rerun:
      - `mix_bits/small 0.000261`
      - `mix_bits/medium 0.001637`
      - `mix_bits/hot 0.007879`
    - that is materially better than the first pass, but still slower than the
      frozen `0.007645`
  - follow-up shift setup variants also failed:
    - removing the pre-shift setup move pushed `mix_bits/hot` to `0.008114`
    - using `LR` for that setup pushed `mix_bits/hot` to `0.008079`
  - result:
    - source reverted to the frozen baseline after the clean-host checks
    - the backend-wide 32-bit opcode-swap family is closed on the current
      normalize-every-result mechanism
    - if this backend line reopens, the next honest family is a deeper
      normalized-result / int32-home design, not more local opcode swaps

- Timestamp: `2026-04-01 06:42:31 PDT`
- Corrected `asm_bnorm32()` classifier names the actual `bitops_mix` backend
  boundary on clean `kdz`
  - source/logging surface:
    - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h)
      now classifies `asm_bnorm32()` sites by producer shape and full-trace
      consumer shape
  - clean-host artifact:
    - [20260401-kdz-bitops-int32home-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-bitops-int32home-audit/summary.md)
  - finite probe result:
    - `REMOTE_RC=0`
    - `RESULT 873075307`
    - `TRACE_START 4`
    - `TRACE_STOP 3`
    - `TRACE_ABORT 0`
    - `TEXIT_COUNT 401`
  - corrected site split:
    - `chain-binary`: `1337` total
      - `1146` sites feed only later bitops
      - `174` sites first leave the chain through integer arithmetic
      - `16` sites leave through integer arithmetic plus one non-arith user
      - `1` dead-end site remains
    - `source-binary`: `189` total, all feed later bitops
    - `source-shift`: `763` total, all feed later bitops
    - `source-unary`: `382` total, all feed later bitops
  - named first non-bitop consumer:
    - op `41` = `ADD`
    - hit `190` times in the focused hot probe
  - implication:
    - the live backend seam is no longer “can we swap to 32-bit opcodes?”
    - it is “can the backend carry a normalized int32/result-home through the
      bitop chain and only normalize again when the chain leaves into `ADD`?”
    - that is the next honest backend family if `bitops_mix` stays open

- Timestamp: `2026-04-01 07:08:54 PDT`
- First env-gated int32-home carry-skip attempt is a clean `kdz` reject
  - implementation shape:
    - keep the corrected `asm_bnorm32()` classifier
    - under a temporary gate, skip producer-side normalize only for sites whose
      direct consumers stayed inside the safe logical bitop chain
  - clean-host artifact:
    - [20260401-kdz-bitops-int32home-gate-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-bitops-int32home-gate-check/summary.md)
  - structural result:
    - focused probe terminated cleanly with `REMOTE_RC=0`
    - `TRACE_START 4`
    - `TRACE_STOP 3`
    - `TRACE_ABORT 0`
    - `TEXIT_COUNT 401`
    - the gate was active and hit `2392` candidate sites
  - perf result on clean `kdz`:
    - baseline `mix_bits/hot 0.008095`
    - gated `mix_bits/hot 0.008593`
    - regression `+0.000498s` (`1.062x`)
  - result:
    - source returned to the non-behavior baseline after the host check
    - the named int32-home boundary is still real
    - but simple candidate-site `LGFR` skip is not promotable
    - if this family stays open, the next honest cut has to preserve a real
      normalized-result / int32-home state, not just suppress producer
      normalization

- Timestamp: `2026-04-01 10:07:33 PDT`
- First low32-home logical-subchain variant is a same-host `kdz` reject
  - implementation shape:
    - keep the corrected `asm_bnorm32()` classifier
    - open a temporary low32-home path only for:
      - `band` / `bor` / `bxor`
      - classifier-proven carry and `ADD`-tail nodes
    - lower those nodes with 32-bit `LR` / `NR` / `OR` / `XR`
    - do not emit internal post-op normalize on those nodes
  - clean-host artifact:
    - [20260401-kdz-bitops-low32home-subchain-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-bitops-low32home-subchain-check/summary.md)
  - first focused `kdz` pass:
    - baseline `mix_bits/hot 0.008784`
    - gated `mix_bits/hot 0.008609`
    - apparent delta `-0.000175s` (`0.980x`)
  - reduced structural read:
    - gated reduced check terminated cleanly
    - the new path did fire:
      - `logic32carry 21`
      - `logic32tail 2`
    - the focused trace-count script timed out in both baseline and gated
      forms, so it did not provide a usable structural split
  - same-host rerun to check stability:
    - [20260401-kdz-bitops-low32home-subchain-rerun](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-bitops-low32home-subchain-rerun/summary.md)
    - baseline `mix_bits/hot 0.008312`
    - gated `mix_bits/hot 0.008749`
    - regression `+0.000437s`
  - result:
    - source returned to the non-behavior baseline after the rerun
    - the gate is real, but the same-host perf result is unstable and not
      promotable
    - this exact low32-home logical-subchain variant is closed
    - if the backend line stays open, the next honest target is a deeper
      consumer-side normalized-result / int32-home design, not another local
      logical-subchain rewrite

- Timestamp: `2026-04-01 08:18:53 PDT`
- `vararg_paths` is now parked for the current cycle
  - closure read:
    - `sum_loop` remains red, but the front-most split is now classified as
      the normal root-stop path for a caller trace that enters an already-
      compiled nested callee loop at `BC_JFORI`
    - no narrower recorder seam has been named before that nested-loop entry
  - supporting evidence already on branch:
    - reduced clean-host probes showed `sum_loop` split across two hot roots,
      while `retlast_loop` stayed in the caller loop family
    - recorder code at
      [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c#L3597)
      matches the observed `BC_JFORI -> existing loop` root stop
  - control note:
    - there is no completed repo-local x64 reduced-`sum_loop` control artifact
      on this branch
    - the closure here rests on the clean `kdz` reduced probes plus shared
      recorder semantics, not on a new cross-backend diff
  - result:
    - do not reopen `vararg_paths` in this cycle unless a seam earlier than the
      nested callee-loop entry is named first

- Timestamp: `2026-04-01 08:18:53 PDT`
- Reduced logical-chain seam isolators preserve the same named backend family
  on clean `kdz`
  - new in-tree probes:
    - [tests/s390x/perf/logical_chain_tail_add.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/logical_chain_tail_add.lua)
    - [tests/s390x/perf/logical_chain_tail_store.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/logical_chain_tail_store.lua)
  - clean-host artifacts:
    - [20260401-kdz-logical_chain_tail_add-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-logical_chain_tail_add-truth-pack)
    - [20260401-kdz-logical_chain_tail_store-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-logical_chain_tail_store-truth-pack)
  - `logical_chain_tail_add`:
    - `chain_tail_add/hot`: JIT-on `0.008265`, `-joff` `0.002127`, ratio
      `3.89x`
    - focused read: `TRACE_START 0`, `TRACE_STOP 0`, `TRACE_ABORT 0`,
      `TEXIT_COUNT 0`
    - `asm_bnorm32()` first non-bitop consumer split:
      - `ADD`: `70`
      - `OP_-1`: `921`
  - `logical_chain_tail_store`:
    - `chain_tail_store/hot`: JIT-on `0.006822`, `-joff` `0.002020`, ratio
      `3.38x`
    - focused read: `TRACE_START 0`, `TRACE_STOP 0`, `TRACE_ABORT 0`,
      `TEXIT_COUNT 0`
    - `asm_bnorm32()` first non-bitop consumer split:
      - `ASTORE`: `70`
      - `OP_-1`: `924`
  - interpretation:
    - both reduced probes remain compiled-body dominated
    - the live seam is not “`ADD` only”
    - the backend line is now cleanly a broader low32-home /
      normalized-result contract problem where the safe logical chain must
      survive until a forced-normalization boundary
  - next honest target:
    - write the backend-wide invariant first
    - safe internal chain ops currently proven: `band`, `bor`, `bxor`
    - forced-normalization boundaries include at least:
      - integer arithmetic
      - store/compare/guard
      - helper-arg setup
      - snapshot-visible state
    - if that invariant cannot be stated and enforced cleanly, close
      `bitops_mix` as a local family too

- Timestamp: `2026-04-01 08:46:20 PDT`
- First invariant-driven reduced-probe low32-home gate is rejected on clean
  `kdz`
  - implementation shape:
    - keep current 64-bit logical lowering
    - skip producer-side `asm_bnorm32()` only for proven logical-chain carry
      nodes and `ADD`-tail nodes
    - insert explicit normalize at the `ADD` consumer boundary
    - gate names:
      - `LUAJIT_S390X_LOW32HOME_ADD`
      - `LUAJIT_S390X_LOW32HOME_LOG`
  - clean-host artifact:
    - [20260401-kdz-low32home-add-boundary-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-low32home-add-boundary-check/summary.md)
  - host result:
    - `logical_chain_tail_add`
      - baseline `0.008265`
      - gated `0.008807`
      - regression `+0.000542s` (`1.066x`)
    - `logical_chain_tail_store`
      - baseline `0.006822`
      - gated `0.007940`
      - regression `+0.001118s` (`1.164x`)
  - structural read:
    - the gate was active on the intended seam:
      - `logical_chain_tail_add`
        - `add-boundary:add-tail`: `46`
        - `skip-bnorm:add-tail`: `46`
        - `skip-bnorm:carry`: `483`
        - logged `asm_bnorm32()` sites: `991 -> 439`
      - `logical_chain_tail_store`
        - `skip-bnorm:carry`: `487`
        - logged `asm_bnorm32()` sites: `994 -> 489`
    - both reduced trace probes still timed out with `REMOTE_RC=124`
      ([trace.stdout.log](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-low32home-add-boundary-check/raw/logical_chain_tail_add/trace.stdout.log),
      [trace.stdout.log](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-low32home-add-boundary-check/raw/logical_chain_tail_store/trace.stdout.log))
  - result:
    - source returned to the non-behavior baseline after the host check
    - the seam is still real, but this exact consumer-boundary gate is not
      promotable
    - if the backend line stays open from here, the next honest target is no
      longer another partial `ADD`/tail gate
    - it is either:
      - a fuller stateful low32-home / normalized-result contract
      - or closure of `bitops_mix` as a local family

- Timestamp: `2026-04-01 13:06:09 PDT`
- `kdz` addhome classifier moves the next backend seam from “`ADD` boundary”
  to “plain non-guard `ADD` carry”
  - source:
    - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h)
      now has a focused `LUAJIT_S390X_ADDHOME_LOG` classifier for plain integer
      `ADD`
  - clean-host artifact:
    - [20260401-kdz-addhome-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-addhome-audit/summary.md)
  - `logical_chain_tail_add`:
    - `70` plain non-guard integer `ADD` sites matched the carry shape
    - split:
      - `46` where the current bitop result is the only low32-home source
      - `24` where both the carried total and current bitop result are already
        in the same low32-home carry family
    - those candidate adds only feed `PHI` / later plain `ADD`
  - `logical_chain_tail_store`:
    - no `ADD` site matched the carry shape
    - the bitop chain still first leaves into `ASTORE`
  - interpretation:
    - the previous “normalize at the `ADD` boundary” model was too coarse
    - on the add-tail isolator, plain non-guard integer `ADD` is itself part
      of the live low32-home carry family
    - on the store-tail isolator, `ASTORE` remains the first hard consumer
      boundary
  - classifier caveat:
    - the verbose `LUAJIT_S390X_ADDHOME_LOG=1` `hotloop=1` probes timed out
      with `REMOTE_RC=124`, so this is a structural attribution pass, not a
      perf bar
  - next honest target:
    - if `bitops_mix` stays open, try a fuller low32-home carry across plain
      non-guard integer `ADD` plus `PHI`
    - do not reopen another “normalize at `ADD` boundary” or other partial
      tail-only gate

- Timestamp: `2026-04-01 09:32:02 PDT`
- First native `kdz` pass on the low32-home `ADD`/`PHI` carry gate is rejected
  - implementation shape:
    - keep the existing addhome classifier
    - add a temporary low32-home carry gate for plain non-guard integer `ADD`
      when both sources stay inside the bitop / `ADD` / `PHI` carry family
    - skip producer-side `asm_bnorm32()` only for that carry family
    - gate name:
      - `LUAJIT_S390X_LOW32HOME_ADDPHI`
  - clean-host artifact:
    - [20260401-kdz-low32home-addphi-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-low32home-addphi-check/summary.md)
  - structural read:
    - the proof scripts completed cleanly:
      - `proof_add`: `REMOTE_RC=0`, `skip_count=2`
      - `proof_store`: `REMOTE_RC=0`, `skip_count=0`
    - both reduced trace probes timed out on clean `kdz`:
      - `chain_tail_add`: `REMOTE_RC=124`
      - `chain_tail_store`: `REMOTE_RC=124`
    - that is a first-gate failure even though the gate reached the intended
      seam
  - host medians:
    - `logical_chain_tail_add`: gated `0.007441`
    - `logical_chain_tail_store`: gated `0.006737`
  - result:
    - source returned to the non-behavior baseline after the host check
    - this exact low32-home `ADD`/`PHI` carry gate is not promotable
    - the remaining honest backend branch is now narrower:
      - either a fuller stateful low32-home / normalized-result contract that
        keeps the reduced trace probes finite and normalizes before any
        guard/compare, helper/call, store, or snapshot-visible exit boundary
      - or closure of the `bitops_mix` backend family too
  - design read from the current lowering:
    - safe internal family only:
      - bitop logic/unary/shift/rotate
      - plain non-guard integer `ADD`
      - loop `PHI` when both incoming arms remain inside that family
    - hard boundaries remain:
      - guard/compare
      - helper/call
      - store consumers such as `ASTORE`
      - snapshot-visible exits/restores

- Timestamp: `2026-04-01 09:53:56 PDT`
- Clean `kdz` boundary classifier says the live `bitops_mix` seam is
  add/guard-boundary dominated, not store-tail dominated
  - source:
    - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h)
      now has a logging-only `LUAJIT_S390X_LOW32HOME_LOG` classifier for
      bitop and plain non-guard `ADD` producers
  - clean-host artifact:
    - [20260401-kdz-low32home-boundary-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-low32home-boundary-audit/summary.md)
  - `logical_chain_tail_add`:
    - the extended family mostly stays internal:
      - `can_carry=1`: `1039`
      - `can_carry=0`: `94`
    - first hard consumer split:
      - `guard`: `65`
      - `other`: `29`
      - `store`: `0`
  - `logical_chain_tail_store`:
    - this is a separate consumer family:
      - `can_carry=1`: `898`
      - `can_carry=0`: `138`
    - first hard consumer split:
      - `store`: `68`
      - `guard`: `45`
      - `other`: `25`
  - `bitops_mix`:
    - treat the live benchmark as matching the add-tail seam, not the
      store-tail seam
  - classifier caveat:
    - the reduced trace probes still timed out with `REMOTE_RC=124` under the
      verbose logger, so this remains structural attribution, not a perf bar
  - result:
    - the current backend queue is no longer “generic low32-home contract”
    - it is low32-home consumption at the compare/guard boundary on the
      add-tail family
    - store-tail remains a real separate boundary family, but it is not the
      main `bitops_mix` seam

- Timestamp: `2026-04-01 10:11:20 PDT`
- Reduced clean `kdz` compare-boundary check names the active `bitops_mix`
  consumer exactly
  - source:
    - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h)
      now carries a summary-only `LUAJIT_S390X_LOW32CMP_LOG` classifier in
      `asm_intcomp()` and `asm_equal()`
  - clean-host artifact:
    - [20260401-kdz-low32cmp-add-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-low32cmp-add-check/summary.md)
  - reduced add-tail probe result:
    - `BUILD_RC=0`
    - `RUN_RC=0`
    - `RESULT 1746150614`
  - compare summary:
    - total low32-home compare consumers: `19`
    - phases:
      - `intcomp`: `14`
      - `equal`: `5`
    - ops:
      - `LE`: `14`
      - `NE`: `5`
    - source shape:
      - left source is always `ADD`: `19`
      - right source is always constant: `19`
      - unsigned compare path never triggers: `cmp32u=0`
      - signed immediate compare path covers the hot majority:
        - `imm16_signed=1`: `14`
        - `imm16_signed=0`: `5`
  - current-lowering match:
    - in
      [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h)
      the hot `LE` path is the signed-immediate `asm_intcomp()` branch using
      `CGHI`
    - the smaller equality side path is `asm_equal()` using `CGR`
    - [src/lj_emit_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_emit_s390x.h)
      currently exposes only the 64-bit compare forms used here:
      `CGR`, `CLGR`, `CGHI`
    - there is no already-wired 32-bit compare-consumer surface to reuse
  - result:
    - the live `bitops_mix` boundary is no longer a generic compare/guard
      frontier
    - it is specifically the carried-`ADD` into signed immediate `LE` loop
      compare boundary, with constant `NE` equality as the secondary consumer
    - the next honest backend target is consumption at that exact compare
      boundary, not another store-tail or broad low32-home skip variant
    - if this family stays open, the next code branch is a real emitter plus
      backend compare-consumer design, not another local normalization skip

- Timestamp: `2026-04-01 10:11:20 PDT`
- Clean `kdz` add-kind split closes the compare-consumer queue for
  `bitops_mix`
  - source:
    - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h)
      now splits compare-side `ADD` sources into control increment vs bitop
      tail kinds under the same summary logger
  - clean-host artifact:
    - [20260401-kdz-low32cmp-addkind-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-low32cmp-addkind-check/summary.md)
  - compare summary:
    - total compare consumers: `19`
    - `LE`: `14`
    - `NE`: `5`
    - left source `ADD`: `19`
    - right source constant: `19`
  - decisive add-kind split:
    - left add kind `ctrl_inc`: `19`
    - left add kind `bitop_tail`: `0`
  - reduced clean `kdz` `-jdump=is` proof matches the classification:
    - the hot `LE` is the induction increment compare `i + 1 <= 200`
    - the hot `NE` is the zero-check on that same induction value in the
      traced `arshift` path
    - the carried value path remains separate:
      - `ADD total, bitop_chain`
      - then loop `PHI total`
  - result:
    - the compare/guard seam is real, but it is loop control, not the carried
      bitop value seam
    - close compare-consumer work for `bitops_mix` on the current mechanism
    - the next honest backend target reverts to the carried value path only:
      low32-home through value-tail `ADD` plus loop `PHI`, explicitly
      excluding the control-increment `ADD + 1` compare path

- Timestamp: `2026-04-01 10:11:20 PDT`
- First exact value-tail `ADD` / `PHI` low32-home gate is rejected on clean
  `kdz`
  - source gate:
    - `LUAJIT_S390X_LOW32VALUEADDPHI=1`
    - only plain non-guard integer `ADD`
    - `op2` non-constant
    - at least one source is a real bitop producer
    - carry users restricted to `ADD` / `PHI`
    - control increment `ADD + 1` excluded by construction
  - clean-host artifact:
    - [20260401-kdz-low32valueaddphi-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-low32valueaddphi-check/summary.md)
  - reduced checks:
    - add-tail reduced probe:
      - `RUN_RC=0`
      - `S390X_LOW32VALUEADDPHI_SUMMARY candidates=10 skips=10`
    - store-tail reduced probe:
      - `RUN_RC=0`
      - no gate hits
  - structural gate:
    - add-tail trace probe: `RUN_RC=124`
    - store-tail trace probe: `RUN_RC=124`
  - result:
    - reject this exact value-tail producer-side skip gate
    - restore source baseline after the host check
    - if `bitops_mix` stays open, the next honest target is deeper than a
      producer-side `asm_bnorm32()` skip:
      it needs a fuller normalized-result / low32-home contract that stays
      finite under real trace formation

- Timestamp: `2026-04-01 11:17:54 PDT`
- Four-track queue correction: park `vararg_paths`, check in the low32-home
  contract, and require a finite reduced validator before any more backend
  code
  - `vararg_paths`:
    - treat `sum_loop` as the normal nested-callee `BC_JFORI -> existing loop`
      root-stop on the current mechanism
    - no narrower recorder seam was named before nested-loop entry
    - no matching reduced `x64` control artifact exists locally or in the
      checked-in repo for the same seam, so that comparison stays unavailable
      rather than inferred
    - park `vararg_paths` for this cycle
  - new checked-in design note:
    - [docs/s390x/low32-home-contract.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/low32-home-contract.md)
    - semantic states:
      - `W32_HOME`: low word authoritative, upper 32 unspecified
      - `W64_NORM`: fully normalized and safe for generic consumers
    - safe internal family:
      - bitop logic/unary/shift/rotate
      - plain non-guard integer `ADD`
      - loop `PHI` when incoming arms stay inside the same family
    - forced-normalization boundaries:
      - guard/compare
      - helper/call arg setup
      - store consumers such as `ASTORE`
      - snapshot-visible exits/restores
      - any consumer outside the family
    - emitter / ABI feasibility:
      - current 64-bit lowering is the only already-wired backend surface
      - word/high-word forms are architecture opportunities, not honest blind
        swaps under the current contract
      - helper ABI strategy is secondary here because `bitops_mix` is
        compiled-body dominated and helper/call remains a hard boundary
  - reduced-validator contract:
    - compile-only proof must return `REMOTE_RC=0` and hit only the intended
      seam
    - reduced trace probes for `logical_chain_tail_add`,
      `logical_chain_tail_store`, and `bitops_mix` must also return
      `REMOTE_RC=0`
    - the family must remain compiled-body dominated with `TEXIT_COUNT=0`
    - bare `REMOTE_RC=124` is automatic reject, not “interesting”
  - fallback queue if the low32-home contract cannot be stated or cannot stay
    finite:
    - `int_add_phi_only`
    - `logic_add_phi_noboundary`
    - `int_add_phi_store_epilogue` only if the first two disagree
  - result:
    - no more local opcode swaps, producer-side skips, or compare-boundary
      branches are honest next steps
    - the only live backend lane is a design-first low32-home /
      normalized-result contract with a finite reduced validator

- Timestamp: `2026-04-01 12:20:09 PDT`
- Emitter / ABI feasibility audit says the low32-home contract can start on
  the current 64-bit surface, but not as another opcode-swap family
  - checked-in design note updated:
    - [docs/s390x/low32-home-contract.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/low32-home-contract.md)
  - currently wired and usable backend surface:
    - normalizers and moves:
      - `LGR`
      - `LGFR`
      - `LLGFR`
    - 64-bit logical/arithmetic:
      - `AGR`
      - `SGR`
      - `NGR`
      - `OGR`
      - `XGR`
    - compare at hard boundaries:
      - `CGR`
      - `CLGR`
      - `CGHI`
    - shift/rotate family already used by the safe chain:
      - `SLLK`
      - `SRLK`
      - `SRAK`
      - `SLLG`
      - `SRLG`
      - `SRAG`
      - `RLL`
      - `LRVR`
    - memory boundary forms already wired:
      - `LLGF`
      - `STY`
      - `STG`
  - current emitter gap:
    - no wired 32-bit RR logical/arithmetic/compare family for this contract
    - no backend-wide `W32_HOME` state tracking
    - no explicit normalization hooks at every hard boundary
  - ABI read:
    - helper / call interaction remains a hard boundary, not the main
      optimization surface for `bitops_mix`
    - preserved-GPR strategy is secondary here because this family is
      compiled-body dominated
  - result:
    - if the backend family stays open, the first honest code branch is a
      stateful `W32_HOME` carry experiment over the existing 64-bit emitter
      surface
    - do not open another local opcode-swap or compare-consumer branch first

- Timestamp: `2026-04-01 12:28:40 PDT`
- First source-level `W32_HOME` carry prototype is now in place behind a gate,
  pending clean-host validation
  - source:
    - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h)
    - gate: `LUAJIT_S390X_W32HOME_STATEFUL`
  - shape:
    - skip post-op `asm_bnorm32()` for the safe-family producers:
      - bitop logic/unary/shift/rotate
      - plain non-guard integer `ADD`
    - normalize hard compare consumers explicitly through scratch registers in
      `asm_intcomp()` and `asm_equal()`
    - leave integer TValue store on the existing `asm_tvstore64x()` packing
      path
  - local validation:
    - `git diff --check` is clean
    - local rebuild succeeded with
      `env MACOSX_DEPLOYMENT_TARGET=15.0 make -C src -j4 luajit`
    - no clean-host `kdz` or `zkd0` result is claimed yet
  - next gate:
    - run the reduced clean-host structural validator on:
      - `logical_chain_tail_add`
      - `logical_chain_tail_store`
      - `bitops_mix`
    - require `REMOTE_RC=0` and the same compiled-body family
    - reject immediately on `REMOTE_RC=124`, structural drift, or same-host
      regression

- Timestamp: `2026-04-01 12:36:01 PDT`
- First stateful `W32_HOME` carry prototype is rejected on clean `kdz`
  - source gate:
    - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h)
    - `LUAJIT_S390X_W32HOME_STATEFUL`
  - clean-host artifact:
    - [20260401-kdz-low32home-stateful-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-low32home-stateful-check)
  - local bar before host check:
    - local rebuild succeeded with
      `env MACOSX_DEPLOYMENT_TARGET=15.0 make -C src -j4 luajit`
    - local smoke `./src/luajit -e 'print("ok")'` succeeded
  - clean `kdz` structural gate:
    - compile-only proofs all passed:
      - `chain_tail_add`: `REMOTE_RC=0`
      - `chain_tail_store`: `REMOTE_RC=0`
      - `mix_bits`: `REMOTE_RC=0`
    - reduced trace probes all failed the first real bar:
      - `chain_tail_add`: `REMOTE_RC=124`
      - `chain_tail_store`: `REMOTE_RC=124`
      - `mix_bits`: `REMOTE_RC=124`
  - result:
    - reject this exact stateful `W32_HOME` carry prototype
    - restore source baseline after the host check
    - if `bitops_mix` stays open, the next honest target is deeper than this
      first stateful carry experiment:
      it needs a backend-wide result-state design that still keeps reduced
      trace formation finite

- Timestamp: `2026-04-01 13:15:00 PDT`
- Throughput helper timeout parsing and validator path are corrected; the
  broader-throughput queue is no longer a compiled-body read
  - helper fixes:
    - [tools/s390x/build_throughput_truth_pack.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/build_throughput_truth_pack.py)
      now parses `REMOTE_RC=...`
    - [tests/s390x/helpers/testlib.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/helpers/testlib.lua)
      now provides lightweight trace/texit counters with no growing hist maps
  - corrected clean `kdz` restamps:
    - `bitops_mix`:
      - `mix_bits/hot`: JIT-on `0.008267`, `-joff` `0.002084`
      - `REMOTE_RC 0`, `TRACE_START 41`, `TRACE_STOP 41`, `TRACE_ABORT 0`,
        `TEXIT_COUNT 7981`
      - classification: `exit-dominated`
    - `logical_chain_tail_add`:
      - `chain_tail_add/hot`: JIT-on `0.008269`, `-joff` `0.002123`
      - `REMOTE_RC 0`, `TRACE_START 41`, `TRACE_STOP 41`, `TRACE_ABORT 0`,
        `TEXIT_COUNT 7981`
      - classification: `exit-dominated`
    - `logical_chain_tail_store`:
      - `chain_tail_store/hot`: JIT-on `0.006676`, `-joff` `0.002319`
      - `REMOTE_RC 0`, `TRACE_START 42`, `TRACE_STOP 42`, `TRACE_ABORT 0`,
        `TEXIT_COUNT 7983`
      - classification: `exit-dominated`
    - `int_add_phi_only`:
      - `add_phi_only/hot`: JIT-on `0.000672`, `-joff` `0.000020`
      - `REMOTE_RC 0`, `TRACE_START 20`, `TRACE_STOP 20`, `TRACE_ABORT 0`,
        `TEXIT_COUNT 4001`
      - classification: `exit-dominated`
    - `logic_add_phi_noboundary`:
      - `logic_add_phi_noboundary/hot`: JIT-on `0.003527`, `-joff`
        `0.002153`
      - `REMOTE_RC 0`, `TRACE_START 23`, `TRACE_STOP 21`, `TRACE_ABORT 2`,
        `TEXIT_COUNT 4001`
      - classification: `exit-dominated`
  - manual bare `-jv` clean-host probes for `int_add_phi_only` and
    `logic_add_phi_noboundary` both terminate with `REMOTE_RC=0` and show the
    same self-loop clone ladder after the first root/side formation
  - queue correction:
    - the earlier low32-home / normalized-result line was based on a false
      compiled-body read from the old validator path
    - do not reopen backend low32-home work on the current mechanism
    - the next honest target is generic throughput loop-clone / exit behavior,
      starting with `int_add_phi_only` as the smallest reproducer and
      `logical_chain_tail_add` / `bitops_mix` as the aligned siblings

- Timestamp: `2026-04-01 13:44:15 PDT`
- Generic throughput hotside queue now has a real promotable candidate:
  `CANON_EQUIV + SHARE_EQUIV`
  - smallest-reproducer artifact:
    [20260401-kdz-hotside-share-equiv-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-share-equiv-audit/summary.md)
  - `int_add_phi_only` focused `kdz` read:
    - baseline:
      - `hot 0.000786`
      - `TRACE_START 21`
      - `TEXIT_COUNT 4001`
      - `TRACEINFO_COUNT 27`
    - `SHARE_EQUIV` alone:
      - `hot 0.000659`
      - `TRACE_START 100`
      - `TEXIT_COUNT 300`
      - `TRACEINFO_COUNT 106`
      - focused parent `24 exit 0` proof shows:
        - `phase=share-done ... target=199`
        - immediately followed by `phase=start ... snapcount=200`
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
  - methodology correction:
    - the apparent mismatch between trace counter totals and `-jv` trace lines
      is expected
    - the reduced probes call
      [trace_counter_capture_lite()](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/helpers/testlib.lua),
      which installs its own `jit.attach("trace")` handler and therefore
      replaces the `-jv` trace logger after warmup
    - so `-jv` lines only show the pre-capture traces; `TRACEINFO_COUNT`
      confirms the later trace population
  - clean `kdz` sibling validation:
    - artifact:
      [20260401-kdz-hotside-canon-share-family-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-family-check/summary.md)
    - `logical_chain_tail_add`
      - baseline `hot 0.008741`, `TRACE_START 41`, `TEXIT_COUNT 7981`
      - candidate `hot 0.002683`, `TRACE_START 2`, `TEXIT_COUNT 8000`
    - `bitops_mix`
      - baseline `hot 0.008902`, `TRACE_START 41`, `TEXIT_COUNT 7981`
      - candidate `hot 0.002968`, `TRACE_START 2`, `TEXIT_COUNT 8000`
  - first `zkd0` screen:
    - artifact:
      [20260401-zkd0-hotside-canon-share-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-zkd0-hotside-canon-share-check/summary.md)
    - first trace probes returned `REMOTE_RC=1` only because the clean repo
      still had an older
      [tests/s390x/helpers/testlib.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/helpers/testlib.lua)
      without `trace_counter_capture_lite()`
    - after tracked-file resync and rebuild, structural counts matched `kdz`:
      - `TRACE_START 41 -> 2`
      - `TEXIT_COUNT 7981 -> 8000`
    - hot medians also improved materially:
      - `logical_chain_tail_add`: `0.018642 -> 0.005414`
      - `bitops_mix`: `0.009459 -> 0.004000`
  - queue correction:
    - the live question is no longer “can hotside share reduce exits?”
    - it is “what exact loop-clone mechanism lets `CANON_EQUIV + SHARE_EQUIV`
      win while aggregate exits stay flat?”
    - the next honest target is the stable tiny-trace-set shape under that
      combined policy, not `SHARE_EQUIV` alone and not backend low32-home work

- Timestamp: `2026-04-01 13:56:32 PDT`
- The generic throughput exit flurry is now mechanically narrowed on the
  smallest reproducer
  - mechanism artifact:
    [20260401-kdz-hotside-canon-share-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-mechanism/summary.md)
  - `SHARE_EQUIV` alone already proved the bad late seam:
    - `phase=equiv parent=24 exit=0 cand=6 child=7`
    - `phase=share-done parent=24 exit=0 cand=5 ... target=199`
    - immediately followed by `phase=start parent=24 exit=0 ... snapcount=200`
  - the combined canon/share pair behaves differently in the warmed measured
    run:
    - `FOCUS_PARENT=24` is silent
      - `TRACE_START 1`
      - `TRACE_STOP 1`
      - `TRACE_ABORT 0`
      - `TEXIT_COUNT 4021`
      - focused log lines `0`
    - the warmed `-jv` run shows only early-seam hotside activity:
      - `parent=24` hits `0`
      - `parent=4` hits `202`
      - final `phase=start parent=4 exit=0 ... snapcount=200`
  - source-order interpretation from
    [trace_hotside()](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c):
    - `try_canon()` runs before `share_equiv()`
    - so the combined policy rewrites the current parent back to an earlier
      equivalent trace before the hotcount/share logic acts
  - current strongest read:
    - the flurry is not many distinct hot exits
    - it is one repeated `exit 0` self-loop seam whose hotcount used to walk
      up an equivalent-parent clone ladder
    - `CANON_EQUIV + SHARE_EQUIV` wins by stopping that migration, not by
      lowering the raw number of exits
  - next queue correction:
    - stop treating the exit flurry itself as mysterious
    - the next honest target is to decide whether this pair should become one
      dedicated policy/gate and then validate it beyond the current throughput
      surfaces
