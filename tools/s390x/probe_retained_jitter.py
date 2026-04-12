#!/usr/bin/env python3
"""Probe retained-env process jitter for official s390x perf rows."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import pathlib
import shlex
import statistics
from typing import Any

import restamp_iterator_perf as restamp


ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT_ROOT = pathlib.Path("/tmp")
BENCH_FILES: dict[str, str] = {
    "be_helpers": "tests/s390x/perf/be_helpers.lua",
    "be_helpers_localized": "tests/s390x/perf/be_helpers_localized.lua",
    "bitops_mix": "tests/s390x/perf/bitops_mix.lua",
    "dispatch_trace": "tests/s390x/perf/dispatch_trace.lua",
    "ffi_calls": "tests/s390x/perf/ffi_calls.lua",
    "ffi_calls_static_stop": "tests/s390x/perf/ffi_calls_static_stop.lua",
    "ffi_cdata": "tests/s390x/perf/ffi_cdata.lua",
    "ffi_fixed_call_pressure": "tests/s390x/perf/ffi_fixed_call_pressure.lua",
    "ffi_fixed_struct_calls": "tests/s390x/perf/ffi_fixed_struct_calls.lua",
    "int_add_phi_only": "tests/s390x/perf/int_add_phi_only.lua",
    "iterator_table": "tests/s390x/perf/iterator_table.lua",
    "large_immediates": "tests/s390x/perf/large_immediates.lua",
    "logic_add_phi_noboundary": "tests/s390x/perf/logic_add_phi_noboundary.lua",
    "logical_chain_tail_add": "tests/s390x/perf/logical_chain_tail_add.lua",
    "logical_chain_tail_store": "tests/s390x/perf/logical_chain_tail_store.lua",
    "lower_frame_same_callsite": "tests/s390x/perf/lower_frame_same_callsite.lua",
    "mixed_ffi": "tests/s390x/perf/mixed_ffi.lua",
    "mixed_noffi": "tests/s390x/perf/mixed_noffi.lua",
    "numeric_ops": "tests/s390x/perf/numeric_ops.lua",
    "promotion_core_static_stop": "tests/s390x/perf/promotion_core_static_stop.lua",
    "route_around_reducers": "tests/s390x/perf/route_around_reducers.lua",
    "vararg_paths": "tests/s390x/perf/vararg_paths.lua",
}
ORACLE_BENCH_FAMILIES = {
    "ffi_fixed_call_pressure",
    "ffi_fixed_struct_calls",
}


def write_text(path: pathlib.Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def load_jsonl(text: str) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    for line in text.splitlines():
        line = line.strip()
        if line:
            records.append(json.loads(line))
    return records


def record_key(record: dict[str, Any]) -> str:
    return f"{record.get('family', '')}/{record.get('workload', '')}/{record.get('scale', '')}"


def record_median(record: dict[str, Any]) -> float:
    return float(record.get("median_runtime_sec", record.get("median_sec")))


def remote_env_prefix(env: dict[str, str]) -> str:
    return "env " + " ".join(f"{key}={shlex.quote(value)}" for key, value in sorted(env.items()))


def parse_env_overrides(items: list[str]) -> dict[str, str]:
    overrides: dict[str, str] = {}
    for item in items:
        if "=" not in item:
            raise SystemExit(f"invalid --env entry {item!r}; expected KEY=VALUE")
        key, value = item.split("=", 1)
        key = key.strip()
        if not key:
            raise SystemExit(f"invalid --env entry {item!r}; missing KEY")
        overrides[key] = value
    return overrides


def build_remote_oracles(host: str, repo: str, raw_dir: pathlib.Path) -> None:
    script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
CC=gcc sh tests/s390x/build_oracles.sh
test -s tests/s390x/ffi_abi/build/liboracle.so
"""
    proc = restamp.run_ssh_script(host, script)
    write_text(raw_dir / "oracle-build.stdout.log", proc.stdout)
    write_text(raw_dir / "oracle-build.stderr.log", proc.stderr)
    restamp.require_ok(proc, f"{host} oracle build")


