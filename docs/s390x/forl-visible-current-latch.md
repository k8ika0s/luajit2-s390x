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

So the next honest remediation lane is not "remove the guard". It is:

- determine whether the visible current-value type agreement is an
  entry-latch property of replayed `FORL`
- and, if so, prove it once per replay entry instead of re-checking the same
  lane every trip

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

## Next Proof

Open remediation again only if the reduced probes support this statement:

- visible `FORL_IDX` type agreement is an entry-latch property of replayed
  `FORL`, not a value that can drift mid-replay

The proof target is narrow:

1. reduced `number_helper_loop`
2. reduced `pure_add_reducer`

For the visible `FORL_IDX` lane, compare:

1. restored runtime tag/value at replay entry
2. runtime tag/value after the first traced loop-carried update
3. runtime tag/value at the next exit from that same replayed loop

Decision rule:

- if mismatch exists only at replay entry, reopen remediation as a
  once-per-replay latch/proof design
- if mismatch recurs after the replayed loop has already advanced and
  republished the visible current value, keep remediation closed

## Non-Targets

Do not reopen:

- helper lookup/header theories
- `FORL` fastpath rematerialization
- blind no-typecheck variants
- low32-home
- iterator or dispatch families
