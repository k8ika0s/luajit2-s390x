#!/usr/bin/env python3
"""Compare standard perf artifacts in cross-target or single-target profile mode."""

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
PROFILE_ORDER = ["jit_off", "jit_on", "h1e10", "h1e1"]
PROFILE_FIELDS = {
    "jit_off": "jit_off",
    "jit_on": "jit_on",
    "h1e10": "jit_accel_h1e10",
    "h1e1": "jit_accel_h1e1",
}
PROFILE_RATIO_FIELDS = {
    "jit_on": "jit_on_faster",
    "h1e10": "jit_accel_h1e10_faster",
    "h1e1": "jit_accel_h1e1_faster",
}
PROFILE_TITLES = {
    "jit_on": "JIT-On",
    "h1e10": "Accelerated h1e10",
    "h1e1": "Accelerated h1e1",
}
FAMILY_ALIASES = {
    "route_around_reducers_truth_pack": "route_around_reducers",
}


def load_manifest(artifact: pathlib.Path) -> dict:
    manifest = artifact / "manifest.json"
    if not manifest.exists():
        return {}
    data = json.loads(manifest.read_text(encoding="utf-8"))
    return data if isinstance(data, dict) else {}


def relpath(path: pathlib.Path) -> str:
    try:
        return str(path.resolve().relative_to(ROOT.resolve()))
    except ValueError:
        return str(path)


