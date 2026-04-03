# 2026-04-02 kdz static-stop local broad canon/share

Host: `kdz`

Scope:

- reduced repeated-call localized static-stop subgroup
- explicit broad opt-in only:
  - `LUAJIT_S390X_HOTSIDE_CANON_SHARE_EQUIV=1`
- current shipping filtered default remains unchanged

What changed:

- `trace_hotside()` now allows explicit broad canon/share envs to reach the
  equivalence matcher even when the shipping `UGET`/looproot prefilter does not
  match.
- this is a bounded opt-in boundary fix, not a widening of the envless default.

Key mechanism proof:

- before the boundary fix, the localized static-stop lane never emitted
  `S390X_HOTSIDE_EQUIV` logs under either the shipping default or broad envs.
- after the boundary fix, the same lane reaches the matcher and accepts real
  equivalent parents:
  - `parent=6 exit=0 root=1 cand=5`
  - `parent=7 exit=0 root=1 cand=6`
- rejected earlier candidates are now explained as ordering, not shape drift:
  - repeated `phase=reject-order`

Repeated-call quant on `number_helper_literal_stop_real_local_tobit`:

- default:
  - `RUN 1 -149783296 0.027580 103`
  - `RUN 2 -149783296 0.030517 103`
- broad opt-in:
  - `RUN 1 -149783296 0.007981 7`
  - `RUN 2 -149783296 0.007844 7`

Repeated-call quant on the localized static-stop `be_pack` sibling:

- default:
  - `RUN 1 2048032000 0.016238 7`
  - `RUN 2 2048032000 0.016659 7`
- broad opt-in:
  - `RUN 1 2048032000 0.015817 7`
  - `RUN 2 2048032000 0.015774 7`

Conclusion:

- the new boundary fix opens a real opt-in remediation lane for the localized
  static-stop `number_helper` subgroup.
- it is not a general subgroup win:
  - `number_helper` drops from a clone-ladder `103`-trace shape to `7` traces
    and from roughly `0.028-0.031s` to `0.0078s`
  - `be_pack` is already near the smaller trace set and barely moves
- next honest target:
  - quantify whether this opt-in subgroup can beat `-joff`
  - and decide whether a selective non-`UGET` canon/share policy is worth
    designing, rather than widening the shipping default blindly
