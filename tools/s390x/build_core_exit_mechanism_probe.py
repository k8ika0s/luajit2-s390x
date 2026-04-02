#!/usr/bin/env python3
"""Capture reduced exit-mechanism artifacts for the promoted core throughput slice."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import pathlib
import re
import shlex
import sys
from typing import Any

THIS_DIR = pathlib.Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

import restamp_iterator_perf as restamp


ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT_ROOT = ROOT / "artifacts" / "s390x" / "manual"
CANDIDATE_ENVS: dict[str, dict[str, str]] = {
    "baseline": {
        "LUAJIT_S390X_DISABLE_HOTSIDE_CANON_SHARE_UGET_LOOPROOT": "1",
    },
    "hotside_canon_share_uget_looproot_default": {},
}


def emit_hist_lua() -> str:
    return """\
local function emit_hist(label, buckets)
  local keys = {}
  for key in pairs(buckets) do keys[#keys + 1] = key end
  table.sort(keys)
  local parts = {}
  for i = 1, #keys do
    local key = keys[i]
    parts[#parts + 1] = key .. "=" .. buckets[key]
  end
  print(label, table.concat(parts, ","))
end
"""


def emit_traceinfo_lua() -> str:
    return """\
local function emit_traceinfo(limit)
  local util = require("jit.util")
  for tr = 1, limit do
    local info = util.traceinfo(tr)
    if info then
      print(
        "TRACEINFO",
        tr,
        tonumber(info.link) or 0,
        tostring(info.linktype),
        tonumber(info.nins) or 0,
        tonumber(info.nk) or 0,
        tonumber(info.nexit) or 0
      )
    end
  end
end
"""


WORKLOADS: dict[str, dict[str, Any]] = {
    "number_helper_loop": {
        "family": "be_helpers",
        "iterations": 64000,
        "label": "NUMBER_HELPER_LOOP",
        "script": """\
local bit = require("bit")
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
jit.opt.start("hotloop=1")
{emit_hist}
{emit_traceinfo}
local function run(n)
  local total = 0
  for i = 1, n do
    total = bit.tobit(total + i * 65537)
  end
  return bit.tobit(total)
end
run(20); run(20); run(20)
local trace_cap = testlib.trace_counter_capture()
local texit_cap = testlib.texit_counter_capture()
print("RESULT", run({iterations}))
trace_cap.stop()
texit_cap.stop()
print("TRACE_START", trace_cap.start)
print("TRACE_STOP", trace_cap.stop_count)
print("TRACE_ABORT", trace_cap.abort)
print("TRACE_TOTAL", trace_cap.total)
print("TEXIT_COUNT", texit_cap.total)
emit_hist("TRACE_HIST", trace_cap.hist)
emit_hist("TEXIT_HIST", texit_cap.hist)
emit_traceinfo(32)
""",
    },
    "be_pack_loop": {
        "family": "be_helpers",
        "iterations": 64000,
        "label": "BE_PACK_LOOP",
        "script": """\
local bit = require("bit")
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
jit.opt.start("hotloop=1")
{emit_hist}
{emit_traceinfo}
local function run(n)
  local total = 0
  for i = 1, n do
    local b1 = bit.band(bit.rshift(i, 24), 0xff)
    local b2 = bit.band(bit.rshift(i, 16), 0xff)
    local b3 = bit.band(bit.rshift(i, 8), 0xff)
    local b4 = bit.band(i, 0xff)
    total = bit.tobit(total + bit.lshift(b1, 24) + bit.lshift(b2, 16) + bit.lshift(b3, 8) + b4)
  end
  return bit.tobit(total)
end
run(20); run(20); run(20)
local trace_cap = testlib.trace_counter_capture()
local texit_cap = testlib.texit_counter_capture()
print("RESULT", run({iterations}))
trace_cap.stop()
texit_cap.stop()
print("TRACE_START", trace_cap.start)
print("TRACE_STOP", trace_cap.stop_count)
print("TRACE_ABORT", trace_cap.abort)
print("TRACE_TOTAL", trace_cap.total)
print("TEXIT_COUNT", texit_cap.total)
emit_hist("TRACE_HIST", trace_cap.hist)
emit_hist("TEXIT_HIST", texit_cap.hist)
emit_traceinfo(32)
""",
    },
    "direct_abs": {
        "family": "ffi_calls",
        "iterations": 80000,
        "label": "DIRECT_ABS",
        "script": """\
local ffi = require("ffi")
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
ffi.cdef[[ int abs(int x); ]]
jit.opt.start("hotloop=1")
{emit_hist}
{emit_traceinfo}
local function run(n)
  local total = 0
  for i = 1, n do
    total = total + ffi.C.abs((i % 17) - 8)
  end
  return total
end
run(20); run(20); run(20)
local trace_cap = testlib.trace_counter_capture()
local texit_cap = testlib.texit_counter_capture()
print("RESULT", run({iterations}))
trace_cap.stop()
texit_cap.stop()
print("TRACE_START", trace_cap.start)
print("TRACE_STOP", trace_cap.stop_count)
print("TRACE_ABORT", trace_cap.abort)
print("TRACE_TOTAL", trace_cap.total)
print("TEXIT_COUNT", texit_cap.total)
emit_hist("TRACE_HIST", trace_cap.hist)
emit_hist("TEXIT_HIST", texit_cap.hist)
emit_traceinfo(32)
""",
    },
    "stored_abs": {
        "family": "ffi_calls",
        "iterations": 80000,
        "label": "STORED_ABS",
        "script": """\
local ffi = require("ffi")
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
ffi.cdef[[ int abs(int x); ]]
local cabs = ffi.C.abs
jit.opt.start("hotloop=1")
{emit_hist}
{emit_traceinfo}
local function run(n)
  local total = 0
  for i = 1, n do
    total = total + cabs((i % 17) - 8)
  end
  return total
end
run(20); run(20); run(20)
local trace_cap = testlib.trace_counter_capture()
local texit_cap = testlib.texit_counter_capture()
print("RESULT", run({iterations}))
trace_cap.stop()
texit_cap.stop()
print("TRACE_START", trace_cap.start)
print("TRACE_STOP", trace_cap.stop_count)
print("TRACE_ABORT", trace_cap.abort)
print("TRACE_TOTAL", trace_cap.total)
print("TEXIT_COUNT", texit_cap.total)
emit_hist("TRACE_HIST", trace_cap.hist)
emit_hist("TEXIT_HIST", texit_cap.hist)
emit_traceinfo(32)
""",
    },
}


def parse_hist_line(line: str, prefix: str) -> dict[str, int]:
    match = re.match(rf"^{re.escape(prefix)}\s*(.*)$", line.strip())
    if not match:
        return {}
    payload = match.group(1).strip()
    if not payload:
        return {}
    hist: dict[str, int] = {}
    for item in payload.split(","):
        item = item.strip()
        if not item or "=" not in item:
            continue
        key, value = item.split("=", 1)
        try:
            hist[key] = int(value)
        except ValueError:
            continue
    return hist


def parse_scalar_lines(text: str) -> dict[str, int]:
    out: dict[str, int] = {}
    for raw_line in text.splitlines():
        line = raw_line.strip()
        for key in ("TRACE_START", "TRACE_STOP", "TRACE_ABORT", "TRACE_TOTAL", "TEXIT_COUNT"):
            if line.startswith(key):
                parts = line.split()
                if not parts or parts[0] != key:
                    continue
                try:
                    out[key] = int(parts[1])
                except (IndexError, ValueError):
                    pass
    return out


def parse_traceinfo(text: str) -> dict[int, dict[str, Any]]:
    traceinfo: dict[int, dict[str, Any]] = {}
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line.startswith("TRACEINFO"):
            continue
        parts = line.split()
        if len(parts) < 7 or parts[0] != "TRACEINFO":
            continue
        try:
            traceno = int(parts[1])
            link = int(parts[2])
            nins = int(parts[4])
            nk = int(parts[5])
            nexit = int(parts[6])
        except ValueError:
            continue
        traceinfo[traceno] = {
            "traceno": traceno,
            "link": link,
            "linktype": parts[3],
            "nins": nins,
            "nk": nk,
            "nexit": nexit,
        }
    return traceinfo


def dominant_hist(hist: dict[str, int]) -> dict[str, Any] | None:
    if not hist:
        return None
    key, count = max(hist.items(), key=lambda item: (item[1], item[0]))
    return {"key": key, "count": count}


def render_lua_script(template: str, iterations: int) -> str:
    return template.format(
        iterations=iterations,
        emit_hist=emit_hist_lua().rstrip(),
        emit_traceinfo=emit_traceinfo_lua().rstrip(),
    )


def write_text(path: pathlib.Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def write_json(path: pathlib.Path, payload: Any) -> None:
    write_text(path, json.dumps(payload, indent=2, sort_keys=True) + "\n")


def run_probe(
    *,
    host: str,
    repo: str,
    raw_dir: pathlib.Path,
    workload: str,
    extra_env: dict[str, str],
    capture_dump: bool,
    dump_flags: str,
    iterations_override: int | None,
) -> dict[str, Any]:
    config = WORKLOADS[workload]
    remote_name = f"{workload}.lua"
    remote_script_path = f"/tmp/{remote_name}"
    iterations = iterations_override or int(config["iterations"])
    script_text = render_lua_script(config["script"], iterations)
    restamp.run_remote_command(
        host,
        f"""
set -euo pipefail
cat > {shlex.quote(remote_script_path)} <<'EOF'
{script_text.rstrip()}
EOF
""",
        stdout_path=raw_dir / f"{workload}.setup.stdout.log",
        stderr_path=raw_dir / f"{workload}.setup.stderr.log",
        label=f"{host} setup {workload}",
    )
    extra_env_prefix = restamp.remote_extra_env_prefix(extra_env)
    proc = restamp.run_remote_command(
        host,
        f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
{extra_env_prefix}./src/luajit {shlex.quote(remote_script_path)}
""",
        stdout_path=raw_dir / f"{workload}.stdout.log",
        stderr_path=raw_dir / f"{workload}.stderr.log",
        label=f"{host} mechanism probe {workload}",
    )
    if capture_dump:
        restamp.run_remote_command(
            host,
            f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
{extra_env_prefix}./src/luajit -jdump={shlex.quote(dump_flags)} {shlex.quote(remote_script_path)}
""",
            stdout_path=raw_dir / f"{workload}.dump.stdout.log",
            stderr_path=raw_dir / f"{workload}.dump.stderr.log",
            label=f"{host} mechanism dump {workload}",
        )
    restamp.run_ssh_script(
        host,
        f"""
set -euo pipefail
rm -f {shlex.quote(remote_script_path)}
""",
    )
    stdout = proc.stdout
    trace_hist = parse_hist_line(next((line for line in stdout.splitlines() if line.startswith("TRACE_HIST")), "TRACE_HIST"), "TRACE_HIST")
    texit_hist = parse_hist_line(next((line for line in stdout.splitlines() if line.startswith("TEXIT_HIST")), "TEXIT_HIST"), "TEXIT_HIST")
    scalars = parse_scalar_lines(stdout)
    traceinfo = parse_traceinfo(stdout)
    dominant_texit = dominant_hist(texit_hist)
    dominant_trace = dominant_hist(trace_hist)
    dominant_traceinfo = None
    if dominant_texit:
        match = re.match(r"^(\d+):(\d+)$", dominant_texit["key"])
        if match:
            dominant_traceinfo = traceinfo.get(int(match.group(1)))
    return {
        "workload": workload,
        "family": config["family"],
        "iterations": iterations,
        "counts": scalars,
        "trace_hist": trace_hist,
        "texit_hist": texit_hist,
        "dominant_trace": dominant_trace,
        "dominant_texit": dominant_texit,
        "dominant_traceinfo": dominant_traceinfo,
        "traceinfo": traceinfo,
    }


def render_summary(
    *,
    host: str,
    repo: str,
    candidate: str,
    commit: str,
    host_info: dict[str, str],
    output_dir: pathlib.Path,
    results: list[dict[str, Any]],
) -> str:
    lines = [
        "# Core Exit Mechanism Probe",
        "",
        f"- Timestamp: `{dt.datetime.now().astimezone().strftime('%Y-%m-%d %H:%M:%S %Z')}`",
        f"- Host label: `{host}`",
        f"- Hostname: `{host_info.get('HOSTNAME_FQDN', host_info.get('HOSTNAME_SHORT', host))}`",
        f"- Machine type: `{host_info.get('MACHINE_TYPE', 'unknown')}` (`{host_info.get('GENERATION', 'unknown')}`)",
        f"- Repo: `{repo}`",
        f"- Commit: `{commit}`",
        f"- Candidate: `{candidate}`",
        f"- Output dir: `{output_dir}`",
        "",
        "## Read",
        "",
    ]
    family_groups: dict[str, list[dict[str, Any]]] = {}
    for result in results:
        family_groups.setdefault(str(result["family"]), []).append(result)
    for family, family_results in family_groups.items():
        lines.append(f"### `{family}`")
        lines.append("")
        for result in family_results:
            counts = result["counts"]
            dominant_texit = result["dominant_texit"]
            dominant_traceinfo = result["dominant_traceinfo"]
            lines.append(
                f"- `{result['workload']}`: `TRACE_START {counts.get('TRACE_START', 0)}`, "
                f"`TRACE_STOP {counts.get('TRACE_STOP', 0)}`, `TRACE_ABORT {counts.get('TRACE_ABORT', 0)}`, "
                f"`TEXIT_COUNT {counts.get('TEXIT_COUNT', 0)}`"
            )
            if dominant_texit:
                lines.append(
                    f"- `{result['workload']}` dominant texit: `{dominant_texit['key']}` x `{dominant_texit['count']}`"
                )
            if dominant_traceinfo:
                lines.append(
                    f"- `{result['workload']}` dominant traceinfo: "
                    f"`link {dominant_traceinfo['link']}`, `linktype {dominant_traceinfo['linktype']}`, "
                    f"`nins {dominant_traceinfo['nins']}`, `nexit {dominant_traceinfo['nexit']}`"
                )
        lines.append("")
    lines.extend(
        [
            "## Raw Artifacts",
            "",
            f"- `{output_dir / 'raw'}`",
        ]
    )
    return "\n".join(lines) + "\n"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Capture reduced core exit-mechanism artifacts.")
    parser.add_argument("--host", choices=tuple(restamp.HOST_LABELS), required=True)
    parser.add_argument("--repo", help="Remote clean repo path. Defaults to the authoritative repo for the selected host.")
    parser.add_argument("--candidate", choices=tuple(CANDIDATE_ENVS), default="hotside_canon_share_uget_looproot_default")
    parser.add_argument(
        "--workload",
        action="append",
        choices=tuple(WORKLOADS),
        help="Specific workload(s) to probe. Defaults to all core mechanism workloads.",
    )
    parser.add_argument("--output-dir", help="Local artifact output directory.")
    parser.add_argument(
        "--capture-dump",
        action="store_true",
        help="Also capture reduced -jdump=is output for each selected workload.",
    )
    parser.add_argument(
        "--dump-flags",
        default="is",
        help="Dump flags to pass to -jdump when --capture-dump is used (default: is).",
    )
    parser.add_argument(
        "--iterations",
        type=int,
        help="Override the configured iteration count for all selected workloads.",
    )
    parser.add_argument(
        "--env",
        action="append",
        default=[],
        metavar="KEY=VALUE",
        help="Extra environment variable to set for the remote probe process.",
    )
    return parser.parse_args()


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


def main() -> int:
    args = parse_args()
    repo = args.repo or restamp.AUTHORITATIVE_REPOS[args.host]
    workloads = args.workload or ["number_helper_loop", "be_pack_loop", "direct_abs", "stored_abs"]
    timestamp = dt.datetime.now().astimezone().strftime("%Y%m%d-%H%M%S")
    output_dir = pathlib.Path(args.output_dir) if args.output_dir else (
        DEFAULT_OUTPUT_ROOT / f"{timestamp}-{args.host}-{args.candidate}-core-exit-mechanism"
    )
    raw_dir = output_dir / "raw"
    raw_dir.mkdir(parents=True, exist_ok=True)

    host_info = restamp.collect_host_info(args.host)
    commit = restamp.current_commit()
    restamp.sync_tracked_files(args.host, repo)
    restamp.build_remote_repo(args.host, repo, raw_dir)

    extra_env = dict(CANDIDATE_ENVS[args.candidate])
    extra_env.update(parse_env_overrides(args.env))
    results = [
        run_probe(
            host=args.host,
            repo=repo,
            raw_dir=raw_dir,
            workload=workload,
            extra_env=extra_env,
            capture_dump=args.capture_dump,
            dump_flags=args.dump_flags,
            iterations_override=args.iterations,
        )
        for workload in workloads
    ]

    write_json(output_dir / "summary.json", {
        "host": args.host,
        "repo": repo,
        "candidate": args.candidate,
        "commit": commit,
        "host_info": host_info,
        "results": results,
    })
    write_text(
        output_dir / "summary.md",
        render_summary(
            host=args.host,
            repo=repo,
            candidate=args.candidate,
            commit=commit,
            host_info=host_info,
            output_dir=output_dir,
            results=results,
        ),
    )
    print(output_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
