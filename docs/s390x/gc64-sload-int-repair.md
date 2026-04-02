# GC64 Integer SLOAD Repair Boundary

Last updated: 2026-04-02 17:42:00 PDT

## Live Seam

The promoted throughput slice now has one front-most backend seam on the real
workload:

- inherited hidden `STEP`
- `IR=SLOAD #4 TI`
- exact taken guard on clean `kdz`: `curins=3`
- repeated runtime mismatch:
  - live extracted tag: `0x1fff2`
  - expected constant: `0x1ffff`

This is not just a bad constant. It is a bad extraction contract.

## Current s390x Lowering

Current integer `SLOAD` typecheck in
[lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h):

1. load full 64-bit slot
2. logical right shift by `47` (`SRLG`)
3. compare against `((uint32_t)LJ_TISNUM >> 15)`

For a GC64 int TValue like `0xfff9000000060006`, that yields:

- logical `>> 47` -> `0x1fff2`
- expected -> `0x1ffff`

## Generic GC64 Contract

The generic GC64 contract is based on signed `itype()` extraction:

- [lj_obj.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_obj.h)
  defines `itype(o)` as `((uint32_t)((o)->it64 >> 47))`
- `it64` is signed, so this is an arithmetic shift

For the same GC64 int TValue:

- arithmetic `>> 47` -> `0xfffffff2`
- `LJ_TISNUM` -> `0xfffffff2`

That is why x86/arm64 do not use the current s390x compare shape:

- x86/x64 compare the high-word form `LJ_TISNUM << 15`
- arm64 compares the upper 32 bits against `LJ_TISNUM << 15`

## Closed Wrong Fix

Direct tag swapping is already rejected.

Changing only the expected constant without changing the extraction contract
made the reduced sibling look good, but broke the real helper workload.

So the next family is not:

- “use a different constant”
- “skip this compare”
- “reopen low32-home”

## Next Honest Candidate

Any repair must preserve the GC64 signed-tag contract for inherited integer
`SLOAD` typechecks.

Candidate shape:

- keep scope narrow:
  - inherited integer `SLOAD`
  - restored `SNAP #0`
  - promoted `UGET`/looproot throughput slice
- change the typecheck to a signed/GC64-consistent tag extraction
  instead of the current logical-shift form
- validate first on:
  - pure-add reducer
  - `number_helper_loop`
- use the reduced no-counter probe path first, so later Lua callback churn does
  not masquerade as the next workload seam

Accept only if all three hold:

1. reduced probes stay finite
2. real helper workload stays correct
3. same-host pinned medians improve on `kdz`, then survive `zkd0`

## Closed Misread

The later reduced `BC_ISF` crash surface from the rejected arithmetic-shift
prototype is not the next workload seam.

- the reduced helper loop body itself has no `BC_ISF`
- the repeated restored sequence around that crash is `ISF -> RET0 -> TGETS`
- that matches the Lua callback/header shape used by the probe-side
  `jit.attach()` counter hooks

So the next real question remains the helper-backed wrong-result path on the
workload itself, not the callback path inside the reduced validator.

## Narrowed Failure Shape

With both counter callbacks and post-run `traceinfo/traceir` loops removed from
the reduced probe helper, the signed/arithmetic extraction repair no longer
fails on the first hot helper run.

- reduced helper path is correct at `n=200`
- reduced helper path is correct on the first hot `n=64000` run
- the break appears on the immediately following second hot run:
  - first: `1323881804`
  - second: `34304`

So the remaining seam is now replay after one successful long run, not first
compile birth and not the stripped-down helper loop body itself.

## Second-Hot Replay Attribution

The next host slice narrowed that replay seam further.

- direct two-hot host replay is captured in:
  [20260402-kdz-gc64-int-sload-ashift-two-hot-replay-v1](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-gc64-int-sload-ashift-two-hot-replay-v1/summary.md)
