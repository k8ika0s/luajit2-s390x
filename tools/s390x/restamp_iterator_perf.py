#!/usr/bin/env python3
"""Restamp the frozen s390x iterator perf baseline on a native host.

This helper syncs tracked files from the current local workspace into one clean
native repo, rebuilds there, runs the authoritative iterator benchmark and the
focused iterator micros, and writes a stable local artifact bundle.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import pathlib
import posixpath
import re
import shlex
import subprocess
import sys
import textwrap
from typing import Any


ROOT = pathlib.Path(__file__).resolve().parents[2]
BENCH_FILE = "tests/s390x/perf/iterator_table.lua"
HOST_LABELS = ("kdz", "zkd0")
DEFAULT_SAMPLES = 9
DEFAULT_WARMUP = 2
DEFAULT_PIN_CORE = 0
REMOTE_ROOT = "/root/luajit2-s390x"
AUTHORITATIVE_REPOS = {
    "kdz": f"{REMOTE_ROOT}/canon/repo",
    "zkd0": f"{REMOTE_ROOT}/canon/repo",
}
AUTHORITATIVE_RUNS = {
    "kdz": f"{REMOTE_ROOT}/runs",
    "zkd0": f"{REMOTE_ROOT}/runs",
}
AUTHORITATIVE_ARCHIVES = {
    "kdz": f"{REMOTE_ROOT}/archive",
    "zkd0": f"{REMOTE_ROOT}/archive",
}
AUTHORITATIVE_HASH_PATHS = [
    "src/vm_s390x.dasc",
    "tools/s390x/sync_remote_mirror.py",
    "tools/s390x/restamp_iterator_perf.py",
    "tools/s390x/build_iterator_truth_pack.py",
    "docs/s390x/findings.md",
]
RETAINED_BASELINE_ENV: dict[str, str] = {
    "LUAJIT_S390X_DISPATCH_FORL_SKIP_JFORI": "1",
    "LUAJIT_S390X_DISPATCH_FORL_PARK_ROOT_HOTEXIT_EXACT_COOLDOWN": "12",
    "LUAJIT_S390X_AREF_BASE_ALLGPR": "1",
    "LUAJIT_S390X_IPAIRS_EXIT1_SKIP_BODY": "1",
    "LUAJIT_S390X_ROOT1_ITERL_REPLAY_TRIPLET": "1",
    "LUAJIT_S390X_ROOT1_ITERL_REPLAY_TRIPLET_LINK_PARENT": "1",
    "LUAJIT_S390X_SUM_LOOP_SELECT_EXIT0_DONE": "1",
    "LUAJIT_S390X_SUM_LOOP_SELECT_SKIP_FUNC_EQ": "1",
    "LUAJIT_S390X_SUM_LOOP_SELECT_CONST_GGET": "1",
    "LUAJIT_S390X_SUM_LOOP_FORL_BLACKLIST": "1",
    "LUAJIT_S390X_VARARG_SIBLING_FORL_BLACKLIST": "1",
    "LUAJIT_S390X_MIXED_FFI_POST_STITCH_SAVE_DONE": "1",
    "LUAJIT_S390X_MIXED_FFI_FORL_PROTO_NOJIT": "1",
    "LUAJIT_S390X_FFI_CDATA_PAIR_SAVE_DONE": "1",
    "LUAJIT_S390X_ITERATOR_ITERN_BLACKLIST": "1",
    "LUAJIT_S390X_ITERATOR_ITERL_BLACKLIST": "1",
    "LUAJIT_S390X_ITERATOR_ITERN_PROTO_NOJIT": "1",
    "LUAJIT_S390X_ITERATOR_ARRAY_ITERN_NOJIT_HOTCOUNT_PARK": "1",
    "LUAJIT_S390X_ITERATOR_HASH_ITERN_NOJIT_HOTCOUNT_PARK": "1",
    "LUAJIT_S390X_ITERATOR_POST_PROTO_ITERN_NOHOT": "1",
    "LUAJIT_S390X_MIXED_NOFFI_ITERL_BLACKLIST": "1",
    "LUAJIT_S390X_MIXED_NOFFI_ITERN_BLACKLIST": "1",
    "LUAJIT_S390X_MIXED_NOFFI_FORL_STITCH_BLACKLIST": "1",
    "LUAJIT_S390X_MIXED_NOFFI_ITERL_ABORT_BLACKLIST": "1",
    "LUAJIT_S390X_MIXED_NOFFI_EARLY_PROTO_NOJIT": "1",
    "LUAJIT_S390X_LOCALIZED_HOTSIDE_CANON_SHARE_EQUIV": "1",
    "LUAJIT_S390X_LOWER_FRAME_LUA_ABS_PROTO_NOJIT": "1",
    "LUAJIT_S390X_PROMOTION_CORE_FORL_PROTO_NOJIT": "1",
}
CANDIDATE_ENVS: dict[str, dict[str, str]] = {
    "retained_baseline": RETAINED_BASELINE_ENV,
    "raw_jit": {},
}
KNOWN_MACHINE_TYPES = {
    "8561": "z15",
    "3906": "z14",
}
FROZEN_BASELINES = {
    "kdz": {
        "jit_on": {
            "pairs_sum/hot": 0.059818,
            "pairs_array_sum/hot": 0.061622,
        },
        "joff": {
            "pairs_sum/hot": 0.004267,
            "pairs_array_sum/hot": 0.004011,
        },
    },
    "zkd0": {
        "jit_on": {
            "pairs_sum/hot": 0.093881,
            "pairs_array_sum/hot": 0.087945,
        },
    },
}
RECOVERY_BAR = {
    "pairs_sum/hot": 0.056341,
    "pairs_array_sum/hot": 0.059806,
}
FIRST_RESULTS_BAR = {
    "pairs_sum/hot": 0.050000,
    "pairs_array_sum/hot": 0.055000,
}

ONESHOT_SCRIPT = """\
local n = assert(tonumber(arg[1]), "missing iteration count")
local t = { 1, 3, 5, 7, 9 }
local function run(iterations)
  local total = 0
  for _ = 1, iterations do
    for _, value in pairs(t) do
      total = total + value
    end
  end
  return total
