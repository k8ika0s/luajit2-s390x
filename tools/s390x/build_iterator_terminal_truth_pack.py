#!/usr/bin/env python3
"""Build the official iterator terminal-leave truth pack.

This helper is intentionally non-mutating.  It compares the retained semantic
iterator fold with the raw terminal-restart mode exposed by disabling that
fold, then captures the trace/snapshot/JLOOP evidence needed to decide whether
a future source change has a real terminal-state contract to implement.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import pathlib
import re
import shlex
import sys

THIS_DIR = pathlib.Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

import restamp_iterator_perf as restamp


ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT_ROOT = ROOT / "artifacts" / "s390x" / "truth-packs"
OFFICIAL_BENCH = "tests/s390x/perf/iterator_table.lua"
TERMINAL_DISABLE_ENV = {
    "LUAJIT_S390X_DISABLE_ITERATOR_TABLE_LOOP_FOLD": "1",
}
TERMINAL_FOCUS_ENV = {
    "LUAJIT_S390X_RECSTOP_LOG": "1",
    "LUAJIT_S390X_TRACE_META_LOG": "1",
    "LUAJIT_S390X_TRACE_START_LOG": "1",
    "LUAJIT_S390X_TRACE_ABORT_LOG": "1",
    "LUAJIT_S390X_SIDE_FOCUS": "1",
    "LUAJIT_S390X_SIDE_FOCUS_PARENT": "2",
    "LUAJIT_S390X_SIDE_FOCUS_EXIT": "1",
    "LUAJIT_S390X_ITERN_FOCUS": "1",
    "LUAJIT_S390X_ITERN_FOCUS_PARENT": "2",
    "LUAJIT_S390X_ITERN_FOCUS_EXIT": "1",
    "LUAJIT_S390X_JLOOP_EXIT_LOG": "1",
    "LUAJIT_S390X_JLOOP_EXIT_PARENT": "2",
    "LUAJIT_S390X_JLOOP_EXIT_EXIT": "1",
}

PERF_RE = re.compile(r"^PERF ([^ ]+) median=([0-9.]+) p95=([0-9.]+)")
KV_RE = re.compile(r"^([A-Z_]+)\s+(.+)$")
TRACEINFO_RE = re.compile(
    r"^TRACEINFO tr=(\d+) link=([^ ]+) linktype=([^ ]+) nins=(\d+) nk=(\d+) nexit=(\d+)"
)
TRACEMC_RE = re.compile(r"^TRACEMC tr=(\d+) addr=([^ ]+) loop=(-?\d+) size=(\d+)")
TRACESNAP_RE = re.compile(r"^TRACESNAP tr=(\d+) ex=(\d+) ref=(-?\d+) nslots=(-?\d+) map=(.*)$")
TRACEIR_RE = re.compile(
    r"^TRACEIR tr=(\d+) ins=(\d+)(?: op=([^ ]+))? ot=(-?\d+) mode=(-?\d+) "
    r"op1=(-?\d+) op2=(-?\d+) prev=(-?\d+)$"
)
TRACE_META_RE = re.compile(
    r"^S390X_TRACE_META phase=([^ ]+) trace=(\d+) parent=(\d+) exit=(\d+) root=(\d+) "
    r"link=(\d+) linktype=(\d+) .*?resumechild=(\d+) .*?startpc=([^ ]+) startop=(\d+) "
    r".*?nsnap=(\d+) .*?nins=(\d+) .*?mcloop=(\d+)"
)
TRACE_META_SNAP_RE = re.compile(
    r"^S390X_TRACE_META_SNAP phase=([^ ]+) trace=(\d+) snap=(\d+) .*?nent=(\d+) "
    r".*?nslots=(\d+) .*?pc=([^ ]+) op=(\d+)"
)
JLOOP_EXIT_RE = re.compile(
    r"^S390X_JLOOP_EXIT parent=(\d+) exit=(\d+) pc=([^ ]+) op=(\d+) target=(\d+) "
    r"target_exec=(\d+) .*?retop=(\d+) trace=(\d+) link=(\d+) linktype=(\d+) "
    r".*?target_root=(\d+) target_link=(\d+) target_linktype=(\d+) "
    r"target_startpc=([^ ]+) target_startop=(\d+) target_resumepc=([^ ]+) "
    r"target_resumeop=(\d+) target_resumevalid=(\d+) target_resumechild=(\d+) "
    r"target_mcloop=(\d+) .*?state=(\d+)"
)
JLOOP_PHASE_RE = re.compile(
    r"^S390X_JLOOP_EXIT phase=([^ ]+) parent=(\d+) exit=(\d+) trace=(\d+) retop=(\d+) state=(\d+)"
)
TRACE_ABORT_META_RE = re.compile(
    r"^S390X_TRACE_ABORT trace=(\d+) parent=(\d+) exit=(\d+) state=(\d+) pc=([^ ]+) "
    r"op=(\d+) startpc=([^ ]+) startop=(\d+) err=(\d+) root=(\d+) link=(\d+) "
    r"linktype=(\d+) resumepc=([^ ]+) resumeop=(\d+) resumevalid=(\d+)"
)
ITERN_FOCUS_RE = re.compile(
    r"^S390X_ITERN_FOCUS site=([^ ]+) trace=(\d+) parent=(\d+) exit=(\d+) "
    r"startpc=([^ ]+) pc=([^ ]+) op=(\d+) startop=(\d+) nextt=(\d+) "
    r"keyflags=([^ ]+) numkey=(\d+) key_nil=(\d+) idxchain=(\d+) "
    r"mobj_ref=(-?\d+) key_ref=(-?\d+) val_ref=(-?\d+) "
    r"tab_u64=([^ ]+) ctrl_u64=([^ ]+) key_u64=([^ ]+) val_u64=([^ ]+) "
    r"ix_keyv=([^ ]+) ix_tabv=([^ ]+) tab_asize=(\d+) tab_hmask=(\d+)"
)
LLEAVE_RE = re.compile(
    r"^S390X_LLEAVE site=([^ ]+) trace=(\d+) parent=(\d+) exit=(\d+) "
    r"pc=([^ ]+) op=(\d+) startop=(\d+) framedepth=(\d+) retdepth=(\d+) "
    r"parent_root=(\d+) parent_linktype=(\d+) parent_snapcount=(\d+) parent_snapnent=(\d+)"
)
EXIT_RE = re.compile(
    r"^S390X_EXIT phase=([^ ]+) trace=(\d+) exit=(\d+) pc=([^ ]+) op=(\d+) "
    r"snapcount=(\d+) snapref=(\d+) snapnent=(\d+) state=(\d+) .*"
)
EXIT_SNAP_RE = re.compile(
    r"^S390X_EXIT_SNAP trace=(\d+) exit=(\d+) snappc=([^ ]+) snapop=(\d+)(.*)$"
)
SLOTS_RE = re.compile(r"^S390X_SLOTS pc=([^ ]+) op=(\d+) base=([^ ]+)$")
SLOT_RE = re.compile(r"^S390X_SLOT idx=(\d+) itype=(-?\d+) u64=(0x[0-9a-fA-F]+)$")

REF_BIAS = 0x8000
SNAP_FLAGS = {
    0x01: "FRAME",
    0x02: "CONT",
    0x04: "NORESTORE",
    0x08: "SOFTFPNUM",
    0x10: "KEYINDEX",
}


def trace_error_names() -> list[str]:
    names: list[str] = []
    traceerr = ROOT / "src" / "lj_traceerr.h"
    for line in traceerr.read_text(encoding="utf-8").splitlines():
        match = re.match(r"\s*TREDEF\(([^,\s]+),", line)
        if match:
            names.append("LJ_TRERR_" + match.group(1))
    return names


TRACE_ERROR_NAMES = trace_error_names()


def trace_error_name(err: int) -> str:
    if 0 <= err < len(TRACE_ERROR_NAMES):
        return TRACE_ERROR_NAMES[err]
    return f"LJ_TRERR_UNKNOWN_{err}"


OFFICIAL_CAPTURE_SCRIPT = r"""
local jit = require("jit")
local util = require("jit.util")
local vmdef = require("jit.vmdef")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()

