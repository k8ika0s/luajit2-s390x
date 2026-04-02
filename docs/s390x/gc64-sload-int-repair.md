# GC64 Integer SLOAD Repair Boundary

Last updated: 2026-04-02 10:33:14 PDT

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

## VM Contract Mismatch

The next source read narrows that replay-materialization problem again.

On s390x, the VM integer `FORI/FORL` fast path in
[vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc#L4331) is explicitly a 32-bit value path:

- `lg RB, FOR_IDX`
- `checkint RB`
- `ar RB, ITYPE`
- `setint RB`
- `stg RB, FOR_IDX`
- `stg RB, FOR_EXT`

So the VM contract for the hot integer loop body is not “use the inherited
tagged slot as-is”. It is “prove integer type, clear the tag into the working
register, do 32-bit arithmetic, then retag for storage”.

The repaired trace that still fails on the second hot run does not reach a
stable version of that contract. It passes the inherited integer `SLOAD`
typecheck, then repeatedly dies at:

- `trace 1 exit 0`
- `guardmark=0xe`
- `curins 14`
- `0014 > int MULOV 0003 +65537`
- `0003 = int SLOAD #4 TI`

So the current live problem is now narrower than tag extraction by itself:
the inherited numeric-`for` replay value is still not re-materialized as the
same cleared 32-bit arithmetic input that the VM fast path uses before the
header multiply/add chain.

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