- repeated failure after the first successful hot run is:
  - `trace 1 exit 0`
  - restored `pc op=45` / `snapop=45` (`BC_UGET`)
  - repeated exact-taken `guardmark=0xe`
- on the real workload trace, `guardmark=0xe` maps to:
  - `curins 14`
  - `0014 > int MULOV 0003 +65537`
  - where `0003` is `int SLOAD #4 TI`
- runtime state at that repeated seam shows packed numeric-`for` replay, not a
  plain integer loop index:
  - `r11=0x8001`, `0x8002`, `0x8003`, ...
  - `r12=0xffffffff80018001`, `0xffffffff80028002`, ...

So the signed/arithmetic extraction idea is directionally right for the
inherited integer-`SLOAD` typecheck itself, but the remaining failure is the
numeric-`for` replay materialization contract after that check starts passing.

## VM Contract Correction

The earlier `FORI/JFORI` rematerialization read was too broad.

On s390x:

- `BC_JFORI` does **not** do `idx += step`
- only `BC_JFORL` / `BC_IFORL` perform the integer update/store step before
  the loop body
- `BC_JFORI` checks the current integer loop state and stores the current
  visible `FOR_EXT` before `JLOOP`

So the live replay question is not “where did `JFORI` forget to advance the
index?”. It is narrower:

- where does the current visible numeric-for value that `trace 1` expects at
  restored `SNAP #0` come from on compiled re-entry?
- and why does that inherited visible-value contract still fail every trip?

## Paired Repair Slice

The next `kdz` pair now has a clean outcome:

- `LUAJIT_S390X_GC64_SIGNED_INT_SLOAD=1`
- `LUAJIT_S390X_JFORI_INTERP_HANDOFF=1`

Results:

- the earlier second-hot wrong-result path is corrected on both:
  - the pure-add sibling
  - the real `number_helper_loop` workload
- but the steady real-workload counters stay flat:
  - baseline `TRACE_START 7`, `TEXIT_COUNT 64001`
  - paired gate `TRACE_START 7`, `TEXIT_COUNT 64001`
- the tiny entry trace changes only from:
  - baseline `TRACEINFO 2 1 root 4 6 3`
  - to paired gate `TRACEINFO 2 0 interpreter 4 6 3`

And the exact exit read under the pair still says:

- repeated seam: `trace 1 exit 0`
- restored `pc op=45` / `snapop=45` (`BC_UGET`)
- exact taken `guardmark=0x3`

That means:

- the paired gate fixes a real secondary correctness blocker
- it does not fix the primary steady-state exit seam
- the remaining live problem is the inherited visible numeric-for current-value
  contract at restored `SNAP #0`, not another `JFORI` population tweak

## Recorder/Header Split

The next reduced recorder slice corrects the header picture again:

- on the real `number_helper_loop` path, `fori_arg()` already constantizes
  hidden `STEP`
- hidden `STOP` does not constantize there, because it is the runtime stop
  argument `n`
- a literal-stop sibling (`for i = 1, 400 do`) does constantize both hidden
  `STOP` and hidden `STEP`
- but the repeated exit flurry still survives there and shifts to a later
  guard (`guardmark=0xd`)

So a direct recorder const-init repair is not the next honest fix. The dynamic
form’s inherited hidden-`STOP` replay is front-most, but stabilizing it only
exposes a later header/body guard family.

## Hard Boundaries

Do not reopen:

- root `FORI` constructor surgery
- direct tag swap by itself
- low32-home / normalization families
- iterator / dispatch / bridge / vararg families

This is now a narrow GC64 inherited numeric-`for` replay-materialization
problem: restore the VM-style cleared 32-bit arithmetic contract before the
header multiply path, not just the tag-extraction compare.

## Second-Run Entry Path

The next read tightens where that rematerialization has to happen.

`TRACE 1` is the loop trace:

- starts at `BC_FORL`
- body snapshots restore to `BC_UGET`
- first header arithmetic use is:
  - `0003 > int SLOAD #4 TI`
  - `0014 > int MULOV 0003 +65537`

