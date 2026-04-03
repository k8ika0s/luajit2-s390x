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

PROBE_DEBUG_ENVS: dict[str, str] = {
    "LUAJIT_S390X_EXIT_LOG": "1",
    "LUAJIT_S390X_GUARD_LOG": "1",
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


def emit_traceir_lua() -> str:
    return """\
local vmdef = require("jit.vmdef")

local function ir_op_name(ot)
  local idx = math.floor(ot / 256) * 6
  return (string.sub(vmdef.irnames, idx + 1, idx + 6):gsub("%s+$", ""))
end

local function emit_traceir(limit)
  local util = require("jit.util")
  for tr = 1, limit do
    local info = util.traceinfo(tr)
    if info then
      for ins = 0, (tonumber(info.nins) or 0) - 1 do
        local mode, ot, op1, op2, prev = util.traceir(tr, ins)
        print(
          string.format(
            "TRACEIR tr=%d ins=%d op=%s ot=%d mode=%d op1=%d op2=%d prev=%d",
            tr,
            ins,
            ir_op_name(ot),
            tonumber(ot) or -1,
            tonumber(mode) or -1,
            tonumber(op1) or -1,
            tonumber(op2) or -1,
            tonumber(prev) or -1
          )
        )
      end
    end
  end
end
"""


def emit_counter_lua(*, enabled: bool) -> str:
    if not enabled:
        return """\
local function start_counters()
  return nil, nil
end

local function stop_counters() end
"""
    return """\
local function start_counters()
  local trace_cap = testlib.trace_counter_capture()
  local texit_cap = testlib.texit_counter_capture()
  return trace_cap, texit_cap
end

local function stop_counters(trace_cap, texit_cap)
  trace_cap.stop()
  texit_cap.stop()
  print("TRACE_START", trace_cap.start)
  print("TRACE_STOP", trace_cap.stop_count)
  print("TRACE_ABORT", trace_cap.abort)
  print("TRACE_TOTAL", trace_cap.total)
  print("TEXIT_COUNT", texit_cap.total)
  emit_hist("TRACE_HIST", trace_cap.hist)
  emit_hist("TEXIT_HIST", texit_cap.hist)
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
{emit_traceir}
{emit_counter}
local function run(n)
  local total = 0
  for i = 1, n do
    total = bit.tobit(total + i * 65537)
  end
  return bit.tobit(total)
end
run(20); run(20); run(20)
local trace_cap, texit_cap = start_counters()
print("RESULT", run({iterations}))
stop_counters(trace_cap, texit_cap)
emit_traceinfo(32)
emit_traceir(32)
""",
    },
    "number_helper_literal_stop": {
        "family": "header_reducer",
        "iterations": 400,
        "label": "NUMBER_HELPER_LITERAL_STOP",
        "script": """\
local bit = require("bit")
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
jit.opt.start("hotloop=1", "hotexit=1")
{emit_hist}
{emit_traceinfo}
{emit_traceir}
{emit_counter}
local function run()
  local total = 0
  for i = 1, 400 do
    total = bit.tobit(total + i * 65537)
  end
  return bit.tobit(total)
end
run(); run(); run()
local trace_cap, texit_cap = start_counters()
print("RESULT", run())
stop_counters(trace_cap, texit_cap)
emit_traceinfo(32)
emit_traceir(32)
""",
    },
    "number_helper_local_tobit": {
        "family": "header_reducer",
        "iterations": 400,
        "label": "NUMBER_HELPER_LOCAL_TOBIT",
        "script": """\
local bit = require("bit")
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
jit.opt.start("hotloop=1", "hotexit=1")
{emit_hist}
{emit_traceinfo}
{emit_traceir}
{emit_counter}
local function run()
  local total = 0
  local tobit = bit.tobit
  for i = 1, 400 do
    total = tobit(total + i * 65537)
  end
  return tobit(total)
end
run(); run(); run()
local trace_cap, texit_cap = start_counters()
print("RESULT", run())
stop_counters(trace_cap, texit_cap)
emit_traceinfo(32)
emit_traceir(32)
""",
    },
    "number_helper_arg_tobit": {
        "family": "header_reducer",
        "iterations": 400,
        "label": "NUMBER_HELPER_ARG_TOBIT",
        "script": """\
local bit = require("bit")
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
jit.opt.start("hotloop=1", "hotexit=1")
{emit_hist}
{emit_traceinfo}
{emit_traceir}
{emit_counter}
local function run(tobit)
  local total = 0
  for i = 1, 400 do
    total = tobit(total + i * 65537)
  end
  return tobit(total)
end
run(bit.tobit); run(bit.tobit); run(bit.tobit)
local trace_cap, texit_cap = start_counters()
print("RESULT", run(bit.tobit))
stop_counters(trace_cap, texit_cap)
emit_traceinfo(32)
emit_traceir(32)
""",
    },
    "number_helper_loop_local_tobit": {
        "family": "be_helpers",
        "iterations": 64000,
        "label": "NUMBER_HELPER_LOOP_LOCAL_TOBIT",
        "script": """\
local bit = require("bit")
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
jit.opt.start("hotloop=1")
{emit_hist}
{emit_traceinfo}
{emit_traceir}
{emit_counter}
local function run(n)
  local total = 0
  local tobit = bit.tobit
  for i = 1, n do
    total = tobit(total + i * 65537)
  end
  return tobit(total)
end
run(20); run(20); run(20)
local trace_cap, texit_cap = start_counters()
print("RESULT", run({iterations}))
stop_counters(trace_cap, texit_cap)
emit_traceinfo(32)
emit_traceir(32)
""",
    },
    "number_helper_loop_arg_tobit": {
        "family": "be_helpers",
        "iterations": 64000,
        "label": "NUMBER_HELPER_LOOP_ARG_TOBIT",
        "script": """\
local bit = require("bit")
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
jit.opt.start("hotloop=1")
{emit_hist}
{emit_traceinfo}
{emit_traceir}
{emit_counter}
local function run(n, tobit)
  local total = 0
  for i = 1, n do
    total = tobit(total + i * 65537)
  end
  return tobit(total)
end
run(20, bit.tobit); run(20, bit.tobit); run(20, bit.tobit)
local trace_cap, texit_cap = start_counters()
print("RESULT", run({iterations}, bit.tobit))
stop_counters(trace_cap, texit_cap)
emit_traceinfo(32)
emit_traceir(32)
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
{emit_traceir}
{emit_counter}
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
local trace_cap, texit_cap = start_counters()
print("RESULT", run({iterations}))
stop_counters(trace_cap, texit_cap)
emit_traceinfo(32)
emit_traceir(32)
""",
    },
    "be_pack_loop_local_ops": {
        "family": "be_helpers",
        "iterations": 64000,
        "label": "BE_PACK_LOOP_LOCAL_OPS",
        "script": """\
local bit = require("bit")
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
jit.opt.start("hotloop=1")
{emit_hist}
{emit_traceinfo}
{emit_traceir}
{emit_counter}
local function run(n)
  local total = 0
  local band = bit.band
  local rshift = bit.rshift
  local lshift = bit.lshift
  local tobit = bit.tobit
  for i = 1, n do
    local b1 = band(rshift(i, 24), 0xff)
    local b2 = band(rshift(i, 16), 0xff)
    local b3 = band(rshift(i, 8), 0xff)
    local b4 = band(i, 0xff)
    total = tobit(total + lshift(b1, 24) + lshift(b2, 16) + lshift(b3, 8) + b4)
  end
  return tobit(total)
end
run(20); run(20); run(20)
local trace_cap, texit_cap = start_counters()
print("RESULT", run({iterations}))
stop_counters(trace_cap, texit_cap)
emit_traceinfo(32)
emit_traceir(32)
""",
    },
    "be_pack_literal_stop": {
        "family": "header_reducer",
        "iterations": 400,
        "label": "BE_PACK_LITERAL_STOP",
        "script": """\
local bit = require("bit")
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
jit.opt.start("hotloop=1", "hotexit=1")
{emit_hist}
{emit_traceinfo}
{emit_traceir}
{emit_counter}
local function run()
  local total = 0
  for i = 1, 400 do
    local b1 = bit.band(bit.rshift(i, 24), 0xff)
    local b2 = bit.band(bit.rshift(i, 16), 0xff)
    local b3 = bit.band(bit.rshift(i, 8), 0xff)
    local b4 = bit.band(i, 0xff)
    total = bit.tobit(total + bit.lshift(b1, 24) + bit.lshift(b2, 16) + bit.lshift(b3, 8) + b4)
  end
  return bit.tobit(total)
end
run(); run(); run()
local trace_cap, texit_cap = start_counters()
print("RESULT", run())
stop_counters(trace_cap, texit_cap)
emit_traceinfo(32)
emit_traceir(32)
""",
    },
    "be_pack_literal_stop_local_ops": {
        "family": "header_reducer",
        "iterations": 400,
        "label": "BE_PACK_LITERAL_STOP_LOCAL_OPS",
        "script": """\
local bit = require("bit")
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
jit.opt.start("hotloop=1", "hotexit=1")
{emit_hist}
{emit_traceinfo}
{emit_traceir}
{emit_counter}
local function run()
  local total = 0
  local band = bit.band
  local rshift = bit.rshift
  local lshift = bit.lshift
  local tobit = bit.tobit
  for i = 1, 400 do
    local b1 = band(rshift(i, 24), 0xff)
    local b2 = band(rshift(i, 16), 0xff)
    local b3 = band(rshift(i, 8), 0xff)
    local b4 = band(i, 0xff)
    total = tobit(total + lshift(b1, 24) + lshift(b2, 16) + lshift(b3, 8) + b4)
  end
  return tobit(total)
end
run(); run(); run()
local trace_cap, texit_cap = start_counters()
print("RESULT", run())
stop_counters(trace_cap, texit_cap)
emit_traceinfo(32)
emit_traceir(32)
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
{emit_traceir}
{emit_counter}
local function run(n)
  local total = 0
  for i = 1, n do
    total = total + ffi.C.abs((i % 17) - 8)
  end
  return total
end
run(20); run(20); run(20)
local trace_cap, texit_cap = start_counters()
print("RESULT", run({iterations}))
stop_counters(trace_cap, texit_cap)
emit_traceinfo(32)
emit_traceir(32)
""",
    },
    "direct_abs_literal_stop": {
        "family": "header_reducer",
        "iterations": 400,
        "label": "DIRECT_ABS_LITERAL_STOP",
        "script": """\
local ffi = require("ffi")
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
jit.opt.start("hotloop=1", "hotexit=1")
ffi.cdef[[
int abs(int x);
]]
{emit_hist}
{emit_traceinfo}
{emit_traceir}
{emit_counter}
local function run()
  local total = 0
  for i = 1, 400 do
    total = total + ffi.C.abs((i % 17) - 8)
  end
  return total
end
run(); run(); run()
local trace_cap, texit_cap = start_counters()
print("RESULT", run())
stop_counters(trace_cap, texit_cap)
emit_traceinfo(32)
emit_traceir(32)
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
{emit_traceir}
{emit_counter}
local function run(n)
  local total = 0
  for i = 1, n do
    total = total + cabs((i % 17) - 8)
  end
  return total
end
run(20); run(20); run(20)
local trace_cap, texit_cap = start_counters()
print("RESULT", run({iterations}))
stop_counters(trace_cap, texit_cap)
emit_traceinfo(32)
emit_traceir(32)
""",
    },
    "pure_add_reducer": {
        "family": "header_reducer",
        "iterations": 800,
        "label": "PURE_ADD_REDUCER",
        "script": """\
local bit = require("bit")
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
jit.opt.start("hotloop=1", "hotexit=1")
{emit_hist}
{emit_traceinfo}
{emit_traceir}
{emit_counter}
local function run(n)
  local total = 0
  for i = 1, n do
    total = total + i * 65537
  end
  return bit.tobit(total)
end
run(20); run(20); run(20)
local trace_cap, texit_cap = start_counters()
print("RESULT", run({iterations}))
stop_counters(trace_cap, texit_cap)
emit_traceinfo(32)
emit_traceir(32)
""",
    },
}


