#!/usr/bin/env python3
"""Compare a s390x perf artifact against a carried x86 perf artifact."""

from __future__ import annotations

import argparse
import csv
import json
import math
import pathlib
import statistics
from collections import defaultdict
from typing import Iterable


ROOT = pathlib.Path(__file__).resolve().parents[2]


def relpath(path: pathlib.Path) -> str:
    try:
        return str(path.resolve().relative_to(ROOT.resolve()))
    except ValueError:
        return str(path)


def load_records(artifact: pathlib.Path) -> list[dict]:
    candidates = [
        artifact / "perf" / "benchmarks.json",
        artifact / "benchmarks.json",
        artifact / "benchmarks.jsonl",
    ]
    for path in candidates:
        if not path.exists():
            continue
        if path.suffix == ".json":
            data = json.loads(path.read_text(encoding="utf-8"))
            if isinstance(data, list):
                return [record for record in data if isinstance(record, dict)]
            raise SystemExit(f"unsupported benchmark JSON shape: {path}")
        records = []
        for raw in path.read_text(encoding="utf-8").splitlines():
            line = raw.strip()
            if line:
                records.append(json.loads(line))
        return records
    raise SystemExit(f"no benchmark records found under {artifact}")


def load_failure_count(artifact: pathlib.Path) -> int:
    for name in ("benchmark-failures.jsonl", "failures.json"):
        path = artifact / name
        if not path.exists():
            continue
        if path.suffix == ".jsonl":
            return sum(1 for raw in path.read_text(encoding="utf-8").splitlines() if raw.strip())
        data = json.loads(path.read_text(encoding="utf-8"))
        if isinstance(data, list):
            return len(data)
    manifest = artifact / "manifest.json"
    if manifest.exists():
        data = json.loads(manifest.read_text(encoding="utf-8"))
        failures = data.get("failures")
        if isinstance(failures, list):
            return len(failures)
    return 0


def is_baseline(record: dict) -> bool:
    return record.get("tuning") in (None, "", "baseline")


def runtime_index(records: Iterable[dict]) -> dict[tuple[str, str, str, str, str], float]:
    grouped: dict[tuple[str, str, str, str, str], list[float]] = defaultdict(list)
    for record in records:
        if not is_baseline(record):
            continue
        if record.get("correct") is False:
            continue
        runtime = record.get("median_runtime_sec")
        if runtime is None:
            continue
        key = (
            str(record.get("compiler", "")),
            str(record.get("family", "")),
            str(record.get("workload", "")),
            str(record.get("scale", "")),
            str(record.get("jit", "")),
        )
        grouped[key].append(float(runtime))
    return {key: statistics.median(values) for key, values in grouped.items() if values}


def row_keys(*indices: dict[tuple[str, str, str, str, str], float]) -> list[tuple[str, str, str, str]]:
    keys = set()
    for index in indices:
        for compiler, family, workload, scale, _jit in index:
            keys.add((compiler, family, workload, scale))
    return sorted(keys)


def div(numerator: float | None, denominator: float | None) -> float | None:
    if numerator is None or denominator is None or numerator <= 0 or denominator <= 0:
        return None
    return numerator / denominator


def geomean(values: Iterable[float]) -> float | None:
    vals = [v for v in values if v and v > 0 and math.isfinite(v)]
    if not vals:
        return None
    return math.exp(sum(math.log(v) for v in vals) / len(vals))


def fmt_seconds(value: float | None) -> str:
    if value is None:
        return "n/a"
    return f"{value:.6f}"


def fmt_speedup(value: float | None) -> str:
    if value is None:
        return "n/a"
    return f"{value:.3f}x"


def fmt_arch(value: float | None) -> str:
    if value is None or value <= 0:
        return "n/a"
    if value >= 1:
        return f"+{value:.3f}x"
    return f"-{(1.0 / value):.3f}x"


def csv_value(value: float | None) -> str:
    return "n/a" if value is None else repr(value)


def build_rows(s390x_records: list[dict], x86_records: list[dict]) -> list[dict]:
    s390x = runtime_index(s390x_records)
    x86 = runtime_index(x86_records)
    rows = []
    for compiler, family, workload, scale in row_keys(s390x, x86):
        s_on = s390x.get((compiler, family, workload, scale, "on"))
        s_off = s390x.get((compiler, family, workload, scale, "off"))
        x_on = x86.get((compiler, family, workload, scale, "on"))
        x_off = x86.get((compiler, family, workload, scale, "off"))
        rows.append(
            {
                "compiler": compiler,
                "family": family,
                "workload": workload,
                "scale": scale,
                "s390x_jit_on": s_on,
                "s390x_jit_off": s_off,
                "s390x_jit_on_faster": div(s_off, s_on),
                "x86_jit_on": x_on,
                "x86_jit_off": x_off,
                "x86_jit_on_faster": div(x_off, x_on),
                "s390x_vs_x86_jit_on": div(x_on, s_on),
                "s390x_vs_x86_jit_off": div(x_off, s_off),
            }
        )
    return rows