def load_records(artifact: pathlib.Path) -> list[dict]:
    candidates = [
        artifact / "performance" / "benchmarks.json",
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


def infer_target_label(artifact: pathlib.Path, records: list[dict], manifest: dict, fallback: str) -> str:
    archs = {
        str(record.get("target_arch"))
        for record in records
        if record.get("target_arch")
    }
    if len(archs) == 1:
        return next(iter(archs))
    for key in ("target_arch", "arch"):
        value = manifest.get(key)
        if value:
            return str(value)
    return fallback


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


def markdown_table(headers: list[str], body: list[list[str]]) -> list[str]:
    lines = [
        "| " + " | ".join(headers) + " |",
        "| " + " | ".join("---" for _ in headers) + " |",
    ]
    lines.extend("| " + " | ".join(row) + " |" for row in body)
    return lines


def glossary_lines(cross_target: bool) -> list[str]:
    lines = [
        "## Glossary",
        "- `small`, `medium`, `hot`, and any extra labels such as `xhot` are family-local workload buckets, not global sizes. Read them against the owning benchmark file.",
    ]
    if cross_target:
        lines.extend(
            [
                "- `JIT on faster` means the geometric-mean `jit-off / jit-on` speedup for the listed row set on that architecture.",
                "- Architecture comparison columns use `x86_jit_on / s390x_jit_on` or `x86_jit_off / s390x_jit_off`. Positive values mean s390x is faster; negative values mean s390x is slower by that factor.",
            ]
        )
    else:
        lines.extend(
            [
                "- `h1e10` means `hotloop=1 hotexit=10`.",
                "- `h1e1` means `hotloop=1 hotexit=1`.",
                "- Family rollups and conclusion rows use geometric means over the rows with data for that comparison.",
            ]
        )
    return lines


def record_variant_id(record: dict) -> str:
    variant = record.get("variant")
    if variant:
        return str(variant)
    compiler = str(record.get("compiler", ""))
    jit = str(record.get("jit", ""))
    return f"{compiler}-legacy-jit-{jit}" if compiler and jit else jit


def record_family(record: dict) -> str:
    family = str(record.get("family", ""))
    return FAMILY_ALIASES.get(family, family)


def record_runtime(record: dict) -> float | None:
    runtime = record.get("median_runtime_sec")
    if runtime is None:
        return None
    return float(runtime)


def classify_profile_variant(record: dict) -> str | None:
    variant = record_variant_id(record)
    if variant.endswith("jit-off"):
        return "jit_off"
    if variant.endswith("jit-on"):
        return "jit_on"
    if variant.endswith("jit-accel-h1e10"):
        return "h1e10"
    if variant.endswith("jit-accel-h1e1"):
        return "h1e1"
    return None


def runtime_index_by_variant(records: Iterable[dict]) -> dict[tuple[str, str, str, str, str], float]:
    grouped: dict[tuple[str, str, str, str, str], list[float]] = defaultdict(list)
    for record in records:
        if not is_baseline(record):
            continue
        if record.get("correct") is False:
            continue
        runtime = record_runtime(record)
        if runtime is None:
            continue
        key = (
            str(record.get("compiler", "")),
            record_family(record),
            str(record.get("workload", "")),
            str(record.get("scale", "")),
            record_variant_id(record),
        )
        grouped[key].append(runtime)
    return {key: statistics.median(values) for key, values in grouped.items() if values}


def runtime_index_cross_target(records: Iterable[dict]) -> dict[tuple[str, str, str, str, str], float]:
    grouped: dict[tuple[str, str, str, str, str], list[float]] = defaultdict(list)
    for record in records:
        if not is_baseline(record):
            continue
        if record.get("correct") is False:
            continue
        runtime = record_runtime(record)
        if runtime is None:
            continue
        variant = record_variant_id(record)
        jit_key = None
        if variant.endswith("jit-off"):
            jit_key = "off"
        elif variant.endswith("jit-on"):
            jit_key = "on"
        elif record.get("variant") is None and str(record.get("jit", "")) in {"on", "off"}:
            jit_key = str(record.get("jit"))
        if jit_key is None:
            continue
        key = (
            str(record.get("compiler", "")),
            record_family(record),
            str(record.get("workload", "")),
            str(record.get("scale", "")),
            jit_key,
        )
        grouped[key].append(runtime)
    return {key: statistics.median(values) for key, values in grouped.items() if values}


def row_keys_variants(*indices: dict[tuple[str, str, str, str, str], float]) -> list[tuple[str, str, str, str]]:
    keys = set()
    for index in indices:
        for compiler, family, workload, scale, _variant in index:
            keys.add((compiler, family, workload, scale))
    return sorted(keys)


def build_cross_target_rows(s390x_records: list[dict], x86_records: list[dict]) -> list[dict]:
    s390x = runtime_index_cross_target(s390x_records)
    x86 = runtime_index_cross_target(x86_records)
    rows = []
    for compiler, family, workload, scale in row_keys_variants(s390x, x86):
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


def cross_target_family_rollup(rows: list[dict]) -> list[dict]:
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


def build_profile_rows(records: list[dict]) -> list[dict]:
    index = runtime_index_by_variant(records)
    rows = []
    for compiler, family, workload, scale in row_keys_variants(index):
        values: dict[str, float | None] = {name: None for name in PROFILE_ORDER}
        for variant_id, runtime in ((key[4], val) for key, val in index.items() if key[:4] == (compiler, family, workload, scale)):
            profile = classify_profile_variant({"variant": variant_id})
            if profile is not None:
                values[profile] = runtime
        row = {
            "compiler": compiler,
            "family": family,
            "workload": workload,
            "scale": scale,
            "jit_off": values["jit_off"],
            "jit_on": values["jit_on"],
            "jit_on_faster": div(values["jit_off"], values["jit_on"]),
            "jit_accel_h1e10": values["h1e10"],
            "jit_accel_h1e10_faster": div(values["jit_off"], values["h1e10"]),
            "jit_accel_h1e1": values["h1e1"],
            "jit_accel_h1e1_faster": div(values["jit_off"], values["h1e1"]),
        }
        rows.append(row)
    return rows


def profile_family_rollup(rows: list[dict]) -> list[dict]:
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
                "jit_on_faster": geomean(row["jit_on_faster"] for row in group),
                "jit_accel_h1e10_faster": geomean(row["jit_accel_h1e10_faster"] for row in group),
                "jit_accel_h1e1_faster": geomean(row["jit_accel_h1e1_faster"] for row in group),
            }
        )
    return out


def profile_conclusion_rows(rows: list[dict]) -> list[dict]:
    grouped: dict[str, list[dict]] = defaultdict(list)
    for row in rows:
        grouped[row["compiler"]].append(row)
    grouped["pooled"] = list(rows)
    out = []
    for compiler in ("gcc", "clang", "pooled"):
        if compiler not in grouped:
            continue
        group = grouped[compiler]
        out.append(
            {
                "compiler": compiler,
                "rows": len(group),
                "jit_on_faster": geomean(row["jit_on_faster"] for row in group),
                "jit_accel_h1e10_faster": geomean(row["jit_accel_h1e10_faster"] for row in group),
                "jit_accel_h1e1_faster": geomean(row["jit_accel_h1e1_faster"] for row in group),
            }
        )
    return out