TRACEIR_RE = re.compile(
    r"^TRACEIR tr=(?P<tr>\d+) ins=(?P<ins>\d+) op=(?P<op>\S+) ot=(?P<ot>-?\d+) "
    r"mode=(?P<mode>-?\d+) op1=(?P<op1>-?\d+) op2=(?P<op2>-?\d+) prev=(?P<prev>-?\d+)$"
)
GUARD_STUB_RE = re.compile(
    r"^S390X_GUARD curins=(?P<curins>-?\d+) snap=(?P<snap>\d+) loopsnap=(?P<loopsnap>\d+) "
    r"cc=(?P<cc>-?\d+) loopinv=(?P<loopinv>\d+) p=(?P<patchpoint>\S+) target=(?P<target>\S+) invmcp=(?P<invmcp>\S+)$"
)
GUARD_KIND_RE = re.compile(
    r"^S390X_GUARD kind=(?P<kind>\S+) curins=(?P<curins>-?\d+) ir=(?P<ir>-?\d+) op=(?P<op>-?\d+) "
    r"type=(?P<type>-?\d+) cc=(?P<cc>-?\d+) ofs=(?P<ofs>-?\d+) extra=(?P<extra>-?\d+)$"
)
EXIT_RE = re.compile(
    r"^S390X_EXIT phase=(?P<phase>\S+) trace=(?P<trace>\d+) exit=(?P<exit>\d+) .* op=(?P<op>\d+) "
    r"snapcount=(?P<snapcount>\d+) snapref=(?P<snapref>\d+) snapnent=(?P<snapnent>\d+).* guardmark=(?P<guardmark>0x[0-9a-fA-F]+|\d+)"
)
EXIT_SNAP_RE = re.compile(
    r"^S390X_EXIT_SNAP trace=(?P<trace>\d+) exit=(?P<exit>\d+) snappc=(?P<snappc>\S+) snapop=(?P<snapop>\d+)(?P<rest>.*)$"
)


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


