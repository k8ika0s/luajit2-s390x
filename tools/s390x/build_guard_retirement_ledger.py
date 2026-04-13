#!/usr/bin/env python3
"""Build a retirement ledger for retained s390x guard envs.

This is intentionally a read-only classifier. It does not run opt-out perf
experiments and it does not decide that a guard is removable. The goal is to
turn the retained env contract into an explicit work queue: bake-in cleanup,
mechanism work, or re-attribution.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import pathlib
import re
import sys
from dataclasses import dataclass, field
from typing import Any

THIS_DIR = pathlib.Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

import restamp_iterator_perf as restamp


ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT_ROOT = pathlib.Path("/tmp")
SOURCE_GLOBS = ("src/*.c", "src/*.h", "src/*.dasc")


@dataclass(frozen=True)
class GateMeta:
    group: str
    kind: str
    classification: str
    owner: str
    protected_rows: tuple[str, ...] = ()
    opt_out: tuple[str, ...] = ()
    evidence: str = ""
    next_step: str = ""
    notes: tuple[str, ...] = ()


META: dict[str, GateMeta] = {
    "LUAJIT_S390X_DISPATCH_FORL_SKIP_JFORI": GateMeta(
        group="dispatch",
        kind="default-on opt-out",
        classification="bake-in/env-cleanup candidate",
        owner="trace/dispatch",
        protected_rows=("dispatch_trace/numeric_loop",),
        opt_out=("LUAJIT_S390X_DISABLE_DISPATCH_FORL_SKIP_JFORI",),
        evidence="Dispatch opt-out is a retained causality guard; disabling reproduces the known numeric_loop failure.",
        next_step="Keep source default-on; later remove from RETAINED_BASELINE_ENV only after matrix tooling no longer needs the explicit env marker.",
    ),
    "LUAJIT_S390X_DISPATCH_FORL_PARK_ROOT_HOTEXIT_EXACT_COOLDOWN": GateMeta(
        group="dispatch",
        kind="valued opt-in cooldown",
        classification="mechanism debt",
        owner="trace/dispatch",
        protected_rows=("dispatch_trace/hotexit_loop",),
        evidence="Exact cooldown is retained for the FORL/JFORI dispatch seam.",
        next_step="Do not remove directly; re-attribute dispatch only if a current full-env rerank names it materially red.",
    ),
    "LUAJIT_S390X_AREF_BASE_ALLGPR": GateMeta(
        group="backend",
        kind="opt-in backend lowering",
        classification="bake-in candidate",
        owner="backend/AREF",
        protected_rows=("mixed_noffi", "iterator_table"),
        evidence="Retained backend lowering win; current matrix does not show a correctness guard reason to keep this as env-only.",
        next_step="Run one disable-env A/B for guardrails, then make unconditional if exactness and matrix stay clean.",
    ),
    "LUAJIT_S390X_IPAIRS_EXIT1_SKIP_BODY": GateMeta(
        group="iterator/mixed",
        kind="opt-in snapshot route-around",
        classification="mechanism debt",
        owner="snapshot/iterator",
        protected_rows=("mixed_noffi",),
        evidence="Retained mixed/ipairs exactness guard.",
        next_step="Keep until a mixed/ipairs truth pack proves the protected body can compile without wrong-result drift.",
    ),
    "LUAJIT_S390X_ROOT1_ITERL_REPLAY_TRIPLET": GateMeta(
        group="iterator/mixed",
        kind="opt-in replay route-around",
        classification="mechanism debt",
        owner="snapshot/recorder",
        protected_rows=("mixed_noffi",),
        evidence="Paired with ROOT1_ITERL_REPLAY_TRIPLET_LINK_PARENT in the retained mixed floor.",
        next_step="Retire only as a pair after mixed_noffi exactness and row perf pass with both disabled.",
    ),
    "LUAJIT_S390X_ROOT1_ITERL_REPLAY_TRIPLET_LINK_PARENT": GateMeta(
        group="iterator/mixed",
        kind="opt-in replay route-around",
        classification="mechanism debt",
        owner="recorder",
        protected_rows=("mixed_noffi",),
        evidence="Depends on ROOT1_ITERL_REPLAY_TRIPLET.",
        next_step="Retire only as a pair after mixed_noffi exactness and row perf pass with both disabled.",
    ),
    "LUAJIT_S390X_SUM_LOOP_SELECT_EXIT0_DONE": GateMeta(
        group="vararg",
        kind="opt-in recorder route-around",
        classification="mechanism debt",
        owner="recorder/select",
        protected_rows=("vararg_paths/sum_loop",),
        evidence="Retained exact stopper for select/sum_loop.",
        next_step="Needs a real select/vararg root fix before removal.",
    ),
    "LUAJIT_S390X_SUM_LOOP_SELECT_SKIP_FUNC_EQ": GateMeta(
        group="vararg",
        kind="opt-in recorder route-around",
        classification="mechanism debt",
        owner="recorder/select",
        protected_rows=("vararg_paths/sum_loop",),
        evidence="Retained select_detect guard cut.",
        next_step="Needs current sum_loop attribution before removal.",
    ),
    "LUAJIT_S390X_SUM_LOOP_SELECT_CONST_GGET": GateMeta(
        group="vararg",
        kind="opt-in recorder fold",
        classification="bake-in candidate",
        owner="recorder/select",
        protected_rows=("vararg_paths/sum_loop",),
        evidence="Exact constant-fold of BC_GGET select on the carried sum_loop row.",
        next_step="Candidate for unconditional exact matcher if retconst/retlast and compiled_vararg guardrails stay clean.",
    ),
    "LUAJIT_S390X_SUM_LOOP_FORL_BLACKLIST": GateMeta(
        group="vararg",
        kind="opt-in root blacklist",
        classification="still unsafe",
        owner="trace/vararg",
        protected_rows=("vararg_paths/sum_loop",),
        evidence="Vararg-root FORL tracing has had correctness failures; guardrail promotion kept vararg paths runnable.",
        next_step="Do not remove; replace with a vararg-root recorder/VM fix first.",
    ),
    "LUAJIT_S390X_VARARG_SIBLING_FORL_BLACKLIST": GateMeta(
        group="vararg",
        kind="opt-in sibling blacklist",
        classification="still unsafe",
        owner="trace/vararg",
        protected_rows=("vararg_paths/retlast_loop", "vararg_paths/retconst_loop"),
        evidence="Retained sibling blacklist was restamped after promotion drift.",
        next_step="Keep until a fresh vararg truth pack proves sibling FORL roots are safe.",
    ),
    "LUAJIT_S390X_MIXED_FFI_POST_STITCH_SAVE_DONE": GateMeta(
        group="mixed_ffi",
        kind="opt-in save DONE",
        classification="mechanism debt",
        owner="trace/mixed_ffi",
        protected_rows=("mixed_ffi/mixed_ffi_loop",),
        evidence="Route-around for exact stitched mixed_ffi side family.",
        next_step="Retire only after mixed_ffi truth pack names and fixes the underlying stitched save/retry payer.",
    ),
    "LUAJIT_S390X_MIXED_FFI_FORL_PROTO_NOJIT": GateMeta(
        group="mixed_ffi",
        kind="opt-in proto-NOJIT",
        classification="still unsafe",
        owner="trace/mixed_ffi",
        protected_rows=("mixed_ffi/mixed_ffi_loop",),
        evidence="Retained exact mixed_ffi root FORL park.",
        next_step="Needs a real mixed_ffi root trace fix before removal.",
    ),
    "LUAJIT_S390X_FFI_CDATA_PAIR_SAVE_DONE": GateMeta(
        group="ffi_cdata",
        kind="opt-in save DONE",
        classification="mechanism debt",
        owner="trace/ffi_cdata",
        protected_rows=("ffi_cdata/pair_loop",),
        evidence="Retained exact pair_loop save route-around; the older FORL blacklist is already obsolete/removed from retained env.",
        next_step="Recheck with current fast cdata backend before retiring; likely a focused bake-in/narrowing candidate if exactness holds.",
    ),
    "LUAJIT_S390X_ITERATOR_ITERN_BLACKLIST": GateMeta(
        group="iterator",
        kind="opt-in broad blacklist",
        classification="still unsafe",
        owner="trace/iterator",
        protected_rows=("mixed_noffi", "jit_loops/pairs_loop.lua"),
        evidence="Broad iterator root opt-out timed out pairs_loop.lua and regressed mixed_noffi.",
        next_step="Do not remove; replace the unsafe ITERN/JLOOP/TGETV or lj_vm_next handoff mechanism first.",
    ),
    "LUAJIT_S390X_ITERATOR_ITERL_BLACKLIST": GateMeta(
        group="iterator",
        kind="opt-in broad blacklist",
        classification="still unsafe",
        owner="trace/iterator",
        protected_rows=("mixed_noffi", "iterator_table"),
        evidence="Part of retained broad iterator safety fallback.",
        next_step="Do not remove without a current iterator/mixed proof that broad fallback is unnecessary.",
    ),
    "LUAJIT_S390X_ITERATOR_ITERN_PROTO_NOJIT": GateMeta(
        group="iterator",
        kind="default-on exact proto-NOJIT",
        classification="mechanism debt",
        owner="trace/iterator",
        protected_rows=("iterator_table/pairs_sum", "iterator_table/pairs_array_sum"),
        opt_out=("LUAJIT_S390X_DISABLE_ITERATOR_ITERN_PROTO_NOJIT",),
        evidence="Exact iterator_table escape hatch is default-on and currently keeps official iterator rows in band.",
        next_step="Replace by fixing the official iterator lj_vm_next/root-body seam; do not remove before pairs_loop/mixed_noffi stay safe.",
    ),
    "LUAJIT_S390X_ITERATOR_ARRAY_ITERN_NOJIT_HOTCOUNT_PARK": GateMeta(
        group="iterator",
        kind="default-on exact hotcount park",
        classification="mechanism debt",
        owner="trace/iterator",
        protected_rows=("iterator_table/pairs_array_sum",),
        opt_out=("LUAJIT_S390X_DISABLE_ITERATOR_ARRAY_ITERN_NOJIT_HOTCOUNT_PARK",),
        evidence="Exact array iterator hotcount park; opt-out is only diagnostic.",
        next_step="Retire only after iterator_table array row compiles safely without broad fallback regression.",
    ),
    "LUAJIT_S390X_ITERATOR_HASH_ITERN_NOJIT_HOTCOUNT_PARK": GateMeta(
        group="iterator",
        kind="default-on exact hotcount park",
        classification="mechanism debt",
        owner="trace/iterator",
        protected_rows=("iterator_table/pairs_sum",),
        opt_out=("LUAJIT_S390X_DISABLE_ITERATOR_HASH_ITERN_NOJIT_HOTCOUNT_PARK",),
        evidence="Exact hash iterator hotcount park; opt-out is only diagnostic.",
        next_step="Retire only after hash pairs root body can compile safely and beat retained fallback.",
    ),
    "LUAJIT_S390X_ITERATOR_POST_PROTO_ITERN_NOHOT": GateMeta(
        group="iterator",
        kind="default-on exact no-hot",
        classification="mechanism debt",
        owner="trace/iterator",
        protected_rows=("iterator_table",),
        opt_out=("LUAJIT_S390X_DISABLE_ITERATOR_POST_PROTO_ITERN_NOHOT",),
        evidence="Complements the exact iterator proto-NOJIT path.",
        next_step="Retire with the exact iterator proto/hotcount family, not separately.",
    ),
    "LUAJIT_S390X_MIXED_NOFFI_ITERL_BLACKLIST": GateMeta(
        group="mixed_noffi",
        kind="opt-in exact blacklist",
        classification="mechanism debt",
        owner="trace/mixed_noffi",
        protected_rows=("mixed_noffi/mixed_loop",),
        evidence="Exact mixed ITERL root path restored after iterator guard ordering.",
        next_step="Retire only after mixed_noffi official row proves the exact ITERL root body can compile safely.",
    ),
    "LUAJIT_S390X_MIXED_NOFFI_ITERN_BLACKLIST": GateMeta(
        group="mixed_noffi",
        kind="opt-in exact blacklist",
        classification="mechanism debt",
        owner="trace/mixed_noffi",
        protected_rows=("mixed_noffi/mixed_loop",),
        evidence="Exact mixed ITERN route-around retained with broad iterator safety fallback.",
        next_step="Re-attribute mixed_noffi before removal; old stitched/hotside lanes are closed.",
    ),
    "LUAJIT_S390X_MIXED_NOFFI_FORL_STITCH_BLACKLIST": GateMeta(
        group="mixed_noffi",
        kind="opt-in exact blacklist",
        classification="mechanism debt",
        owner="trace/mixed_noffi",
        protected_rows=("mixed_noffi/mixed_loop",),
        evidence="Exact stitched mixed FORL family retained.",
        next_step="Do not reopen stitched hotside reuse/cooldown; replace only after fresh official mixed attribution.",
    ),
    "LUAJIT_S390X_MIXED_NOFFI_ITERL_ABORT_BLACKLIST": GateMeta(
        group="mixed_noffi",
        kind="opt-in abort blacklist",
        classification="mechanism debt",
        owner="trace/mixed_noffi",
        protected_rows=("mixed_noffi/mixed_loop",),
        evidence="Exact abort-route guard for mixed ITERL root.",
        next_step="Retire with the mixed ITERL mechanism, not as a standalone tweak.",
    ),
    "LUAJIT_S390X_MIXED_NOFFI_EARLY_PROTO_NOJIT": GateMeta(
        group="mixed_noffi",
        kind="opt-in proto-NOJIT",
        classification="mechanism debt",
        owner="trace/mixed_noffi",
        protected_rows=("mixed_noffi/mixed_loop",),
        evidence="Early exact mixed noffi proto park.",
        next_step="Needs fresh mixed attribution before removal.",
    ),
    "LUAJIT_S390X_LOCALIZED_HOTSIDE_CANON_SHARE_EQUIV": GateMeta(
        group="promotion_core",
        kind="opt-in localized hotside equivalence",
        classification="bake-in candidate",
        owner="trace/promotion_core",
        protected_rows=("be_helpers_localized", "promotion_core_static_stop", "route_around_reducers"),
        evidence="Localized hotside equivalence is retained for exact safe shapes.",
        next_step="Candidate for exact-shape bake-in after promotion-core and route-around rows pass without explicit env.",
    ),
    "LUAJIT_S390X_LOWER_FRAME_LUA_ABS_PROTO_NOJIT": GateMeta(
        group="lower_frame",
        kind="opt-in proto-NOJIT",
        classification="mechanism debt",
        owner="trace/lower_frame",
        protected_rows=("lower_frame_same_callsite/lua_abs_same_callsite",),
        evidence="Exact lower-frame route-around retained.",
        next_step="Keep until lower-frame same-callsite trace mechanism is replaced.",
    ),
    "LUAJIT_S390X_PROMOTION_CORE_FORL_PROTO_NOJIT": GateMeta(
        group="promotion_core",
        kind="opt-in broad proto-NOJIT with exact exclusions",
        classification="still unsafe",
        owner="trace/promotion_core",
        protected_rows=("promotion_core_static_stop", "route_around_reducers"),
        evidence="Broad removal was unsafe; exact official rows are now excluded only where host-pair proof exists.",
        next_step="Continue replacing this with exact safe compiled-shape exclusions; do not remove broadly.",
    ),
}


@dataclass
class SourceRef:
    path: str
    line: int
    text: str


def iter_source_files() -> list[pathlib.Path]:
    files: list[pathlib.Path] = []
    for glob in SOURCE_GLOBS:
        files.extend(ROOT.glob(glob))
    return sorted({path.resolve() for path in files})


def find_source_refs(patterns: list[str]) -> list[SourceRef]:
    needles = [re.escape(pattern) for pattern in patterns if pattern]
    if not needles:
        return []
    rx = re.compile("|".join(needles))
    refs: list[SourceRef] = []
    for path in iter_source_files():
        try:
            lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
        except OSError:
            continue
        for idx, line in enumerate(lines, 1):
            if rx.search(line):
                refs.append(
                    SourceRef(
                        path=str(path.relative_to(ROOT)),
                        line=idx,
                        text=line.strip(),
                    )
                )
    return refs


def inferred_patterns(name: str, meta: GateMeta | None) -> list[str]:
    patterns = [name]
    if meta:
        patterns.extend(meta.opt_out)
    disable = "LUAJIT_S390X_DISABLE_" + name.removeprefix("LUAJIT_S390X_")
    patterns.append(disable)
    return sorted(set(patterns))


def gate_row(name: str, value: str) -> dict[str, Any]:
    meta = META.get(name)
    patterns = inferred_patterns(name, meta)
    refs = find_source_refs(patterns)
    return {
        "env": name,
        "value": value,
        "group": meta.group if meta else "unknown",
        "kind": meta.kind if meta else "unknown",
        "classification": meta.classification if meta else "needs re-attribution",
        "owner": meta.owner if meta else "unknown",
        "protected_rows": list(meta.protected_rows) if meta else [],
        "opt_out": list(meta.opt_out) if meta else [],
        "evidence": meta.evidence if meta else "No ledger metadata yet.",
        "next_step": meta.next_step if meta else "Add metadata and run focused opt-out proof before removal.",
        "notes": list(meta.notes) if meta else [],
        "source_refs": [ref.__dict__ for ref in refs],
    }


def write_text(path: pathlib.Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def write_json(path: pathlib.Path, payload: Any) -> None:
    write_text(path, json.dumps(payload, indent=2, sort_keys=True) + "\n")


def write_markdown(output: pathlib.Path, rows: list[dict[str, Any]]) -> None:
    counts: dict[str, int] = {}
    for row in rows:
        counts[row["classification"]] = counts.get(row["classification"], 0) + 1

    lines = [
        "# s390x Guard Retirement Ledger",
        "",
        f"- Generated: `{dt.datetime.now().astimezone().isoformat()}`",
        f"- Retained env source: `tools/s390x/restamp_iterator_perf.py`",
        f"- Gate count: `{len(rows)}`",
        "",
        "## Classification Counts",
        "",
    ]
    for key, value in sorted(counts.items()):
        lines.append(f"- `{key}`: `{value}`")

    lines.extend(
        [
            "",
            "## Retirement Queue",
            "",
            "| env | group | kind | classification | protected rows | next step |",
            "| --- | --- | --- | --- | --- | --- |",
        ]
    )
    for row in rows:
        protected = ", ".join(f"`{item}`" for item in row["protected_rows"]) or "`n/a`"
        lines.append(
            f"| `{row['env']}={row['value']}` | `{row['group']}` | "
            f"`{row['kind']}` | `{row['classification']}` | {protected} | "
            f"{row['next_step']} |"
        )

    lines.extend(["", "## Details", ""])
    for row in rows:
        lines.extend(
            [
                f"### `{row['env']}`",
                "",
                f"- value: `{row['value']}`",
                f"- group: `{row['group']}`",
                f"- kind: `{row['kind']}`",
                f"- classification: `{row['classification']}`",
                f"- owner: `{row['owner']}`",
                f"- opt-out envs: {', '.join(f'`{item}`' for item in row['opt_out']) or '`n/a`'}",
                f"- protected rows: {', '.join(f'`{item}`' for item in row['protected_rows']) or '`n/a`'}",
                f"- evidence: {row['evidence']}",
                f"- next step: {row['next_step']}",
            ]
        )
        if row["source_refs"]:
            lines.append("- source refs:")
            for ref in row["source_refs"][:8]:
                lines.append(f"  - `{ref['path']}:{ref['line']}` `{ref['text']}`")
            if len(row["source_refs"]) > 8:
                lines.append(f"  - `... {len(row['source_refs']) - 8} more`")
        else:
            lines.append("- source refs: `none found`")
        lines.append("")

    write_text(output, "\n".join(lines).rstrip() + "\n")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output-dir",
        type=pathlib.Path,
        default=DEFAULT_OUTPUT_ROOT / f"s390x-guard-retirement-{dt.datetime.now().strftime('%Y%m%d%H%M%S')}",
        help="Output directory for ledger.md and ledger.json.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    rows = [
        gate_row(name, value)
        for name, value in restamp.RETAINED_BASELINE_ENV.items()
    ]
    rows.sort(key=lambda row: (row["classification"], row["group"], row["env"]))
    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    write_json(output_dir / "ledger.json", rows)
    write_markdown(output_dir / "ledger.md", rows)
    print(f"ledger={output_dir / 'ledger.md'}")
    print(f"json={output_dir / 'ledger.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
