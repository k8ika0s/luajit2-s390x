# FORL Visible Current Latch

## Current Read

The remaining helper-slice red is no longer a replay-PC problem, a `FORL`
fastpath miss, or a helper-only lookup seam.

What is now pinned:

- the `FORL` fastpath already hits on the live helper seam
  - `pc_match=1`
  - `idx_match=1`
- the exact taken guard is still the visible current numeric-for value lane
  rebuilt as:
  - `IR=SLOAD`
  - `IRSLOAD_INHERIT | IRSLOAD_TYPECHECK`
- dropping that lane's typecheck is not safe, even when hidden `STOP/STEP`
  anchoring is preserved:
  - `pure_add_reducer` explodes into a clone ladder and `table overflow`
- but instrumented reduced baseline runs still show the visible current-value
  slot itself is already a boxed int and increments normally at repeated exits
  before the logging perturbation derails the run

This line is now effectively closed as a remediation candidate on the current
mechanism.

What the later cross-checks proved:

- the same exact repeated guard signature is front-most across the remaining
  promoted-default core workloads:
  - `number_helper_loop`
  - `be_pack_loop`
  - `direct_abs`
- reduced `kdz` mechanism probes for `be_pack_loop` and `direct_abs` land on
  the same exact taken guard:
  - `curins=3`
  - `sload_int`
  - `ofs=16`
  - `extra=20`
- the accompanying reduced dumps show the same front lane:
  - `0003 >  int SLOAD  #4    TI`

So the surviving seam is no longer an entry-only helper/header theory. It is a
generic dynamic-stop numeric-for replay contract shared across the current
promotion-core winners.

## Generic Invariant

Once the recorder chooses the narrowed integer `FORL` path, the visible
`FORL_IDX` slot must enter replay as an int-tagged TValue, not just a
numerically integral value.

That invariant is generic recorder/runtime behavior:

- recorder:
  [lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c)
- slot layout:
  [lj_bc.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_bc.h)
- VM numeric-for contract:
  [vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc)

This is not a second s390x-only backend bug. The earlier GC64 signed-int
extraction issue was s390x-specific; the surviving current-value lane is not.

## Closure

The once-per-replay latch theory should not be reopened on the current
mechanism.

Why:

- recorder logic makes the visible current-value lane a deliberate
  re-entry invariant on dynamic-stop loops, not a root-entry-only check:
  - [lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c)
- snapshot replay preserves slot identity rather than changing numeric-for slot
  meaning after entry:
  - [lj_snap.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_snap.c)
- VM numeric-for paths keep `FOR_IDX` as hidden control state and `FOR_EXT` as
  the visible mirror on every iteration:
  - [vm_s390x.dasc](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/vm_s390x.dasc)
- direct visible-current-value relaxations are already structural rejects,
  including the corrected hidden-anchored variant

That leaves no honest latch-shaped remediation unless a future proof shows a
different mechanism entirely.

## Next Target

The next honest target is not another visible-current-value relaxation. It is
to decide whether the remaining promoted-default gap on these families should
be treated as generic dynamic-stop numeric-for replay cost rather than an
s390x-specific bring-up seam.

## Non-Targets

Do not reopen:

- helper lookup/header theories
- `FORL` fastpath rematerialization
- once-per-replay visible-current-value latch variants
- blind no-typecheck variants
- low32-home
- iterator or dispatch families
