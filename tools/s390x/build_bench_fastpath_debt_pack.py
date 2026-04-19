#!/usr/bin/env python3
"""Measure dependency on branch-local benchmark-shaped s390x fast paths.

This is now a legacy comparison helper. Current WIP has retired the
`LUAJIT_ENABLE_S390X_BENCH_FASTPATHS` source switch, so the default and
generic-only builds should be equivalent unless a future tranche temporarily
reintroduces compile-time benchmark fast paths. The pack still runs both
profiles and reports any divergence.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import pathlib
import shlex
import statistics
from typing import Any

import probe_retained_jitter as jitter
import restamp_iterator_perf as restamp


DEFAULT_OUTPUT_ROOT = pathlib.Path("/tmp")
DEFAULT_TIMEOUT_SECS = 30
DEFAULT_FAMILIES = [
    "dispatch_trace",
    "iterator_table",
    "mixed_noffi",
    "vararg_paths",
    "mixed_ffi",
    "ffi_cdata",
    "ffi_calls",
    "ffi_fixed_call_pressure",
    "ffi_fixed_struct_calls",
    "be_helpers",
    "be_helpers_localized",
    "large_immediates",
    "logic_add_phi_noboundary",
    "logical_chain_tail_add",
    "logical_chain_tail_store",
    "lower_frame_same_callsite",
    "numeric_ops",
    "promotion_core_static_stop",
    "route_around_reducers",
    "string_heavy",
    "bitops_mix",
]


def write_text(path: pathlib.Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def build_profile(host: str, repo: str, raw_dir: pathlib.Path, *, generic_only: bool) -> None:
    extra = " -DLUAJIT_ENABLE_S390X_BENCH_FASTPATHS=0" if generic_only else ""
    label = "generic-only" if generic_only else "default"
    vars_map = (
        "CC=gcc HOST_CC=gcc BUILDMODE=mixed "
        f"XCFLAGS='-DLUAJIT_ENABLE_S390X_JIT{extra}'"
    )
    script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
make -C src clean {vars_map}
make -C src -j4 {vars_map}
./src/luajit -e 'print(jit.arch, jit.status())'
"""
    proc = restamp.run_ssh_script(host, script)
    write_text(raw_dir / f"{label}-build.stdout.log", proc.stdout)
    write_text(raw_dir / f"{label}-build.stderr.log", proc.stderr)
    restamp.require_ok(proc, f"{host} {label} build")


def build_oracles_if_needed(host: str, repo: str, raw_dir: pathlib.Path,
			    families: list[str], label: str) -> None:
    if not any(family in jitter.ORACLE_BENCH_FAMILIES for family in families):
        return
    script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
CC=gcc sh tests/s390x/build_oracles.sh
test -s tests/s390x/ffi_abi/build/liboracle.so
"""
    proc = restamp.run_ssh_script(host, script)
    write_text(raw_dir / f"{label}-oracle-build.stdout.log", proc.stdout)
    write_text(raw_dir / f"{label}-oracle-build.stderr.log", proc.stderr)
    restamp.require_ok(proc, f"{host} {label} oracle build")


def run_family(
    *,
    host: str,
    repo: str,
    family: str,
    bench_file: str,
    label: str,
    raw_dir: pathlib.Path,
    remote_tmp: str,
    samples: int,
    warmup: int,
    timeout_secs: int,
    pin_core: int | None,
    extra_env: dict[str, str],
) -> dict[str, Any]:
    remote_json = f"{remote_tmp}/{label}-{family}.jsonl"
    taskset = f"taskset -c {pin_core} " if pin_core is not None else ""
    env = {
        **extra_env,
        "S390X_PERF_OUTPUT_JSONL": remote_json,
        "S390X_PERF_WARMUP": str(warmup),
        "S390X_PERF_SAMPLES": str(samples),
        "S390X_PERF_BENCH_FILE": bench_file,
    }
    env_prefix = jitter.remote_env_prefix(env)
    script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
rm -f {shlex.quote(remote_json)}
timeout {timeout_secs}s {env_prefix} {taskset}./src/luajit {shlex.quote(bench_file)}
"""
    proc = restamp.run_ssh_script(host, script)
    write_text(raw_dir / f"{label}-{family}.stdout.log", proc.stdout)
    write_text(raw_dir / f"{label}-{family}.stderr.log", proc.stderr)
    result: dict[str, Any] = {
        "profile": label,
        "family": family,
        "bench_file": bench_file,
        "exit_code": proc.returncode,
        "status": "pass" if proc.returncode == 0 else "fail",
        "records": [],
    }
    if proc.returncode != 0:
        result["failure"] = (proc.stderr or proc.stdout).splitlines()[-20:]
        return result
    text = restamp.fetch_remote_file(host, remote_json)
    write_text(raw_dir / f"{label}-{family}.jsonl", text)
    result["records"] = jitter.load_jsonl(text)
    return result


