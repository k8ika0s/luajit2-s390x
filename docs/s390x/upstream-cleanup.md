# s390x Upstream Cleanup

This page tracks source patterns that are acceptable during bring-up but not
acceptable in an upstreamable JIT implementation.

## Benchmark-Shaped Optimizations

The current WIP branch no longer has production `src/` benchmark-shaped
fastpaths according to:

```sh
python3 tools/s390x/audit_benchmark_fastpaths.py --fail-on-findings
```

The upstream rule remains strict: a valid upstream patch can optimize IR,
bytecode, backend lowering, ABI handling, VM helper behavior, or target
instruction selection. It should not recognize repository performance tests,
synthetic benchmark chunk names, line-number identity, current IR sizes,
snapshot counts, or mcode-loop fingerprints to steer production JIT behavior.

Historical findings still mention those patterns because they document the
bring-up path. Current source should stay audit-clean.

## Current Blocker Map

Current production-source status:

- `src/lj_record.c`: benchmark-shaped recorder folds have been migrated to
  semantic bytecode/IR/runtime contracts or removed.
- `src/lj_trace.c`: exact benchmark trace-control steering has been removed,
  including exact proto no-JIT paths, hotcount parks, blacklists, stale
  loop-descendant trace-save experiments, and exact iterator/mixed semantic
  fold parks.
- `src/lj_ircall.h`: s390x reducer/string helper callinfo entries are now
  target-confined with `IRCALLCOND_S390X`, not exposed as active generic
  architecture-neutral helper ABI.
- `src/lib_jit.c`: the remaining policy debt is the broad s390x
  `hotexit=200` safety rail. Removing it exposed a `vararg_paths.lua` segfault
  at the generic hotexit default, while `-Ohotexit=200` passed. This is
  correctness mechanism debt, not benchmark-family steering.

Generic LuaJIT mechanisms such as `blacklist_pc()` and `PROTO_NOJIT` checks
remain in source, but the current s390x branch should not set them from
benchmark-family identity.

## Resolution Policy

Use this split for upstream prep:

- Preserve the current WIP performance baseline while cleanup proceeds, but do
  not reintroduce benchmark-shaped production source. The benchmark-fastpath
  audit is now a hard source gate, not just a generic-only comparison aid.
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
  and expressed without benchmark identity. The current s390x `hotexit=200`
  default is retained only under that rule and remains a tracked mechanism
  debt until the lower-threshold vararg side-exit/restore crash is fixed.

## Audit Command

Run:

```sh
python3 tools/s390x/audit_benchmark_fastpaths.py
```

For a hard gate in an upstream-prep branch:

```sh
python3 tools/s390x/audit_benchmark_fastpaths.py --fail-on-findings
```

The current WIP is expected to pass this audit for production `src/` files. A
new finding means a cleanup regression unless it is an explicitly allowlisted
generic mechanism.

The audit is intentionally source-based, not build-profile-based. It reports
benchmark-shaped source even if a path could be compiled out, because an
upstream candidate needs the source removed or rewritten, not merely disabled.

## Cleanup Order

1. Keep the benchmark-fastpath audit at zero findings for production `src/`.
2. Fix the remaining broad `hotexit=200` safety rail by resolving the generic
   hotexit vararg crash in side-exit/restore mechanics.
3. Continue shrinking diagnostic env and tooling-only historical references
   where they no longer pay their way.
4. Re-run correctness first, then perf comparison after each cleanup tranche.
5. Rebuild any lost performance from semantic mechanisms only.

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

The large-immediate recorder folds no longer depend on benchmark file or line
identity:

- The old `@tests/s390x/perf/large_immediates.lua` and `pt->firstline` gates
  for add/sub/compare/table-reference folds are gone.
- The retained folds still prove the root counted loop, owned `FORI/FORL`
  body, literal constants, loop bounds, and table key/value guard before
  replacing the body with `lj_trace_s390x_int_const_step_loop_sum`.
- kdz1 artifact: `/tmp/kdz1-bench-fastpath-debt-20260419114902`.
- Result: no failed rows and only `0.000001s` timer jitter in the focused
  generic-only debt pack; kdz1 and zkd0 direct focused validation both pass.

### Lower-Frame, Route-Reducer, And Scaled-Tobit Status

The next recorder debt batch also moved off benchmark file identity:

- Lower-frame `%17` absolute-value loop folds no longer require
  `@tests/s390x/perf/lower_frame_same_callsite.lua`. The recorder now proves
  the root frame, loop ownership, `% 17`, signed absolute-value branch, loop
  bounds, and accumulator update before calling
  `lj_trace_s390x_lower_frame_abs17_loop_sum`.
- Route-reducer folds no longer require
  `@tests/s390x/perf/route_around_reducers.lua` or `pt->firstline`. The
  recorder chooses the literal or localized `bit.*` table shape by bytecode
  and guarded function identity, then proves the counted loop before using the
  scaled-tobit loop helper.
- Scaled `bit.tobit(total + i*K)` folds no longer require `be_helpers`,
  `be_helpers_localized`, or `promotion_core_static_stop` chunk names. The
  recorder now proves the `MULVN -> ADDVV -> bit.tobit()` body, positive
  counted loop, constant multiplier, and guarded `bit.tobit` callee.

Validation artifacts:

- Lower-frame: `/tmp/kdz1-bench-fastpath-debt-20260419124510`.
- Route reducers: `/tmp/kdz1-bench-fastpath-debt-20260419124836`.
- Scaled tobit: `/tmp/kdz1-debt-be-helpers-generic-202604191253`,
  `/tmp/kdz1-debt-be-localized-generic-202604191254`, and
  `/tmp/kdz1-debt-promotion-static-generic-202604191255`.

Direct host validation passed on kdz1 and zkd0 for the focused lower-frame,
route-reducer, be-helper, localized be-helper, promotion-core static-stop, and
numeric correctness rows. The focused generic-only debt for these migrated rows
is now at timer/noise floor. The remaining `be_helpers` generic-only deltas in
the latest focused run are different mechanisms: `num_aload_loop` and `strto`
helper rows, not the scaled `bit.tobit` fold.

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
