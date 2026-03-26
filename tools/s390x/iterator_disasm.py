#!/usr/bin/env python3
"""Iterator trace disassembly helper for native s390x hosts.

Builds the current tree on one or more configured hosts, runs the tiny iterator
repro for selected hotexit presets, and dumps the first few trace mcode blocks
plus exit stub addresses for low-level root/child trace correlation.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import pathlib
import shutil
import sys
from typing import Dict, List

from iterator_probe import (  # type: ignore
    ARTIFACTS_ROOT,
    HOSTS,
    ProbeError,
    WORKLOADS,
    build_host,
    collect_remote_artifacts,
    ensure_remote_dirs,
    run_remote_step,
    run_ssh,
    shell_join,
    sync_repo,
    workload_lua,
)


ROOT = pathlib.Path(__file__).resolve().parents[2]
REMOTE_BASE = "/root/luajit2-s390x"
REMOTE_REPO_NAME = "repo"
REMOTE_ARTIFACTS_NAME = "artifacts"

TRACEINFO_RE = __import__("re").compile(
    r"^TRACEINFO tr=(?P<tr>\d+) link=(?P<link>\S+) type=(?P<linktype>\S+) nins=(?P<nins>\d+) nk=(?P<nk>\d+) nexit=(?P<nexit>\d+)$"
)
TRACEMC_RE = __import__("re").compile(
    r"^TRACEMC tr=(?P<tr>\d+) addr=(?P<addr>0x[0-9a-fA-F]+) loop=(?P<loop>-?\d+) size=(?P<size>\d+)$"
)
EXITSTUB_RE = __import__("re").compile(
    r"^EXITSTUB tr=(?P<tr>\d+) ex=(?P<ex>\d+) addr=(?P<addr>0x[0-9a-fA-F]+)$"
)
RESULT_RE = __import__("re").compile(r"^RESULT actual=(?P<actual>-?\d+) expected=(?P<expected>-?\d+)$")


def local_run_id() -> str:
    now = dt.datetime.now(dt.timezone.utc)
    return f"{now.strftime('%Y%m%dT%H%M%S.%fZ')}-iterator-disasm-p{os.getpid()}"


def iterator_disasm_lua(
    hotexit: int,
    trace_limit: int,
    workload: str,
    iterations: int,
    outer: int,
    hotloop: int,
    minstitch: int,
    default_jitopt: bool,
) -> str:
    workload_definition, call_args, descriptor = workload_lua(workload, iterations, outer)
    opt_lines: List[str] = []
    if not default_jitopt:
        opt_lines = [
            "jit.opt.start(",
            f'  "hotloop={hotloop}", "hotexit={hotexit}", "minstitch={minstitch}"',
            ")",
            "",
        ]
    return "\n".join(
        [
            'local jit = require("jit")',
            'local util = require("jit.util")',
            'local outdir = assert(os.getenv("S390X_STEP_DIR"), "missing S390X_STEP_DIR")',
            "",
            *opt_lines,
            workload_definition,
            "",
            "local function interpreter_result(fn, ...)",
            "  jit.off(fn, true)",
            "  local result = fn(...)",
            "  jit.on(fn, true)",
            "  jit.flush()",
            "  return result",
            "end",
            "",
            f"local expected = interpreter_result(run_workload, {call_args})",
            f"local actual = run_workload({call_args})",
            f'print("WORKLOAD {descriptor}")',
            'print(string.format("RESULT actual=%d expected=%d", actual, expected))',
            "",
            f"for tr = 1, {trace_limit} do",
            "  local info = util.traceinfo(tr)",
            "  if not info then break end",
            '  print(string.format("TRACEINFO tr=%d link=%s type=%s nins=%d nk=%d nexit=%d",',
            "    tr, tostring(info.link), tostring(info.linktype),",
            "    tonumber(info.nins) or -1, tonumber(info.nk) or -1, tonumber(info.nexit) or -1))",
            "  local mcode, addr, loop = util.tracemc(tr)",
            "  if mcode and addr then",
            '    print(string.format("TRACEMC tr=%d addr=0x%x loop=%d size=%d", tr, addr, tonumber(loop) or -1, #mcode))',
            '    local path = string.format("%s/trace-%d.bin", outdir, tr)',
            '    local fh = assert(io.open(path, "wb"))',
            "    fh:write(mcode)",
            "    fh:close()",
            '    print(string.format("TRACEBIN tr=%d path=%s", tr, path))',
            "  end",
            "  for ex = 0, info.nexit - 1 do",
            "    local stub = util.traceexitstub(tr, ex)",
            "    if stub then",
            '      print(string.format("EXITSTUB tr=%d ex=%d addr=0x%x", tr, ex, stub))',
            "    end",
            "  end",
            "end",
        ]
    )


def normalize_disasm(stdout_text: str) -> dict:
    result = None
    traces: Dict[str, dict] = {}
    for raw_line in stdout_text.splitlines():
        line = raw_line.rstrip("\n")
        stripped = line.strip()
        if not stripped:
            continue
        if (m := RESULT_RE.match(stripped)):
            result = {"actual": int(m.group("actual")), "expected": int(m.group("expected"))}
            continue
        if (m := TRACEINFO_RE.match(stripped)):
            tr = m.group("tr")
            traces.setdefault(tr, {})
            traces[tr]["info"] = {
                "tr": int(tr),
                "link": m.group("link"),
                "linktype": m.group("linktype"),
                "nins": int(m.group("nins")),
                "nk": int(m.group("nk")),
                "nexit": int(m.group("nexit")),
            }
            continue
        if (m := TRACEMC_RE.match(stripped)):
            tr = m.group("tr")
            traces.setdefault(tr, {})
            traces[tr]["mcode"] = {
                "addr": m.group("addr"),
                "loop": int(m.group("loop")),
                "size": int(m.group("size")),
            }
            continue
        if (m := EXITSTUB_RE.match(stripped)):
            tr = m.group("tr")
            traces.setdefault(tr, {})
            traces[tr].setdefault("exitstubs", []).append(
                {
                    "exit": int(m.group("ex")),
                    "addr": m.group("addr"),
                }
            )

    return {"result": result, "traces": traces}


def write_json(path: pathlib.Path, payload: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def summarize_host(host_dir: pathlib.Path, hotexit_values: List[int]) -> None:
    summary: Dict[str, dict] = {}
    for hotexit in hotexit_values:
        preset_dir = host_dir / f"hotexit-{hotexit}"
        stdout_text = (preset_dir / "raw-stdout.log").read_text(encoding="utf-8", errors="replace")
        normalized = normalize_disasm(stdout_text)
        write_json(preset_dir / "mcode-summary.json", normalized)
        summary[str(hotexit)] = {
            "result": normalized["result"],
            "traces": {
                tr: {
                    "info": entry.get("info"),
                    "mcode": entry.get("mcode"),
                    "exitstubs": entry.get("exitstubs", []),
                }
                for tr, entry in normalized["traces"].items()
            },
        }
    write_json(host_dir / "matrix-summary.json", summary)


def run_disasm_host(
    host: str,
    remote_repo_root: str,
    remote_artifacts_root: str,
    hotexit_values: List[int],
    trace_limit: int,
    workload: str,
    iterations: int,
    outer: int,
    hotloop: int,
    minstitch: int,
    default_jitopt: bool,
) -> None:
    for hotexit in hotexit_values:
        lua = iterator_disasm_lua(
            hotexit,
            trace_limit,
            workload,
            iterations,
            outer,
            hotloop,
            minstitch,
            default_jitopt,
        )
        command = "\n".join(
            [
                "set -euo pipefail",
                "export LUA_PATH=\"$PWD/src/?.lua;$PWD/src/?/?.lua;;\"",
                "cat > \"$S390X_STEP_DIR/iterator_disasm.lua\" <<'LUA'",
                lua,
                "LUA",
                "./src/luajit \"$S390X_STEP_DIR/iterator_disasm.lua\"",
                "for bin in \"$S390X_STEP_DIR\"/trace-*.bin; do",
                "  [ -e \"$bin\" ] || continue",
                "  objdump -D -b binary -m s390 \"$bin\" > \"${bin%.bin}.objdump\"",
                "done",
            ]
        )
        run_remote_step(
            host,
            remote_repo_root,
            remote_artifacts_root,
            f"iterator-disasm-hotexit-{hotexit}",
            command,
            timeout_sec=600,
        )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--hosts", nargs="+", default=["kdz"], choices=HOSTS)
    parser.add_argument("--mode", default="debug", choices=("debug", "release"))
    parser.add_argument("--hotexit", nargs="+", type=int, default=[5, 10])
    parser.add_argument("--trace-limit", type=int, default=4)
    parser.add_argument("--workload", choices=WORKLOADS, default="shape")
    parser.add_argument("--iterations", type=int, default=2)
    parser.add_argument("--outer", type=int, default=2)
    parser.add_argument("--hotloop", type=int, default=2)
    parser.add_argument("--minstitch", type=int, default=1)
    parser.add_argument("--default-jitopt", action="store_true")
    parser.add_argument("--run-id", default="auto")
    parser.add_argument("--keep-remote", action="store_true")
    parser.add_argument(
        "--summarize-existing",
        help="Re-summarize an existing disasm dir under artifacts/s390x/<run-id>/disasm or an absolute disasm path.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.summarize_existing:
        disasm_dir = pathlib.Path(args.summarize_existing)
        if not disasm_dir.is_absolute():
            disasm_dir = ARTIFACTS_ROOT / args.summarize_existing / "disasm"
        if not disasm_dir.exists():
            print(f"[iterator-disasm] missing disasm dir: {disasm_dir}", file=sys.stderr)
            return 1
        for host in args.hosts:
            host_dir = disasm_dir / host
            if host_dir.exists():
                summarize_host(host_dir, args.hotexit)
        print(f"[iterator-disasm] re-summarized: {disasm_dir}", flush=True)
        return 0

    run_id = local_run_id() if args.run_id == "auto" else args.run_id
    local_run_dir = ARTIFACTS_ROOT / run_id
    local_remote_dir = local_run_dir / "remote"
    normalized_dir = local_run_dir / "disasm"
    local_run_dir.mkdir(parents=True, exist_ok=False)
    try:
        for host in args.hosts:
            remote_run_root = f"{REMOTE_BASE}/{run_id}-{host}"
            remote_repo_root = f"{remote_run_root}/{REMOTE_REPO_NAME}"
            remote_artifacts_root = f"{remote_run_root}/{REMOTE_ARTIFACTS_NAME}"

            print(f"[iterator-disasm] syncing {host}", flush=True)
            ensure_remote_dirs(host, remote_run_root, remote_repo_root, remote_artifacts_root)
            sync_repo(host, remote_repo_root)

            print(f"[iterator-disasm] building {host} ({args.mode})", flush=True)
            build_host(host, remote_repo_root, remote_artifacts_root, args.mode)

            print(
                f"[iterator-disasm] dumping {host} workload={args.workload} hotexit presets {args.hotexit}",
                flush=True,
            )
            run_disasm_host(
                host,
                remote_repo_root,
                remote_artifacts_root,
                args.hotexit,
                args.trace_limit,
                args.workload,
                args.iterations,
                args.outer,
                args.hotloop,
                args.minstitch,
                args.default_jitopt,
            )

            host_remote_dir = local_remote_dir / host
            collect_remote_artifacts(host, remote_artifacts_root, host_remote_dir)

            host_out_dir = normalized_dir / host
            host_out_dir.mkdir(parents=True, exist_ok=True)
            for hotexit in args.hotexit:
                step_dir = host_remote_dir / f"iterator-disasm-hotexit-{hotexit}"
                preset_dir = host_out_dir / f"hotexit-{hotexit}"
                preset_dir.mkdir(parents=True, exist_ok=True)
                shutil.copy2(step_dir / "stdout.log", preset_dir / "raw-stdout.log")
                shutil.copy2(step_dir / "stderr.log", preset_dir / "raw-stderr.log")
                shutil.copy2(step_dir / "metadata.json", preset_dir / "metadata.json")
                shutil.copy2(step_dir / "command.txt", preset_dir / "command.txt")
                for objdump_path in sorted(step_dir.glob("trace-*.objdump")):
                    shutil.copy2(objdump_path, preset_dir / objdump_path.name)

            summarize_host(host_out_dir, args.hotexit)

            if not args.keep_remote:
                run_ssh(host, shell_join(["rm", "-rf", remote_run_root]))

        print(f"[iterator-disasm] artifacts: {normalized_dir}", flush=True)
        return 0
    except ProbeError as exc:
        print(f"[iterator-disasm] error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
