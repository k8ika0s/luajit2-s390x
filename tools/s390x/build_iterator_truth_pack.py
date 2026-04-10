#!/usr/bin/env python3
"""Build a focused iterator performance truth pack from the frozen baseline."""

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
    "LUAJIT_S390X_MIXED_FFI_POST_STITCH_SAVE_DONE": "1",
    "LUAJIT_S390X_MIXED_FFI_FORL_PROTO_NOJIT": "1",
    "LUAJIT_S390X_FFI_CDATA_PAIR_SAVE_DONE": "1",
    "LUAJIT_S390X_FFI_CDATA_PAIR_FORL_BLACKLIST": "1",
    "LUAJIT_S390X_ITERATOR_ITERN_BLACKLIST": "1",
    "LUAJIT_S390X_ITERATOR_ITERL_BLACKLIST": "1",
    "LUAJIT_S390X_ITERATOR_ITERN_PROTO_NOJIT": "1",
}
CANDIDATE_ENVS: dict[str, dict[str, str]] = {
    "retained_baseline": RETAINED_BASELINE_ENV,
    "baseline": {
        "LUAJIT_S390X_DISABLE_HOTSIDE_CANON_SHARE_UGET_LOOPROOT": "1",
    },
    "hotside_canon_share_uget_looproot_default": {},
}
MICRO_NAMES = ("hash_value", "hash_key", "array_value")
FOCUSED_ITERATIONS = 80000
FOCUSED_VISIBLE_ITEMS = {
    "hash_value": 5,
    "hash_key": 3,
    "array_value": 5,
}
OWNER_SELECTION_ENV = {
    "LUAJIT_S390X_ITERN_FOCUS": "1",
    "LUAJIT_S390X_ITERN_FOCUS_PARENT": "1",
    "LUAJIT_S390X_ITERN_FOCUS_EXIT": "1",
    "LUAJIT_S390X_SIDE_FOCUS": "1",
    "LUAJIT_S390X_SIDE_FOCUS_PARENT": "1",
    "LUAJIT_S390X_SIDE_FOCUS_EXIT": "1",
    "LUAJIT_S390X_RECSTOP_LOG": "1",
    "LUAJIT_S390X_TRACE_META_LOG": "1",
    "LUAJIT_S390X_TRACE_START_LOG": "1",
    "LUAJIT_S390X_TRACE_ABORT_LOG": "1",
    "LUAJIT_S390X_JLOOP_EXIT_LOG": "1",
    "LUAJIT_S390X_JLOOP_EXIT_PARENT": "1",
    "LUAJIT_S390X_JLOOP_EXIT_EXIT": "1",
}

FOCUSED_BENCH_SCRIPT = """\
local bench = dofile("tests/s390x/perf/benchlib.lua")

local cases = {}

local function add_case(workload, expected, run)
  cases[#cases + 1] = {
    workload = workload,
    scale = "hot",
    iterations = 80000,
    warmup_runs = 2,
    run = run,
    validate = function(result)
      bench.eq(result, expected, workload .. "/hot")
    end,
  }
end

do
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
  add_case("hash_value", run(80000), run)
end

do
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
  add_case("hash_key", run(80000), run)
end

do
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
  add_case("array_value", run(80000), run)
end

bench.run_suite({ family = "iterator_truth_pack", cases = cases })
"""

TRACE_COUNT_SCRIPTS = {
    "hash_value": """\
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
jit.opt.start("hotloop=1")
local function emit_trace_hist(events)
  local buckets = {}
  for i = 1, #events do
    local ev = events[i]
    local kind = tostring(ev[1])
    local traceno = tonumber(ev[2])
    if traceno then
      local key = kind .. ":" .. traceno
      buckets[key] = (buckets[key] or 0) + 1
    end
  end
  local keys = {}
  for key in pairs(buckets) do keys[#keys + 1] = key end
  table.sort(keys)
  local parts = {}
  for i = 1, #keys do
    local key = keys[i]
    parts[#parts + 1] = key .. "=" .. buckets[key]
  end
  print("TRACE_HIST", table.concat(parts, ","))
end
local function emit_texit_hist(events)
  local buckets = {}
  for i = 1, #events do
    local ev = events[i]
    local traceno = tonumber(ev[1])
    local exitno = tonumber(ev[2])
    if traceno and exitno then
      local key = traceno .. ":" .. exitno
      buckets[key] = (buckets[key] or 0) + 1
    end
  end
  local keys = {}
  for key in pairs(buckets) do keys[#keys + 1] = key end
  table.sort(keys)
  local parts = {}
  for i = 1, #keys do
    local key = keys[i]
    parts[#parts + 1] = key .. "=" .. buckets[key]
  end
  print("TEXIT_HIST", table.concat(parts, ","))
end
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
run(20); run(20); run(20)
local trace_cap = testlib.trace_capture()
local texit_cap = testlib.texit_capture()
local result = run(80000)
trace_cap.stop()
texit_cap.stop()
print("RESULT", result)
print("TRACE_START", testlib.count_trace_events(trace_cap.events, "start"))
print("TRACE_STOP", testlib.count_trace_events(trace_cap.events, "stop"))
print("TRACE_ABORT", testlib.count_trace_events(trace_cap.events, "abort"))
print("TRACE_FLUSH", testlib.count_trace_events(trace_cap.events, "flush"))
print("TRACE_EVENT_COUNT", #trace_cap.events)
print("TEXIT_COUNT", #texit_cap.events)
emit_trace_hist(trace_cap.events)
emit_texit_hist(texit_cap.events)
""",
    "hash_key": """\
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
jit.opt.start("hotloop=1")
local function emit_trace_hist(events)
  local buckets = {}
  for i = 1, #events do
    local ev = events[i]
    local kind = tostring(ev[1])
    local traceno = tonumber(ev[2])
    if traceno then
      local key = kind .. ":" .. traceno
      buckets[key] = (buckets[key] or 0) + 1
    end
  end
  local keys = {}
  for key in pairs(buckets) do keys[#keys + 1] = key end
  table.sort(keys)
  local parts = {}
  for i = 1, #keys do
    local key = keys[i]
    parts[#parts + 1] = key .. "=" .. buckets[key]
  end
  print("TRACE_HIST", table.concat(parts, ","))
end
local function emit_texit_hist(events)
  local buckets = {}
  for i = 1, #events do
    local ev = events[i]
    local traceno = tonumber(ev[1])
    local exitno = tonumber(ev[2])
    if traceno and exitno then
      local key = traceno .. ":" .. exitno
      buckets[key] = (buckets[key] or 0) + 1
    end
  end
  local keys = {}
  for key in pairs(buckets) do keys[#keys + 1] = key end
  table.sort(keys)
  local parts = {}
  for i = 1, #keys do
    local key = keys[i]
    parts[#parts + 1] = key .. "=" .. buckets[key]
  end
  print("TEXIT_HIST", table.concat(parts, ","))
end
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
run(20); run(20); run(20)
local trace_cap = testlib.trace_capture()
local texit_cap = testlib.texit_capture()
local result = run(80000)
trace_cap.stop()
texit_cap.stop()
print("RESULT", result)
print("TRACE_START", testlib.count_trace_events(trace_cap.events, "start"))
print("TRACE_STOP", testlib.count_trace_events(trace_cap.events, "stop"))
print("TRACE_ABORT", testlib.count_trace_events(trace_cap.events, "abort"))
print("TRACE_FLUSH", testlib.count_trace_events(trace_cap.events, "flush"))
print("TRACE_EVENT_COUNT", #trace_cap.events)
print("TEXIT_COUNT", #texit_cap.events)
emit_trace_hist(trace_cap.events)
emit_texit_hist(texit_cap.events)
""",
    "array_value": """\
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
jit.opt.start("hotloop=1")
local function emit_trace_hist(events)
  local buckets = {}
  for i = 1, #events do
    local ev = events[i]
    local kind = tostring(ev[1])
    local traceno = tonumber(ev[2])
    if traceno then
      local key = kind .. ":" .. traceno
      buckets[key] = (buckets[key] or 0) + 1
    end
  end
  local keys = {}
  for key in pairs(buckets) do keys[#keys + 1] = key end
  table.sort(keys)
  local parts = {}
  for i = 1, #keys do
    local key = keys[i]
    parts[#parts + 1] = key .. "=" .. buckets[key]
  end
  print("TRACE_HIST", table.concat(parts, ","))
end
local function emit_texit_hist(events)
  local buckets = {}
  for i = 1, #events do
    local ev = events[i]
    local traceno = tonumber(ev[1])
    local exitno = tonumber(ev[2])
    if traceno and exitno then
      local key = traceno .. ":" .. exitno
      buckets[key] = (buckets[key] or 0) + 1
    end
  end
  local keys = {}
  for key in pairs(buckets) do keys[#keys + 1] = key end
  table.sort(keys)
  local parts = {}
  for i = 1, #keys do
    local key = keys[i]
    parts[#parts + 1] = key .. "=" .. buckets[key]
  end
  print("TEXIT_HIST", table.concat(parts, ","))
end
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
run(20); run(20); run(20)
local trace_cap = testlib.trace_capture()
local texit_cap = testlib.texit_capture()
local result = run(80000)
trace_cap.stop()
texit_cap.stop()
print("RESULT", result)
print("TRACE_START", testlib.count_trace_events(trace_cap.events, "start"))
print("TRACE_STOP", testlib.count_trace_events(trace_cap.events, "stop"))
print("TRACE_ABORT", testlib.count_trace_events(trace_cap.events, "abort"))
print("TRACE_FLUSH", testlib.count_trace_events(trace_cap.events, "flush"))
print("TRACE_EVENT_COUNT", #trace_cap.events)
print("TEXIT_COUNT", #texit_cap.events)
emit_trace_hist(trace_cap.events)
emit_texit_hist(texit_cap.events)
""",
}

