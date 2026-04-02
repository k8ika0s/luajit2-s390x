# s390x Performance Status

Last updated: 2026-04-02 10:32:11 PDT

## Latest Matrix

These tables list the latest hot-path rows with matching JIT-on and `-joff`
artifacts. `Updated` is the timestamp of the artifact that produced the row.
Rows without a paired `-joff` restamp are intentionally left out of the top
matrix until they are backfilled.

## Active Shipping Throughput Slice

Treat the envless filtered hotside path as the active shipping-throughput
default for `promotion_core` only:

- active default:
  `hotside_canon_share_uget_looproot_default`
- explicit baseline / opt-out:
  `LUAJIT_S390X_DISABLE_HOTSIDE_CANON_SHARE_UGET_LOOPROOT=1`
- `promotion_secondary`: carry-forward evidence only
- `same_seam_but_dominated`: excluded
- frozen iterator / frozen dispatch: out of scope

Pinned host-pair summary:

- [20260402-hotside-promotion-core-host-pair](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-hotside-promotion-core-host-pair/summary.md)

### kdz

| Updated | Path | Surface | JIT-on | `-joff` | On/Off |
| --- | --- | --- | ---: | ---: | ---: |
| 2026-04-01 17:07:40 PDT | `add_phi_only/hot` | `hotside_canon_share_uget_looproot` | `0.000664` | `0.000020` | `33.20x` |
| 2026-04-01 14:32:40 PDT | `logic_add_phi_noboundary/hot` | `hotside_canon_share` | `0.002619` | `0.002159` | `1.21x` |
| 2026-04-01 19:56:12 PDT | `chain_tail_add/hot` | `hotside_canon_share_uget_looproot_default` | `0.003212` | `0.002104` | `1.53x` |
| 2026-04-01 19:56:12 PDT | `chain_tail_store/hot` | `hotside_canon_share_uget_looproot_default` | `0.002907` | `0.002036` | `1.43x` |
| 2026-04-01 19:56:12 PDT | `mix_bits/hot` | `hotside_canon_share_uget_looproot_default` | `0.003182` | `0.002086` | `1.53x` |
| 2026-04-01 20:20:45 PDT | `numeric_loop/hot` | `hotside_canon_share_uget_looproot_default` | `0.342594` | `0.002173` | `157.66x` |
| 2026-04-01 20:20:45 PDT | `side_exit_loop/hot` | `hotside_canon_share_uget_looproot_default` | `0.526504` | `0.004692` | `112.21x` |
| 2026-04-01 20:20:45 PDT | `hotexit_loop/hot` | `hotside_canon_share_uget_looproot_default` | `0.611632` | `0.005619` | `108.85x` |
| 2026-04-01 19:56:12 PDT | `be_pack_loop/hot` | `hotside_canon_share_uget_looproot_default` | `0.023744` | `0.018831` | `1.26x` |
| 2026-04-01 19:56:12 PDT | `number_helper_loop/hot` | `hotside_canon_share_uget_looproot_default` | `0.008341` | `0.002267` | `3.68x` |
| 2026-04-01 19:56:12 PDT | `direct_abs/hot` | `hotside_canon_share_uget_looproot_default` | `0.017982` | `0.010090` | `1.78x` |
| 2026-04-01 19:56:12 PDT | `stored_abs/hot` | `hotside_canon_share_uget_looproot_default` | `0.013056` | `0.006895` | `1.89x` |
| 2026-04-01 16:25:27 PDT | `mixed_width_loop/hot` | `hotside_canon_share_uget_looproot` | `0.027964` | `0.027969` | `1.00x` |
| 2026-04-01 16:25:27 PDT | `pair_loop/hot` | `hotside_canon_share_uget_looproot` | `0.137179` | `0.017319` | `7.92x` |
| 2026-04-01 17:53:04 PDT | `retconst_loop/hot` | `hotside_canon_share_uget_looproot` | `0.001660` | `0.000591` | `2.81x` |
| 2026-04-01 17:53:04 PDT | `retlast_loop/hot` | `hotside_canon_share_uget_looproot` | `0.003093` | `0.002025` | `1.53x` |
| 2026-04-01 16:25:27 PDT | `sum_loop/hot` | `hotside_canon_share_uget_looproot` | `0.675950` | `0.005150` | `131.25x` |
| 2026-04-01 16:25:27 PDT | `mixed_ffi_loop/hot` | `hotside_canon_share_uget_looproot` | `0.059215` | `0.012404` | `4.77x` |
| 2026-04-01 17:55:23 PDT | `mixed_loop/hot` | `hotside_canon_share_uget_looproot` | `0.036412` | `0.003764` | `9.67x` |
| 2026-04-01 20:14:22 PDT | `pairs_sum/hot` | `hotside_canon_share_uget_looproot_default` | `0.058992` | `0.005459` | `10.81x` |
| 2026-04-01 20:14:22 PDT | `pairs_array_sum/hot` | `hotside_canon_share_uget_looproot_default` | `0.064010` | `0.003679` | `17.40x` |

### zkd0

| Updated | Path | Surface | JIT-on | `-joff` | On/Off |
| --- | --- | --- | ---: | ---: | ---: |
| 2026-04-01 19:56:12 PDT | `chain_tail_add/hot` | `hotside_canon_share_uget_looproot_default` | `0.004231` | `0.002338` | `1.81x` |
| 2026-04-01 19:56:12 PDT | `chain_tail_store/hot` | `hotside_canon_share_uget_looproot_default` | `0.003796` | `0.002276` | `1.67x` |
| 2026-04-01 19:56:12 PDT | `mix_bits/hot` | `hotside_canon_share_uget_looproot_default` | `0.004088` | `0.003184` | `1.28x` |
| 2026-04-01 21:18:33 PDT | `numeric_loop/hot` | `hotside_canon_share_uget_looproot_default` | `0.777567` | `0.003257` | `238.72x` |
| 2026-04-01 21:18:33 PDT | `side_exit_loop/hot` | `hotside_canon_share_uget_looproot_default` | `1.299877` | `0.006691` | `194.28x` |
| 2026-04-01 21:18:33 PDT | `hotexit_loop/hot` | `hotside_canon_share_uget_looproot_default` | `1.586069` | `0.008171` | `194.11x` |
| 2026-04-01 19:56:12 PDT | `be_pack_loop/hot` | `hotside_canon_share_uget_looproot_default` | `0.031455` | `0.026039` | `1.21x` |
| 2026-04-01 19:56:12 PDT | `number_helper_loop/hot` | `hotside_canon_share_uget_looproot_default` | `0.012384` | `0.002873` | `4.31x` |
| 2026-04-01 19:56:12 PDT | `direct_abs/hot` | `hotside_canon_share_uget_looproot_default` | `0.024177` | `0.012092` | `2.00x` |
| 2026-04-01 19:56:12 PDT | `stored_abs/hot` | `hotside_canon_share_uget_looproot_default` | `0.016539` | `0.008667` | `1.91x` |
| 2026-04-01 16:39:41 PDT | `mixed_width_loop/hot` | `hotside_canon_share_uget_looproot` | `0.045325` | `0.045030` | `1.01x` |
| 2026-04-01 16:39:41 PDT | `pair_loop/hot` | `hotside_canon_share_uget_looproot` | `0.366784` | `0.024836` | `14.77x` |
| 2026-04-01 17:55:45 PDT | `retconst_loop/hot` | `hotside_canon_share_uget_looproot` | `0.003574` | `0.000849` | `4.21x` |
| 2026-04-01 17:55:45 PDT | `retlast_loop/hot` | `hotside_canon_share_uget_looproot` | `0.012619` | `0.003196` | `3.95x` |
| 2026-04-01 16:39:41 PDT | `sum_loop/hot` | `hotside_canon_share_uget_looproot` | `1.499405` | `0.005568` | `269.29x` |
| 2026-04-01 16:39:41 PDT | `mixed_ffi_loop/hot` | `hotside_canon_share_uget_looproot` | `0.081518` | `0.015637` | `5.21x` |
| 2026-04-01 17:59:38 PDT | `mixed_loop/hot` | `hotside_canon_share_uget_looproot` | `0.079668` | `0.006760` | `11.79x` |
| 2026-04-01 20:54:30 PDT | `pairs_sum/hot` | `hotside_canon_share_uget_looproot_default` | `0.082382` | `0.006774` | `12.16x` |
| 2026-04-01 20:54:30 PDT | `pairs_array_sum/hot` | `hotside_canon_share_uget_looproot_default` | `0.106179` | `0.005346` | `19.86x` |

## Scope

This page tracks the current native s390x performance state after the branch
was frozen into three lanes:

- Lane A: build and stability only
- Lane B: promotable recorder-side iterator perf only
- Lane C: parked bridge and continuation research only

Iterator is still frozen at the current Lane A + Lane B checkpoint unless a
genuinely new seam appears outside the reject pile. Dispatch loop-clone work
on the old mechanism stays closed. The current live hotside policy candidate is
the filtered gate:

- `LUAJIT_S390X_HOTSIDE_CANON_SHARE_UGET_LOOPROOT=1`

That gate is now clearly narrower than the old global
`LUAJIT_S390X_HOTSIDE_CANON_SHARE_EQUIV=1` surface:

- it still wins on the reduced `UGET`/looproot throughput family
- it is effectively inert on the plain `int_add_phi_only` control reproducer
- it no longer carries as a broader-suite promotion candidate on `kdz`
- it does not carry as a broader-suite promotion candidate on either host
- `zkd0` completes the same narrower read:
  - dispatch stays catastrophically far from `-joff`
  - helper-heavy and call-heavy families can improve sharply
  - `sum_loop` remains extremely red even when it improves
  - iterator is no longer the main regression driver there, but it is still
    far from `-joff`

So the active queue is no longer “broader gate promotion”. It is:

1. broader positive-family mechanism proof for
   `LUAJIT_S390X_HOTSIDE_CANON_SHARE_UGET_LOOPROOT=1`
2. confirm whether helper-heavy and call-heavy winners are still improving
   through the exact same reduced `UGET`/looproot seam
3. only after that, any wider promotion claim outside the filtered
   `UGET`/looproot mechanism

Frozen-family fence status is now helper-backed on both hosts:

- iterator:
  [baseline](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-201055-kdz-baseline-iterator-truth-pack/summary.md)
  vs
  [promoted default](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-201051-kdz-hotside_canon_share_uget_looproot_default-iterator-truth-pack/summary.md)
  - medians improve slightly while trace/exit shape is unchanged
- dispatch:
  [baseline](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-201441-kdz-baseline-dispatch-truth-pack/summary.md)
  vs
  [promoted default](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-201748-kdz-hotside_canon_share_uget_looproot_default-dispatch-truth-pack/summary.md)
  - medians are effectively flat and trace/exit shape is unchanged

- `zkd0` iterator:
  [baseline](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-204030-zkd0-baseline-iterator-truth-pack/summary.md)
  vs
  [promoted default](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-205106-zkd0-hotside_canon_share_uget_looproot_default-iterator-truth-pack/summary.md)
  - `pairs_sum/hot`: `0.115511 -> 0.082382`
  - `pairs_array_sum/hot`: `0.115958 -> 0.106179`
  - trace/exit shape is unchanged
- `zkd0` dispatch:
  [baseline](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-211042-zkd0-baseline-dispatch-truth-pack/summary.md)
  vs
  [promoted default](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-211442-zkd0-hotside_canon_share_uget_looproot_default-dispatch-truth-pack/summary.md)
  - `numeric_loop/hot`: `0.850875 -> 0.777567`
  - `side_exit_loop/hot`: `1.192460 -> 1.299877`
  - `hotexit_loop/hot`: `1.327162 -> 1.586069`
  - trace/exit shape is unchanged

So the envless promoted default now reads as a scoped throughput improvement,
not a frozen-family promotion candidate. It remains fenced out of iterator and
dispatch on both hosts.

Reduced host-pair mechanism proof for the remaining `promotion_core` red is now
in hand:

- [20260401-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-214311-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism/summary.md)
- [20260401-zkd0-hotside_canon_share_uget_looproot_default-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-214447-zkd0-hotside_canon_share_uget_looproot_default-core-exit-mechanism/summary.md)

That read is narrower than the older “helper-heavy” / “FFI-heavy” labels:

- `be_helpers` and `ffi_calls` both converge to the same dominant steady-state
  exit on both hosts: `trace 7 exit 0`
- `number_helper_loop` and `be_pack_loop` both spend `63457 / 64001` exits on
  that one site; the remaining split is mostly body size (`nins 28` vs `71`)
- `direct_abs` and `stored_abs` both spend `79457 / 80001` exits on that same
  site; the remaining split is also body size (`nins 33` vs `23`)
- focused reduced dump on clean `kdz` corrects the first attribution target:
  - [20260401-214848-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-214848-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism/summary.md)
  - [20260401-215124-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-215124-kdz-hotside_canon_share_uget_looproot_default-core-exit-mechanism/summary.md)
  - `number_helper_loop` hot loop has no call IR inside the traced body
  - `direct_abs` hot loop does keep `CALLXS`
- conclusion:
  - the filtered hotside default already removed the clone ladder on this
    slice
  - the front-most remaining payer is one stable loop exit, but the clean
    first attribution target is now `number_helper_loop`, because it removes
    the call-boundary complication that still exists in `direct_abs`

Reduced runtime exit attribution on clean `kdz` now pins that stable loop exit
to the same exact bytecode seam in both representative workloads:

- [20260401-kdz-core-exit-attribution-reduced](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-core-exit-attribution-reduced/summary.md)
  - `number_helper_loop`:
    - `TRACE_START 5`, `TRACE_STOP 5`, `TRACE_ABORT 0`, `TEXIT_COUNT 801`
    - dominant texit: `7:0` x `257`
    - runtime exit log: `trace 7 exit 0` resumes at `pc op=45`,
      `snapop=45`, `snapcount=0`
  - `direct_abs`:
    - `TRACE_START 5`, `TRACE_STOP 5`, `TRACE_ABORT 0`, `TEXIT_COUNT 801`
    - dominant texit: `7:0` x `257`
    - runtime exit log: `trace 7 exit 0` also resumes at `pc op=45`,
      `snapop=45`, `snapcount=0`
- `op=45` is `BC_UGET`, which matches the original filtered hotside seam in
  [src/lj_trace.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c)