def cross_target_row_complete(row: dict) -> bool:
    return all(
        row[key] is not None
        for key in ("s390x_jit_on", "s390x_jit_off", "x86_jit_on", "x86_jit_off")
    )


def profile_row_complete(row: dict) -> bool:
    return all(row[key] is not None for key in ("jit_off", "jit_on", "jit_accel_h1e10", "jit_accel_h1e1"))


def write_cross_target_csv(path: pathlib.Path, rows: list[dict]) -> None:
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


def write_cross_target_rollup_csv(path: pathlib.Path, rows: list[dict]) -> None:
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


def write_profile_csv(path: pathlib.Path, rows: list[dict]) -> None:
    fields = [
        "compiler",
        "family",
        "workload",
        "scale",
        "jit_off",
        "jit_on",
        "jit_on_faster",
        "jit_accel_h1e10",
        "jit_accel_h1e10_faster",
        "jit_accel_h1e1",
        "jit_accel_h1e1_faster",
    ]
    with path.open("w", newline="", encoding="utf-8") as fh:
        writer = csv.DictWriter(fh, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row[field] if isinstance(row[field], str) else csv_value(row[field]) for field in fields})


def write_profile_rollup_csv(path: pathlib.Path, rows: list[dict]) -> None:
    fields = [
        "compiler",
        "family",
        "rows",
        "jit_on_faster",
        "jit_accel_h1e10_faster",
        "jit_accel_h1e1_faster",
    ]
    with path.open("w", newline="", encoding="utf-8") as fh:
        writer = csv.DictWriter(fh, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row[field] if isinstance(row[field], (str, int)) else csv_value(row[field]) for field in fields})


