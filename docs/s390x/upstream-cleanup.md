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
