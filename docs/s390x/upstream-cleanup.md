# s390x Upstream Cleanup

This page tracks source patterns that are acceptable during bring-up but not
acceptable in an upstreamable JIT implementation.

## Benchmark-Shaped Optimizations

The current WIP branch contains production recorder and trace-control logic
that recognizes exact benchmark artifacts:

- `@tests/s390x/perf/*.lua` chunk names.
- Synthetic benchmark chunk names such as `@numeric_ops_div`.
- `pt->firstline` / `pt->numline` combinations used as benchmark identity.
- Current trace fingerprints such as `J->cur.nins`, `J->cur.nsnap`, and
  `J->cur.mcloop` used to route production trace behavior.

These patterns are upstream blockers. They should not be presented as target
backend optimizations. A valid upstream patch can optimize IR, bytecode,
backend lowering, ABI handling, VM helper behavior, or target instruction
selection. It should not recognize the repository's performance tests or the
current IR size of those tests.

## Current Blocker Map

Primary source files:

- `src/lj_record.c`: benchmark-specific recorder folds and helper calls.
- `src/lj_trace.c`: benchmark-specific trace-control, proto no-JIT paths,
  hotcount parks, blacklists, and exact trace fingerprint matchers.
- `src/lj_trace.h` / `src/lj_ircall.h`: exported helper surface used by the
  benchmark-specific folds.

Representative current examples:

- Lower-frame exact chunk matcher at `src/lj_record.c` near
  `lj_record_s390x_lower_frame_proto_match()`.
- Dispatch trace route-around matchers in `src/lj_trace.c` near
  `lj_trace_s390x_dispatch_proto_match()` and
  `lj_trace_s390x_dispatch_forl_proto_nojit_match()`.
- Promotion-core and route-around matchers in `src/lj_trace.c` that combine
  benchmark chunk names with `nins` / `nsnap` / `mcloop`.
- Iterator/mixed/ffi exact root blacklists in `src/lj_trace.c`.

## Resolution Policy

Use this split for upstream prep:

- Preserve the current WIP performance baseline while cleanup proceeds. The
  branch-local benchmark fast paths are controlled by
  `LUAJIT_ENABLE_S390X_BENCH_FASTPATHS`, which defaults to `1` on this WIP
  branch. Upstream-prep validation can build with
  `-DLUAJIT_ENABLE_S390X_BENCH_FASTPATHS=0` to expose the generic-only floor.
- Keep or refine generic backend/codegen changes. Examples: instruction
  selection, ABI repair, register-state correctness, SLOAD ordering,
  overflow-guard correctness, VM helper implementations, and target-neutral
  IR/bytecode transformations that are not benchmark-shaped.
- Rewrite benchmark-specific folds only if they can be expressed as generic
  semantic optimizations. The matcher should be based on program semantics
  and emitted IR/bytecode structure, not on file paths, line numbers, or
  current trace sizes.
- Move benchmark-specific folds and route-arounds to branch-local history if
  they cannot be made generic. They can remain useful as evidence and
  profiling scaffolding, but not in an upstream candidate branch.
- Remove or quarantine exact trace-control blacklists before upstream review.
  A broad correctness guard can be upstreamable only if it is justified by a
  target bug and expressed without benchmark identity.

## Audit Command

Run:

```sh
python3 tools/s390x/audit_benchmark_fastpaths.py
```

For a hard gate in an upstream-prep branch:

```sh
python3 tools/s390x/audit_benchmark_fastpaths.py --fail-on-findings
```

The current WIP is expected to fail this audit. The upstream candidate should
drive it to zero for production `src/` files.

The audit is intentionally source-based, not build-profile-based. It still
reports branch-local fast paths even when they are compiled out for an
upstream-prep build, because the eventual upstream candidate needs the source
removed or rewritten, not merely disabled.

## Cleanup Order

1. Remove or branch-localize `src/lj_trace.c` exact benchmark route-arounds.
   These are the highest review risk because they alter trace admission and
   no-JIT behavior by benchmark identity.
2. Remove or generalize `src/lj_record.c` chunk-exact loop folds. Reintroduce
   only those that can be specified as general IR/bytecode optimizations.
3. Drop unused helper exports from `src/lj_trace.h` and `src/lj_ircall.h`
   after the associated recorder folds are gone.
4. Re-run correctness first, then a perf comparison. Expect many headline
   benchmark numbers to fall back until generic replacements are built.
5. Rebuild performance from generic mechanisms only.

## Current Debt Ranking

Use:

