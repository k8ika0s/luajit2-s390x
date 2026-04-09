# s390x Validation Runbook

Last updated: 2026-04-06 12:20:00 PDT

## Purpose

This runbook describes the authoritative s390x validation loop after the
layout reset. The remote trees are deliberate nongit mirrors, not remote git
worktrees.

For current status, read
[state-of-project.md](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/docs/s390x/state-of-project.md)
first.

## Canonical Layout

### Local

- authoritative repo:
  `/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x`
- disposable scratch:
  `/private/tmp/luajit2-s390x-scratch/<purpose>/`
- generated outputs:
  [artifacts/s390x](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x)
- archived generated journal payloads:
  [artifacts/archive/s390x/analyze-journal](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/archive/s390x/analyze-journal)

### Remote

Keep exactly one canonical mirror path per host:

- `kdz:/root/luajit2-s390x/canon/repo`
- `zkd0:/root/luajit2-s390x/canon/repo`

Keep exactly one timestamped output namespace per host:

- `kdz:/root/luajit2-s390x/runs/<timestamp>-<purpose>/`
- `zkd0:/root/luajit2-s390x/runs/<timestamp>-<purpose>/`

Optional quarantine/archive namespace:

- `kdz:/root/luajit2-s390x/archive/<timestamp>/`
- `zkd0:/root/luajit2-s390x/archive/<timestamp>/`

## Authoritative Sync Entry Point

Use the checked-in helper before any manual native run:

```sh
python3 tools/s390x/sync_remote_mirror.py \
  --host kdz \
  --verify-path src/lj_trace.c \
  --verify-path docs/s390x/findings.md

python3 tools/s390x/sync_remote_mirror.py \
  --host zkd0 \
  --verify-path src/lj_trace.c \
  --verify-path docs/s390x/findings.md
```

That helper is the only approved manual sync entrypoint because it enforces:

- tracked-file sync only
- canonical `canon/repo` target only
- preserved relative paths only
- `runs/` and `archive/` namespace creation beside the mirror
- post-sync verification of relative landing paths

## Layout Invariants

- The mirror root must stay buildable and free of experiment debris.
- All source sync must preserve relative paths under `canon/repo`.
- Single-file syncs must still preserve relative paths.
- Run outputs must land under `runs/<timestamp>-<purpose>/`, never under
  `canon/repo`.
- The mirror root is not a scratchpad, and it is not a git checkout.

Correct:

```sh
python3 tools/s390x/sync_remote_mirror.py --host kdz --verify-path src/lj_record.c
rsync -aR src/lj_trace.c kdz:/root/luajit2-s390x/canon/repo/
ssh kdz 'mkdir -p /root/luajit2-s390x/runs/20260406-sanity'
```

Wrong:

```sh
scp src/lj_trace.c kdz:/root/luajit2-s390x/canon/repo/
rsync -a src/lj_trace.c kdz:/root/luajit2-s390x/canon/repo/
scp findings.md kdz:/root/luajit2-s390x/canon/repo/
ssh kdz 'cd /root/luajit2-s390x/canon/repo && ./src/luajit ... >/root/luajit2-s390x/canon/repo/out.log'
```

## Authoritative Restamp Entry Points

Use the checked-in helpers from the clean local repo. They now default to the
canonical remote mirror path.

```sh
python3 tools/s390x/restamp_iterator_perf.py \
  --host kdz \
  --output-dir artifacts/s390x/restamps/20260406-kdz-restamp

python3 tools/s390x/restamp_iterator_perf.py \
  --host zkd0 \
  --output-dir artifacts/s390x/restamps/20260406-zkd0-restamp
```

For focused truth packs:

```sh
python3 tools/s390x/build_iterator_truth_pack.py \
  --host kdz \
  --output-dir artifacts/s390x/truth-packs/20260406-kdz-iterator-truth-pack
```

The helper contract remains:

- tracked-file sync only
- direct `src/` rebuild only
- `S390X_PERF_SAMPLES=9`
- `S390X_PERF_WARMUP=2`
- pinned `taskset -c 0` benchmark runs where supported
- same benchmark file for `jit.on` and `-joff`
- raw owner logs and IR dumps retained beside the benchmark JSONL

## Non-Negotiable Validation Rules

- sync through
  [tools/s390x/sync_remote_mirror.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/sync_remote_mirror.py)
  or through helpers that call the same tracked-file sync path
- rebuild in `src/` only
- treat `kdz` same-host pinned A/B as the policy signal
- treat `zkd0` as a regression screen, not a policy chooser
- use low-noise manual logs or debugger only
- do not use the dirty-tree
  [tools/s390x/iterator_probe.py](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/tools/s390x/iterator_probe.py)
  wrapper for perf decisions
- do not add helper-call probes in VM fast paths

## Clean Rebuild

From the canonical remote mirror:

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

## Output Discipline

- Write run bundles under
  [artifacts/s390x/manual](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/manual),
  [artifacts/s390x/restamps](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/restamps),
  or
  [artifacts/s390x/truth-packs](/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x/artifacts/s390x/truth-packs)
  locally.
- Use `runs/<timestamp>-<purpose>/` remotely only for transient native outputs.
- If a remote run leaves behind ad hoc files in `canon/repo`, stop and clean
  the mirror before opening another perf branch.
