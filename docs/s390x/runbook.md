# s390x Validation Runbook

Last updated: 2026-03-31 09:12:31 PDT

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

Pinned `kdz` policy run:

```sh
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
taskset -c 0 ./src/luajit tests/s390x/perf/iterator_table.lua
```

Current frozen baseline:

- `pairs_sum/hot median=0.059818`
- `pairs_array_sum/hot median=0.061622`

`zkd0` regression screen uses the same benchmark plus focused micros and should
stay within the current green band:

- `HASH_VALUE 3000`
- `HASH_KEY 1320`
- `ARRAY_VALUE 3000`
- `pairs_sum/hot median=0.093881`
- `pairs_array_sum/hot median=0.087945`

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

Current owner map on the frozen baseline:

- shared `addov_rr_int_eq` is the dominant cross-family payer
- value-only hash still carries the hidden `KEYINDEX` load cluster
- value-only hash still carries the carried-total `SLOAD`
- key-using hash adds a visible key/type `SLOAD`
- array still carries numeric-key control loads
- hash root no longer frame-sources the visible value lane

## Entry Gate For New Perf Work

Before writing another iterator perf patch, require all of:

- a named remaining payer
- a concrete structural proof target
- an explanation of why the idea is not already in the reject pile

If any of those are missing, stop and restamp the frozen baseline instead of
starting a new patch family.

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
