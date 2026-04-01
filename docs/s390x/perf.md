# s390x Performance Status

Last updated: 2026-03-31 18:56:55 PDT

## Scope

This page tracks the current native s390x performance state after the branch
was frozen into three lanes:

- Lane A: build and stability only
- Lane B: promotable recorder-side iterator perf only
- Lane C: parked bridge and continuation research only

The active performance frontier is no longer iterator-only. Iterator is frozen
at the current Lane A + Lane B checkpoint unless a genuinely new seam appears
outside the reject pile. The next queued workstream is dispatch/side-exit on
the same clean-host contract. The bridge and continuation line stays parked.

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

## Queued Dispatch / Side-Exit Frontier

Current `kdz` dispatch hot medians from the active truth pack:

- `numeric_loop/hot`
  - JIT-on `0.642036`
  - `-joff` `0.002168`
  - gap `+0.639868s`
  - ratio `296.14x`
- `side_exit_loop/hot`
  - JIT-on `0.155224`
  - `-joff` `0.004739`
  - gap `+0.150485s`
  - ratio `32.75x`
- `hotexit_loop/hot`
  - JIT-on `0.380652`
  - `-joff` `0.005653`
  - gap `+0.374999s`
  - ratio `67.34x`

Focused runtime read on the same branch tip:

- `numeric_loop` after warmup:
  - `TRACE_START 10`
  - `TRACE_STOP 10`
  - `TRACE_ABORT 0`
  - `TEXIT_COUNT 2001`
  - `TEXIT_HIST 1:0=142,2:0=1,3:0=200,4:0=200,5:0=200,6:0=200,7:0=200,8:0=200,9:0=200,10:0=200,11:0=200,12:0=58`
- `side_exit_loop` after warmup:
  - `TRACE_START 11`
  - `TRACE_STOP 11`
  - `TRACE_ABORT 0`
  - `TEXIT_COUNT 2001`
- current `kdz` still reports `perf stat` hardware counters as:
  - `<not supported>`

The key read is that the branch-free numeric loop already reproduces the same
pathology. This is not primarily a loop-body branchiness problem.

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

Next exact target:

- default hotside reuse/adoption policy itself:
  - on the late steady-state focused probe, default `trace_hotside()` already
    logs `phase=equiv parent=10 exit=0 cand=6 child=7`
  - but with the reuse gates off it still just counts toward `hotexit` and
    starts another trace
  - so the remaining dispatch red is now explicitly a policy choice, not a
    failure to discover equivalent loop owners

Next exact target:

- one narrow dispatch-side reuse/adoption experiment that proves a real
  owner/exit win on this seam, or closes the family if it only reproduces the
  earlier branch-hostile classifier behavior

Active gated experiment now in tree:

- `LUAJIT_S390X_HOTSIDE_REUSE_LOOP_CHILD`
  - reuses an already-existing equivalent child loop on the late `exit 0`
    `FORL` / `JFORI` seam by patching the current parent exit directly to that
    child
  - this is intentionally narrower than `CANON_EQUIV`, `CANON_CHILD`, or
    `SHARE_EQUIV`
  - current validation is local build/smoke only
  - no native `kdz` structural or perf claim is attached yet

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