end
print("RESULT", run(n))
"""

MICRO_SCRIPTS = {
    "hash_value": """\
jit.opt.start("hotloop=1")
local t = { a = 10, b = 20, c = 30, d = 40, e = 50 }
local function run(n)
  local total = 0
  for _ = 1, n do
    for _, value in pairs(t) do
      total = total + value
    end
  end
  return total
end
print("HASH_VALUE", run(20))
""",
    "hash_key": """\
jit.opt.start("hotloop=1")
local t = { aa = 10, bb = 20, cc = 30 }
local function run(n)
  local total = 0
  for _ = 1, n do
    for key, value in pairs(t) do
      total = total + value + #key
    end
  end
  return total
end
print("HASH_KEY", run(20))
""",
    "array_value": """\
jit.opt.start("hotloop=1")
local t = { 10, 20, 30, 40, 50 }
local function run(n)
  local total = 0
  for _ = 1, n do
    for _, value in pairs(t) do
      total = total + value
    end
  end
  return total
end
print("ARRAY_VALUE", run(20))
""",
}
EXPECTED_MICRO_OUTPUTS = {
    "hash_value": "HASH_VALUE 3000",
    "hash_key": "HASH_KEY 1320",
    "array_value": "ARRAY_VALUE 3000",
}
EXPECTED_ONESHOT_OUTPUTS = {
    "20": "RESULT 500",
    "2000": "RESULT 50000",
    "200000": "RESULT 5000000",
}


class RestampError(RuntimeError):
    """Restamp helper failure."""


def shell_join(parts: list[str]) -> str:
    return " ".join(shlex.quote(part) for part in parts)


def write_text(path: pathlib.Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def write_json(path: pathlib.Path, payload: Any) -> None:
    write_text(path, json.dumps(payload, indent=2, sort_keys=True) + "\n")


def run_local(argv: list[str], *, cwd: pathlib.Path | None = None, input_bytes: bytes | None = None) -> subprocess.CompletedProcess[bytes]:
    proc = subprocess.run(argv, cwd=str(cwd or ROOT), input=input_bytes, capture_output=True)
    return proc


def run_local_shell(command: str, *, input_bytes: bytes | None = None) -> subprocess.CompletedProcess[bytes]:
    return subprocess.run(
        ["/bin/bash", "-lc", command],
        cwd=str(ROOT),
        input=input_bytes,
        capture_output=True,
    )


def run_ssh_script(host: str, script: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["ssh", "-o", "BatchMode=yes", host, "bash -s"],
        cwd=str(ROOT),
        input=script,
        text=True,
        capture_output=True,
    )


def require_ok(proc: subprocess.CompletedProcess[Any], label: str) -> None:
    if proc.returncode != 0:
        raise RestampError(f"{label} failed with exit code {proc.returncode}")


def current_commit() -> str:
    proc = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=str(ROOT),
        text=True,
        capture_output=True,
        check=False,
    )
    require_ok(proc, "git rev-parse HEAD")
    return proc.stdout.strip()


def remote_layout(remote_repo: str) -> dict[str, str]:
    canon_dir = posixpath.dirname(remote_repo.rstrip("/"))
    root_dir = posixpath.dirname(canon_dir)
    return {
        "root": root_dir,
        "canon": canon_dir,
        "repo": remote_repo,
        "runs": posixpath.join(root_dir, "runs"),
        "archive": posixpath.join(root_dir, "archive"),
    }


def ensure_remote_layout(host: str, remote_repo: str) -> dict[str, str]:
    layout = remote_layout(remote_repo)
    script = f"""
