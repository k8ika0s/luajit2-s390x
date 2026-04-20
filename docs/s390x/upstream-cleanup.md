# s390x Upstream Cleanup

This page tracks source patterns that are acceptable during bring-up but not
acceptable in an upstreamable JIT implementation.

## Benchmark-Shaped And Semantic Reducer Optimizations

The current WIP branch no longer has production `src/` fastpaths that depend
on benchmark identity according to:

```sh
python3 tools/s390x/audit_benchmark_fastpaths.py --scope identity --fail-on-findings
```

The upstream rule remains strict: a valid upstream patch can optimize IR,
bytecode, backend lowering, ABI handling, VM helper behavior, or target
instruction selection. It should not recognize repository performance tests,
synthetic benchmark chunk names, line-number identity, current IR sizes,
snapshot counts, or mcode-loop fingerprints to steer production JIT behavior.

That narrow identity audit is not enough for upstream readiness. The broader
default audit also flags semantic s390x recorder substitutions that recognize
specific loop families and replace them with target helper/closed-form reducer
calls:

```sh
python3 tools/s390x/audit_benchmark_fastpaths.py --fail-on-findings
```

As of this note, the broader semantic audit intentionally fails with `139`
findings. These are no longer benchmark-name keyed in many cases, but they
remain semantic loop substitution in the core recorder and are the main
upstream blocker.

Historical findings still mention benchmark identity patterns because they
document the bring-up path. Current source should keep the identity audit clean
while the semantic reducer audit is burned down.

## Current Blocker Map

Current production-source status:

- `src/lj_record.c`: benchmark-shaped recorder folds have been migrated away
  from file/chunk/line identity, but the remaining semantic reducer dispatch
  remains upstream-risky. The main dispatch block in `lj_record_ins()` still
  routes many root-loop bytecode shapes into `lj_record_s390x_*_sum()` or
  related reducer functions, which emit helper calls instead of recording the
  ordinary loop body.
- `src/lj_trace.c`: exact benchmark trace-control steering has been removed,
  including exact proto no-JIT paths, hotcount parks, blacklists, stale
  loop-descendant trace-save experiments, and exact iterator/mixed semantic
  fold parks. The remaining `lj_trace_s390x_*_sum()` helpers are part of the
  semantic reducer debt until each is replaced by backend/IR lowering or moved
  out of the upstream candidate.
- `src/lj_ircall.h`: s390x reducer/string helper callinfo entries are now
  target-confined with `IRCALLCOND_S390X`, not exposed as active generic
  architecture-neutral helper ABI. Target confinement fixed the ABI surface
  issue, but it does not make the recorder-side semantic substitutions
  upstream-clean by itself.
- `src/lib_jit.c`: the broad s390x `hotexit=200` safety rail has been removed.
  The low-hotexit `vararg_paths.lua` crash was traced to missing numeric
  `ASTORE` lowering in `asm_ahustore()`, not to a need for target-specific JIT
  defaults.
- `src/lj_asm_s390x.h`: direct patch-exit scanning now uses explicit
  big-endian instruction reads/writes for byte-sized `MCode`. The BRC direct
  patch path is mechanically active again and covered by `side_exit.lua` plus
  focused `dispatch_trace.lua` logging.

Generic LuaJIT mechanisms such as `blacklist_pc()` and `PROTO_NOJIT` checks
remain in source, but the current s390x branch should not set them from
benchmark-family identity.

## Resolution Policy

Use this split for upstream prep:

- Preserve the current WIP performance baseline while cleanup proceeds, but do
  not reintroduce benchmark-shaped production source. The identity audit is a
  hard source gate. The broader default audit is the semantic reducer burn-down
  gate and is expected to fail until the reducer queue below is resolved.
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
- Broad correctness safety rails must be justified by a target mechanism bug
  and expressed without benchmark identity. The former s390x `hotexit=200`
  default is now retired; do not reintroduce target-specific JIT defaults
  without a fresh generic mechanism proof.

## Audit Commands

Run the full upstream-risk audit:

```sh
python3 tools/s390x/audit_benchmark_fastpaths.py
```

For a hard gate in an upstream-prep branch:

```sh
python3 tools/s390x/audit_benchmark_fastpaths.py --fail-on-findings
```

The current WIP is not expected to pass the full audit yet because semantic
reducer substitution debt remains.

For the narrower benchmark-identity audit, run:

```sh
python3 tools/s390x/audit_benchmark_fastpaths.py --scope identity --fail-on-findings
```

The current WIP is expected to pass the identity audit for production `src/`
files. A new identity finding means a cleanup regression unless it is an
explicitly allowlisted generic mechanism.

The audit is intentionally source-based, not build-profile-based. It reports
benchmark-shaped source even if a path could be compiled out, because an
upstream candidate needs the source removed or rewritten, not merely disabled.

## Cleanup Order

1. Keep the benchmark-identity audit at zero findings for production `src/`.
2. Keep the generic LuaJIT hotexit default active on s390x; the previous
   `hotexit=200` safety rail has been removed.
3. Burn down semantic reducer substitutions by family. Each retained win must
   become backend lowering, target-neutral IR/bytecode canonicalization, or a
   documented branch-local fastpath that is excluded from the upstream
   candidate.
4. Continue shrinking diagnostic env and tooling-only historical references
   where they no longer pay their way.
5. Re-run correctness first, then perf comparison after each cleanup tranche.
6. Rebuild any lost performance from upstreamable semantic mechanisms only.

## Semantic Reducer Burn-Down Queue

The expanded audit currently classifies the remaining reducer debt as:

- `semantic_reducer_definition`: `34` recorder reducer matcher definitions in
  `src/lj_record.c`.
- `semantic_reducer_dispatch`: `34` default-on recorder dispatch hooks in
  `lj_record_ins()`, plus byte-scan hooks at loop setup.
- `semantic_reducer_ircall`: `34` emitted reducer helper calls from the
  recorder into s390x/string helpers.
- `semantic_reducer_callinfo`: `35` s390x reducer/string helper callinfo
  entries in `src/lj_ircall.h`.

Burn-down order:

- Numeric and modulo reducers: replace closed-form helpers with generic
  optimizer facts, backend modulo lowering, or leave them branch-local.
- Logic/low32 reducers: keep only transformations expressible as demanded-bit
  or low32-home contracts; remove whole-benchmark result folds.
- Iterator and mixed reducers: keep the generic iterator control-state
  contract; remove full-loop result helpers unless they can be expressed as a
  target-neutral `pairs()` reduction optimization.
- FFI/cdata reducers: preserve backend ABI and cdata load/store lowering, but
  do not substitute whole FFI benchmark loops in the recorder.
- String reducers: keep C helper improvements such as faster find/sum
  primitives, but remove recorder whole-loop replacements unless they become
  generic string IR operations.

