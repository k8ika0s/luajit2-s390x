# Promotion Core ISA Audit

Classic s390x constraint first: blind 32-bit opcode swaps remain rejected.
Classic 32-bit instructions operate on the low 32 bits and leave the upper 32
bits unchanged, so any experiment that mixes them into a 64-bit lane without an
explicit normalization step is off-contract.

Current exact-guard answer:

- the first literal taken guard on the live `promotion_core` seam is still the
  inherited visible current-value lane
- on both `kdz` and `zkd0`, both `number_helper_loop` and `be_pack_loop` now
  pin:
  - `trace 7 exit 0`
  - `guardmark=curins 3`
  - `IR=SLOAD`
  - `op1=4`
  - `op2=36`
  - `sload_int ofs=16 extra=20`

ISA/ABI memo:

- If the exact first taken guard were compare-like:
  - the only plausible ISA-level experiment would be a narrow consumer-side
    fused compare-and-branch lowering for that exact family
  - the current backend still lowers through split compare plus branch
    sequences; there is no existing `CRJ`/`CGRJ`/`CIJ`/`CGIJ` path to widen
    blindly
- Because the exact first taken guard is still inherited integer `SLOAD`:
  - preserved-register carry is not the right lever
  - the active question is replay/slot identity and recorder contract, not
    helper ABI carry across nonvolatile GPRs
- Predictor hints such as `BPP`/`BPRP` remain out of scope:
  - there is no stable branch-table-like site yet
  - the active seam is still a replay/header guard, not a settled consumer
    branch family

Recommendation:

- do not reopen preserved-register or predictor-hint work on this seam
- the only ISA-side lever worth reconsidering later is a fused
  compare-and-branch experiment, and only if a later cycle moves the first
  literal taken guard from inherited `SLOAD` to one stable compare consumer
