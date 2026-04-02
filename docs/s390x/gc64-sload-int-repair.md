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

## Hard Boundaries

Do not reopen:

- root `FORI` constructor surgery
- direct tag swap by itself
- low32-home / normalization families
- iterator / dispatch / bridge / vararg families

This is now a narrow GC64 integer-`SLOAD` typecheck repair problem.