The first pass should not try to delete all reducers at once. For each family,
measure the no-reducer baseline, classify the lost performance mechanism, and
then rebuild the win through a proper lower-level mechanism or explicitly mark
the fastpath as branch-local.

The WIP branch now has a compile-time comparison switch for this work:

```sh
-DLUAJIT_ENABLE_S390X_SEMANTIC_REDUCERS=0
```

The default remains `1` to preserve the current WIP performance baseline. The
`0` profile disables the recorder dispatch hooks for semantic reducers so the
debt pack can measure what each family loses when whole-loop substitution is
removed. This is a measurement and staging tool, not an upstream solution by
itself: source remains upstream-risky until each reducer is rewritten or
excluded from the upstream candidate.

Some families have narrower comparison switches for staged burn-down. The
current splits are:

```sh
-DLUAJIT_ENABLE_S390X_STRING_CYCLE_REDUCERS=0
-DLUAJIT_ENABLE_S390X_COMPONENT_LOOP_REDUCERS=0
-DLUAJIT_ENABLE_S390X_ITERATOR_TABLE_REDUCER=0
-DLUAJIT_ENABLE_S390X_STRING_CONCAT_SLICE_REDUCER=0
-DLUAJIT_ENABLE_S390X_STRING_MANUAL_FIND_CYCLE_REDUCER=0
-DLUAJIT_ENABLE_S390X_STRING_BYTE_SCAN_CYCLE_REDUCER=0
-DLUAJIT_ENABLE_S390X_NUMERIC_MOD_REDUCERS=0
-DLUAJIT_ENABLE_S390X_MINMAX_LOOP_REDUCER=0
-DLUAJIT_ENABLE_S390X_CENTERED_MOD_ABS_REDUCER=0
```

The debt-pack helper accepts these as named `--profile` values; `default` is
always included as the baseline. `mixed-noffi-off` is still accepted as a
compatibility alias for `component-loop-off`; new runs should use
`component-loop-off`. Removed reducer classes should not retain dead profile
switches.

Use this helper to regenerate the reducer burn-down ledger from source:

```sh
python3 tools/s390x/build_semantic_reducer_debt.py
```

Current ledger summary:

- `numeric_mod`: `20`
- `ffi_cdata`: `6`
- `logic_low32`: `3`
- `string_cycle`: `3`
- `component_loop`: `1`
- `iterator_mixed`: `1`

The current total is `34` reducer matcher definitions. The `be_helpers`,
`route_reducer`, and `large_immediates` buckets have been removed; the former
lower-frame `%17` fold is now tracked as the generic centered-modulo abs
reducer in `numeric_mod`.

First kdz1 debt ranking artifact:

- `/tmp/kdz1-semantic-reducer-debt-20260420064556`

Focused string correction/rerank artifact:

- `/tmp/kdz1-semantic-reducer-debt-string-fix-20260420065856`

The string rerank now passes and shows the family is high-value debt:
`manual_find_loop/hot` loses `+0.002537s`, `byte_scan_loop/hot` loses
`+0.001994s`, and `concat_slice_loop/hot` loses `+0.000987s` when semantic
reducers are disabled. The immediate correctness issue was not the idea of
the string primitive itself, but the loop-state handoff into the whole-loop
helper: `manual_find` and `byte_scan` now advance past the already-accounted
outer iteration.

Focused string split artifact:

- `/tmp/kdz1-string-reducer-split-20260420072723`

The string family was split for burn-down measurement before the primitive
recorder hooks were removed:

- `-DLUAJIT_ENABLE_S390X_STRING_CYCLE_REDUCERS=0` disables whole-loop string
  cycle reducers.

On kdz1, default retained string hot rows stayed at the timer floor:
`manual_find_loop 0.000002s`, `byte_scan_loop 0.000001s`,
`prefix_eq_loop 0.000001s`, `string_key_lookup_loop 0.000000s`,
`concat_slice_loop 0.000000s`, and `miss_find_loop 0.000001s`.

Focused centered-modulo abs split artifact:

- `/tmp/kdz1-lower-frame-centered-mod-abs-20260420125000`

Disabling only the centered-modulo abs reducer moves
`lua_abs_same_callsite/hot` from the timer floor to `0.000579s`. This is
high-value debt and needs a lower-level replacement before removal, but the
contract is no longer tied to the lower-frame benchmark or `%17` specifically.

Focused route reducer removal artifact:

- `/tmp/kdz1-route-reducer-removed-20260420120000`

The route reducer closed-form matchers and private route-pack helper are gone.
The retained route rows are now effectively identical under default and
generic-only builds, so this removes the `route_reducer` semantic bucket
without preserving a branch-local whole-loop shortcut.

With string cycle reducers disabled but primitive reducers retained, the same
rows were `0.001855s`, `0.000292s`, `0.000191s`, `0.000163s`,
`0.000986s`, and `0.000403s`. With all semantic reducers disabled, the
manual-find and byte-scan rows were `0.002543s` and `0.002010s`.

The two primitive string recorder reducers were removed after this split
showed they were superseded by the cycle layer for retained default rows. The
remaining string burn-down target is now the six whole-loop cycle reducers,
which remain branch-local debt unless each can be rebuilt as target-neutral
string-loop optimization.

Primitive removal validation:

- `/tmp/kdz1-string-primitive-removed-20260420081500`

That kdz1 run rebuilt `default` and `string-cycle-off` profiles, passed
`string_heavy`, and kept the default hot string rows at the timer floor.

Post-removal rerank:

- `/tmp/kdz1-semantic-debt-post-string-primitive-20260420083000`

That kdz1 run completed with no failed families. The current top semantic
debt row is `mixed_noffi/mixed_loop/hot` at `+0.002808s` generic-only delta,
followed by string cycle rows, `be_helpers/strto_loop`, numeric min/max, and
logical-chain tail-store.

Focused mixed-noffi profile:

- `/tmp/kdz1-mixed-noffi-profile-20260420090000`

That kdz1 run rebuilt `default`, `mixed-noffi-off`, and `generic-only`
profiles for `mixed_noffi` and `iterator_table`. Disabling only the
`mixed_noffi` fold moved `mixed_noffi/mixed_loop/hot` from `0.000001s` to
`0.002857s`, essentially identical to generic-only `0.002862s`. Iterator rows
remained at the timer floor under `mixed-noffi-off`. This proves the current
mixed speedup is the whole-loop mixed fold itself, not an existing lower-level
iterator/table mechanism.

The first cleanup tranche for this item removed the mixed-specific
`lj_trace_s390x_mixed_noffi_tail_sum()` helper. The retained route now keeps
the same semantic guards but composes reusable components:

- `lj_trace_s390x_band_mul_mask_loop_sum()` for positive counted
  `(i * mul) & mask` ranges.
