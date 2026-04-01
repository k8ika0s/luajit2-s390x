# s390x Performance Status

Last updated: 2026-03-31 22:45:00 PDT

## Scope

This page tracks the current native s390x performance state after the branch
was frozen into three lanes:

- Lane A: build and stability only
- Lane B: promotable recorder-side iterator perf only
- Lane C: parked bridge and continuation research only

The active performance frontier is no longer iterator-only. Iterator is frozen
at the current Lane A + Lane B checkpoint unless a genuinely new seam appears
outside the reject pile. The first dispatch/side-exit loop-clone queue has now
also been classified and closed on the current mechanism. The follow-up
dispatch-adjacent side-exit pass did not expose a second seam; the branchy
loops collapse back to the same closed loop-clone ladder. The first
helper-boundary follow-up is also now classified and did not expose a new
surface. The next queued workstream is broader JIT throughput work unless a
new helper-boundary storage/materialization seam can be named first. The
bridge and continuation line stays parked.

That broader-throughput queue is now explicit:

1. [tests/s390x/perf/vararg_paths.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/vararg_paths.lua)
   is first because it pressures arg-bank, call, return, and `select()` /
   vararg flow without reusing the closed iterator or dispatch seams
2. [tests/s390x/perf/bitops_mix.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/bitops_mix.lua)
   is second as a backend-heavy compiled-body control
3. [tests/s390x/perf/mixed_noffi.lua](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tests/s390x/perf/mixed_noffi.lua)
   stays out of this queue because `pairs(map)` would drag iterator behavior
   back into a family that is supposed to sit outside the frozen iterator line

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
  - if this family stays open, the next exact target is not another skip gate
  - it is whether the backend has a valid 32-bit logical lowering surface at
    all; without that, this line is close to closure

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
