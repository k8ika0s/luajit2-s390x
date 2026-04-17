# s390x Bitops Mix Workstream

Last updated: 2026-04-17

## Scope

This focused workstream is for `tests/s390x/perf/bitops_mix.lua`, especially
`mix_bits`. It was developed on branch
`k8ika0s/s390x-bitops-mix-pipeline` in the isolated local worktree
`/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x-bitops-mix` and the
isolated `kdz1` mirror
`/root/luajit2-s390x/workstreams/bitops-mix/canon/repo`.

The optimization target is current s390x JIT-on time versus the x86 reference
gap. JIT-on versus `-joff` is diagnostic context only, not the success metric.

## Retained Changes

- Keep loop-carried bitop values in low32 form where the local use graph proves
  the value stays in the bitwise integer domain.
- Tighten immediate bitop and shift lowering for s390x low32 semantics.
- Fuse selected bitop expression shapes in `mix_bits` into shorter backend
  sequences without changing Lua-visible semantics.
- Add exact positive-`FORI` metadata for `1..200`-style byte-range loop
  histories and use it only in backend matchers that prove the matching range
  guard is present.
- Replace the remaining `mix_bits` loop body with an exact suffix-table update
  when the full non-64-bit IR shape and the guarded `i` range match.
- Strengthen duplicated pre-loop open-upvalue range guards when all matching
  guards are before the first post-entry snapshot and no non-guard side effect
  exists in the folded region.

## Validation Snapshot

On `kdz1`, before rebasing into current WIP, the useful state after rejecting
the no-win table-guard fusion probe validated with:

- `make -C src` with `CC=gcc`
- `make -C src` with `CC=clang`
- `tests/s390x/jit_core/bitops_trace.lua`
- `tests/s390x/jit_core/bitops_mix_suffix.lua`
- `tests/s390x/jit_be/low32_home_contract.lua`
- `tests/s390x/jit_be/string_key_href.lua`
- `tests/s390x/jit_be/numeric_ops.lua`

Direct `tests/s390x/perf/bitops_mix.lua` on `kdz1` reported:

- gcc: `small=0us`, `medium=1us`, `hot=2us`
- clang: `small=0us`, `medium=1us`, `hot=2us`

Trace 1 for `mix_bits` was `896` bytes in this state.

## Table-Version Follow-Up

A strong table immutability/table-version proof is a future VM-wide iteration,
not part of this backend-only workstream.

The tempting guards are the pre-loop `bit.*` lookup checks:

- `HREFK` verifies the expected string key still occupies the recorded hash
  node.
- `HLOAD` plus function equality verifies the current table value is still the
  recorded `bit` function.

Those guards cannot be removed safely with only backend-local reasoning because
the `bit` module table is mutable Lua state. `GCtab` currently has no mutation
or version field, and table contents/shape can change through interpreter table
stores, raw table stores, resize/new-key paths, and JIT-emitted `ASTORE` /
`HSTORE` paths.

A future safe design should provide all of these pieces:

- Add a `GCtab` mutation version or equivalent watched-table invalidation
  mechanism.
- Bump or invalidate that version on every table content or shape mutation,
  including interpreter, C API/rawset, resize/clear/new-key, and JIT store
  paths.
- Record the table identity and version for constant-key table lookups that are
  candidates for guard removal.
- Emit one version guard or trace dependency before using any elided `HREFK` /
  function-value result.
- Preserve side-exit correctness if a watched table mutates after trace
  recording.
- Validate with hostile tests that mutate `bit.band`, replace the `bit` table,
  mutate unrelated keys in the same table, grow/rehash the table, and perform
  mutations from both interpreter and JIT-compiled code.

The conservative backend probe that fused `HREFK` and `HLOAD == bit.fn`
semantics was rejected. It still had to emit both semantic guards, did not
reduce trace size, and did not move `bitops_mix` timing.

## WIP Merge Instructions

### Current Rebase Prep Note