def family_rollup(rows: list[dict]) -> list[dict]:
    grouped: dict[tuple[str, str], list[dict]] = defaultdict(list)
    for row in rows:
        grouped[(row["compiler"], row["family"])].append(row)
    out = []
    for (compiler, family), group in sorted(grouped.items()):
        out.append(
            {
                "compiler": compiler,
                "family": family,
                "rows": len(group),
                "s390x_jit_on_faster": geomean(row["s390x_jit_on_faster"] for row in group),
                "x86_jit_on_faster": geomean(row["x86_jit_on_faster"] for row in group),
                "s390x_vs_x86_jit_on": geomean(row["s390x_vs_x86_jit_on"] for row in group),
                "s390x_vs_x86_jit_off": geomean(row["s390x_vs_x86_jit_off"] for row in group),
            }
        )
    return out


def write_csv(path: pathlib.Path, rows: list[dict]) -> None:
    fields = [
        "compiler",
        "family",
        "workload",
        "scale",
        "s390x_jit_on",
        "s390x_jit_off",
        "s390x_jit_on_faster",
        "x86_jit_on",
        "x86_jit_off",
        "x86_jit_on_faster",
        "s390x_vs_x86_jit_on",
        "s390x_vs_x86_jit_off",
    ]
    with path.open("w", newline="", encoding="utf-8") as fh:
        writer = csv.DictWriter(fh, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row[field] if isinstance(row[field], str) else csv_value(row[field]) for field in fields})


def write_rollup_csv(path: pathlib.Path, rows: list[dict]) -> None:
    fields = [
        "compiler",
        "family",
        "rows",
        "s390x_jit_on_faster",
        "x86_jit_on_faster",
        "s390x_vs_x86_jit_on",
        "s390x_vs_x86_jit_off",
    ]
    with path.open("w", newline="", encoding="utf-8") as fh:
        writer = csv.DictWriter(fh, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row[field] if isinstance(row[field], (str, int)) else csv_value(row[field]) for field in fields})


def row_complete(row: dict) -> bool:
    return all(
        row[key] is not None
        for key in (
            "s390x_jit_on",
            "s390x_jit_off",
            "x86_jit_on",
            "x86_jit_off",
        )
    )


def markdown_table(headers: list[str], body: list[list[str]]) -> list[str]:
    lines = [
        "| " + " | ".join(headers) + " |",
        "| " + " | ".join("---" for _ in headers) + " |",
    ]
    lines.extend("| " + " | ".join(row) + " |" for row in body)
    return lines