But the second hot run is not reaching that loop only through the VM
`FORI/FORL` path.

From the real workload dump:

- `TRACE 2` starts earlier as a tiny `FUNCF` root
- it only checks the loop bound `n`:
  - `0001 > int SLOAD #2 T`
  - `0002 > int LE 0001 +2147483646`
  - `0003 > int GE 0001 +1`
- then it stops `-> 1`

So the current source-backed read is:

- `TRACE 2` is a function-entry handoff into the existing loop trace
- its `stop -> 1` shape matches the compiled-loop handoff path, not the VM
  `FORI/FORL` path
- that handoff can reach `TRACE 1` without re-running the VM integer
  `FORI/FORL` materialization path
- `lj_snap_replay()` itself only recreates inherited `IR_SLOAD` nodes; it does
  not perform the VM-style `checkint -> 32-bit add -> setint -> store`
  rematerialization

That is why the repeated bad second-run value matters:

- the failing arithmetic input increments as `0x8001`, `0x8002`, `0x8003`, ...
- this cannot be the constant step slot
- it matches the inherited numeric-for index/current-value path reaching
  `TRACE 1` already stale/high before the first body arithmetic use

So the next honest target is no longer just “fix the inherited integer
typecheck”. It is:

- explain the function-entry handoff into `TRACE 1`
- identify where the VM-style numeric-for state is supposed to be rebuilt on
  that path
- and why it is not rebuilt before `TRACE 1` consumes `SLOAD #4`

## Likely Missing Rematerialization Point

The next source read points at one specific handoff shape.