def build_luajit_line(
    *,
    repo: str,
    bench_file: str,
    remote_json: str,
    samples: int,
    warmup: int,
    pin_core: int | None,
    joff: bool,
    extra_env: dict[str, str],
) -> str:
    env = {
        **extra_env,
        "S390X_PERF_OUTPUT_JSONL": remote_json,
        "S390X_PERF_WARMUP": str(warmup),
        "S390X_PERF_SAMPLES": str(samples),
        "S390X_PERF_BENCH_FILE": bench_file,
    }
    taskset = f"taskset -c {pin_core} " if pin_core is not None else ""
    joff_arg = "-joff " if joff else ""
    return (
        f"{remote_env_prefix(env)} {taskset}./src/luajit "
        f"{joff_arg}{shlex.quote(bench_file)}"
    )


def run_mode(
    *,
    host: str,
    repo: str,
    remote_json: str,
    local_json: pathlib.Path,
    stdout_log: pathlib.Path,
    stderr_log: pathlib.Path,
    bench_files: list[str],
    samples: int,
    warmup: int,
    pin_core: int | None,
    joff: bool,
    extra_env: dict[str, str],
) -> list[dict[str, Any]]:
    lines = [
        "set -euo pipefail",
        f"cd {shlex.quote(repo)}",
        'export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"',
        f"rm -f {shlex.quote(remote_json)}",
    ]
    for bench_file in bench_files:
        lines.append(
            build_luajit_line(
                repo=repo,
                bench_file=bench_file,
                remote_json=remote_json,
                samples=samples,
                warmup=warmup,
                pin_core=pin_core,
                joff=joff,
                extra_env=extra_env,
            )
        )
    proc = restamp.run_ssh_script(host, "\n".join(lines) + "\n")
    write_text(stdout_log, proc.stdout)
    write_text(stderr_log, proc.stderr)
    restamp.require_ok(proc, f"{host} retained jitter {'joff' if joff else 'jit-on'}")
    text = restamp.fetch_remote_file(host, remote_json)
    write_text(local_json, text)
    return load_jsonl(text)