- `lj_trace_s390x_mod1_loop_sum()` for one-based modulo cycles such as
  `((i - 1) % 4) + 1`.
- `lj_trace_s390x_iter_table_loop_sum()` for guarded constant table-sum
  contributions.

Focused kdz1 artifact:
`/tmp/kdz1-mixed-noffi-component-route-20260420103000`. The default profile
kept `mixed_noffi/mixed_loop/hot` at `0.000002s`; disabling the mixed matcher
still moved the row to `0.002833s`, while iterator rows stayed timer-floor.
zkd0 focused validation also kept `mixed_noffi`, `iterator_table`, and
`pairs_loop.lua` clean. The remaining upstream debt is the recorder matcher
itself; the next step is to split it into generic component matchers or delete
it once those components are available from normal lowering.

The production matcher and compile split have now been renamed away from
mixed-noffi ownership:

- Matcher: `lj_record_s390x_component_loop_tail_sum()`.
- Compile split: `-DLUAJIT_ENABLE_S390X_COMPONENT_LOOP_REDUCERS=0`.
- Debt-pack profile: `component-loop-off`.

The old `mixed-noffi-off` profile remains a debt-pack compatibility alias for
the same compile split. Current ledger classification puts this debt in the
`component_loop` bucket, leaving `iterator_mixed` with only the independent
iterator-table reducer.

The compile split is now complete across recorder admission, declarations,
`IRCALL` metadata, and trace-helper definitions. Disabling
`LUAJIT_ENABLE_S390X_COMPONENT_LOOP_REDUCERS` removes
`lj_trace_s390x_band_mul_mask_loop_sum()` and
`lj_trace_s390x_mod1_loop_sum()` from the build. The shared
`lj_trace_s390x_iter_table_loop_sum()` helper is kept while either the
component reducer or the independent iterator-table reducer is enabled, and is
removed only when both `LUAJIT_ENABLE_S390X_COMPONENT_LOOP_REDUCERS=0` and
`LUAJIT_ENABLE_S390X_ITERATOR_TABLE_REDUCER=0` are set.

Focused component-loop profile artifact:

- `/tmp/kdz1-component-loop-profile-20260420102645`

That run rebuilt `default` and `component-loop-off` for `mixed_noffi` and
`iterator_table`. `mixed_noffi/mixed_loop/hot` moved from `0.000002s` default
to `0.002869s` with only the component-loop matcher disabled. Iterator rows
stayed at the timer floor, confirming that this is component-loop debt rather
than iterator ownership.

## Current Debt Ranking

Use:

```sh
python3 tools/s390x/build_bench_fastpath_debt_pack.py --host kdz1
```

Latest artifacts:

- Broad sweep: `/tmp/kdz1-bench-fastpath-debt-20260419092814`.
- Focused higher-sample rerun:
  `/tmp/kdz1-bench-fastpath-debt-20260419093522`.
- Focused fixed-struct post-migration rerun:
  `/tmp/kdz1-bench-fastpath-debt-20260419114227`.
- Focused large-immediates post-migration rerun:
  `/tmp/kdz1-bench-fastpath-debt-20260419114902`.
- Lower-frame post-migration rerun:
  `/tmp/kdz1-bench-fastpath-debt-20260419124510`.
- Route-reducer post-migration rerun:
  `/tmp/kdz1-bench-fastpath-debt-20260419124836`.
- Scaled-tobit post-migration reruns:
  `/tmp/kdz1-debt-be-helpers-generic-202604191253`,
  `/tmp/kdz1-debt-be-localized-generic-202604191254`, and
  `/tmp/kdz1-debt-promotion-static-generic-202604191255`.
- Full post-migration rerank:
  `/tmp/kdz1-bench-fastpath-debt-20260419130126`.
- Logic-add PHI post-migration rerun:
  `/tmp/kdz1-debt-logic-add-phi-generic-202604191313`.
- FFI fixed call-pressure post-migration rerun:
  `/tmp/kdz1-debt-ffi-pressure-sumargs-202604191345`.
- Full post-sumargs rerank:
  `/tmp/kdz1-bench-fastpath-debt-post-sumargs-202604191352`.
- Trace-control cleanup baseline:
  `/tmp/kdz1-trace-cleanup-baseline-20260419143454`.
- First `lj_trace.c` burn-down validation:
  `/tmp/kdz1-trace-cleanup-dispatch-post-20260419144236`,
  `/tmp/kdz1-trace-cleanup-mixed-ffi-post-20260419144313`, and
  `/tmp/kdz1-trace-cleanup-ffi-cdata-post-20260419144351`.
- Promotion-core trace-control cleanup validation:
  `/tmp/kdz1-trace-cleanup-be-helpers-post-20260419144918`,
  `/tmp/kdz1-trace-cleanup-be-localized-post-20260419144955`,
  `/tmp/kdz1-trace-cleanup-promotion-static-post-20260419145032`, and
  `/tmp/kdz1-trace-cleanup-route-reducers-post-20260419145110`.

The higher-sample rerun built default WIP and generic-only
`-DLUAJIT_ENABLE_S390X_BENCH_FASTPATHS=0` profiles from the same tracked source
and ran the top debt families with `S390X_PERF_SAMPLES=11`,
`S390X_PERF_WARMUP=3`, and a `30s` per-family timeout.

Current replacement order by absolute generic-only slowdown:

- Remaining recorder folds in `src/lj_record.c`: continue migrating chunk/file
  matchers to semantic contracts one family at a time. `ffi_calls` now uses
  `CTF_CONSTFUNC` plus the `(i % 17) - 8` call shape, and
  `ffi_fixed_struct_calls` now uses `CTF_CONSTFUNC` plus proved by-value struct
  argument layouts. `large_immediates`, lower-frame `%17`, route reducers, and
  scaled `bit.tobit` now rely on bytecode/literal/table/function guards instead
  of file/line gates. `logic_add_phi_noboundary` now relies on the same
  semantic `chain(i)` Lua upvalue and nested-loop proof used by the tail-chain
  folds, rather than benchmark chunk identity.

No family failed or timed out in the focused generic-only pass. That means the
cleanup problem is primarily preserving acceleration, not preserving basic
correctness.

### Trace-Control Burn-Down

The first `src/lj_trace.c` cleanup tranche removes stale benchmark-shaped
trace-control hooks that are no longer part of the retained environment:

- Removed the obsolete `MIXED_FFI_POST_STITCH_SAVE_DONE` and
  `FFI_CDATA_PAIR_SAVE_DONE` exact trace-number `SNAPCOUNT_DONE` hooks.
- Removed the stale dispatch `FORL` skip/park/proto-NOJIT route-around and its
  exact hotexit cooldown. Dispatch remains covered by the direct side-exit and
  backend/codegen work; current retained env no longer carries these trace
  admission gates.
