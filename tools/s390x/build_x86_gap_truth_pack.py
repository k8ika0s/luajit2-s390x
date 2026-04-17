#!/usr/bin/env python3
"""Rank cross-arch acceleration gaps from a s390x/x86 comparison artifact."""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import json
import math
import pathlib
from typing import Any


ROOT = pathlib.Path(__file__).resolve().parents[2]
COMPARISON_ROOT = ROOT / "artifacts" / "s390x"
DEFAULT_OUTPUT_ROOT = COMPARISON_ROOT / "x86-gap"


def parse_float(value: str | None) -> float | None:
    if value is None:
        return None
    text = value.strip()
    if not text or text.lower() in {"n/a", "nan", "none"}:
        return None
    try:
        number = float(text)
    except ValueError:
        return None
    if math.isnan(number) or math.isinf(number):
        return None
    return number


def find_latest_comparison() -> pathlib.Path:
    candidates = sorted(
        COMPARISON_ROOT.glob("compare-*/combined-comparison.csv"),
        key=lambda path: path.stat().st_mtime,
        reverse=True,
    )
    if not candidates:
        raise FileNotFoundError("no artifacts/s390x/compare-*/combined-comparison.csv files found")
    return candidates[0]


def target_for(family: str, workload: str) -> dict[str, str]:
    if family == "lower_frame_same_callsite":
        return {"target": "lower_frame_body", "status": "supported"}
    if family == "route_around_reducers_truth_pack" or workload.startswith("be_pack"):
        return {"target": "route_around_reducers", "status": "supported"}
    if family == "numeric_ops":
        return {"target": "numeric_ops_micro", "status": "supported"}
    if family == "ffi_cdata":
        return {"target": "ffi_cdata_width", "status": "supported"}
    if family == "ffi_fixed_call_pressure":
        return {"target": "ffi_fixed_gpr", "status": "supported"}
    if family in {"iterator_table", "mixed_noffi"}:
        return {"target": "iterator_safety", "status": "coverage-gated"}
    if family == "be_helpers" and workload == "strto_loop":
        return {"target": "string_scan", "status": "supported"}
    if family in {"be_helpers", "be_helpers_localized"} and "number_helper" in workload:
        return {"target": "be_number_helper", "status": "supported"}
    if family in {"bitops_mix", "logical_chain_tail_add", "logical_chain_tail_store"}:
        return {"target": "low32_home", "status": "timer-floor-check"}
    if family == "string_heavy":
        return {"target": "string_heavy", "status": "timer-floor-check"}
    return {"target": "unmapped", "status": "needs-manual-attribution"}


def row_key(row: dict[str, str]) -> str:
    return f"{row.get('family', '')}/{row.get('workload', '')}/{row.get('scale', '')}"