def write_cross_target_markdown(
    path: pathlib.Path,
    *,
    rows: list[dict],
    rollup: list[dict],
    s390x_artifact: pathlib.Path,
    x86_artifact: pathlib.Path,
    s390x_label: str,
    x86_label: str,
    previous: pathlib.Path | None,
    s390x_failures: int,
    x86_failures: int,
) -> None:
    complete_rows = sum(1 for row in rows if cross_target_row_complete(row))
    missing_s390x = sum(1 for row in rows if row["s390x_jit_on"] is None or row["s390x_jit_off"] is None)
    missing_x86 = sum(1 for row in rows if row["x86_jit_on"] is None or row["x86_jit_off"] is None)
    lines = [
        f"# {s390x_label} vs {x86_label} Performance Matrix",
        "",
        f"Positive architecture ratios mean {s390x_label} is faster than {x86_label} for that JIT mode. Negative ratios mean {s390x_label} is slower than {x86_label}, with the magnitude showing how many times slower.",
        "",
        "## Inputs",
        f"- {s390x_label} artifact: `{relpath(s390x_artifact)}`",
        f"- {x86_label} artifact: `{relpath(x86_artifact)}`",
    ]
    if previous:
        lines.append(f"- Previous comparison format reference: `{relpath(previous)}`")
    lines.extend(
        [
            "- Full CSV: `combined-comparison.csv`",
            "- Family rollup CSV: `family-rollup.csv`",
            "",
        ]
    )
    lines.extend(glossary_lines(cross_target=True))
    lines.extend(
        [
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
                f"{s390x_label} JIT on faster",
                f"{x86_label} JIT on faster",
                f"{s390x_label} vs {x86_label} JIT on",
                f"{s390x_label} vs {x86_label} JIT off",
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

    headers = [
        "Compiler",
        "Family",
        "Workload",
        "Scale",
        f"{s390x_label} JIT on (s)",
        f"{s390x_label} JIT off (s)",
        f"{s390x_label} JIT on faster",
        f"{x86_label} JIT on (s)",
        f"{x86_label} JIT off (s)",
        f"{x86_label} JIT on faster",
        f"{s390x_label} vs {x86_label} JIT on",
        f"{s390x_label} vs {x86_label} JIT off",
    ]
    advantages = sorted([row for row in rows if row["s390x_vs_x86_jit_on"] is not None], key=lambda row: row["s390x_vs_x86_jit_on"], reverse=True)[:20]
    disadvantages = sorted([row for row in rows if row["s390x_vs_x86_jit_on"] is not None], key=lambda row: row["s390x_vs_x86_jit_on"])[:20]
    slowest = sorted([row for row in rows if row["s390x_jit_on"] is not None], key=lambda row: row["s390x_jit_on"], reverse=True)[:20]
    regressions = sorted([row for row in rows if row["s390x_jit_on_faster"] is not None and row["s390x_jit_on_faster"] < 1.0], key=lambda row: row["s390x_jit_on_faster"])[:20]
    missing = [row for row in rows if not cross_target_row_complete(row)][:40]
    lines.extend(["", f"## Largest {s390x_label} JIT-On Advantages"])
    lines.extend(markdown_table(headers, [row_fields(row) for row in advantages]))
    lines.extend(["", f"## Largest {s390x_label} JIT-On Disadvantages"])
    lines.extend(markdown_table(headers, [row_fields(row) for row in disadvantages]))
    lines.extend(["", f"## Largest {s390x_label} JIT-On Runtimes"])
    lines.extend(markdown_table(headers, [row_fields(row) for row in slowest]))
    lines.extend(["", f"## {s390x_label} JIT-On Slower Than -joff"])
    lines.extend(markdown_table(headers, [row_fields(row) for row in regressions]) if regressions else ["- None"])
    lines.extend(["", "## Missing Data Audit"])
    lines.extend(markdown_table(headers, [row_fields(row) for row in missing]) if missing else [f"- No missing {s390x_label}/{x86_label} on/off cells."])
    lines.extend(["", "## Full Matrix"])
    lines.extend(markdown_table(headers, [row_fields(row) for row in rows]))
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_profile_markdown(
    path: pathlib.Path,
    *,
    rows: list[dict],
    rollup: list[dict],
    conclusions: list[dict],
    profile_artifacts: list[pathlib.Path],
    profile_label: str,
    previous: pathlib.Path | None,
    failures: list[tuple[str, int]],
) -> None:
    complete_rows = sum(1 for row in rows if profile_row_complete(row))
    missing_counts = {
        "jit_off": sum(1 for row in rows if row["jit_off"] is None),
        "jit_on": sum(1 for row in rows if row["jit_on"] is None),
        "h1e10": sum(1 for row in rows if row["jit_accel_h1e10"] is None),
        "h1e1": sum(1 for row in rows if row["jit_accel_h1e1"] is None),
    }
    lines = [
        f"# {profile_label} Performance Matrix",
        "",
        "Positive speedups mean the compared mode is faster than `jit-off` for that row.",
        "",
        "## Inputs",
    ]
    lines.extend([f"- Artifact: `{relpath(artifact)}`" for artifact in profile_artifacts])
    if previous:
        lines.append(f"- Previous comparison format reference: `{relpath(previous)}`")
    lines.extend(
        [
            "- Full CSV: `combined-comparison.csv`",
            "- Family rollup CSV: `family-rollup.csv`",
            "",
        ]
    )
    lines.extend(glossary_lines(cross_target=False))
    lines.extend(
        [
            "",
            "## Run Counts",
            f"- Total comparison rows: `{len(rows)}`",
            f"- Rows with complete jit-off/jit-on/h1e10/h1e1 data: `{complete_rows}`",
            f"- Rows missing jit-off data: `{missing_counts['jit_off']}`",
            f"- Rows missing jit-on data: `{missing_counts['jit_on']}`",
            f"- Rows missing h1e10 data: `{missing_counts['h1e10']}`",
            f"- Rows missing h1e1 data: `{missing_counts['h1e1']}`",
            f"- Artifact failure counts: `{', '.join(f'{name}={count}' for name, count in failures)}`",
            "",
            "## Conclusion Row",
        ]
    )
    lines.extend(
        markdown_table(
            ["Compiler", "Rows", "JIT on vs off", "h1e10 vs off", "h1e1 vs off"],
            [
                [
                    row["compiler"],
                    str(row["rows"]),
                    fmt_speedup(row["jit_on_faster"]),
                    fmt_speedup(row["jit_accel_h1e10_faster"]),
                    fmt_speedup(row["jit_accel_h1e1_faster"]),
                ]
                for row in conclusions
            ],
        )
    )
    lines.extend(["", "## Family Rollup"])
    lines.extend(
        markdown_table(
            ["Compiler", "Family", "Rows", "JIT on faster", "h1e10 faster", "h1e1 faster"],
            [
                [
                    row["compiler"],
                    row["family"],
                    str(row["rows"]),
                    fmt_speedup(row["jit_on_faster"]),
                    fmt_speedup(row["jit_accel_h1e10_faster"]),
                    fmt_speedup(row["jit_accel_h1e1_faster"]),
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
            fmt_seconds(row["jit_off"]),
            fmt_seconds(row["jit_on"]),
            fmt_speedup(row["jit_on_faster"]),
            fmt_seconds(row["jit_accel_h1e10"]),
            fmt_speedup(row["jit_accel_h1e10_faster"]),
            fmt_seconds(row["jit_accel_h1e1"]),
            fmt_speedup(row["jit_accel_h1e1_faster"]),
        ]

    headers = [
        "Compiler",
        "Family",
        "Workload",
        "Scale",
        "JIT off (s)",
        "JIT on (s)",
        "JIT on faster",
        "h1e10 (s)",
        "h1e10 faster",
        "h1e1 (s)",
        "h1e1 faster",
    ]
    jit_advantages = sorted([row for row in rows if row["jit_on_faster"] is not None], key=lambda row: row["jit_on_faster"], reverse=True)[:20]
    jit_disadvantages = sorted([row for row in rows if row["jit_on_faster"] is not None], key=lambda row: row["jit_on_faster"])[:20]
    h1e10_advantages = sorted([row for row in rows if row["jit_accel_h1e10_faster"] is not None], key=lambda row: row["jit_accel_h1e10_faster"], reverse=True)[:20]
    h1e1_advantages = sorted([row for row in rows if row["jit_accel_h1e1_faster"] is not None], key=lambda row: row["jit_accel_h1e1_faster"], reverse=True)[:20]
    missing = [row for row in rows if not profile_row_complete(row)][:40]
    lines.extend(["", "## Largest JIT-On Advantages"])
    lines.extend(markdown_table(headers, [row_fields(row) for row in jit_advantages]))
    lines.extend(["", "## Largest JIT-On Disadvantages"])
    lines.extend(markdown_table(headers, [row_fields(row) for row in jit_disadvantages]))
    lines.extend(["", "## Largest h1e10 Advantages"])
    lines.extend(markdown_table(headers, [row_fields(row) for row in h1e10_advantages]))
    lines.extend(["", "## Largest h1e1 Advantages"])
    lines.extend(markdown_table(headers, [row_fields(row) for row in h1e1_advantages]))
    lines.extend(["", "## Missing Data Audit"])
    lines.extend(markdown_table(headers, [row_fields(row) for row in missing]) if missing else ["- No missing jit-off/jit-on/h1e10/h1e1 cells."])
    lines.extend(["", "## Full Matrix"])
    lines.extend(markdown_table(headers, [row_fields(row) for row in rows]))
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=pathlib.Path)
    parser.add_argument("--previous-comparison", type=pathlib.Path)
    parser.add_argument("--s390x-artifact", type=pathlib.Path)
    parser.add_argument("--x86-artifact", type=pathlib.Path)
    parser.add_argument("--s390x-label")
    parser.add_argument("--x86-label")
    parser.add_argument("--profile-artifact", action="append", type=pathlib.Path, default=[])
    parser.add_argument("--profile-label")
    return parser.parse_args()


def run_cross_target(args: argparse.Namespace) -> int:
    s390x_artifact = args.s390x_artifact
    x86_artifact = args.x86_artifact
    assert s390x_artifact is not None
    assert x86_artifact is not None
    output_dir = args.output_dir
    output_dir.mkdir(parents=True, exist_ok=True)
    s390x_records = load_records(s390x_artifact)
    x86_records = load_records(x86_artifact)
    s390x_manifest = load_manifest(s390x_artifact)
    x86_manifest = load_manifest(x86_artifact)
    s390x_label = args.s390x_label or infer_target_label(s390x_artifact, s390x_records, s390x_manifest, "s390x")
    x86_label = args.x86_label or infer_target_label(x86_artifact, x86_records, x86_manifest, "x86")
    rows = build_cross_target_rows(s390x_records, x86_records)
    rollup = cross_target_family_rollup(rows)
    write_cross_target_csv(output_dir / "combined-comparison.csv", rows)
    write_cross_target_rollup_csv(output_dir / "family-rollup.csv", rollup)
    s390x_failures = load_failure_count(s390x_artifact)
    x86_failures = load_failure_count(x86_artifact)
    summary = {
        "mode": "cross-target",
        "rows": len(rows),
        "complete_rows": sum(1 for row in rows if cross_target_row_complete(row)),
        "s390x_missing_rows": sum(1 for row in rows if row["s390x_jit_on"] is None or row["s390x_jit_off"] is None),
        "x86_missing_rows": sum(1 for row in rows if row["x86_jit_on"] is None or row["x86_jit_off"] is None),
        "s390x_failures": s390x_failures,
        "x86_failures": x86_failures,
        "s390x_label": s390x_label,
        "x86_label": x86_label,
        "s390x_artifact": relpath(s390x_artifact),
        "x86_artifact": relpath(x86_artifact),
        "previous_comparison_format_reference": relpath(args.previous_comparison) if args.previous_comparison else None,
        "output_dir": relpath(output_dir),
    }
    (output_dir / "summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    write_cross_target_markdown(
        output_dir / "combined-comparison.md",
        rows=rows,
        rollup=rollup,
        s390x_artifact=s390x_artifact,
        x86_artifact=x86_artifact,
        s390x_label=s390x_label,
        x86_label=x86_label,
        previous=args.previous_comparison,
        s390x_failures=s390x_failures,
        x86_failures=x86_failures,
    )
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


def run_profile_mode(args: argparse.Namespace) -> int:
    artifacts = args.profile_artifact
    if not artifacts:
        raise SystemExit("profile mode requires at least one --profile-artifact")
    output_dir = args.output_dir
    output_dir.mkdir(parents=True, exist_ok=True)
    records: list[dict] = []
    failures: list[tuple[str, int]] = []
    for artifact in artifacts:
        records.extend(load_records(artifact))
        failures.append((artifact.name, load_failure_count(artifact)))
    rows = build_profile_rows(records)
    rollup = profile_family_rollup(rows)
    conclusions = profile_conclusion_rows(rows)
    write_profile_csv(output_dir / "combined-comparison.csv", rows)
    write_profile_rollup_csv(output_dir / "family-rollup.csv", rollup)
    summary = {
        "mode": "profile",
        "rows": len(rows),
        "complete_rows": sum(1 for row in rows if profile_row_complete(row)),
        "missing_rows": {
            "jit_off": sum(1 for row in rows if row["jit_off"] is None),
            "jit_on": sum(1 for row in rows if row["jit_on"] is None),
            "h1e10": sum(1 for row in rows if row["jit_accel_h1e10"] is None),
            "h1e1": sum(1 for row in rows if row["jit_accel_h1e1"] is None),
        },
        "artifact_failures": {name: count for name, count in failures},
        "profile_artifacts": [relpath(path) for path in artifacts],
        "profile_label": args.profile_label or artifacts[0].name,
        "previous_comparison_format_reference": relpath(args.previous_comparison) if args.previous_comparison else None,
        "output_dir": relpath(output_dir),
    }
    (output_dir / "summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    write_profile_markdown(
        output_dir / "combined-comparison.md",
        rows=rows,
        rollup=rollup,
        conclusions=conclusions,
        profile_artifacts=artifacts,
        profile_label=args.profile_label or artifacts[0].name,
        previous=args.previous_comparison,
        failures=failures,
    )
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


def main() -> int:
    args = parse_args()
    if args.s390x_artifact and args.x86_artifact:
        return run_cross_target(args)
    if args.profile_artifact:
        return run_profile_mode(args)
    raise SystemExit("use either --s390x-artifact/--x86-artifact or one-or-more --profile-artifact arguments")


if __name__ == "__main__":
    raise SystemExit(main())