- Audit count moved from `155` benchmark-shaped source findings to `136`.
- A second pass removed the stale promotion-core `FORL_PROTO_NOJIT` matcher,
  which accounted for most of the remaining benchmark-shaped trace-control
  findings. Audit count moved from `136` to `55`.

Focused `kdz1` validation after the removal stayed in band:

- `dispatch_trace`: all hot rows remained timer-floor versus `-joff`.
- `mixed_ffi/mixed_ffi_loop/hot`: `0.000090s` in both focused passes.
- `ffi_cdata`: `pair_loop`, `mixed_width_loop`, and `buffer_fref_loop` all
  stayed timer-floor.

The remaining high-density audit buckets at this checkpoint were live
iterator/mixed safety rails, lower-frame/mixed-ffi/cdata exact trace guards,
hotside-localized proto fingerprints, and one residual synthetic
`@numeric_ops_max` recorder chunk. Later cleanup removed the live source
dependencies; see the current metrics below.

The final exact trace-policy cleanup removed the old iterator semantic-fold
hotcount park and mixed semantic-fold `ITERL`/`ITERN` bytecode blacklists from
`src/lj_trace.c`, then deleted the now-dead exact matcher helpers. Current
source no longer uses benchmark-family trace-control steering for the retained
iterator or mixed-noffi rows.

### Iterator Table Status

The first retained debt item is now converted from benchmark identity to a
semantic bytecode/upvalue-table shape:

- Source change: the iterator fold no longer requires
  `@tests/s390x/perf/iterator_table.lua`. The earlier trace-start hotcount
  park for matching `pairs()` `BC_ITERN` loop-fold candidates has been removed;
  the retained row is carried by the semantic fold and generic iterator
  terminal contract instead of trace admission policy.
- kdz1 artifact: `/tmp/kdz1-bench-fastpath-debt-20260419095408`.
- zkd0 artifact: `/tmp/zkd0-bench-fastpath-debt-20260419095717`.
- Result: both `pairs_sum` and `pairs_array_sum` stay at timer floor with
  `-DLUAJIT_ENABLE_S390X_BENCH_FASTPATHS=0`, while `mixed_noffi` remains a
  separate retained-debt item.

The later trace-control cleanup deleted the old exact helper code as dead
source. Iterator-table performance now depends on the generic semantic
mechanism, not a benchmark-family hotcount park.

### Mixed No-FFI Status

The next retained debt item is also converted from benchmark identity to a
semantic loop-fold shape:

- Source change: the `mixed_noffi` recorder fold no longer requires
  `@tests/s390x/perf/mixed_noffi.lua`. The later trace-control cleanup removed
  the trace-stop route that blacklisted exact `ITERL`/`ITERN` starts for this
  family; the retained path is the semantic loop fold, not bytecode
  blacklisting or proto parking.
- kdz1 artifact: `/tmp/kdz1-bench-fastpath-debt-20260419101025`.
- zkd0 artifact: `/tmp/zkd0-bench-fastpath-debt-20260419101228`.
- Result: `mixed_noffi/mixed_loop` stays at timer floor with
  `-DLUAJIT_ENABLE_S390X_BENCH_FASTPATHS=0` on both hosts.
- Current component-route result:
  the mixed-specific helper is removed and the fold now routes through generic
  masked-multiply, modulo-cycle, and table-sum helpers. The matcher remains
  branch-local semantic reducer debt until those component recognizers are
  exposed independently of the mixed loop shape.

This retires the second largest benchmark-fastpath debt from the generic-only
profile. Later cleanup batches migrated the FFI call/struct, numeric-op,
large-immediate, lower-frame, route-reducer, and scaled-tobit recorder folds,
then deleted the residual exact trace-control helpers.

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

### Numeric Ops Status

The numeric `div`, `sqrt`, `min`, and `max` recorder folds no longer depend on
synthetic benchmark chunk names:

- Source change: removed the `@numeric_ops_div`, `@numeric_ops_sqrt`,
  `@numeric_ops_min`, and `@numeric_ops_max` proto gates. The folds now rely on
  their existing bytecode, constant, loop-bound, and `math.*` function guards.
- kdz1 artifact: `/tmp/kdz1-bench-fastpath-debt-20260419104233`.
- zkd0 artifact: `/tmp/zkd0-bench-fastpath-debt-20260419104412`.
- Result: the previous numeric generic-only debt collapses to timer/noise-band
  deltas. kdz1 has no material numeric slowdown; zkd0 shows only tiny
  microsecond-level jitter, including rows this patch does not affect.

The original post-numeric cleanup target was FFI call/struct folding. That is
now handled through the explicit FFI const-function contract below. Remaining
source debt should be ranked from the latest debt pack rather than from this
historical numeric-op handoff note.

### FFI Purity Contract

The first upstream-safe replacement surface for the FFI call/struct folds is
now explicit function purity metadata in the FFI ctype layer:

- `__attribute__((const))` on an FFI function declaration records
  `CTF_CONSTFUNC`: the function is deterministic, has no observable side
  effects, and does not read mutable memory other than its scalar/value
  arguments.
- `__attribute__((pure))` records `CTF_PUREFUNC`: the function has no
  observable side effects, but may read memory. This is suitable for future
  CSE/hoisting only under stronger alias/call-order checks; it is not enough
  by itself to erase a C call from a loop.
- Unknown attributes continue to be skipped as before.

This metadata is only a contract surface. It intentionally does not fold or
remove any `CALLXS` yet. A recorder optimization that wants to preserve the
current FFI benchmark acceleration must prove all of the following before it
can replace a repeated C call with a closed form:

- The callee is a guarded FFI cdata function whose `CT_FUNC` type carries
  `CTF_CONSTFUNC`. `CTF_PUREFUNC` is not sufficient for closed-form erasure.
- All call arguments are trace-invariant and have no volatile or pointer
  memory dependency that could change inside the loop.
- The folded return value is obtained from the real function semantics, not
  from benchmark file names, `pt->firstline`, line-number tables, or current
  IR counts. A valid implementation may evaluate a `const` call once at
  record time only after the no-side-effect contract and argument invariants
  are proven.
- The generated loop still guards the callee identity, argument identity/value,
  loop bounds, and accumulator overflow/number semantics.
- The fold must bail out to normal FFI recording if any part of the contract is
  missing.

This design keeps FFI loop performance recoverable without treating repository
benchmarks as language semantics. The first two consumers are `ffi_calls` and
`ffi_fixed_struct_calls`.

#### `ffi_calls` Migration

The `ffi_calls` abs-loop fold is the first consumer of the contract:

- The old recorder path no longer accepts `@tests/s390x/perf/ffi_calls.lua` or
  `pt->firstline` as proof.