PERF_STAT_SCRIPTS = {
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
run(20); run(20); run(20)
print("RESULT", run(80000))
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
run(20); run(20); run(20)
print("RESULT", run(80000))
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
run(20); run(20); run(20)
print("RESULT", run(80000))
""",
}

OWNER_SELECTION_SCRIPTS = {
    "hash_value": """\
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
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
run(20); run(20); run(20)
local trace_cap = testlib.trace_capture()
local texit_cap = testlib.texit_capture()
print("RESULT", run(250))
trace_cap.stop()
texit_cap.stop()
print("TRACE_START", testlib.count_trace_events(trace_cap.events, "start"))
print("TRACE_STOP", testlib.count_trace_events(trace_cap.events, "stop"))
print("TRACE_ABORT", testlib.count_trace_events(trace_cap.events, "abort"))
print("TEXIT_COUNT", #texit_cap.events)
""",
    "hash_key": """\
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
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
run(20); run(20); run(20)
local trace_cap = testlib.trace_capture()
local texit_cap = testlib.texit_capture()
print("RESULT", run(250))
trace_cap.stop()
texit_cap.stop()
print("TRACE_START", testlib.count_trace_events(trace_cap.events, "start"))
print("TRACE_STOP", testlib.count_trace_events(trace_cap.events, "stop"))
print("TRACE_ABORT", testlib.count_trace_events(trace_cap.events, "abort"))
print("TEXIT_COUNT", #texit_cap.events)
""",
    "array_value": """\
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
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
run(20); run(20); run(20)
local trace_cap = testlib.trace_capture()
local texit_cap = testlib.texit_capture()
print("RESULT", run(250))
trace_cap.stop()
texit_cap.stop()
print("TRACE_START", testlib.count_trace_events(trace_cap.events, "start"))
print("TRACE_STOP", testlib.count_trace_events(trace_cap.events, "stop"))
print("TRACE_ABORT", testlib.count_trace_events(trace_cap.events, "abort"))
print("TEXIT_COUNT", #texit_cap.events)
""",
}

TRACE_START_RE = re.compile(r"^---- TRACE (\d+) start(?: ([0-9]+)/([0-9]+))?")
TRACE_ABORT_RE = re.compile(r"^---- TRACE (\d+) abort .* -- (.+)$")
TRACE_STOP_RE = re.compile(r"^---- TRACE (\d+) stop -> (\w+)$")
RECITERN_RE = re.compile(
    r"^S390X_RECITERN trace=(\d+) parent=(\d+) exit=(\d+) .* newop=(\d+) .*?startpc=([^ ]+) key_nil=(\d+)$"
)
RECSETUP_RE = re.compile(
    r"^S390X_RECSETUP site=([^ ]+) trace=(\d+) parent=(\d+) exit=(\d+) root=(\d+) .* op=(\d+) .* startop=(\d+) "
)
RECSTOP_RE = re.compile(
    r"^S390X_RECSTOP trace=(\d+) parent=(\d+) exit=(\d+) pc=([^ ]+) op=(\d+) .* startop=(\d+) linktype=(\d+) link=(\d+) "
)
TRACE_META_RE = re.compile(
    r"^S390X_TRACE_META phase=([^ ]+) trace=(\d+) parent=(\d+) exit=(\d+) root=(\d+) link=(\d+) linktype=(\d+) .* startpc=([^ ]+) startop=(\d+) .* resumechild=(\d+) "
)
TRACE_META_SNAP_RE = re.compile(
    r"^S390X_TRACE_META_SNAP phase=([^ ]+) trace=(\d+) snap=(\d+) .* pc=([^ ]+) op=(\d+)$"
)
JLOOP_EXIT_RE = re.compile(
    r"^S390X_JLOOP_EXIT parent=(\d+) exit=(\d+) pc=([^ ]+) op=(\d+) target=(\d+) target_exec=(\d+) .* retop=(\d+) trace=(\d+) link=(\d+) linktype=(\d+) .* target_startop=(\d+) .* target_resumevalid=(\d+) target_resumechild=(\d+) .*$"
)
JLOOP_EXIT_PHASE_RE = re.compile(
    r"^S390X_JLOOP_EXIT phase=([^ ]+) parent=(\d+) exit=(\d+) trace=(\d+) retop=(\d+) state=(\d+)$"
)
LINNER_RE = re.compile(
    r"^S390X_LINNER site=([^ ]+) trace=(\d+) parent=(\d+) exit=(\d+) .* op=(\d+) .* startop=(\d+) ev=(\d+) lnk=(\d+) root=(\d+) "
)
TRACE_ABORT_META_RE = re.compile(
    r"^S390X_TRACE_ABORT trace=(\d+) parent=(\d+) exit=(\d+) .* startop=(\d+) err=(\d+) root=(\d+) link=(\d+) linktype=(\d+) "
)
ITERN_FOCUS_RE = re.compile(
    r"^S390X_ITERN_FOCUS site=([^ ]+) trace=(\d+) parent=(\d+) exit=(\d+) .* startop=(\d+) nextt=(\d+) keyflags=([^ ]+) .* key_nil=(\d+) .*"
)

SEAM_FIRST_LOOP_AFTER_HELPER = "first_loop_leave_after_helper_result"
SEAM_HELPER_REGION = "helper_call_or_result_region"
SEAM_PRE_HELPER_KEYINDEX = "hidden_keyindex_before_helper"
OWNER_FAMILY_CLOSED_LAZY_KEY = "same_closed_lazy_key_family"
OWNER_FAMILY_NEW_STOP_TARGET = "new_stop_target_seam"
OWNER_FAMILY_NEW_PRESERVED_GPR = "new_preserved_gpr_opportunity"
OWNER_FAMILY_ARRAY_ROOT_LINKED = "array_root_linked_side_path"


class TruthPackError(restamp.RestampError):
    """Truth pack helper failure."""


def write_text(path: pathlib.Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def write_json(path: pathlib.Path, payload: object) -> None:
    write_text(path, json.dumps(payload, indent=2, sort_keys=True) + "\n")


def parse_key_value_lines(text: str) -> dict[str, int | str]:
    result: dict[str, int | str] = {}
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        parts = line.split(None, 1)
        if len(parts) != 2:
            continue
        key, value = parts
        if re.fullmatch(r"-?\d+", value):
            result[key] = int(value)
        else:
            result[key] = value
    return result


def log_lines(path: pathlib.Path) -> list[str]:
    return [line.strip() for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]


def to_float_counter(value: object) -> float | None:
    if not isinstance(value, str):
        return None
    text = value.strip()
    if not text or text.startswith("<"):
        return None
    text = text.replace(",", "")
    try:
        return float(text)
    except ValueError:
        return None


def parse_dump_details(path: pathlib.Path) -> dict[str, object]:
    details: dict[str, object] = {
        "trace_events": [],
        "first_trace2_root_start": None,
        "first_trace2_root_abort": None,
        "first_trace2_side_start": None,
        "first_trace2_side_abort": None,
        "first_trace3_start": None,
        "first_trace3_abort": None,
    }
    trace_events: list[dict[str, object]] = []
    current_start: dict[str, object] | None = None
    for line in log_lines(path):
        match = TRACE_START_RE.match(line)
        if match:
            current_start = {
                "kind": "start",
                "trace": int(match.group(1)),
                "parent": int(match.group(2)) if match.group(2) else None,
                "exit": int(match.group(3)) if match.group(3) else None,
                "line": line,
            }
            trace_events.append(current_start)
            trace_no = current_start["trace"]
            if trace_no == 2 and current_start["parent"] is None and details["first_trace2_root_start"] is None:
                details["first_trace2_root_start"] = current_start
            elif trace_no == 2 and current_start["parent"] == 1 and current_start["exit"] == 1 and details["first_trace2_side_start"] is None:
                details["first_trace2_side_start"] = current_start
            elif trace_no == 3 and details["first_trace3_start"] is None:
                details["first_trace3_start"] = current_start
            continue
        match = TRACE_ABORT_RE.match(line)
        if match:
            event = {
                "kind": "abort",
                "trace": int(match.group(1)),
                "reason": match.group(2),
                "line": line,
            }
            trace_events.append(event)
            trace_no = event["trace"]
            if trace_no == 2:
                if current_start and current_start["parent"] is None and details["first_trace2_root_abort"] is None:
                    details["first_trace2_root_abort"] = event
                elif current_start and current_start["parent"] == 1 and current_start["exit"] == 1 and details["first_trace2_side_abort"] is None:
                    details["first_trace2_side_abort"] = event
            elif trace_no == 3 and details["first_trace3_abort"] is None:
                details["first_trace3_abort"] = event
            continue
        match = TRACE_STOP_RE.match(line)
        if match:
            trace_events.append(
                {
                    "kind": "stop",
                    "trace": int(match.group(1)),
                    "target": match.group(2),
                    "line": line,
                }
            )
    details["trace_events"] = trace_events
    return details


def parse_owner_selection_details(path: pathlib.Path) -> dict[str, object]:
    details: dict[str, object] = {
        "root_recitern": None,
        "root_stop": None,
        "root_meta": None,
        "root_snapshots": [],
        "first_jloop_exit": None,
        "first_jloop_phase": None,
        "first_root_candidate": None,
        "first_root_abort": None,
        "first_side_enter": None,
        "first_side_focus": None,
        "first_recstop_trace2": None,
        "first_trace2_meta": None,
    }
    root_snaps: list[dict[str, int | str]] = []
    for line in log_lines(path):
        match = RECITERN_RE.match(line)
        if match and match.group(1) == "1" and details["root_recitern"] is None:
            details["root_recitern"] = {
                "trace": int(match.group(1)),
                "parent": int(match.group(2)),
                "exit": int(match.group(3)),
                "newop": int(match.group(4)),
                "startpc": match.group(5),
                "key_nil": int(match.group(6)),
                "line": line,
            }
            continue
        match = RECSTOP_RE.match(line)
        if match:
            event = {
                "trace": int(match.group(1)),
                "parent": int(match.group(2)),
                "exit": int(match.group(3)),
                "pc": match.group(4),
                "op": int(match.group(5)),
                "startop": int(match.group(6)),
                "linktype": int(match.group(7)),
                "link": int(match.group(8)),
                "line": line,
            }
            if event["trace"] == 1 and details["root_stop"] is None:
                details["root_stop"] = event
            if event["trace"] == 2 and details["first_recstop_trace2"] is None:
                details["first_recstop_trace2"] = event
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
                "startpc": match.group(8),
                "startop": int(match.group(9)),
                "resumechild": int(match.group(10)),
                "line": line,
            }
            if event["trace"] == 1 and event["phase"] == "stop" and details["root_meta"] is None:
                details["root_meta"] = event
            if event["trace"] == 2 and details["first_trace2_meta"] is None:
                details["first_trace2_meta"] = event
            continue
        match = TRACE_META_SNAP_RE.match(line)
        if match and match.group(1) == "stop" and match.group(2) == "1":
            root_snaps.append(
                {
                    "snap": int(match.group(3)),
                    "pc": match.group(4),
                    "op": int(match.group(5)),
                    "line": line,
                }
            )
            continue
        match = JLOOP_EXIT_RE.match(line)
        if match and match.group(1) == "1" and match.group(2) == "1" and match.group(8) == "1" and details["first_jloop_exit"] is None:
            details["first_jloop_exit"] = {
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
                "target_startop": int(match.group(11)),
                "target_resumevalid": int(match.group(12)),
                "target_resumechild": int(match.group(13)),
                "line": line,
            }
            continue
        match = JLOOP_EXIT_PHASE_RE.match(line)
        if match and match.group(2) == "1" and match.group(3) == "1" and match.group(4) == "1" and details["first_jloop_phase"] is None:
            details["first_jloop_phase"] = {
                "phase": match.group(1),
                "parent": int(match.group(2)),
                "exit": int(match.group(3)),
                "trace": int(match.group(4)),
                "retop": int(match.group(5)),
                "state": int(match.group(6)),
                "line": line,
            }
            continue
        match = RECSETUP_RE.match(line)
        if match:
            event = {
                "site": match.group(1),
                "trace": int(match.group(2)),
                "parent": int(match.group(3)),
                "exit": int(match.group(4)),
                "root": int(match.group(5)),
                "op": int(match.group(6)),
                "startop": int(match.group(7)),
                "line": line,
            }
            if event["trace"] == 2 and event["site"] == "root_ready" and details["first_root_candidate"] is None:
                details["first_root_candidate"] = event
            if event["trace"] == 2 and event["site"] == "side_enter" and details["first_side_enter"] is None:
                details["first_side_enter"] = event
            continue
        match = LINNER_RE.match(line)
        if match and match.group(2) == "2" and details["first_root_abort"] is None:
            details["first_root_abort"] = {
                "site": match.group(1),
                "trace": int(match.group(2)),
                "parent": int(match.group(3)),
                "exit": int(match.group(4)),
                "op": int(match.group(5)),
                "startop": int(match.group(6)),
                "ev": int(match.group(7)),
                "lnk": int(match.group(8)),
                "root": int(match.group(9)),
                "line": line,
            }
            continue
        match = TRACE_ABORT_META_RE.match(line)
        if match and match.group(1) == "2" and details["first_root_abort"] is None:
            details["first_root_abort"] = {
                "site": "trace_abort",
                "trace": int(match.group(1)),
                "parent": int(match.group(2)),
                "exit": int(match.group(3)),
                "startop": int(match.group(4)),
                "err": int(match.group(5)),
                "root": int(match.group(6)),
                "link": int(match.group(7)),
                "linktype": int(match.group(8)),
                "line": line,
            }
            continue
        match = ITERN_FOCUS_RE.match(line)
        if match and match.group(2) == "2" and details["first_side_focus"] is None:
            details["first_side_focus"] = {
                "site": match.group(1),
                "trace": int(match.group(2)),
                "parent": int(match.group(3)),
                "exit": int(match.group(4)),
                "startop": int(match.group(5)),
                "nextt": int(match.group(6)),
                "keyflags": match.group(7),
                "key_nil": int(match.group(8)),
                "line": line,
            }
            continue
    details["root_snapshots"] = sorted(root_snaps, key=lambda item: int(item["snap"]))
    return details


def seam_site(details: dict[str, object]) -> str:
    recitern = details.get("root_recitern")
    if isinstance(recitern, dict):
        return SEAM_FIRST_LOOP_AFTER_HELPER
    if details.get("first_jloop_exit"):
        return SEAM_HELPER_REGION
    return SEAM_PRE_HELPER_KEYINDEX


def owner_family(name: str, details: dict[str, object], dump_details: dict[str, object]) -> str:
    recitern = details.get("root_recitern")
    first_phase = details.get("first_jloop_phase")
    first_trace2_abort = dump_details.get("first_trace2_side_abort")
    if name.startswith("hash"):
        if isinstance(recitern, dict) and int(recitern.get("key_nil", 0)) == 1:
            if isinstance(first_phase, dict) and first_phase.get("phase") == "dispatch-original":
                if not isinstance(first_trace2_abort, dict):
                    return OWNER_FAMILY_CLOSED_LAZY_KEY
                if "leaving loop in root trace" in str(first_trace2_abort.get("reason", "")):
                    return OWNER_FAMILY_CLOSED_LAZY_KEY
    if name == "array_value":
        return OWNER_FAMILY_ARRAY_ROOT_LINKED
    return OWNER_FAMILY_NEW_STOP_TARGET


def format_snapshots(snaps: list[dict[str, int | str]]) -> str:
    if not snaps:
        return "(none)"
    return ", ".join(f"snap{snap['snap']}@op{snap['op']}" for snap in snaps)


def parse_histogram(hist: object) -> dict[str, int]:
    if not isinstance(hist, str) or not hist or hist == "(none)":
        return {}
    result: dict[str, int] = {}
    for part in hist.split(","):
        if "=" not in part:
            continue
        key, value = part.split("=", 1)
        try:
            result[key] = int(value)
        except ValueError:
            continue
    return result


def focused_runtime_metrics(
    focused_jit_on: dict[str, dict[str, object]],
    focused_joff: dict[str, dict[str, object]],
    trace_counts: dict[str, dict[str, int | str]],
) -> dict[str, dict[str, object]]:
    metrics: dict[str, dict[str, object]] = {}
    for name in MICRO_NAMES:
        jit_record = focused_jit_on[f"{name}/hot"]
        joff_record = focused_joff[f"{name}/hot"]
        jit_median = float(jit_record["median_runtime_sec"])
        joff_median = float(joff_record["median_runtime_sec"])
        texits = int(trace_counts[name].get("TEXIT_COUNT", 0))
        items = FOCUSED_VISIBLE_ITEMS[name]
        work_items = FOCUSED_ITERATIONS * items
        hist = parse_histogram(trace_counts[name].get("TEXIT_HIST", ""))
        steady_exit_site = None
        steady_exit_count = 0
        if hist:
            steady_exit_site, steady_exit_count = max(hist.items(), key=lambda item: item[1])
        steady_exit_share = (steady_exit_count / texits) if texits else None
        texits_per_outer_iter = texits / FOCUSED_ITERATIONS
        texits_per_work_item = texits / work_items
        trace_starts = int(trace_counts[name].get("TRACE_START", 0))
        trace_aborts = int(trace_counts[name].get("TRACE_ABORT", 0))
        trace_abort_rate = (trace_aborts / trace_starts) if trace_starts else None
        gap_sec = jit_median - joff_median
        gap_ratio = (jit_median / joff_median) if joff_median else None
        runtime_ns_per_texit = (jit_median * 1.0e9 / texits) if texits else None
        if steady_exit_share is not None and steady_exit_share >= 0.90 and texits_per_work_item >= 1.0:
            classification = "exit-dominated"
        elif texits == 0 and gap_ratio is not None and gap_ratio > 1.0:
            classification = "compiled-body-dominated"
        else:
            classification = "mixed"
        metrics[name] = {
            "steady_exit_site": steady_exit_site,
            "steady_exit_count": steady_exit_count,
            "steady_exit_share": steady_exit_share,
            "texits_per_outer_iter": texits_per_outer_iter,
            "texits_per_work_item": texits_per_work_item,
            "trace_abort_rate": trace_abort_rate,
            "jit_median_sec": jit_median,
            "joff_median_sec": joff_median,
            "jit_gap_sec": gap_sec,
            "jit_gap_ratio": gap_ratio,
            "runtime_ns_per_texit": runtime_ns_per_texit,
            "classification": classification,
        }
    return metrics


def derive_perf_metrics(trace_counts: dict[str, dict[str, int | str]], perf_stats: dict[str, dict[str, object]]) -> dict[str, dict[str, object]]:
    metrics: dict[str, dict[str, object]] = {}
    for name in MICRO_NAMES:
        info = perf_stats[name]
        counters = info.get("counters", {}) if info.get("status") == "ok" else {}
        cycles = to_float_counter(counters.get("cycles")) if isinstance(counters, dict) else None
        instructions = to_float_counter(counters.get("instructions")) if isinstance(counters, dict) else None
        branches = to_float_counter(counters.get("branches")) if isinstance(counters, dict) else None
        branch_misses = to_float_counter(counters.get("branch-misses")) if isinstance(counters, dict) else None
        texits = int(trace_counts[name].get("TEXIT_COUNT", 0))
        cpi = (cycles / instructions) if cycles is not None and instructions not in (None, 0.0) else None
        branch_miss_rate = (branch_misses / branches) if branch_misses is not None and branches not in (None, 0.0) else None
        cycles_per_texit = (cycles / texits) if cycles is not None and texits else None
        if texits >= 1000:
            classification = "exit-dominated"
        elif texits > 0:
            classification = "mixed"
        elif cpi is not None and cpi >= 1.5:
            classification = "compiled-body-dominated"
        else:
            classification = "mixed"
        metrics[name] = {
            "cycles": cycles,
            "instructions": instructions,
            "branches": branches,
            "branch_misses": branch_misses,
            "texits": texits,
            "cpi": cpi,
            "branch_miss_rate": branch_miss_rate,
            "cycles_per_texit": cycles_per_texit,
            "classification": classification,
        }
    return metrics


def prepare_truth_scripts(host: str, remote_tmp: str) -> None:
    lines = ["set -euo pipefail"]
    lines.extend(
        [
            f'cat >"{remote_tmp}/focused_bench.lua" <<\'EOF\'',
            FOCUSED_BENCH_SCRIPT.rstrip(),
            "EOF",
        ]
    )
    for name, content in TRACE_COUNT_SCRIPTS.items():
        lines.extend(
            [
                f'cat >"{remote_tmp}/{name}_trace.lua" <<\'EOF\'',
                content.rstrip(),
                "EOF",
            ]
        )
    for name, content in PERF_STAT_SCRIPTS.items():
        lines.extend(
            [
                f'cat >"{remote_tmp}/{name}_perf.lua" <<\'EOF\'',
                content.rstrip(),
                "EOF",
            ]
        )
    for name, content in OWNER_SELECTION_SCRIPTS.items():
        lines.extend(
            [
                f'cat >"{remote_tmp}/{name}_owner.lua" <<\'EOF\'',
                content.rstrip(),
                "EOF",
            ]
        )
    proc = restamp.run_ssh_script(host, "\n".join(lines) + "\n")
    restamp.require_ok(proc, f"{host} truth-pack script setup")


def run_focused_bench(
    *,
    host: str,
    repo: str,
    remote_tmp: str,
    raw_dir: pathlib.Path,
    pin_core: int | None,
    samples: int,
    warmup: int,
    joff: bool,
    extra_env: dict[str, str] | None = None,
) -> list[dict[str, object]]:
    mode = "focused-jit-on" if not joff else "focused-joff"
    remote_json = f"{remote_tmp}/{mode}.jsonl"
    luajit_cmd = f"{f'taskset -c {pin_core} ' if pin_core is not None else ''}./src/luajit {'-joff ' if joff else ''}{shlex.quote(f'{remote_tmp}/focused_bench.lua')}"
    extra_env_prefix = restamp.remote_extra_env_prefix(extra_env)
    script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
rm -f {shlex.quote(remote_json)}
{restamp.remote_env_prefix(jsonl_path=remote_json, samples=samples, warmup=warmup)} {extra_env_prefix}{luajit_cmd}
"""
    restamp.run_remote_command(
        host,
        script,
        stdout_path=raw_dir / f"{mode}.stdout.log",
        stderr_path=raw_dir / f"{mode}.stderr.log",
        label=f"{host} {mode} hot medians",
    )
    json_text = restamp.fetch_remote_file(host, remote_json)
    local_json = raw_dir.parent / f"{mode}.jsonl"
    write_text(local_json, json_text)
    return restamp.parse_jsonl_records(local_json)


def run_mcode_dump(host: str, repo: str, remote_tmp: str, raw_dir: pathlib.Path, name: str,
                   extra_env: dict[str, str] | None = None) -> None:
    extra_env_prefix = restamp.remote_extra_env_prefix(extra_env)
    script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
{extra_env_prefix}./src/luajit -jdump=ism {shlex.quote(f"{remote_tmp}/{name}.lua")}
"""
    restamp.run_remote_command(
        host,
        script,
        stdout_path=raw_dir / f"{name}.stdout.log",
        stderr_path=raw_dir / f"{name}.stderr.log",
        label=f"{host} mcode dump {name}",
    )


def run_trace_count(host: str, repo: str, remote_tmp: str, raw_dir: pathlib.Path, name: str,
                    extra_env: dict[str, str] | None = None) -> dict[str, int | str]:
    extra_env_prefix = restamp.remote_extra_env_prefix(extra_env)
    script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
{extra_env_prefix}./src/luajit {shlex.quote(f"{remote_tmp}/{name}_trace.lua")}
"""
    proc = restamp.run_remote_command(
        host,
        script,
        stdout_path=raw_dir / f"{name}.stdout.log",
        stderr_path=raw_dir / f"{name}.stderr.log",
        label=f"{host} trace count {name}",
    )
    return parse_key_value_lines(proc.stdout)


def run_perf_stat(host: str, repo: str, remote_tmp: str, raw_dir: pathlib.Path, name: str,
                  pin_core: int | None, extra_env: dict[str, str] | None = None) -> dict[str, object]:
    taskset = f"taskset -c {pin_core} " if pin_core is not None else ""
    extra_env_prefix = restamp.remote_extra_env_prefix(extra_env)
    script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
if ! command -v perf >/dev/null 2>&1; then
  echo "PERF_STATUS unavailable:perf-not-found"
  exit 0
fi
perf stat -x, -e cycles,instructions,branches,branch-misses -- {extra_env_prefix}{taskset}./src/luajit {shlex.quote(f"{remote_tmp}/{name}_perf.lua")}
"""
    proc = restamp.run_ssh_script(host, script)
    write_text(raw_dir / f"{name}.stdout.log", proc.stdout)
    write_text(raw_dir / f"{name}.stderr.log", proc.stderr)
    if proc.returncode != 0:
        return {"status": "unavailable", "reason": f"exit-{proc.returncode}"}
    if "PERF_STATUS unavailable:" in proc.stdout:
        return {"status": "unavailable", "reason": proc.stdout.strip().split(":", 1)[1]}
    counters: dict[str, str] = {}
    for raw_line in proc.stderr.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        parts = [part.strip() for part in line.split(",")]
        if len(parts) < 3:
            continue
        value, _, event = parts[:3]
        if value in ("<not supported>", "<not counted>"):
            counters[event] = value
        else:
            counters[event] = value
    return {"status": "ok", "counters": counters}


def run_owner_selection_log(host: str, repo: str, remote_tmp: str, raw_dir: pathlib.Path, name: str,
                            extra_env: dict[str, str] | None = None) -> None:
    env_map = dict(OWNER_SELECTION_ENV)
    if extra_env:
        env_map.update(extra_env)
    env_prefix = "env " + " ".join(
        f"{key}={shlex.quote(value)}" for key, value in env_map.items()
    )
    script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
{env_prefix} ./src/luajit {shlex.quote(f"{remote_tmp}/{name}_owner.lua")}
"""
    restamp.run_remote_command(
        host,
        script,
        stdout_path=raw_dir / f"{name}.stdout.log",
        stderr_path=raw_dir / f"{name}.stderr.log",
        label=f"{host} owner-selection log {name}",
    )


def summarize_trace_decision(trace_counts: dict[str, dict[str, int | str]]) -> tuple[str, list[str]]:
    details = []
    materially_nonzero = False
    for name in MICRO_NAMES:
        info = trace_counts[name]
        starts = int(info.get("TRACE_START", 0))
        aborts = int(info.get("TRACE_ABORT", 0))
        exits = int(info.get("TEXIT_COUNT", 0))
        details.append(
            f"- `{name}`: trace starts `{starts}`, aborts `{aborts}`, texits `{exits}`"
        )
        if starts or aborts or exits:
            materially_nonzero = True
    if materially_nonzero:
        return ("exits_or_trace_activity_still_present", details)
    return ("steady_state_compiled_loop_throughput", details)


def hist_line(info: dict[str, int | str], key: str) -> str:
    value = info.get(key, "")
    return str(value) if value else "(none)"


def render_summary(
    *,
    host: str,
    repo: str,
    output_dir: pathlib.Path,
    commit: str,
    host_info: dict[str, str],
    jit_status: str,
    oneshot_results: dict[str, str],
    micro_results: dict[str, str],
    iterator_jit_on_records: list[dict[str, object]],
    iterator_joff_records: list[dict[str, object]],
    focused_records: list[dict[str, object]],
    focused_joff_records: list[dict[str, object]],
    trace_counts: dict[str, dict[str, int | str]],
    perf_stats: dict[str, dict[str, object]],
    owner_selection_analysis: dict[str, dict[str, object]],
    dump_analysis: dict[str, dict[str, object]],
    perf_metrics: dict[str, dict[str, object]],
    runtime_metrics: dict[str, dict[str, object]],
    pin_core: int | None,
    samples: int,
    warmup: int,
    freeze_branch: str,
) -> str:
    iterator_jit_on = restamp.perf_index(iterator_jit_on_records)
    iterator_joff = restamp.perf_index(iterator_joff_records)
    focused = restamp.perf_index(focused_records)
    focused_joff = restamp.perf_index(focused_joff_records)
    decision, trace_lines = summarize_trace_decision(trace_counts)
    lines = [
        "# Iterator Truth Pack",
        "",
        f"- Timestamp: `{dt.datetime.now().astimezone().strftime('%Y-%m-%d %H:%M:%S %Z')}`",
        f"- Freeze branch: `{freeze_branch}`",
        f"- Host label: `{host}`",
        f"- Hostname: `{host_info.get('HOSTNAME_FQDN', host)}`",
        f"- Machine type: `{host_info.get('MACHINE_TYPE', 'unknown')}` (`{host_info.get('GENERATION', 'unknown')}`)",
        f"- Model: `{host_info.get('MODEL', 'unknown')}`",
        f"- Repo: `{repo}`",
        f"- Commit: `{commit}`",
        f"- Benchmark: `{restamp.BENCH_FILE}`",
        f"- Pinned core: `{pin_core if pin_core is not None else 'unbound'}`",
        f"- Samples: `{samples}`",
        f"- Warmup runs: `{warmup}`",
        f"- `jit.status()`: `{jit_status}`",
        "",
        "## Baseline Checks",
        "",
        f"- `/tmp/oneshot_iter.lua 20`: `{oneshot_results['20']}`",
        f"- `/tmp/oneshot_iter.lua 2000`: `{oneshot_results['2000']}`",
        f"- `/tmp/oneshot_iter.lua 200000`: `{oneshot_results['200000']}`",
        f"- `-joff /tmp/oneshot_iter.lua 200000`: `{oneshot_results['joff_200000']}`",
        f"- `HASH_VALUE`: `{micro_results['hash_value']}`",
        f"- `HASH_KEY`: `{micro_results['hash_key']}`",
        f"- `ARRAY_VALUE`: `{micro_results['array_value']}`",
        "",
        "## Iterator Table Baseline",
        "",
        f"- `pairs_sum/hot` JIT-on median `{iterator_jit_on['pairs_sum/hot']['median_runtime_sec']:.6f}s`",
        f"- `pairs_array_sum/hot` JIT-on median `{iterator_jit_on['pairs_array_sum/hot']['median_runtime_sec']:.6f}s`",
        f"- `pairs_sum/hot` `-joff` median `{iterator_joff['pairs_sum/hot']['median_runtime_sec']:.6f}s`",
        f"- `pairs_array_sum/hot` `-joff` median `{iterator_joff['pairs_array_sum/hot']['median_runtime_sec']:.6f}s`",
        "",
        "## Focused Hot Medians",
        "",
    ]
    for name in MICRO_NAMES:
        record = focused[f"{name}/hot"]
        joff_record = focused_joff[f"{name}/hot"]
        lines.append(
            f"- `{name}/hot` median `{record['median_runtime_sec']:.6f}s`, "
            f"p95 `{record['p95_runtime_sec']:.6f}s`, samples `{restamp.format_samples(record['samples_sec'])}`"
        )
        lines.append(
            f"  - `-joff` median `{joff_record['median_runtime_sec']:.6f}s`, gap `{record['median_runtime_sec'] - joff_record['median_runtime_sec']:+.6f}s`, ratio `{record['median_runtime_sec'] / joff_record['median_runtime_sec']:.2f}x`"
        )

    lines.extend(["", "## Trace / Exit Counts After Warmup", ""])
    for i, name in enumerate(MICRO_NAMES):
        lines.append(trace_lines[i])
        lines.append(f"  - trace histogram `{hist_line(trace_counts[name], 'TRACE_HIST')}`")
        lines.append(f"  - texit histogram `{hist_line(trace_counts[name], 'TEXIT_HIST')}`")
    lines.append(f"- Decision: `{decision}`")

    lines.extend(["", "## Exit Seam Attribution", ""])
    for name in ("hash_value", "array_value"):
        analysis = owner_selection_analysis[name]
        dump_info = dump_analysis[name]
        seam = seam_site(analysis)
        family = owner_family(name, analysis, dump_info)
        first_phase = analysis.get("first_jloop_phase")
        first_root_candidate = analysis.get("first_root_candidate")
        first_side_enter = analysis.get("first_side_enter")
        lines.append(
            f"- `{name}` seam `{seam}`, family `{family}`"
        )
        if isinstance(first_phase, dict):
            lines.append(
                f"  - repeated `trace 1 exit 1`: phase `{first_phase['phase']}`, retop `{first_phase['retop']}`"
            )
        lines.append(
            f"  - root snapshot order `{format_snapshots(analysis.get('root_snapshots', []))}`"
        )
        if isinstance(first_root_candidate, dict):
            lines.append(
                f"  - first root candidate: site `{first_root_candidate['site']}`, trace `{first_root_candidate['trace']}`, startop `{first_root_candidate['startop']}`"
            )
        if isinstance(first_side_enter, dict):
            lines.append(
                f"  - first side candidate: site `{first_side_enter['site']}`, trace `{first_side_enter['trace']}`, parent `{first_side_enter['parent']}`, exit `{first_side_enter['exit']}`, startop `{first_side_enter['startop']}`"
            )
        first_trace2_root_start = dump_info.get("first_trace2_root_start")
        first_trace2_root_abort = dump_info.get("first_trace2_root_abort")
        first_trace2_side_start = dump_info.get("first_trace2_side_start")
        first_trace2_side_abort = dump_info.get("first_trace2_side_abort")
        if isinstance(first_trace2_root_start, dict):
            lines.append(
                f"  - first root `TRACE 2` start: `{first_trace2_root_start['line']}`"
            )
        if isinstance(first_trace2_root_abort, dict):
            lines.append(
                f"  - first root `TRACE 2` abort: `{first_trace2_root_abort['line']}`"
            )
        if isinstance(first_trace2_side_start, dict):
            lines.append(
                f"  - first side `TRACE 2 start 1/1`: `{first_trace2_side_start['line']}`"
            )
        if isinstance(first_trace2_side_abort, dict):
            lines.append(
                f"  - first side `TRACE 2` abort: `{first_trace2_side_abort['line']}`"
            )

    lines.extend(["", "## Stop-Target Autopsy", ""])
    for name in MICRO_NAMES:
        analysis = owner_selection_analysis[name]
        first_root_candidate = analysis.get("first_root_candidate")
        first_root_abort = analysis.get("first_root_abort")
        first_side_enter = analysis.get("first_side_enter")
        first_side_focus = analysis.get("first_side_focus")
        first_recstop_trace2 = analysis.get("first_recstop_trace2")
        first_trace2_meta = analysis.get("first_trace2_meta")
        lines.append(f"- `{name}`")
        if isinstance(first_root_candidate, dict):
            lines.append(
                f"  - root candidate: site `{first_root_candidate['site']}`, trace `{first_root_candidate['trace']}`, parent `{first_root_candidate['parent']}`, exit `{first_root_candidate['exit']}`, startop `{first_root_candidate['startop']}`, op `{first_root_candidate['op']}`"
            )
        if isinstance(first_root_abort, dict):
            detail = f"site `{first_root_abort.get('site')}`, trace `{first_root_abort.get('trace')}`, startop `{first_root_abort.get('startop')}`"
            if "err" in first_root_abort:
                detail += f", err `{first_root_abort['err']}`"
            lines.append(f"  - first root abort: {detail}")
        if isinstance(first_side_enter, dict):
            lines.append(
                f"  - first side enter: trace `{first_side_enter['trace']}`, parent `{first_side_enter['parent']}`, exit `{first_side_enter['exit']}`, root `{first_side_enter['root']}`, startop `{first_side_enter['startop']}`"
            )
        if isinstance(first_side_focus, dict):
            lines.append(
                f"  - first side focus: site `{first_side_focus['site']}`, nextt `{first_side_focus['nextt']}`, key_nil `{first_side_focus['key_nil']}`, keyflags `{first_side_focus['keyflags']}`"
            )
        if isinstance(first_recstop_trace2, dict):
            lines.append(
                f"  - first trace-2 stop: linktype `{first_recstop_trace2['linktype']}`, link `{first_recstop_trace2['link']}`, startop `{first_recstop_trace2['startop']}`"
            )
        if isinstance(first_trace2_meta, dict):
            lines.append(
                f"  - first trace-2 meta: root `{first_trace2_meta['root']}`, linktype `{first_trace2_meta['linktype']}`, link `{first_trace2_meta['link']}`, resumechild `{first_trace2_meta['resumechild']}`"
            )

    lines.extend(["", "## perf stat", ""])
    for name in MICRO_NAMES:
        info = perf_stats[name]
        if info["status"] != "ok":
            lines.append(f"- `{name}`: unavailable (`{info['reason']}`)")
            continue
        counters = info["counters"]
        metrics = perf_metrics[name]
        lines.append(
            f"- `{name}`: cycles `{counters.get('cycles', 'n/a')}`, "
            f"instructions `{counters.get('instructions', 'n/a')}`, "
            f"branches `{counters.get('branches', 'n/a')}`, "
            f"branch-misses `{counters.get('branch-misses', 'n/a')}`"
        )
        lines.append(
            f"  - CPI `{metrics['cpi']:.3f}`"
            if isinstance(metrics.get("cpi"), float)
            else "  - CPI `n/a`"
        )
        lines.append(
            f"  - branch-miss rate `{metrics['branch_miss_rate'] * 100.0:.2f}%`"
            if isinstance(metrics.get("branch_miss_rate"), float)
            else "  - branch-miss rate `n/a`"
        )
        lines.append(
            f"  - cycles per texit `{metrics['cycles_per_texit']:.2f}`"
            if isinstance(metrics.get("cycles_per_texit"), float)
            else "  - cycles per texit `n/a`"
        )
        lines.append(
            f"  - classification `{metrics['classification']}`"
        )

    lines.extend(["", "## Runtime Fallback Attribution", ""])
    for name in MICRO_NAMES:
        metrics = runtime_metrics[name]
        lines.append(
            f"- `{name}`: steady exit `{metrics['steady_exit_site'] or 'n/a'}` share `{metrics['steady_exit_share'] * 100.0:.2f}%`"
            if isinstance(metrics.get("steady_exit_share"), float)
            else f"- `{name}`: steady exit `n/a`"
        )
        lines.append(
            f"  - texits per outer iter `{metrics['texits_per_outer_iter']:.2f}`, texits per work item `{metrics['texits_per_work_item']:.3f}`"
        )
        lines.append(
            f"  - trace abort rate `{metrics['trace_abort_rate'] * 100.0:.2f}%`"
            if isinstance(metrics.get("trace_abort_rate"), float)
            else "  - trace abort rate `n/a`"
        )
        lines.append(
            f"  - runtime ns per texit `{metrics['runtime_ns_per_texit']:.2f}`"
            if isinstance(metrics.get("runtime_ns_per_texit"), float)
            else "  - runtime ns per texit `n/a`"
        )
        lines.append(
            f"  - classification `{metrics['classification']}`"
        )

    lines.extend(
        [
            "",
            "## Raw Artifacts",
            "",
            f"- Iterator baseline JSONL: `{output_dir / 'iterator-jit-on.jsonl'}`",
            f"- Iterator `-joff` JSONL: `{output_dir / 'iterator-joff.jsonl'}`",
            f"- Focused medians JSONL: `{output_dir / 'focused-jit-on.jsonl'}`",
            f"- Focused `-joff` JSONL: `{output_dir / 'focused-joff.jsonl'}`",
            f"- Trace counts: `{output_dir / 'raw' / 'trace-counts'}`",
            f"- Owner logs: `{output_dir / 'raw' / 'owner'}`",
            f"- Owner-selection logs: `{output_dir / 'raw' / 'owner-selection'}`",
            f"- IR + snapshot + mcode dumps: `{output_dir / 'raw' / 'dump'}`",
            f"- perf stat logs: `{output_dir / 'raw' / 'perf-stat'}`",
        ]
    )
    return "\n".join(lines) + "\n"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build a focused iterator truth pack from the frozen baseline.")
    parser.add_argument("--host", choices=restamp.HOST_LABELS, required=True)
    parser.add_argument("--candidate", choices=tuple(CANDIDATE_ENVS.keys()), default="retained_baseline")
    parser.add_argument("--repo", help="Remote clean repo path. Defaults to the authoritative repo for the selected host.")
    parser.add_argument("--output-dir", help="Local artifact output directory. Defaults under artifacts/s390x/truth-packs.")
    parser.add_argument("--pin-core", type=int, default=restamp.DEFAULT_PIN_CORE)
    parser.add_argument("--samples", type=int, default=restamp.DEFAULT_SAMPLES)
    parser.add_argument("--warmup", type=int, default=restamp.DEFAULT_WARMUP)
    parser.add_argument("--skip-sync", action="store_true")
    parser.add_argument("--freeze-branch", default="k8ika0s/s390x-jit-on-freeze-20260331")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    host = args.host
    candidate = args.candidate
    candidate_env = CANDIDATE_ENVS[candidate]
    repo = args.repo or restamp.AUTHORITATIVE_REPOS[host]
    timestamp = dt.datetime.now().astimezone().strftime("%Y%m%d-%H%M%S")
    output_dir = pathlib.Path(args.output_dir).expanduser().resolve() if args.output_dir else (DEFAULT_OUTPUT_ROOT / f"{timestamp}-{host}-{candidate}-iterator-truth-pack").resolve()
    raw_dir = output_dir / "raw"
    owner_dir = raw_dir / "owner"
    owner_selection_dir = raw_dir / "owner-selection"
    dump_dir = raw_dir / "dump"
    trace_dir = raw_dir / "trace-counts"
    perf_dir = raw_dir / "perf-stat"
    output_dir.mkdir(parents=True, exist_ok=True)
    owner_dir.mkdir(parents=True, exist_ok=True)
    owner_selection_dir.mkdir(parents=True, exist_ok=True)
    dump_dir.mkdir(parents=True, exist_ok=True)
    trace_dir.mkdir(parents=True, exist_ok=True)
    perf_dir.mkdir(parents=True, exist_ok=True)

    commit = restamp.current_commit()
    host_info = restamp.collect_host_info(host)

    if not args.skip_sync:
        restamp.sync_tracked_files(host, repo)

    remote_tmp = restamp.prepare_remote_scripts(host)
    try:
        prepare_truth_scripts(host, remote_tmp)
        restamp.build_remote_repo(host, repo, raw_dir)
        jit_status = restamp.run_jit_status(host, repo, raw_dir)
        oneshot_results = restamp.run_oneshot_checks(host, repo, remote_tmp, raw_dir, candidate_env)
        micro_results = {
            "hash_value": restamp.run_micro(host, repo, remote_tmp, raw_dir, "hash_value", candidate_env),
            "hash_key": restamp.run_micro(host, repo, remote_tmp, raw_dir, "hash_key", candidate_env),
            "array_value": restamp.run_micro(host, repo, remote_tmp, raw_dir, "array_value", candidate_env),
        }
        restamp.validate_results(
            host=host,
            jit_status=jit_status,
            oneshot_results=oneshot_results,
            micro_results=micro_results,
        )

        iterator_jit_on = restamp.run_iterator_bench(
            host=host,
            repo=repo,
            remote_tmp=remote_tmp,
            raw_dir=raw_dir,
            mode_label="iterator-jit-on",
            pin_core=args.pin_core,
            samples=args.samples,
            warmup=args.warmup,
            joff=False,
            extra_env=candidate_env,
        )
        iterator_joff = restamp.run_iterator_bench(
            host=host,
            repo=repo,
            remote_tmp=remote_tmp,
            raw_dir=raw_dir,
            mode_label="iterator-joff",
            pin_core=args.pin_core,
            samples=args.samples,
            warmup=args.warmup,
            joff=True,
            extra_env=candidate_env,
        )
        (output_dir / "jit-on.jsonl").rename(output_dir / "iterator-jit-on.jsonl")
        (output_dir / "joff.jsonl").rename(output_dir / "iterator-joff.jsonl")

        focused_records = run_focused_bench(
            host=host,
            repo=repo,
            remote_tmp=remote_tmp,
            raw_dir=raw_dir,
            pin_core=args.pin_core,
            samples=args.samples,
            warmup=args.warmup,
            joff=False,
            extra_env=candidate_env,
        )
        focused_joff_records = run_focused_bench(
            host=host,
            repo=repo,
            remote_tmp=remote_tmp,
            raw_dir=raw_dir,
            pin_core=args.pin_core,
            samples=args.samples,
            warmup=args.warmup,
            joff=True,
            extra_env=candidate_env,
        )
        trace_counts: dict[str, dict[str, int | str]] = {}
        perf_stats: dict[str, dict[str, object]] = {}
        owner_selection_analysis: dict[str, dict[str, object]] = {}
        dump_analysis: dict[str, dict[str, object]] = {}
        for name in MICRO_NAMES:
            restamp.run_owner_logs(host, repo, remote_tmp, owner_dir, name, candidate_env)
            run_owner_selection_log(host, repo, remote_tmp, owner_selection_dir, name, candidate_env)
            run_mcode_dump(host, repo, remote_tmp, dump_dir, name, candidate_env)
            trace_counts[name] = run_trace_count(host, repo, remote_tmp, trace_dir, name, candidate_env)
            perf_stats[name] = run_perf_stat(host, repo, remote_tmp, perf_dir, name, args.pin_core, candidate_env)
            owner_selection_analysis[name] = parse_owner_selection_details(owner_selection_dir / f"{name}.stderr.log")
            dump_analysis[name] = parse_dump_details(dump_dir / f"{name}.stdout.log")
        perf_metrics = derive_perf_metrics(trace_counts, perf_stats)
        runtime_metrics = focused_runtime_metrics(
            restamp.perf_index(focused_records),
            restamp.perf_index(focused_joff_records),
            trace_counts,
        )

        metadata = {
            "timestamp": dt.datetime.now(dt.timezone.utc).isoformat(),
            "git_commit": commit,
            "freeze_branch": args.freeze_branch,
            "host_label": host,
            "candidate": candidate,
            "candidate_env": candidate_env,
            "hostname": host_info.get("HOSTNAME_FQDN", host),
            "host_shortname": host_info.get("HOSTNAME_SHORT", host),
            "machine_type": host_info.get("MACHINE_TYPE", ""),
            "generation": host_info.get("GENERATION", "unknown"),
            "model": host_info.get("MODEL", ""),
            "repo_path": repo,
            "benchmark_file": restamp.BENCH_FILE,
            "pin_core": args.pin_core,
            "sample_count": args.samples,
            "warmup_runs": args.warmup,
            "jit_status": jit_status,
            "focused_micros": list(MICRO_NAMES),
            "trace_exit_decision": summarize_trace_decision(trace_counts)[0],
            "owner_selection_env": OWNER_SELECTION_ENV,
            "runtime_fallback_metrics": runtime_metrics,
            "seam_attribution": {
                name: {
                    "semantic_site": seam_site(owner_selection_analysis[name]),
                    "owner_family": owner_family(name, owner_selection_analysis[name], dump_analysis[name]),
                    "root_snapshots": owner_selection_analysis[name].get("root_snapshots", []),
                }
                for name in MICRO_NAMES
            },
            "derived_perf_metrics": perf_metrics,
        }
        write_json(output_dir / "metadata.json", metadata)
        summary = render_summary(
            host=host,
            repo=repo,
            output_dir=output_dir,
            commit=commit,
            host_info=host_info,
            jit_status=jit_status,
            oneshot_results=oneshot_results,
            micro_results=micro_results,
            iterator_jit_on_records=iterator_jit_on,
            iterator_joff_records=iterator_joff,
            focused_records=focused_records,
            focused_joff_records=focused_joff_records,
            trace_counts=trace_counts,
            perf_stats=perf_stats,
            owner_selection_analysis=owner_selection_analysis,
            dump_analysis=dump_analysis,
            perf_metrics=perf_metrics,
            runtime_metrics=runtime_metrics,
            pin_core=args.pin_core,
            samples=args.samples,
            warmup=args.warmup,
            freeze_branch=args.freeze_branch,
        )
        write_text(output_dir / "summary.md", summary)
    finally:
        restamp.cleanup_remote_scripts(host, remote_tmp)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except TruthPackError as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
    except restamp.RestampError as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