def load_rows(path: pathlib.Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as fh:
        return list(csv.DictReader(fh))


def enriched_gap_rows(
    rows: list[dict[str, str]],
    *,
    min_s390x_jit_on: float,
    min_x86_faster: float,
) -> list[dict[str, Any]]:
    out: list[dict[str, Any]] = []
    for row in rows:
        s390x_jit = parse_float(row.get("s390x_jit_on"))
        x86_jit = parse_float(row.get("x86_jit_on"))
        ratio = parse_float(row.get("s390x_vs_x86_jit_on"))
        if s390x_jit is None or x86_jit is None or ratio is None:
            continue
        if s390x_jit < min_s390x_jit_on or ratio <= 0:
            continue
        x86_faster = 1.0 / ratio
        if x86_faster < min_x86_faster:
            continue
        target = target_for(row["family"], row["workload"])
        enriched = dict(row)
        enriched.update(
            {
                "row": row_key(row),
                "s390x_jit_on_float": s390x_jit,
                "x86_jit_on_float": x86_jit,
                "x86_faster_float": x86_faster,
                "truth_pack_target": target["target"],
                "target_status": target["status"],
            }
        )
        out.append(enriched)
    return out


def missing_x86_rows(rows: list[dict[str, str]]) -> list[dict[str, Any]]:
    missing: list[dict[str, Any]] = []
    for row in rows:
        s390x_jit = parse_float(row.get("s390x_jit_on"))
        if s390x_jit is None:
            continue
        x86_jit = parse_float(row.get("x86_jit_on"))
        if x86_jit is not None:
            continue
        target = target_for(row["family"], row["workload"])
        item = dict(row)
        item.update(
            {
                "row": row_key(row),
                "missing_x86_jit_on": True,
                "truth_pack_target": target["target"],
                "target_status": target["status"],
            }
        )
        missing.append(item)
    return missing


def write_csv(path: pathlib.Path, rows: list[dict[str, Any]], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as fh:
        writer = csv.DictWriter(fh, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def format_seconds(value: Any) -> str:
    number = parse_float(str(value)) if value is not None else None
    if number is None:
        return "n/a"
    return f"{number:.6f}s"


def format_ratio(value: Any) -> str:
    number = parse_float(str(value)) if value is not None else None
    if number is None:
        return "n/a"
    return f"{number:.2f}x"


def target_rollup(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    grouped: dict[str, dict[str, Any]] = {}
    for row in rows:
        target = row["truth_pack_target"]
        item = grouped.setdefault(
            target,
            {
                "target": target,
                "status": row["target_status"],
                "rows": 0,
                "max_s390x_jit_on": 0.0,
                "max_x86_faster": 0.0,
                "top_row": "",
            },
        )
        item["rows"] += 1
        if row["s390x_jit_on_float"] > item["max_s390x_jit_on"]:
            item["max_s390x_jit_on"] = row["s390x_jit_on_float"]
            item["top_row"] = row["row"]
        item["max_x86_faster"] = max(item["max_x86_faster"], row["x86_faster_float"])
    return sorted(
        grouped.values(),
        key=lambda item: (item["max_s390x_jit_on"], item["max_x86_faster"]),
        reverse=True,
    )


def write_summary(
    *,
    output_dir: pathlib.Path,
    comparison: pathlib.Path,
    absolute_rows: list[dict[str, Any]],
    ratio_rows: list[dict[str, Any]],
    missing_rows: list[dict[str, Any]],
    rollup: list[dict[str, Any]],
    top: int,
    min_s390x_jit_on: float,
    min_x86_faster: float,
) -> None:
    lines = [
        "# x86-Gap Acceleration Truth Pack",
        "",
        f"- Comparison: `{comparison}`",
        f"- Generated: `{dt.datetime.now().astimezone().isoformat()}`",
        f"- Filter: s390x JIT-on >= `{min_s390x_jit_on:.6f}s`, x86 faster by >= `{min_x86_faster:.2f}x`",
        "",
        "## Target Rollup",
        "",
        "| target | status | rows | top row | max s390x JIT-on | max x86-over-s390x |",
        "| --- | --- | ---: | --- | ---: | ---: |",
    ]
    for item in rollup:
        lines.append(
            f"| `{item['target']}` | `{item['status']}` | `{item['rows']}` | "
            f"`{item['top_row']}` | `{item['max_s390x_jit_on']:.6f}s` | "
            f"`{item['max_x86_faster']:.2f}x` |"
        )
    lines.extend(["", "## Top Absolute Runtime Gaps", ""])
    lines.append("| row | compiler | s390x JIT-on | x86 JIT-on | x86-over-s390x | truth pack |")
    lines.append("| --- | --- | ---: | ---: | ---: | --- |")
    for row in absolute_rows[:top]:
        lines.append(
            f"| `{row['row']}` | `{row.get('compiler', 'n/a')}` | "
            f"`{format_seconds(row.get('s390x_jit_on'))}` | `{format_seconds(row.get('x86_jit_on'))}` | "
            f"`{format_ratio(row.get('x86_faster_float'))}` | `{row['truth_pack_target']}` |"
        )
    lines.extend(["", "## Top Ratio Gaps", ""])
    lines.append("| row | compiler | s390x JIT-on | x86 JIT-on | x86-over-s390x | truth pack |")
    lines.append("| --- | --- | ---: | ---: | ---: | --- |")
    for row in ratio_rows[:top]:
        lines.append(
            f"| `{row['row']}` | `{row.get('compiler', 'n/a')}` | "
            f"`{format_seconds(row.get('s390x_jit_on'))}` | `{format_seconds(row.get('x86_jit_on'))}` | "
            f"`{format_ratio(row.get('x86_faster_float'))}` | `{row['truth_pack_target']}` |"
        )
    lines.extend(
        [
            "",
            "## Missing x86 Coverage",
            "",
            f"- Rows with s390x JIT-on data and missing x86 JIT-on data: `{len(missing_rows)}`",
            "",
        ]
    )
    if missing_rows:
        lines.append("| row | compiler | s390x JIT-on | truth pack |")
        lines.append("| --- | --- | ---: | --- |")
        for row in missing_rows[:top]:
            lines.append(
                f"| `{row['row']}` | `{row.get('compiler', 'n/a')}` | "
                f"`{format_seconds(row.get('s390x_jit_on'))}` | "
                f"`{row['truth_pack_target']}` |"
            )
    lines.extend(
        [
            "",
            "## Files",
            "",
            f"- Absolute ranking: `{output_dir / 'ranked-absolute.csv'}`",
            f"- Ratio ranking: `{output_dir / 'ranked-ratio.csv'}`",
            f"- Missing x86 audit: `{output_dir / 'missing-x86.csv'}`",
            f"- Target plan: `{output_dir / 'target-plan.json'}`",
        ]
    )
    (output_dir / "summary.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--comparison", type=pathlib.Path)
    parser.add_argument("--output-dir", type=pathlib.Path)
    parser.add_argument("--min-s390x-jit-on", type=float, default=0.000100)
    parser.add_argument("--min-x86-faster", type=float, default=1.25)
    parser.add_argument("--top", type=int, default=30)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    comparison = (args.comparison or find_latest_comparison()).resolve()
    rows = load_rows(comparison)
    gap_rows = enriched_gap_rows(
        rows,
        min_s390x_jit_on=args.min_s390x_jit_on,
        min_x86_faster=args.min_x86_faster,
    )
    absolute_rows = sorted(
        gap_rows,
        key=lambda row: (row["s390x_jit_on_float"], row["x86_faster_float"]),
        reverse=True,
    )
    ratio_rows = sorted(
        gap_rows,
        key=lambda row: (row["x86_faster_float"], row["s390x_jit_on_float"]),
        reverse=True,
    )
    missing_rows = missing_x86_rows(rows)
    rollup = target_rollup(gap_rows)

    timestamp = dt.datetime.now().strftime("%Y%m%dT%H%M%S")
    output_dir = (args.output_dir or DEFAULT_OUTPUT_ROOT / f"x86-gap-{timestamp}").resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    fields = [
        "compiler",
        "family",
        "workload",
        "scale",
        "row",
        "s390x_jit_on",
        "x86_jit_on",
        "s390x_vs_x86_jit_on",
        "x86_faster_float",
        "s390x_vs_joff",
        "truth_pack_target",
        "target_status",
    ]
    write_csv(output_dir / "ranked-absolute.csv", absolute_rows, fields)
    write_csv(output_dir / "ranked-ratio.csv", ratio_rows, fields)
    write_csv(
        output_dir / "missing-x86.csv",
        missing_rows,
        [
            "compiler",
            "family",
            "workload",
            "scale",
            "row",
            "s390x_jit_on",
            "x86_jit_on",
            "missing_x86_jit_on",
            "truth_pack_target",
            "target_status",
        ],
    )
    (output_dir / "target-plan.json").write_text(
        json.dumps({"targets": rollup, "comparison": str(comparison)}, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    write_summary(
        output_dir=output_dir,
        comparison=comparison,
        absolute_rows=absolute_rows,
        ratio_rows=ratio_rows,
        missing_rows=missing_rows,
        rollup=rollup,
        top=args.top,
        min_s390x_jit_on=args.min_s390x_jit_on,
        min_x86_faster=args.min_x86_faster,
    )
    print(f"summary={output_dir / 'summary.md'}")
    print(f"ranked_absolute={output_dir / 'ranked-absolute.csv'}")
    print(f"missing_x86={output_dir / 'missing-x86.csv'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