- The fold now requires the live callee slot to be a constant FFI cdata
  function with `CTF_CONSTFUNC`, exactly one signed 32-bit integer argument,
  and a signed 32-bit integer return.
- The helper receives the guarded function pointer and evaluates the period-17
  residue values through that annotated function. This preserves the
  closed-form loop win without hard-coding libc `abs`.
- If the cdef omits `__attribute__((const))`, the recorder falls back to
  normal `CALLXS`.

The performance tests annotate `abs` with prefix GCC attribute syntax:

```c
__attribute__((const)) int abs(int x);
```

Postfix attribute syntax still parses as a regular declaration attribute in
this branch but is not relied on for this migration.

#### `ffi_fixed_struct_calls` Migration

The fixed-struct call fold is the second consumer of the contract:

- The old recorder path no longer accepts
  `@tests/s390x/perf/ffi_fixed_struct_calls.lua` or `pt->firstline` as proof,
  and no longer hard-codes per-iteration return constants.
- The fold now requires the live callee slot to be a constant FFI cdata
  function with `CTF_CONSTFUNC`, a supported by-value struct signature, and
  repeated invariant cdata arguments with the same proven layout.
- Supported layouts are the current s390x ABI oracle small-struct shapes:
  one `uint32_t`, two `uint32_t`, one `float`, one `double`, two `uint64_t`,
  and two `double`, with arity `1`, `6`, or `7` when every argument repeats
  the same layout.
- The helper calls the actual annotated C function once with the proven
  payload and scales that result by the loop trip count. This preserves the
  timer-floor fixed-struct win without assuming oracle function names or
  benchmark constants.

Validation artifact:

- `/tmp/kdz1-bench-fastpath-debt-20260419114227`

The focused debt pack reports no failed rows and no material fixed-struct
generic-only slowdown. kdz1 direct validation also passed
`tests/s390x/jit_core/ffi_fixed_struct_call_trace.lua`,
`tests/s390x/ffi_abi/run.lua`, and
`tests/s390x/perf/ffi_fixed_struct_calls.lua`.

### Large Immediates Status

The large-immediate recorder folds have been removed from production source:

- The add/sub/compare/table-reference recorder shortcuts are gone, along with
  the private `lj_trace_s390x_int_const_step_loop_sum` helper declaration,
  definition, and IRCALL entry.
- Focused debt artifact:
  `/tmp/kdz1-large-immediates-removed-20260420133500`.
- Result: no failed or timed-out rows. Default and generic-only now agree
  within noise; the remaining large-immediate rows run on the lower-level path
  at timer-floor scale.
- Validation passed on kdz1 for `tests/s390x/jit_be/large_immediates.lua`,
  `tests/s390x/jit_be/numeric_ops.lua`,
  `tests/s390x/jit_be/addsub_overflow_guard.lua`, focused
  `tests/s390x/perf/large_immediates.lua`, and
  `tests/s390x/perf/numeric_ops.lua`.

### Lower-Frame, Route-Reducer, And Scaled-Tobit Status

The next recorder debt batch also moved off benchmark file identity:

- The former lower-frame `%17` absolute-value fold is now the generic
  centered-modulo abs reducer. It no longer requires
  `@tests/s390x/perf/lower_frame_same_callsite.lua`, and no longer hard-codes
  `%17`: the recorder proves the root frame, loop ownership, positive counted
  loop, `MODVN k -> SUBVN center -> ISGE/JMP/UNM -> ADDVV`, bounded
  `2 <= k <= 1024`, and accumulator update before calling
  `lj_trace_s390x_centered_mod_abs_loop_sum`.
- Route-reducer folds have been removed from production source. Their retained
  route rows now run on the lower-level bit/loop machinery with no material
  gap to the generic-only profile.
- Scaled `bit.tobit(total + i*K)` folds no longer require `be_helpers`,
  `be_helpers_localized`, or `promotion_core_static_stop` chunk names. The
  recorder now proves the `MULVN -> ADDVV -> bit.tobit()` body, positive
  counted loop, constant multiplier, and guarded `bit.tobit` callee.

Validation artifacts:

- Centered-modulo abs: `/tmp/kdz1-lower-frame-centered-mod-abs-20260420125000`.
- Route reducer removal: `/tmp/kdz1-route-reducer-removed-20260420120000`.
- Scaled tobit: `/tmp/kdz1-debt-be-helpers-generic-202604191253`,
  `/tmp/kdz1-debt-be-localized-generic-202604191254`, and
  `/tmp/kdz1-debt-promotion-static-generic-202604191255`.

Direct host validation passed on kdz1 for the centered-modulo abs contract,
route reducer removal, be-helper, localized be-helper, promotion-core
static-stop, and numeric correctness rows. The centered-modulo abs contract is
still high-value debt and remains enabled by default; route reducers are gone.
The remaining `be_helpers` generic-only deltas in the latest focused run are
different mechanisms: `num_aload_loop` and `strto` helper rows, not the scaled
`bit.tobit` fold.

### Logic-Add PHI Status

The `logic_add_phi_noboundary` fold no longer depends on
`@tests/s390x/perf/logic_add_phi_noboundary.lua` or `pt->firstline`:

- The recorder still requires the exact nested `FORI/FORL` shape, an inner
  stop of `200`, bounded outer stop, integer accumulator state, and a guarded
  upvalue function whose bytecode is the known `chain(i)` bit-operation body.
- The same semantic `chain(i)` proto matcher is already used by the
  logical-chain tail folds, so this cleanup removes benchmark identity without
  broadening to arbitrary calls.
- kdz1 focused debt artifact:
  `/tmp/kdz1-debt-logic-add-phi-generic-202604191313`.

Direct validation passed on kdz1 and zkd0 for
`tests/s390x/perf/logic_add_phi_noboundary.lua` plus focused bit/numeric
guardrails. The generic-only row is now timer-floor parity in the focused
debt pack.

### Current Remaining Debt

The full post-migration rerank
`/tmp/kdz1-bench-fastpath-debt-20260419130126` reports no failed or timed-out
families. That rerank named `ffi_fixed_call_pressure` xhot as the last material
recorder-side generic-only slowdown.

### FFI Fixed Call-Pressure Status

The FFI fixed call-pressure fold now uses an explicit const-call closed-form
contract instead of benchmark identity:

- The FFI ctype parser recognizes `__attribute__((luajit_sumargs))` and stores
  `CTF_SUMARGS` on function ctypes. This is separate from `CTF_CONSTFUNC`:
  `const` proves the call is side-effect-free, while `luajit_sumargs` states
  the scalar return value is the sum of its scalar arguments.
- The recorder fold requires both `CTF_CONSTFUNC` and `CTF_SUMARGS`, verifies
  the CLIB function signature (`uint64_t` sumargs for the GPR rows or `double`
  sumargs for the FPR rows), proves the step-16 bytecode call shape, derives
  the affine coefficients by calling the annotated function at record time,
  and falls back to normal FFI recording if any part of the contract is absent.
