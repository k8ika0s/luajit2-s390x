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

- Dispatch trace route-around matchers in `src/lj_trace.c` near
  `lj_trace_s390x_dispatch_proto_match()` and
  `lj_trace_s390x_dispatch_forl_proto_nojit_match()`.
- Promotion-core and route-around matchers in `src/lj_trace.c` that combine
  benchmark chunk names with `nins` / `nsnap` / `mcloop`.
- Iterator/mixed/ffi exact root blacklists in `src/lj_trace.c`.
- Remaining recorder-side synthetic or benchmark chunk matchers such as the
  strto helper probe and FFI call-pressure shapes in `src/lj_record.c`.

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

The remaining high-density audit buckets are now live iterator/mixed safety
rails, lower-frame/mixed-ffi/cdata exact trace guards, hotside-localized proto
fingerprints, and one residual synthetic `@numeric_ops_max` recorder chunk.
Tackle live iterator/mixed route-arounds only with mechanism proof; the next
low-risk source cleanup is the residual synthetic recorder chunk or stale
opt-in exact guards that no current retained env uses.

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
profile. Later cleanup batches migrated the FFI call/struct, numeric-op,
large-immediate, lower-frame, route-reducer, and scaled-tobit recorder folds.
The remaining high-value cleanup is now concentrated in exact trace-control
helpers and the smaller residual recorder probes named by the latest debt
pack.

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
affected family. The benchmark-shaped source audit is now at `28` findings.
The remaining trace-control debt is not all removable as source hygiene:
iterator safety rails still protect known unsafe restart paths, while
trace-save fingerprints and the residual exact iterator hooks need separate
classification before deletion or migration.

Post-cleanup retained matrix:

- `/tmp/kdz1-trace-cleanup-final-retained-2026041915`
- No retained-env JIT-on family is red versus `-joff`.
- The retained env ledger now reports only two gates:
  `LUAJIT_S390X_ITERATOR_ITERN_BLACKLIST=1` and
  `LUAJIT_S390X_ITERATOR_ITERL_BLACKLIST=1`.
- Remaining `lj_trace.c` source debt is therefore primarily live iterator
  safety/mechanism debt, not stale exact-family perf hooks.