- queue correction:
  - the promoted-default core red is not front-most call/FFI exit churn
  - the remaining steady seam is the full `SNAP #0` header-guard interval,
    with `BC_UGET` only marking the original helper form's restore point
  - on `number_helper_loop`, that is no longer described as a helper-only
    `bit.tobit` identity seam
  - reduced baseline comparison now closes the mechanism split:
    - [20260401-kdz-core-exit-attribution-baseline-reduced](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-core-exit-attribution-baseline-reduced/summary.md)
    - baseline and promoted default restore to the same `BC_UGET` header seam
    - only the dominant owning loop clone changes:
      - baseline: `trace 6 exit 0`
      - promoted default: `trace 7 exit 0`
    - the promoted default collapses the equivalent-parent ladder; it does not
      remove the restored `UGET bit -> TGETS "tobit"` header seam
  - focused first-clone proof now explains why the seam survives:
    - [20260401-kdz-core-exit-hotside-focus-parent1](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-core-exit-hotside-focus-parent1/summary.md)
    - on the first hot source (`parent=1 exit=0`), hotside sees:
      - `pc=snappc`
      - `op=snapop=BC_UGET`
      - `cand=0`
      - `child=0`
    - it simply counts that restored header seam to `hotexit` and starts the
      first side trace from the same header snapshot
    - the first clone therefore inherits the same loop shape as the root,
      rather than a deeper arithmetic-only body
  - source-backed start-point rule now confirms there is no generic deeper
    entry on the current mechanism:
    - [lj_snap_restore()](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_snap.c#L1196) returns the restored `snap_pc`
    - [lj_trace_exit()](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c#L3197) forwards that `pc` to
      [trace_hotside()](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c#L2941)
    - [trace_hotside()](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c#L3055) starts the side trace with `lj_trace_ins(J, pc)`
    - [lj_record_setup()](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c#L3833) then copies that `pc` into the new side
      trace's `startpc`
    - `resumepc` setup in [trace_save()](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_trace.c#L1902) only changes how an already-saved
      child later re-enters; it does not let the first clone start later than
      the restored snapshot
  - reduced exit attribution narrows the remaining red to the pre-snapshot
    header guard cluster:
    - [20260401-kdz-core-exit-attribution-reduced](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-core-exit-attribution-reduced/summary.md)
    - dominant `exit 0` reports:
      - `pc op=BC_UGET`
      - `snapop=BC_UGET`
      - `snapnent=0`
    - so the hot seam is still inside the full `SNAP #0` guard interval before
      any snapshot-carried state exists

Header variants on clean `kdz` now close the helper-only reading:

- [20260402-kdz-uget-header-variant-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-uget-header-variant-audit/summary.md)
  - original helper form:
    - restored hot seam starts at `BC_UGET` (`45`)
  - local/arg `tobit` form:
    - restored hot seam moves to `BC_MOV` (`18`)
  - pure-add reducer:
    - restored hot seam moves to `BC_MULVN` (`24`)
  - all three still keep the same reduced `exit 0` clone ladder
- queue correction:
  - the helper lookup/identity chain is not required to reproduce the flurry
  - the live family is generic `SNAP #0` header-guard failure, not a
    helper-only `BC_UGET/TGETS/HLOAD/fun EQ` seam

Guard-log intersection on the same reduced `kdz` scripts narrows the shared
candidate set:

- original helper `snap=0`: `curins=17,15,14,13,12,10,8,7,3,2`
- pure-add reducer `snap=0`: `curins=6,5,4,3,2`
- shared surviving guard kinds:
  - `sload_int`

Exact runtime guard attribution now sharpens that further:

- real workload:
  [20260402-kdz-number-helper-guardmark-attribution-v5](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-number-helper-guardmark-attribution-v5/summary.md)
  - dominant runtime failure on `trace 7 exit 0` is `curins=3`, `IR=SLOAD`
  - exact runtime guard:
    - `op1=4`
    - `op2=36`
    - `sload_int ofs=16 extra=20 cc=6`
- no-helper sibling:
  [20260402-kdz-number-helper-guardmark-attribution-v4](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-number-helper-guardmark-attribution-v4/raw/pure_add_reducer.stderr.log)
  - first repeated reduced exit cluster lands on `guardmark=0x3`
  - matching early guard log is the same inherited
    `sload_int curins=3 ofs=16 extra=20`
  - restored header marker moves to `BC_MULVN`, but the first exact runtime
    failure stays on the same inherited `sload_int`
- semantic meaning:
  - this is the first shared marked header guard, `IR=SLOAD #4 TI`
  - on this GC64 build, `op1=4` maps to top-frame slot `2`
  - on `number_helper_loop`, that slot is the numeric `for` index state
  - `op2=36` is `IRSLOAD_TYPECHECK|IRSLOAD_INHERIT`, so the guard is
    intentionally revalidated on exits and side traces
  - this is the hidden narrowed `FORL_IDX` reload created by `rec_for_loop()`,
    not the visible helper header or the carried accumulator
  - `snapnent=0` remains true on the dominant exit, so this guard is checking
    live interpreter frame state at restored `SNAP #0`
  - focused slot logging on the same reduced seam shows that restored slot is
    already int-tagged at the repeated exit point, so `guardmark=0x3` is not
    yet enough to prove this is the literal failing compare

So the current promoted-slice red is no longer best described as the
carried-`total` reload seam. But the exact first failing guard inside the
merged restored-`SNAP #0` numeric-`for` header cluster is still unresolved:
the leading marked seam is the inherited numeric-`for` index `sload_int`
guard (`ofs=16 extra=20`), while the later stop-bound `LE` on `n` remains a
live competing failure in the same cluster.

Shared `sload_int` attribution remains useful, but it is now explicitly
secondary:

- [20260402-kdz-number-helper-sload-attribution](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260402-kdz-number-helper-sload-attribution/summary.md)
  - dominant guard order:
    `25,22,20,17,15,14,13,12,10,8,7,3,2`
  - first shared `sload_int` on the real workload:
    - `curins=15`
    - `IR=SLOAD`
    - `op1=3`
    - `op2=4`
    - `ofs=8`
    - `extra=12`
    - semantic source: loop-carried `total`
  - later shared `sload_int`:
    - `curins=3`
    - `ofs=16`
    - `extra=20`
    - semantic source: numeric `for` index state

Current queue correction:

- the next live seam on the promoted slice is the earlier numeric-`for`
  header `LE` guard
- the carried-`total` reload stays relevant as the first shared `sload_int`
  seam across reducers, but it is no longer the front-most exact runtime
  failure
- do not reopen helper-header, low32-home, or generic hotside-population work
  from this result

x64 control status:

- there is still no checked-in mature x64 reduced runner for this seam
- an ad hoc Rosetta x64 path was tested on this workstation, but a direct
  `arch -x86_64 make -C src ...` still selected the arm64 VM build and failed
  in `vm_arm64.dasc`
- treat mature x64 reduced control as a tooling gap for now, not as a blocker
  on the s390x seam read

Exact reduced-family scope proof on clean `kdz` is now recorded here:

- [20260401-kdz-hotside-uget-looproot-scope-proof](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-uget-looproot-scope-proof/summary.md)
  - `int_add_phi_only`: `match_count 0`
  - `logical_chain_tail_add`: `match_count 8792`
  - `logical_chain_tail_store`: `match_count 8792`
  - `bitops_mix`: `match_count 8792`
  - all positive reduced hits stay on:
    - `exit=0`
    - `op=BC_UGET`
    - `startop=BC_JMP`
    - `root_startop in {BC_FORL, BC_FUNCF}`

Broader positive-family mechanism proof on clean `kdz` now says the helper and
FFI-call winners are improving through that same seam:

- [20260401-kdz-hotside-uget-looproot-broader-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-uget-looproot-broader-mechanism/summary.md)
  - `number_helper_loop`: `match_count 63858`
  - `be_pack_loop`: `match_count 63858`
  - `direct_abs`: `match_count 79858`
  - `stored_abs`: `match_count 79858`
  - every positive broader family still stayed on:
    - `exit=0`
    - `op=BC_UGET`
    - `startop=BC_JMP`
    - `root_startop=BC_FORL`

Representative host-pair confirmation now matches on `zkd0` too:

- [20260401-zkd0-hotside-uget-looproot-broader-mechanism-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-zkd0-hotside-uget-looproot-broader-mechanism-check/summary.md)
  - `number_helper_loop`: `match_count 63858`
  - `direct_abs`: `match_count 79858`
  - both representative broader winners still stayed on:
    - `exit=0`
    - `op=BC_UGET`
    - `startop=BC_JMP`
    - `root_startop=BC_FORL`

Current clean-`kdz` broader-throughput frontier:

- `vararg_paths` stays parked, not live
  - `sum_loop` remains classified as the normal caller-root stop into an
    already-compiled nested callee loop
  - no narrower recorder seam has been named before nested `BC_JFORI` entry
- broader-throughput observability is now corrected:
  - [tools/s390x/build_throughput_truth_pack.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/build_throughput_truth_pack.py)
    now parses `REMOTE_RC=...` correctly
  - [tests/s390x/helpers/testlib.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/helpers/testlib.lua)
    now exposes lightweight trace/texit counters so reduced trace validation
    does not wedge on hist bookkeeping
- corrected clean-`kdz` frontier:
  - [20260401-kdz-bitops_mix-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-bitops_mix-truth-pack)
    - `mix_bits/hot`: JIT-on `0.008267`, `-joff` `0.002084`, ratio `3.97x`
    - `REMOTE_RC 0`, `TRACE_START 41`, `TRACE_STOP 41`, `TRACE_ABORT 0`,
      `TEXIT_COUNT 7981`
    - classification: `exit-dominated`
  - [20260401-kdz-logical_chain_tail_add-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-logical_chain_tail_add-truth-pack)
    - `chain_tail_add/hot`: JIT-on `0.008269`, `-joff` `0.002123`, ratio
      `3.89x`
    - `REMOTE_RC 0`, `TRACE_START 41`, `TRACE_STOP 41`, `TRACE_ABORT 0`,
      `TEXIT_COUNT 7981`
    - classification: `exit-dominated`
  - [20260401-kdz-logical_chain_tail_store-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-logical_chain_tail_store-truth-pack)
    - `chain_tail_store/hot`: JIT-on `0.006676`, `-joff` `0.002319`, ratio
      `2.88x`
    - `REMOTE_RC 0`, `TRACE_START 42`, `TRACE_STOP 42`, `TRACE_ABORT 0`,
      `TEXIT_COUNT 7983`
    - classification: `exit-dominated`
  - [20260401-kdz-int_add_phi_only-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-int_add_phi_only-truth-pack)
    - `add_phi_only/hot`: JIT-on `0.000672`, `-joff` `0.000020`, ratio
      `33.60x`
    - `REMOTE_RC 0`, `TRACE_START 20`, `TRACE_STOP 20`, `TRACE_ABORT 0`,
      `TEXIT_COUNT 4001`
    - classification: `exit-dominated`
  - [20260401-kdz-logic_add_phi_noboundary-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-logic_add_phi_noboundary-truth-pack)
    - `logic_add_phi_noboundary/hot`: JIT-on `0.003527`, `-joff` `0.002153`,
      ratio `1.64x`
    - `REMOTE_RC 0`, `TRACE_START 23`, `TRACE_STOP 21`, `TRACE_ABORT 2`,
      `TEXIT_COUNT 4001`
    - classification: `exit-dominated`
- queue correction:
  - `bitops_mix` is no longer an honest compiled-body / low32-home frontier on
    the current evidence
  - the low32-home / normalized-result contract note is parked design context,
    not the active main-line queue:
    [docs/s390x/low32-home-contract.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/low32-home-contract.md)
  - the live target is now a narrower hotside policy family on that generic
    throughput loop-clone surface:
    - `int_add_phi_only` as the smallest reproducer
    - `logical_chain_tail_add` as the value-tail sibling
    - `bitops_mix` as the larger mixed logic/add reproducer
  - do not reopen low32-home prototype work until a finite compiled-body family
    exists again under the corrected validator
- first real policy candidate on that family:
  - `LUAJIT_S390X_HOTSIDE_CANON_EQUIV=1` plus
    `LUAJIT_S390X_HOTSIDE_SHARE_EQUIV=1`
  - smallest reproducer artifact:
    [20260401-kdz-hotside-share-equiv-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-share-equiv-audit/summary.md)
  - `int_add_phi_only`:
    - baseline `hot 0.000786`, `TRACE_START 21`, `TEXIT_COUNT 4001`,
      `TRACEINFO_COUNT 27`
    - `SHARE_EQUIV` alone `hot 0.000659`, `TRACE_START 100`,
      `TEXIT_COUNT 300`, `TRACEINFO_COUNT 106`
    - `CANON_EQUIV + SHARE_EQUIV` `hot 0.000341`, `TRACE_START 3`,
      `TEXIT_COUNT 4001`, `TRACEINFO_COUNT 9`
    - `CANON_CHILD + SHARE_EQUIV` `hot 0.000407`, `TRACE_START 4`,
      `TEXIT_COUNT 4001`, `TRACEINFO_COUNT 10`
  - focused read:
    - `SHARE_EQUIV` alone wins by accelerating new trace formation
    - the combined canon/share policy wins differently: it collapses actual
      trace population while preserving the throughput gain
  - mechanism artifact:
    [20260401-kdz-hotside-canon-share-mechanism](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-mechanism/summary.md)
  - mechanism read:
    - `SHARE_EQUIV` alone still reaches late `parent=24 exit=0` and primes
      that late parent straight to `hotexit - 1`, then immediately starts a
      new trace there
    - the combined canon/share policy does not lower total exits on this
      reproducer
    - instead, the warmed measured run no longer reaches `parent=24` at all
    - it keeps paying the same repeated `exit 0` seam on early `parent=4`,
      which is why `TEXIT_COUNT` stays flat while trace population collapses
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
  - `logical_chain_tail_add`: baseline `0.018642`, candidate `0.005414`
  - `bitops_mix`: baseline `0.009459`, candidate `0.004000`
  - after tracked-file resync and rebuild, structural counts match `kdz`:
    `TRACE_START 41 -> 2`, `TEXIT_COUNT 7981 -> 8000`
- dedicated single-gate promotion:
  - `LUAJIT_S390X_HOTSIDE_CANON_SHARE_EQUIV=1`
  - clean `kdz` check:
    [20260401-kdz-hotside-canon-share-gate-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-gate-check/summary.md)
    - `int_add_phi_only`: `hot 0.000349`, `TRACE_START 3`, `TEXIT_COUNT 4001`
    - `logical_chain_tail_add`: `hot 0.003015`, `TRACE_START 2`, `TEXIT_COUNT 8000`
    - `bitops_mix`: `hot 0.003064`, `TRACE_START 2`, `TEXIT_COUNT 8000`
  - `zkd0` screen:
    [20260401-zkd0-hotside-canon-share-gate-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-zkd0-hotside-canon-share-gate-check/summary.md)
    - `logical_chain_tail_add`: `hot 0.003333`, `TRACE_START 2`, `TEXIT_COUNT 8000`
    - `bitops_mix`: `hot 0.004170`, `TRACE_START 2`, `TEXIT_COUNT 8000`
- queue correction:
  - the root-cause question is answered on this mechanism
  - the live candidate surface is now the dedicated single gate, not the old
    ad hoc env pair
  - helper integration is now complete in
    [tools/s390x/build_throughput_truth_pack.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/build_throughput_truth_pack.py)
    via `--candidate hotside_canon_share`
  - helper-backed `kdz` candidate artifacts:
    - [20260401-kdz-int_add_phi_only-hotside_canon_share-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-int_add_phi_only-hotside_canon_share-truth-pack)
      - `add_phi_only/hot`: JIT-on `0.000350`, `-joff` `0.000032`,
        `TRACE_START 3`, `TEXIT_COUNT 4001`
    - [20260401-kdz-logic_add_phi_noboundary-hotside_canon_share-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-logic_add_phi_noboundary-hotside_canon_share-truth-pack)
      - `logic_add_phi_noboundary/hot`: JIT-on `0.002619`, `-joff`
        `0.002159`, `TRACE_START 5`, `TRACE_ABORT 2`, `TEXIT_COUNT 4001`
    - [20260401-kdz-logical_chain_tail_add-hotside_canon_share-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-logical_chain_tail_add-hotside_canon_share-truth-pack)
      - `chain_tail_add/hot`: JIT-on `0.003052`, `-joff` `0.002541`,
        `TRACE_START 2`, `TEXIT_COUNT 8000`
    - [20260401-kdz-logical_chain_tail_store-hotside_canon_share-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-logical_chain_tail_store-hotside_canon_share-truth-pack)
      - `chain_tail_store/hot`: JIT-on `0.002800`, `-joff` `0.002016`,
        `TRACE_START 2`, `TEXIT_COUNT 8000`
    - [20260401-kdz-bitops_mix-hotside_canon_share-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-bitops_mix-hotside_canon_share-truth-pack)
      - `mix_bits/hot`: JIT-on `0.003084`, `-joff` `0.002168`,
        `TRACE_START 2`, `TEXIT_COUNT 8000`
  - helper-backed `zkd0` screen:
    - [20260401-zkd0-logical_chain_tail_add-baseline-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-zkd0-logical_chain_tail_add-baseline-truth-pack)
      - baseline `chain_tail_add/hot`: JIT-on `0.016437`,
        `TRACE_START 41`, `TEXIT_COUNT 7981`
    - [20260401-zkd0-logical_chain_tail_add-hotside_canon_share-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-zkd0-logical_chain_tail_add-hotside_canon_share-truth-pack)
      - candidate `chain_tail_add/hot`: JIT-on `0.003722`,
        `-joff 0.003899`, `TRACE_START 2`, `TEXIT_COUNT 8000`
    - [20260401-zkd0-bitops_mix-hotside_canon_share-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-zkd0-bitops_mix-hotside_canon_share-truth-pack)
      - `mix_bits/hot`: JIT-on `0.004230`, `-joff` `0.002933`,
        `TRACE_START 2`, `TEXIT_COUNT 8000`
    - [20260401-zkd0-logical_chain_tail_store-baseline-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-zkd0-logical_chain_tail_store-baseline-truth-pack)
      - baseline `chain_tail_store/hot`: JIT-on `0.009260`,
        `TRACE_START 42`, `TEXIT_COUNT 7983`
    - [20260401-zkd0-logical_chain_tail_store-hotside_canon_share-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-zkd0-logical_chain_tail_store-hotside_canon_share-truth-pack)
      - candidate `chain_tail_store/hot`: JIT-on `0.003955`,
        `TRACE_START 2`, `TEXIT_COUNT 8000`
  - queue correction:
    - the dedicated gate now covers every active reduced throughput surface in
      the current queue with helper-backed evidence
    - broader non-reduced screens now also hold:
      - `kdz`:
        - [20260401-kdz-hotside-canon-share-broader-suite-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-broader-suite-check/summary.md)
        - [20260401-kdz-hotside-canon-share-ffi-screen](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-ffi-screen/summary.md)
        - [20260401-kdz-hotside-canon-share-ffi-cdata-rerun](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-ffi-cdata-rerun/summary.md)
        - [20260401-kdz-hotside-canon-share-promotion-scope-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-promotion-scope-check/summary.md)
      - `zkd0`:
        - [20260401-zkd0-hotside-canon-share-broader-suite-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-zkd0-hotside-canon-share-broader-suite-check/summary.md)
        - [20260401-zkd0-hotside-canon-share-promotion-scope-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-zkd0-hotside-canon-share-promotion-scope-check/summary.md)
    - the remaining off-family caveat is gone:
      - `ffi_cdata/mixed_width_loop` on `kdz` reran from
        `0.028053 -> 0.027835`
    - the promotion boundary is now explicit:
      - broader throughput families win heavily on both hosts
      - `iterator_table` regresses on both hosts
      - so `LUAJIT_S390X_HOTSIDE_CANON_SHARE_EQUIV=1` is a broader throughput
        candidate, not a safe global default
    - first selective-scope retries are now rejected on `kdz`:
      - exact loop-clone seam only:
        [20260401-kdz-hotside-canon-share-loop0-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-loop0-check/summary.md)
        - throughput still wins, iterator still regresses
      - loop-clone seam plus root-`ITERN` exclusion:
        [20260401-kdz-hotside-canon-share-loop0-noitern-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-loop0-noitern-check/summary.md)
        - throughput still wins, iterator still regresses
      - fast rerun after removing avoidable no-op overhead:
        [20260401-kdz-hotside-canon-share-loop0-noitern-fastcheck](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-loop0-noitern-fastcheck/summary.md)
        - `chain_tail_add/hot`: `0.007930 -> 0.003045`
        - `pairs_sum/hot`: `0.064785 -> 0.072845`
        - `pairs_array_sum/hot`: `0.068692 -> 0.076979`
    - deeper selective activation is now proven and the current active
      candidate is filtered rather than global:
      - exact throughput activation proof:
        [20260401-kdz-hotside-throughput-shape-proof](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-throughput-shape-proof/summary.md)
        - actual canon/share hits sit on:
          - `exit=0`
          - `startop=BC_JMP`
          - `pcop=snapop=BC_UGET`
          - `root_startop=BC_FORL` or `BC_FUNCF`
      - exact iterator non-hit proof:
        [20260401-kdz-hotside-canon-share-iterator-bench-proof](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-iterator-bench-proof/summary.md)
        - full `iterator_table` shows zero actual canon/share hits under the
          old dedicated gate
      - filtered gate:
        - `LUAJIT_S390X_HOTSIDE_CANON_SHARE_UGET_LOOPROOT=1`
        - first inner-only filter still left iterator slightly red because
          the remaining cost was no-op check overhead, not bad rewrites
        - match proof:
          [20260401-kdz-hotside-uget-looproot-match-proof/logical_chain_tail_add.summary.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-uget-looproot-match-proof/logical_chain_tail_add.summary.md)
          and
          [20260401-kdz-hotside-uget-looproot-match-proof/iterator_table.summary.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-uget-looproot-match-proof/iterator_table.summary.md)
      - hoisted prefilter retry is the first selective gate that keeps the
        throughput win without iterator fallout:
        - `kdz`:
          [20260401-kdz-hotside-canon-share-uget-looproot-perf](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-canon-share-uget-looproot-perf/summary.md)
          - `logical_chain_tail_add/hot`: `0.003130`
          - `bitops_mix/hot`: `0.003232`
          - `pairs_sum/hot`: `0.059845`
          - `pairs_array_sum/hot`: `0.061876`
        - `zkd0`:
          [20260401-zkd0-hotside-canon-share-uget-looproot-perf](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-zkd0-hotside-canon-share-uget-looproot-perf/summary.md)
          - `logical_chain_tail_add/hot`: `0.004477`
          - `bitops_mix/hot`: `0.003607`
          - `pairs_sum/hot`: `0.090623`
          - `pairs_array_sum/hot`: `0.102340`
    - queue correction:
      - the active candidate is now the filtered gate, not the global
        `LUAJIT_S390X_HOTSIDE_CANON_SHARE_EQUIV=1` surface
      - helper support now exists via
        [build_throughput_truth_pack.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/build_throughput_truth_pack.py)
        as `--candidate hotside_canon_share_uget_looproot`
      - but the first clean `kdz` broader-suite restamp closes broad
        promotion for this gate:
        [20260401-kdz-hotside-uget-looproot-broader-suite-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-uget-looproot-broader-suite-check/summary.md)
        - `dispatch_trace/numeric_loop`: `0.341080 -> 0.340611`
        - `mixed_ffi/mixed_ffi_loop`: `0.059399 -> 0.059215`
        - `ffi_cdata/mixed_width_loop`: `0.027778 -> 0.027964`
        - `vararg_paths/sum_loop`: `1.207311 -> 0.675950`, still far from
          `-joff 0.005150`
        - `iterator_table` still regresses:
          - `pairs_sum/hot`: `0.068483 -> 0.076181`
          - `pairs_array_sum/hot`: `0.066698 -> 0.084550`
      - broader scope proof now makes the promotion boundary explicit on
        clean `kdz`:
        [20260401-kdz-hotside-uget-looproot-promotion-scope](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-hotside-uget-looproot-promotion-scope/summary.md)
        - same-seam positives:
          - `retconst_loop`: `match_count 15858`
          - `retlast_loop`: `match_count 15858`
          - `sum_loop`: `match_count 15858`
          - `mixed_loop`: `match_count 15858`
        - zero-hit non-targets:
          - `mixed_ffi_loop`: `match_count 0`
          - `pair_loop`: `match_count 0`
          - `mixed_width_loop`: `match_count 0`
          - `pairs_sum`: `match_count 0`
          - `pairs_array_sum`: `match_count 0`
      - representative `zkd0` confirmation matches:
        [20260401-zkd0-hotside-uget-looproot-promotion-scope-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-zkd0-hotside-uget-looproot-promotion-scope-check/summary.md)
        - `retconst_loop`: `match_count 15858`
        - `mixed_loop`: `match_count 15858`
        - `mixed_ffi_loop`: `match_count 0`
        - `pairs_sum`: `match_count 0`
      - promotion scope is now:
        - in-scope candidate slice:
          - reduced `UGET`/looproot siblings
          - `be_helpers`
          - `ffi_calls`
          - `retconst_loop`
          - `retlast_loop`
          - `mixed_loop`
        - same-seam but not promotion evidence:
          - `sum_loop`
            - same seam hit, but still dominated by the parked nested-callee
              vararg frontier
        - out of scope on the current mechanism:
          - `dispatch_trace`
          - `iterator_table`
          - `mixed_ffi`
          - `ffi_cdata`
          - `int_add_phi_only`
      - that scope is now also codified in
        [build_throughput_truth_pack.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/build_throughput_truth_pack.py)
        so helper-backed candidate summaries report:
        - `promotion_core`
        - `promotion_secondary`
        - `same_seam_but_dominated`
        - `out_of_scope`
      - helper-backed host-pair truth packs now cover the full non-dominated
        scoped slice, which now splits cleanly into:
        - `promotion_core`
          - reduced `UGET`/looproot siblings
          - `be_helpers`
          - `ffi_calls`
        - `promotion_secondary`
          - `retconst_loop`
          - `retlast_loop`
          - `mixed_loop`
      - the secondary slice now has enough baseline/candidate A/B to stay
        explicitly secondary instead of floating as generic same-seam evidence:
        - `kdz`
          - `retlast_loop/hot`: `0.029461 -> 0.003093`
          - `retconst_loop/hot`: `0.028523 -> 0.001660`
          - `mixed_loop/hot`: `0.084383 -> 0.036412`
          - `sum_loop/hot`: `1.123796 -> 0.675104`, still dominated
        - `zkd0`
          - `retlast_loop/hot`: `0.052306 -> 0.012619`
          - `retconst_loop/hot`: `0.063160 -> 0.003574`
          - `mixed_loop/hot`: `0.189387 -> 0.079668`
          - `sum_loop/hot`: `2.894826 -> 2.985526`, still dominated and not
            promotion evidence
        - `zkd0` vararg baseline numbers above come from the completed raw
          benchmark logs while the helper summary is still pending:
          - [jit-on](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-zkd0-vararg_paths-baseline-truth-pack/raw/jit-on.stdout.log)
          - [joff](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-zkd0-vararg_paths-baseline-truth-pack/raw/joff.stdout.log)
        - `kdz`
          - [20260401-kdz-be_helpers-hotside_canon_share_uget_looproot-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-be_helpers-hotside_canon_share_uget_looproot-truth-pack/summary.md)
            - `number_helper_loop/hot`: `0.008169` vs `-joff 0.002285`,
              `TRACE_START 6`, `TEXIT_COUNT 64001`
            - `be_pack_loop/hot`: `0.023346` vs `-joff 0.018789`,
              `TRACE_START 6`, `TEXIT_COUNT 64001`
          - [20260401-kdz-ffi_calls-hotside_canon_share_uget_looproot-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-ffi_calls-hotside_canon_share_uget_looproot-truth-pack/summary.md)
            - `direct_abs/hot`: `0.018044` vs `-joff 0.009995`,
              `TRACE_START 5`, `TEXIT_COUNT 80001`
            - `stored_abs/hot`: `0.012581` vs `-joff 0.006919`,
              `TRACE_START 5`, `TEXIT_COUNT 80001`
          - [20260401-kdz-vararg_paths-hotside_canon_share_uget_looproot-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-vararg_paths-hotside_canon_share_uget_looproot-truth-pack/summary.md)
            - `retlast_loop/hot`: `0.003093` vs `-joff 0.002025`, `TRACE_START 5`
            - `retconst_loop/hot`: `0.001660` vs `-joff 0.000591`, `TRACE_START 5`
            - `sum_loop/hot`: `0.675104` vs `-joff 0.004523`, still
              `same_seam_but_dominated`
          - [20260401-kdz-mixed_noffi-hotside_canon_share_uget_looproot-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-mixed_noffi-hotside_canon_share_uget_looproot-truth-pack/summary.md)
            - `mixed_loop/hot`: `0.036412` vs `-joff 0.003764`,
              `TRACE_START 102`, `TEXIT_COUNT 195722`
        - `zkd0`
          - [20260401-zkd0-be_helpers-hotside_canon_share_uget_looproot-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-zkd0-be_helpers-hotside_canon_share_uget_looproot-truth-pack/summary.md)
            - `number_helper_loop/hot`: `0.015573` vs `-joff 0.003520`,
              `TRACE_START 6`, `TEXIT_COUNT 64001`
            - `be_pack_loop/hot`: `0.051882` vs `-joff 0.039644`,
              `TRACE_START 6`, `TEXIT_COUNT 64001`
          - [20260401-zkd0-ffi_calls-hotside_canon_share_uget_looproot-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-zkd0-ffi_calls-hotside_canon_share_uget_looproot-truth-pack/summary.md)
            - `direct_abs/hot`: `0.039104` vs `-joff 0.024293`,
              `TRACE_START 5`, `TEXIT_COUNT 80001`
            - `stored_abs/hot`: `0.041040` vs `-joff 0.019167`,
              `TRACE_START 5`, `TEXIT_COUNT 80001`
          - [20260401-zkd0-vararg_paths-hotside_canon_share_uget_looproot-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-zkd0-vararg_paths-hotside_canon_share_uget_looproot-truth-pack/summary.md)
            - `retlast_loop/hot`: `0.012619` vs `-joff 0.003196`, `TRACE_START 5`
            - `retconst_loop/hot`: `0.003574` vs `-joff 0.000849`, `TRACE_START 5`
            - `sum_loop/hot`: `2.985526` vs `-joff 0.007298`, still
              `same_seam_but_dominated`
          - [20260401-zkd0-mixed_noffi-hotside_canon_share_uget_looproot-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-zkd0-mixed_noffi-hotside_canon_share_uget_looproot-truth-pack/summary.md)
            - `mixed_loop/hot`: `0.079668` vs `-joff 0.006760`,
              `TRACE_START 102`, `TEXIT_COUNT 195722`
      - promotion decision:
        - treat the envless filtered seam as the active promoted/default
          scoped throughput surface, with
          `LUAJIT_S390X_HOTSIDE_CANON_SHARE_UGET_LOOPROOT=1` kept as a
          compatibility alias, for:
          - `promotion_core` workloads as the immediate promotion surface
          - `promotion_secondary` workloads as carry-forward same-seam evidence,
            not the first promotion bar
        - keep `sum_loop` out of promotion evidence
        - keep `dispatch_trace`, `iterator_table`, `mixed_ffi`, `ffi_cdata`,
          `int_add_phi_only`, and `logic_add_phi_noboundary` out of this
          candidate surface
      - the next honest target is no longer deciding whether this filtered
        gate has a promotable slice or filling host-pair gaps inside it
      - it is selective promotion planning from this helper-backed split:
        core promotion first, secondary same-seam carry-forward second
      - that now means:
        - promote only `promotion_core` on the first surface
        - keep `promotion_secondary` as documented same-seam carry-forward
          evidence, not the first enable set
      - checked-in boundary:
        - [hotside-uget-looproot-promotion.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/hotside-uget-looproot-promotion.md)
          now pins the first enable set and deferred slices explicitly
      - next honest target:
        - broader rollout criteria from that note, now that
          [build_hotside_promotion_slice.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/build_hotside_promotion_slice.py),
          has completed the full host-pair wave on the envless promoted default
      - runner-backed read:
        - all core families still stamp `promotion_core`
        - all core families still stamp `eligible_first_enable_set`
        - all runner-produced reduced trace probes completed with
          `REMOTE_RC 0` on both hosts
- first invariant-driven reduced-probe gate is now a clean `kdz` reject:
  - artifact:
    [20260401-kdz-low32home-add-boundary-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-low32home-add-boundary-check/summary.md)
  - gate shape:
    - keep current 64-bit logical lowering
    - skip producer-side `asm_bnorm32()` only for proven logical-chain carry
      nodes and `ADD`-tail nodes
    - normalize explicitly at the `ADD` consumer boundary
  - host result:
    - `logical_chain_tail_add`: baseline `0.008265`, gated `0.008807`,
      regression `+0.000542s` (`1.066x`)
    - `logical_chain_tail_store`: baseline `0.006822`, gated `0.007940`,
      regression `+0.001118s` (`1.164x`)
  - structural result:
    - the gate did fire:
      - `add-boundary:add-tail`: `46`
      - `skip-bnorm:add-tail`: `46`
      - `skip-bnorm:carry`: `483` on add-tail, `487` on store-tail
    - logged `asm_bnorm32()` totals dropped from:
      - add-tail `991 -> 439`
      - store-tail `994 -> 489`
    - but both reduced trace probes timed out with `REMOTE_RC=124`
  - result:
    - source returned to the non-behavior baseline after the host check
    - this exact consumer-boundary low32-home gate is closed
    - if `bitops_mix` stays open, the next honest target is not another
      partial `ADD`/tail gate
    - it has to be a fuller stateful low32-home / normalized-result contract,
      or the family should close too
- next classifier read on clean `kdz`:
  - artifact:
    [20260401-kdz-addhome-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-addhome-audit/summary.md)
  - `logical_chain_tail_add`:
    - the first live low32-home carry seam is plain non-guard integer `ADD`,
      not the earlier “normalize at `ADD` boundary” shape
    - `70` `ADD` sites matched the carry shape:
      - `46` with the current bitop result as the only low32-home source
      - `24` with both the carried total and current bitop result in the same
        low32-home carry family
    - those candidates only feed `PHI` / later plain `ADD`
  - `logical_chain_tail_store`:
    - no `ADD` site matched that carry shape
    - the bitop chain still first leaves into `ASTORE`
  - classifier note:
    - the verbose `LUAJIT_S390X_ADDHOME_LOG=1` trace probes timed out with
      `REMOTE_RC=124`, so this is structural attribution, not a perf result
  - queue correction:
    - the next backend family, if opened, is a fuller low32-home carry across
      plain non-guard integer `ADD` plus `PHI`
    - `ASTORE`, `LE`/guard, helper, and other noncarry consumers remain hard
      boundaries
- first host check on that `ADD`/`PHI` carry gate:
  - artifact:
    [20260401-kdz-low32home-addphi-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-low32home-addphi-check/summary.md)
  - gated medians:
    - `logical_chain_tail_add`: `0.007441`
    - `logical_chain_tail_store`: `0.006737`
  - structural gate:
    - proof scripts finished cleanly and the gate fired at the targeted seam:
      - `proof_add`: `REMOTE_RC=0`, `skip_count=2`
      - `proof_store`: `REMOTE_RC=0`, `skip_count=0`
    - both reduced trace probes timed out on clean `kdz`:
      - `chain_tail_add`: `REMOTE_RC=124`
      - `chain_tail_store`: `REMOTE_RC=124`
  - result:
    - this exact low32-home `ADD`/`PHI` carry gate is not promotable
    - source returned to the non-behavior baseline after the host check
    - the backend line now either needs a fuller stateful low32-home /
      normalized-result contract that preserves finite trace behavior and
      normalizes before guard/compare, helper/call, store, and
      snapshot-visible boundaries, or it should close too
  - design map from the lowering read:
    - safe internal family only:
      - bitop logic/unary/shift/rotate
      - plain non-guard integer `ADD`
      - loop `PHI` whose incoming arms stay inside that family
    - hard boundaries remain:
      - guard/compare
      - helper/call
      - store consumers such as `ASTORE`
      - snapshot-visible exits/restores
- next structural classifier on clean `kdz`:
  - artifact:
    [20260401-kdz-low32home-boundary-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-low32home-boundary-audit/summary.md)
  - `logical_chain_tail_add`:
    - `can_carry=1`: `1039`
    - first hard consumer split:
      - `guard`: `65`
      - `other`: `29`
      - `store`: `0`
  - `logical_chain_tail_store`:
    - `can_carry=1`: `898`
    - first hard consumer split:
      - `store`: `68`
      - `guard`: `45`
      - `other`: `25`
  - `bitops_mix`:
    - treat the live benchmark family as matching the add-tail seam rather
      than the store-tail seam
  - result:
    - the current compiled-body red is add/guard-boundary dominated
    - store-tail is a real separate boundary family, but it is not the main
      `bitops_mix` seam
    - if this backend line stays open, the next honest target is low32-home
      consumption at the compare/guard boundary, not another store-tail or
      add-only gate
- reduced clean `kdz` compare-boundary check:
  - artifact:
    [20260401-kdz-low32cmp-add-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-low32cmp-add-check/summary.md)
  - reduced add-tail probe:
    - `BUILD_RC=0`
    - `RUN_RC=0`
    - `RESULT 1746150614`
  - compare consumers:
    - total: `19`
    - `LE`: `14`
    - `NE`: `5`
    - `intcomp`: `14`
    - `equal`: `5`
    - left source is always carried `ADD`: `19`
    - right source is always constant: `19`
    - unsigned compare path is unused: `cmp32u=0`
    - hot compare path is the signed immediate path:
      - `imm16_signed=1`: `14`
      - `imm16_signed=0`: `5`
  - current lowering match:
    - `asm_intcomp()` signed-immediate compare lowers through `CGHI`
    - `asm_equal()` remaining equality path lowers through `CGR`
    - `src/lj_emit_s390x.h` only exposes the 64-bit compare forms used here:
      `CGR`, `CLGR`, `CGHI`
    - there is no pre-existing 32-bit compare-consumer path already wired in
  - result:
    - the active `bitops_mix` boundary is not a generic guard frontier
    - it is specifically carried `ADD` into signed immediate `LE`, with
      constant `NE` equality as a secondary boundary
    - the next honest backend target is consumption at that exact compare
      boundary, not another store-tail or broad low32-home skip variant
    - if the family stays open, the next real code branch is emitter plus
      backend compare-consumer design, not another local skip gate
- corrected clean `kdz` add-kind proof:
  - artifact:
    [20260401-kdz-low32cmp-addkind-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-low32cmp-addkind-check/summary.md)
  - same compare counts:
    - total `19`
    - `LE`: `14`
    - `NE`: `5`
    - left source `ADD`: `19`
    - right source constant: `19`
  - decisive split:
    - left add kind `ctrl_inc`: `19`
    - left add kind `bitop_tail`: `0`
  - reduced `-jdump=is` proof on clean `kdz` matches that:
    - `LE` is the induction increment compare `i + 1 <= 200`
    - `NE` is the induction zero-check in the traced `arshift` path
    - the carried value path remains separate as `ADD total, bitop_chain`
      followed by loop `PHI`
  - result:
    - the compare boundary is a loop-control seam, not the carried bitop value
      seam
    - close compare-consumer work for `bitops_mix` on the current mechanism
    - the next honest backend target is the value-tail `ADD` plus `PHI`
      boundary only, explicitly excluding the control-increment compare path
- narrowed value-tail `ADD` / `PHI` gate check on clean `kdz`:
  - artifact:
    [20260401-kdz-low32valueaddphi-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-low32valueaddphi-check/summary.md)
  - gate:
    - `LUAJIT_S390X_LOW32VALUEADDPHI=1`
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
    - if the backend family stays open, the next honest target is not another
      producer-side skip and not another compare-consumer branch
    - it is a deeper normalized-result / low32-home contract that remains
      finite under real trace formation

First broader-throughput family read from clean `kdz`:

- artifact root:
  - [20260331-kdz-vararg_paths-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260331-kdz-vararg_paths-truth-pack)
- current raw medians:
  - `sum_loop/hot`
    - JIT-on `1.109134`
    - `-joff` `0.004543`
    - gap `+1.104591s`
    - ratio `244.14x`
  - `retlast_loop/hot`
    - JIT-on `0.029367`
    - `-joff` `0.001993`
    - gap `+0.027374s`
    - ratio `14.74x`
  - `retconst_loop/hot`
    - JIT-on `0.028397`
    - `-joff` `0.000561`
    - gap `+0.027836s`
    - ratio `50.62x`
- focused hot-only medians:
  - `sum_loop/hot`
    - JIT-on `0.459993`
    - `-joff` `0.004453`
    - ratio `103.30x`
  - `retlast_loop/hot`
    - JIT-on `0.029780`
    - `-joff` `0.002047`
    - ratio `14.55x`
  - `retconst_loop/hot`
    - JIT-on `0.027820`
    - `-joff` `0.000574`
    - ratio `48.47x`
- current interpretation:
  - `vararg_paths` is a real broader-throughput red family on the current
    tree, not a mild widening check
  - `sum_loop` is the front-most hot case by a wide margin
  - the branch should not widen farther into `bitops_mix` before naming the
    traced hot vararg seam first
- exact next tasks on this family:
  1. isolate traced hot `sum_loop` on clean `kdz`
  2. compare that path against `retlast_loop` and `retconst_loop`
  3. classify the extra red as:
     - `select()` control,
     - vararg value/materialization,
     - or exit-heavy traced hot flow

That first contrast is now partially answered:

- `retlast_loop` and `retconst_loop` both show the same base loop-clone
  pattern on `kdz`
- `sum_loop` is different in kind, not just in degree:
  - it forms the inner traced vararg loop
  - then adds caller-side handoff / return traces back into the outer loop
- so the next live seam is:
  - nested `sum(...)` vararg scan plus caller return/handoff
  - not generic vararg throughput
  - and not the already-shared base loop-clone behavior by itself

That seam is narrower again after reduced clean-host handoff probes:

- the extra `sum_loop` red is not coming from a reopened lower-frame return
  path
- reduced `kdz` `LUAJIT_S390X_RECRET_LOG=1` probes show:
  - all three workloads return through `lua_intrace_return`
  - none of them hit `lua_lower_frame_retf`
  - none of them hit `lua_root_lower_frame_lleave`
- what singles `sum_loop` out is the extra trace family:
  - `sum_loop` forms an inner callee loop family, then a separate caller
    handoff family (`TRACE 2`, later `TRACE 7`) linking back into it
  - `retlast_loop` and `retconst_loop` stay inside the already-shared
    single-family loop-clone pattern
- focused artifact bundle for this seam:
  - [20260331-kdz-vararg-handoff-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260331-kdz-vararg-handoff-audit)
- the next exact target is now:
  - caller/callee handoff around a traced Lua callee loop
  - not generic vararg return lowering
  - and not another lower-frame return bug family
- one narrower candidate inside that seam is now closed:
  - a focused clean-host `LUAJIT_S390X_FUNCJIT_LOG` pass was silent on the
    reduced `sum_loop` and `retlast_loop` probes
  - so the extra `sum_loop` family is not being born at `rec_func_jit()` /
    compiled-callee entry
  - the next exact target moves later in the path:
    - caller-side re-entry after `lua_intrace_return`
    - before it settles into the separate `TRACE 2` / `TRACE 7` handoff family
- two more clean-host classifiers narrow that caller-side seam further:
  - reduced traceinfo probes:
    - `sum_loop`
      - `trace 1`: callee vararg scan loop
      - `trace 2`: caller-side root trace
      - `trace 7`: later caller-side root trace in the same family
    - `retlast_loop`
      - caller loop family only through the hot phase
      - later stitch traces exist, but they are not unique to this workload
  - focused artifact bundles:
    - [20260331-kdz-vararg-rootstart-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260331-kdz-vararg-rootstart-audit)
    - [20260331-kdz-vararg-callhandoff-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260331-kdz-vararg-callhandoff-audit)
  - a synced-and-rebuilt `LUAJIT_S390X_CALLHANDOFF_LOG` classifier then stayed
    completely silent while `sum_loop` still formed `TRACE 2` / `TRACE 7`
  - that rules out the generic `trace_stop(... BC_CALL/BC_CALLM/BC_ITERC ...)`
    plus `lj_trace_stitch()` handoff path as the birth point of the extra
    caller family
- one corrected clean-host start classifier changed that read again:
  - the actual gate is `LUAJIT_S390X_TRACE_START_LOG`
  - with that gate on clean `kdz`:
    - `sum_loop`
      - `trace 1` starts as a root at the callee vararg loop
      - `trace 2` starts as a second independent root at the caller site
    - `retlast_loop`
      - `trace 1` starts as a root at the caller site
      - later traces then grow from that caller root
  - so `sum_loop` is not creating `trace 2` through hidden post-return
    root-link selection
  - it is splitting across two hotcounted root sites instead
  - the next exact target therefore shifts:
    - explain why the caller root in `sum_loop` stops `-> 1` and keeps feeding
      the inner-loop ladder instead of converging into the stable caller-loop
      family seen in `retlast_loop`
- the reduced caller-root dumps make that stop-point explicit:
  - focused artifact bundle:
    - [20260331-kdz-vararg-rootdump-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260331-kdz-vararg-rootdump-audit)
  - `retlast_loop`
    - caller root already contains:
      - the caller add
      - the outer loop increment/check
      - outer-loop PHIs
    - it stops as a loop immediately
  - `sum_loop`
    - caller root stops after:
      - callee function identity guard
      - `select` env / identity guards
    - it does not yet materialize:
      - the caller add of callee result into the outer total
      - the outer-loop carried total / PHIs
    - it stops `-> 1` before the caller body becomes a real loop owner
  - the next exact target is therefore no longer “why is there a second root?”
  - it is:
    - why traced-callee return to caller in `sum_loop` stops before caller-body
      materialization, while `retlast_loop` reaches caller add/loop formation
      in the caller root itself
  - reduced recstop logs narrow that one step further:
    - focused artifact bundle:
      - [20260401-kdz-vararg-recstop-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-vararg-recstop-audit)
    - `retlast_loop`
      - caller side traces reach `rec_loop_jit()` on the caller loop seam
      - they log `S390X_RECLOOP ... ev=2 ...` and stop `-> loop`
    - `sum_loop`
      - caller roots (`TRACE 2`, later `TRACE 7`) stop `-> 1` before any
        caller-path `S390X_RECLOOP` appears
    - only the separate callee loop family reaches `rec_loop_jit()`
    - so `rec_loop_jit_root` is not the live vararg seam
    - the live target is now the earlier recorder/return condition that stops
      `sum_loop` caller roots before they ever reach the caller loop op
  - reduced return logs correct that again:
    - focused artifact bundle:
      - [20260401-kdz-vararg-recret-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-vararg-recret-audit)
    - the focused clean-host `sum_loop` probe does not show a divergent
      post-return branch in `lj_record_ret()`
    - the only observed branch is `lua_intrace_return`, and it belongs to the
      inner `select()` work in the callee loop
    - the actual caller-root cutoff is earlier:
      - `TRACE 2` starts at the caller site
      - enters `sum(...)`
      - then stops `-> 1` on the callee `JFORI` path, with `pc` already moved
        to the callee loop body start (`GGET`, previous op `JFORI`)
      - so the caller root links directly into the already-compiled callee loop
        trace before caller add / outer-loop PHIs materialize
    - `retlast_loop` does not have that nested callee loop boundary, so its
      caller root reaches caller add / loop formation directly
  - the next exact target is therefore:
    - decide whether this vararg cliff is simply the normal root-stop behavior
      for a caller trace that enters an already-compiled nested callee loop via
      `BC_JFORI`, or whether there is still a narrower recorder ownership seam
      above that boundary
  - code reading now matches that exactly:
    - [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c#L3597)
      handles `BC_JFORI`
    - when the loop is entered in a root trace, it takes:
      `lj_record_stop(J, LJ_TRLINK_ROOT, bc_d(...))`
    - that matches the observed `sum_loop` `TRACE 2 ... -> 1` stop with
      `prevop=JFORI`, `pc` already moved to the callee loop body start, and
      `link=1`
  - so the current live question is:
    - is caller-root ownership across a call into an already-compiled nested
      callee loop a real remaining optimization surface, or is that just the
      normal boundary on the current mechanism

That branch decision is now clean enough to move the broader-throughput queue:

- `sum_loop` still explains the vararg cliff, but its front-most split is the
  normal root `BC_JFORI -> existing loop` stop into an already-compiled nested
  callee loop
- that is not the next grounded local optimization target on the current
  mechanism
- the broader-throughput queue therefore moves forward to
  [tests/s390x/perf/bitops_mix.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/bitops_mix.lua)

First `bitops_mix` read from clean `kdz`:

- artifact root:
  - [20260331-kdz-bitops_mix-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260331-kdz-bitops_mix-truth-pack)
- raw medians:
  - `mix_bits/small`: JIT-on `0.000273`, `-joff` `0.000105`, ratio `2.60x`
  - `mix_bits/medium`: JIT-on `0.002093`, `-joff` `0.000525`, ratio `3.99x`
  - `mix_bits/hot`: JIT-on `0.007645`, `-joff` `0.002099`, ratio `3.64x`
- focused hot read:
  - JIT-on `0.007662`
  - `-joff` `0.002123`
  - gap `+0.005539s`
  - ratio `3.61x`
- runtime classification:
  - `TRACE_START 0`
  - `TRACE_STOP 0`
  - `TRACE_ABORT 0`
  - `TEXIT_COUNT 0`
  - `compiled-body-dominated`
- interpretation:
  - this is the first clean non-iterator, non-dispatch, non-helper family in
    the current queue that is materially red without any live exit churn
  - so the next work is a real compiled-body / lowering audit, not more
    ownership or exit chasing

Focused backend audit on that family:

- artifact root:
  - [20260401-kdz-bitop-log-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-bitop-log-audit)
  - [20260401-kdz-bitop-log-audit-v2](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-bitop-log-audit-v2)
- the hot chain is exactly the workload shape:
  - `band`, `bxor`, `bor`, shifts, rotates, `bswap`, `bnot`
  - every logged hot op is `IRT_INT`
  - no helper-call seam and no exit seam appear in this classifier
  - the refined producer log shows the hot body is mostly bitop-on-bitop:
    - later `logic` ops repeatedly consume earlier `logic`, `shiftk`, `brolk`,
      `bswap`, and `bnot` producers
    - the chain is not repeatedly reloading a fresh non-bitop value each step
- the s390x backend path is the interesting part:
  - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h#L1428)
    `asm_bitop_logic()`
  - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h#L1443)
    `asm_bitshift()`
  - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h#L1490)
    `asm_brot()`
  - all of those paths currently run through `asm_bnorm32()`
- next exact target:
  - prove whether repeated `asm_bnorm32()` work is being paid across an
    already-`IRT_INT` producer chain in `bitops_mix`
  - only then decide whether one narrow normalization-state / int32-home
    experiment is justified
- focused `asm_bnorm32()` classifier on clean `kdz` now confirms the payer
  shape:
  - [20260401-kdz-bnorm-log-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-bnorm-log-audit)
  - `84` total normalization sites in the focused hot run
  - exact split:
    - `42` unary/shift sites normalize values coming straight from the source
      integer or loop-carried arithmetic
    - `42` binary chain sites normalize results whose left and right inputs are
      already prior bitops
  - so the live backend question is now narrow:
    - can the s390x backend safely carry “already normalized int32” state
      across the binary bitop chain instead of reissuing `asm_bnorm32()` on
      every chain node
    - if not, this family should be closed without opening a backend patch
- first exact skip experiment on that seam is now rejected:
  - [20260401-kdz-bitop-chain-bnorm-skip-direct-v2](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-bitop-chain-bnorm-skip-direct-v2)
  - structural proof:
    - `S390X_BNORM_SKIP` fired on the intended binary chain nodes
  - `kdz` medians with the gate enabled:
    - `mix_bits/small`: `0.000340`
    - `mix_bits/medium`: `0.001965`
    - `mix_bits/hot`: `0.008870`
  - compared with the frozen baseline:
    - `small` regressed from `0.000273`
    - `hot` regressed from `0.007645`
  - conclusion:
    - a plain chain-node `asm_bnorm32()` delete is not promotable
    - any next backend step must be narrower than “skip result normalization on
      binary bitops”
- focused clean-`kdz` mcode dump now confirms the emitted hot-loop shape:
  - [20260401-kdz-bitops-mcode-audit-v3](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-bitops-mcode-audit-v3)
  - using the repo-local dump module, the hot binary chain repeatedly emits:
    - `LGR`
    - `NGR` / `OGR` / `XGR`
    - `LGFR`
  - `BSWAP` likewise emits `LRVR` followed by `LGFR`
  - no 32-bit logical register forms appear in the dumped hot body
- consequence:
  - the current backend is not just “doing some extra normalization”
  - it is explicitly materializing a `64-bit logical op + post-op sign-extend`
    pattern throughout the chain
  - clean source review now shows that this contract is broader than bitops:
    - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h#L1341)
      `asm_add()` uses `asm_bnorm32()` or `LGFR` around integer result ops
    - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h#L1635)
      `asm_sub()` uses the same pattern
    - [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h#L1710)
      `asm_mul()` does too
    - [src/lj_emit_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_emit_s390x.h#L97)
      exposes only the 64-bit register forms currently used by these paths:
      `AGR`, `SGR`, `NGR`, `OGR`, `XGR`, `MSGFR`
    - there are no active 32-bit register `AR` / `SR` / `NR` / `OR` / `XR`
      forms to retarget to from the current emitter surface
  - if this family stays open, the next exact target is not another bitops-only
    skip gate
  - it is whether the backend has a broader valid 32-bit integer-result
    lowering surface that would need to be added at all; without that, this
    `bitops_mix` line is close to closure as a local family
- one backend-wide 32-bit integer-result lowering pass is now measured and
  rejected:
  - native `kdz` probes showed the candidate 32-bit ops (`AR`, `SR`, `NR`,
    `OR`, `XR`, `AHI`, `MSR`, `LR`) do not auto-normalize in 64-bit mode
  - so the current backend contract still needs explicit `LGFR` / `LLGFR`
    after those ops
  - first code pass:
    - three-register arithmetic/logical forms plus the existing normalize step
    - clean truth-pack artifact:
      [20260401-kdz-bitops_mix-truth-pack](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260401-kdz-bitops_mix-truth-pack)
    - `mix_bits/hot` regressed to `0.008957`
  - second code pass:
    - two-register `AR` / `SR` / `NR` / `OR` / `XR`
    - `AHI`
    - direct `CC_OF` guards for int32 add/sub overflow
    - selective `LR` setup before a later normalize
    - best clean `kdz` rerun improved to `mix_bits/hot 0.007879`
    - still slower than the frozen `0.007645`
  - follow-up shift setup variants were also negative:
    - remove setup move: `0.008114`
    - `LR` setup move: `0.008079`
  - conclusion:
    - the backend-wide opcode-swap family is not promotable on the current
      normalize-every-result contract
    - if `bitops_mix` stays open, the next real family is a deeper
      normalized-result / int32-home design, not more local opcode
      substitutions
    - first clean `kdz` classifier for that deeper family:
      [20260401-kdz-bitops-int32home-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-bitops-int32home-audit/summary.md)
    - corrected `asm_bnorm32()` split:
      - `chain-binary`: `1337` sites total, `1146` of them pure carry sites
        consumed only by later bitops, `174` first leave the chain through
        `ADD`
      - `source-binary`: `189` sites, all still feed later bitops
      - `source-shift`: `763` sites, all still feed later bitops
      - `source-unary`: `382` sites, all still feed later bitops
    - the only named first non-bitop consumer is op `41` (`ADD`) with `190`
      hits
    - so the next honest backend target is not “fewer `LGFR`s everywhere”
    - it is a carried normalized int32/result-home across the bitop chain with
      one explicit leave-the-chain boundary into integer arithmetic
    - first env-gated carry-skip check on that exact seam:
      [20260401-kdz-bitops-int32home-gate-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-bitops-int32home-gate-check/summary.md)
    - clean `kdz` A/B:
      - baseline `mix_bits/hot 0.008095`
      - gated `mix_bits/hot 0.008593`
      - regression `+0.000498s` (`1.062x`)
    - the gate was not dead:
      - `2392` candidate sites fired in the focused structural probe
      - probe completed cleanly with `REMOTE_RC=0`
    - conclusion:
      - the boundary is real, but plain candidate-site `LGFR` skip is not
        promotable
      - any remaining backend family here has to carry a real normalized
        int32/result-home state rather than simply suppressing producer
        normalization at candidate nodes
    - first low32-home logical-subchain variant is also rejected:
      [20260401-kdz-bitops-low32home-subchain-check](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260401-kdz-bitops-low32home-subchain-check/summary.md)
    - exact shape:
      - only `band` / `bor` / `bxor`
      - only classifier-proven carry and `ADD`-tail nodes
      - 32-bit `LR` / `NR` / `OR` / `XR` low32-home lowering with no internal
        normalize on those nodes
    - clean `kdz` read:
      - first focused pass:
        - baseline `mix_bits/hot 0.008784`
        - gated `mix_bits/hot 0.008609`
      - same-host rerun without rebuild:
        - baseline `mix_bits/hot 0.008312`
        - gated `mix_bits/hot 0.008749`
    - supporting structural read:
      - reduced gated check completed and hit the new path:
        - `logic32carry 21`
        - `logic32tail 2`
      - the focused trace-count script timed out in both baseline and gated
        forms, so it did not separate the variant structurally
    - conclusion:
      - the gate is live, but the same-host perf result is unstable and not
        promotable
      - this exact low32-home logical-subchain variant is closed
      - if `bitops_mix` stays open from here, the next honest target is a
        deeper consumer-side normalized-result / int32-home design, not more
        local logical-subchain rewrites

## Authoritative Validation Surfaces

- Primary perf host:
  - `kdz:/root/luajit2-s390x/perf-clean-20260330/repo`
  - machine type `8561` (`z15`)
- Regression screen host:
  - `zkd0:/root/luajit2-s390x/perf-clean-20260330/repo`
  - machine type `3906` (`z14`)

Validation rules:

- tracked-file sync only
- direct `src/` rebuild only
- same-host pinned `kdz` A/B is the policy signal
- `zkd0` is regression-only
- low-noise manual logs or debugger only
- no dirty-tree `iterator_probe.py` runs for perf decisions

Checked-in restamp helper:

- [tools/s390x/restamp_iterator_perf.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/restamp_iterator_perf.py)
  now owns the authoritative iterator restamp path
- it syncs tracked files only, rebuilds directly in `src/`, captures both
  `jit.on` and `-joff`, runs the three focused micros, and writes:
  - `metadata.json`
  - `jit-on.jsonl`
  - `joff.jsonl`
  - `summary.md`
  - raw build, micro, owner-log, and IR-dump logs

Checked-in truth-pack helper:

- [tools/s390x/build_iterator_truth_pack.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/build_iterator_truth_pack.py)
  now owns the focused frozen-baseline evidence pack
- it reuses the same tracked-file sync and direct `src/` rebuild path, then
  adds:
  - focused hot medians for value-only hash, key-using hash, and array
    value-only control
  - `-jdump=im` IR+mcode for the same three loops
  - low-noise owner logs
  - smaller non-resume owner-selection probes for the same three loops
  - `jit.attach("trace")` and `jit.attach("texit")` counts after warmup
  - `perf stat` capture when the host supports those events

Checked-in dispatch truth-pack helper:

- [tools/s390x/build_dispatch_truth_pack.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/build_dispatch_truth_pack.py)
  now owns the queued dispatch/side-exit evidence pack
- it reuses the same tracked-file sync and direct `src/` rebuild path, then
  captures:
  - `dispatch_trace` JIT-on and `-joff` medians
  - focused hot medians for `numeric_loop`, `side_exit_loop`, and
    `hotexit_loop`
  - `jit.attach("trace")` and `jit.attach("texit")` counts after warmup
  - `-jdump=ism` IR+mcode for the focused loops
  - focused runtime `JLOOP_EXIT`, `HOTSIDE_FOCUS`, and recorder
    `SIDE_FOCUS` logs for the dominant seam
  - `perf stat` when the host supports those events

Checked-in broader-throughput truth-pack helper:

- [tools/s390x/build_throughput_truth_pack.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/build_throughput_truth_pack.py)
  now owns the first broader-throughput restamp path
- current supported families:
  - `vararg_paths`
  - `bitops_mix`
- it reuses the same tracked-file sync and direct `src/` rebuild path, then
  captures:
  - full-family JIT-on and `-joff` medians
  - focused hot-only medians
  - `jit.attach("trace")` and `jit.attach("texit")` counts after warmup
  - `perf stat` when the host supports those events
  - raw smoke, trace-count, and perf-stat logs
- the first native `vararg_paths` pass also forced one hardening step:
  - focused per-workload probes now run under a fixed timeout instead of
    hanging the entire helper when a hot traced surface wedges
- the helper now also owns reduced vararg handoff probes for this family:
  - `-jv` reduced scripts with `LUAJIT_S390X_RECRET_LOG=1`
  - saved under `raw/handoff`
  - summarized in `handoff-counts.json`

## Queued Dispatch / Side-Exit Frontier

Current `kdz` dispatch hot medians from the latest clean truth pack:

- `numeric_loop/hot`
  - JIT-on `0.350184`
  - `-joff` `0.002168`
  - gap `+0.348016s`
  - ratio `161.52x`
- `side_exit_loop/hot`
  - JIT-on `0.535201`
  - `-joff` `0.004667`
  - gap `+0.530534s`
  - ratio `114.68x`
- `hotexit_loop/hot`
  - JIT-on `0.622022`
  - `-joff` `0.005627`
  - gap `+0.616395s`
  - ratio `110.55x`

Focused runtime read on the same clean rerun:

- `numeric_loop` after warmup:
  - `TRACE_START 11`
  - `TRACE_STOP 11`
  - `TRACE_ABORT 0`
  - `TEXIT_COUNT 2001`
  - `TEXIT_HIST 10:0=200,11:0=200,12:0=200,13:0=58,1:0=142,2:0=1,4:0=200,5:0=200,6:0=200,7:0=200,8:0=200,9:0=200`
- `side_exit_loop` after warmup:
  - `TRACE_START 11`
  - `TRACE_STOP 11`
  - `TRACE_ABORT 0`
  - `TEXIT_COUNT 2001`
  - `TEXIT_HIST 10:0=200,11:0=200,12:0=200,13:0=58,1:0=142,2:0=1,4:0=200,5:0=200,6:0=200,7:0=200,8:0=200,9:0=200`
- `hotexit_loop` after warmup:
  - `TRACE_START 11`
  - `TRACE_STOP 11`
  - `TRACE_ABORT 0`
  - `TEXIT_COUNT 2001`
  - `TEXIT_HIST 10:0=200,11:0=200,12:0=200,13:0=58,1:0=142,2:0=1,4:0=200,5:0=200,6:0=200,7:0=200,8:0=200,9:0=200`
- current `kdz` still reports `perf stat` hardware counters as:
  - `<not supported>`

The key read is stronger now:

- the branch-free numeric loop already reproduced the same pathology
- and the branchy `side_exit_loop` / `hotexit_loop` surfaces now reproduce the
  exact same trace/exit histogram and focused side-entry markers
- so the current dispatch red is not opening a second branch-payload seam
  outside the closed loop-clone mechanism

## Dispatch Seam Attribution

The active seam on the frozen dispatch baseline is now mechanically pinned:

- root `trace 1` starts at `BC_FORL` and stops as a loop
- the hot seam is `trace 1 exit 0`
- focused runtime logs show the hot-side replay at:
  - `pc = BC_MODVN`
  - `prevop = BC_JFORI`
  - `snappc = BC_MODVN`
  - `parent_startop = BC_FORL`
- focused recorder logs show the first side trace enters as:
  - `parent=1 exit=0`
  - `startop = BC_JMP`
  - `startpc == pc == snappc`
  - `parent_snapnent = 0`
- a focused recorder rerun now shows that same first side trace does pass the
  current extra-loop narrow gate:
  - `prev_is_jfori = 1`
  - `fori_target = 1`
  - `target_match = 1`
  - `site=extra_loop_narrow`
- after `sidecheck`, that trace is still on the same bare body-entry state

Current named seam:

- `loop-body-entry-after-JFORI`

Current read:

- the hot failure is in the generic `FORL` / `JFORI` loop-entry path
- it is not a missed side-trace `JFORI` / `FORL` eligibility check
- the current extra-loop narrow path is firing
- hot-side duplication is downstream of that seam
- this is not an iterator seam, not bridge/continuation machinery, and not a
  late backend lowering opportunity

A focused `traceinfo` snapshot on the same `kdz` numeric seam corrects the
owner read:

- the descendants are not staying root-linked stubs
- `trace 3` through `trace 12` are already self-loop loop traces with the same
  `nins=18`, `nk=7`, and `nexit=4`
- the remaining dispatch problem is churn/reuse:
  - equivalent self-loop loop traces keep getting cloned on the same `exit 0`
    seam instead of reusing a stable earlier owner

Dispatch hotside classifiers are now split:

- `LUAJIT_S390X_HOTSIDE_CANON_EQUIV=1` does not fix the problem
- on the focused numeric probe it collapses the observed exit traffic into one
  reused site:
  - `7:0=160743`
- that is not a real owner/materialization win
- `LUAJIT_S390X_HOTSIDE_CANON_CHILD=1` reduces trace churn but not the real
  payer:
  - `TRACE_START` drops from `10` to `6`
  - `TEXIT_COUNT` stays at `2001`
  - the last trace still absorbs `8:0=858`
- `LUAJIT_S390X_HOTSIDE_SHARE_EQUIV=1` timed out after `20s` on the focused
  `numeric_loop` probe with no result and is not safe to treat as a live path

Next exact target from there:

- default hotside reuse/adoption policy itself:
  - on the late steady-state focused probe, default `trace_hotside()` already
    logs `phase=equiv parent=10 exit=0 cand=6 child=7`
  - but with the reuse gates off it still just counts toward `hotexit` and
    starts another trace
  - so the remaining dispatch red is now explicitly a policy choice, not a
    failure to discover equivalent loop owners

Next exact target from there:

- one narrow dispatch-side reuse/adoption experiment that proves a real
  owner/exit win on this seam, or closes the family if it only reproduces the
  earlier branch-hostile classifier behavior

First narrow reuse/adoption experiment from this seam is now rejected:

- `LUAJIT_S390X_HOTSIDE_REUSE_LOOP_CHILD`
  - exact attempt:
    - patch a late `exit 0` self-loop parent directly to an already-existing
      equivalent child loop
    - skip recording another equivalent side trace
  - why it was worth testing:
    - narrower than `CANON_EQUIV`, `CANON_CHILD`, or `SHARE_EQUIV`
    - directly targeted the known `cand` + `child` late seam
  - clean `kdz` structural gate:
    - focused `numeric_loop_trace.lua`
    - `timeout 20`
    - `REMOTE_RC=124`
  - decision:
    - reject before perf
    - do not keep the gate in-tree

The next earlier patch-target classifier from the same seam is now rejected:

- `LUAJIT_S390X_SIDEEXIT_MCLOOP`
  - exact attempt:
    - patch parent side exits to the loop-body target (`T->mcloop`) instead of
      generic trace entry
  - clean `kdz` structural gate:
    - focused `numeric_loop_trace.lua`
    - clean rebuild succeeded
    - the native probe then segfaulted before any trace-count output
  - code-level autopsy:
    - `trace_stop()` can redirect to `J->cur.mcode + T->mcloop`
    - but `mcloop` is only defined as an internal loop-body entry offset
    - the VM consumes that path only through owner/resume-gated entry flow
    - so it is not a generic safe side-exit landing target
  - decision:
    - reject before wider classification
    - do not widen to `side_exit_loop` or `hotexit_loop`

That closed the current dispatch loop-clone mechanism:

- default hotside policy sees equivalent candidates
- direct late child-retarget is unsafe
- patch-target shape does not unlock a safe owner/exit win
- no new seam remains in this mechanism

The follow-up dispatch-adjacent side-exit pass is now also classified:

- `side_exit_loop` and `hotexit_loop` do not expose a second hot seam
- on clean `kdz` they collapse back to the same `loop-body-entry-after-JFORI`
  practical shape:
  - `pc = BC_MODVN`
  - `prevop = BC_JFORI`
  - `startop = BC_JMP`
  - `site=extra_loop_narrow`
- so the current generic dispatch/side-exit line is now closed on this
  mechanism too

Next queued redirect:

1. helper-boundary storage/materialization audits where the ABI may help
2. only then broader JIT throughput families

The first helper-boundary follow-up from that redirect is now classified:

- surface:
  - [hotexit_update_preinterned.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/jit_loops/hotexit_update_preinterned.lua)
  - artifact:
    - [20260331-kdz-href-helper-audit](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260331-kdz-href-helper-audit)
- clean `kdz` result:
  - `TRACE_start iter=2 tr=1`
  - `TRACE_start iter=21 tr=2 otr=1 oex=2`
  - `TRACE_start iter=20 tr=3 otr=1 oex=0`
  - `TRACE_start iter=101 tr=4 otr=3 oex=3`
  - `TRACEINFO tr=1 link=1 type=loop`
  - `TRACEINFO tr=2 link=1 type=root`
  - `TRACEINFO tr=3 link=3 type=loop`
  - `TRACEINFO tr=4 link=0 type=stitch`
- interpretation:
  - the existing helper-backed dynamic `HREF` update path is structurally
    converged on the current tree
  - it does not expose a new broken helper-boundary family and does not reopen
    the older mixed-update hot-exit line

Updated queue:

1. any new helper-boundary work must name a fresh seam first
2. otherwise move to broader JIT throughput families

The first two broader-throughput targets are now fixed:

1. `vararg_paths`
2. `bitops_mix`

That order is intentional:

- `vararg_paths` is the first ABI-sensitive throughput family that avoids the
  closed iterator and dispatch seams
- `bitops_mix` is the first helper-light control for telling exit-heavy red
  from pure compiled-body red

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

## Current Checkpoint Baseline

Frozen checkpoint branch:

- `k8ika0s/s390x-jit-on-freeze-20260331`

Latest checkpoint evidence:

- `kdz` truth pack:
  - `pairs_sum/hot median=0.066259`
  - `pairs_array_sum/hot median=0.069155`
- `zkd0`:
  - `pairs_sum/hot median=0.104839`
  - `pairs_array_sum/hot median=0.097884`

Current same-harness `-joff` comparator on `kdz`:

- `pairs_sum/hot median=0.005540`
- `pairs_array_sum/hot median=0.003716`

These are the numbers new iterator perf work must beat.

The earlier freeze-point reference is still useful as a historical anchor:

- `kdz`: `0.059818 / 0.061622`
- `zkd0`: `0.093881 / 0.087945`

But the measured branch-tip contract is now the post-cleanup restamp above,
not the older reference.

## Distance To Expectation

Current `kdz` JIT-on distance to same-harness `-joff`:

- `pairs_sum/hot`
  - JIT-on `0.066259`
  - `-joff` `0.005540`
  - gap `+0.060719s`
  - ratio `11.96x`
- `pairs_array_sum/hot`
  - JIT-on `0.069155`
  - `-joff` `0.003716`
  - gap `+0.065439s`
  - ratio `18.61x`

Delivery ladder from the current `kdz` restamp:

- Restamp bar:
  - still failed
  - hash `+10.77%` slower than the earlier freeze-point reference
  - array `+12.22%` slower than the earlier freeze-point reference
- Recovery bar:
  - hash target `<= 0.056341`, current gap `+0.009918s`
  - array target `<= 0.059806`, current gap `+0.009349s`
- First real-results bar:
  - hash target `<= 0.050000`, current gap `+0.016259s`
  - array target `<= 0.055000`, current gap `+0.014155s`

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

Low-noise manual logging and raw IR on the post-cleanup branch tip show:

- Value-only hash:
  - dominant shared payer is still `addov_rr_int_eq`
  - main non-value cluster is still the hidden `KEYINDEX` load
  - only other frame `SLOAD` is the carried total slot
  - helper `VLOAD #0` feeds the visible value lane directly
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
- the refreshed owner map did not expose a new target outside the reject pile

## Freeze-Point Truth-Pack Decision

The newest focused truth pack answered the next gating question directly:

- steady-state trace and exit activity is still materially nonzero after
  warmup
- value-only hash:
  - `TRACE_START 10`
  - `TRACE_ABORT 9`
  - `TEXIT_COUNT 960000`
- key-using hash:
  - `TRACE_START 10`
  - `TRACE_ABORT 9`
  - `TEXIT_COUNT 640000`
- array value-only control:
  - `TRACE_START 11`
  - `TRACE_ABORT 10`
  - `TEXIT_COUNT 960000`

So the remaining red is not yet just compiled-loop throughput. The next
justified target is still root-trace or side-trace ownership on the frozen
baseline, starting from the exact steady-state exit site for value-only hash.
This is not permission to reopen bridge work, no-guard families, or backend
micro-surgery.

Focused non-resume owner-selection follow-up on `kdz` now gives the next
decision boundary directly:

- the root-`ITERN` resume-contract family is closed again on the current tree
- the next open seam is non-resume owner selection only

Value-only hash:

- root `trace 1` still stops as `link=1`, `linktype=2`, `startop=70`
- the hot steady-state seam is still:
  - `S390X_JLOOP_EXIT phase=dispatch-original parent=1 exit=1 trace=1`
- the first materially different owner candidate is `trace 2`
- that candidate does not reach child-link/runtime owner logic
- it dies immediately in recorder loop-stop handling:
  - `S390X_RECSETUP site=root_ready trace=2 ... startop=79`
  - `S390X_LINNER site=rec_loop_jit_root trace=2 ...`
  - `S390X_TRACE_ABORT trace=2 ... err=9`

Key-using hash:

- same owner-selection outcome as value-only hash
- the first candidate also dies in `rec_loop_jit_root` before any later
  ownership machinery can matter

Array value-only control:

- root `trace 1` also spends the early hot seam in `dispatch-original`
- its first side trace gets farther than hash:
  - `trace 2 parent=1 exit=1 root=1 startop=88`
  - repeated nil-path aborts with `err=8`
  - eventual stop as `linktype=6`, `link=0`, `root=1`
- later descendants do stop, but they are still root-linked:
  - `trace 3`, `trace 4`, `trace 6` stop as `linktype=1`, `link=1`, `root=1`

Current read:

- hash dies too early, in
  [src/lj_record.c](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_record.c)
  inside `rec_loop_jit()`
- array survives farther, but still first lands in interpreter/root-linked
  ownership instead of a stable non-root owner
- so the next valid code family, if one exists at all, is one narrow
  non-resume owner-selection cut that changes that exact outcome

Fresh proof artifacts from the checked-in helpers:

- `kdz` truth-pack bundle:
  - [summary.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260331-kdz-nonresume-owner-selection/summary.md)
- `zkd0` restamp bundle:
  - [summary.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/restamps/20260331-zkd0-nonresume-owner-screen/summary.md)
- focused array owner probe:
  - [stdout.log](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260331-kdz-array-owner-probe/stdout.log)
  - [stderr.log](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual/20260331-kdz-array-owner-probe/stderr.log)
- value-only hash IR proof on `kdz`:
  - [hash_value.stdout.log](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260331-kdz-nonresume-owner-selection/raw/dump/hash_value.stdout.log)
  - still shows:
    - `int VLOAD 0005 #0`
    - `int SLOAD #3 T`
    - `int ADDOV`
  - and no extra visible value-lane frame `SLOAD`

Corrected finite owner-selection rerun on `kdz`:

- the first rerun target was a fresh truth-pack directory using the fixed
  finite owner-selection probe path:
  - [hash_value.stderr.log](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260331-kdz-nonresume-owner-selection-v3/raw/owner-selection/hash_value.stderr.log)
  - [hash_key.stderr.log](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260331-kdz-nonresume-owner-selection-v3/raw/owner-selection/hash_key.stderr.log)
- that corrected rerun tightens the mechanism:
  - value-only hash and key-using hash share the same first-side failure
  - on both loops, the hot path stays in:
    - `S390X_JLOOP_EXIT phase=dispatch-original parent=1 exit=1 trace=1`
  - the first fresh root candidate is still:
    - `trace 2 startop=79`
    - `S390X_LINNER site=rec_loop_jit_root`
    - `err=9`
  - and the side attempts still show:
    - `TRACE 2 start 1/1`
    - `abort ... leaving loop in root trace`
- current read after the corrected rerun:
  - the hash owner-selection seam is not a distinct later runtime-owner
    problem
  - it is the same first-side nil-descendant / unloaded-visible-key family
    already exposed by the earlier focused hash seam probes
  - subagent forensics and the mature-control diff both pin the first
    divergence earlier, at the `rec_itern()` payload-vs-nil fork on `ix.key`
    after the helper result already exists
  - so this family is closed again on the current tree
  - any future cut must be genuinely different from those rejected
    first-side lazy-key classifiers

Four-track frozen-baseline restamp on `kdz` and `zkd0` now closes the current
iterator reopening window:

- authoritative `kdz` truth pack:
  - [summary.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs/20260331-kdz-frozen-baseline-v3/summary.md)
  - `pairs_sum/hot median=0.062519`
  - `pairs_array_sum/hot median=0.066353`
  - focused same-harness `-joff` gaps:
    - `hash_value` `10.84x`
    - `hash_key` `11.58x`
    - `array_value` `15.58x`
  - `perf stat` is still unsupported, so the active exit/body attribution uses
    the runtime fallback section in the truth pack
  - all three focused loops still classify as `exit-dominated`
- exact seam read from that restamp:
  - `hash_value` and `hash_key` still classify as the same closed first-side
    lazy-key family
  - `array_value` still reaches the payload/root-linked side path, but not a
    new iterator family worth opening
  - `rec_loop_jit_root` remains a downstream symptom, not a new stop-target
    seam
- ABI-aware preserved-GPR audit is also negative on the current tree
  - no proven loop-carried value is being dropped only because current s390x
    register-home/liveness fails to keep it in a preserved GPR across
    `lj_vm_next`
- `zkd0` regression screen:
  - [summary.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/restamps/20260331-zkd0-post-tracks-screen/summary.md)
  - `pairs_sum/hot median=0.119175` (`+26.94%` vs frozen)
  - `pairs_array_sum/hot median=0.120802` (`+37.36%` vs frozen)

Queueing decision:

- no new iterator seam is open from the current mechanism
- iterator stays frozen at the current Lane A + Lane B checkpoint
- next queued perf workstream moves to dispatch/side-exit

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
- exact `rec_itern()` accumulator preloads that still leave the root trace on
  `int SLOAD #3` plus `ADDOV`
- accumulator-to-`num` cuts that still keep the loop-unroll `int.num` check
- backend `AR/SR` overflow rewrites
- backend `AGFR/CGFR` equality-guard rewrites

The common failure modes were:

- semantic breakage
- cross-host regression
- same-host pinned `kdz` regression
- or real structural change with no promotable hot-loop win

## Current Gate Result

The one remaining accumulator-family pass was tried and rejected.

- Exact experiment:
  - preload the exact iterator accumulator slot from `rec_itern()` as a real
    `num` `SLOAD`
  - add the minimal s390x `num-from-int` `IRSLOAD_CONVERT` path needed to
    support that slot load
- Structural result:
  - rejected immediately
  - raw IR on `kdz` still showed:
    - `int SLOAD #3`
    - `int ADDOV`
  - the carried slot was not actually born as `num`
  - the back-edge `int.num` problem therefore was not removed
- Decision:
  - there is no remaining justified accumulator-family pass from the current
    mechanism
  - do not reopen that family unless a future cut can prove the original
    carried slot becomes `num` before `loop_unroll()` sees it

The one allowed backend classifier also came back negative:

- the surviving hash `sload_keyindex` / `sload_type` cluster does not lower as
  a plain load + compare + branch sequence
- current lowering in
  [src/lj_asm_s390x.h](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/src/lj_asm_s390x.h)
  is a load + tag-extract shift + compare + branch sequence
- there is no narrow semantic-preserving load/test or compare/branch fusion
  candidate visible from the current lowering

## Future Entry Gate

Do not start another iterator perf patch unless all of these are true first:

- a named remaining payer exists
- there is a direct structural proof target in raw IR or low-noise logs
- the idea is not already in the reject pile
- the candidate can be tested against this exact measured branch-tip `kdz` baseline with
  `zkd0` used only as a regression screen

Current status against that gate:

- the checkpoint truth pack is complete on `kdz`
- the minimal checkpoint regression screen is complete on `zkd0`
- the owner map is refreshed
- the next open question is now narrower:
  - can hash `exit 1` survive past `rec_loop_jit_root` into a materially
    different non-root owner shape, or is this family exhausted?
- there is still no justified new code-level perf patch until that site is
  identified cleanly

Acceptable future target shapes:

- one new recorder/live-in idea that removes a remaining root-trace
  storage/control read
- one new semantic-preserving lowering idea only if it targets an actually
  fuseable sequence, not a hoped-for micro-op win

Unacceptable future target shapes:

- anything whose main claim is “fewer backend instructions”
- anything whose proof is only “the IR looks cleaner”
- anything that depends on bridge or continuation policy

## Promotable Patch Gate

A future iterator patch is promotable only if it:

- keeps value-only hash, key-using hash, and array control micros green
- beats the measured branch-tip `kdz` baseline
- does not regress `zkd0`
- removes a real steady-state payer in IR or low-noise logs
- does not rely on branch-shape churn or late backend micro-surgery

## Benchmark And Logging Commands

From the clean local repo, drive the authoritative host restamp with:

```sh
python3 tools/s390x/restamp_iterator_perf.py \
  --host kdz \
  --output-dir artifacts/s390x/restamps/20260331-kdz-post-cleanup-restamp2

python3 tools/s390x/restamp_iterator_perf.py \
  --host zkd0 \
  --output-dir artifacts/s390x/restamps/20260331-zkd0-post-cleanup-restamp
```

From the clean local repo, drive the frozen-baseline truth pack with:

```sh
python3 tools/s390x/build_iterator_truth_pack.py \
  --host kdz \
  --output-dir artifacts/s390x/truth-packs/20260331-kdz-frozen-baseline-truth-pack
```

The helpers enforce:

- tracked-file sync only
- direct `src/` rebuild only
- `S390X_PERF_SAMPLES=9`
- `S390X_PERF_WARMUP=2`
- pinned `taskset -c 0` benchmark runs
- both `jit.on` and `-joff` in the same restamp

Equivalent manual `kdz` benchmark command from the clean remote repo:

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