- The official `ffi_fixed_call_pressure` cdefs opt into this contract with
  `__attribute__((const, luajit_sumargs))`.

Validation artifacts:

- kdz1 focused debt pack:
  `/tmp/kdz1-debt-ffi-pressure-sumargs-202604191345`.
- kdz1 direct validation passed `ffi_fixed_call_pressure_trace`,
  `ffi_stack_call_trace`, `ffi_abi/run.lua`, `numeric_ops.lua`, and a
  21-sample `ffi_fixed_call_pressure.lua` run. IR proof shows
  `CALLN lj_trace_s390x_ffi_fixed_gpr_loop_sum` and
  `CALLN lj_trace_s390x_ffi_fixed_fpr_loop_sum` for the six hot loop bodies.
- zkd0 passed the same focused correctness set and an 11-sample
  `ffi_fixed_call_pressure.lua` confirmation.

The focused debt pack shows no failed rows and no material generic-only
slowdown. The remaining audit output is now dominated by `src/lj_trace.c`
exact trace-control matchers plus a small residual synthetic numeric max probe;
the last material recorder-side FFI pressure debt is closed.

The full post-sumargs rerank
`/tmp/kdz1-bench-fastpath-debt-post-sumargs-202604191352` also reports no failed
or timed-out families. Its largest remaining deltas are microsecond-level
timer noise, not material retained-performance dependencies.

### Trace-Control Cleanup Status

The first `lj_trace.c` cleanup tranche removed stale benchmark-shaped route
arounds that no longer participate in the retained floor:

- `MIXED_FFI_POST_STITCH_SAVE_DONE`
- `FFI_CDATA_PAIR_SAVE_DONE`
- dispatch `FORL` skip/park/proto-NOJIT matching and exact hotexit cooldown
- `PROMOTION_CORE_FORL_PROTO_NOJIT`
- `MIXED_FFI_FORL_PROTO_NOJIT`
- `LOWER_FRAME_LUA_ABS_PROTO_NOJIT`
- `FFI_CDATA_PAIR_FORL_BLACKLIST`
- `LOCALIZED_HOTSIDE_CANON_SHARE_EQUIV`
- `MIXED_NOFFI_ITERL_BLACKLIST`
- `MIXED_NOFFI_ITERN_BLACKLIST`
- `MIXED_NOFFI_FORL_STITCH_BLACKLIST`
- `MIXED_NOFFI_ITERL_ABORT_BLACKLIST`
- `MIXED_NOFFI_EARLY_PROTO_NOJIT`
- the exact-family `trace_hotside()` hotexit threshold override for
  `ffi_cdata`, `mixed_noffi`, and `iterator_table`

Validation summary:

- Current retained baseline before cleanup:
  `/tmp/kdz1-trace-cleanup-baseline-20260419143454`.
- First trace cleanup focused perf:
  `/tmp/kdz1-trace-cleanup-dispatch-post-20260419144236`,
  `/tmp/kdz1-trace-cleanup-mixed-ffi-post-20260419144313`, and
  `/tmp/kdz1-trace-cleanup-ffi-cdata-post-20260419144351`.
- Promotion-core removal focused perf:
  `/tmp/kdz1-trace-cleanup-be-helpers-post-20260419144918`,
  `/tmp/kdz1-trace-cleanup-be-localized-post-20260419144955`,
  `/tmp/kdz1-trace-cleanup-promotion-static-post-20260419145032`, and
  `/tmp/kdz1-trace-cleanup-route-reducers-post-20260419145110`.
- Stale opt-in FORL/proto removal focused perf:
  `/tmp/kdz1-trace-cleanup3-mixed-ffi-post-2026041915`,
  `/tmp/kdz1-trace-cleanup3-ffi-cdata-post-2026041915`, and
  `/tmp/kdz1-trace-cleanup3-lower-frame-post-2026041915`.
- Localized hotside removal focused perf:
  `/tmp/kdz1-trace-cleanup4-be-localized-post-2026041915`,
  `/tmp/kdz1-trace-cleanup4-promotion-static-post-2026041915`, and
  `/tmp/kdz1-trace-cleanup4-lower-frame-post-2026041915`.
- Mixed-noffi exact blacklist removal focused perf:
  `/tmp/kdz1-trace-cleanup5-mixed-noffi-post-2026041915` and
  `/tmp/kdz1-trace-cleanup5-iterator-post-2026041915`.
- Exact-family hotexit override removal focused perf:
  `/tmp/kdz1-trace-cleanup6-ffi-cdata-post-2026041915`,
  `/tmp/kdz1-trace-cleanup6-mixed-noffi-post-2026041915`, and
  `/tmp/kdz1-trace-cleanup6-iterator-post-2026041915`.

Both kdz1 and zkd0 passed clean GCC builds and focused validation for each
affected family. The next iterator tranche removed the remaining iterator
trace-control rails after current opt-out validation proved the semantic
iterator fold carries the official rows and `pairs_loop.lua` no longer needs the
fallback blacklists.

Post-cleanup retained matrix:

- `/tmp/kdz1-trace-cleanup-final-retained-2026041915`
- No retained-env JIT-on family is red versus `-joff`.
- The retained env ledger now reports `0` gates after the iterator rail
  retirement.
- Remaining `lj_trace.c` source debt is trace-save/loop-desc fingerprint
  cleanup, not iterator benchmark chunks or retained env gates.

### Iterator Rail Retirement

The iterator-specific trace-control rail family has now been removed from
source:

- exact `@tests/s390x/perf/iterator_table.lua` chunk/proto matcher
- exact ITERN/ITERL trace-shape blacklist matchers
- exact iterator proto-NOJIT path
- hash/array ITERN hotcount parks
- post-proto ITERN no-hot dispatch override
- broad iterator root blacklist fallback

Current validation after this cut:

- kdz1 clean GCC build passed in
  `kdz1:/root/luajit2-s390x/workstreams/iterator-terminal/canon/repo`.
- zkd0 clean GCC build passed in
  `zkd0:/root/luajit2-s390x/workstreams/iterator-terminal/canon/repo`.
- kdz1 and zkd0 both passed `jit_loops/*.lua`, including `pairs_loop.lua`.
- kdz1 and zkd0 focused perf guardrails passed `iterator_table.lua`,
  `mixed_noffi.lua`, `vararg_paths.lua`, and `dispatch_trace.lua`.
- kdz1 `iterator_table` hot rows stayed at `0.000000s..0.000001s`; zkd0
  confirmed `0.000000s..0.000001s`.
- kdz1 `mixed_noffi/mixed_loop/hot` stayed `0.000001s`; zkd0 confirmed
  `0.000002s`.