def record_key(record: dict[str, Any]) -> str:
    return f"{record.get('family', '')}/{record.get('workload', '')}/{record.get('scale', '')}"


def median_runtime(record: dict[str, Any]) -> float:
    return float(record.get("median_runtime_sec", record.get("median_sec")))


def summarize(default_results: list[dict[str, Any]],
	      generic_results: list[dict[str, Any]]) -> list[dict[str, Any]]:
    default_rows: dict[str, list[float]] = {}
    generic_rows: dict[str, list[float]] = {}
    for result, out in ((r, default_rows) for r in default_results):
        if result["status"] != "pass":
            continue
        for record in result["records"]:
            out.setdefault(record_key(record), []).append(median_runtime(record))
    for result, out in ((r, generic_rows) for r in generic_results):
        if result["status"] != "pass":
            continue
        for record in result["records"]:
            out.setdefault(record_key(record), []).append(median_runtime(record))

    rows: list[dict[str, Any]] = []
    all_keys = sorted(set(default_rows) | set(generic_rows))
    for key in all_keys:
        dvals = default_rows.get(key, [])
        gvals = generic_rows.get(key, [])
        if not dvals or not gvals:
            rows.append({
                "row": key,
                "status": "missing-default" if not dvals else "missing-generic",
            })
            continue
        d = statistics.median(dvals)
        g = statistics.median(gvals)
        rows.append({
            "row": key,
            "status": "compared",
            "default_median": d,
            "generic_median": g,
            "delta": g - d,
            "ratio_generic_vs_default": g / d if d else None,
        })
    rows.sort(key=lambda row: (
        row.get("status") != "compared",
        -(row.get("delta") or 0),
        -(row.get("ratio_generic_vs_default") or 0),
    ))
    return rows