def summarize_pass(
    *,
    pass_no: int,
    order: str,
    jit_records: list[dict[str, Any]],
    joff_records: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    joff_by_key = {record_key(record): record for record in joff_records}
    rows: list[dict[str, Any]] = []
    for jit_record in jit_records:
        if jit_record.get("scale") != "hot":
            continue
        key = record_key(jit_record)
        joff_record = joff_by_key.get(key)
        if joff_record is None:
            continue
        jit_median = record_median(jit_record)
        joff_median = record_median(joff_record)
        rows.append(
            {
                "pass": pass_no,
                "order": order,
                "row": key,
                "jit": jit_median,
                "joff": joff_median,
                "ratio": jit_median / joff_median if joff_median else None,
                "delta": jit_median - joff_median,
            }
        )
    return rows


def row_jitter(values: list[float]) -> float:
    if not values:
        return 0.0
    lo = min(values)
    hi = max(values)
    return hi / lo if lo else 0.0


def format_ratio(value: float | None) -> str:
    return "n/a" if value is None else f"{value:.4f}"


def summarize_all(rows: list[dict[str, Any]], *, threshold: float) -> list[dict[str, Any]]:
    by_row: dict[str, list[dict[str, Any]]] = {}
    for row in rows:
        by_row.setdefault(row["row"], []).append(row)
    summary: list[dict[str, Any]] = []
    for name, group in sorted(by_row.items()):
        ratios = [float(row["ratio"]) for row in group if row["ratio"] is not None]
        jit_values = [float(row["jit"]) for row in group]
        joff_values = [float(row["joff"]) for row in group]
        positive = sum(1 for ratio in ratios if ratio >= threshold)
        summary.append(
            {
                "row": name,
                "passes": len(group),
                "positive": positive,
                "median_ratio": statistics.median(ratios) if ratios else 0.0,
                "average_ratio": sum(ratios) / len(ratios) if ratios else 0.0,
                "min_ratio": min(ratios) if ratios else 0.0,
                "max_ratio": max(ratios) if ratios else 0.0,
                "median_jit": statistics.median(jit_values) if jit_values else 0.0,
                "median_joff": statistics.median(joff_values) if joff_values else 0.0,
                "jit_jitter": row_jitter(jit_values),
                "joff_jitter": row_jitter(joff_values),
                "ratios": ratios,
            }
        )
    summary.sort(key=lambda row: (row["median_ratio"], row["positive"]), reverse=True)
    return summary


def write_summary(
    *,
    output_dir: pathlib.Path,
    host: str,
    repo: str,
    families: list[str],
    samples: int,
    warmup: int,
    passes: int,
    threshold: float,
    rows: list[dict[str, Any]],
) -> None:
    aggregate = summarize_all(rows, threshold=threshold)
    lines = [
        "# Retained-Env Jitter Probe",
        "",
        f"- Timestamp: `{dt.datetime.now().astimezone().isoformat()}`",
        f"- Host: `{host}`",
        f"- Repo: `{repo}`",
        f"- Families: `{', '.join(families)}`",
        f"- Samples per process: `{samples}`",
        f"- Warmup runs: `{warmup}`",
        f"- Alternating passes: `{passes}`",
        f"- Red threshold: `{threshold:.4f}`",
        "",
        "## Aggregate Hot Rows",
        "",
        "| row | median ratio | avg ratio | min | max | red passes | jit jitter | joff jitter | ratios |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |",
    ]
    for row in aggregate:
        ratios = ", ".join(f"{ratio:.4f}" for ratio in row["ratios"])
        lines.append(
            f"| `{row['row']}` | `{row['median_ratio']:.4f}` | "
            f"`{row['average_ratio']:.4f}` | `{row['min_ratio']:.4f}` | "
            f"`{row['max_ratio']:.4f}` | `{row['positive']}/{row['passes']}` | "
            f"`{row['jit_jitter']:.4f}` | `{row['joff_jitter']:.4f}` | `{ratios}` |"
        )
    lines.extend(["", "## Pass Rows", ""])
    lines.append("| pass | order | row | jit | joff | ratio | delta |")
    lines.append("| ---: | --- | --- | ---: | ---: | ---: | ---: |")
    for row in rows:
        lines.append(
            f"| `{row['pass']}` | `{row['order']}` | `{row['row']}` | "
            f"`{row['jit']:.6f}` | `{row['joff']:.6f}` | "
            f"`{format_ratio(row['ratio'])}` | `{row['delta']:+.6f}` |"
        )
    write_text(output_dir / "summary.md", "\n".join(lines) + "\n")
    write_text(output_dir / "aggregate.json", json.dumps(aggregate, indent=2, sort_keys=True) + "\n")
    write_text(output_dir / "pass-rows.json", json.dumps(rows, indent=2, sort_keys=True) + "\n")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", choices=restamp.HOST_LABELS, default="kdz")
    parser.add_argument("--repo", help="Override authoritative remote repo path.")
    parser.add_argument("--family", action="append", choices=sorted(BENCH_FILES), default=[])
    parser.add_argument("--output-dir", type=pathlib.Path)
    parser.add_argument("--samples", type=int, default=1)
    parser.add_argument("--warmup", type=int, default=2)
    parser.add_argument("--passes", type=int, default=6)
    parser.add_argument("--pin-core", type=int, default=restamp.DEFAULT_PIN_CORE)
    parser.add_argument("--threshold", type=float, default=1.01)
    parser.add_argument("--skip-sync", action="store_true")
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument(
        "--env",
        action="append",
        default=[],
        metavar="KEY=VALUE",
        help="Extra environment variable to add to the retained baseline env.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    host = args.host
    repo = args.repo or restamp.AUTHORITATIVE_REPOS[host]
    families = args.family or list(BENCH_FILES)
    bench_files = [BENCH_FILES[family] for family in families]
    stamp = dt.datetime.now().strftime("%Y%m%d%H%M%S")
    output_dir = args.output_dir or DEFAULT_OUTPUT_ROOT / f"{host}-retained-jitter-{stamp}"
    raw_dir = output_dir / "raw"
    raw_dir.mkdir(parents=True, exist_ok=True)
    remote_tmp = f"/tmp/{host}-retained-jitter-{stamp}"
    retained_env = dict(restamp.RETAINED_BASELINE_ENV)
    retained_env.update(parse_env_overrides(args.env))

    if not args.skip_sync:
        restamp.sync_tracked_files(host, repo)
    if not args.skip_build:
        restamp.build_remote_repo(host, repo, raw_dir)
    if any(family in ORACLE_BENCH_FAMILIES for family in families):
        build_remote_oracles(host, repo, raw_dir)
    proc = restamp.run_ssh_script(host, f"mkdir -p {shlex.quote(remote_tmp)}")
    restamp.require_ok(proc, f"{host} retained jitter remote tmp")

    pass_rows: list[dict[str, Any]] = []
    try:
        for pass_no in range(1, args.passes + 1):
            order = "jit-first" if pass_no % 2 else "joff-first"
            jit_json = f"{remote_tmp}/pass{pass_no}-jit.jsonl"
            joff_json = f"{remote_tmp}/pass{pass_no}-joff.jsonl"
            if order == "jit-first":
                jit_records = run_mode(
                    host=host,
                    repo=repo,
                    remote_json=jit_json,
                    local_json=output_dir / f"pass{pass_no}-jit.jsonl",
                    stdout_log=raw_dir / f"pass{pass_no}-jit.stdout.log",
                    stderr_log=raw_dir / f"pass{pass_no}-jit.stderr.log",
                    bench_files=bench_files,
                    samples=args.samples,
                    warmup=args.warmup,
                    pin_core=args.pin_core,
                    joff=False,
                    extra_env=retained_env,
                )
                joff_records = run_mode(
                    host=host,
                    repo=repo,
                    remote_json=joff_json,
                    local_json=output_dir / f"pass{pass_no}-joff.jsonl",
                    stdout_log=raw_dir / f"pass{pass_no}-joff.stdout.log",
                    stderr_log=raw_dir / f"pass{pass_no}-joff.stderr.log",
                    bench_files=bench_files,
                    samples=args.samples,
                    warmup=args.warmup,
                    pin_core=args.pin_core,
                    joff=True,
                    extra_env=retained_env,
                )
            else:
                joff_records = run_mode(
                    host=host,
                    repo=repo,
                    remote_json=joff_json,
                    local_json=output_dir / f"pass{pass_no}-joff.jsonl",
                    stdout_log=raw_dir / f"pass{pass_no}-joff.stdout.log",
                    stderr_log=raw_dir / f"pass{pass_no}-joff.stderr.log",
                    bench_files=bench_files,
                    samples=args.samples,
                    warmup=args.warmup,
                    pin_core=args.pin_core,
                    joff=True,
                    extra_env=retained_env,
                )
                jit_records = run_mode(
                    host=host,
                    repo=repo,
                    remote_json=jit_json,
                    local_json=output_dir / f"pass{pass_no}-jit.jsonl",
                    stdout_log=raw_dir / f"pass{pass_no}-jit.stdout.log",
                    stderr_log=raw_dir / f"pass{pass_no}-jit.stderr.log",
                    bench_files=bench_files,
                    samples=args.samples,
                    warmup=args.warmup,
                    pin_core=args.pin_core,
                    joff=False,
                    extra_env=retained_env,
                )
            rows = summarize_pass(
                pass_no=pass_no,
                order=order,
                jit_records=jit_records,
                joff_records=joff_records,
            )
            pass_rows.extend(rows)
            print(f"pass {pass_no} {order}")
            for row in rows:
                print(
                    f"  {row['row']} jit={row['jit']:.6f} "
                    f"joff={row['joff']:.6f} ratio={format_ratio(row['ratio'])} "
                    f"delta={row['delta']:+.6f}"
                )
    finally:
        restamp.run_ssh_script(host, f"rm -rf {shlex.quote(remote_tmp)}")

    write_summary(
        output_dir=output_dir,
        host=host,
        repo=repo,
        families=families,
        samples=args.samples,
        warmup=args.warmup,
        passes=args.passes,
        threshold=args.threshold,
        rows=pass_rows,
    )
    print(f"summary={output_dir / 'summary.md'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
