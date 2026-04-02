# Filtered Hotside Promotion Plan

Last updated: 2026-04-01 18:57:41 PDT

This note defines the first enable boundary for
`LUAJIT_S390X_HOTSIDE_CANON_SHARE_UGET_LOOPROOT=1`.

## Mechanism

- Named activation family only:
  - repeated `exit=0`
  - `op=BC_UGET`
  - `startop=BC_JMP`
  - loop-like roots (`BC_FORL`, `BC_FUNCF`)
- Proven win mechanism:
  - canon rewrites hot exits back to the earlier equivalent parent
  - share then reuses that earlier seam instead of walking up a later clone ladder
  - exit counts stay high, but trace population collapses to the early canonical seam

## First Enable Set

Promote only workloads already classified as `promotion_core`.

### Reduced UGET/Looproot Siblings

| Workload | kdz baseline -> candidate | zkd0 baseline -> candidate |
| --- | --- | --- |
| `chain_tail_add/hot` | `0.008267 -> 0.003169` | `0.016437 -> 0.004309` |
| `chain_tail_store/hot` | `0.006676 -> 0.002914` | `0.009260 -> 0.004228` |
| `mix_bits/hot` | `0.008267 -> 0.003249` | `0.009459 -> 0.004763` |

### Helper And FFI Families

| Workload | kdz baseline -> candidate | zkd0 baseline -> candidate |
| --- | --- | --- |
| `number_helper_loop/hot` | `0.770458 -> 0.008169` | `2.019211 -> 0.015573` |
| `be_pack_loop/hot` | `0.345916 -> 0.023346` | `0.729473 -> 0.051882` |
| `direct_abs/hot` | `1.206350 -> 0.018044` | `2.540878 -> 0.039104` |
| `stored_abs/hot` | `0.617257 -> 0.012581` | `1.986473 -> 0.041040` |

Promotion rule:
- allow this gate for the full `promotion_core` slice
- require helper-backed host-pair evidence or equivalent already-pinned manual A/B
- reject any attempt to broaden beyond the named seam without new scope proof

## Carry-Forward Only

These workloads hit the same seam but are not part of the first enable set.

| Workload | kdz baseline -> candidate | zkd0 baseline -> candidate | Status |
| --- | --- | --- | --- |
| `retconst_loop/hot` | `0.028523 -> 0.001660` | `0.063160 -> 0.003574` | `promotion_secondary` |
| `retlast_loop/hot` | `0.029461 -> 0.003093` | `0.052306 -> 0.012619` | `promotion_secondary` |
| `mixed_loop/hot` | `0.084383 -> 0.036412` | `0.189387 -> 0.079668` | `promotion_secondary` |
| `sum_loop/hot` | `1.123796 -> 0.675104` | `2.894826 -> 2.985526` | `same_seam_but_dominated` |

Keep these out of the first enable set because:
- `promotion_secondary` still improves through the same mechanism, but it is not the cleanest promotion surface
- `sum_loop` remains dominated by the parked nested-callee vararg frontier and is slightly worse on `zkd0`

## Explicitly Out Of Scope

Do not treat these as evidence for this gate:
- `iterator_table`
- `dispatch_trace`
- `mixed_ffi`
- `ffi_cdata`
- `int_add_phi_only`
- `logic_add_phi_noboundary`

Reason:
- either they do not hit the filtered seam
- or they remain dominated by a different mechanism
- or they regress under the broader/global hotside policy

## Next Promotion Work

- keep the filtered gate as the active scoped throughput candidate
- use `promotion_core` as the first promotion surface
- keep `promotion_secondary` as carry-forward evidence only
- do not reopen iterator, dispatch, low32-home, or generic global canon/share from this queue

## Rollout Criteria

Do not treat this gate as a safe broader default yet.

Required before any broader promotion:
- helper-backed host-pair A/B remains positive across the whole `promotion_core` slice
- the filtered hit shape stays on the named seam only:
  - `exit=0`
  - `op=BC_UGET`
  - `startop=BC_JMP`
  - loop-like roots
- frozen iterator stays out of scope and does not reactivate under this gate
- `promotion_secondary` does not get silently upgraded into the first enable set
- no fallback to the older global canon/share gate

Immediate rollout shape:
- keep `LUAJIT_S390X_HOTSIDE_CANON_SHARE_UGET_LOOPROOT=1` as an explicit candidate surface
- validate and restamp only against `promotion_core`
- carry the secondary slice as supporting same-seam evidence, not as an enable criterion
