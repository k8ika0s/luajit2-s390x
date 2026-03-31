# s390x Validation Runbook

Last updated: 2026-03-31 11:35:00 PDT

## Purpose

This runbook describes the current authoritative validation loop for the s390x
branch. It is intentionally narrower than the older closure-era workflow and
matches the current lane split:

- Lane A: build and stability
- Lane B: recorder-side iterator perf
- Lane C: parked bridge and continuation research

For current status, read
[state-of-project.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/state-of-project.md)
first.

## Authoritative Worktrees

Keep exactly two authoritative native repos:

- `kdz:/root/luajit2-s390x/perf-clean-20260330/repo`
- `zkd0:/root/luajit2-s390x/perf-clean-20260330/repo`

Use them for:

- clean rebuilds
- focused micros
- pinned `kdz` perf A/B
- `zkd0` regression screening

Do not keep additional long-lived manual perf trees unless the current pair is
discarded and replaced.

## Authoritative Restamp Entry Point

Use the checked-in helper from the clean local repo:

```sh
python3 tools/s390x/restamp_iterator_perf.py \
  --host kdz \
  --output-dir artifacts/s390x/restamps/20260331-kdz-post-cleanup-restamp2

python3 tools/s390x/restamp_iterator_perf.py \
  --host zkd0 \
  --output-dir artifacts/s390x/restamps/20260331-zkd0-post-cleanup-restamp
```

That helper is now the default measurement path because it locks the contract:

- tracked-file sync only
- direct `src/` rebuild only
- `S390X_PERF_SAMPLES=9`
- `S390X_PERF_WARMUP=2`
- pinned `taskset -c 0` benchmark runs
- same benchmark file for `jit.on` and `-joff`
- raw owner logs and IR dumps retained beside the benchmark JSONL

For the frozen-baseline “measure, then decide” pass, use the truth-pack helper:

```sh
python3 tools/s390x/build_iterator_truth_pack.py \
  --host kdz \
  --output-dir artifacts/s390x/truth-packs/20260331-kdz-frozen-baseline-truth-pack
```

That helper uses the same sync and rebuild contract, then adds:

- focused hot medians for:
  - value-only hash
  - key-using hash
  - array value-only control
- `-jdump=im` IR+mcode dumps for the same three loops
- low-noise owner logs
- steady-state `jit.attach("trace")` and `jit.attach("texit")` counts after
  warmup
- `perf stat` capture when the host supports the requested counters

Required output bundle:

- `metadata.json`
- `jit-on.jsonl`
- `joff.jsonl`
- `summary.md`
- `raw/`

## Non-Negotiable Validation Rules

- sync tracked files only
- rebuild in `src/` only
- treat `kdz` same-host pinned A/B as the policy signal
- treat `zkd0` as a regression screen, not a policy chooser
- use low-noise manual logs or debugger only
- do not use the dirty-tree
  [tools/s390x/iterator_probe.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/iterator_probe.py)
  wrapper for perf decisions
- do not add helper-call probes in VM fast paths

## Clean Rebuild

From the clean remote repo:

```sh
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
make -C src clean
make -C src -j4
./src/luajit -e 'local a,b,c=jit.status(); print(a,b,c)'
```

Current expected result on both hosts:

- `true fold cse`

This should work without `XCFLAGS=-DLUAJIT_ENABLE_S390X_JIT`.

## Baseline Correctness Checks

Keep these green before perf work:

```sh
./src/luajit /tmp/oneshot_iter.lua 20
./src/luajit /tmp/oneshot_iter.lua 2000
./src/luajit /tmp/oneshot_iter.lua 200000
./src/luajit -joff /tmp/oneshot_iter.lua 200000
```

Current expected outputs:

- `RESULT 500`
- `RESULT 50000`
- `RESULT 5000000`
- `RESULT 5000000`

## Iterator Perf Restamp

Pinned `kdz` policy run from the helper now comes back as:

- `pairs_sum/hot median=0.061851`
- `pairs_array_sum/hot median=0.063845`

`zkd0` regression screen uses the same benchmark plus focused micros and
currently comes back as:

- `HASH_VALUE 3000`
- `HASH_KEY 1320`
- `ARRAY_VALUE 3000`
- `pairs_sum/hot median=0.156370`
- `pairs_array_sum/hot median=0.154843`

Same-harness `-joff` comparator on `kdz`:

- `pairs_sum/hot median=0.004289`
- `pairs_array_sum/hot median=0.003695`

Current `kdz` distance to that comparator from the frozen-baseline truth pack:

- hash hot:
  - gap `+0.055205s`
  - ratio `10.90x`
- array hot:
  - gap `+0.062611s`
  - ratio `16.17x`

Current `kdz` delivery ladder:

