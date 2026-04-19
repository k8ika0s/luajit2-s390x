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
        kind="removed trace-control guard",
        classification="retired source guard",
        owner="trace/dispatch",
        protected_rows=("dispatch_trace/numeric_loop",),
        opt_out=("LUAJIT_S390X_DISABLE_DISPATCH_FORL_SKIP_JFORI",),
        evidence="Removed from lj_trace.c during upstream cleanup; dispatch_trace remains covered by direct side-exit/codegen work.",
        next_step="Do not restore as a benchmark-shaped FORL matcher. Re-attribute dispatch with the truth pack if it regresses.",
    ),
    "LUAJIT_S390X_DISPATCH_FORL_PARK_ROOT_HOTEXIT_EXACT_COOLDOWN": GateMeta(
        group="dispatch",
        kind="removed valued cooldown",
        classification="retired source guard",
        owner="trace/dispatch",
        protected_rows=("dispatch_trace/hotexit_loop",),
        evidence="Removed with the stale dispatch FORL route-around; current retained env no longer carries the cooldown.",
        next_step="Do not restore without a fresh generic dispatch mechanism.",
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
        classification="retired source guard",
        owner="recorder/select",
        protected_rows=("vararg_paths/sum_loop",),
        evidence="Vararg correctness branch removed the select/sum_loop route-around and validates compiled vararg loop-index/header-limit traces.",
        next_step="Keep out of retained env; only reintroduce as diagnostic if a fresh vararg truth pack names the original seam again.",
    ),
    "LUAJIT_S390X_SUM_LOOP_SELECT_SKIP_FUNC_EQ": GateMeta(
        group="vararg",
        kind="opt-in recorder route-around",
        classification="retired source guard",
        owner="recorder/select",
        protected_rows=("vararg_paths/sum_loop",),
        evidence="Vararg correctness branch removed the select_detect guard cut and validates sum_loop against -joff.",
        next_step="Keep out of retained env unless a new exact failure identifies this guard independently.",
    ),
    "LUAJIT_S390X_SUM_LOOP_SELECT_CONST_GGET": GateMeta(
        group="vararg",
        kind="opt-in recorder fold",
        classification="retired source guard",
        owner="recorder/select",
        protected_rows=("vararg_paths/sum_loop",),
        evidence="No longer needed as an env gate after vararg select recording and retlast specialization fixes.",
        next_step="Do not restore as a benchmark gate; use the general select/vararg trace tests instead.",
    ),
    "LUAJIT_S390X_SUM_LOOP_FORL_BLACKLIST": GateMeta(
        group="vararg",
        kind="opt-in root blacklist",
        classification="retired source guard",
        owner="trace/vararg",
        protected_rows=("vararg_paths/sum_loop",),
        evidence="Vararg-root FORL now validates through vararg_paths and compiled_vararg without PROTO_NOJIT/root blacklists.",
        next_step="Keep retired; any new issue needs trace/snapshot evidence, not a broad vararg root blacklist.",
    ),
    "LUAJIT_S390X_VARARG_SIBLING_FORL_BLACKLIST": GateMeta(
        group="vararg",
        kind="opt-in sibling blacklist",
        classification="retired source guard",
        owner="trace/vararg",
        protected_rows=("vararg_paths/retlast_loop", "vararg_paths/retconst_loop"),
        evidence="Sibling vararg paths validate and retlast now compiles through the value-level recurrence fast path.",
        next_step="Keep retired; retlast/retconst regressions should be handled by vararg_correctness.lua and trace evidence.",
    ),
    "LUAJIT_S390X_MIXED_FFI_POST_STITCH_SAVE_DONE": GateMeta(
        group="mixed_ffi",
        kind="removed save DONE",
        classification="retired source guard",
        owner="trace/mixed_ffi",
        protected_rows=("mixed_ffi/mixed_ffi_loop",),
        evidence="Removed from lj_trace.c after canonical retained env stopped carrying it and focused mixed_ffi validation stayed clean.",
        next_step="Do not restore exact trace-number SAVE_DONE hooks; fix any future mixed_ffi issue by mechanism.",
    ),
    "LUAJIT_S390X_MIXED_FFI_FORL_PROTO_NOJIT": GateMeta(
        group="mixed_ffi",
        kind="removed opt-in proto-NOJIT",
        classification="retired source guard",
        owner="trace/mixed_ffi",
        protected_rows=("mixed_ffi/mixed_ffi_loop",),
        evidence="Removed from lj_trace.c after canonical retained env stopped carrying it and focused mixed_ffi validation stayed clean on kdz1 and zkd0.",
        next_step="Do not restore exact trace-shape proto parking; re-attribute any future mixed_ffi regression to a generic mechanism.",
    ),
    "LUAJIT_S390X_FFI_CDATA_PAIR_SAVE_DONE": GateMeta(
        group="ffi_cdata",
        kind="removed save DONE",
        classification="retired source guard",
        owner="trace/ffi_cdata",
        protected_rows=("ffi_cdata/pair_loop",),
        evidence="Removed from lj_trace.c after canonical retained env stopped carrying it and focused ffi_cdata validation stayed clean.",
        next_step="Do not restore exact trace-number SAVE_DONE hooks; fix any future cdata issue by mechanism.",
    ),
    "LUAJIT_S390X_FFI_CDATA_PAIR_FORL_BLACKLIST": GateMeta(
        group="ffi_cdata",
        kind="removed opt-in exact blacklist",
        classification="retired source guard",
        owner="trace/ffi_cdata",
        protected_rows=("ffi_cdata/pair_loop",),
        evidence="Removed from lj_trace.c after canonical retained env stopped carrying it and focused ffi_cdata validation stayed clean on kdz1 and zkd0.",
        next_step="Do not restore benchmark-shaped FORL blacklists; re-attribute future cdata regressions to helper/backend mechanisms.",
    ),
    "LUAJIT_S390X_ITERATOR_ITERN_BLACKLIST": GateMeta(
        group="iterator",
        kind="removed opt-in exact/broad blacklist",
        classification="retired source guard",
        owner="trace/iterator",
        protected_rows=("mixed_noffi", "jit_loops/pairs_loop.lua"),
        evidence="Removed after semantic iterator/mixed folds kept official rows timer-floor and kdz1 opt-out validation passed jit_loops, iterator_table, mixed_noffi, vararg_paths, dispatch_trace, mixed_ffi, and ffi_cdata.",
        next_step="Do not restore trace-shape blacklists; future iterator work should target the generic terminal restart path directly.",
    ),
    "LUAJIT_S390X_ITERATOR_ITERL_BLACKLIST": GateMeta(
        group="iterator",
        kind="removed opt-in exact/broad blacklist",
        classification="retired source guard",
        owner="trace/iterator",
        protected_rows=("mixed_noffi", "iterator_table"),
        evidence="Removed with the ITERN blacklist after current kdz1 guardrail validation showed the retained semantic folds no longer depend on this rail.",
        next_step="Do not restore trace-shape blacklists; re-attribute any new iterator failure to recorder/VM state.",
    ),
    "LUAJIT_S390X_ITERATOR_ITERN_PROTO_NOJIT": GateMeta(
        group="iterator",
        kind="removed default-on exact proto-NOJIT",
        classification="retired source guard",
        owner="trace/iterator",
        protected_rows=("iterator_table/pairs_sum", "iterator_table/pairs_array_sum"),
        opt_out=("LUAJIT_S390X_DISABLE_ITERATOR_ITERN_PROTO_NOJIT",),
        evidence="Removed after semantic loop-fold reachability replaced the exact iterator_table proto route-around and opt-out validation stayed clean.",
        next_step="No source action; keep historical opt-out references only for old artifact interpretation.",
    ),
    "LUAJIT_S390X_ITERATOR_ARRAY_ITERN_NOJIT_HOTCOUNT_PARK": GateMeta(
        group="iterator",
        kind="removed default-on exact hotcount park",
        classification="retired source guard",
        owner="trace/iterator",
        protected_rows=("iterator_table/pairs_array_sum",),
        opt_out=("LUAJIT_S390X_DISABLE_ITERATOR_ARRAY_ITERN_NOJIT_HOTCOUNT_PARK",),
        evidence="Removed with the exact proto-NOJIT family after semantic iterator-table folding covered the array row.",
        next_step="No source action.",
    ),
    "LUAJIT_S390X_ITERATOR_HASH_ITERN_NOJIT_HOTCOUNT_PARK": GateMeta(
        group="iterator",
        kind="removed default-on exact hotcount park",
        classification="retired source guard",
        owner="trace/iterator",
        protected_rows=("iterator_table/pairs_sum",),
        opt_out=("LUAJIT_S390X_DISABLE_ITERATOR_HASH_ITERN_NOJIT_HOTCOUNT_PARK",),
        evidence="Removed with the exact proto-NOJIT family after semantic iterator-table folding covered the hash row.",
        next_step="No source action.",
    ),
    "LUAJIT_S390X_ITERATOR_POST_PROTO_ITERN_NOHOT": GateMeta(
        group="iterator",
        kind="removed default-on exact no-hot",
        classification="retired source guard",
        owner="trace/iterator",
        protected_rows=("iterator_table",),
        opt_out=("LUAJIT_S390X_DISABLE_ITERATOR_POST_PROTO_ITERN_NOHOT",),
        evidence="Removed after current validation proved no post-proto ITERN dispatch override is needed.",
        next_step="No source action.",
    ),
    "LUAJIT_S390X_MIXED_NOFFI_ITERL_BLACKLIST": GateMeta(
        group="mixed_noffi",
        kind="removed opt-in exact blacklist",
        classification="retired source guard",
        owner="trace/mixed_noffi",
        protected_rows=("mixed_noffi/mixed_loop",),
        evidence="Removed from lj_trace.c after canonical retained env stopped carrying it and semantic loop-fold plus iterator fallback kept mixed_noffi clean on kdz1 and zkd0.",
        next_step="Do not restore exact mixed trace-shape blacklists; future mixed issues need semantic attribution.",
    ),
    "LUAJIT_S390X_MIXED_NOFFI_ITERN_BLACKLIST": GateMeta(
        group="mixed_noffi",
        kind="removed opt-in exact blacklist",
        classification="retired source guard",
        owner="trace/mixed_noffi",
        protected_rows=("mixed_noffi/mixed_loop",),
        evidence="Removed from lj_trace.c after canonical retained env stopped carrying it and semantic loop-fold plus iterator fallback kept mixed_noffi clean on kdz1 and zkd0.",
        next_step="Do not restore exact mixed trace-shape blacklists; future mixed issues need semantic attribution.",
    ),
    "LUAJIT_S390X_MIXED_NOFFI_FORL_STITCH_BLACKLIST": GateMeta(
        group="mixed_noffi",
        kind="removed opt-in exact blacklist",
        classification="retired source guard",
        owner="trace/mixed_noffi",
        protected_rows=("mixed_noffi/mixed_loop",),
        evidence="Removed from lj_trace.c after canonical retained env stopped carrying it and mixed_noffi validation stayed clean on kdz1 and zkd0.",
        next_step="Do not restore exact stitched-family blacklists; future stitched work needs semantic mechanism proof.",
    ),
    "LUAJIT_S390X_MIXED_NOFFI_ITERL_ABORT_BLACKLIST": GateMeta(
        group="mixed_noffi",
        kind="removed opt-in abort blacklist",
        classification="retired source guard",
        owner="trace/mixed_noffi",
        protected_rows=("mixed_noffi/mixed_loop",),
        evidence="Removed from lj_trace.c after canonical retained env stopped carrying it and mixed_noffi validation stayed clean on kdz1 and zkd0.",
        next_step="Do not restore exact abort-route blacklists; future abort handling must be generic.",
    ),
    "LUAJIT_S390X_MIXED_NOFFI_EARLY_PROTO_NOJIT": GateMeta(
        group="mixed_noffi",
        kind="removed opt-in proto-NOJIT",
        classification="retired source guard",
        owner="trace/mixed_noffi",
        protected_rows=("mixed_noffi/mixed_loop",),
        evidence="Removed from lj_trace.c with the stale exact mixed noffi blacklist family.",
        next_step="Do not restore exact mixed proto parking; use semantic loop-fold or iterator mechanism fixes.",
    ),
    "LUAJIT_S390X_LOCALIZED_HOTSIDE_CANON_SHARE_EQUIV": GateMeta(
        group="promotion_core",
        kind="removed opt-in localized hotside equivalence",
        classification="retired source guard",
        owner="trace/promotion_core",
        protected_rows=("be_helpers_localized", "promotion_core_static_stop", "route_around_reducers"),
        evidence="Removed from lj_trace.c after semantic recorder/backend folds covered the localized rows and kdz1/zkd0 focused validation stayed clean.",
        next_step="Do not restore benchmark-shaped localized hotside matching; future hotside work must be bytecode/IR semantic.",
    ),
    "LUAJIT_S390X_LOWER_FRAME_LUA_ABS_PROTO_NOJIT": GateMeta(
        group="lower_frame",
        kind="removed opt-in proto-NOJIT",
        classification="retired source guard",
        owner="trace/lower_frame",
        protected_rows=("lower_frame_same_callsite/lua_abs_same_callsite",),
        evidence="Removed from lj_trace.c after canonical retained env stopped carrying it and focused lower-frame validation stayed clean on kdz1 and zkd0.",
        next_step="Do not restore exact lower-frame proto parking; the same-callsite row is covered by semantic recorder/backend folds.",
    ),
    "LUAJIT_S390X_PROMOTION_CORE_FORL_PROTO_NOJIT": GateMeta(
        group="promotion_core",
        kind="removed broad proto-NOJIT with exact exclusions",
        classification="retired source guard",
        owner="trace/promotion_core",
        protected_rows=("promotion_core_static_stop", "route_around_reducers"),
        evidence="Removed from lj_trace.c after recorder/backend semantic replacements made the current generic-only debt flat.",
        next_step="Do not restore benchmark-shaped promotion-core proto parking; re-attribute any future regression to a generic mechanism.",
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