set -euo pipefail
mkdir -p {shlex.quote(layout["canon"])}
mkdir -p {shlex.quote(layout["runs"])}
mkdir -p {shlex.quote(layout["archive"])}
"""
    proc = run_ssh_script(host, script)
    require_ok(proc, f"{host} remote layout")
    return layout


def verify_remote_paths(host: str, remote_repo: str, relative_paths: list[str]) -> dict[str, bool]:
    if not relative_paths:
        return {}
    checks = [
        "set -euo pipefail",
        f"cd {shlex.quote(remote_repo)}",
    ]
    for relpath in relative_paths:
        checks.append(
            f"if [ -e {shlex.quote(relpath)} ]; then printf '%s\\tOK\\n' {shlex.quote(relpath)}; "
            f"else printf '%s\\tMISSING\\n' {shlex.quote(relpath)}; fi"
        )
    proc = run_ssh_script(host, "\n".join(checks) + "\n")
    require_ok(proc, f"{host} remote verify")
    results: dict[str, bool] = {}
    for line in proc.stdout.splitlines():
        relpath, _, status = line.partition("\t")
        if relpath:
            results[relpath] = (status.strip() == "OK")
    return results


def remote_file_hashes(host: str, remote_repo: str, relative_paths: list[str]) -> dict[str, str | None]:
    if not relative_paths:
        return {}
    checks = [
        "set -euo pipefail",
        f"cd {shlex.quote(remote_repo)}",
    ]
    for relpath in relative_paths:
        checks.append(
            "if [ -e {path} ]; then "
            "printf '%s\\t%s\\n' {path} \"$(sha256sum {path} | awk '{{print $1}}')\"; "
            "else printf '%s\\tMISSING\\n' {path}; fi".format(path=shlex.quote(relpath))
        )
    proc = run_ssh_script(host, "\n".join(checks) + "\n")
    require_ok(proc, f"{host} remote hashes")
    results: dict[str, str | None] = {}
    for line in proc.stdout.splitlines():
        relpath, _, digest = line.partition("\t")
        if relpath:
            value = digest.strip()
            results[relpath] = None if value == "MISSING" else value
    return results


def sync_tracked_files(host: str, remote_repo: str) -> None:
    ensure_remote_layout(host, remote_repo)
    prep_script = f"""