def parse_traceir(text: str) -> dict[int, dict[int, dict[str, Any]]]:
    traceir: dict[int, dict[int, dict[str, Any]]] = {}
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not (m := TRACEIR_RE.match(line)):
            continue
        try:
            traceno = int(m.group("tr"))
            ins = int(m.group("ins"))
            ot = int(m.group("ot"))
            mode = int(m.group("mode"))
            op1 = int(m.group("op1"))
            op2 = int(m.group("op2"))
            prev = int(m.group("prev"))
        except ValueError:
            continue
        traceir.setdefault(traceno, {})[ins] = {
            "tr": traceno,
            "ins": ins,
            "op": m.group("op"),
            "ot": ot,
            "mode": mode,
            "op1": op1,
            "op2": op2,
            "prev": prev,
        }
    return traceir


def dedupe_entries(entries: list[dict[str, Any]], keys: list[str]) -> list[dict[str, Any]]:
    seen = set()
    result = []
    for entry in entries:
        marker = tuple(entry.get(key) for key in keys)
        if marker in seen:
            continue
        seen.add(marker)
        result.append(entry)
    return result


def parse_exit_guard_clusters(stderr_text: str) -> list[dict[str, Any]]:
    pending_stubs: list[dict[str, Any]] = []
    pending_kinds: list[dict[str, Any]] = []
    clusters: list[dict[str, Any]] = []
    last_exit: dict[str, Any] | None = None
    for lineno, raw_line in enumerate(stderr_text.splitlines(), start=1):
        line = raw_line.strip()
        if not line:
            continue
        if (m := GUARD_KIND_RE.match(line)):
            pending_kinds.append(
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
        if (m := GUARD_STUB_RE.match(line)):
            pending_stubs.append(
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
        if (m := EXIT_RE.match(line)):
            if m.group("phase") != "exit":
                continue
            cluster = {
                "line": lineno,
                "trace": int(m.group("trace")),
                "exit": int(m.group("exit")),
                "op": int(m.group("op")),
                "snapcount": int(m.group("snapcount")),
                "snapref": int(m.group("snapref")),
                "snapnent": int(m.group("snapnent")),
                "guardmark": int(m.group("guardmark"), 0),
                "guard_stubs": pending_stubs[:],
                "guard_kinds": pending_kinds[:],
            }
            clusters.append(cluster)
            pending_stubs = []
            pending_kinds = []
            last_exit = cluster
            continue
        if (m := EXIT_SNAP_RE.match(line)):
            if (
                last_exit
                and last_exit["trace"] == int(m.group("trace"))
                and last_exit["exit"] == int(m.group("exit"))
            ):
                last_exit["snappc"] = m.group("snappc")
                last_exit["snapop"] = int(m.group("snapop"))
    return clusters


def summarize_dominant_exit_cluster(
    *,
    dominant_texit: dict[str, Any] | None,
    traceir: dict[int, dict[int, dict[str, Any]]],
    guard_clusters: list[dict[str, Any]],
) -> dict[str, Any] | None:
    if not dominant_texit:
        return None
    match = re.match(r"^(?P<trace>\d+):(?P<exit>\d+)$", dominant_texit["key"])
    if not match:
        return None
    trace_no = int(match.group("trace"))
    exit_no = int(match.group("exit"))
    matching = [cluster for cluster in guard_clusters if cluster["trace"] == trace_no and cluster["exit"] == exit_no]
    if not matching:
        return {"trace": trace_no, "exit": exit_no, "samples": 0}
    exemplar = matching[0]
    guardmark_hist: dict[int, int] = {}
    for cluster in matching:
        guardmark = int(cluster.get("guardmark", 0) or 0)
        guardmark_hist[guardmark] = guardmark_hist.get(guardmark, 0) + 1
    stubs = dedupe_entries(
        exemplar["guard_stubs"],
        ["curins", "snap", "loopsnap", "cc", "patchpoint", "target"],
    )
    guard_sequence = []
    first_sload = None
    dominant_guardmark = None
    exact_guard = None
    for stub in stubs:
        curins = int(stub["curins"])
        kinds = dedupe_entries(
            [entry for entry in exemplar["guard_kinds"] if int(entry["curins"]) == curins],
            ["kind", "curins", "ir", "op", "type", "cc", "ofs", "extra"],
        )
        item = {
            "curins": curins,
            "stub": stub,
            "trace_ir": traceir.get(trace_no, {}).get(curins),
            "guard_kinds": kinds,
        }
        guard_sequence.append(item)
        if first_sload is None and any(kind.get("kind") == "sload_int" for kind in kinds):
            first_sload = item
    if guardmark_hist:
        guardmark, count = max(guardmark_hist.items(), key=lambda item: (item[1], item[0]))
        dominant_guardmark = {"curins": guardmark, "count": count}
        for item in guard_sequence:
            if int(item["curins"]) == guardmark:
                exact_guard = item
                break
    return {
        "trace": trace_no,
        "exit": exit_no,
        "samples": len(matching),
        "op": exemplar.get("op"),
        "snapop": exemplar.get("snapop"),
        "snapcount": exemplar.get("snapcount"),
        "snapref": exemplar.get("snapref"),
        "snapnent": exemplar.get("snapnent"),
        "snappc": exemplar.get("snappc"),
        "guardmark_hist": guardmark_hist,
        "dominant_guardmark": dominant_guardmark,
        "guard_curins": [item["curins"] for item in guard_sequence],
        "guard_sequence": guard_sequence,
        "first_sload_guard": first_sload,
        "exact_guard": exact_guard,
    }


def dominant_hist(hist: dict[str, int]) -> dict[str, Any] | None:
    if not hist:
        return None
    key, count = max(hist.items(), key=lambda item: (item[1], item[0]))
    return {"key": key, "count": count}


def render_lua_script(
    template: str,
    iterations: int,
    *,
    counters_enabled: bool,
    posthooks_enabled: bool,
) -> str:
    return template.format(
        iterations=iterations,
        emit_hist=emit_hist_lua().rstrip(),
        emit_traceinfo=(emit_traceinfo_lua().rstrip() if posthooks_enabled else "local function emit_traceinfo() end"),
        emit_traceir=(emit_traceir_lua().rstrip() if posthooks_enabled else "local function emit_traceir() end"),
        emit_counter=emit_counter_lua(enabled=counters_enabled).rstrip(),
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
    posthooks_enabled = not bool(extra_env.get("LUAJIT_S390X_PROBE_NO_POSTHOOKS"))
    script_text = render_lua_script(
        config["script"],
        iterations,
        counters_enabled=not bool(extra_env.get("LUAJIT_S390X_PROBE_NO_COUNTERS")),
        posthooks_enabled=posthooks_enabled,
    )
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
    probe_env = dict(PROBE_DEBUG_ENVS)
    probe_env.update(extra_env)
    extra_env_prefix = restamp.remote_extra_env_prefix(probe_env)
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
    traceir = parse_traceir(stdout)
    guard_clusters = parse_exit_guard_clusters(proc.stderr)
    dominant_texit = dominant_hist(texit_hist)
    dominant_trace = dominant_hist(trace_hist)
    dominant_traceinfo = None
    if dominant_texit:
        match = re.match(r"^(\d+):(\d+)$", dominant_texit["key"])
        if match:
            dominant_traceinfo = traceinfo.get(int(match.group(1)))
    dominant_exit_cluster = summarize_dominant_exit_cluster(
        dominant_texit=dominant_texit,
        traceir=traceir,
        guard_clusters=guard_clusters,
    )
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
        "dominant_exit_cluster": dominant_exit_cluster,
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
            if counts:
                lines.append(
                    f"- `{result['workload']}`: `TRACE_START {counts.get('TRACE_START', 0)}`, "
                    f"`TRACE_STOP {counts.get('TRACE_STOP', 0)}`, `TRACE_ABORT {counts.get('TRACE_ABORT', 0)}`, "
                    f"`TEXIT_COUNT {counts.get('TEXIT_COUNT', 0)}`"
                )
            else:
                lines.append(
                    f"- `{result['workload']}`: counter hooks disabled; using raw `S390X_EXIT`/`TRACEIR` artifacts only"
                )
            if not result["traceinfo"]:
                lines.append(f"- `{result['workload']}`: post-run `traceinfo/traceir` hooks disabled")
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
            dominant_exit_cluster = result.get("dominant_exit_cluster")
            if dominant_exit_cluster and dominant_exit_cluster.get("samples"):
                lines.append(
                    f"- `{result['workload']}` dominant exit cluster: "
                    f"`trace {dominant_exit_cluster['trace']} exit {dominant_exit_cluster['exit']}`, "
                    f"`op {dominant_exit_cluster.get('op')}`, `snapop {dominant_exit_cluster.get('snapop')}`, "
                    f"`snapnent {dominant_exit_cluster.get('snapnent')}`, "
                    f"`samples {dominant_exit_cluster.get('samples')}`"
                )
                lines.append(
                    f"- `{result['workload']}` dominant guard curins: "
                    f"`{','.join(str(curins) for curins in dominant_exit_cluster.get('guard_curins', []))}`"
                )
                dominant_guardmark = dominant_exit_cluster.get("dominant_guardmark")
                if dominant_guardmark:
                    lines.append(
                        f"- `{result['workload']}` dominant runtime `guardmark`: "
                        f"`curins {dominant_guardmark['curins']}` x `{dominant_guardmark['count']}`"
                    )
                exact_guard = dominant_exit_cluster.get("exact_guard")
                if exact_guard:
                    ir = exact_guard.get("trace_ir") or {}
                    kind_bits = []
                    for kind in exact_guard.get("guard_kinds", []):
                        kind_bits.append(
                            f"{kind.get('kind')} ofs {kind.get('ofs')} extra {kind.get('extra')} cc {kind.get('cc')}"
                        )
                    kind_text = ", ".join(kind_bits) if kind_bits else "kind detail unavailable"
                    lines.append(
                        f"- `{result['workload']}` exact runtime guard: "
                        f"`curins {exact_guard['curins']}`, `ir {ir.get('op', 'unknown')}`, "
                        f"`op1 {ir.get('op1', 'unknown')}`, `op2 {ir.get('op2', 'unknown')}`, `{kind_text}`"
                    )
                first_sload = dominant_exit_cluster.get("first_sload_guard")
                if first_sload:
                    ir = first_sload.get("trace_ir") or {}
                    kind_bits = []
                    for kind in first_sload.get("guard_kinds", []):
                        if kind.get("kind") == "sload_int":
                            kind_bits.append(
                                f"ofs {kind.get('ofs')} extra {kind.get('extra')} cc {kind.get('cc')}"
                            )
                    kind_text = ", ".join(kind_bits) if kind_bits else "kind detail unavailable"
                    lines.append(
                        f"- `{result['workload']}` first `sload_int`: "
                        f"`curins {first_sload['curins']}`, `ir {ir.get('op', 'unknown')}`, "
                        f"`op1 {ir.get('op1', 'unknown')}`, `op2 {ir.get('op2', 'unknown')}`, `{kind_text}`"
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
    parser.add_argument(
        "--no-counters",
        action="store_true",
        help="Do not install Lua trace/texit counter callbacks inside the probe script.",
    )
    parser.add_argument(
        "--no-posthooks",
        action="store_true",
        help="Do not emit post-run jit.util traceinfo/traceir helper loops inside the probe script.",
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
    if args.no_counters:
        extra_env["LUAJIT_S390X_PROBE_NO_COUNTERS"] = "1"
    if args.no_posthooks:
        extra_env["LUAJIT_S390X_PROBE_NO_POSTHOOKS"] = "1"
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