- restamp bar:
  - failed vs the earlier `0.059818 / 0.061622` freeze-point reference
- recovery bar:
  - hash current gap `+0.005510s`
  - array current gap `+0.004039s`
- first real-results bar:
  - hash current gap `+0.011851s`
  - array current gap `+0.008845s`

If the helper restamp bar fails again, stop and explain the drift before
opening another perf patch family.

If the frozen-baseline truth pack still shows materially nonzero steady-state
trace starts, aborts, or texits after warmup, do not switch the branch over to
“compiled throughput only” debugging. The next target remains root/side-trace
ownership on the frozen baseline.

## Focused Micros

Value-only hash:

```lua
jit.opt.start("hotloop=1")
local t={a=10,b=20,c=30,d=40,e=50}
local function run(n)
  local total=0
  for _=1,n do
    for _,v in pairs(t) do total = total + v end
  end
  return total
end
print("HASH_VALUE", run(20))
```

Key-using hash:

```lua
jit.opt.start("hotloop=1")
local t={aa=10,bb=20,cc=30}
local function run(n)
  local total=0
  for _=1,n do
    for k,v in pairs(t) do total = total + v + #k end
  end
  return total
end
print("HASH_KEY", run(20))
```

Value-only array control:

```lua
jit.opt.start("hotloop=1")
local t={10,20,30,40,50}
local function run(n)
  local total=0
  for _=1,n do
    for _,v in pairs(t) do total = total + v end
  end
  return total
end
print("ARRAY_VALUE", run(20))
```

## Low-Noise Owner Mapping

Use only focused manual logging:

```sh
LUAJIT_S390X_ADD_LOG=1 LUAJIT_S390X_SLOAD_LOG=1 ./src/luajit /tmp/hash_value.lua
LUAJIT_S390X_ADD_LOG=1 LUAJIT_S390X_SLOAD_LOG=1 ./src/luajit /tmp/hash_key.lua
LUAJIT_S390X_ADD_LOG=1 LUAJIT_S390X_SLOAD_LOG=1 ./src/luajit /tmp/array_value.lua
```

Current owner map on the measured branch-tip baseline:

- shared `addov_rr_int_eq` is the dominant cross-family payer
- value-only hash still carries the hidden `KEYINDEX` load cluster
- value-only hash still carries the carried-total `SLOAD`
- key-using hash adds a visible key/type `SLOAD`
- array still carries numeric-key control loads
- hash root no longer frame-sources the visible value lane

Current frozen-baseline truth-pack decision on `kdz`:

- value-only hash:
  - `TRACE_START 10`
  - `TRACE_ABORT 9`
  - `TEXIT_COUNT 960000`
- key-using hash:
  - `TRACE_START 10`
  - `TRACE_ABORT 9`
  - `TEXIT_COUNT 640000`
- array value-only control:
  - `TRACE_START 12`
  - `TRACE_ABORT 10`
  - `TEXIT_COUNT 960000`
- conclusion:
  - steady-state exit behavior is still materially nonzero
  - do not treat the current perf gap as pure compiled-loop throughput yet

Fresh `kdz` proof bundle:

- [summary.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/restamps/20260331-kdz-post-cleanup-restamp2/summary.md)
- [hash_value.stdout.log](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/restamps/20260331-kdz-post-cleanup-restamp2/raw/ir/hash_value.stdout.log)

## Entry Gate For New Perf Work

Before writing another iterator perf patch, require all of:

- a named remaining payer
- a concrete structural proof target
- an explanation of why the idea is not already in the reject pile

If any of those are missing, stop and restamp the measured branch-tip baseline instead of
starting a new patch family.

After the new helper landed, that rule tightens further:

- if the helper restamp does not reproduce a stable measured branch-tip
  baseline, do not trust older manual numbers for patch decisions

## Reset Rules

- If a validation repo is contaminated, discard it and resync tracked files.
- Do not hand-edit remote source trees as part of the normal loop.
- Do not trust partial syncs or stale detached repos.
- If a result depends on invasive logging or a dirty tree, treat it as advisory
  only.

## What Is Parked

Do not reopen these during routine perf work:

- bridge and continuation experiments
- hidden-control carry family
- `TRACE 2` churn elimination as a perf proxy
- `KEYINDEX` no-guard variants
- full-lazy collapse
- accumulator-to-`num` variants that still keep the back-edge `int.num` check
- exact accumulator preloads that still leave `int SLOAD #3` plus `ADDOV`
- backend `AR/SR` and `AGFR/CGFR` rewrite ideas

## Related Docs

- High-level current status:
  [state-of-project.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/state-of-project.md)
- Current perf status:
  [perf.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/perf.md)
- Technical notebook:
  [findings.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/findings.md)