local trace_events = {}
local texit_events = {}

local function trace_hook(what, tr, otr, oex)
  trace_events[#trace_events + 1] = { tostring(what), tonumber(tr), tostring(otr), tostring(oex) }
end

local function texit_hook(tr, ex)
  texit_events[#texit_events + 1] = { tonumber(tr), tonumber(ex) }
end

local function emit_hist(label, events, keyfn)
  local hist = {}
  for i = 1, #events do
    local key = keyfn(events[i])
    if key then hist[key] = (hist[key] or 0) + 1 end
  end
  local keys = {}
  for key in pairs(hist) do keys[#keys + 1] = key end
  table.sort(keys)
  local parts = {}
  for i = 1, #keys do
    local key = keys[i]
    parts[#parts + 1] = key .. "=" .. hist[key]
  end
  print(label, #parts == 0 and "(none)" or table.concat(parts, ","))
end

local function ir_op_name(ot)
  local idx = math.floor(tonumber(ot) / 256) * 6
  return (string.sub(vmdef.irnames, idx + 1, idx + 6):gsub("%s+$", ""))
end

local function emit_trace_dump(limit)
  limit = limit or 128
  for tr = 1, limit do
    local info = util.traceinfo(tr)
    if info then
      print(string.format(
        "TRACEINFO tr=%d link=%s linktype=%s nins=%d nk=%d nexit=%d",
        tr, tostring(info.link), tostring(info.linktype), tonumber(info.nins) or -1,
        tonumber(info.nk) or -1, tonumber(info.nexit) or -1))
      local mcode, addr, loop = util.tracemc(tr)
      if mcode and addr then
        print(string.format("TRACEMC tr=%d addr=0x%x loop=%d size=%d",
          tr, addr, tonumber(loop) or -1, #mcode))
      end
      for ex = 0, (tonumber(info.nexit) or 0) - 1 do
        local snap = util.tracesnap(tr, ex)
        if snap then
          local parts = {}
          for i = 2, #snap do parts[#parts + 1] = tostring(snap[i]) end
          print(string.format("TRACESNAP tr=%d ex=%d ref=%d nslots=%d map=%s",
            tr, ex, snap[0] or -1, snap[1] or -1, table.concat(parts, ",")))
        end
      end
      if tr <= 4 then
        for ins = 0, math.min((tonumber(info.nins) or 0) - 1, 40) do
          local ok, mode, ot, op1, op2, prev = pcall(util.traceir, tr, ins)
          if ok and mode then
            print(string.format(
              "TRACEIR tr=%d ins=%d op=%s ot=%d mode=%d op1=%d op2=%d prev=%d",
              tr, ins, ir_op_name(ot), tonumber(ot) or -1, tonumber(mode) or -1,
              tonumber(op1) or -1, tonumber(op2) or -1, tonumber(prev) or -1))
          end
        end
      end
    end
  end
end

jit.attach(trace_hook, "trace")
jit.attach(texit_hook, "texit")
local ok, err = pcall(dofile, "tests/s390x/perf/iterator_table.lua")
jit.attach(trace_hook)
jit.attach(texit_hook)

print("OFFICIAL_STATUS", ok and "ok" or "error", ok and "" or tostring(err))
print("TRACE_START", testlib.count_trace_events(trace_events, "start"))
print("TRACE_STOP", testlib.count_trace_events(trace_events, "stop"))
print("TRACE_ABORT", testlib.count_trace_events(trace_events, "abort"))
print("TRACE_TOTAL", #trace_events)
print("TEXIT_COUNT", #texit_events)
emit_hist("TRACE_HIST", trace_events, function(ev)
  return ev[1] .. ":" .. tostring(ev[2]) .. ":" .. ev[3] .. ":" .. ev[4]
end)
emit_hist("TEXIT_HIST", texit_events, function(ev)
  if ev[1] and ev[2] then return tostring(ev[1]) .. ":" .. tostring(ev[2]) end
end)
for i = 1, math.min(#trace_events, 96) do
  local ev = trace_events[i]
  print(string.format("TRACE_EVENT what=%s tr=%s otr=%s oex=%s", ev[1], tostring(ev[2]), ev[3], ev[4]))
end
emit_trace_dump(128)
if not ok then error(err, 0) end
"""


class TerminalTruthError(restamp.RestampError):
    """Iterator terminal truth-pack failure."""


def write_text(path: pathlib.Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def write_json(path: pathlib.Path, payload: object) -> None:
    write_text(path, json.dumps(payload, indent=2, sort_keys=True) + "\n")


def terminal_optout_env() -> dict[str, str]:
    env = dict(restamp.RETAINED_BASELINE_ENV)
    env.update(TERMINAL_DISABLE_ENV)
    return env


def parse_extra_env(values: list[str] | None) -> dict[str, str]:
    env: dict[str, str] = {}
    for item in values or []:
        if "=" not in item:
            raise TerminalTruthError(f"bad --extra-env value {item!r}; expected KEY=VALUE")
        key, value = item.split("=", 1)
        if not key:
            raise TerminalTruthError(f"bad --extra-env value {item!r}; empty key")
        env[key] = value
    return env


def env_prefix(env: dict[str, str]) -> str:
    return "env " + " ".join(f"{key}={shlex.quote(value)}" for key, value in sorted(env.items()))


def prepare_terminal_script(host: str, remote_tmp: str) -> None:
    script = "\n".join(
        [
            "set -euo pipefail",
            f"cat > {shlex.quote(remote_tmp + '/iterator_terminal_capture.lua')} <<'EOF'",
            OFFICIAL_CAPTURE_SCRIPT.rstrip(),
            "EOF",
        ]
    ) + "\n"
    proc = restamp.run_ssh_script(host, script)
    restamp.require_ok(proc, f"{host} iterator terminal script setup")


def run_mode(
    *,
    host: str,
    repo: str,
    remote_tmp: str,
    raw_dir: pathlib.Path,
    mode: str,
    env: dict[str, str],
    samples: int,
    warmup: int,
    pin_core: int | None,
    stderr_line_cap: int,
    timeout_sec: int,
) -> None:
    taskset = f"taskset -c {pin_core} " if pin_core is not None else ""
    remote_stdout = f"{remote_tmp}/{mode}.stdout"
    remote_stderr = f"{remote_tmp}/{mode}.stderr.raw"
    script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
rm -f {shlex.quote(remote_stdout)} {shlex.quote(remote_stderr)}
set +e
timeout {timeout_sec}s {env_prefix(env)} S390X_PERF_SAMPLES={samples} S390X_PERF_WARMUP={warmup} S390X_PERF_BENCH_FILE={shlex.quote(OFFICIAL_BENCH)} {taskset}./src/luajit {shlex.quote(remote_tmp + '/iterator_terminal_capture.lua')} >{shlex.quote(remote_stdout)} 2>{shlex.quote(remote_stderr)}
rc=$?
cat {shlex.quote(remote_stdout)}
awk -v cap={stderr_line_cap} 'BEGIN {{ n=0 }} /^S390X_/ {{ print; n++; if (n >= cap) {{ print "S390X_LOG_CAP reached " cap; exit }} }}' {shlex.quote(remote_stderr)} >&2
grep -v '^S390X_' {shlex.quote(remote_stderr)} | head -n 200 >&2 || true
exit "$rc"
"""
    restamp.run_remote_command(
        host,
        script,
        stdout_path=raw_dir / f"{mode}.stdout.log",
        stderr_path=raw_dir / f"{mode}.stderr.log",
        label=f"{host} iterator terminal {mode}",
    )


def run_bytecode(host: str, repo: str, raw_dir: pathlib.Path) -> None:
    script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
./src/luajit -bl {shlex.quote(OFFICIAL_BENCH)}
"""
    restamp.run_remote_command(
        host,
        script,
        stdout_path=raw_dir / "bytecode.stdout.log",
        stderr_path=raw_dir / "bytecode.stderr.log",
        label=f"{host} iterator bytecode",
    )


def parse_stdout(path: pathlib.Path) -> dict[str, object]:
    result: dict[str, object] = {
        "perf": {},
        "kv": {},
        "traceinfo": {},
        "tracemc": {},
        "tracesnap": [],
        "traceir": {},
    }
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line:
            continue
        match = PERF_RE.match(line)
        if match:
            result["perf"][match.group(1)] = {
                "median": float(match.group(2)),
                "p95": float(match.group(3)),
            }
            continue
        match = KV_RE.match(line)
        if match and match.group(1) in {
            "OFFICIAL_STATUS", "TRACE_START", "TRACE_STOP", "TRACE_ABORT",
            "TRACE_TOTAL", "TEXIT_COUNT", "TRACE_HIST", "TEXIT_HIST",
        }:
            value = match.group(2).strip()
            if value.isdigit():
                result["kv"][match.group(1)] = int(value)
            else:
                result["kv"][match.group(1)] = value
            continue
        match = TRACEINFO_RE.match(line)
        if match:
            result["traceinfo"][match.group(1)] = {
                "link": match.group(2),
                "linktype": match.group(3),
                "nins": int(match.group(4)),
                "nk": int(match.group(5)),
                "nexit": int(match.group(6)),
            }
            continue
        match = TRACEMC_RE.match(line)
        if match:
            result["tracemc"][match.group(1)] = {
                "addr": match.group(2),
                "loop": int(match.group(3)),
                "size": int(match.group(4)),
            }
            continue
        match = TRACESNAP_RE.match(line)
        if match:
            result["tracesnap"].append(
                {
                    "trace": int(match.group(1)),
                    "exit": int(match.group(2)),
                    "ref": int(match.group(3)),
                    "nslots": int(match.group(4)),
                    "map": match.group(5),
                }
            )
            continue
        match = TRACEIR_RE.match(line)
        if match:
            trace = int(match.group(1))
            ins = int(match.group(2))
            result["traceir"].setdefault(str(trace), {})[str(ins)] = {
                "trace": trace,
                "ins": ins,
                "op": match.group(3) or "",
                "ot": int(match.group(4)),
                "mode": int(match.group(5)),
                "op1": int(match.group(6)),
                "op2": int(match.group(7)),
                "prev": int(match.group(8)),
            }
            continue
    decode_tracesnaps(result)
    return result


def decode_snap_entry(raw_value: int, traceir: dict[str, object]) -> dict[str, object]:
    value = raw_value & 0xffffffff
    slot = (value >> 24) & 0xff
    flags_value = (value >> 16) & 0xff
    ref = value & 0xffff
    irref = ref - REF_BIAS
    flags = [
        name for bit, name in sorted(SNAP_FLAGS.items())
        if flags_value & bit
    ]
    ir = traceir.get(str(irref)) if irref >= 0 else None
    decoded: dict[str, object] = {
        "raw": raw_value,
        "hex": f"0x{value:08x}",
        "slot": slot,
        "flags": flags,
        "ref": ref,
        "irref": irref,
    }
    if slot == 255 and ref == 0 and flags_value == 0:
        decoded["kind"] = "SNAP_PC_OR_SENTINEL"
    elif ref == REF_BIAS - 1:
        decoded["kind"] = "REF_NIL"
    elif ref == REF_BIAS - 2:
        decoded["kind"] = "REF_FALSE"
    elif ref == REF_BIAS - 3:
        decoded["kind"] = "REF_TRUE"
    elif ir is not None:
        decoded["ir"] = ir
    return decoded


def decode_tracesnaps(result: dict[str, object]) -> None:
    traceir_by_trace = result.get("traceir", {})
    for snap in result.get("tracesnap", []):
        values: list[int] = []
        for item in str(snap.get("map", "")).split(","):
            item = item.strip()
            if not item:
                continue
            try:
                values.append(int(item))
            except ValueError:
                continue
        trace = str(snap.get("trace"))
        snap["decoded_map"] = [
            decode_snap_entry(value, traceir_by_trace.get(trace, {}))
            for value in values
        ]


def parse_focus_stderr(
    path: pathlib.Path,
    *,
    focus_parent: int,
    focus_exit: int,
    focus_trace: int,
) -> dict[str, object]:
    result: dict[str, object] = {
        "trace_meta": [],
        "trace_meta_snap": [],
        "trace_aborts": [],
        "itern_focus": [],
        "lleave": [],
        "first_jloop_exit": None,
        "first_jloop_phase": None,
        "first_terminal_abort": None,
        "first_terminal_itern_nil": None,
        "first_terminal_lleave": None,
        "first_terminal_exit": None,
        "first_terminal_exit_snap": None,
        "first_terminal_slots": None,
        "jloop_phase_counts": {},
        "abort_counts": {},
        "log_cap_reached": False,
    }
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line:
            continue
        if line.startswith("S390X_LOG_CAP"):
            result["log_cap_reached"] = True
            continue
        match = TRACE_META_RE.match(line)
        if match:
            event = {
                "phase": match.group(1),
                "trace": int(match.group(2)),
                "parent": int(match.group(3)),
                "exit": int(match.group(4)),
                "root": int(match.group(5)),
                "link": int(match.group(6)),
                "linktype": int(match.group(7)),
                "resumechild": int(match.group(8)),
                "startpc": match.group(9),
                "startop": int(match.group(10)),
                "nsnap": int(match.group(11)),
                "nins": int(match.group(12)),
                "mcloop": int(match.group(13)),
                "line": line,
            }
            result["trace_meta"].append(event)
            continue
        match = TRACE_META_SNAP_RE.match(line)
        if match:
            event = {
                "phase": match.group(1),
                "trace": int(match.group(2)),
                "snap": int(match.group(3)),
                "nent": int(match.group(4)),
                "nslots": int(match.group(5)),
                "pc": match.group(6),
                "op": int(match.group(7)),
                "line": line,
            }
            result["trace_meta_snap"].append(event)
            continue
        match = JLOOP_EXIT_RE.match(line)
        if match and result["first_jloop_exit"] is None:
            result["first_jloop_exit"] = {
                "parent": int(match.group(1)),
                "exit": int(match.group(2)),
                "pc": match.group(3),
                "op": int(match.group(4)),
                "target": int(match.group(5)),
                "target_exec": int(match.group(6)),
                "retop": int(match.group(7)),
                "trace": int(match.group(8)),
                "link": int(match.group(9)),
                "linktype": int(match.group(10)),
                "target_root": int(match.group(11)),
                "target_link": int(match.group(12)),
                "target_linktype": int(match.group(13)),
                "target_startpc": match.group(14),
                "target_startop": int(match.group(15)),
                "target_resumepc": match.group(16),
                "target_resumeop": int(match.group(17)),
                "target_resumevalid": int(match.group(18)),
                "target_resumechild": int(match.group(19)),
                "target_mcloop": int(match.group(20)),
                "state": int(match.group(21)),
                "line": line,
            }
            continue
        match = JLOOP_PHASE_RE.match(line)
        if match:
            phase = match.group(1)
            counts = result["jloop_phase_counts"]
            counts[phase] = counts.get(phase, 0) + 1
            if result["first_jloop_phase"] is None:
                result["first_jloop_phase"] = {
                    "phase": phase,
                    "parent": int(match.group(2)),
                    "exit": int(match.group(3)),
                    "trace": int(match.group(4)),
                    "retop": int(match.group(5)),
                    "state": int(match.group(6)),
                    "line": line,
                }
            continue
        match = TRACE_ABORT_META_RE.match(line)
        if match:
            err = int(match.group(9))
            event = {
                "trace": int(match.group(1)),
                "parent": int(match.group(2)),
                "exit": int(match.group(3)),
                "state": int(match.group(4)),
                "pc": match.group(5),
                "op": int(match.group(6)),
                "startpc": match.group(7),
                "startop": int(match.group(8)),
                "err": err,
                "err_name": trace_error_name(err),
                "root": int(match.group(10)),
                "link": int(match.group(11)),
                "linktype": int(match.group(12)),
                "resumepc": match.group(13),
                "resumeop": int(match.group(14)),
                "resumevalid": int(match.group(15)),
                "line": line,
            }
            result["trace_aborts"].append(event)
            counts = result["abort_counts"]
            counts[event["err_name"]] = counts.get(event["err_name"], 0) + 1
            if (
                event["parent"] == focus_parent and event["exit"] == focus_exit
                and result["first_terminal_abort"] is None
            ):
                result["first_terminal_abort"] = event
            continue
        match = ITERN_FOCUS_RE.match(line)
        if match:
            event = {
                "site": match.group(1),
                "trace": int(match.group(2)),
                "parent": int(match.group(3)),
                "exit": int(match.group(4)),
                "startpc": match.group(5),
                "pc": match.group(6),
                "op": int(match.group(7)),
                "startop": int(match.group(8)),
                "nextt": int(match.group(9)),
                "keyflags": match.group(10),
                "numkey": int(match.group(11)),
                "key_nil": int(match.group(12)),
                "idxchain": int(match.group(13)),
                "mobj_ref": int(match.group(14)),
                "key_ref": int(match.group(15)),
                "val_ref": int(match.group(16)),
                "tab_u64": match.group(17),
                "ctrl_u64": match.group(18),
                "key_u64": match.group(19),
                "val_u64": match.group(20),
                "ix_keyv": match.group(21),
                "ix_tabv": match.group(22),
                "tab_asize": int(match.group(23)),
                "tab_hmask": int(match.group(24)),
                "line": line,
            }
            result["itern_focus"].append(event)
            if (
                event["parent"] == focus_parent and event["exit"] == focus_exit
                and event["site"] == "nil"
                and result["first_terminal_itern_nil"] is None
            ):
                result["first_terminal_itern_nil"] = event
            continue
        match = LLEAVE_RE.match(line)
        if match:
            event = {
                "site": match.group(1),
                "trace": int(match.group(2)),
                "parent": int(match.group(3)),
                "exit": int(match.group(4)),
                "pc": match.group(5),
                "op": int(match.group(6)),
                "startop": int(match.group(7)),
                "framedepth": int(match.group(8)),
                "retdepth": int(match.group(9)),
                "parent_root": int(match.group(10)),
                "parent_linktype": int(match.group(11)),
                "parent_snapcount": int(match.group(12)),
                "parent_snapnent": int(match.group(13)),
                "line": line,
            }
            result["lleave"].append(event)
            if (
                event["parent"] == focus_parent and event["exit"] == focus_exit
                and result["first_terminal_lleave"] is None
            ):
                result["first_terminal_lleave"] = event
            continue
        match = EXIT_RE.match(line)
        if match:
            event = {
                "phase": match.group(1),
                "trace": int(match.group(2)),
                "exit": int(match.group(3)),
                "pc": match.group(4),
                "op": int(match.group(5)),
                "snapcount": int(match.group(6)),
                "snapref": int(match.group(7)),
                "snapnent": int(match.group(8)),
                "state": int(match.group(9)),
                "line": line,
            }
            if (
                event["trace"] == focus_trace and event["exit"] == focus_exit
                and result["first_terminal_exit"] is None
            ):
                result["first_terminal_exit"] = event
            continue
        match = EXIT_SNAP_RE.match(line)
        if match:
            event = {
                "trace": int(match.group(1)),
                "exit": int(match.group(2)),
                "snappc": match.group(3),
                "snapop": int(match.group(4)),
                "tail": match.group(5).strip(),
                "line": line,
            }
            if (
                event["trace"] == focus_trace and event["exit"] == focus_exit
                and result["first_terminal_exit_snap"] is None
            ):
                result["first_terminal_exit_snap"] = event
            continue
        match = SLOTS_RE.match(line)
        if match and result["first_terminal_slots"] is None:
            result["first_terminal_slots"] = {
                "pc": match.group(1),
                "op": int(match.group(2)),
                "base": match.group(3),
                "slots": [],
            }
            continue
        match = SLOT_RE.match(line)
        if match and result.get("first_terminal_slots") is not None:
            slots = result["first_terminal_slots"]["slots"]
            if len(slots) < 20:
                slots.append(
                    {
                        "idx": int(match.group(1)),
                        "itype": int(match.group(2)),
                        "u64": match.group(3),
                    }
                )
            continue
    return result


def top_texit(hist: object) -> tuple[str, int] | None:
    if not isinstance(hist, str) or hist in ("", "(none)"):
        return None
    best: tuple[str, int] | None = None
    for part in hist.split(","):
        if "=" not in part:
            continue
        key, value = part.split("=", 1)
        try:
            count = int(value)
        except ValueError:
            continue
        if best is None or count > best[1]:
            best = (key, count)
    return best


def format_decoded_snapshot(snap: dict[str, object] | None) -> str:
    if not snap:
        return "`(not captured)`"
    decoded = snap.get("decoded_map", [])
    entries = []
    for entry in decoded:
        if entry.get("kind") == "SNAP_PC_OR_SENTINEL":
            continue
        flags = "|".join(entry.get("flags", [])) or "-"
        ir = entry.get("ir") or {}
        op = ir.get("op") or "?"
        entries.append(
            f"s{entry.get('slot')}:r{entry.get('irref')}:{op}:{flags}"
        )
    return "`" + (", ".join(entries) if entries else "(empty)") + "`"


def render_summary(
    *,
    host: str,
    repo: str,
    commit: str,
    output_dir: pathlib.Path,
    retained: dict[str, object],
    terminal: dict[str, object],
    focus_stdout: dict[str, object],
    focus: dict[str, object],
    focus_parent: int,
    focus_exit: int,
    focus_trace: int,
    samples: int,
    warmup: int,
    pin_core: int | None,
) -> str:
    retained_perf = retained["perf"]
    terminal_perf = terminal["perf"]
    retained_kv = retained["kv"]
    terminal_kv = terminal["kv"]
    terminal_top = top_texit(terminal_kv.get("TEXIT_HIST"))
    focus_snaps = focus["trace_meta_snap"]
    trace2_exit1_snaps = [
        snap for snap in focus_snaps
        if snap["trace"] == focus_trace and snap["snap"] == focus_exit
    ]
    trace2_decoded = {
        snap["exit"]: snap for snap in focus_stdout.get("tracesnap", [])
        if snap.get("trace") == focus_trace and snap.get("exit") in (
            focus_exit, focus_exit + 1, focus_exit + 2
        )
    }
    lines = [
        "# Iterator Terminal Truth Pack",
        "",
        f"- Timestamp: `{dt.datetime.now().astimezone().strftime('%Y-%m-%d %H:%M:%S %Z')}`",
        f"- Host: `{host}`",
        f"- Repo: `{repo}`",
        f"- Commit: `{commit}`",
        f"- Official benchmark: `{OFFICIAL_BENCH}`",
        f"- Samples: `{samples}`",
        f"- Warmup: `{warmup}`",
        f"- Pinned core: `{pin_core if pin_core is not None else 'unbound'}`",
        f"- Focus parent/exit: `{focus_parent}/{focus_exit}`",
        f"- Focus snapshot trace: `{focus_trace}`",
        "",
        "## Retained Official Row",
        "",
        f"- `pairs_sum/hot`: `{retained_perf.get('iterator_table/pairs_sum/hot', {}).get('median', 'n/a')}`",
        f"- `pairs_array_sum/hot`: `{retained_perf.get('iterator_table/pairs_array_sum/hot', {}).get('median', 'n/a')}`",
        f"- `TRACE_START`: `{retained_kv.get('TRACE_START', 'n/a')}`",
        f"- `TRACE_STOP`: `{retained_kv.get('TRACE_STOP', 'n/a')}`",
        f"- `TRACE_ABORT`: `{retained_kv.get('TRACE_ABORT', 'n/a')}`",
        f"- `TEXIT_COUNT`: `{retained_kv.get('TEXIT_COUNT', 'n/a')}`",
        "",
        "## Terminal-Unlocked Official Row",
        "",
        f"- `pairs_sum/hot`: `{terminal_perf.get('iterator_table/pairs_sum/hot', {}).get('median', 'n/a')}`",
        f"- `pairs_array_sum/hot`: `{terminal_perf.get('iterator_table/pairs_array_sum/hot', {}).get('median', 'n/a')}`",
        f"- `TRACE_START`: `{terminal_kv.get('TRACE_START', 'n/a')}`",
        f"- `TRACE_STOP`: `{terminal_kv.get('TRACE_STOP', 'n/a')}`",
        f"- `TRACE_ABORT`: `{terminal_kv.get('TRACE_ABORT', 'n/a')}`",
        f"- `TEXIT_COUNT`: `{terminal_kv.get('TEXIT_COUNT', 'n/a')}`",
        f"- Dominant exit: `{terminal_top[0]}` count `{terminal_top[1]}`" if terminal_top else "- Dominant exit: `(none)`",
        "",
        "## Terminal Contract Evidence",
        "",
    ]
    first_jloop = focus.get("first_jloop_exit")
    first_phase = focus.get("first_jloop_phase")
    if first_jloop:
        lines.append(
            "- First focused JLOOP exit: "
            f"`parent={first_jloop['parent']} exit={first_jloop['exit']} "
            f"retop={first_jloop['retop']} target={first_jloop['target']} "
            f"target_exec={first_jloop['target_exec']} "
            f"target_startop={first_jloop['target_startop']} "
            f"target_resumevalid={first_jloop['target_resumevalid']} "
            f"target_resumechild={first_jloop['target_resumechild']}`"
        )
    else:
        lines.append("- First focused JLOOP exit: `(not captured)`")
    if first_phase:
        lines.append(
            f"- First focused JLOOP phase: `{first_phase['phase']}` for "
            f"`parent={first_phase['parent']} exit={first_phase['exit']}`"
        )
    lines.append(f"- JLOOP phase counts: `{focus.get('jloop_phase_counts', {})}`")
    if trace2_exit1_snaps:
        snap = trace2_exit1_snaps[0]
        lines.append(
            f"- Focused `trace {focus_trace} exit {focus_exit}` snapshot: "
            f"`nent={snap['nent']} nslots={snap['nslots']} op={snap['op']}`"
        )
    else:
        lines.append(f"- Focused `trace {focus_trace} exit {focus_exit}` snapshot: `(not captured)`")
    if trace2_decoded:
        lines.append(
            f"- Decoded `trace {focus_trace}` snapshots: "
            f"`exit {focus_exit}` {format_decoded_snapshot(trace2_decoded.get(focus_exit))}; "
            f"`exit {focus_exit + 1}` {format_decoded_snapshot(trace2_decoded.get(focus_exit + 1))}; "
            f"`exit {focus_exit + 2}` {format_decoded_snapshot(trace2_decoded.get(focus_exit + 2))}"
        )
    first_abort = focus.get("first_terminal_abort")
    if first_abort:
        lines.append(
            "- First terminal side abort: "
            f"`trace={first_abort['trace']} parent={first_abort['parent']} "
            f"exit={first_abort['exit']} op={first_abort['op']} "
            f"startop={first_abort['startop']} err={first_abort['err_name']} "
            f"root={first_abort['root']} linktype={first_abort['linktype']}`"
        )
    else:
        lines.append("- First terminal side abort: `(not captured)`")
    lines.append(f"- Abort counts: `{focus.get('abort_counts', {})}`")
    first_nil = focus.get("first_terminal_itern_nil")
    if first_nil:
        lines.append(
            "- First terminal `rec_itern()` nil state: "
            f"`trace={first_nil['trace']} parent={first_nil['parent']} exit={first_nil['exit']} "
            f"nextt={first_nil['nextt']} key_nil={first_nil['key_nil']} "
            f"mobj_ref={first_nil['mobj_ref']} key_ref={first_nil['key_ref']} "
            f"val_ref={first_nil['val_ref']} ctrl={first_nil['ctrl_u64']} "
            f"tab={first_nil['tab_u64']}`"
        )
    else:
        lines.append("- First terminal `rec_itern()` nil state: `(not captured)`")
    first_lleave = focus.get("first_terminal_lleave")
    if first_lleave:
        lines.append(
            "- First terminal leave abort site: "
            f"`{first_lleave['site']} trace={first_lleave['trace']} "
            f"parent_snapcount={first_lleave['parent_snapcount']} "
            f"parent_snapnent={first_lleave['parent_snapnent']}`"
        )
    else:
        lines.append("- First terminal leave abort site: `(not captured)`")
    first_exit = focus.get("first_terminal_exit")
    if first_exit:
        lines.append(
            "- First terminal runtime exit: "
            f"`trace={first_exit['trace']} exit={first_exit['exit']} "
            f"pc={first_exit['pc']} op={first_exit['op']} "
            f"snapnent={first_exit['snapnent']} state={first_exit['state']}`"
        )
    first_exit_snap = focus.get("first_terminal_exit_snap")
    if first_exit_snap:
        lines.append(
            "- First terminal runtime exit snapshot: "
            f"`snappc={first_exit_snap['snappc']} snapop={first_exit_snap['snapop']} "
            f"{first_exit_snap['tail']}`"
        )
    first_slots = focus.get("first_terminal_slots")
    if first_slots:
        slot_summary = ", ".join(
            f"s{slot['idx']}:{slot['itype']}:{slot['u64']}"
            for slot in first_slots.get("slots", [])[:12]
        )
        lines.append(
            "- First terminal restored slots: "
            f"`pc={first_slots['pc']} op={first_slots['op']} {slot_summary}`"
        )
    lines.extend(
        [
            "",
            "## Decision",
            "",
            "- This pack is read-only. It is a gate for a future terminal-leave contract, not a retained optimization by itself.",
            f"- A candidate is allowed only if it changes the `trace {focus_trace} exit {focus_exit}` terminal contract without weakening the retained semantic iterator fold.",
            "",
            "## Raw Artifacts",
            "",
            f"- Retained stdout: `{output_dir / 'raw' / 'retained.stdout.log'}`",
            f"- Terminal stdout: `{output_dir / 'raw' / 'terminal_unlocked.stdout.log'}`",
            f"- Terminal focus stderr: `{output_dir / 'raw' / 'terminal_focus.stderr.log'}`",
            f"- Bytecode: `{output_dir / 'raw' / 'bytecode.stdout.log'}`",
        ]
    )
    return "\n".join(lines) + "\n"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build an official iterator terminal-leave truth pack.")
    parser.add_argument("--host", choices=restamp.HOST_LABELS, required=True)
    parser.add_argument("--repo", help="Remote clean repo path. Defaults to authoritative repo for host.")
    parser.add_argument("--output-dir", help="Local artifact output directory.")
    parser.add_argument("--pin-core", type=int, default=restamp.DEFAULT_PIN_CORE)
    parser.add_argument("--samples", type=int, default=1)
    parser.add_argument("--warmup", type=int, default=0)
    parser.add_argument("--stderr-line-cap", type=int, default=20000)
    parser.add_argument("--timeout-sec", type=int, default=45)
    parser.add_argument("--focus-parent", type=int, default=2)
    parser.add_argument("--focus-exit", type=int, default=1)
    parser.add_argument("--focus-trace", type=int, default=2)
    parser.add_argument(
        "--live-state-log",
        action="store_true",
        help="Enable verbose exit/slot live-state logging for terminal focus.",
    )
    parser.add_argument(
        "--extra-env",
        action="append",
        help="Extra remote environment variable as KEY=VALUE, applied to all run modes.",
    )
    parser.add_argument("--skip-sync", action="store_true")
    parser.add_argument("--skip-build", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    host = args.host
    repo = args.repo or restamp.AUTHORITATIVE_REPOS[host]
    timestamp = dt.datetime.now().astimezone().strftime("%Y%m%d-%H%M%S")
    output_dir = pathlib.Path(args.output_dir).expanduser().resolve() if args.output_dir else (
        DEFAULT_OUTPUT_ROOT / f"{timestamp}-{host}-iterator-terminal-truth-pack"
    ).resolve()
    raw_dir = output_dir / "raw"
    raw_dir.mkdir(parents=True, exist_ok=True)

    if not args.skip_sync:
        restamp.sync_tracked_files(host, repo)
    if not args.skip_build:
        restamp.build_remote_repo(host, repo, raw_dir)

    remote_tmp = restamp.prepare_remote_scripts(host)
    try:
        prepare_terminal_script(host, remote_tmp)
        run_bytecode(host, repo, raw_dir)
        extra_env = parse_extra_env(args.extra_env)
        retained_env = dict(restamp.RETAINED_BASELINE_ENV)
        retained_env.update(extra_env)
        terminal_env = terminal_optout_env()
        terminal_env.update(extra_env)
        focus_env = dict(terminal_env)
        focus_env.update(TERMINAL_FOCUS_ENV)
        focus_env.update({
            "LUAJIT_S390X_SIDE_FOCUS_PARENT": str(args.focus_parent),
            "LUAJIT_S390X_SIDE_FOCUS_EXIT": str(args.focus_exit),
            "LUAJIT_S390X_ITERN_FOCUS_PARENT": str(args.focus_parent),
            "LUAJIT_S390X_ITERN_FOCUS_EXIT": str(args.focus_exit),
            "LUAJIT_S390X_JLOOP_EXIT_PARENT": str(args.focus_parent),
            "LUAJIT_S390X_JLOOP_EXIT_EXIT": str(args.focus_exit),
        })
        if args.live_state_log:
            focus_env["LUAJIT_S390X_EXIT_LOG"] = "1"
            focus_env["LUAJIT_S390X_SLOT_LOG"] = "1"
        run_mode(
            host=host,
            repo=repo,
            remote_tmp=remote_tmp,
            raw_dir=raw_dir,
            mode="retained",
            env=retained_env,
            samples=args.samples,
            warmup=args.warmup,
            pin_core=args.pin_core,
            stderr_line_cap=args.stderr_line_cap,
            timeout_sec=args.timeout_sec,
        )
        run_mode(
            host=host,
            repo=repo,
            remote_tmp=remote_tmp,
            raw_dir=raw_dir,
            mode="terminal_unlocked",
            env=terminal_env,
            samples=args.samples,
            warmup=args.warmup,
            pin_core=args.pin_core,
            stderr_line_cap=args.stderr_line_cap,
            timeout_sec=args.timeout_sec,
        )
        run_mode(
            host=host,
            repo=repo,
            remote_tmp=remote_tmp,
            raw_dir=raw_dir,
            mode="terminal_focus",
            env=focus_env,
            samples=args.samples,
            warmup=args.warmup,
            pin_core=args.pin_core,
            stderr_line_cap=args.stderr_line_cap,
            timeout_sec=args.timeout_sec,
        )
    finally:
        restamp.cleanup_remote_scripts(host, remote_tmp)

    retained = parse_stdout(raw_dir / "retained.stdout.log")
    terminal = parse_stdout(raw_dir / "terminal_unlocked.stdout.log")
    focus_stdout = parse_stdout(raw_dir / "terminal_focus.stdout.log")
    focus_stderr = parse_focus_stderr(
        raw_dir / "terminal_focus.stderr.log",
        focus_parent=args.focus_parent,
        focus_exit=args.focus_exit,
        focus_trace=args.focus_trace,
    )
    metadata = {
        "timestamp": dt.datetime.now(dt.timezone.utc).isoformat(),
        "git_commit": restamp.current_commit(),
        "host": host,
        "repo": repo,
        "official_bench": OFFICIAL_BENCH,
        "samples": args.samples,
        "warmup": args.warmup,
        "pin_core": args.pin_core,
        "focus_parent": args.focus_parent,
        "focus_exit": args.focus_exit,
        "focus_trace": args.focus_trace,
        "retained_env": retained_env,
        "terminal_unlocked_env": terminal_env,
        "terminal_focus_env_extra": TERMINAL_FOCUS_ENV,
        "extra_env": extra_env,
        "terminal_unlock_control": TERMINAL_DISABLE_ENV,
        "retained": retained,
        "terminal_unlocked": terminal,
        "terminal_focus_stdout": focus_stdout,
        "terminal_focus": focus_stderr,
    }
    write_json(output_dir / "metadata.json", metadata)
    summary = render_summary(
        host=host,
        repo=repo,
        commit=metadata["git_commit"],
        output_dir=output_dir,
        retained=retained,
        terminal=terminal,
        focus_stdout=focus_stdout,
        focus=focus_stderr,
        focus_parent=args.focus_parent,
        focus_exit=args.focus_exit,
        focus_trace=args.focus_trace,
        samples=args.samples,
        warmup=args.warmup,
        pin_core=args.pin_core,
    )
    write_text(output_dir / "summary.md", summary)
    print(output_dir)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (TerminalTruthError, restamp.RestampError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