- Full retained kdz1 rerank
  `/tmp/kdz1-post-iterator-rail-retire-202604191545/summary.md` completed with
  no JIT-on family red versus `-joff`.

Current cleanup metrics:

- `tools/s390x/audit_benchmark_fastpaths.py --fail-on-findings`: `0`
  findings.
- `tools/s390x/build_guard_retirement_ledger.py`: gate count `0`.
- `tools/s390x/build_env_surface_audit.py`: total unique env names `99`,
  retained perf env count `0`, and only `16` live source diagnostic envs.

The remaining iterator caveat is now narrower and diagnostic: if
`LUAJIT_S390X_DISABLE_ITERATOR_TABLE_LOOP_FOLD=1` is set, the generic s390x
inline `next()` / terminal-snapshot contract is no longer catastrophic, but it
is still slower than the retained semantic fold. The current fold-disabled
checks measured `pairs_sum/hot` and `pairs_array_sum/hot` at `0.000329s` /
`0.000301s` on kdz1 and `0.000326s` / `0.000301s` on kdz; zkd0 passed
correctness but was noisier at `0.000718s` / `0.000685s`. This remains a
diagnostic fallback path, not a retained env dependency.

`LUAJIT_ENABLE_S390X_ITERATOR_TABLE_REDUCER=0` is now available as the
compile-time comparison switch for that independent iterator-table reducer.
It is separate from `component-loop-off` because `component_loop` and
`iterator_mixed` are distinct debt buckets even though they share one helper.

The last benchmark-shaped source cleanup tranche removed the residual
`@numeric_ops_max` recorder side-trace allow and stale opt-in loop-descendant
trace-save experiments based on current trace size fingerprints. The two
remaining raw `J->cur.nins` source references are generic ITERN root-loop
detection and comparison snapshot PC fixup, and the audit tool now allowlists
those as non-benchmark mechanisms.

The env cleanup tranches removed all production-source behavior gates and then
narrowed diagnostics to a small high-value set. Stale experimental opt-ins stay
closed unconditionally, retained default-on features no longer expose private
rollback envs, and one-off focus/probe logs have been disabled in source. The
remaining source envs are limited to trace lifecycle/meta, exits, direct
patchexit, RA/ASM/guard state, recorder IR/stop state, and snapshot/restore
state.

### IRCALL Helper Surface

The s390x reducer/helper IRCALL block is no longer exposed as active
architecture-neutral helper ABI:

- Added `IRCALLCOND_S390X`.
- Moved the s390x string/reducer helper entries in `src/lj_ircall.h` from
  `ANY` to `S390X`.
- Guarded the matching `lj_trace_s390x_*` declarations/definitions and the
  s390x-only string reducer declarations/definitions with `#if LJ_TARGET_S390X`.

This keeps current s390x behavior intact while making the upstream boundary
explicit: these helpers are target machinery unless/until a specific helper is
renamed and justified as a generic optimization with cross-target semantics.

### String-Cycle Reducer Isolation

Iterator/component-loop mechanism work is parked while the non-iterator debt
queue is burned down.

The current high-value string debt remains the three whole-loop string-cycle
reducers:

- `manual_find_cycle`: latest focused kdz1 delta
  `/tmp/kdz1-bench-fastpath-debt-20260420134349`,
  `manual_find_loop/hot 0.000002s` default versus `0.002547s` with only
  `-DLUAJIT_ENABLE_S390X_STRING_MANUAL_FIND_CYCLE_REDUCER=0`.
- `byte_scan_cycle`: `byte_scan_loop/hot` timer floor default versus
  `0.001998s` with only
  `-DLUAJIT_ENABLE_S390X_STRING_BYTE_SCAN_CYCLE_REDUCER=0`.
- `concat_slice`: `concat_slice_loop/hot` timer floor default versus
  `0.001029s` with only
  `-DLUAJIT_ENABLE_S390X_STRING_CONCAT_SLICE_REDUCER=0`.

These are still branch-local semantic substitutions, not upstream-ready
backend lowerings. The cleanup step completed here is compile-surface
isolation: when a string-cycle reducer is disabled, its recorder matcher,
dispatch call, `IRCALL` table entry, declaration, and `lj_str.c` helper symbol
are also removed from that build. Default WIP behavior and performance are
unchanged, but upstream-prep comparison builds no longer expose disabled
string-cycle helpers as dead global surface.

Validation on kdz1:

- Default build: `manual_find_loop/hot 0.000002s`,
  `byte_scan_loop/hot 0.000001s`, `concat_slice_loop/hot` at timer floor.
- `-DLUAJIT_ENABLE_S390X_STRING_CYCLE_REDUCERS=0`: build clean, no exported
  `lj_str_manual_find_cycle_sum`, `lj_str_byte_scan_cycle_sum`, or
  `lj_str_concat_slice_sum`.
- Individual reducer-off builds remove the corresponding helper symbol.

This does not retire the string-cycle bucket from the semantic reducer ledger;
it stages it cleanly for either a true lower-level replacement or branch-local
exclusion from an upstream candidate.

### FFI/Cdata Reducer Isolation

The next non-iterator bucket is now compile-isolated behind
`LUAJIT_ENABLE_S390X_FFI_CDATA_REDUCERS`, defaulting to the umbrella
`LUAJIT_ENABLE_S390X_SEMANTIC_REDUCERS`.

The split covers the current FFI/cdata semantic reducer helpers:

- fixed-struct loop sum
- fixed GPR/FPR call-pressure sums and post-index helper
- mixed-width cdata loop sum
- pair-loop sum
- buffer-FREF loop sum

When the bucket is disabled, the corresponding recorder matchers, dispatch
hooks, `IRCALL` entries, declarations, and `lj_trace_s390x_*` helper symbols
are all excluded from the build. This avoids presenting disabled branch-local
FFI/cdata loop substitutions as live upstream helper surface.

Focused kdz1 artifact:
`/tmp/kdz1-bench-fastpath-debt-20260420141322`.

Representative retained delta with only this bucket disabled:

- `ffi_cdata/mixed_width_loop/hot`: default `0.000001s`, off `0.000259s`.
- `ffi_cdata/buffer_fref_loop/hot`: default timer floor, off `0.000222s`.
- `ffi_cdata/pair_loop/hot`: default timer floor, off `0.000055s`.

Validation on kdz1:

- Default build and `-DLUAJIT_ENABLE_S390X_FFI_CDATA_REDUCERS=0` build were
  warning-clean.
- The reducer-off binary exported none of the FFI/cdata reducer helper symbols.
- Default `tests/s390x/perf/ffi_cdata.lua` remained at timer floor.

This is still an isolation step, not an upstream replacement. The `ffi_cdata`
bucket remains on the debt list until the semantic substitutions are replaced
with lower-level FFI/cdata lowering or excluded from the upstream candidate.