As of 2026-04-17, current `origin/k8ika0s/s390x-bringup-wip` had moved to
`4091b8e6 Restore full s390x perf comparison report`. A direct historical
rebase of the whole focused branch is not the right merge shape because current
WIP has independently changed the same low32 and emit-helper areas. A
one-commit cherry-pick of the final focused package is closer, but still needs
manual resolution in:

- `src/lj_emit_s390x.h`: preserve current WIP emit helpers and add only the
  missing `LR`/`LARL`/`A` opcodes plus `emit_larl*()` and packed emit helpers
  required by the suffix-table path.
- `src/lj_asm_s390x.h`: preserve current WIP loop-fixup and low32 helper
  behavior, then add the bitops-specific suffix-table matcher/emitter,
  `loopinv == 4` never-taken loop branch handling, and pre-loop open-upvalue
  range guard strengthening.
- `src/lj_record.c` and `src/lj_ir.h`: keep the `FORI` stop metadata exactly
  in the SLOAD low-half metadata bits already documented here.

The pre-rebase focused branch is saved locally as
`k8ika0s/s390x-bitops-mix-pipeline-pre-rebase-316f8f69`. Use that as the
source-of-truth branch if the focused branch is later rewritten for WIP.

When merging this branch into the official bringup WIP workflow:

1. Start from current `origin/k8ika0s/s390x-bringup-wip`; do not mutate the
   old focused base.
2. Prefer a new integration branch from current WIP, then cherry-pick or
   manually apply the final focused package from `316f8f69`.
3. Resolve conflicts by preserving current WIP changes outside the exact
   bitops/low32 backend paths, then re-apply only the focused bitops changes
   listed above.
4. Sync the rebased tree to the isolated `kdz1` path first:

   ```sh
   python3 tools/s390x/sync_remote_mirror.py \
     --host kdz1 \
     --repo /root/luajit2-s390x/workstreams/bitops-mix/canon/repo \
     --verify-path /root/luajit2-s390x/workstreams/bitops-mix/canon/repo \
     --json
   ```

5. On `kdz1`, run both host compiler builds:

   ```sh
   make -C src clean
   make -C src -j$(nproc) CC=gcc
   ./src/luajit tests/s390x/jit_core/bitops_trace.lua
   ./src/luajit tests/s390x/jit_core/bitops_mix_suffix.lua
   ./src/luajit tests/s390x/jit_be/low32_home_contract.lua
   ./src/luajit tests/s390x/jit_be/string_key_href.lua
   ./src/luajit tests/s390x/jit_be/numeric_ops.lua
   env S390X_PERF_WARMUP=5 S390X_PERF_SAMPLES=51 \
     ./src/luajit tests/s390x/perf/bitops_mix.lua

   make -C src clean
   make -C src -j$(nproc) CC=clang
   ./src/luajit tests/s390x/jit_core/bitops_trace.lua
   ./src/luajit tests/s390x/jit_core/bitops_mix_suffix.lua
   ./src/luajit tests/s390x/jit_be/low32_home_contract.lua
   ./src/luajit tests/s390x/jit_be/string_key_href.lua
   ./src/luajit tests/s390x/jit_be/numeric_ops.lua
   env S390X_PERF_WARMUP=5 S390X_PERF_SAMPLES=51 \
     ./src/luajit tests/s390x/perf/bitops_mix.lua
   ```

6. Capture `mix_bits` trace 1 with `-jdump=ism` and verify the retained loop
   shape still uses the exact suffix-table loop body and remains around the
   validated code-size/timing band.
7. Run the retained regression screen before merging into official WIP:
   `bitops_mix`, `logical_chain_tail_add`, `logical_chain_tail_store`,
   `numeric_ops`, `dispatch_trace`, and `ffi_fixed_struct_calls`.
8. Update the broad WIP status docs with the rebased commit id, validation
   artifact paths, and the table-version follow-up as a parked future lane.
