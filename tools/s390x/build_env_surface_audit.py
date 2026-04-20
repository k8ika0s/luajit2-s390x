#!/usr/bin/env python3
"""Inventory and classify the s390x environment-variable surface.

This is a source-level audit, not a perf validator. It answers a different
question from the retained jitter probes: which knobs still exist, which knobs
are part of the retained perf contract, and which ones are only diagnostics,
default-on opt-outs, or historical experiment hooks.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import pathlib
import re
import sys
from dataclasses import dataclass
from typing import Any

THIS_DIR = pathlib.Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

import restamp_iterator_perf as restamp


ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT_ROOT = pathlib.Path("/tmp")
ENV_RE = re.compile(r"LUAJIT_S390X_[A-Z0-9_]+")
SOURCE_GLOBS = (
    "src/*.c",
    "src/*.h",
    "src/*.dasc",
    "tests/s390x/**/*.lua",
    "tools/s390x/*.py",
)

DEBUG_MARKERS = (
    "LOG",
    "DUMP",
    "TRACE",
    "PROBE",
    "FOCUS",
    "COUNTER",
    "SNAP",
    "SLOT",
    "BADRA",
    "RESTORE",
    "REPLAY_PARENT",
    "REPLAY_EXIT",
    "PREF_REG",
)

EXPERIMENT_MARKERS = (
    "CHILD",
    "DESC",
    "HOTSIDE",
    "JLOOP",
    "LOOPDESC",
    "REENTER",
    "RESUME",
    "RETARGET",
    "SKIP",
    "FORCE",
    "ALLOW",
    "MARK",
    "RETRY",
    "PARK",
    "BLACKLIST",
    "PROTO_NOJIT",
    "SAVE_DONE",
    "DONE",
    "HANDOFF",
    "BIAS",
    "MCLOOP",
    "RECLOOP",
    "STUB",
    "TRIPLET",
)

DEFAULT_ON_KNOWN = {
    "LUAJIT_S390X_DISABLE_AREF_BASE_ALLGPR",
    "LUAJIT_S390X_DISABLE_BYTE_SCAN_CYCLE",
    "LUAJIT_S390X_DISABLE_BYTE_SCAN_SUM",
    "LUAJIT_S390X_DISABLE_CONCAT_SLICE",
    "LUAJIT_S390X_DISABLE_COUNT_LT_CLIP",
    "LUAJIT_S390X_DISABLE_DIRECT_CALL_ARG",
    "LUAJIT_S390X_DISABLE_FORL_CURRENT_COMPARE_FIX",
    "LUAJIT_S390X_DISABLE_GC64_SIGNED_INT_SLOAD",
    "LUAJIT_S390X_DISABLE_INT_MINMAX",
    "LUAJIT_S390X_DISABLE_MANUAL_FIND",
    "LUAJIT_S390X_DISABLE_MANUAL_FIND_CYCLE",
    "LUAJIT_S390X_DISABLE_NARROW_XSTORE",
    "LUAJIT_S390X_DISABLE_SMALL_TABLE_LEN_CONST",
    "LUAJIT_S390X_DISABLE_SMALL_TABLE_UPVALUE_CONST",
    "LUAJIT_S390X_DISABLE_STRSCAN_NUM_CACHE",
    "LUAJIT_S390X_DISABLE_STRING_SUB_EQ_MEMCMP",
}

RETAINED_NOTES = {}


@dataclass(frozen=True)
class Ref:
    path: str
    line: int
    text: str


def iter_files() -> list[pathlib.Path]:
    files: set[pathlib.Path] = set()
    for glob in SOURCE_GLOBS:
        files.update(path.resolve() for path in ROOT.glob(glob))
    return sorted(files)


def collect_refs() -> dict[str, list[Ref]]:
    refs: dict[str, list[Ref]] = {}
    for path in iter_files():
        try:
            lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
        except OSError:
            continue
        rel = str(path.relative_to(ROOT))
        for line_no, line in enumerate(lines, 1):
            for env in sorted(set(ENV_RE.findall(line))):
                refs.setdefault(env, []).append(Ref(rel, line_no, line.strip()))
    return refs


def is_debug_env(env: str) -> bool:
    return any(marker in env for marker in DEBUG_MARKERS)


def is_experiment_env(env: str) -> bool:
    return any(marker in env for marker in EXPERIMENT_MARKERS)


def category_for(env: str, retained: set[str], refs: list[Ref]) -> tuple[str, str]:
    source_refs = [ref for ref in refs if ref.path.startswith("src/")]
    test_refs = [ref for ref in refs if ref.path.startswith("tests/")]
    tool_refs = [ref for ref in refs if ref.path.startswith("tools/")]

    if env in retained:
        return "retained opt-in safety rail", RETAINED_NOTES.get(env, "")
    if not source_refs and not test_refs and tool_refs:
        return "tooling-only historical reference", "Referenced by tooling only; not a live source behavior knob."
    if env in DEFAULT_ON_KNOWN or env.startswith("LUAJIT_S390X_DISABLE_"):
        return "default-on feature opt-out", "Feature is enabled by default; env disables it for causality or safety checks."
    if is_debug_env(env):
        return "debug/probe only", "Logging, focus, dump, or truth-pack instrumentation knob."
    if any(ref.path.startswith("tests/") for ref in refs) and not any(ref.path.startswith("src/") for ref in refs):
        return "test-only setup", "Only referenced by tests or perf probes."
    if is_experiment_env(env):
        return "experimental opt-in or historical route-around", "Not part of the retained perf contract; requires fresh mechanism proof before use."
    return "unclassified source knob", "Needs owner review before removal or promotion."


def row_for(env: str, retained: set[str], refs: list[Ref]) -> dict[str, Any]:
    category, note = category_for(env, retained, refs)
    source_refs = [ref for ref in refs if ref.path.startswith("src/")]
    test_refs = [ref for ref in refs if ref.path.startswith("tests/")]
    tool_refs = [ref for ref in refs if ref.path.startswith("tools/")]
    return {
        "env": env,
        "category": category,
        "note": note,
        "source_ref_count": len(source_refs),
        "test_ref_count": len(test_refs),
        "tool_ref_count": len(tool_refs),
        "refs": [ref.__dict__ for ref in refs],
    }


def write_json(path: pathlib.Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def write_markdown(path: pathlib.Path, rows: list[dict[str, Any]]) -> None:
    counts: dict[str, int] = {}
    for row in rows:
        counts[row["category"]] = counts.get(row["category"], 0) + 1

    lines = [
        "# s390x Environment Surface Audit",
        "",
        f"- Generated: `{dt.datetime.now().astimezone().isoformat()}`",
        "- Scope: `src/`, `tests/s390x/`, and `tools/s390x/` source references",
        f"- Total unique env names: `{len(rows)}`",
        f"- Retained perf env count: `{len(restamp.RETAINED_BASELINE_ENV)}`",
        "",
        "## Category Counts",
        "",
    ]
    for category, count in sorted(counts.items()):
        lines.append(f"- `{category}`: `{count}`")

    lines.extend(
        [
            "",
            "## Retained Perf Contract",
            "",
        ]
    )
    retained = sorted(
        (row for row in rows if row["category"] == "retained opt-in safety rail"),
        key=lambda row: row["env"],
    )
    if retained:
        for row in retained:
            lines.append(f"- `{row['env']}`: {row['note']}")
    else:
        lines.append("- No retained opt-in env gates.")

    lines.extend(
        [
            "",
            "## Non-Retained Behavior Gates",
            "",
            "| env | category | source refs | test refs | tool refs | note |",
            "| --- | --- | ---: | ---: | ---: | --- |",
        ]
    )
    for row in rows:
        if row["category"] == "debug/probe only":
            continue
        lines.append(
            f"| `{row['env']}` | `{row['category']}` | "
            f"`{row['source_ref_count']}` | `{row['test_ref_count']}` | "
            f"`{row['tool_ref_count']}` | {row['note']} |"
        )

    lines.extend(["", "## Debug And Probe Surface", ""])
    debug_rows = [row for row in rows if row["category"] == "debug/probe only"]
    if debug_rows:
        for row in debug_rows:
            lines.append(f"- `{row['env']}`")
    else:
        lines.append("- No debug/probe envs found.")

    lines.extend(["", "## Reference Index", ""])
    for row in rows:
        lines.append(f"### `{row['env']}`")
        lines.append("")
        lines.append(f"- Category: `{row['category']}`")
        lines.append(f"- Note: {row['note']}")
        for ref in row["refs"][:8]:
            lines.append(f"- `{ref['path']}:{ref['line']}` `{ref['text']}`")
        if len(row["refs"]) > 8:
            lines.append(f"- ... {len(row['refs']) - 8} more references")
        lines.append("")

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", default=None)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%d%H%M%S")
    outdir = pathlib.Path(args.output_dir) if args.output_dir else DEFAULT_OUTPUT_ROOT / f"s390x-env-surface-{stamp}"
    refs = {env: env_refs for env, env_refs in collect_refs().items() if not env.endswith("_")}
    retained = set(restamp.RETAINED_BASELINE_ENV)
    rows = [row_for(env, retained, refs[env]) for env in sorted(refs)]
    payload = {
        "generated_at": dt.datetime.now().astimezone().isoformat(),
        "retained_env": dict(sorted(restamp.RETAINED_BASELINE_ENV.items())),
        "rows": rows,
    }
    write_json(outdir / "env_surface.json", payload)
    write_markdown(outdir / "env_surface.md", rows)
    print(f"markdown={outdir / 'env_surface.md'}")
    print(f"json={outdir / 'env_surface.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
