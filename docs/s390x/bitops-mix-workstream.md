# s390x Bitops Mix Workstream

This note preserves the useful integration boundary from the earlier
`bitops_mix` workstream without keeping the private worktree and host-specific
validation trail.

## Scope

The workstream was narrowly about `tests/s390x/perf/bitops_mix.lua`,
especially `mix_bits`.

The technical goal was to reduce the remaining s390x gap on that family by
tightening low32 handling and shortening the backend path for the exact
loop-carried bitop shapes used there.

## Retained Technical Content

The useful changes from that workstream were:

- keep loop-carried bitop values in low32 form when the use graph stays inside
  the bitwise integer domain,
- tighten immediate bitop and shift lowering for s390x low32 semantics,
- fuse selected bitop expression shapes into shorter backend sequences,
- preserve exact positive-`FORI` metadata for guarded `1..200` style range
  loops,
- use an exact suffix-table update only when the full loop shape and guarded
  range proof match,
- strengthen duplicated pre-loop open-upvalue range guards only when all
  matching guards are before the first post-entry snapshot and the folded
  region has no side effect.

## Rejected Follow-Up

The tempting next step was to remove `bit.*` table guards by reasoning that the
table contents were effectively constant. That remains rejected.

The reason is still straightforward:

- `bit` is mutable Lua state,
- table contents and shape can change through interpreter, C API, and JIT
  store paths,
- a backend-local optimization cannot safely infer global table immutability.

Any future attempt to elide those guards needs a proper table-version or
watched-table invalidation mechanism. Until then, keep the semantic guards.

## Validation Lanes

The focused validation set for this workstream remains:

- `tests/s390x/jit_core/bitops_trace.lua`
- `tests/s390x/jit_core/bitops_mix_suffix.lua`
- `tests/s390x/jit_be/low32_home_contract.lua`
- `tests/s390x/jit_be/string_key_href.lua`
- `tests/s390x/jit_be/numeric_ops.lua`
- `tests/s390x/perf/bitops_mix.lua`

Run both gcc and clang native builds when restamping this area.

## Integration Guidance

If this workstream is rebased or reintroduced:

1. Start from the current bring-up branch state.
2. Preserve newer low32 and emit-helper work outside the exact bitops changes.
3. Re-apply only the bitops-specific suffix-table and guard-strengthening
   logic that still survives code review.
4. Revalidate the focused correctness lanes and then the `bitops_mix` perf
   family.
5. Check that the retained trace still uses the exact suffix-table shape it was
   designed around.

Relevant source areas:

- [`src/lj_emit_s390x.h`](../../src/lj_emit_s390x.h)
- [`src/lj_asm_s390x.h`](../../src/lj_asm_s390x.h)
- [`src/lj_record.c`](../../src/lj_record.c)
- [`src/lj_ir.h`](../../src/lj_ir.h)
