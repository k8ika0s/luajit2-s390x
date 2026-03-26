#!/usr/bin/env python3
"""Iterator trace probe for native s390x hosts.

Builds the current tree on one or more configured hosts, runs a tiny iterator
repro across fixed hotexit presets, and normalizes the existing s390x trace
diagnostics into machine-readable artifacts.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import pathlib
import re
import shlex
import shutil
import subprocess
import sys
import textwrap
from typing import Dict, Iterable, List, Optional


ROOT = pathlib.Path(__file__).resolve().parents[2]
ARTIFACTS_ROOT = ROOT / "artifacts" / "s390x"
REMOTE_BASE = "/root/luajit2-s390x"
REMOTE_REPO_NAME = "repo"
REMOTE_ARTIFACTS_NAME = "artifacts"
HOSTS = ("kdz", "zkd0")
DEFAULT_HOTEXIT = [1, 4, 5, 10, 100000]
WORKLOADS = ("shape", "pairs_sum", "pairs_array_sum")
DEBUG_ENV_KEYS = [
    "LUAJIT_S390X_GUARD_LOG",
    "LUAJIT_S390X_CALL_LOG",
    "LUAJIT_S390X_ADD_LOG",
    "LUAJIT_S390X_EXIT_LOG",
    "LUAJIT_S390X_SLOT_LOG",
    "LUAJIT_S390X_RECSTOP_LOG",
    "LUAJIT_S390X_BADRA_LOG",
]


TRACE_EVENT_RE = re.compile(
    r"^TRACE_EVENT what=(?P<what>\S+) tr=(?P<tr>-?\d+) otr=(?P<otr>\S+) oex=(?P<oex>\S+)$"
)
TEXIT_RE = re.compile(r"^TEXIT tr=(?P<tr>-?\d+) ex=(?P<ex>-?\d+)$")
RESULT_RE = re.compile(r"^RESULT actual=(?P<actual>-?\d+) expected=(?P<expected>-?\d+)$")
TRACEINFO_RE = re.compile(
    r"^TRACEINFO tr=(?P<tr>\d+) link=(?P<link>\S+) type=(?P<linktype>\S+) nins=(?P<nins>\d+) nk=(?P<nk>\d+) nexit=(?P<nexit>\d+)$"
)
TRACEIR_RE = re.compile(
    r"^TRACEIR tr=(?P<tr>\d+) ins=(?P<ins>\d+) op=(?P<op>\S+) ot=(?P<ot>-?\d+) mode=(?P<mode>-?\d+) op1=(?P<op1>-?\d+) op2=(?P<op2>-?\d+) prev=(?P<prev>-?\d+)$"
)
TRACESNAP_RE = re.compile(
    r"^TRACESNAP tr=(?P<tr>\d+) sn=(?P<sn>\d+) ref=(?P<ref>-?\d+) nslots=(?P<nslots>-?\d+) map=(?P<map>.*)$"
)
TRACEMC_RE = re.compile(
    r"^TRACEMC tr=(?P<tr>\d+) addr=(?P<addr>0x[0-9a-fA-F]+) loop=(?P<loop>-?\d+) size=(?P<size>\d+)$"
)
EXITSTUB_RE = re.compile(
    r"^EXITSTUB tr=(?P<tr>\d+) ex=(?P<ex>\d+) addr=(?P<addr>0x[0-9a-fA-F]+)$"
)

GUARD_STUB_RE = re.compile(
    r"^S390X_GUARD curins=(?P<curins>-?\d+) snap=(?P<snap>\d+) loopsnap=(?P<loopsnap>\d+) "
    r"cc=(?P<cc>-?\d+) loopinv=(?P<loopinv>\d+) p=(?P<patchpoint>\S+) target=(?P<target>\S+) invmcp=(?P<invmcp>\S+)$"
)
GUARD_KIND_RE = re.compile(
    r"^S390X_GUARD kind=(?P<kind>\S+) curins=(?P<curins>-?\d+) ir=(?P<ir>-?\d+) op=(?P<op>-?\d+) "
    r"type=(?P<type>-?\d+) cc=(?P<cc>-?\d+) ofs=(?P<ofs>-?\d+) extra=(?P<extra>-?\d+)$"
)
RECSTOP_RE = re.compile(
    r"^S390X_RECSTOP trace=(?P<trace>\d+) parent=(?P<parent>-?\d+) exit=(?P<exit>-?\d+) "
    r"pc=(?P<pc>\S+) op=(?P<op>-?\d+) prevop=(?P<prevop>-?\d+) startop=(?P<startop>-?\d+) "
    r"linktype=(?P<linktype>-?\d+) link=(?P<link>-?\d+) lnkop=(?P<lnkop>-?\d+) root=(?P<root>-?\d+) "
    r"framedepth=(?P<framedepth>-?\d+) retdepth=(?P<retdepth>-?\d+)$"
)
EXIT_RE = re.compile(
    r"^S390X_EXIT phase=(?P<phase>\S+) trace=(?P<trace>\d+) exit=(?P<exit>\d+) .* op=(?P<op>\d+) "
    r"snapcount=(?P<snapcount>\d+) snapref=(?P<snapref>\d+) snapnent=(?P<snapnent>\d+)"
)
EXIT_SNAP_RE = re.compile(
    r"^S390X_EXIT_SNAP trace=(?P<trace>\d+) exit=(?P<exit>\d+) snappc=(?P<snappc>\S+) snapop=(?P<snapop>\d+)(?P<rest>.*)$"
)
RESUME_RE = re.compile(r"^S390X_RESUME pc=(?P<pc>\S+) op=(?P<op>\d+)$")
SLOT_RE = re.compile(
    r"^S390X_SLOT idx=(?P<idx>\d+) itype=(?P<itype>-?\d+) u64=0x(?P<u64>[0-9a-fA-F]+)$"
)


class ProbeError(RuntimeError):
    pass


def shell_join(parts: Iterable[str]) -> str:
    return " ".join(shlex.quote(str(part)) for part in parts)


def run(argv: List[str], *, cwd: pathlib.Path = ROOT, input_bytes: Optional[bytes] = None) -> subprocess.CompletedProcess:
    return subprocess.run(argv, cwd=str(cwd), input=input_bytes, capture_output=True, text=False)


def run_ssh(host: str, command: str) -> subprocess.CompletedProcess:
    return run(["ssh", "-o", "BatchMode=yes", host, f"bash -lc {shlex.quote(command)}"])


def ensure_remote_dirs(host: str, remote_run_root: str, remote_repo_root: str, remote_artifacts_root: str) -> None:
    command = shell_join(["mkdir", "-p", remote_run_root, remote_repo_root, remote_artifacts_root])
    proc = run_ssh(host, command)
    if proc.returncode != 0:
        raise ProbeError(f"failed to create remote dirs on {host}: {proc.stderr.decode(errors='replace')}")


def sync_repo(host: str, remote_repo_root: str) -> None:
    prep = (
        f"rm -rf {shlex.quote(remote_repo_root)}/* "
        f"{shlex.quote(remote_repo_root)}/.[!.]* "
        f"{shlex.quote(remote_repo_root)}/..?* 2>/dev/null || true"
    )
    proc = run_ssh(host, prep)
    if proc.returncode != 0:
        raise ProbeError(f"failed to clean remote repo on {host}: {proc.stderr.decode(errors='replace')}")

    file_list = run(["git", "ls-files", "-z"])
    if file_list.returncode != 0:
        raise ProbeError(f"git ls-files failed: {file_list.stderr.decode(errors='replace')}")

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
    remote_cmd = f"tar -xf - -C {shlex.quote(remote_repo_root)}"
    ssh_cmd = f"ssh -o BatchMode=yes {shlex.quote(host)} {shlex.quote(f'bash -lc {shlex.quote(remote_cmd)}')}"
    pipeline = f"COPYFILE_DISABLE=1 COPY_EXTENDED_ATTRIBUTES_DISABLE=1 {shell_join(tar_parts)} | {ssh_cmd}"
    proc = run(["/bin/bash", "-lc", pipeline], input_bytes=file_list.stdout)
    if proc.returncode != 0:
        raise ProbeError(f"repo sync failed for {host}: {proc.stderr.decode(errors='replace')}")


def collect_remote_artifacts(host: str, remote_artifacts_root: str, local_remote_dir: pathlib.Path) -> None:
    local_remote_dir.mkdir(parents=True, exist_ok=True)
    remote_cmd = f"tar -C {shlex.quote(remote_artifacts_root)} -cf - ."
    ssh_cmd = f"ssh -o BatchMode=yes {shlex.quote(host)} {shlex.quote(f'bash -lc {shlex.quote(remote_cmd)}')}"
    pipeline = f"{ssh_cmd} | tar -xf - -C {shlex.quote(str(local_remote_dir))}"
    proc = run(["/bin/bash", "-lc", pipeline])
    if proc.returncode != 0:
        raise ProbeError(f"artifact collection failed for {host}: {proc.stderr.decode(errors='replace')}")


def make_vars(mode: str) -> str:
    vars_map: Dict[str, str] = {
        "CC": "gcc",
        "HOST_CC": "gcc",
        "BUILDMODE": "debug" if mode == "debug" else "mixed",
        "XCFLAGS": "-DLUAJIT_ENABLE_S390X_JIT",
    }
    if mode == "debug":
        vars_map["CCDEBUG"] = "-g3"
        vars_map["CCOPT"] = "-O0"
        vars_map["XCFLAGS"] += " -DLUA_USE_ASSERT"
    return " ".join(f"{key}={shlex.quote(value)}" for key, value in vars_map.items())


def build_command(mode: str) -> str:
    vars_string = make_vars(mode)
    return textwrap.dedent(
        f"""
        set -euo pipefail
        export PATH="$PWD/src:$PATH"
        make -C src clean {vars_string}
        make -C src {vars_string}
        """
    ).strip()


def workload_lua(workload: str, iterations: int, outer: int) -> tuple[str, str, str]:
    if workload == "shape":
        definition = textwrap.dedent(
            """
            local base_array = { 1, 3, 5, 7, 9 }

            local function run_workload(n, outer_loops)
              local total = 0
              for _ = 1, outer_loops do
                for _ = 1, n do
                  for _, value in pairs(base_array) do
                    total = total + value
                  end
                end
              end
              return total
            end
            """
        ).strip()
        return definition, f"{iterations}, {outer}", f"shape:{iterations}x{outer}"
    if workload == "pairs_sum":
        definition = textwrap.dedent(
            """
            local base_table = { a = 1, b = 2, c = 3, d = 4, e = 5 }

            local function run_workload(n)
              local total = 0
              for _ = 1, n do
                for _, value in pairs(base_table) do
                  total = total + value
                end
              end
              return total
            end
            """
        ).strip()
        return definition, f"{iterations}", f"pairs_sum:{iterations}"
    if workload == "pairs_array_sum":
        definition = textwrap.dedent(
            """
            local base_array = { 1, 3, 5, 7, 9 }

            local function run_workload(n)
              local total = 0
              for _ = 1, n do
                for _, value in pairs(base_array) do
                  total = total + value
                end
              end
              return total
            end
            """
        ).strip()
        return definition, f"{iterations}", f"pairs_array_sum:{iterations}"
    raise ProbeError(f"unsupported workload: {workload}")


def iterator_probe_lua(
    hotexit: int,
    workload: str,
    iterations: int,
    outer: int,
    hotloop: int,
    minstitch: int,
    default_jitopt: bool,
) -> str:
    workload_definition, call_args, descriptor = workload_lua(workload, iterations, outer)
    opt_line = ""
    if not default_jitopt:
        opt_line = f'jit.opt.start("hotloop={hotloop}", "hotexit={hotexit}", "minstitch={minstitch}")'
    return textwrap.dedent(
        f"""
        local jit = require("jit")
        local util = require("jit.util")
        local vmdef = require("jit.vmdef")

        local function ir_op_name(ot)
          local idx = math.floor(ot / 256) * 6
          return (string.sub(vmdef.irnames, idx + 1, idx + 6):gsub("%s+$", ""))
        end

        local function emit_trace_event(what, tr, func, pc, otr, oex)
          io.write(string.format(
            "TRACE_EVENT what=%s tr=%d otr=%s oex=%s\\n",
            tostring(what),
            tonumber(tr) or -1,
            tostring(otr),
            tostring(oex)
          ))
        end

        local function emit_texit(tr, ex)
          io.write(string.format("TEXIT tr=%d ex=%d\\n", tonumber(tr) or -1, tonumber(ex) or -1))
        end

        {opt_line}
        jit.attach(emit_trace_event, "trace")
        jit.attach(emit_texit, "texit")

        {workload_definition}

        local function interpreter_result(fn, ...)
          jit.off(fn, true)
          local result = fn(...)
          jit.on(fn, true)
          jit.flush()
          return result
        end

        local expected = interpreter_result(run_workload, {call_args})
        local actual = run_workload({call_args})
        print("WORKLOAD {descriptor}")
        print(string.format("RESULT actual=%d expected=%d", actual, expected))

        local function dump_traces()
          for tr = 1, 16 do
            local info = util.traceinfo(tr)
            if not info then
              break
            end
            print(string.format(
              "TRACEINFO tr=%d link=%s type=%s nins=%d nk=%d nexit=%d",
              tr,
              tostring(info.link),
              tostring(info.linktype),
              tonumber(info.nins) or -1,
              tonumber(info.nk) or -1,
              tonumber(info.nexit) or -1
            ))
            local mcode, addr, loop = util.tracemc(tr)
            if mcode and addr then
              print(string.format(
                "TRACEMC tr=%d addr=0x%x loop=%d size=%d",
                tr,
                addr,
                tonumber(loop) or -1,
                #mcode
              ))
            end
            for ins = 0, info.nins - 1 do
              local mode, ot, op1, op2, prev = util.traceir(tr, ins)
              print(string.format(
                "TRACEIR tr=%d ins=%d op=%s ot=%d mode=%d op1=%d op2=%d prev=%d",
                tr, ins, ir_op_name(ot), ot, mode, op1, op2, prev
              ))
            end
            for sn = 0, info.nexit - 1 do
              local stub = util.traceexitstub(tr, sn)
              if stub then
                print(string.format("EXITSTUB tr=%d ex=%d addr=0x%x", tr, sn, stub))
              end
              local snap = util.tracesnap(tr, sn)
              if snap then
                local parts = {{}}
                for i = 2, #snap do
                  parts[#parts + 1] = tostring(snap[i])
                end
                print(string.format(
                  "TRACESNAP tr=%d sn=%d ref=%d nslots=%d map=%s",
                  tr, sn, snap[0], snap[1], table.concat(parts, ",")
                ))
              end
            end
          end
        end

        jit.off(dump_traces, true)
        dump_traces()

        jit.attach(emit_texit)
        jit.attach(emit_trace_event)
        print("PROBE_DONE")
        """
    ).strip()


def run_remote_step(
    host: str,
    remote_repo_root: str,
    remote_artifacts_root: str,
    step_name: str,
    command: str,
    *,
    timeout_sec: int,
) -> None:
    remote_step_dir = f"{remote_artifacts_root}/{step_name}"
    remote_runner = f"{remote_repo_root}/tools/s390x/remote_run.sh"
    remote_cmd = (
        f"cd {shlex.quote(remote_repo_root)} && "
        f"export S390X_TIMEOUT_SEC={timeout_sec} && "
        f"{shlex.quote(remote_runner)} "
        f"{shlex.quote(step_name)} {shlex.quote(remote_step_dir)} {shlex.quote(remote_repo_root)} {shlex.quote(command)}"
    )
    proc = run_ssh(host, remote_cmd)
    if proc.returncode != 0:
        raise ProbeError(f"remote step {step_name} failed on {host}: {proc.stderr.decode(errors='replace')}")


def parse_trace_stdout(text: str) -> dict:
    result: dict = {"events": [], "texits": [], "traces": {}, "result": None}
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        if (m := TRACE_EVENT_RE.match(line)):
            result["events"].append(
                {
                    "what": m.group("what"),
                    "tr": int(m.group("tr")),
                    "otr": None if m.group("otr") == "nil" else m.group("otr"),
                    "oex": None if m.group("oex") == "nil" else m.group("oex"),
                }
            )
            continue
        if (m := TEXIT_RE.match(line)):
            result["texits"].append({"tr": int(m.group("tr")), "ex": int(m.group("ex"))})
            continue
        if (m := RESULT_RE.match(line)):
            result["result"] = {"actual": int(m.group("actual")), "expected": int(m.group("expected"))}
            continue
        if (m := TRACEINFO_RE.match(line)):
            tr = m.group("tr")
            result["traces"].setdefault(tr, {"ir": [], "snaps": [], "exitstubs": []})
            result["traces"][tr]["info"] = {
                "tr": int(tr),
                "link": m.group("link"),
                "linktype": m.group("linktype"),
                "nins": int(m.group("nins")),
                "nk": int(m.group("nk")),
                "nexit": int(m.group("nexit")),
            }
            continue
        if (m := TRACEMC_RE.match(line)):
            tr = m.group("tr")
            result["traces"].setdefault(tr, {"ir": [], "snaps": [], "exitstubs": []})
            result["traces"][tr]["mcode"] = {
                "addr": m.group("addr"),
                "loop": int(m.group("loop")),
                "size": int(m.group("size")),
            }
            continue
        if (m := TRACEIR_RE.match(line)):
            tr = m.group("tr")
            result["traces"].setdefault(tr, {"ir": [], "snaps": [], "exitstubs": []})
            result["traces"][tr]["ir"].append(
                {
                    "ins": int(m.group("ins")),
                    "op": m.group("op"),
                    "ot": int(m.group("ot")),
                    "mode": int(m.group("mode")),
                    "op1": int(m.group("op1")),
                    "op2": int(m.group("op2")),
                    "prev": int(m.group("prev")),
                }
            )
            continue
        if (m := EXITSTUB_RE.match(line)):
            tr = m.group("tr")
            result["traces"].setdefault(tr, {"ir": [], "snaps": [], "exitstubs": []})
            result["traces"][tr]["exitstubs"].append(
                {
                    "exit": int(m.group("ex")),
                    "addr": m.group("addr"),
                }
            )
            continue
        if (m := TRACESNAP_RE.match(line)):
            tr = m.group("tr")
            result["traces"].setdefault(tr, {"ir": [], "snaps": [], "exitstubs": []})
            snap_map = [part for part in m.group("map").split(",") if part]
            result["traces"][tr]["snaps"].append(
                {
                    "sn": int(m.group("sn")),
                    "ref": int(m.group("ref")),
                    "nslots": int(m.group("nslots")),
                    "map": snap_map,
                }
            )
    return result


def parse_resume_slots(lines: List[str]) -> List[dict]:
    groups: List[dict] = []
    current: Optional[dict] = None
    for raw_line in lines:
        line = raw_line.strip()
        if (m := RESUME_RE.match(line)):
            current = {"pc": m.group("pc"), "op": int(m.group("op")), "slots": []}
            groups.append(current)
            continue
        if current and (m := SLOT_RE.match(line)):
            current["slots"].append(
                {
                    "idx": int(m.group("idx")),
                    "itype": int(m.group("itype")),
                    "u64": f"0x{m.group('u64').lower()}",
                }
            )
    return groups


def classify_path(trace_entry: Optional[dict]) -> str:
    if not trace_entry:
        return "missing"
    ops = [entry["op"] for entry in trace_entry.get("ir", [])]
    if "ADDOV" in ops:
        return "payload"
    if "VLOAD" in ops:
        return "restart"
    return "unknown"


def parse_recstops(lines: List[str]) -> List[dict]:
    recstops: List[dict] = []
    for lineno, raw_line in enumerate(lines, start=1):
        line = raw_line.strip()
        if not (m := RECSTOP_RE.match(line)):
            continue
        recstops.append(
            {
                "line": lineno,
                "trace": int(m.group("trace")),
                "parent": int(m.group("parent")),
                "exit": int(m.group("exit")),
                "pc": m.group("pc"),
                "op": int(m.group("op")),
                "prevop": int(m.group("prevop")),
                "startop": int(m.group("startop")),
                "linktype": int(m.group("linktype")),
                "link": int(m.group("link")),
                "lnkop": int(m.group("lnkop")),
                "root": int(m.group("root")),
                "framedepth": int(m.group("framedepth")),
                "retdepth": int(m.group("retdepth")),
            }
        )
    return recstops


def build_texit_histogram(parsed: dict) -> List[dict]:
    counts: Dict[tuple[int, int], int] = {}
    for entry in parsed["texits"]:
        key = (entry["tr"], entry["ex"])
        counts[key] = counts.get(key, 0) + 1
    histogram = [
        {"trace": trace, "exit": exit_no, "count": count}
        for (trace, exit_no), count in counts.items()
    ]
    histogram.sort(key=lambda entry: (-entry["count"], entry["trace"], entry["exit"]))
    return histogram


def build_trace_event_counts(parsed: dict) -> List[dict]:
    counts: Dict[tuple[str, int, Optional[str], Optional[str]], int] = {}
    for entry in parsed["events"]:
        key = (entry["what"], entry["tr"], entry.get("otr"), entry.get("oex"))
        counts[key] = counts.get(key, 0) + 1
    summary = [
        {
            "what": what,
            "tr": tr,
            "otr": otr,
            "oex": oex,
            "count": count,
        }
        for (what, tr, otr, oex), count in counts.items()
    ]
    summary.sort(key=lambda entry: (-entry["count"], entry["what"], entry["tr"]))
    return summary


def trace_segment(recstops: List[dict], trace: int) -> tuple[Optional[int], Optional[int]]:
    for idx, recstop in enumerate(recstops):
        if recstop["trace"] != trace:
            continue
        start = recstop["line"]
        end = None if idx + 1 >= len(recstops) else recstops[idx + 1]["line"]
        return start, end
    return None, None


def in_segment(entry: dict, start: Optional[int], end: Optional[int]) -> bool:
    line = entry.get("line")
    if line is None or start is None:
        return False
    if line < start:
        return False
    if end is not None and line >= end:
        return False
    return True


def dedupe_entries(entries: List[dict], keys: List[str]) -> List[dict]:
    seen = set()
    result = []
    for entry in entries:
        marker = tuple(entry.get(key) for key in keys)
        if marker in seen:
            continue
        seen.add(marker)
        result.append(entry)
    return result


def build_exit_guard_summary(
    parsed: dict,
    recstops: List[dict],
    guard_stubs: List[dict],
    guard_kinds: List[dict],
    exits: List[dict],
    exit_snaps: List[dict],
    trace_no: int,
    exit_no: int,
) -> dict:
    trace_start, trace_end = trace_segment(recstops, trace_no)
    trace_guard_stubs = [entry for entry in guard_stubs if in_segment(entry, trace_start, trace_end)]
    trace_guard_kinds = [entry for entry in guard_kinds if in_segment(entry, trace_start, trace_end)]
    exit_guard_stubs = [entry for entry in trace_guard_stubs if entry["snap"] == exit_no]
    exit_guard_stubs = dedupe_entries(
        exit_guard_stubs,
        ["curins", "snap", "loopsnap", "cc", "patchpoint", "target"],
    )
    runtime_exits = [entry for entry in exits if entry["trace"] == trace_no and entry["exit"] == exit_no]
    runtime_snaps = [entry for entry in exit_snaps if entry["trace"] == trace_no and entry["exit"] == exit_no]
    trace_ir = parsed["traces"].get(str(trace_no), {}).get("ir", [])
    trace_ir_by_ins = {entry["ins"]: entry for entry in trace_ir}
    guard_sequence = []
    for curins in [entry["curins"] for entry in exit_guard_stubs]:
        guard_sequence.append(
            {
                "curins": curins,
                "trace_ir": trace_ir_by_ins.get(curins),
                "guard_kinds": dedupe_entries(
                    [entry for entry in trace_guard_kinds if entry["curins"] == curins],
                    ["kind", "curins", "ir", "op", "type", "cc", "ofs", "extra"],
                ),
            }
        )
    return {
        "trace": trace_no,
        "exit": exit_no,
        "trace_segment": {"start_line": trace_start, "end_line": trace_end},
        "guard_stubs": exit_guard_stubs,
        "guard_kinds_by_curins": {
            str(entry["curins"]): dedupe_entries(
                [kind for kind in trace_guard_kinds if kind["curins"] == entry["curins"]],
                ["kind", "curins", "ir", "op", "type", "cc", "ofs", "extra"],
            )
            for entry in exit_guard_stubs
        },
        "guard_sequence": guard_sequence,
        "trace_ir_by_ins": {str(key): value for key, value in trace_ir_by_ins.items()},
        "runtime_exits": runtime_exits,
        "runtime_snaps": runtime_snaps,
    }


def normalize_probe(stdout_text: str, stderr_text: str) -> dict:
    parsed = parse_trace_stdout(stdout_text)
    stderr_lines = stderr_text.splitlines()

    guard_stubs = []
    guard_kinds = []
    recstops = parse_recstops(stderr_lines)
    exits = []
    exit_snaps = []
    for lineno, raw_line in enumerate(stderr_lines, start=1):
        line = raw_line.strip()
        if (m := GUARD_STUB_RE.match(line)):
            guard_stubs.append(
                {
                    "line": lineno,
                    "curins": int(m.group("curins")),
                    "snap": int(m.group("snap")),
                    "loopsnap": int(m.group("loopsnap")),
                    "cc": int(m.group("cc")),
                    "loopinv": int(m.group("loopinv")),
                    "patchpoint": m.group("patchpoint"),
                    "target": m.group("target"),
                    "invmcp": m.group("invmcp"),
                }
            )
            continue
        if (m := GUARD_KIND_RE.match(line)):
            guard_kinds.append(
                {
                    "line": lineno,
                    "kind": m.group("kind"),
                    "curins": int(m.group("curins")),
                    "ir": int(m.group("ir")),
                    "op": int(m.group("op")),
                    "type": int(m.group("type")),
                    "cc": int(m.group("cc")),
                    "ofs": int(m.group("ofs")),
                    "extra": int(m.group("extra")),
                }
            )
            continue
        if (m := EXIT_RE.match(line)):
            exits.append(
                {
                    "line": lineno,
                    "phase": m.group("phase"),
                    "trace": int(m.group("trace")),
                    "exit": int(m.group("exit")),
                    "op": int(m.group("op")),
                    "snapcount": int(m.group("snapcount")),
                    "snapref": int(m.group("snapref")),
                    "snapnent": int(m.group("snapnent")),
                }
            )
            continue
        if (m := EXIT_SNAP_RE.match(line)):
            exit_snaps.append(
                {
                    "line": lineno,
                    "trace": int(m.group("trace")),
                    "exit": int(m.group("exit")),
                    "snappc": m.group("snappc"),
                    "snapop": int(m.group("snapop")),
                    "raw": m.group("rest").strip(),
                }
            )

    first_child = next(
        (
            entry
            for entry in parsed["events"]
            if entry["what"] == "start"
            and str(entry.get("otr")) == "1"
            and str(entry.get("oex")) == "1"
        ),
        None,
    )
    first_child_trace = None if not first_child else parsed["traces"].get(str(first_child["tr"]))
    root_chain_length = sum(
        1
        for trace in parsed["traces"].values()
        if trace.get("info", {}).get("linktype") == "root" and trace.get("info", {}).get("link") == "1"
    )

    root_exit_1_summary = build_exit_guard_summary(
        parsed, recstops, guard_stubs, guard_kinds, exits, exit_snaps, 1, 1
    )
    first_child_exit_1_summary = None
    if first_child and str(first_child["tr"]).isdigit():
        first_child_exit_1_summary = build_exit_guard_summary(
            parsed,
            recstops,
            guard_stubs,
            guard_kinds,
            exits,
            exit_snaps,
            int(first_child["tr"]),
            1,
        )

    return {
        "result": parsed["result"],
        "trace_events": parsed["events"],
        "trace_event_counts": build_trace_event_counts(parsed),
        "texits": parsed["texits"],
        "texit_histogram": build_texit_histogram(parsed),
        "traces": parsed["traces"],
        "trace_start_stop": parsed["events"],
        "trace_recstops": recstops,
        "trace_count": len(parsed["traces"]),
        "root_chain_length": root_chain_length,
        "root_exit_1_guards": {
            **root_exit_1_summary,
            "first_child_trace": None
            if not first_child
            else {
                "event": first_child,
                "classification": classify_path(first_child_trace),
                "ir_ops": [entry["op"] for entry in (first_child_trace or {}).get("ir", [])],
            },
        },
        "first_child_exit_1_guards": first_child_exit_1_summary,
        "resume_slots": parse_resume_slots(stderr_lines),
    }


def write_json(path: pathlib.Path, payload: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def build_host(host: str, remote_repo_root: str, remote_artifacts_root: str, mode: str) -> None:
    run_remote_step(
        host,
        remote_repo_root,
        remote_artifacts_root,
        f"iterator-build-{mode}",
        build_command(mode),
        timeout_sec=1800,
    )


def probe_host(
    host: str,
    remote_repo_root: str,
    remote_artifacts_root: str,
    hotexit_values: List[int],
    workload: str,
    iterations: int,
    outer: int,
    hotloop: int,
    minstitch: int,
    default_jitopt: bool,
    extra_env: List[str],
    jit_dump_mode: str,
) -> None:
    env_keys = merged_debug_env_keys(extra_env)
    for hotexit in hotexit_values:
        env_lines = [f"export {key}=1" for key in env_keys]
        lua = iterator_probe_lua(hotexit, workload, iterations, outer, hotloop, minstitch, default_jitopt)
        luajit_cmd = "./src/luajit"
        if jit_dump_mode:
            dump_spec = f"{jit_dump_mode},$S390X_STEP_DIR/jit-dump.log"
            luajit_cmd = f"./src/luajit -jdump={dump_spec}"
        command = "\n".join(
            [
                "set -euo pipefail",
                *env_lines,
                "export LUA_PATH=\"$PWD/src/?.lua;$PWD/src/?/?.lua;;\"",
                "cat > \"$S390X_STEP_DIR/iterator_probe.lua\" <<'LUA'",
                lua,
                "LUA",
                f"{luajit_cmd} \"$S390X_STEP_DIR/iterator_probe.lua\"",
            ]
        )
        run_remote_step(
            host,
            remote_repo_root,
            remote_artifacts_root,
            f"iterator-probe-hotexit-{hotexit}",
            command,
            timeout_sec=600,
        )


def local_run_id() -> str:
    now = dt.datetime.now(dt.timezone.utc)
    return f"{now.strftime('%Y%m%dT%H%M%S.%fZ')}-iterator-probe-p{os.getpid()}"


def merged_debug_env_keys(extra_env: Iterable[str]) -> List[str]:
    merged: List[str] = []
    seen = set()
    for key in [*DEBUG_ENV_KEYS, *extra_env]:
        if key in seen:
            continue
        seen.add(key)
        merged.append(key)
    return merged


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--hosts", nargs="+", default=list(HOSTS), choices=HOSTS)
    parser.add_argument("--mode", default="debug", choices=("debug", "release"))
    parser.add_argument("--hotexit", nargs="+", type=int, default=DEFAULT_HOTEXIT)
    parser.add_argument("--hotloop", type=int, default=2)
    parser.add_argument("--minstitch", type=int, default=1)
    parser.add_argument("--default-jitopt", action="store_true")
    parser.add_argument("--workload", default="shape", choices=WORKLOADS)
    parser.add_argument("--iterations", type=int, default=2)
    parser.add_argument("--outer", type=int, default=2)
    parser.add_argument(
        "--extra-env",
        nargs="*",
        default=[],
        help="Additional LUAJIT_S390X_* debug env vars to export during the probe run.",
    )
    parser.add_argument(
        "--jit-dump-mode",
        default="",
        help="Optional jit.dump mode string, written to $S390X_STEP_DIR/jit-dump.log.",
    )
    parser.add_argument("--run-id", default="auto")
    parser.add_argument("--keep-remote", action="store_true")
    parser.add_argument(
        "--summarize-existing",
        help="Re-summarize an existing probe dir under artifacts/s390x/<run-id>/probe or an absolute probe path.",
    )
    return parser.parse_args()


def summarize_host(host_dir: pathlib.Path, hotexit_values: List[int]) -> None:
    summary: Dict[str, dict] = {}
    for hotexit in hotexit_values:
        preset_dir = host_dir / f"hotexit-{hotexit}"
        stderr_text = (preset_dir / "raw-stderr.log").read_text(encoding="utf-8", errors="replace")
        stdout_text = (preset_dir / "raw-stdout.log").read_text(encoding="utf-8", errors="replace")
        normalized = normalize_probe(stdout_text, stderr_text)
        write_json(preset_dir / "trace-start-stop.json", normalized["trace_start_stop"])
        write_json(preset_dir / "trace-event-counts.json", normalized["trace_event_counts"])
        write_json(preset_dir / "texit-histogram.json", normalized["texit_histogram"])
        write_json(preset_dir / "root-exit-1-guards.json", normalized["root_exit_1_guards"])
        write_json(preset_dir / "resume-slots.json", normalized["resume_slots"])
        write_json(
            preset_dir / "summary.json",
            {
                "result": normalized["result"],
                "first_child_trace": normalized["root_exit_1_guards"]["first_child_trace"],
                "runtime_exit_1": normalized["root_exit_1_guards"]["runtime_exits"],
                "trace_count": normalized["trace_count"],
                "root_chain_length": normalized["root_chain_length"],
                "root_exit_1_guard_sequence": normalized["root_exit_1_guards"]["guard_sequence"],
                "first_child_exit_1_guard_sequence": None
                if not normalized["first_child_exit_1_guards"]
                else normalized["first_child_exit_1_guards"]["guard_sequence"],
                "texit_histogram": normalized["texit_histogram"],
                "trace_event_counts": normalized["trace_event_counts"],
            },
        )
        summary[str(hotexit)] = {
            "result": normalized["result"],
            "first_child_trace": normalized["root_exit_1_guards"]["first_child_trace"],
            "trace_count": normalized["trace_count"],
            "root_chain_length": normalized["root_chain_length"],
            "top_texits": normalized["texit_histogram"][:5],
            "root_exit_1_guard_curins": [entry["curins"] for entry in normalized["root_exit_1_guards"]["guard_stubs"]],
            "root_exit_1_guard_ops": [
                {
                    "curins": entry["curins"],
                    "op": None if not entry["trace_ir"] else entry["trace_ir"]["op"],
                }
                for entry in normalized["root_exit_1_guards"]["guard_sequence"]
            ],
        }
    write_json(host_dir / "matrix-summary.json", summary)


def main() -> int:
    args = parse_args()
    if args.summarize_existing:
        probe_dir = pathlib.Path(args.summarize_existing)
        if not probe_dir.is_absolute():
            probe_dir = ARTIFACTS_ROOT / args.summarize_existing / "probe"
        if not probe_dir.exists():
            print(f"[iterator-probe] missing probe dir: {probe_dir}", file=sys.stderr)
            return 1
        for host in args.hosts:
            host_dir = probe_dir / host
            if not host_dir.exists():
                continue
            summarize_host(host_dir, args.hotexit)
        print(f"[iterator-probe] re-summarized: {probe_dir}", flush=True)
        return 0

    run_id = local_run_id() if args.run_id == "auto" else args.run_id
    local_run_dir = ARTIFACTS_ROOT / run_id
    local_remote_dir = local_run_dir / "remote"
    normalized_dir = local_run_dir / "probe"
    local_run_dir.mkdir(parents=True, exist_ok=False)
    try:
        for host in args.hosts:
            remote_run_root = f"{REMOTE_BASE}/{run_id}-{host}"
            remote_repo_root = f"{remote_run_root}/{REMOTE_REPO_NAME}"
            remote_artifacts_root = f"{remote_run_root}/{REMOTE_ARTIFACTS_NAME}"

            print(f"[iterator-probe] syncing {host}", flush=True)
            ensure_remote_dirs(host, remote_run_root, remote_repo_root, remote_artifacts_root)
            sync_repo(host, remote_repo_root)

            print(f"[iterator-probe] building {host} ({args.mode})", flush=True)
            build_host(host, remote_repo_root, remote_artifacts_root, args.mode)

            print(
                f"[iterator-probe] probing {host} workload={args.workload} "
                f"iterations={args.iterations} outer={args.outer} "
                f"hotexit presets {args.hotexit}",
                flush=True,
            )
            probe_host(
                host,
                remote_repo_root,
                remote_artifacts_root,
                args.hotexit,
                args.workload,
                args.iterations,
                args.outer,
                args.hotloop,
                args.minstitch,
                args.default_jitopt,
                args.extra_env,
                args.jit_dump_mode,
            )

            host_remote_dir = local_remote_dir / host
            collect_remote_artifacts(host, remote_artifacts_root, host_remote_dir)

            host_out_dir = normalized_dir / host
            host_out_dir.mkdir(parents=True, exist_ok=True)
            for hotexit in args.hotexit:
                step_dir = host_remote_dir / f"iterator-probe-hotexit-{hotexit}"
                preset_dir = host_out_dir / f"hotexit-{hotexit}"
                preset_dir.mkdir(parents=True, exist_ok=True)
                shutil.copy2(step_dir / "stdout.log", preset_dir / "raw-stdout.log")
                shutil.copy2(step_dir / "stderr.log", preset_dir / "raw-stderr.log")
                shutil.copy2(step_dir / "metadata.json", preset_dir / "metadata.json")
                shutil.copy2(step_dir / "command.txt", preset_dir / "command.txt")
                jit_dump_file = step_dir / "jit-dump.log"
                if jit_dump_file.exists():
                    shutil.copy2(jit_dump_file, preset_dir / "jit-dump.log")

            summarize_host(host_out_dir, args.hotexit)

            if not args.keep_remote:
                run_ssh(host, f"rm -rf {shlex.quote(remote_run_root)}")

        print(f"[iterator-probe] artifacts: {normalized_dir}", flush=True)
        return 0
    except ProbeError as exc:
        print(f"[iterator-probe] error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