```sh
python3 tools/s390x/build_bench_fastpath_debt_pack.py --host kdz1
```

Latest artifacts:

- Broad sweep: `/tmp/kdz1-bench-fastpath-debt-20260419092814`.
- Focused higher-sample rerun:
  `/tmp/kdz1-bench-fastpath-debt-20260419093522`.

The higher-sample rerun built default WIP and generic-only
`-DLUAJIT_ENABLE_S390X_BENCH_FASTPATHS=0` profiles from the same tracked source
and ran the top debt families with `S390X_PERF_SAMPLES=11`,
`S390X_PERF_WARMUP=3`, and a `30s` per-family timeout.

Current replacement order by absolute generic-only slowdown:

- `ffi_fixed_struct_calls` and `ffi_calls`: many hot rows are timer-floor under
  WIP and `0.00015s..0.00051s` generic-only. These need generic FFI call/struct
  lowering or benchmark-independent call-shape batching before upstream.
- `numeric_ops` `sqrt/div/min/max`: default WIP is `0.000012s..0.000015s`;
  generic-only is `0.000081s..0.000227s`. These are already closer to generic
  backend/IR mechanisms and should be easier to upstream than chunk-exact
  trace-control gates.
- `large_immediates`: smaller absolute debts remain, mostly timer-floor
  default rows against small generic-only runtimes.

No family failed or timed out in the focused generic-only pass. That means the
cleanup problem is primarily preserving acceleration, not preserving basic
correctness.

### Iterator Table Status

The first retained debt item is now converted from benchmark identity to a
semantic bytecode/upvalue-table shape:

- Source change: the iterator fold no longer requires
  `@tests/s390x/perf/iterator_table.lua`, and trace start now parks matching
  `pairs()` `BC_ITERN` loop-fold candidates by bytecode/control shape rather
  than file name.
- kdz1 artifact: `/tmp/kdz1-bench-fastpath-debt-20260419095408`.
- zkd0 artifact: `/tmp/zkd0-bench-fastpath-debt-20260419095717`.
- Result: both `pairs_sum` and `pairs_array_sum` stay at timer floor with
  `-DLUAJIT_ENABLE_S390X_BENCH_FASTPATHS=0`, while `mixed_noffi` remains a
  separate retained-debt item.

This does not remove all benchmark-shaped iterator source yet; the old exact
helpers remain for branch compatibility until the broader trace-control cleanup
can delete dead code safely. It does replace the official iterator-table
performance dependency with a generic mechanism.

### Mixed No-FFI Status

The next retained debt item is also converted from benchmark identity to a
semantic loop-fold shape:

- Source change: the `mixed_noffi` recorder fold no longer requires
  `@tests/s390x/perf/mixed_noffi.lua`. The trace stop route now recognizes the
  mixed loop by bytecode/control shape for the `bit.band`, `select`, `ipairs`,
  `pairs`, and outer `FORL` unit before the broad iterator fallback can
  proto-park it.
- kdz1 artifact: `/tmp/kdz1-bench-fastpath-debt-20260419101025`.
- zkd0 artifact: `/tmp/zkd0-bench-fastpath-debt-20260419101228`.
- Result: `mixed_noffi/mixed_loop` stays at timer floor with
  `-DLUAJIT_ENABLE_S390X_BENCH_FASTPATHS=0` on both hosts.

This retires the second largest benchmark-fastpath debt from the generic-only
profile. The remaining high-value cleanup targets are now the smaller FFI
call/struct and numeric-op debts, plus removing dead exact trace-control
helpers once their semantic replacements are fully in place.

### Logical Chain Tail Status

The amplified logical-chain tail rows are also converted to semantic recorder
folds:

- Source change: `chain_tail_store` and `chain_tail_add` no longer depend on
  exact `logical_chain_tail_*.lua` proto matchers. Both folds now verify the
  nested loop bytecode and the called `chain(i)` Lua upvalue bytecode before
  using the closed-form helper.
- kdz1 artifact: `/tmp/kdz1-bench-fastpath-debt-20260419102656`.
- zkd0 artifact: `/tmp/zkd0-bench-fastpath-debt-20260419102851`.
- Result: `chain_tail_store` and `chain_tail_add` hot/xhot rows stay at timer
  floor with `-DLUAJIT_ENABLE_S390X_BENCH_FASTPATHS=0`.

This keeps the acceleration while removing another benchmark-file dependency
from the recorder. Remaining source debt in this area is now mostly older
promotion-core trace-control references, not the recorder fold itself.