set -euo pipefail
mkdir -p {shlex.quote(remote_repo)}
rm -rf {shlex.quote(remote_repo)}/* {shlex.quote(remote_repo)}/.[!.]* {shlex.quote(remote_repo)}/..?* 2>/dev/null || true
"""
    prep = run_ssh_script(host, prep_script)
    require_ok(prep, f"{host} repo prep")

    tracked = run_local(["git", "ls-files", "-z"])
    require_ok(tracked, "git ls-files -z")

    tar_parts = [
        "tar",
        "--disable-copyfile",
        "--no-mac-metadata",
        "--no-xattrs",
        "--no-acls",
        "--no-fflags",
        "-C",
        str(ROOT),
        "--null",
        "-T",
        "-",
        "-cf",
        "-",
    ]
    remote_cmd = f"tar -xf - -C {shlex.quote(remote_repo)}"
    ssh_cmd = f"ssh -o BatchMode=yes {shlex.quote(host)} {shlex.quote(f'bash -lc {shlex.quote(remote_cmd)}')}"
    pipeline = (
        f"COPYFILE_DISABLE=1 COPY_EXTENDED_ATTRIBUTES_DISABLE=1 "
        f"{shell_join(tar_parts)} | {ssh_cmd}"
    )
    proc = run_local_shell(pipeline, input_bytes=tracked.stdout)
    if proc.returncode == 0:
        return

    retry = run_local_shell(pipeline, input_bytes=tracked.stdout)
    require_ok(retry, f"{host} tracked-file sync")


def collect_host_info(host: str) -> dict[str, str]:
    script = """
set -euo pipefail
hostname_short=$(hostname)
hostname_fqdn=$(hostname -f 2>/dev/null || hostname)
machine_type=$(awk -F: '/^Type:/{gsub(/^[ \t]+/, "", $2); print $2; exit}' /proc/sysinfo)
model=$(awk -F: '/^Model:/{gsub(/^[ \t]+/, "", $2); print $2; exit}' /proc/sysinfo)
os_name=$(awk -F= '/^PRETTY_NAME=/{gsub(/^"/, "", $2); gsub(/"$/, "", $2); print $2; exit}' /etc/os-release 2>/dev/null || true)
printf 'HOSTNAME_SHORT=%s\n' "$hostname_short"
printf 'HOSTNAME_FQDN=%s\n' "$hostname_fqdn"
printf 'UNAME=%s\n' "$(uname -a)"
printf 'MACHINE_TYPE=%s\n' "$machine_type"
printf 'MODEL=%s\n' "$model"
printf 'OS_PRETTY_NAME=%s\n' "$os_name"
"""
    proc = run_ssh_script(host, script)
    require_ok(proc, f"{host} host metadata")
    info: dict[str, str] = {}
    for raw_line in proc.stdout.splitlines():
        line = raw_line.strip()
        if not line or "=" not in line:
            continue
        key, value = line.split("=", 1)
        info[key] = value.strip()
    machine_type = info.get("MACHINE_TYPE", "")
    info["GENERATION"] = KNOWN_MACHINE_TYPES.get(machine_type, "unknown")
    return info


def prepare_remote_scripts(host: str) -> str:
    parts = [
        "set -euo pipefail",
        'tmpdir=$(mktemp -d /tmp/lj-restamp-iterator.XXXXXX)',
        'cat >"$tmpdir/oneshot_iter.lua" <<\'EOF\'',
        ONESHOT_SCRIPT.rstrip(),
        "EOF",
    ]
    for name, content in MICRO_SCRIPTS.items():
        parts.extend(
            [
                f'cat >"$tmpdir/{name}.lua" <<\'EOF\'',
                content.rstrip(),
                "EOF",
            ]
        )
    parts.append('printf "%s\\n" "$tmpdir"')
    proc = run_ssh_script(host, "\n".join(parts) + "\n")
    require_ok(proc, f"{host} probe setup")
    lines = [line.strip() for line in proc.stdout.splitlines() if line.strip()]
    if not lines:
        raise RestampError(f"{host} probe setup returned no scratch directory")
    return lines[-1]


def cleanup_remote_scripts(host: str, remote_tmp: str) -> None:
    script = f"""
set -euo pipefail
rm -rf {shlex.quote(remote_tmp)}
"""
    run_ssh_script(host, script)


def remote_env_prefix(*, jsonl_path: str | None, samples: int, warmup: int) -> str:
    env = []
    if jsonl_path:
        env.append(f"S390X_PERF_OUTPUT_JSONL={shlex.quote(jsonl_path)}")
    env.extend(
        [
            f"S390X_PERF_WARMUP={warmup}",
            f"S390X_PERF_SAMPLES={samples}",
            f"S390X_PERF_BENCH_FILE={shlex.quote(BENCH_FILE)}",
        ]
    )
    return "env " + " ".join(env)


def remote_luajit_cmd(*, repo: str, pin_core: int | None, args: list[str]) -> str:
    taskset = f"taskset -c {pin_core} " if pin_core is not None else ""
    return f"cd {shlex.quote(repo)} && export LUA_PATH='./src/?.lua;./src/jit/?.lua;;' && {taskset}./src/luajit {' '.join(shlex.quote(part) for part in args)}"


def run_remote_command(host: str, script: str, *, stdout_path: pathlib.Path, stderr_path: pathlib.Path, label: str) -> subprocess.CompletedProcess[str]:
    proc = run_ssh_script(host, script)
    write_text(stdout_path, proc.stdout)
    write_text(stderr_path, proc.stderr)
    require_ok(proc, label)
    return proc


def remote_extra_env_prefix(extra_env: dict[str, str] | None = None) -> str:
    if not extra_env:
        return ""
    parts = [f"{key}={shlex.quote(value)}" for key, value in sorted(extra_env.items())]
    return "env " + " ".join(parts) + " "


def fetch_remote_file(host: str, remote_path: str) -> str:
    proc = run_ssh_script(
        host,
        f"""
set -euo pipefail
cat {shlex.quote(remote_path)}
""",
    )
    require_ok(proc, f"{host} fetch {remote_path}")
    return proc.stdout


def build_remote_repo(host: str, repo: str, raw_dir: pathlib.Path) -> None:
    src_dir = shlex.quote(f"{repo}/src")
    clean_script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
make -C {src_dir} clean
make -C {src_dir} -j4
"""
    proc = run_ssh_script(host, clean_script)
    write_text(raw_dir / "build.stdout.log", proc.stdout)
    write_text(raw_dir / "build.stderr.log", proc.stderr)
    if proc.returncode == 0:
        return

    retry_script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
make -C {src_dir} -j4
"""
    retry = run_ssh_script(host, retry_script)
    retry_stdout = (
        proc.stdout
        + "\n=== RETRY make -C src -j4 ===\n"
        + retry.stdout
    )
    retry_stderr = (
        proc.stderr
        + "\n=== RETRY make -C src -j4 ===\n"
        + retry.stderr
    )
    write_text(raw_dir / "build.stdout.log", retry_stdout)
    write_text(raw_dir / "build.stderr.log", retry_stderr)
    if retry.returncode == 0:
        return

    retry_serial_script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
make -C src -j1
"""
    retry_serial = run_ssh_script(host, retry_serial_script)
    serial_stdout = (
        retry_stdout
        + "\n=== RETRY make -C src -j1 ===\n"
        + retry_serial.stdout
    )
    serial_stderr = (
        retry_stderr
        + "\n=== RETRY make -C src -j1 ===\n"
        + retry_serial.stderr
    )
    write_text(raw_dir / "build.stdout.log", serial_stdout)
    write_text(raw_dir / "build.stderr.log", serial_stderr)
    require_ok(retry_serial, f"{host} clean build")


def run_jit_status(host: str, repo: str, raw_dir: pathlib.Path) -> str:
    extra_env_prefix = remote_extra_env_prefix(None)
    script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
{extra_env_prefix}./src/luajit -e 'local a,b,c=jit.status(); print(a,b,c)'
"""
    proc = run_remote_command(
        host,
        script,
        stdout_path=raw_dir / "jit-status.stdout.log",
        stderr_path=raw_dir / "jit-status.stderr.log",
        label=f"{host} jit.status()",
    )
    return proc.stdout.strip()


def run_oneshot_checks(host: str, repo: str, remote_tmp: str, raw_dir: pathlib.Path,
                       extra_env: dict[str, str] | None = None) -> dict[str, str]:
    results: dict[str, str] = {}
    extra_env_prefix = remote_extra_env_prefix(extra_env)
    for count in ("20", "2000", "200000"):
        script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
{extra_env_prefix}./src/luajit {shlex.quote(f"{remote_tmp}/oneshot_iter.lua")} {shlex.quote(count)}
"""
        proc = run_remote_command(
            host,
            script,
            stdout_path=raw_dir / f"oneshot-{count}.stdout.log",
            stderr_path=raw_dir / f"oneshot-{count}.stderr.log",
            label=f"{host} oneshot {count}",
        )
        results[count] = proc.stdout.strip()
    script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
{extra_env_prefix}./src/luajit -joff {shlex.quote(f"{remote_tmp}/oneshot_iter.lua")} 200000
"""
    proc = run_remote_command(
        host,
        script,
        stdout_path=raw_dir / "oneshot-joff-200000.stdout.log",
        stderr_path=raw_dir / "oneshot-joff-200000.stderr.log",
        label=f"{host} oneshot -joff 200000",
    )
    results["joff_200000"] = proc.stdout.strip()
    return results


def run_iterator_bench(
    *,
    host: str,
    repo: str,
    remote_tmp: str,
    raw_dir: pathlib.Path,
    mode_label: str,
    pin_core: int | None,
    samples: int,
    warmup: int,
    joff: bool,
    extra_env: dict[str, str] | None = None,
) -> list[dict[str, Any]]:
    json_name = "joff.jsonl" if joff else "jit-on.jsonl"
    remote_json = f"{remote_tmp}/{json_name}"
    luajit_args = ["-joff", BENCH_FILE] if joff else [BENCH_FILE]
    extra_env_prefix = remote_extra_env_prefix(extra_env)
    script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
rm -f {shlex.quote(remote_json)}
{remote_env_prefix(jsonl_path=remote_json, samples=samples, warmup=warmup)} {extra_env_prefix}{f"taskset -c {pin_core} " if pin_core is not None else ""}./src/luajit {' '.join(shlex.quote(arg) for arg in luajit_args)}
"""
    run_remote_command(
        host,
        script,
        stdout_path=raw_dir / f"{mode_label}.stdout.log",
        stderr_path=raw_dir / f"{mode_label}.stderr.log",
        label=f"{host} iterator_table {mode_label}",
    )
    json_text = fetch_remote_file(host, remote_json)
    local_json = raw_dir.parent / json_name
    write_text(local_json, json_text)
    return parse_jsonl_records(local_json)


def run_micro(host: str, repo: str, remote_tmp: str, raw_dir: pathlib.Path, name: str,
              extra_env: dict[str, str] | None = None) -> str:
    extra_env_prefix = remote_extra_env_prefix(extra_env)
    script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
{extra_env_prefix}./src/luajit {shlex.quote(f"{remote_tmp}/{name}.lua")}
"""
    proc = run_remote_command(
        host,
        script,
        stdout_path=raw_dir / f"{name}.stdout.log",
        stderr_path=raw_dir / f"{name}.stderr.log",
        label=f"{host} micro {name}",
    )
    return proc.stdout.strip()


def run_owner_logs(host: str, repo: str, remote_tmp: str, raw_dir: pathlib.Path, name: str,
                   extra_env: dict[str, str] | None = None) -> None:
    extra_env_prefix = remote_extra_env_prefix(extra_env)
    script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
env LUAJIT_S390X_ADD_LOG=1 LUAJIT_S390X_SLOAD_LOG=1 {extra_env_prefix}./src/luajit {shlex.quote(f"{remote_tmp}/{name}.lua")}
"""
    run_remote_command(
        host,
        script,
        stdout_path=raw_dir / f"{name}.stdout.log",
        stderr_path=raw_dir / f"{name}.stderr.log",
        label=f"{host} owner log {name}",
    )


def run_ir_dump(host: str, repo: str, remote_tmp: str, raw_dir: pathlib.Path, name: str,
                extra_env: dict[str, str] | None = None) -> None:
    extra_env_prefix = remote_extra_env_prefix(extra_env)
    script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
{extra_env_prefix}./src/luajit -jdump=ir {shlex.quote(f"{remote_tmp}/{name}.lua")}
"""
    run_remote_command(
        host,
        script,
        stdout_path=raw_dir / f"{name}.stdout.log",
        stderr_path=raw_dir / f"{name}.stderr.log",
        label=f"{host} ir dump {name}",
    )


def parse_jsonl_records(path: pathlib.Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line:
            continue
        records.append(json.loads(line))
    return records


def perf_index(records: list[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    indexed: dict[str, dict[str, Any]] = {}
    for record in records:
        key = f"{record['workload']}/{record['scale']}"
        indexed[key] = record
    return indexed


def fmt_runtime(value: float | None) -> str:
    if value is None:
        return "n/a"
    return f"{value:.6f}s"


def fmt_delta(value: float) -> str:
    return f"{value:+.6f}s"


def fmt_ratio(value: float | None) -> str:
    if value is None:
        return "n/a"
    return f"{value:.2f}x"


def format_samples(samples: list[float]) -> str:
    return "[" + ", ".join(f"{sample:.6f}" for sample in samples) + "]"


def normalize_output_line(value: str) -> str:
    return re.sub(r"\s+", " ", value.strip())


def compare_against_baseline(current: float, baseline: float) -> dict[str, float]:
    delta = current - baseline
    ratio = current / baseline if baseline else 0.0
    pct = ((current / baseline) - 1.0) * 100.0 if baseline else 0.0
    return {"delta_sec": delta, "ratio": ratio, "pct": pct}


def delivery_status(jit_on: dict[str, dict[str, Any]]) -> dict[str, dict[str, bool]]:
    status: dict[str, dict[str, bool]] = {}
    for key, limit in RECOVERY_BAR.items():
        runtime = float(jit_on[key]["median_runtime_sec"])
        status[key] = {
            "restamp_bar": runtime <= FROZEN_BASELINES["kdz"]["jit_on"][key],
            "recovery_bar": runtime <= limit,
            "first_real_results_bar": runtime <= FIRST_RESULTS_BAR[key],
        }
    return status


def render_summary(
    *,
    host: str,
    repo: str,
    output_dir: pathlib.Path,
    commit: str,
    candidate: str,
    candidate_env: dict[str, str],
    host_info: dict[str, str],
    jit_status: str,
    oneshot_results: dict[str, str],
    micro_results: dict[str, str],
    remote_hashes: dict[str, str | None],
    jit_on_records: list[dict[str, Any]],
    joff_records: list[dict[str, Any]],
    pin_core: int | None,
    samples: int,
    warmup: int,
) -> str:
    jit_on_index = perf_index(jit_on_records)
    joff_index = perf_index(joff_records)
    lines = [
        "# Iterator Perf Restamp",
        "",
        f"- Timestamp: `{dt.datetime.now().astimezone().strftime('%Y-%m-%d %H:%M:%S %Z')}`",
        f"- Host label: `{host}`",
        f"- Hostname: `{host_info.get('HOSTNAME_FQDN', host_info.get('HOSTNAME_SHORT', host))}`",
        f"- Machine type: `{host_info.get('MACHINE_TYPE', 'unknown')}` (`{host_info.get('GENERATION', 'unknown')}`)",
        f"- Model: `{host_info.get('MODEL', 'unknown')}`",
        f"- Repo: `{repo}`",
        f"- Commit: `{commit}`",
        f"- Candidate env: `{candidate}`",
        f"- Benchmark: `{BENCH_FILE}`",
        f"- Pinned core: `{pin_core if pin_core is not None else 'unbound'}`",
        f"- Samples: `{samples}`",
        f"- Warmup runs: `{warmup}`",
        f"- Output dir: `{output_dir}`",
        "",
        "## Contract",
        "",
        "- tracked-file sync only",
        "- direct `src/` rebuild only",
        "- same benchmark file for `jit.on` and `-joff`",
        "- raw JSONL retained for both modes",
        "- raw owner logs and IR dumps retained for focused micros",
        "- full retained env applied by default; use `--candidate raw_jit` only for explicit diagnostic runs",
        "",
        "## Retained Env",
        "",
    ]
    for key, value in sorted(candidate_env.items()):
        lines.append(f"- `{key}={value}`")

    lines.extend([
        "",
        "## Delivered File Hashes",
        "",
    ])
    for relpath in AUTHORITATIVE_HASH_PATHS:
        digest = remote_hashes.get(relpath)
        lines.append(f"- `{relpath}`: `{digest if digest else 'missing'}`")

    lines.extend([
        "",
        "## Build And Correctness",
        "",
        f"- `jit.status()`: `{jit_status}`",
        f"- `/tmp/oneshot_iter.lua 20`: `{oneshot_results['20']}`",
        f"- `/tmp/oneshot_iter.lua 2000`: `{oneshot_results['2000']}`",
        f"- `/tmp/oneshot_iter.lua 200000`: `{oneshot_results['200000']}`",
        f"- `-joff /tmp/oneshot_iter.lua 200000`: `{oneshot_results['joff_200000']}`",
        f"- `HASH_VALUE`: `{micro_results['hash_value']}`",
        f"- `HASH_KEY`: `{micro_results['hash_key']}`",
        f"- `ARRAY_VALUE`: `{micro_results['array_value']}`",
        "",
        "## JIT-On Baseline",
        "",
    ])
    for key in ("pairs_sum/small", "pairs_array_sum/small", "pairs_sum/medium", "pairs_array_sum/medium", "pairs_sum/hot", "pairs_array_sum/hot"):
        record = jit_on_index[key]
        lines.append(
            f"- `{key}` median `{record['median_runtime_sec']:.6f}s`, "
            f"p95 `{record['p95_runtime_sec']:.6f}s`, samples `{format_samples(record['samples_sec'])}`"
        )
    lines.extend(["", "## -joff Comparator", ""])
    for key in ("pairs_sum/small", "pairs_array_sum/small", "pairs_sum/medium", "pairs_array_sum/medium", "pairs_sum/hot", "pairs_array_sum/hot"):
        record = joff_index[key]
        lines.append(
            f"- `{key}` median `{record['median_runtime_sec']:.6f}s`, "
            f"p95 `{record['p95_runtime_sec']:.6f}s`, samples `{format_samples(record['samples_sec'])}`"
        )

    lines.extend(["", "## Frozen Baseline Delta", ""])
    frozen = FROZEN_BASELINES[host]["jit_on"]
    for key in ("pairs_sum/hot", "pairs_array_sum/hot"):
        current = float(jit_on_index[key]["median_runtime_sec"])
        baseline = frozen[key]
        comp = compare_against_baseline(current, baseline)
        lines.append(
            f"- `{key}` current `{current:.6f}s` vs frozen `{baseline:.6f}s`: "
            f"delta `{fmt_delta(comp['delta_sec'])}`, ratio `{comp['ratio']:.2f}x`, pct `{comp['pct']:+.2f}%`"
        )

    lines.extend(["", "## JIT-On Distance To -joff", ""])
    for key in ("pairs_sum/hot", "pairs_array_sum/hot"):
        current = float(jit_on_index[key]["median_runtime_sec"])
        off = float(joff_index[key]["median_runtime_sec"])
        comp = compare_against_baseline(current, off)
        lines.append(
            f"- `{key}` JIT-on `{current:.6f}s` vs `-joff` `{off:.6f}s`: "
            f"gap `{fmt_delta(comp['delta_sec'])}`, ratio `{comp['ratio']:.2f}x`, pct `{comp['pct']:+.2f}%`"
        )

    if host == "kdz":
        lines.extend(["", "## Delivery Ladder", ""])
        status = delivery_status(jit_on_index)
        for key in ("pairs_sum/hot", "pairs_array_sum/hot"):
            runtime = float(jit_on_index[key]["median_runtime_sec"])
            lines.append(
                f"- `{key}` current `{runtime:.6f}s`, recovery bar `<= {RECOVERY_BAR[key]:.6f}s`, "
                f"first real-results bar `<= {FIRST_RESULTS_BAR[key]:.6f}s`, "
                f"restamp bar `{'pass' if status[key]['restamp_bar'] else 'fail'}`, "
                f"recovery bar `{'pass' if status[key]['recovery_bar'] else 'fail'}`, "
                f"first real-results bar `{'pass' if status[key]['first_real_results_bar'] else 'fail'}`"
            )

    lines.extend(
        [
            "",
            "## Focused Owner Artifacts",
            "",
            "- Raw owner logs:",
            f"  - `{output_dir / 'raw' / 'owner' / 'hash_value.stderr.log'}`",
            f"  - `{output_dir / 'raw' / 'owner' / 'hash_key.stderr.log'}`",
            f"  - `{output_dir / 'raw' / 'owner' / 'array_value.stderr.log'}`",
            "- Raw IR dumps:",
            f"  - `{output_dir / 'raw' / 'ir' / 'hash_value.stderr.log'}`",
            f"  - `{output_dir / 'raw' / 'ir' / 'hash_key.stderr.log'}`",
            f"  - `{output_dir / 'raw' / 'ir' / 'array_value.stderr.log'}`",
        ]
    )
    return "\n".join(lines) + "\n"


def validate_results(
    *,
    host: str,
    jit_status: str,
    oneshot_results: dict[str, str],
    micro_results: dict[str, str],
) -> None:
    if not jit_status.startswith("true"):
        raise RestampError(f"{host} clean rebuild is not JIT-enabled: {jit_status!r}")
    for count, expected in EXPECTED_ONESHOT_OUTPUTS.items():
        actual = normalize_output_line(oneshot_results[count])
        if actual != expected:
            raise RestampError(f"{host} oneshot {count} mismatch: expected {expected!r}, got {actual!r}")
    if normalize_output_line(oneshot_results["joff_200000"]) != EXPECTED_ONESHOT_OUTPUTS["200000"]:
        raise RestampError(
            f"{host} oneshot -joff mismatch: expected {EXPECTED_ONESHOT_OUTPUTS['200000']!r}, got {normalize_output_line(oneshot_results['joff_200000'])!r}"
        )
    for name, expected in EXPECTED_MICRO_OUTPUTS.items():
        actual = normalize_output_line(micro_results[name])
        if actual != expected:
            raise RestampError(f"{host} micro {name} mismatch: expected {expected!r}, got {actual!r}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Restamp the frozen s390x iterator perf baseline.")
    parser.add_argument("--host", choices=HOST_LABELS, required=True)
    parser.add_argument("--candidate", choices=sorted(CANDIDATE_ENVS), default="retained_baseline")
    parser.add_argument("--repo", help="Remote clean repo path. Defaults to the authoritative repo for the selected host.")
    parser.add_argument("--output-dir", required=True, help="Local artifact output directory.")
    parser.add_argument("--pin-core", type=int, default=DEFAULT_PIN_CORE, help="Pinned CPU core for benchmark runs (default: 0).")
    parser.add_argument("--samples", type=int, default=DEFAULT_SAMPLES, help="Measured sample count for perf runs (default: 9).")
    parser.add_argument("--warmup", type=int, default=DEFAULT_WARMUP, help="Warmup run count for perf runs (default: 2).")
    parser.add_argument("--skip-sync", action="store_true", help="Skip tracked-file sync into the remote clean repo.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    host = args.host
    candidate_env = CANDIDATE_ENVS[args.candidate]
    repo = args.repo or AUTHORITATIVE_REPOS[host]
    output_dir = pathlib.Path(args.output_dir).expanduser().resolve()
    raw_dir = output_dir / "raw"
    owner_dir = raw_dir / "owner"
    ir_dir = raw_dir / "ir"
    output_dir.mkdir(parents=True, exist_ok=True)
    owner_dir.mkdir(parents=True, exist_ok=True)
    ir_dir.mkdir(parents=True, exist_ok=True)

    commit = current_commit()
    host_info = collect_host_info(host)

    if not args.skip_sync:
        sync_tracked_files(host, repo)
    remote_hashes = remote_file_hashes(host, repo, AUTHORITATIVE_HASH_PATHS)

    remote_tmp = prepare_remote_scripts(host)
    try:
        build_remote_repo(host, repo, raw_dir)
        jit_status = run_jit_status(host, repo, raw_dir)
        oneshot_results = run_oneshot_checks(host, repo, remote_tmp, raw_dir, candidate_env)
        micro_results = {
            "hash_value": run_micro(host, repo, remote_tmp, raw_dir, "hash_value", candidate_env),
            "hash_key": run_micro(host, repo, remote_tmp, raw_dir, "hash_key", candidate_env),
            "array_value": run_micro(host, repo, remote_tmp, raw_dir, "array_value", candidate_env),
        }
        validate_results(
            host=host,
            jit_status=jit_status,
            oneshot_results=oneshot_results,
            micro_results=micro_results,
        )

        jit_on_records = run_iterator_bench(
            host=host,
            repo=repo,
            remote_tmp=remote_tmp,
            raw_dir=raw_dir,
            mode_label="jit-on",
            pin_core=args.pin_core,
            samples=args.samples,
            warmup=args.warmup,
            joff=False,
            extra_env=candidate_env,
        )
        joff_records = run_iterator_bench(
            host=host,
            repo=repo,
            remote_tmp=remote_tmp,
            raw_dir=raw_dir,
            mode_label="joff",
            pin_core=args.pin_core,
            samples=args.samples,
            warmup=args.warmup,
            joff=True,
            extra_env=candidate_env,
        )

        for name in MICRO_SCRIPTS:
            run_owner_logs(host, repo, remote_tmp, owner_dir, name, candidate_env)
            run_ir_dump(host, repo, remote_tmp, ir_dir, name, candidate_env)

        metadata = {
            "timestamp": dt.datetime.now(dt.timezone.utc).isoformat(),
            "git_commit": commit,
            "host_label": host,
            "candidate": args.candidate,
            "candidate_env": candidate_env,
            "hostname": host_info.get("HOSTNAME_FQDN", host_info.get("HOSTNAME_SHORT", host)),
            "host_shortname": host_info.get("HOSTNAME_SHORT", host),
            "machine_type": host_info.get("MACHINE_TYPE", ""),
            "generation": host_info.get("GENERATION", "unknown"),
            "model": host_info.get("MODEL", ""),
            "os_pretty_name": host_info.get("OS_PRETTY_NAME", ""),
            "uname": host_info.get("UNAME", ""),
            "repo_path": repo,
            "benchmark_file": BENCH_FILE,
            "pin_core": args.pin_core,
            "sample_count": args.samples,
            "warmup_runs": args.warmup,
            "jit_status": jit_status,
            "remote_hashes": remote_hashes,
            "modes_recorded": ["jit.on", "-joff"],
            "frozen_baseline": FROZEN_BASELINES.get(host, {}),
            "recovery_bar": RECOVERY_BAR,
            "first_real_results_bar": FIRST_RESULTS_BAR,
        }
        write_json(output_dir / "metadata.json", metadata)
        summary = render_summary(
            host=host,
            repo=repo,
            output_dir=output_dir,
            commit=commit,
            candidate=args.candidate,
            candidate_env=candidate_env,
            host_info=host_info,
            jit_status=jit_status,
            oneshot_results=oneshot_results,
            micro_results=micro_results,
            remote_hashes=remote_hashes,
            jit_on_records=jit_on_records,
            joff_records=joff_records,
            pin_core=args.pin_core,
            samples=args.samples,
            warmup=args.warmup,
        )
        write_text(output_dir / "summary.md", summary)
    finally:
        cleanup_remote_scripts(host, remote_tmp)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RestampError as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