def write_markdown(
    path: pathlib.Path,
    *,
    rows: list[dict],
    rollup: list[dict],
    s390x_artifact: pathlib.Path,
    x86_artifact: pathlib.Path,
    previous: pathlib.Path | None,
    s390x_failures: int,
    x86_failures: int,
) -> None:
    complete_rows = sum(1 for row in rows if row_complete(row))
    missing_s390x = sum(1 for row in rows if row["s390x_jit_on"] is None or row["s390x_jit_off"] is None)
    missing_x86 = sum(1 for row in rows if row["x86_jit_on"] is None or row["x86_jit_off"] is None)

    lines = [
        "# kdz1 s390x vs ka0s01 x86 Performance Matrix",
        "",
        "Positive architecture ratios mean s390x is faster than x86 for that JIT mode. Negative ratios mean s390x is slower than x86, with the magnitude showing how many times slower.",
        "",
        "## Inputs",
        f"- s390x artifact: `{relpath(s390x_artifact)}`",
        f"- x86 artifact: `{relpath(x86_artifact)}`",
    ]
    if previous:
        lines.append(f"- Previous comparison format reference: `{relpath(previous)}`")
    lines.extend(
        [
            "- Full CSV: `combined-comparison.csv`",
            "- Family rollup CSV: `family-rollup.csv`",
            "",
            "## Run Counts",
            f"- Total comparison rows: `{len(rows)}`",
            f"- Rows with complete s390x/x86 on+off data: `{complete_rows}`",
            f"- Rows missing s390x on/off data: `{missing_s390x}`",
            f"- Rows missing x86 on/off data: `{missing_x86}`",
            f"- s390x failures: `{s390x_failures}`",
            f"- x86 failures: `{x86_failures}`",
            "",
            "## Family Rollup",
        ]
    )
    lines.extend(
        markdown_table(
            [
                "Compiler",
                "Family",
                "Rows",
                "s390x JIT on faster",
                "x86 JIT on faster",
                "s390x vs x86 JIT on",
                "s390x vs x86 JIT off",
            ],
            [
                [
                    row["compiler"],
                    row["family"],
                    str(row["rows"]),
                    fmt_speedup(row["s390x_jit_on_faster"]),
                    fmt_speedup(row["x86_jit_on_faster"]),
                    fmt_arch(row["s390x_vs_x86_jit_on"]),
                    fmt_arch(row["s390x_vs_x86_jit_off"]),
                ]
                for row in rollup
            ],
        )
    )

    def row_fields(row: dict) -> list[str]:
        return [
            row["compiler"],
            row["family"],
            row["workload"],
            row["scale"],
            fmt_seconds(row["s390x_jit_on"]),
            fmt_seconds(row["s390x_jit_off"]),
            fmt_speedup(row["s390x_jit_on_faster"]),
            fmt_seconds(row["x86_jit_on"]),
            fmt_seconds(row["x86_jit_off"]),
            fmt_speedup(row["x86_jit_on_faster"]),
            fmt_arch(row["s390x_vs_x86_jit_on"]),
            fmt_arch(row["s390x_vs_x86_jit_off"]),
        ]

    table_headers = [
        "Compiler",
        "Family",
        "Workload",
        "Scale",
        "s390x JIT on (s)",
        "s390x JIT off (s)",
        "s390x JIT on faster",
        "x86 JIT on (s)",
        "x86 JIT off (s)",
        "x86 JIT on faster",
        "s390x vs x86 JIT on",
        "s390x vs x86 JIT off",
    ]
    advantages = sorted(
        [row for row in rows if row["s390x_vs_x86_jit_on"] is not None],
        key=lambda row: row["s390x_vs_x86_jit_on"],
        reverse=True,
    )[:20]
    disadvantages = sorted(
        [row for row in rows if row["s390x_vs_x86_jit_on"] is not None],
        key=lambda row: row["s390x_vs_x86_jit_on"],
    )[:20]
    slowest = sorted(
        [row for row in rows if row["s390x_jit_on"] is not None],
        key=lambda row: row["s390x_jit_on"],
        reverse=True,
    )[:20]
    regressions = sorted(
        [
            row for row in rows
            if row["s390x_jit_on_faster"] is not None and row["s390x_jit_on_faster"] < 1.0
        ],
        key=lambda row: row["s390x_jit_on_faster"],
    )[:20]
    missing = [
        row for row in rows
        if row["s390x_jit_on"] is None or row["s390x_jit_off"] is None or row["x86_jit_on"] is None or row["x86_jit_off"] is None
    ][:40]

    lines.extend(["", "## Largest s390x JIT-On Advantages"])
    lines.extend(markdown_table(table_headers, [row_fields(row) for row in advantages]))
    lines.extend(["", "## Largest s390x JIT-On Disadvantages"])
    lines.extend(markdown_table(table_headers, [row_fields(row) for row in disadvantages]))
    lines.extend(["", "## Largest s390x JIT-On Runtimes"])
    lines.extend(markdown_table(table_headers, [row_fields(row) for row in slowest]))
    lines.extend(["", "## s390x JIT-On Slower Than -joff"])
    if regressions:
        lines.extend(markdown_table(table_headers, [row_fields(row) for row in regressions]))
    else:
        lines.append("- None")
    lines.extend(["", "## Missing Data Audit"])
    if missing:
        lines.extend(markdown_table(table_headers, [row_fields(row) for row in missing]))
    else:
        lines.append("- No missing s390x/x86 on/off cells.")
    lines.extend(["", "## Full Matrix"])
    lines.extend(markdown_table(table_headers, [row_fields(row) for row in rows]))
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--s390x-artifact", required=True, type=pathlib.Path)
    parser.add_argument("--x86-artifact", required=True, type=pathlib.Path)
    parser.add_argument("--output-dir", required=True, type=pathlib.Path)
    parser.add_argument("--previous-comparison", type=pathlib.Path)
    args = parser.parse_args()

    s390x_artifact = args.s390x_artifact
    x86_artifact = args.x86_artifact
    output_dir = args.output_dir
    output_dir.mkdir(parents=True, exist_ok=True)

    s390x_records = load_records(s390x_artifact)
    x86_records = load_records(x86_artifact)
    rows = build_rows(s390x_records, x86_records)
    rollup = family_rollup(rows)

    write_csv(output_dir / "combined-comparison.csv", rows)
    write_rollup_csv(output_dir / "family-rollup.csv", rollup)
    s390x_failures = load_failure_count(s390x_artifact)
    x86_failures = load_failure_count(x86_artifact)
    summary = {
        "rows": len(rows),
        "complete_rows": sum(1 for row in rows if row_complete(row)),
        "s390x_missing_rows": sum(1 for row in rows if row["s390x_jit_on"] is None or row["s390x_jit_off"] is None),
        "x86_missing_rows": sum(1 for row in rows if row["x86_jit_on"] is None or row["x86_jit_off"] is None),
        "s390x_failures": s390x_failures,
        "x86_failures": x86_failures,
        "s390x_artifact": relpath(s390x_artifact),
        "x86_artifact": relpath(x86_artifact),
        "previous_comparison_format_reference": relpath(args.previous_comparison) if args.previous_comparison else None,
        "output_dir": relpath(output_dir),
    }
    (output_dir / "summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    write_markdown(
        output_dir / "combined-comparison.md",
        rows=rows,
        rollup=rollup,
        s390x_artifact=s390x_artifact,
        x86_artifact=x86_artifact,
        previous=args.previous_comparison,
        s390x_failures=s390x_failures,
        x86_failures=x86_failures,
    )
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
