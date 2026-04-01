# s390x Low32-Home / Normalized-Result Contract

## Purpose

This note defines a backend-wide invariant for the current `bitops_mix` family.
It is stronger than "skip `asm_bnorm32()` here" because it names the semantic
state that may be carried and the exact points where that state must be
normalized.

The current evidence says the live compiled-body seam is not a store-tail
problem and not a compare-consumer problem. The add-tail family carries an
integer result through bitop logic, unary, shift, rotate, plain non-guard
integer `ADD`, and loop `PHI`. Anything outside that family must force
normalization.

## States

Use two semantic states:

- `W32_HOME`
- `W64_NORM`

`W32_HOME` means the semantic value is a 32-bit integer result whose low word
is authoritative. The upper 32 bits are unspecified and must not be observed by
generic consumers.

`W64_NORM` means the value is in canonical signed 64-bit form and is safe for
any consumer that expects the ordinary LuaJIT integer representation.

These are semantic states, not physical register classes. The backend may still
allocate ordinary GPRs; the contract is about what the value means at each IR
boundary.

## Contract

The contract is:

- values may stay in `W32_HOME` across the safe internal family
- the backend must normalize to `W64_NORM` before any forced boundary
- the backend may re-enter `W32_HOME` only from a fresh safe producer

In practice this is a true word-home discipline layered on top of ordinary
low-word GPR state. It is not a new register class. The implementation should
track the home state in backend lowering and register-home decisions, while
still using the normal s390x GPR allocator.

## Safe Internal Family

Based on the current clean `kdz` evidence, the safe internal family is:

- bitop logic
- bitop unary
- bitop shift
- bitop rotate
- plain non-guard integer `ADD`
- loop `PHI` when all incoming arms stay inside this same family

This family is the only place where `W32_HOME` may flow without forced
normalization.

## Forced-Normalization Boundaries

Normalize to `W64_NORM` before any of the following:

- guard or compare sites
- helper or call argument setup
- store consumers such as `ASTORE`
- snapshot-visible exits or restores
- any consumer outside the safe family above

The compare frontier is explicitly a hard boundary. The current add-tail compare
read showed that the hot compare sites are loop-control compares, not the
carried bitop value seam. Do not treat compare as part of the safe internal
family.

## Placement

The contract should live in both places:

- ordinary low-word GPR state, as the physical home where the value resides
- a true word-home semantic discipline, as the backend rule that decides when
  the low word may be carried forward and when it must be normalized

The physical GPR allocation remains ordinary. The contract is the semantic layer
that says when the backend may treat a value as `W32_HOME` and when it must
materialize `W64_NORM`.

## Minimal Prototype Targets

The first prototype should be narrow:

1. `logical_chain_tail_add`
   - carry `W32_HOME` across the bitop chain and plain non-guard `ADD`
   - preserve the loop `PHI` state

2. `logical_chain_tail_store`
   - force normalization before `ASTORE`
   - keep the store path a hard boundary

3. `bitops_mix`
   - apply the same contract to the hot add-tail body
   - do not touch the loop-control compare seam

The prototype should prove that a carried value can remain `W32_HOME` through
the safe family and still normalize correctly before the first hard boundary.

## Emitter / ABI Feasibility Snapshot

Current implementation reality splits into three buckets:

### Already available and usable

- current 64-bit logical and arithmetic lowering already used by the backend
- current 64-bit compare lowering already used at hard boundaries
- ordinary GPR allocation and existing loop `PHI` lowering

### Available in the ISA but not yet a safe contract surface here

- word-based logical and arithmetic instruction forms
- high-word / word-home instruction forms
- register-storage boundary forms that may be useful once the contract is
  explicit

These are architecture opportunities, not immediate drop-in swaps. The earlier
opcode-swap and local skip experiments already showed that using narrower forms
without a stronger state model is not promotable.

### Missing or not yet wired in the emitter/backend contract

- a backend-wide notion that a value is still `W32_HOME`
- emitter support and lowering rules that preserve that state across the safe
  family
- explicit normalization hooks at every hard boundary

For the current queue, helper and call interaction are hard boundaries rather
than the main optimization surface. `bitops_mix` is compiled-body dominated, so
preserved-GPR strategy is secondary here; the primary problem is the backend
result-state contract, not helper ABI traffic.

## Non-Goals

Do not use this note to reopen:

- iterator owner selection
- dispatch loop-clone reuse
- compare-consumer rewrites on the current control-add seam
- store-tail as the primary `bitops_mix` seam
- local opcode swaps that only delete `asm_bnorm32()` without a state model

If the contract cannot stay finite through reduced trace formation, it should be
rejected before any broad performance work.