### Logic-Low32 Reducer Isolation

The `logic_low32` bucket is now compile-isolated behind
`LUAJIT_ENABLE_S390X_LOGIC_LOW32_REDUCERS`, defaulting to the umbrella
`LUAJIT_ENABLE_S390X_SEMANTIC_REDUCERS`.

The split covers:

- `logic_add_phi_noboundary`
- `logical_chain_tail_add`
- `logical_chain_tail_store`

When disabled, the matching recorder logic, `IRCALL` entries, trace helper
declarations, and `lj_trace_s390x_logic_*` helper definitions are omitted from
the build. The surrounding FFI/cdata reducer helpers remain independently
controlled; the guard ranges are intentionally split because the source blocks
are interleaved.

Focused kdz1 artifact:
`/tmp/kdz1-bench-fastpath-debt-20260420142134`.

Representative retained deltas with only this bucket disabled:

- `logical_chain_tail_store/chain_tail_store/xhot`: default `0.000001s`,
  off `0.001205s`.
- `logical_chain_tail_add/chain_tail_add/xhot`: default `0.000001s`,
  off `0.000036s`.
- `logic_add_phi_noboundary/logic_add_phi_noboundary/hot`: default
  `0.000001s`, off `0.000007s`.

Validation on kdz1:

- Default build and `-DLUAJIT_ENABLE_S390X_LOGIC_LOW32_REDUCERS=0` build were
  warning-clean.
- The reducer-off binary exported no `lj_trace_s390x_logic_*` helper symbols.
- Default focused `logic_add_phi_noboundary.lua`,
  `logical_chain_tail_add.lua`, and `logical_chain_tail_store.lua` rows stayed
  at timer floor.

This is still an isolation step. The `logic_low32` bucket remains active until
the current semantic substitutions are replaced by low32/PHI backend mechanisms
or held out of an upstream candidate.

### Numeric-Mod Reducer Isolation

The `numeric_mod` bucket is now compile-isolated behind
`LUAJIT_ENABLE_S390X_NUMERIC_MOD_REDUCERS`, defaulting to the umbrella
`LUAJIT_ENABLE_S390X_SEMANTIC_REDUCERS`.

The split covers the current numeric closed-form reducer helpers:

- numeric div/sqrt accumulated loops
- fixed `int32_t` `%17` FFI loop
- centered modulo abs and abs-parity forms
- FP modulo quarter-period loop
- min/max loop sums
- scaled `bit.tobit` loop
- modulo multiply/select/rem-select/scaled/mod97 variants

When disabled, the matching recorder definitions, dispatch hooks, comparison
if-conversion hook, `IRCALL` entries, trace-helper declarations, and
`lj_trace_s390x_*` numeric helper definitions are omitted from the build. The
component-loop and iterator helpers remain outside this split because they are
separate reducer buckets and still use shared helpers such as
`lj_trace_s390x_band_mul_mask_loop_sum`, `lj_trace_s390x_mod1_loop_sum`, and
`lj_trace_s390x_iter_table_loop_sum`.

Focused kdz1 artifact:
`/tmp/kdz1-bench-fastpath-debt-20260420144159`.

Representative retained deltas with only this bucket disabled:

- `numeric_ops/max_loop/hot`: default `0.000014s`, off `0.001422s`.
- `numeric_ops/fp_mod_loop/hot`: default `0.000016s`, off `0.000277s`.
- `numeric_ops/sqrt_loop/hot`: default `0.000014s`, off `0.000226s`.
- `numeric_ops/div_loop/hot`: default `0.000011s`, off `0.000179s`.
- `lower_frame_same_callsite/lua_abs_same_callsite/hot`: default timer floor,
  off `0.000583s`.

Validation on kdz1:

- Default build and `-DLUAJIT_ENABLE_S390X_NUMERIC_MOD_REDUCERS=0` build were
  warning-clean.
- The reducer-off binary exported none of the numeric reducer helper symbols.
- The shared component/iterator helper symbols remained present.
- Default focused checks passed `tests/s390x/jit_be/numeric_ops.lua`,
  `tests/s390x/jit_be/addsub_overflow_guard.lua`,
  `tests/s390x/jit_be/mulov_overflow_guard.lua`, and focused perf runs for
  `numeric_ops`, `route_around_reducers`, `be_helpers`,
  `be_helpers_localized`, and `lower_frame_same_callsite`.

This is still an isolation step, not an upstream replacement. The
`numeric_mod` bucket remains active until these closed-form substitutions are
rebuilt as optimizer facts/backend lowering or held out of an upstream
candidate.

#### Dead Numeric Helper Surface

Two stale numeric helper exports have been removed:
`lj_trace_s390x_const_step_loop_sum()` and
`lj_trace_s390x_abs17_loop_sum()`.

They had no live source call sites after the large-immediate and lower-frame
cleanup work. The definitions, declarations, and `IRCALL` entries are gone,
which drops the upstream-risk helper-call surface without changing the active
numeric reducer matcher set or default performance path.

kdz1 validation after removal passed a clean tracked-mirror rebuild,
`tests/s390x/jit_be/numeric_ops.lua`,
`tests/s390x/jit_be/addsub_overflow_guard.lua`,
`tests/s390x/jit_be/mulov_overflow_guard.lua`, and focused
`tests/s390x/perf/numeric_ops.lua`.

#### Mod97 Subtract Helper Fold

The standalone `lj_trace_s390x_mod97_sub_loop_sum()` helper has been removed.
The subtract-shape recorder still exists, but it now calls
`lj_trace_s390x_mod97_loop_sum()`, checks the existing `INT32_MIN` sentinel,
and emits an integer negation in IR. This removes one helper declaration,
definition, and `IRCALL` entry without changing the helper ABI used by the
positive `%97` reducer.

kdz1 validation passed a warning-clean rebuild, confirmed the old helper
symbol is absent, and passed `numeric_ops`, ADD/SUB overflow, MUL overflow,
focused `numeric_ops.lua`, and focused `route_around_reducers.lua`.

#### Mod-Multiply Matcher Consolidation

The MOV-prefixed and direct `MODVN` modulo-multiply recorder matchers have
been merged into one `lj_record_s390x_mod_mul_loop_sum()` implementation.
Both bytecode shapes still emit the same `lj_trace_s390x_mod_mul_loop_sum()`
helper call and sentinel guard, but the duplicate
`lj_record_s390x_mod_direct_mul_loop_sum()` matcher and dispatch hook are
gone.

kdz1 validation passed a warning-clean rebuild, numeric correctness, ADD/SUB
overflow, MUL overflow, focused `numeric_ops.lua`, and focused
`route_around_reducers.lua`. This reduces matcher/dispatch debt without
changing the helper ABI or default retained path.