In
[rec_for(..., isforl=0)](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c#L1127),
the entry-path recording for `FORI/JFORI`:

- loads or constifies `IDX/STOP/STEP`
- sets `FORL_EXT = FORL_IDX`
- emits the enter/leave guard

But on that path it does not emit the VM-style integer-loop update that the
interpreter performs in `FORI/FORL`:

- clear the current integer working value
- add `STEP`
- mirror the updated value into both hidden `IDX` and visible `EXT`

That is consistent with the tiny `TRACE 2` shape on the real workload:

- only `n` range checks survive as IR
- then the trace stops `-> 1`

So the current best source-backed read is:

- the second-run bad path is a `FORI/JFORI` entry trace handing off directly to
  the existing loop trace
- that handoff is likely proving loop entry without rebuilding the same
  numeric-for state the VM would have established before `BC_UGET`
- `TRACE 1` then consumes the inherited numeric-for value too early, and on
  the bad path it is already stale/high

## Direct Remediation Result

The direct inherited integer `SLOAD` seam is now answered, but the first exact
repair is rejected and the branch source is back on baseline.

The repair that was tested:

- signed/arithmetic GC64 integer `SLOAD` extraction on s390x
- matching `JFORI` interpreter handoff

The corrected signed comparison is real:

- for a boxed GC64 int, arithmetic `>> 47` produces signed `itype()` space
- the current s390x signed path was comparing that signed value against the
  wrong expected constant
- changing the expected constant to signed `LJ_TISNUM` clears the inherited
  integer `SLOAD` mismatch on the reduced seam

But it is not promotable on the real helper workload.

Reduced `kdz` checks with the signed-expected repair:

- localized helper reducer:
  [20260402-kdz-dynamic-local-after-signed-expected-fix](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-dynamic-local-after-signed-expected-fix/summary.md)
  - `RESULT 961100104`
  - old inherited `SLOAD(op1=5)` seam is gone
  - front-most repeated seam moves to `trace 1 exit 3`
- reduced real helper workload:
  [20260402-kdz-number-helper-after-signed-expected-fix](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-number-helper-after-signed-expected-fix/summary.md)
  - `RESULT 961100104` at reduced `n=400`
  - repeated seam also moves off the old inherited `SLOAD`

Real hot helper workload on clean `kdz` fails the first promotion bar:

- helper-backed truth-pack failure:
  [20260402-kdz-be_helpers-hotside_canon_share_uget_looproot_default-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260402-kdz-be_helpers-hotside_canon_share_uget_looproot_default-truth-pack/raw/jit-on.stderr.log)
  - `number_helper_loop/hot: expected 1323881804, got 34304`

The failure shape is now pinned:

- first long hot run is correct
- the second long hot run in the same process is wrong
- the break appears only at larger second-run sizes
  - still correct through smaller reruns
  - fails from roughly `n=40000` onward
- direct second-run counter check:
  - `TRACE_START 3`
  - `TRACE_STOP 2`
  - `TRACE_ABORT 1`
  - `TEXIT_COUNT 2`

And the moved live seam is no longer the inherited `SLOAD` typecheck itself.
On the bad warm-built rerun:

- `TRACE 1` is still the main loop
- `TRACE 2 (1/0)` is the warm-built overflow side loop
- `TRACE 3` is the later fallback/interpreter path
- the decisive shifted side-loop body is:
  - `num CONV`
  - `num MUL`
  - `int TOBIT`
  - `int ADD`

So the corrected state is:

- the inherited GC64 integer `SLOAD` mismatch was real
- the signed-expected compare repair is directionally right
- but it is not safe to promote because the real helper workload then falls
  into a later warm-built overflow-side-loop continuation bug
- this document therefore treats the signed-expected repair as a rejected
  classifier, not an active default

## Current Honest Target

The next live seam is now the warmed overflow-side-loop continuation on the
real helper path, not the original inherited integer `SLOAD` compare.

The no-print-mid two-run dump closes the first ambiguity:

- artifact:
  [20260402-kdz-signedfix-two-run-noprint-mid](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-signedfix-two-run-noprint-mid/summary.md)
- `WARM 132610`
- `SECOND 25535`
- exact trace chain:
  - `TRACE 1`: main int loop
  - `TRACE 2 (1/2)`: overflow side path, `stop -> 1`
  - `TRACE 3 (1/0)`: warmed overflow loop
  - `TRACE 4 (3/3)`: return-side continuation at `return bit.tobit(total)`,
    `stop -> 1`
  - `TRACE 5 (4/0)`: later stitch into `print`
- exact narrowed return seam:
  - `trace 4 exit 0`
  - `guardmark=0xd`
  - in `TRACE 4` IR that lines up with `curins 13`
  - `0013 > p64 RETF ...`

So the next exact family is narrower again:

- second hot run only
- overflow side-loop return-to-caller continuation
- specifically the `RETF` / lower-frame return handoff after the warmed
  `TRACE 3` loop
- not the later print stitch

That is the next exact family to map before any new code attempt.

Reduced helper variants after the repair close the helper-specific split:

- artifact:
  [20260402-kdz-postrepair-helper-variant-only](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-postrepair-helper-variant-only/summary.md)
- original helper form:
  - `number_helper_literal_stop`: `TRACE_START 1`, `TEXIT_COUNT 399`
  - exact taken guard still lands on the carried-state `SLOAD #2 T`
- moving `tobit` into a local:
  - `number_helper_local_tobit`: `TRACE_START 1`, `TEXIT_COUNT 0`
- passing `tobit` as an argument:
  - `number_helper_arg_tobit`: `TRACE_START 2`, `TEXIT_COUNT 0`

So the next honest frontier is narrower than either old theory:

- not more inherited-int extraction work
- not generic `TGETS` attribution
- specifically: explain why the helper-form `bit.tobit` header interaction
  keeps the inherited numeric-for index/current-value `SLOAD` seam live,
  while local/arg helper forms eliminate exits entirely

That reduced-only clue is not directly promotable yet.

Clean `kdz` dynamic localization check:

- artifact:
  [20260402-kdz-dynamic-helper-localization-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-dynamic-helper-localization-check/raw/number_helper_loop_local_tobit.stderr.log)
- real `n=64000` local-helper form:
  - `RESULT -149783296`
  - `TRACE_START 321`
  - `TRACE_STOP 321`
  - `TRACE_ABORT 0`
  - `TEXIT_COUNT 64001`
  - then aborts with `table overflow`

So the reduced helper-localization split is evidence only:

- it proves the imported helper header matters
- it does **not** by itself define a promotable remediation family on the real
  workload

The dynamic helper-form split is now sharper than that first write-up:

- stripped real-workload localization runs without counter/posthook churn and
  reduced `n=400` keep both dynamic localized forms finite, correct, and on
  the same moved seam instead of removing it:
  - local helper form:
    [20260402-kdz-dynamic-local-iter400](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-dynamic-local-iter400/summary.md)
  - arg helper form:
    [20260402-kdz-dynamic-arg-iter400](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-dynamic-arg-iter400/summary.md)
- both reduced real-workload variants are correct:
  - `RESULT 961100104`
- both keep the same reduced trace population shape:
  - local helper:
    - `TRACEINFO 1 1 loop 19 8 4`
    - `TRACEINFO 2 0 interpreter 14 12 3`
  - arg helper:
    - `TRACEINFO 1 1 loop 19 8 4`
    - `TRACEINFO 2 0 interpreter 4 6 3`
- both restore and re-exit at:
  - `pc op=18`
  - `snapop=18`
  - repeated `guardmark=0x3`
- recorder setup plus reduced `TRACEIR` now pins the moved inherited lane
  semantically on both localized forms:
  - `baseslot=2`
  - `op1=3` -> carried `total`
  - `op1=4` -> localized `tobit` value
  - `op1=5` -> current numeric-for value feeding `* 65537`
  - `op1=6` -> loop bound `n`
- both now pin the same exact moved inherited guard:
  - `curins=3`
  - `IR=SLOAD`
  - `op1=5`
  - `op2=36`
  - `kind=sload_int`
  - `ofs=24`
  - `extra=28`
- matching reduced `TRACEIR` on both localized forms:
  - `TRACEIR tr=1 ins=3 op=SLOAD ... op1=5 op2=36`
  - `TRACEIR tr=1 ins=5 op=MULOV ... op1=3 op2=-6`
  - that `MULOV` use closes the semantic owner:
    `SLOAD(op1=5)` is the current localized numeric-for value, not the helper
    itself
- `op=18` is `BC_MOV`, and on the parser/recorder side that is plain
  slot-to-slot variable movement:
  - [lj_bc.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_bc.h)
    encodes `MOV` as `dst <- var`
  - [lj_parse.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_parse.c#L544)
    emits `BC_MOV` when a non-reloc value has to be copied to a different slot
  - [lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c#L3527)
    records `BC_MOV` as a stack-slot move with no new arithmetic IR

So the reduced helper-localization split is evidence only, and the dynamic
story is now precise:

- helper localization does not clear the real replay problem
- it shifts the steady seam from imported-helper `BC_UGET` replay to
  stack-visible helper/value `BC_MOV` replay on the real workload
- after that shift, both dynamic localized forms still converge on the same
  inherited integer `SLOAD` lane for the current numeric-for value
  (`op1=5`, `ofs=24`, `extra=28`)
- this is still the same promoted-slice replay family, just one step later in
  the header/call setup

The next reduced slot-state slice closes the remaining rematerialization split:

- artifact:
  [20260402-kdz-dynamic-local-slotlog](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-dynamic-local-slotlog/summary.md)
- the repeated exact seam is still:
  - `trace 1 exit 0`
  - restored `pc op=18`, `snapop=18`
  - `guardmark=0x3`
  - `curins=3`, `IR=SLOAD`, `op1=5`, `op2=36`
- but the replayed loop state at that seam is already coherent and advancing:
  - live current value register `r11` walks `0x3`, `0x4`, `0x5`, ...
  - the carried `total` dump in `r3tv q0` stays a valid boxed GC64 int:
    - `0xfff9000000060006`
    - `0xfff90000000a000a`
    - `0xfff90000000f000f`
    - `0xfff9000000150015`
  - those values match the expected carried totals for the previous loop
    iterations

So the dynamic-localized replay failure is no longer honestly described as
missing current-value rematerialization before the inherited `SLOAD` fires.
The live question is narrower:

- why inherited integer `SLOAD` replay/typecheck still exits on the localized
  current numeric-for value lane even when replayed state is already live and
  progressing
- not imported-helper lookup
- not another rematerialization theory

The follow-up current-`HEAD` slot-logger run makes that sharper still:

- artifact:
  [20260402-kdz-dynamic-local-slotlog-v2](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-dynamic-local-slotlog-v2/summary.md)
- the exact failing inherited lane is still:
  - `curins=3`
  - `IR=SLOAD`
  - `op1=5`
  - `op2=36`
  - `ofs=24`
- `S390X_SLOADMAP` now shows that lane is compiled off the normal stack base:
  - `base=12`
  - runtime exit dump shows `r12 == L->base`
- and `S390X_SLOT` proves the physical slot at that base/offset is good:
  - `baseslot=2`, so `op1=5` maps to `idx=3`
  - `S390X_SLOT idx=3` is a valid boxed int on every repeated exit:
    - `0xfff9000000000003`
    - `0xfff9000000000004`
    - `0xfff9000000000005`
    - ...

So the remaining localized seam is no longer “maybe stale slot, maybe missing
store-back”. The inherited integer `SLOAD` replay/typecheck is firing on the
correct live current-value slot.

That keeps the next honest target where it belongs:

- exact inherited integer `SLOAD` replay/typecheck contract at that shifted
  current numeric-for-value lane
- not imported-helper lookup attribution
- not “just localize the helper”

## RETF Callsite Split

The next return-contract slice closes one wrong generalization.

- artifact:
  `/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-retf-runtime-contract/raw`
- in the two-call-site no-print-mid reducer, `TRACE 4` really is specialized to
  one lower-frame return PC and then re-entered from another:
  - recorder side:
    - `site=lua_lower_frame_retf trace=4 ... frame_pc=0x...6b70`
  - backend side:
    - `S390X_RETF trace=4 curins=13 delta=4`
  - exact taken runtime exit:
    - `trace 4 exit 0`
    - `guardmark=0xd`
    - `r2=0x...6b70`
    - `r11=0x...6b7c`
- the local bytecode listing for that reducer explains the `0xc` gap:
  - top-level call sites are:
    - `0013 CALL 2 2 2` for `warm = run(64000)`
    - `0016 CALL 3 2 2` for `second = run(40000)`
  - so `0x...6b70` and `0x...6b7c` are distinct caller continuation PCs three
    bytecodes apart

So this is now explicit:

- direct `RETF` mismatch is real on polymorphic lower-frame return PCs
- but it is a side seam in the two-call-site reducer
- it is not yet sufficient to explain the whole post-repair wrong-result path

## Stable-Callsite Control

The same-call-site loop control closes the next ambiguity.

- artifact:
  `/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-retf-single-callsite-loop/raw`
- probe shape:
  - `drive(n, reps)` calls `run(n)` twice from the same `FORL` caller site
  - result is still wrong:
    - `RESULT 25535`
- but the failing seam moves past the direct `RETF` mismatch:
  - `TRACE 4` still contains:
    - `0013 > p64 RETF ...`
  - after that handoff it records the caller loop header:
    - `0014    int SLOAD  #6    RI`
    - `0015 >  int SLOAD  #5    TI`
    - `0016    int ADD    0015  +1`
    - `0017 >  int LE     0016  0014`
  - the exact taken runtime exit is:
    - `trace 4 exit 2`
    - restored `pc op=76`
    - `guardmark=0x11`
  - `TRACE 5` then starts from `4/2`, not from a failed `RETF`

This changes the live read materially:

- stabilizing the caller return PC does not make the wrong-result path go away
- once `RETF` is no longer the first failing point, the live seam moves into
  the caller numeric-for header after return
- the active post-repair family is therefore:
  - lower-frame return into a caller loop
  - then inherited caller `FORI/FORL` state (`SLOAD #5/#6`, `ADD`, `LE`)
  - not generic `RETF` alone

## Current Honest Target

The next exact target is now narrower again:

- map the caller numeric-for header contract after a successful lower-frame
  return on the post-repair path
- use the stable-callsite loop as the clean control
- keep the polymorphic `RETF` mismatch as evidence-only, not the primary live
  remediation target

## Caller FORL Slot-State Correction

The next caller-loop slot-state probe closes one more wrong theory.

- artifact:
  `/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-caller-forl-seam/raw`
- on the stable-callsite loop, the exact taken runtime exit is:
  - `trace 4 exit 2`
  - restored `pc op=76`
  - `guardmark=0x11`
- local bytecode listing identifies `op=76` as the caller `FORL` in
  `drive(n, reps)`:
  - caller bytecode:
    - `0005 FORI 3 => 0011`
    - `0010 FORL 3 => 0006`

The slot-state dump at that exact `FORL` exit is coherent:

- `idx=2` (caller `out`) is wrong:
  - `0xfff90000000063bf`
  - value `25535`
- caller loop state itself is consistent with a normal second-iteration exit:
  - `idx=3` -> `2`
  - `idx=4` -> `2`
  - `idx=5` -> `1`
  - `idx=6` -> `2`

So this closes the broader caller-loop theory:

- the stable-callsite seam is not “bad caller `FORL` state”
- the caller numeric-for header is coherent at the taken exit
- the wrong value is already sitting in the caller-visible result slot when the
  loop exits

That moves the live post-repair target one step again:

- return-value handoff from the warmed overflow path into the lower-frame
  caller-visible result slot
- not generic `RETF`
- not generic caller `FORL` state

## Current Pair Closure

The current opt-in GC64 replay pair changes that read.

- opt-ins:
  - `LUAJIT_S390X_GC64_SIGNED_INT_SLOAD=1`
  - `LUAJIT_S390X_JFORI_INTERP_HANDOFF=1`

Stable-callsite control on `kdz` is now correct:

- [20260402-kdz-recret-slotlog-signedfix-v1](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-recret-slotlog-signedfix-v1/raw/stdout.log)
- `RESULT -2050009568`
- recorder return logs show no `lua_lower_frame_retf`; only
  `lua_intrace_return`

The real helper workload is also correct on both hosts under the same pair:

- [20260402-kdz-number-helper-optinpair-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-number-helper-optinpair-check/raw/stdout.log)
- [20260402-zkd0-number-helper-optinpair-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-zkd0-number-helper-optinpair-check/raw/stdout.log)
- both return:
  - `WARM -149783296`
  - `SECOND -149783296`

Authoritative `kdz` helper-backed validation with the pair layered onto the
promoted default confirms that correctness improvement does not open a new perf
family:

- [20260402-kdz-be_helpers-hotside_canon_share_uget_looproot_default-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260402-kdz-be_helpers-hotside_canon_share_uget_looproot_default-truth-pack/summary.md)
- `number_helper_loop/hot 0.008248` vs `-joff 0.002282`
- `be_pack_loop/hot 0.023373` vs `-joff 0.018732`
- focused read remains:
  - `TRACE_START 6`
  - `TRACE_STOP 5`
  - `TRACE_ABORT 1`
  - `TEXIT_COUNT 64001`

Focused mechanism under the pair:

- [20260402-kdz-number-helper-optinpair-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-number-helper-optinpair-mechanism/summary.md)
- dominant seam is still:
  - `trace 7 exit 0`
  - restored `BC_UGET`
  - first `sload_int`: `curins 15`, `IR=SLOAD`, `op1=3`, `ofs=8`,
    `extra=12`

So the old lower-frame return-value failure is stale for the current pair. The
pair is correctness-positive, but on the active helper slice it is effectively
perf-inert and does not displace the steady header-guard seam.