def write_summary(output_dir: pathlib.Path, payload: dict[str, Any]) -> None:
    rows = payload["rows"]
    failures = payload["failures"]
    write_text(output_dir / "results.json", json.dumps(payload, indent=2, sort_keys=True) + "\n")
    lines = [
        "# s390x Benchmark Fastpath Debt Pack",
        "",
        f"- Host: `{payload['host']}`",
        f"- Repo: `{payload['repo']}`",
        f"- Samples: `{payload['samples']}`",
        f"- Warmup: `{payload['warmup']}`",
        f"- Timeout: `{payload['timeout_secs']}s`",
        "",
        "## Failed Or Timed Out Families",
        "",
    ]
    if failures:
        lines.append("| Profile | Family | Exit |")
        lines.append("|---|---:|---:|")
        for failure in failures:
            lines.append(
                f"| `{failure['profile']}` | `{failure['family']}` | `{failure['exit_code']}` |"
            )
    else:
        lines.append("None.")
    lines.extend([
        "",
        "## Largest Generic-Only Slowdowns",
        "",
        "| Row | Default | Generic-only | Ratio | Delta |",
        "|---|---:|---:|---:|---:|",
    ])
    for row in rows[:80]:
        if row["status"] != "compared":
            lines.append(f"| `{row['row']}` | n/a | n/a | `{row['status']}` | n/a |")
            continue
        ratio = row["ratio_generic_vs_default"]
        ratio_text = "n/a" if ratio is None else f"{ratio:.3f}"
        lines.append(
            f"| `{row['row']}` | `{row['default_median']:.6f}` | "
            f"`{row['generic_median']:.6f}` | "
            f"`{ratio_text}` | `{row['delta']:+.6f}` |"
        )
    write_text(output_dir / "summary.md", "\n".join(lines) + "\n")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", choices=restamp.HOST_LABELS, default="kdz1")
    parser.add_argument("--repo")
    parser.add_argument("--family", action="append", choices=sorted(jitter.BENCH_FILES), default=[])
    parser.add_argument("--output-dir", type=pathlib.Path)
    parser.add_argument("--samples", type=int, default=5)
    parser.add_argument("--warmup", type=int, default=2)
    parser.add_argument("--timeout-secs", type=int, default=DEFAULT_TIMEOUT_SECS)
    parser.add_argument("--pin-core", type=int, default=restamp.DEFAULT_PIN_CORE)
    parser.add_argument("--skip-sync", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    host = args.host
    repo = args.repo or restamp.AUTHORITATIVE_REPOS[host]
    families = args.family or DEFAULT_FAMILIES
    stamp = dt.datetime.now().strftime("%Y%m%d%H%M%S")
    output_dir = args.output_dir or DEFAULT_OUTPUT_ROOT / f"{host}-bench-fastpath-debt-{stamp}"
    raw_dir = output_dir / "raw"
    raw_dir.mkdir(parents=True, exist_ok=True)
    remote_tmp = f"/tmp/{host}-bench-fastpath-debt-{stamp}-{os.getpid()}"
    retained_env = dict(restamp.RETAINED_BASELINE_ENV)

    if not args.skip_sync:
        restamp.sync_tracked_files(host, repo)
    proc = restamp.run_ssh_script(host, f"mkdir -p {shlex.quote(remote_tmp)}")
    restamp.require_ok(proc, f"{host} debt remote tmp")

    default_results: list[dict[str, Any]] = []
    generic_results: list[dict[str, Any]] = []
    try:
        build_profile(host, repo, raw_dir, generic_only=False)
        build_oracles_if_needed(host, repo, raw_dir, families, "default")
        for family in families:
            default_results.append(run_family(
                host=host,
                repo=repo,
                family=family,
                bench_file=jitter.BENCH_FILES[family],
                label="default",
                raw_dir=raw_dir,
                remote_tmp=remote_tmp,
                samples=args.samples,
                warmup=args.warmup,
                timeout_secs=args.timeout_secs,
                pin_core=args.pin_core,
                extra_env=retained_env,
            ))

        build_profile(host, repo, raw_dir, generic_only=True)
        build_oracles_if_needed(host, repo, raw_dir, families, "generic-only")
        for family in families:
            generic_results.append(run_family(
                host=host,
                repo=repo,
                family=family,
                bench_file=jitter.BENCH_FILES[family],
                label="generic-only",
                raw_dir=raw_dir,
                remote_tmp=remote_tmp,
                samples=args.samples,
                warmup=args.warmup,
                timeout_secs=args.timeout_secs,
                pin_core=args.pin_core,
                extra_env=retained_env,
            ))
    finally:
        restamp.run_ssh_script(host, f"rm -rf {shlex.quote(remote_tmp)}")

    failures = [
        result for result in [*default_results, *generic_results]
        if result["status"] != "pass"
    ]
    payload = {
        "host": host,
        "repo": repo,
        "families": families,
        "samples": args.samples,
        "warmup": args.warmup,
        "timeout_secs": args.timeout_secs,
        "default_results": default_results,
        "generic_results": generic_results,
        "failures": failures,
        "rows": summarize(default_results, generic_results),
    }
    write_summary(output_dir, payload)
    print(f"summary={output_dir / 'summary.md'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
