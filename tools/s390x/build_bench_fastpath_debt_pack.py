#!/usr/bin/env python3
"""Measure dependency on branch-local s390x semantic reducer fast paths.

Current WIP keeps semantic reducer substitutions enabled by default for the
bring-up performance baseline. The comparison profiles rebuild with selected
reducer classes disabled while preserving the rest of the backend/runtime
source. Use this pack to measure how much performance each family still gets
from branch-local reducer substitution before replacing it with upstreamable
lower-level mechanisms.
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
DEFAULT_PROFILES = ["default", "generic-only"]
PROFILE_XCFLAGS = {
    "default": "",
    "generic-only": " -DLUAJIT_ENABLE_S390X_SEMANTIC_REDUCERS=0",
    "mixed-noffi-off": " -DLUAJIT_ENABLE_S390X_MIXED_NOFFI_REDUCERS=0",
    "string-cycle-off": " -DLUAJIT_ENABLE_S390X_STRING_CYCLE_REDUCERS=0",
    "string-key-lookup-off": " -DLUAJIT_ENABLE_S390X_STRING_KEY_LOOKUP_REDUCER=0",
    "string-concat-slice-off": " -DLUAJIT_ENABLE_S390X_STRING_CONCAT_SLICE_REDUCER=0",
    "string-miss-find-off": " -DLUAJIT_ENABLE_S390X_STRING_MISS_FIND_REDUCER=0",
    "string-prefix-eq-off": " -DLUAJIT_ENABLE_S390X_STRING_PREFIX_EQ_REDUCER=0",
    "string-manual-find-cycle-off": " -DLUAJIT_ENABLE_S390X_STRING_MANUAL_FIND_CYCLE_REDUCER=0",
    "string-byte-scan-cycle-off": " -DLUAJIT_ENABLE_S390X_STRING_BYTE_SCAN_CYCLE_REDUCER=0",
}
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


def build_profile(host: str, repo: str, raw_dir: pathlib.Path, *, profile: str) -> None:
    extra = PROFILE_XCFLAGS[profile]
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
    write_text(raw_dir / f"{profile}-build.stdout.log", proc.stdout)
    write_text(raw_dir / f"{profile}-build.stderr.log", proc.stderr)
    restamp.require_ok(proc, f"{host} {profile} build")


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


def summarize_profile(default_results: list[dict[str, Any]],
		      comparison_results: list[dict[str, Any]],
		      comparison_profile: str) -> list[dict[str, Any]]:
    default_rows: dict[str, list[float]] = {}
    comparison_rows: dict[str, list[float]] = {}
    for result, out in ((r, default_rows) for r in default_results):
        if result["status"] != "pass":
            continue
        for record in result["records"]:
            out.setdefault(record_key(record), []).append(median_runtime(record))
    for result, out in ((r, comparison_rows) for r in comparison_results):
        if result["status"] != "pass":
            continue
        for record in result["records"]:
            out.setdefault(record_key(record), []).append(median_runtime(record))

    rows: list[dict[str, Any]] = []
    all_keys = sorted(set(default_rows) | set(comparison_rows))
    for key in all_keys:
        dvals = default_rows.get(key, [])
        cvals = comparison_rows.get(key, [])
        if not dvals or not cvals:
            rows.append({
                "row": key,
                "comparison_profile": comparison_profile,
                "status": "missing-default" if not dvals else f"missing-{comparison_profile}",
            })
            continue
        d = statistics.median(dvals)
        c = statistics.median(cvals)
        rows.append({
            "row": key,
            "comparison_profile": comparison_profile,
            "status": "compared",
            "default_median": d,
            "comparison_median": c,
            "delta": c - d,
            "ratio_vs_default": c / d if d else None,
        })
    rows.sort(key=lambda row: (
        row.get("status") != "compared",
        -(row.get("delta") or 0),
        -(row.get("ratio_vs_default") or 0),
    ))
    return rows


def summarize(default_results: list[dict[str, Any]],
	      profile_results: dict[str, list[dict[str, Any]]]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for profile, results in profile_results.items():
        if profile == "default":
            continue
        rows.extend(summarize_profile(default_results, results, profile))
    rows.sort(key=lambda row: (
        row.get("status") != "compared",
        -(row.get("delta") or 0),
        -(row.get("ratio_vs_default") or 0),
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
        f"- Profiles: `{', '.join(payload['profiles'])}`",
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
        "## Largest Profile Slowdowns Versus Default",
        "",
        "| Row | Profile | Default | Profile median | Ratio | Delta |",
        "|---|---|---:|---:|---:|---:|",
    ])
    for row in rows[:80]:
        if row["status"] != "compared":
            lines.append(
                f"| `{row['row']}` | `{row['comparison_profile']}` | "
                f"n/a | n/a | `{row['status']}` | n/a |"
            )
            continue
        ratio = row["ratio_vs_default"]
        ratio_text = "n/a" if ratio is None else f"{ratio:.3f}"
        lines.append(
            f"| `{row['row']}` | `{row['comparison_profile']}` | "
            f"`{row['default_median']:.6f}` | "
            f"`{row['comparison_median']:.6f}` | "
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
    parser.add_argument("--profile", action="append", choices=sorted(PROFILE_XCFLAGS), default=[])
    parser.add_argument("--skip-sync", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    host = args.host
    repo = args.repo or restamp.AUTHORITATIVE_REPOS[host]
    families = args.family or DEFAULT_FAMILIES
    profiles = args.profile or DEFAULT_PROFILES
    profiles = list(dict.fromkeys(["default", *profiles]))
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

    profile_results: dict[str, list[dict[str, Any]]] = {}
    try:
        for profile in profiles:
            results: list[dict[str, Any]] = []
            build_profile(host, repo, raw_dir, profile=profile)
            build_oracles_if_needed(host, repo, raw_dir, families, profile)
            for family in families:
                results.append(run_family(
                    host=host,
                    repo=repo,
                    family=family,
                    bench_file=jitter.BENCH_FILES[family],
                    label=profile,
                    raw_dir=raw_dir,
                    remote_tmp=remote_tmp,
                    samples=args.samples,
                    warmup=args.warmup,
                    timeout_secs=args.timeout_secs,
                    pin_core=args.pin_core,
                    extra_env=retained_env,
                ))
            profile_results[profile] = results
    finally:
        restamp.run_ssh_script(host, f"rm -rf {shlex.quote(remote_tmp)}")

    failures = [
        result for results in profile_results.values() for result in results
        if result["status"] != "pass"
    ]
    default_results = profile_results.get("default", [])
    generic_results = profile_results.get("generic-only", [])
    payload = {
        "host": host,
        "repo": repo,
        "families": families,
        "profiles": profiles,
        "samples": args.samples,
        "warmup": args.warmup,
        "timeout_secs": args.timeout_secs,
        "profile_results": profile_results,
        "default_results": default_results,
        "generic_results": generic_results,
        "failures": failures,
        "rows": summarize(default_results, profile_results),
    }
    write_summary(output_dir, payload)
    print(f"summary={output_dir / 'summary.md'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
