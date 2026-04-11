#!/usr/bin/env python3
"""Build a broader JIT-throughput truth pack on native s390x."""

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
DEFAULT_OUTPUT_ROOT = ROOT / "artifacts" / "s390x" / "truth-packs"
PROBE_TIMEOUT_SECS = 20
CANDIDATE_ENVS: dict[str, dict[str, str]] = {
    "retained_baseline": restamp.RETAINED_BASELINE_ENV,
    "baseline": {
        "LUAJIT_S390X_DISABLE_HOTSIDE_CANON_SHARE_UGET_LOOPROOT": "1",
    },
    "hotside_canon_share_uget_looproot_default": {},
    "hotside_canon_share": {
        "LUAJIT_S390X_HOTSIDE_CANON_SHARE_EQUIV": "1",
    },
    "hotside_canon_share_uget_looproot": {
        "LUAJIT_S390X_HOTSIDE_CANON_SHARE_UGET_LOOPROOT": "1",
    },
}

CANDIDATE_SCOPE: dict[str, dict[str, Any]] = {
    "hotside_canon_share_uget_looproot": {
        "summary": (
            "Scoped to the repeated exit=0 / BC_UGET / BC_JMP / loop-root seam. "
            "Treat reduced UGET/looproot siblings, be_helpers, and ffi_calls as "
            "core promotion evidence. Carry retconst_loop, retlast_loop, and "
            "mixed_loop as same-seam secondary evidence. Keep sum_loop out of "
            "this candidate's evidence because the active vararg frontier is a "
            "distinct nested-callee handoff seam, not a same-seam dominated "
            "tail. Treat dispatch_trace, iterator_table, mixed_ffi, ffi_cdata, "
            "int_add_phi_only, and logic_add_phi_noboundary as out of scope on "
            "the current mechanism."
        ),
        "workloads": {
            "add_phi_only": "out_of_scope",
            "logic_add_phi_noboundary": "out_of_scope",
            "chain_tail_add": "promotion_core",
            "chain_tail_store": "promotion_core",
            "mix_bits": "promotion_core",
            "number_helper_loop": "promotion_core",
            "be_pack_loop": "promotion_core",
            "direct_abs": "promotion_core",
            "stored_abs": "promotion_core",
            "retconst_loop": "promotion_secondary",
            "retlast_loop": "promotion_secondary",
            "mixed_loop": "promotion_secondary",
            "sum_loop": "out_of_scope",
            "mixed_ffi_loop": "out_of_scope",
            "pair_loop": "out_of_scope",
            "mixed_width_loop": "out_of_scope",
            "pairs_sum": "out_of_scope",
            "pairs_array_sum": "out_of_scope",
            "numeric_loop": "out_of_scope",
            "side_exit_loop": "out_of_scope",
            "hotexit_loop": "out_of_scope",
        },
    },
}

CANDIDATE_SCOPE["hotside_canon_share_uget_looproot_default"] = (
    CANDIDATE_SCOPE["hotside_canon_share_uget_looproot"]
)

PROMOTION_CLASS_ORDER = {
    "promotion_core": 0,
    "promotion_secondary": 1,
    "same_seam_but_dominated": 2,
    "out_of_scope": 3,
    "unclassified": 4,
}


def scope_status_for_workload(candidate: str, workload: str) -> str:
    candidate_scope = CANDIDATE_SCOPE.get(candidate, {})
    workload_scope = candidate_scope.get("workloads", {})
    return str(workload_scope.get(workload, "unclassified"))


def family_scope_summary(candidate: str, config: dict[str, Any]) -> dict[str, Any]:
    hot_workloads = [hot_key.split("/", 1)[0] for hot_key in config["hot_cases"]]
    statuses = {workload: scope_status_for_workload(candidate, workload) for workload in hot_workloads}
    unique_statuses = sorted(
        set(statuses.values()),
        key=lambda status: (PROMOTION_CLASS_ORDER.get(status, 99), status),
    )
    if unique_statuses == ["promotion_core"]:
        family_status = "promotion_core"
        promotion_action = "eligible_first_enable_set"
    elif unique_statuses == ["promotion_secondary"]:
        family_status = "promotion_secondary"
        promotion_action = "carry_forward_only"
    elif unique_statuses == ["same_seam_but_dominated"]:
        family_status = "same_seam_but_dominated"
        promotion_action = "exclude_from_enable_set"
    elif unique_statuses == ["out_of_scope"]:
        family_status = "out_of_scope"
        promotion_action = "exclude_from_candidate"
    elif unique_statuses == ["promotion_secondary", "same_seam_but_dominated"]:
        family_status = "secondary_with_dominated_tail"
        promotion_action = "keep_family_out_of_first_enable_set"
    else:
        family_status = "mixed_scope"
        promotion_action = "manual_review"
    return {
        "hot_workloads": hot_workloads,
        "workload_statuses": statuses,
        "family_status": family_status,
        "promotion_action": promotion_action,
    }


def load_ir_op_names() -> dict[int, str]:
    names: dict[int, str] = {}
    in_irdef = False
    idx = 0
    for raw_line in (ROOT / "src" / "lj_ir.h").read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if line.startswith("#define IRDEF("):
            in_irdef = True
            continue
        if in_irdef and line.startswith("/* -- Named IR literals"):
            break
        if not in_irdef:
            continue
        match = re.match(r"_\(([^,]+),", line)
        if match:
            names[idx] = match.group(1)
            idx += 1
    return names


IR_OP_NAMES = load_ir_op_names()


VARARG_FOCUSED_BENCH = """\
local bit = require("bit")
local bench = dofile("tests/s390x/perf/benchlib.lua")

local function sum(...)
  local total = 0
  for i = 1, select("#", ...) do
    total = total + select(i, ...)
  end
  return total
end

local function retlast(...)
  return select(select("#", ...), ...)
end

local function retconst(...)
  return 42
end

local cases = {
  {
    workload = "sum_loop",
    scale = "hot",
    iterations = 16000,
    warmup_runs = 2,
    run = function(n)
      local result = 0
      for i = 1, n do
        result = bit.tobit(result + sum(1, 2, 3, ((i - 1) % 17) + 1))
      end
      return result
    end,
      validate = function(result)
        bench.eq(result, 239979, "sum_loop/hot")
      end,
  },
  {
    workload = "retlast_loop",
    scale = "hot",
    iterations = 16000,
    warmup_runs = 2,
    run = function(n)
      local result = 0
      for i = 1, n do
        result = bit.tobit(result + retlast(1, 2, 3, ((i - 1) % 17) + 1))
      end
      return result
    end,
      validate = function(result)
        bench.eq(result, 143979, "retlast_loop/hot")
      end,
  },
  {
    workload = "retconst_loop",
    scale = "hot",
    iterations = 16000,
    warmup_runs = 2,
    run = function(n)
      local result = 0
      for i = 1, n do
        result = bit.tobit(result + retconst(1, 2, 3, i))
      end
      return result
    end,
    validate = function(result)
      bench.eq(result, 672000, "retconst_loop/hot")
    end,
  },
}

bench.run_suite({ family = "vararg_truth_pack", cases = cases })
"""

VARARG_CHECK_SCRIPTS = {
    "sum_loop": """\
local bit = require("bit")
local function sum(...)
  local total = 0
  for i = 1, select("#", ...) do
    total = total + select(i, ...)
  end
  return total
end
local function run(n)
  local result = 0
  for i = 1, n do
    result = bit.tobit(result + sum(1, 2, 3, ((i - 1) % 17) + 1))
  end
  return result
end
print("SUM_LOOP", run(20))
""",
    "retlast_loop": """\
local bit = require("bit")
local function retlast(...)
  return select(select("#", ...), ...)
end
local function run(n)
  local result = 0
  for i = 1, n do
    result = bit.tobit(result + retlast(1, 2, 3, ((i - 1) % 17) + 1))
  end
  return result
end
print("RETLAST_LOOP", run(20))
""",
    "retconst_loop": """\
local bit = require("bit")
local function retconst(...)
  return 42
end
local function run(n)
  local result = 0
  for i = 1, n do
    result = bit.tobit(result + retconst(1, 2, 3, i))
  end
  return result
end
print("RETCONST_LOOP", run(20))
""",
}

VARARG_TRACE_SCRIPTS = {
    "sum_loop": """\
local bit = require("bit")
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
jit.opt.start("hotloop=1")
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
local function sum(...)
  local total = 0
  for i = 1, select("#", ...) do
    total = total + select(i, ...)
  end
  return total
end
local function run(n)
  local result = 0
  for i = 1, n do
    result = bit.tobit(result + sum(1, 2, 3, ((i - 1) % 17) + 1))
  end
  return result
end
run(20); run(20); run(20)
local trace_cap = testlib.trace_counter_capture_lite()
local texit_cap = testlib.texit_counter_capture_lite()
print("RESULT", run(16000))
trace_cap.stop()
texit_cap.stop()
print("TRACE_START", trace_cap.start)
print("TRACE_STOP", trace_cap.stop_count)
print("TRACE_ABORT", trace_cap.abort)
print("TEXIT_COUNT", texit_cap.total)
""",
    "retlast_loop": """\
local bit = require("bit")
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
jit.opt.start("hotloop=1")
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
local function retlast(...)
  return select(select("#", ...), ...)
end
local function run(n)
  local result = 0
  for i = 1, n do
    result = bit.tobit(result + retlast(1, 2, 3, ((i - 1) % 17) + 1))
  end
  return result
end
run(20); run(20); run(20)
local trace_cap = testlib.trace_counter_capture_lite()
local texit_cap = testlib.texit_counter_capture_lite()
print("RESULT", run(16000))
trace_cap.stop()
texit_cap.stop()
print("TRACE_START", trace_cap.start)
print("TRACE_STOP", trace_cap.stop_count)
print("TRACE_ABORT", trace_cap.abort)
print("TEXIT_COUNT", texit_cap.total)
""",
    "retconst_loop": """\
local bit = require("bit")
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
jit.opt.start("hotloop=1")
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
local function retconst(...)
  return 42
end
local function run(n)
  local result = 0
  for i = 1, n do
    result = bit.tobit(result + retconst(1, 2, 3, i))
  end
  return result
end
run(20); run(20); run(20)
local trace_cap = testlib.trace_counter_capture_lite()
local texit_cap = testlib.texit_counter_capture_lite()
print("RESULT", run(16000))
trace_cap.stop()
texit_cap.stop()
print("TRACE_START", trace_cap.start)
print("TRACE_STOP", trace_cap.stop_count)
print("TRACE_ABORT", trace_cap.abort)
print("TEXIT_COUNT", texit_cap.total)
""",
}

VARARG_HANDOFF_SCRIPTS = {
    "sum_loop": """\
local bit = require("bit")
local jit = require("jit")
jit.opt.start("hotloop=1")
local function sum(...)
  local total = 0
  for i = 1, select("#", ...) do
    total = total + select(i, ...)
  end
  return total
end
local result = 0
for i = 1, 2000 do
  result = bit.tobit(result + sum(1, 2, 3, ((i - 1) % 17) + 1))
end
print("RESULT", result)
""",
    "retlast_loop": """\
local bit = require("bit")
local jit = require("jit")
jit.opt.start("hotloop=1")
local function retlast(...)
  return select(select("#", ...), ...)
end
local result = 0
for i = 1, 2000 do
  result = bit.tobit(result + retlast(1, 2, 3, ((i - 1) % 17) + 1))
end
print("RESULT", result)
""",
    "retconst_loop": """\
local bit = require("bit")
local jit = require("jit")
jit.opt.start("hotloop=1")
local function retconst(...)
  return 42
end
local result = 0
for i = 1, 2000 do
  result = bit.tobit(result + retconst(1, 2, 3, i))
end
print("RESULT", result)
""",
}

BITOPS_FOCUSED_BENCH = """\
local bit = require("bit")
local bench = dofile("tests/s390x/perf/benchlib.lua")

local function mix(i)
  local x = bit.band(i, 0xff)
  x = bit.bxor(x, bit.lshift(i, 3))
  x = bit.bor(x, bit.rshift(i, 1))
  x = bit.bxor(x, bit.arshift(-i, 2))
  x = bit.bxor(x, bit.rol(i, 5))
  x = bit.bxor(x, bit.ror(i, 7))
  x = bit.bxor(x, bit.bswap(i))
  x = bit.bxor(x, bit.bnot(i))
  return x
end

local function run(chunks)
  local total = 0
  for _ = 1, chunks do
    for i = 1, 200 do
      total = bit.tobit(total + mix(i))
    end
  end
  return total
end

bench.run_suite({
  family = "bitops_truth_pack",
  cases = {
    {
      workload = "mix_bits",
      scale = "hot",
      iterations = 20,
      warmup_runs = 2,
      run = run,
      validate = function(result)
        bench.eq(result, 281636956, "mix_bits/hot")
      end,
    },
  },
})
"""

BITOPS_CHECK_SCRIPTS = {
    "mix_bits": """\
local bit = require("bit")
local function mix(i)
  local x = bit.band(i, 0xff)
  x = bit.bxor(x, bit.lshift(i, 3))
  x = bit.bor(x, bit.rshift(i, 1))
  x = bit.bxor(x, bit.arshift(-i, 2))
  x = bit.bxor(x, bit.rol(i, 5))
  x = bit.bxor(x, bit.ror(i, 7))
  x = bit.bxor(x, bit.bswap(i))
  x = bit.bxor(x, bit.bnot(i))
  return x
end
local function run(chunks)
  local total = 0
  for _ = 1, chunks do
    for i = 1, 200 do
      total = bit.tobit(total + mix(i))
    end
  end
  return total
end
print("MIX_BITS", run(1))
""",
}

BITOPS_TRACE_SCRIPTS = {
    "mix_bits": """\
local bit = require("bit")
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
jit.opt.start("hotloop=1")
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
local function mix(i)
  local x = bit.band(i, 0xff)
  x = bit.bxor(x, bit.lshift(i, 3))
  x = bit.bor(x, bit.rshift(i, 1))
  x = bit.bxor(x, bit.arshift(-i, 2))
  x = bit.bxor(x, bit.rol(i, 5))
  x = bit.bxor(x, bit.ror(i, 7))
  x = bit.bxor(x, bit.bswap(i))
  x = bit.bxor(x, bit.bnot(i))
  return x
end
local function run(chunks)
  local total = 0
  for _ = 1, chunks do
    for i = 1, 200 do
      total = bit.tobit(total + mix(i))
    end
  end
  return total
end
run(1); run(1); run(1)
local trace_cap = testlib.trace_counter_capture_lite()
local texit_cap = testlib.texit_counter_capture_lite()
print("RESULT", run(20))
trace_cap.stop()
texit_cap.stop()
print("TRACE_START", trace_cap.start)
print("TRACE_STOP", trace_cap.stop_count)
print("TRACE_ABORT", trace_cap.abort)
print("TEXIT_COUNT", texit_cap.total)
""",
}

LOGICAL_CHAIN_TAIL_ADD_BENCH = """\
local bit = require("bit")
local bench = dofile("tests/s390x/perf/benchlib.lua")

local scale_order = { "hot" }
local scales = {
  hot = 20,
}

local function chain(i)
  local x = bit.band(i, 0xff)
  x = bit.bxor(x, bit.lshift(i, 3))
  x = bit.bor(x, bit.rshift(i, 1))
  x = bit.bxor(x, bit.arshift(-i, 2))
  x = bit.bxor(x, bit.rol(i, 5))
  x = bit.bxor(x, bit.ror(i, 7))
  x = bit.bxor(x, bit.bswap(i))
  x = bit.bxor(x, bit.bnot(i))
  return x
end

local function chain_tail_add(chunks)
  local total = 0
  for _ = 1, chunks do
    for i = 1, 200 do
      total = bit.tobit(total + chain(i))
    end
  end
  return total
end

local cases = {}
for _, scale in ipairs(scale_order) do
  local chunks = scales[scale]
  local expected = chain_tail_add(chunks)
  cases[#cases + 1] = {
    workload = "chain_tail_add",
    scale = scale,
    iterations = chunks,
    run = chain_tail_add,
    validate = function(result)
      bench.eq(result, expected, "chain_tail_add/" .. scale)
    end,
  }
end

bench.run_suite({ family = "logical_chain_tail_add", cases = cases })
"""

LOGICAL_CHAIN_TAIL_ADD_TRACE_SCRIPT = """\
local bit = require("bit")
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
jit.opt.start("hotloop=1")
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
local function chain(i)
  local x = bit.band(i, 0xff)
  x = bit.bxor(x, bit.lshift(i, 3))
  x = bit.bor(x, bit.rshift(i, 1))
  x = bit.bxor(x, bit.arshift(-i, 2))
  x = bit.bxor(x, bit.rol(i, 5))
  x = bit.bxor(x, bit.ror(i, 7))
  x = bit.bxor(x, bit.bswap(i))
  x = bit.bxor(x, bit.bnot(i))
  return x
end
local function run(chunks)
  local total = 0
  for _ = 1, chunks do
    for i = 1, 200 do
      total = bit.tobit(total + chain(i))
    end
  end
  return total
end
run(1); run(1); run(1)
local trace_cap = testlib.trace_counter_capture_lite()
local texit_cap = testlib.texit_counter_capture_lite()
print("RESULT", run(20))
trace_cap.stop()
texit_cap.stop()
print("TRACE_START", trace_cap.start)
print("TRACE_STOP", trace_cap.stop_count)
print("TRACE_ABORT", trace_cap.abort)
print("TEXIT_COUNT", texit_cap.total)
"""

LOGICAL_CHAIN_TAIL_ADD_CHECK_SCRIPT = """\
local bit = require("bit")
local function chain(i)
  local x = bit.band(i, 0xff)
  x = bit.bxor(x, bit.lshift(i, 3))
  x = bit.bor(x, bit.rshift(i, 1))
  x = bit.bxor(x, bit.arshift(-i, 2))
  x = bit.bxor(x, bit.rol(i, 5))
  x = bit.bxor(x, bit.ror(i, 7))
  x = bit.bxor(x, bit.bswap(i))
  x = bit.bxor(x, bit.bnot(i))
  return x
end
local function run(chunks)
  local total = 0
  for _ = 1, chunks do
    for i = 1, 200 do
      total = bit.tobit(total + chain(i))
    end
  end
  return total
end
print("CHAIN_TAIL_ADD", run(20))
"""

LOGICAL_CHAIN_TAIL_STORE_BENCH = """\
local bit = require("bit")
local bench = dofile("tests/s390x/perf/benchlib.lua")

local scale_order = { "hot" }
local scales = {
  hot = 20,
}

local function chain(i)
  local x = bit.band(i, 0xff)
  x = bit.bxor(x, bit.lshift(i, 3))
  x = bit.bor(x, bit.rshift(i, 1))
  x = bit.bxor(x, bit.arshift(-i, 2))
  x = bit.bxor(x, bit.rol(i, 5))
  x = bit.bxor(x, bit.ror(i, 7))
  x = bit.bxor(x, bit.bswap(i))
  x = bit.bxor(x, bit.bnot(i))
  return x
end

local function chain_tail_store(chunks)
  local total = 0
  local sink = { 0 }
  for _ = 1, chunks do
    for i = 1, 200 do
      local x = chain(i)
      sink[1] = x
      if x == sink[1] then
        total = total + 1
      end
    end
  end
  return bit.tobit(total + sink[1])
end

local cases = {}
for _, scale in ipairs(scale_order) do
  local chunks = scales[scale]
  local expected = chain_tail_store(chunks)
  cases[#cases + 1] = {
    workload = "chain_tail_store",
    scale = scale,
    iterations = chunks,
    run = chain_tail_store,
    validate = function(result)
      bench.eq(result, expected, "chain_tail_store/" .. scale)
    end,
  }
end

bench.run_suite({ family = "logical_chain_tail_store", cases = cases })
"""

LOGICAL_CHAIN_TAIL_STORE_TRACE_SCRIPT = """\
local bit = require("bit")
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
jit.opt.start("hotloop=1")
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
local function chain(i)
  local x = bit.band(i, 0xff)
  x = bit.bxor(x, bit.lshift(i, 3))
  x = bit.bor(x, bit.rshift(i, 1))
  x = bit.bxor(x, bit.arshift(-i, 2))
  x = bit.bxor(x, bit.rol(i, 5))
  x = bit.bxor(x, bit.ror(i, 7))
  x = bit.bxor(x, bit.bswap(i))
  x = bit.bxor(x, bit.bnot(i))
  return x
end
local function run(chunks)
  local total = 0
  local sink = { 0 }
  for _ = 1, chunks do
    for i = 1, 200 do
      local x = chain(i)
      sink[1] = x
      if x == sink[1] then
        total = total + 1
      end
    end
  end
  return bit.tobit(total + sink[1])
end
run(1); run(1); run(1)
local trace_cap = testlib.trace_counter_capture_lite()
local texit_cap = testlib.texit_counter_capture_lite()
print("RESULT", run(20))
trace_cap.stop()
texit_cap.stop()
print("TRACE_START", trace_cap.start)
print("TRACE_STOP", trace_cap.stop_count)
print("TRACE_ABORT", trace_cap.abort)
print("TEXIT_COUNT", texit_cap.total)
"""

LOGICAL_CHAIN_TAIL_STORE_CHECK_SCRIPT = """\
local bit = require("bit")
local function chain(i)
  local x = bit.band(i, 0xff)
  x = bit.bxor(x, bit.lshift(i, 3))
  x = bit.bor(x, bit.rshift(i, 1))
  x = bit.bxor(x, bit.arshift(-i, 2))
  x = bit.bxor(x, bit.rol(i, 5))
  x = bit.bxor(x, bit.ror(i, 7))
  x = bit.bxor(x, bit.bswap(i))
  x = bit.bxor(x, bit.bnot(i))
  return x
end
local function run(chunks)
  local total = 0
  local sink = { 0 }
  for _ = 1, chunks do
    for i = 1, 200 do
      local x = chain(i)
      sink[1] = x
      if x == sink[1] then
        total = total + 1
      end
    end
  end
  return bit.tobit(total + sink[1])
end
print("CHAIN_TAIL_STORE", run(20))
"""

INT_ADD_PHI_ONLY_BENCH = """\
local bench = dofile("tests/s390x/perf/benchlib.lua")

local scale_order = { "small", "medium", "hot" }
local scales = {
  small = 1,
  medium = 5,
  hot = 20,
}

local function run(chunks)
  local total = 0
  for _ = 1, chunks do
    for i = 1, 200 do
      total = total + i + 3
    end
  end
  return total
end

local cases = {}
for _, scale in ipairs(scale_order) do
  local chunks = scales[scale]
  local expected = run(chunks)
  cases[#cases + 1] = {
    workload = "add_phi_only",
    scale = scale,
    iterations = chunks,
    run = run,
    validate = function(result)
      bench.eq(result, expected, "add_phi_only/" .. scale)
    end,
  }
end

bench.run_suite({ family = "int_add_phi_only", cases = cases })
"""

INT_ADD_PHI_ONLY_TRACE_SCRIPT = """\
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
jit.opt.start("hotloop=1")
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
local function run(chunks)
  local total = 0
  for _ = 1, chunks do
    for i = 1, 200 do
      total = total + i + 3
    end
  end
  return total
end
run(1); run(1); run(1)
local trace_cap = testlib.trace_counter_capture_lite()
local texit_cap = testlib.texit_counter_capture_lite()
print("RESULT", run(20))
trace_cap.stop()
texit_cap.stop()
print("TRACE_START", trace_cap.start)
print("TRACE_STOP", trace_cap.stop_count)
print("TRACE_ABORT", trace_cap.abort)
print("TEXIT_COUNT", texit_cap.total)
"""

INT_ADD_PHI_ONLY_CHECK_SCRIPT = """\
local function run(chunks)
  local total = 0
  for _ = 1, chunks do
    for i = 1, 200 do
      total = total + i + 3
    end
  end
  return total
end
print("ADD_PHI_ONLY", run(20))
"""

LOGIC_ADD_PHI_NOBOUNDARY_BENCH = """\
local bit = require("bit")
local bench = dofile("tests/s390x/perf/benchlib.lua")

local scale_order = { "small", "medium", "hot" }
local scales = {
  small = 1,
  medium = 5,
  hot = 20,
}

local function chain(i)
  local x = bit.band(i, 0xff)
  x = bit.bxor(x, bit.lshift(i, 3))
  x = bit.bor(x, bit.rshift(i, 1))
  x = bit.bxor(x, bit.arshift(-i, 2))
  x = bit.bxor(x, bit.rol(i, 5))
  x = bit.bxor(x, bit.ror(i, 7))
  x = bit.bxor(x, bit.bswap(i))
  x = bit.bxor(x, bit.bnot(i))
  return bit.band(x, 0x3ff)
end

local function run(chunks)
  local total = 0
  for _ = 1, chunks do
    for i = 1, 200 do
      total = total + chain(i)
    end
  end
  return total
end

local cases = {}
for _, scale in ipairs(scale_order) do
  local chunks = scales[scale]
  local expected = run(chunks)
  cases[#cases + 1] = {
    workload = "logic_add_phi_noboundary",
    scale = scale,
    iterations = chunks,
    run = run,
    validate = function(result)
      bench.eq(result, expected, "logic_add_phi_noboundary/" .. scale)
    end,
  }
end

bench.run_suite({ family = "logic_add_phi_noboundary", cases = cases })
"""

LOGIC_ADD_PHI_NOBOUNDARY_TRACE_SCRIPT = """\
local bit = require("bit")
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
jit.opt.start("hotloop=1")
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
local function chain(i)
  local x = bit.band(i, 0xff)
  x = bit.bxor(x, bit.lshift(i, 3))
  x = bit.bor(x, bit.rshift(i, 1))
  x = bit.bxor(x, bit.arshift(-i, 2))
  x = bit.bxor(x, bit.rol(i, 5))
  x = bit.bxor(x, bit.ror(i, 7))
  x = bit.bxor(x, bit.bswap(i))
  x = bit.bxor(x, bit.bnot(i))
  return bit.band(x, 0x3ff)
end
local function run(chunks)
  local total = 0
  for _ = 1, chunks do
    for i = 1, 200 do
      total = total + chain(i)
    end
  end
  return total
end
run(1); run(1); run(1)
local trace_cap = testlib.trace_counter_capture_lite()
local texit_cap = testlib.texit_counter_capture_lite()
print("RESULT", run(20))
trace_cap.stop()
texit_cap.stop()
print("TRACE_START", trace_cap.start)
print("TRACE_STOP", trace_cap.stop_count)
print("TRACE_ABORT", trace_cap.abort)
print("TEXIT_COUNT", texit_cap.total)
"""

MIXED_NOFFI_FOCUSED_BENCH = """\
local bit = require("bit")
local bench = dofile("tests/s390x/perf/benchlib.lua")

local numbers = { 1, 2, 3, 4, 5, 6, 7, 8 }

local function mixed_loop(n)
  local total = 0
  local map = { a = 1, b = 2, c = 3, d = 4 }
  for i = 1, n do
    total = total + bit.band(i * 17, 0x3ff)
    total = total + select(((i - 1) % 4) + 1, 1, 2, 3, 4)
    for _, value in ipairs(numbers) do
      total = total + value
    end
    for _, value in pairs(map) do
      total = total + value
    end
  end
  return total
end

bench.run_suite({
  family = "mixed_noffi_truth_pack",
  cases = {
    {
      workload = "mixed_loop",
      scale = "hot",
      iterations = 16000,
      warmup_runs = 2,
      run = mixed_loop,
      validate = function(result)
        bench.eq(result, mixed_loop(16000), "mixed_loop/hot")
      end,
    },
  },
})
"""

MIXED_NOFFI_CHECK_SCRIPT = """\
local bit = require("bit")
local numbers = { 1, 2, 3, 4, 5, 6, 7, 8 }
local function run(n)
  local total = 0
  local map = { a = 1, b = 2, c = 3, d = 4 }
  for i = 1, n do
    total = total + bit.band(i * 17, 0x3ff)
    total = total + select(((i - 1) % 4) + 1, 1, 2, 3, 4)
    for _, value in ipairs(numbers) do total = total + value end
    for _, value in pairs(map) do total = total + value end
  end
  return total
end
print("MIXED_LOOP", run(20))
"""

MIXED_NOFFI_TRACE_SCRIPT = """\
local bit = require("bit")
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
jit.opt.start("hotloop=1")
local numbers = { 1, 2, 3, 4, 5, 6, 7, 8 }
local function run(n)
  local total = 0
  local map = { a = 1, b = 2, c = 3, d = 4 }
  for i = 1, n do
    total = total + bit.band(i * 17, 0x3ff)
    total = total + select(((i - 1) % 4) + 1, 1, 2, 3, 4)
    for _, value in ipairs(numbers) do total = total + value end
    for _, value in pairs(map) do total = total + value end
  end
  return total
end
run(20); run(20); run(20)
local trace_cap = testlib.trace_counter_capture_lite()
local texit_cap = testlib.texit_counter_capture_lite()
print("RESULT", run(16000))
trace_cap.stop()
texit_cap.stop()
print("TRACE_START", trace_cap.start)
print("TRACE_STOP", trace_cap.stop_count)
print("TRACE_ABORT", trace_cap.abort)
print("TEXIT_COUNT", texit_cap.total)
"""

BE_HELPERS_FOCUSED_BENCH = """\
local bit = require("bit")
local bench = dofile("tests/s390x/perf/benchlib.lua")

local function number_helper_loop(n)
  local total = 0
  for i = 1, n do
    total = bit.tobit(total + i * 65537)
  end
  return bit.tobit(total)
end

local function be_pack_loop(n)
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

bench.run_suite({
  family = "be_helpers_truth_pack",
  cases = {
    {
      workload = "number_helper_loop",
      scale = "hot",
      iterations = 64000,
      warmup_runs = 2,
      run = number_helper_loop,
      validate = function(result)
        bench.eq(result, number_helper_loop(64000), "number_helper_loop/hot")
      end,
    },
    {
      workload = "be_pack_loop",
      scale = "hot",
      iterations = 64000,
      warmup_runs = 2,
      run = be_pack_loop,
      validate = function(result)
        bench.eq(result, be_pack_loop(64000), "be_pack_loop/hot")
      end,
    },
  },
})
"""

BE_HELPERS_CHECK_SCRIPTS = {
    "number_helper_loop": """\
local bit = require("bit")
local function run(n)
  local total = 0
  for i = 1, n do
    total = bit.tobit(total + i * 65537)
  end
  return bit.tobit(total)
end
print("NUMBER_HELPER_LOOP", run(20))
""",
    "be_pack_loop": """\
local bit = require("bit")
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
print("BE_PACK_LOOP", run(20))
""",
}

BE_HELPERS_TRACE_SCRIPTS = {
    "number_helper_loop": """\
local bit = require("bit")
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
jit.opt.start("hotloop=1")
local function run(n)
  local total = 0
  for i = 1, n do
    total = bit.tobit(total + i * 65537)
  end
  return bit.tobit(total)
end
run(20); run(20); run(20)
local trace_cap = testlib.trace_counter_capture_lite()
local texit_cap = testlib.texit_counter_capture_lite()
print("RESULT", run(64000))
trace_cap.stop()
texit_cap.stop()
print("TRACE_START", trace_cap.start)
print("TRACE_STOP", trace_cap.stop_count)
print("TRACE_ABORT", trace_cap.abort)
print("TEXIT_COUNT", texit_cap.total)
""",
    "be_pack_loop": """\
local bit = require("bit")
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
jit.opt.start("hotloop=1")
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
local trace_cap = testlib.trace_counter_capture_lite()
local texit_cap = testlib.texit_counter_capture_lite()
print("RESULT", run(64000))
trace_cap.stop()
texit_cap.stop()
print("TRACE_START", trace_cap.start)
print("TRACE_STOP", trace_cap.stop_count)
print("TRACE_ABORT", trace_cap.abort)
print("TEXIT_COUNT", texit_cap.total)
""",
}

BE_HELPERS_LOCALIZED_FOCUSED_BENCH = """\
local bit = require("bit")
local bench = dofile("tests/s390x/perf/benchlib.lua")

local function number_helper_loop_local_tobit(n)
  local total = 0
  local tobit = bit.tobit
  for i = 1, n do
    total = tobit(total + i * 65537)
  end
  return tobit(total)
end

local function be_pack_loop_local_ops_real(n)
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

bench.run_suite({
  family = "be_helpers_localized_truth_pack",
  cases = {
    {
      workload = "number_helper_loop_local_tobit",
      scale = "hot",
      iterations = 64000,
      warmup_runs = 2,
      run = number_helper_loop_local_tobit,
      validate = function(result)
        bench.eq(result, number_helper_loop_local_tobit(64000), "number_helper_loop_local_tobit/hot")
      end,
    },
    {
      workload = "be_pack_loop_local_ops_real",
      scale = "hot",
      iterations = 64000,
      warmup_runs = 2,
      run = be_pack_loop_local_ops_real,
      validate = function(result)
        bench.eq(result, be_pack_loop_local_ops_real(64000), "be_pack_loop_local_ops_real/hot")
      end,
    },
  },
})
"""

BE_HELPERS_LOCALIZED_CHECK_SCRIPTS = {
    "number_helper_loop_local_tobit": """\
local bit = require("bit")
local tobit = bit.tobit
local function run(n)
  local total = 0
  for i = 1, n do
    total = tobit(total + i * 65537)
  end
  return tobit(total)
end
print("NUMBER_HELPER_LOOP_LOCAL_TOBIT", run(20))
""",
    "be_pack_loop_local_ops_real": """\
local bit = require("bit")
local band = bit.band
local rshift = bit.rshift
local lshift = bit.lshift
local tobit = bit.tobit
local function run(n)
  local total = 0
  for i = 1, n do
    local b1 = band(rshift(i, 24), 0xff)
    local b2 = band(rshift(i, 16), 0xff)
    local b3 = band(rshift(i, 8), 0xff)
    local b4 = band(i, 0xff)
    total = tobit(total + lshift(b1, 24) + lshift(b2, 16) + lshift(b3, 8) + b4)
  end
  return tobit(total)
end
print("BE_PACK_LOOP_LOCAL_OPS_REAL", run(20))
""",
}

BE_HELPERS_LOCALIZED_TRACE_SCRIPTS = {
    "number_helper_loop_local_tobit": """\
local bit = require("bit")
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
jit.opt.start("hotloop=1")
local tobit = bit.tobit
local function run(n)
  local total = 0
  for i = 1, n do
    total = tobit(total + i * 65537)
  end
  return tobit(total)
end
run(20); run(20); run(20)
local trace_cap = testlib.trace_counter_capture_lite()
local texit_cap = testlib.texit_counter_capture_lite()
print("RESULT", run(64000))
trace_cap.stop()
texit_cap.stop()
print("TRACE_START", trace_cap.start)
print("TRACE_STOP", trace_cap.stop_count)
print("TRACE_ABORT", trace_cap.abort)
print("TEXIT_COUNT", texit_cap.total)
""",
    "be_pack_loop_local_ops_real": """\
local bit = require("bit")
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
jit.opt.start("hotloop=1")
local band = bit.band
local rshift = bit.rshift
local lshift = bit.lshift
local tobit = bit.tobit
local function run(n)
  local total = 0
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
local trace_cap = testlib.trace_counter_capture_lite()
local texit_cap = testlib.texit_counter_capture_lite()
print("RESULT", run(64000))
trace_cap.stop()
texit_cap.stop()
print("TRACE_START", trace_cap.start)
print("TRACE_STOP", trace_cap.stop_count)
print("TRACE_ABORT", trace_cap.abort)
print("TEXIT_COUNT", texit_cap.total)
""",
}

PROMOTION_CORE_STATIC_STOP_FOCUSED_BENCH = """\
local bit = require("bit")
local bench = dofile("tests/s390x/perf/benchlib.lua")

local function number_helper_literal_stop_real()
  local total = 0
  for i = 1, 64000 do
    total = bit.tobit(total + i * 65537)
  end
  return bit.tobit(total)
end

local function number_helper_literal_stop_real_local_tobit()
  local total = 0
  local tobit = bit.tobit
  for i = 1, 64000 do
    total = tobit(total + i * 65537)
  end
  return tobit(total)
end

local function be_pack_literal_stop_real()
  local total = 0
  for i = 1, 64000 do
    local b1 = bit.band(bit.rshift(i, 24), 0xff)
    local b2 = bit.band(bit.rshift(i, 16), 0xff)
    local b3 = bit.band(bit.rshift(i, 8), 0xff)
    local b4 = bit.band(i, 0xff)
    total = bit.tobit(total + bit.lshift(b1, 24) + bit.lshift(b2, 16) + bit.lshift(b3, 8) + b4)
  end
  return bit.tobit(total)
end

bench.run_suite({
  family = "promotion_core_static_stop_truth_pack",
  cases = {
    {
      workload = "number_helper_literal_stop_real",
      scale = "hot",
      iterations = 1,
      warmup_runs = 2,
      run = number_helper_literal_stop_real,
      validate = function(result)
        bench.eq(result, number_helper_literal_stop_real(), "number_helper_literal_stop_real/hot")
      end,
    },
    {
      workload = "number_helper_literal_stop_real_local_tobit",
      scale = "hot",
      iterations = 1,
      warmup_runs = 2,
      run = number_helper_literal_stop_real_local_tobit,
      validate = function(result)
        bench.eq(result, number_helper_literal_stop_real_local_tobit(), "number_helper_literal_stop_real_local_tobit/hot")
      end,
    },
    {
      workload = "be_pack_literal_stop_real",
      scale = "hot",
      iterations = 1,
      warmup_runs = 2,
      run = be_pack_literal_stop_real,
      validate = function(result)
        bench.eq(result, be_pack_literal_stop_real(), "be_pack_literal_stop_real/hot")
      end,
    },
  },
})
"""

PROMOTION_CORE_STATIC_STOP_CHECK_SCRIPTS = {
    "number_helper_literal_stop_real": """\
local bit = require("bit")
local function run()
  local total = 0
  for i = 1, 64000 do
    total = bit.tobit(total + i * 65537)
  end
  return bit.tobit(total)
end
print("NUMBER_HELPER_LITERAL_STOP_REAL", run())
""",
    "number_helper_literal_stop_real_local_tobit": """\
local bit = require("bit")
local function run()
  local total = 0
  local tobit = bit.tobit
  for i = 1, 64000 do
    total = tobit(total + i * 65537)
  end
  return tobit(total)
end
print("NUMBER_HELPER_LITERAL_STOP_REAL_LOCAL_TOBIT", run())
""",
    "be_pack_literal_stop_real": """\
local bit = require("bit")
local function run()
  local total = 0
  for i = 1, 64000 do
    local b1 = bit.band(bit.rshift(i, 24), 0xff)
    local b2 = bit.band(bit.rshift(i, 16), 0xff)
    local b3 = bit.band(bit.rshift(i, 8), 0xff)
    local b4 = bit.band(i, 0xff)
    total = bit.tobit(total + bit.lshift(b1, 24) + bit.lshift(b2, 16) + bit.lshift(b3, 8) + b4)
  end
  return bit.tobit(total)
end
print("BE_PACK_LITERAL_STOP_REAL", run())
""",
}

PROMOTION_CORE_STATIC_STOP_TRACE_SCRIPTS = {
    "number_helper_literal_stop_real": """\
local bit = require("bit")
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
jit.opt.start("hotloop=1")
local function run()
  local total = 0
  for i = 1, 64000 do
    total = bit.tobit(total + i * 65537)
  end
  return bit.tobit(total)
end
run(); run(); run()
local trace_cap = testlib.trace_counter_capture_lite()
local texit_cap = testlib.texit_counter_capture_lite()
print("RESULT", run())
trace_cap.stop()
texit_cap.stop()
print("TRACE_START", trace_cap.start)
print("TRACE_STOP", trace_cap.stop_count)
print("TRACE_ABORT", trace_cap.abort)
print("TEXIT_COUNT", texit_cap.total)
""",
    "number_helper_literal_stop_real_local_tobit": """\
local bit = require("bit")
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
jit.opt.start("hotloop=1")
local function run()
  local total = 0
  local tobit = bit.tobit
  for i = 1, 64000 do
    total = tobit(total + i * 65537)
  end
  return tobit(total)
end
run(); run(); run()
local trace_cap = testlib.trace_counter_capture_lite()
local texit_cap = testlib.texit_counter_capture_lite()
print("RESULT", run())
trace_cap.stop()
texit_cap.stop()
print("TRACE_START", trace_cap.start)
print("TRACE_STOP", trace_cap.stop_count)
print("TRACE_ABORT", trace_cap.abort)
print("TEXIT_COUNT", texit_cap.total)
""",
    "be_pack_literal_stop_real": """\
local bit = require("bit")
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
jit.opt.start("hotloop=1")
local function run()
  local total = 0
  for i = 1, 64000 do
    local b1 = bit.band(bit.rshift(i, 24), 0xff)
    local b2 = bit.band(bit.rshift(i, 16), 0xff)
    local b3 = bit.band(bit.rshift(i, 8), 0xff)
    local b4 = bit.band(i, 0xff)
    total = bit.tobit(total + bit.lshift(b1, 24) + bit.lshift(b2, 16) + bit.lshift(b3, 8) + b4)
  end
  return bit.tobit(total)
end
run(); run(); run()
local trace_cap = testlib.trace_counter_capture_lite()
local texit_cap = testlib.texit_counter_capture_lite()
print("RESULT", run())
trace_cap.stop()
texit_cap.stop()
print("TRACE_START", trace_cap.start)
print("TRACE_STOP", trace_cap.stop_count)
print("TRACE_ABORT", trace_cap.abort)
print("TEXIT_COUNT", texit_cap.total)
""",
}

FFI_CALLS_FOCUSED_BENCH = """\
local ffi = require("ffi")
local bench = dofile("tests/s390x/perf/benchlib.lua")
ffi.cdef[[ int abs(int x); ]]
local cabs = ffi.C.abs

local function direct_abs(n)
  local total = 0
  for i = 1, n do
    total = total + ffi.C.abs((i % 17) - 8)
  end
  return total
end

local function stored_abs(n)
  local total = 0
  for i = 1, n do
    total = total + cabs((i % 17) - 8)
  end
  return total
end

bench.run_suite({
  family = "ffi_calls_truth_pack",
  cases = {
    {
      workload = "direct_abs",
      scale = "hot",
      iterations = 80000,
      warmup_runs = 2,
      run = direct_abs,
      validate = function(result)
        bench.eq(result, direct_abs(80000), "direct_abs/hot")
      end,
    },
    {
      workload = "stored_abs",
      scale = "hot",
      iterations = 80000,
      warmup_runs = 2,
      run = stored_abs,
      validate = function(result)
        bench.eq(result, stored_abs(80000), "stored_abs/hot")
      end,
    },
  },
})
"""

FFI_CALLS_CHECK_SCRIPTS = {
    "direct_abs": """\
local ffi = require("ffi")
ffi.cdef[[ int abs(int x); ]]
local function run(n)
  local total = 0
  for i = 1, n do total = total + ffi.C.abs((i % 17) - 8) end
  return total
end
print("DIRECT_ABS", run(20))
""",
    "stored_abs": """\
local ffi = require("ffi")
ffi.cdef[[ int abs(int x); ]]
local cabs = ffi.C.abs
local function run(n)
  local total = 0
  for i = 1, n do total = total + cabs((i % 17) - 8) end
  return total
end
print("STORED_ABS", run(20))
""",
}

FFI_CALLS_TRACE_SCRIPTS = {
    "direct_abs": """\
local ffi = require("ffi")
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
jit.opt.start("hotloop=1")
ffi.cdef[[ int abs(int x); ]]
local function run(n)
  local total = 0
  for i = 1, n do total = total + ffi.C.abs((i % 17) - 8) end
  return total
end
run(20); run(20); run(20)
local trace_cap = testlib.trace_counter_capture_lite()
local texit_cap = testlib.texit_counter_capture_lite()
print("RESULT", run(80000))
trace_cap.stop()
texit_cap.stop()
print("TRACE_START", trace_cap.start)
print("TRACE_STOP", trace_cap.stop_count)
print("TRACE_ABORT", trace_cap.abort)
print("TEXIT_COUNT", texit_cap.total)
""",
    "stored_abs": """\
local ffi = require("ffi")
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
jit.opt.start("hotloop=1")
ffi.cdef[[ int abs(int x); ]]
local cabs = ffi.C.abs
local function run(n)
  local total = 0
  for i = 1, n do total = total + cabs((i % 17) - 8) end
  return total
end
run(20); run(20); run(20)
local trace_cap = testlib.trace_counter_capture_lite()
local texit_cap = testlib.texit_counter_capture_lite()
print("RESULT", run(80000))
trace_cap.stop()
texit_cap.stop()
print("TRACE_START", trace_cap.start)
print("TRACE_STOP", trace_cap.stop_count)
print("TRACE_ABORT", trace_cap.abort)
print("TEXIT_COUNT", texit_cap.total)
""",
}

ROUTE_AROUND_REDUCERS_FOCUSED_BENCH = """\
local bit = require("bit")
local bench = dofile("tests/s390x/perf/benchlib.lua")

local function be_pack_literal_stop(chunks)
  local total = 0
  for _ = 1, chunks do
    for i = 1, 400 do
      local b1 = bit.band(bit.rshift(i, 24), 0xff)
      local b2 = bit.band(bit.rshift(i, 16), 0xff)
      local b3 = bit.band(bit.rshift(i, 8), 0xff)
      local b4 = bit.band(i, 0xff)
      total = bit.tobit(total + bit.lshift(b1, 24) + bit.lshift(b2, 16) + bit.lshift(b3, 8) + b4)
    end
  end
  return bit.tobit(total)
end

local function be_pack_literal_stop_local_ops(chunks)
  local total = 0
  local band = bit.band
  local rshift = bit.rshift
  local lshift = bit.lshift
  local tobit = bit.tobit
  for _ = 1, chunks do
    for i = 1, 400 do
      local b1 = band(rshift(i, 24), 0xff)
      local b2 = band(rshift(i, 16), 0xff)
      local b3 = band(rshift(i, 8), 0xff)
      local b4 = band(i, 0xff)
      total = tobit(total + lshift(b1, 24) + lshift(b2, 16) + lshift(b3, 8) + b4)
    end
  end
  return tobit(total)
end

local function be_pack_loop_local_ops(chunks)
  local total = 0
  local band = bit.band
  local rshift = bit.rshift
  local lshift = bit.lshift
  local tobit = bit.tobit
  for _ = 1, chunks do
    for i = 1, 400 do
      local b1 = band(rshift(i, 24), 0xff)
      local b2 = band(rshift(i, 16), 0xff)
      local b3 = band(rshift(i, 8), 0xff)
      local b4 = band(i, 0xff)
      total = tobit(total + lshift(b1, 24) + lshift(b2, 16) + lshift(b3, 8) + b4)
    end
  end
  return tobit(total)
end

bench.run_suite({
  family = "route_around_reducers_truth_pack",
  cases = {
    {
      workload = "be_pack_literal_stop",
      scale = "hot",
      iterations = 400,
      warmup_runs = 2,
      run = be_pack_literal_stop,
      validate = function(result)
        bench.eq(result, be_pack_literal_stop(400), "be_pack_literal_stop/hot")
      end,
    },
    {
      workload = "be_pack_literal_stop_local_ops",
      scale = "hot",
      iterations = 400,
      warmup_runs = 2,
      run = be_pack_literal_stop_local_ops,
      validate = function(result)
        bench.eq(result, be_pack_literal_stop_local_ops(400), "be_pack_literal_stop_local_ops/hot")
      end,
    },
    {
      workload = "be_pack_loop_local_ops",
      scale = "hot",
      iterations = 400,
      warmup_runs = 2,
      run = be_pack_loop_local_ops,
      validate = function(result)
        bench.eq(result, be_pack_loop_local_ops(400), "be_pack_loop_local_ops/hot")
      end,
    },
  },
})
"""

ROUTE_AROUND_REDUCERS_CHECK_SCRIPTS = {
    "be_pack_literal_stop": """\
local bit = require("bit")
local function run(chunks)
  local total = 0
  for _ = 1, chunks do
    for i = 1, 400 do
      local b1 = bit.band(bit.rshift(i, 24), 0xff)
      local b2 = bit.band(bit.rshift(i, 16), 0xff)
      local b3 = bit.band(bit.rshift(i, 8), 0xff)
      local b4 = bit.band(i, 0xff)
      total = bit.tobit(total + bit.lshift(b1, 24) + bit.lshift(b2, 16) + bit.lshift(b3, 8) + b4)
    end
  end
  return bit.tobit(total)
end
print("BE_PACK_LITERAL_STOP", run(4))
""",
    "be_pack_literal_stop_local_ops": """\
local bit = require("bit")
local function run(chunks)
  local total = 0
  local band = bit.band
  local rshift = bit.rshift
  local lshift = bit.lshift
  local tobit = bit.tobit
  for _ = 1, chunks do
    for i = 1, 400 do
      local b1 = band(rshift(i, 24), 0xff)
      local b2 = band(rshift(i, 16), 0xff)
      local b3 = band(rshift(i, 8), 0xff)
      local b4 = band(i, 0xff)
      total = tobit(total + lshift(b1, 24) + lshift(b2, 16) + lshift(b3, 8) + b4)
    end
  end
  return tobit(total)
end
print("BE_PACK_LITERAL_STOP_LOCAL_OPS", run(4))
""",
    "be_pack_loop_local_ops": """\
local bit = require("bit")
local function run(chunks)
  local total = 0
  local band = bit.band
  local rshift = bit.rshift
  local lshift = bit.lshift
  local tobit = bit.tobit
  for _ = 1, chunks do
    for i = 1, 400 do
      local b1 = band(rshift(i, 24), 0xff)
      local b2 = band(rshift(i, 16), 0xff)
      local b3 = band(rshift(i, 8), 0xff)
      local b4 = band(i, 0xff)
      total = tobit(total + lshift(b1, 24) + lshift(b2, 16) + lshift(b3, 8) + b4)
    end
  end
  return tobit(total)
end
print("BE_PACK_LOOP_LOCAL_OPS", run(4))
""",
}

ROUTE_AROUND_REDUCERS_TRACE_SCRIPTS = {
    "be_pack_literal_stop": """\
local bit = require("bit")
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
jit.opt.start("hotloop=1", "hotexit=1")
local function run(chunks)
  local total = 0
  for _ = 1, chunks do
    for i = 1, 400 do
      local b1 = bit.band(bit.rshift(i, 24), 0xff)
      local b2 = bit.band(bit.rshift(i, 16), 0xff)
      local b3 = bit.band(bit.rshift(i, 8), 0xff)
      local b4 = bit.band(i, 0xff)
      total = bit.tobit(total + bit.lshift(b1, 24) + bit.lshift(b2, 16) + bit.lshift(b3, 8) + b4)
    end
  end
  return bit.tobit(total)
end
run(1); run(1); run(1)
local trace_cap = testlib.trace_counter_capture_lite()
local texit_cap = testlib.texit_counter_capture_lite()
print("RESULT", run(400))
trace_cap.stop()
texit_cap.stop()
print("TRACE_START", trace_cap.start)
print("TRACE_STOP", trace_cap.stop_count)
print("TRACE_ABORT", trace_cap.abort)
print("TEXIT_COUNT", texit_cap.total)
""",
    "be_pack_literal_stop_local_ops": """\
local bit = require("bit")
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
jit.opt.start("hotloop=1", "hotexit=1")
local function run(chunks)
  local total = 0
  local band = bit.band
  local rshift = bit.rshift
  local lshift = bit.lshift
  local tobit = bit.tobit
  for _ = 1, chunks do
    for i = 1, 400 do
      local b1 = band(rshift(i, 24), 0xff)
      local b2 = band(rshift(i, 16), 0xff)
      local b3 = band(rshift(i, 8), 0xff)
      local b4 = band(i, 0xff)
      total = tobit(total + lshift(b1, 24) + lshift(b2, 16) + lshift(b3, 8) + b4)
    end
  end
  return tobit(total)
end
run(1); run(1); run(1)
local trace_cap = testlib.trace_counter_capture_lite()
local texit_cap = testlib.texit_counter_capture_lite()
print("RESULT", run(400))
trace_cap.stop()
texit_cap.stop()
print("TRACE_START", trace_cap.start)
print("TRACE_STOP", trace_cap.stop_count)
print("TRACE_ABORT", trace_cap.abort)
print("TEXIT_COUNT", texit_cap.total)
""",
    "be_pack_loop_local_ops": """\
local bit = require("bit")
local jit = require("jit")
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
jit.opt.start("hotloop=1", "hotexit=1")
local function run(chunks)
  local total = 0
  local band = bit.band
  local rshift = bit.rshift
  local lshift = bit.lshift
  local tobit = bit.tobit
  for _ = 1, chunks do
    for i = 1, 400 do
      local b1 = band(rshift(i, 24), 0xff)
      local b2 = band(rshift(i, 16), 0xff)
      local b3 = band(rshift(i, 8), 0xff)
      local b4 = band(i, 0xff)
      total = tobit(total + lshift(b1, 24) + lshift(b2, 16) + lshift(b3, 8) + b4)
    end
  end
  return tobit(total)
end
run(1); run(1); run(1)
local trace_cap = testlib.trace_counter_capture_lite()
local texit_cap = testlib.texit_counter_capture_lite()
print("RESULT", run(400))
trace_cap.stop()
texit_cap.stop()
print("TRACE_START", trace_cap.start)
print("TRACE_STOP", trace_cap.stop_count)
print("TRACE_ABORT", trace_cap.abort)
print("TEXIT_COUNT", texit_cap.total)
""",
}

LOGIC_ADD_PHI_NOBOUNDARY_CHECK_SCRIPT = """\
local bit = require("bit")
local function chain(i)
  local x = bit.band(i, 0xff)
  x = bit.bxor(x, bit.lshift(i, 3))
  x = bit.bor(x, bit.rshift(i, 1))
  x = bit.bxor(x, bit.arshift(-i, 2))
  x = bit.bxor(x, bit.rol(i, 5))
  x = bit.bxor(x, bit.ror(i, 7))
  x = bit.bxor(x, bit.bswap(i))
  x = bit.bxor(x, bit.bnot(i))
  return bit.band(x, 0x3ff)
end
local function run(chunks)
  local total = 0
  for _ = 1, chunks do
    for i = 1, 200 do
      total = total + chain(i)
    end
  end
  return total
end
print("LOGIC_ADD_PHI_NOBOUNDARY", run(20))
"""

FAMILY_CONFIGS = {
    "vararg_paths": {
        "bench_file": "tests/s390x/perf/vararg_paths.lua",
        "focus_label": "vararg throughput",
        "selection_reason": (
            "near-parity retained regression screen after the exact root-FORL "
            "blacklist wins; restamp only if a fresh attribution names a new "
            "vararg subsystem or a retained regression appears"
        ),
        "focused_bench_script": VARARG_FOCUSED_BENCH,
        "check_scripts": VARARG_CHECK_SCRIPTS,
        "trace_scripts": VARARG_TRACE_SCRIPTS,
        "handoff_scripts": VARARG_HANDOFF_SCRIPTS,
        "hot_cases": ("sum_loop/hot", "retlast_loop/hot", "retconst_loop/hot"),
        "work_items": {
            "sum_loop": 16000,
            "retlast_loop": 16000,
            "retconst_loop": 16000,
        },
    },
    "bitops_mix": {
        "bench_file": "tests/s390x/perf/bitops_mix.lua",
        "focus_label": "bitops throughput",
        "selection_reason": (
            "backend-heavy compiled-body control; use after vararg to tell "
            "exit-driven red from pure ALU/bitop throughput red"
        ),
        "focused_bench_script": BITOPS_FOCUSED_BENCH,
        "check_scripts": BITOPS_CHECK_SCRIPTS,
        "trace_scripts": BITOPS_TRACE_SCRIPTS,
        "hot_cases": ("mix_bits/hot",),
        "work_items": {
            "mix_bits": 4000,
        },
    },
    "logical_chain_tail_add": {
        "bench_file": "tests/s390x/perf/logical_chain_tail_add.lua",
        "focus_label": "logical chain add-tail throughput",
        "selection_reason": (
            "seam isolator for a logical producer chain whose first non-bitop "
            "consumer is exactly one ADD"
        ),
        "focused_bench_script": LOGICAL_CHAIN_TAIL_ADD_BENCH,
        "check_scripts": {
            "chain_tail_add": LOGICAL_CHAIN_TAIL_ADD_CHECK_SCRIPT,
        },
        "trace_scripts": {
            "chain_tail_add": LOGICAL_CHAIN_TAIL_ADD_TRACE_SCRIPT,
        },
        "bnorm_probe_scripts": {
            "chain_tail_add": LOGICAL_CHAIN_TAIL_ADD_TRACE_SCRIPT,
        },
        "hot_cases": ("chain_tail_add/hot",),
        "work_items": {
            "chain_tail_add": 4000,
        },
    },
    "logical_chain_tail_store": {
        "bench_file": "tests/s390x/perf/logical_chain_tail_store.lua",
        "focus_label": "logical chain store-tail throughput",
        "selection_reason": (
            "seam isolator for the same logical chain when the first non-bitop "
            "consumer is a store/compare path"
        ),
        "focused_bench_script": LOGICAL_CHAIN_TAIL_STORE_BENCH,
        "check_scripts": {
            "chain_tail_store": LOGICAL_CHAIN_TAIL_STORE_CHECK_SCRIPT,
        },
        "trace_scripts": {
            "chain_tail_store": LOGICAL_CHAIN_TAIL_STORE_TRACE_SCRIPT,
        },
        "bnorm_probe_scripts": {
            "chain_tail_store": LOGICAL_CHAIN_TAIL_STORE_TRACE_SCRIPT,
        },
        "hot_cases": ("chain_tail_store/hot",),
        "work_items": {
            "chain_tail_store": 4000,
        },
    },
    "int_add_phi_only": {
        "bench_file": "tests/s390x/perf/int_add_phi_only.lua",
        "focus_label": "integer add/phi throughput",
        "selection_reason": (
            "fallback control family for plain integer result carry through "
            "ADD and PHI without bitops in the hot body"
        ),
        "focused_bench_script": INT_ADD_PHI_ONLY_BENCH,
        "check_scripts": {
            "add_phi_only": INT_ADD_PHI_ONLY_CHECK_SCRIPT,
        },
        "trace_scripts": {
            "add_phi_only": INT_ADD_PHI_ONLY_TRACE_SCRIPT,
        },
        "hot_cases": ("add_phi_only/hot",),
        "work_items": {
            "add_phi_only": 4000,
        },
    },
    "logic_add_phi_noboundary": {
        "bench_file": "tests/s390x/perf/logic_add_phi_noboundary.lua",
        "focus_label": "logic-to-add/phi throughput",
        "selection_reason": (
            "fallback family for a bounded logical chain whose first non-bitop "
            "consumer is carried ADD and PHI, without store or value-compare "
            "tails in the hot body"
        ),
        "focused_bench_script": LOGIC_ADD_PHI_NOBOUNDARY_BENCH,
        "check_scripts": {
            "logic_add_phi_noboundary": LOGIC_ADD_PHI_NOBOUNDARY_CHECK_SCRIPT,
        },
        "trace_scripts": {
            "logic_add_phi_noboundary": LOGIC_ADD_PHI_NOBOUNDARY_TRACE_SCRIPT,
        },
        "hot_cases": ("logic_add_phi_noboundary/hot",),
        "work_items": {
            "logic_add_phi_noboundary": 4000,
        },
    },
    "mixed_noffi": {
        "bench_file": "tests/s390x/perf/mixed_noffi.lua",
        "focus_label": "mixed non-ffi throughput",
        "selection_reason": (
            "active small residual after the retained exact root BC_ITERL / "
            "BC_ITERN / stitched BC_FORL blacklist win; re-attribute the "
            "official hot row before opening another subsystem"
        ),
        "focused_bench_script": MIXED_NOFFI_FOCUSED_BENCH,
        "check_scripts": {
            "mixed_loop": MIXED_NOFFI_CHECK_SCRIPT,
        },
        "trace_scripts": {
            "mixed_loop": MIXED_NOFFI_TRACE_SCRIPT,
        },
        "hot_cases": ("mixed_loop/hot",),
        "work_items": {
            "mixed_loop": 16000,
        },
    },
    "be_helpers": {
        "bench_file": "tests/s390x/perf/be_helpers.lua",
        "focus_label": "backend helper throughput",
        "selection_reason": (
            "same-seam helper-heavy family already proven positive under the "
            "filtered UGET/looproot mechanism; use helper-backed truth packs "
            "to close the in-scope slice"
        ),
        "focused_bench_script": BE_HELPERS_FOCUSED_BENCH,
        "check_scripts": BE_HELPERS_CHECK_SCRIPTS,
        "trace_scripts": BE_HELPERS_TRACE_SCRIPTS,
        "hot_cases": ("number_helper_loop/hot", "be_pack_loop/hot"),
        "work_items": {
            "number_helper_loop": 64000,
            "be_pack_loop": 64000,
        },
    },
    "be_helpers_localized": {
        "bench_file": "tests/s390x/perf/be_helpers_localized.lua",
        "focus_label": "localized helper throughput",
        "selection_reason": (
            "quantify whether localizing helper bindings on the real dynamic-stop "
            "workload shape materially narrows the remaining promotion-core gap"
        ),
        "focused_bench_script": BE_HELPERS_LOCALIZED_FOCUSED_BENCH,
        "check_scripts": BE_HELPERS_LOCALIZED_CHECK_SCRIPTS,
        "trace_scripts": BE_HELPERS_LOCALIZED_TRACE_SCRIPTS,
        "hot_cases": (
            "number_helper_loop_local_tobit/hot",
            "be_pack_loop_local_ops_real/hot",
        ),
        "work_items": {
            "number_helper_loop_local_tobit": 64000,
            "be_pack_loop_local_ops_real": 64000,
        },
    },
    "promotion_core_static_stop": {
        "bench_file": "tests/s390x/perf/promotion_core_static_stop.lua",
        "focus_label": "promotion-core static-stop throughput",
        "selection_reason": (
            "quantify how much of the remaining promotion-core gap is the "
            "dynamic-stop replay floor by measuring real-shape static-stop siblings"
        ),
        "focused_bench_script": PROMOTION_CORE_STATIC_STOP_FOCUSED_BENCH,
        "check_scripts": PROMOTION_CORE_STATIC_STOP_CHECK_SCRIPTS,
        "trace_scripts": PROMOTION_CORE_STATIC_STOP_TRACE_SCRIPTS,
        "hot_cases": (
            "number_helper_literal_stop_real/hot",
            "number_helper_literal_stop_real_local_tobit/hot",
            "be_pack_literal_stop_real/hot",
        ),
        "work_items": {
            "number_helper_literal_stop_real": 1,
            "number_helper_literal_stop_real_local_tobit": 1,
            "be_pack_literal_stop_real": 1,
        },
    },
    "ffi_calls": {
        "bench_file": "tests/s390x/perf/ffi_calls.lua",
        "focus_label": "ffi call throughput",
        "selection_reason": (
            "same-seam call-heavy family already proven positive under the "
            "filtered UGET/looproot mechanism; use helper-backed truth packs "
            "to close the in-scope slice"
        ),
        "focused_bench_script": FFI_CALLS_FOCUSED_BENCH,
        "check_scripts": FFI_CALLS_CHECK_SCRIPTS,
        "trace_scripts": FFI_CALLS_TRACE_SCRIPTS,
        "hot_cases": ("direct_abs/hot", "stored_abs/hot"),
        "work_items": {
            "direct_abs": 80000,
            "stored_abs": 80000,
        },
    },
    "route_around_reducers": {
        "bench_file": "tests/s390x/perf/route_around_reducers.lua",
        "focus_label": "reduced route-around throughput",
        "selection_reason": (
            "quantify the structurally improved reduced siblings to see which "
            "route-around shapes actually cross into useful JIT throughput"
        ),
        "focused_bench_script": ROUTE_AROUND_REDUCERS_FOCUSED_BENCH,
        "check_scripts": ROUTE_AROUND_REDUCERS_CHECK_SCRIPTS,
        "trace_scripts": ROUTE_AROUND_REDUCERS_TRACE_SCRIPTS,
        "hot_cases": (
            "be_pack_literal_stop/hot",
            "be_pack_literal_stop_local_ops/hot",
            "be_pack_loop_local_ops/hot",
        ),
        "work_items": {
            "be_pack_literal_stop": 160000,
            "be_pack_literal_stop_local_ops": 160000,
            "be_pack_loop_local_ops": 160000,
        },
    },
}


def write_text(path: pathlib.Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def write_json(path: pathlib.Path, payload: object) -> None:
    write_text(path, json.dumps(payload, indent=2, sort_keys=True) + "\n")


def write_jsonl(path: pathlib.Path, records: list[dict[str, Any]]) -> None:
    write_text(path, "".join(json.dumps(record, sort_keys=True) + "\n" for record in records))


def parse_key_value_lines(text: str) -> dict[str, int | str]:
    result: dict[str, int | str] = {}
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        if "=" in line and " " not in line:
            key, value = line.split("=", 1)
            key = key.strip()
            value = value.strip()
            if not key:
                continue
            if re.fullmatch(r"-?\d+", value):
                result[key] = int(value)
            else:
                result[key] = value
            continue
        parts = line.split(None, 1)
        if len(parts) != 2:
            continue
        key, value = parts
        value = value.strip()
        if re.fullmatch(r"-?\d+", value):
            result[key] = int(value)
        else:
            result[key] = value
    return result


def parse_histogram(hist: object) -> dict[str, int]:
    if not isinstance(hist, str) or not hist:
        return {}
    buckets: dict[str, int] = {}
    for part in hist.split(","):
        if "=" not in part:
            continue
        key, value = part.split("=", 1)
        key = key.strip()
        value = value.strip()
        if not key or not re.fullmatch(r"-?\d+", value):
            continue
        buckets[key] = int(value)
    return buckets


def parse_inline_kv(line: str) -> dict[str, int | str]:
    result: dict[str, int | str] = {}
    for key, value in re.findall(r"([A-Za-z0-9_]+)=([^\s]+)", line):
        if re.fullmatch(r"-?\d+", value):
            result[key] = int(value)
        else:
            result[key] = value
    return result


def ir_op_name(op: object) -> str:
    if isinstance(op, int):
        return IR_OP_NAMES.get(op, f"OP_{op}")
    return str(op)


def parse_bnorm_counts(stderr_text: str) -> dict[str, Any]:
    total = 0
    by_site: dict[str, int] = {}
    by_consumer: dict[str, int] = {}
    by_pair: dict[str, dict[str, int]] = {}
    records: list[dict[str, int | str]] = []
    for raw_line in stderr_text.splitlines():
        line = raw_line.strip()
        if not line.startswith("S390X_BNORM "):
            continue
        record = parse_inline_kv(line)
        if not record:
            continue
        site = str(record.get("site", "unknown"))
        consumer = ir_op_name(record.get("first_nonbitop_use_op"))
        record["first_nonbitop_use_name"] = consumer
        total += 1
        by_site[site] = by_site.get(site, 0) + 1
        by_consumer[consumer] = by_consumer.get(consumer, 0) + 1
        pair = by_pair.setdefault(site, {})
        pair[consumer] = pair.get(consumer, 0) + 1
        records.append(record)
    return {
        "total": total,
        "by_site": by_site,
        "by_consumer": by_consumer,
        "by_pair": by_pair,
        "records": records,
    }


def to_float_counter(value: object) -> float | None:
    if not isinstance(value, str):
        return None
    cleaned = value.replace(",", "").strip()
    if not cleaned or cleaned in {"<not", "<not supported>"}:
        return None
    try:
        return float(cleaned)
    except ValueError:
        return None


def shell_env_prefix(extra_env: dict[str, str] | None = None) -> str:
    if not extra_env:
        return ""
    return "env " + " ".join(f"{key}={shlex.quote(value)}" for key, value in extra_env.items()) + " "


def remote_env_prefix(
    *,
    bench_file: str,
    jsonl_path: str | None,
    samples: int,
    warmup: int,
    extra_env: dict[str, str] | None = None,
) -> str:
    env = []
    if extra_env:
        env.extend(f"{key}={shlex.quote(value)}" for key, value in extra_env.items())
    if jsonl_path:
        env.append(f"S390X_PERF_OUTPUT_JSONL={shlex.quote(jsonl_path)}")
    env.extend(
        [
            f"S390X_PERF_WARMUP={warmup}",
            f"S390X_PERF_SAMPLES={samples}",
            f"S390X_PERF_BENCH_FILE={shlex.quote(bench_file)}",
        ]
    )
    return "env " + " ".join(env)


def perf_index(records: list[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    indexed: dict[str, dict[str, Any]] = {}
    for record in records:
        indexed[f"{record['workload']}/{record['scale']}"] = record
    return indexed


def prepare_truth_scripts(host: str, remote_tmp: str, config: dict[str, Any]) -> None:
    lines = ["set -euo pipefail"]
    lines.extend(
        [
            f'cat >"{remote_tmp}/focused_bench.lua" <<\'EOF\'',
            config["focused_bench_script"].rstrip(),
            "EOF",
        ]
    )
    for name, content in config["check_scripts"].items():
        lines.extend([f'cat >"{remote_tmp}/{name}.lua" <<\'EOF\'', content.rstrip(), "EOF"])
    for name, content in config["trace_scripts"].items():
        lines.extend([f'cat >"{remote_tmp}/{name}_trace.lua" <<\'EOF\'', content.rstrip(), "EOF"])
        lines.extend([f'cat >"{remote_tmp}/{name}_perf.lua" <<\'EOF\'', content.rstrip(), "EOF"])
    for name, content in config.get("handoff_scripts", {}).items():
        lines.extend([f'cat >"{remote_tmp}/{name}_handoff.lua" <<\'EOF\'', content.rstrip(), "EOF"])
    proc = restamp.run_ssh_script(host, "\n".join(lines) + "\n")
    restamp.require_ok(proc, f"{host} throughput truth-pack script setup")


def run_family_bench(
    *,
    host: str,
    repo: str,
    remote_tmp: str,
    raw_dir: pathlib.Path,
    bench_file: str,
    mode_label: str,
    pin_core: int | None,
    samples: int,
    warmup: int,
    joff: bool,
    extra_env: dict[str, str] | None,
) -> list[dict[str, Any]]:
    json_name = f"{mode_label}.jsonl"
    remote_json = f"{remote_tmp}/{json_name}"
    luajit_args = ["-joff", bench_file] if joff else [bench_file]
    script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
rm -f {shlex.quote(remote_json)}
{remote_env_prefix(bench_file=bench_file, jsonl_path=remote_json, samples=samples, warmup=warmup, extra_env=extra_env)} {f"taskset -c {pin_core} " if pin_core is not None else ""}./src/luajit {' '.join(shlex.quote(arg) for arg in luajit_args)}
"""
    restamp.run_remote_command(
        host,
        script,
        stdout_path=raw_dir / f"{mode_label}.stdout.log",
        stderr_path=raw_dir / f"{mode_label}.stderr.log",
        label=f"{host} {bench_file} {mode_label}",
    )
    json_text = restamp.fetch_remote_file(host, remote_json)
    local_json = raw_dir.parent.parent / json_name
    write_text(local_json, json_text)
    return restamp.parse_jsonl_records(local_json)


def run_focused_bench(
    *,
    host: str,
    repo: str,
    remote_tmp: str,
    raw_dir: pathlib.Path,
    bench_file: str,
    pin_core: int | None,
    samples: int,
    warmup: int,
    joff: bool,
    extra_env: dict[str, str] | None,
) -> list[dict[str, Any]]:
    mode = "focused-jit-on" if not joff else "focused-joff"
    remote_json = f"{remote_tmp}/{mode}.jsonl"
    luajit_cmd = f"{f'taskset -c {pin_core} ' if pin_core is not None else ''}./src/luajit {'-joff ' if joff else ''}{shlex.quote(f'{remote_tmp}/focused_bench.lua')}"
    script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
rm -f {shlex.quote(remote_json)}
{remote_env_prefix(bench_file=bench_file, jsonl_path=remote_json, samples=samples, warmup=warmup, extra_env=extra_env)} {luajit_cmd}
"""
    restamp.run_remote_command(
        host,
        script,
        stdout_path=raw_dir / f"{mode}.stdout.log",
        stderr_path=raw_dir / f"{mode}.stderr.log",
        label=f"{host} {mode} broader throughput medians",
    )
    json_text = restamp.fetch_remote_file(host, remote_json)
    local_json = raw_dir.parent.parent / f"{mode}.jsonl"
    write_text(local_json, json_text)
    return restamp.parse_jsonl_records(local_json)


def hot_records_from_official(config: dict[str, Any], records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    hot_cases = set(config["hot_cases"])
    return [
        record for record in records
        if f"{record['workload']}/{record['scale']}" in hot_cases
    ]


def run_check(
    host: str,
    repo: str,
    remote_tmp: str,
    raw_dir: pathlib.Path,
    name: str,
    extra_env: dict[str, str] | None,
) -> str:
    script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
set +e
{shell_env_prefix(extra_env)}timeout {PROBE_TIMEOUT_SECS} ./src/luajit {shlex.quote(f"{remote_tmp}/{name}.lua")}
rc=$?
set -e
printf 'REMOTE_RC=%s\\n' "$rc"
"""
    proc = restamp.run_remote_command(
        host,
        script,
        stdout_path=raw_dir / f"{name}.stdout.log",
        stderr_path=raw_dir / f"{name}.stderr.log",
        label=f"{host} broader throughput check {name}",
    )
    return proc.stdout.strip()


def run_trace_count(
    host: str,
    repo: str,
    remote_tmp: str,
    raw_dir: pathlib.Path,
    name: str,
    extra_env: dict[str, str] | None,
) -> dict[str, int | str]:
    script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
set +e
{shell_env_prefix(extra_env)}timeout {PROBE_TIMEOUT_SECS} ./src/luajit {shlex.quote(f"{remote_tmp}/{name}_trace.lua")}
rc=$?
set -e
printf 'REMOTE_RC=%s\\n' "$rc"
"""
    proc = restamp.run_remote_command(
        host,
        script,
        stdout_path=raw_dir / f"{name}.stdout.log",
        stderr_path=raw_dir / f"{name}.stderr.log",
        label=f"{host} broader throughput trace count {name}",
    )
    return parse_key_value_lines(proc.stdout)


def run_bnorm_probe(
    host: str,
    repo: str,
    remote_tmp: str,
    raw_dir: pathlib.Path,
    name: str,
    extra_env: dict[str, str] | None,
) -> dict[str, object]:
    probe_env = dict(extra_env or {})
    probe_env["LUAJIT_S390X_BNORM_LOG"] = "1"
    script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
set +e
{shell_env_prefix(probe_env)}timeout {PROBE_TIMEOUT_SECS} ./src/luajit {shlex.quote(f"{remote_tmp}/{name}_trace.lua")}
rc=$?
set -e
printf 'REMOTE_RC=%s\\n' "$rc"
"""
    proc = restamp.run_remote_command(
        host,
        script,
        stdout_path=raw_dir / f"{name}.stdout.log",
        stderr_path=raw_dir / f"{name}.stderr.log",
        label=f"{host} broader throughput bnorm probe {name}",
    )
    stderr_text = (raw_dir / f"{name}.stderr.log").read_text(encoding="utf-8")
    counts = parse_bnorm_counts(stderr_text)
    remote_rc_match = re.search(r"REMOTE_RC=(\d+)", proc.stdout)
    counts["remote_rc"] = int(remote_rc_match.group(1)) if remote_rc_match else -1
    return counts


def run_perf_stat(
    host: str,
    repo: str,
    remote_tmp: str,
    raw_dir: pathlib.Path,
    name: str,
    pin_core: int | None,
    extra_env: dict[str, str] | None,
) -> dict[str, object]:
    taskset = f"taskset -c {pin_core} " if pin_core is not None else ""
    script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
if ! command -v perf >/dev/null 2>&1; then
  echo "PERF_STATUS unavailable:perf-not-found"
  exit 0
fi
set +e
perf stat -x, -e cycles,instructions,branches,branch-misses -- {shell_env_prefix(extra_env)}timeout {PROBE_TIMEOUT_SECS} {taskset}./src/luajit {shlex.quote(f"{remote_tmp}/{name}_perf.lua")}
rc=$?
set -e
printf 'PERF_RC=%s\\n' "$rc"
"""
    proc = restamp.run_ssh_script(host, script)
    write_text(raw_dir / f"{name}.stdout.log", proc.stdout)
    write_text(raw_dir / f"{name}.stderr.log", proc.stderr)
    if proc.returncode != 0:
        return {"status": "unavailable", "reason": f"exit-{proc.returncode}"}
    if "PERF_STATUS unavailable:" in proc.stdout:
        return {"status": "unavailable", "reason": proc.stdout.strip().split(":", 1)[1]}
    if "PERF_RC=124" in proc.stdout:
        return {"status": "timeout", "reason": "timeout-20s"}
    counters: dict[str, str] = {}
    for raw_line in proc.stderr.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        parts = [part.strip() for part in line.split(",")]
        if len(parts) < 3:
            continue
        value, _, event = parts[:3]
        counters[event] = value
    return {"status": "ok", "counters": counters}


def parse_handoff_counts(text: str) -> dict[str, int]:
    return {
        "trace_loop_count": len(re.findall(r"^\[TRACE\s+\d+.* loop\]$", text, re.MULTILINE)),
        "trace_handoff_count": len(re.findall(r"^\[TRACE\s+\d+.* -> \d+\]$", text, re.MULTILINE)),
        "lua_intrace_return_count": len(re.findall(r"site=lua_intrace_return", text)),
        "lua_lower_frame_retf_count": len(re.findall(r"site=lua_lower_frame_retf", text)),
        "lua_lleave_count": len(re.findall(r"site=lua_root_lower_frame_lleave", text)),
    }


def run_handoff_probe(
    host: str,
    repo: str,
    remote_tmp: str,
    raw_dir: pathlib.Path,
    name: str,
    extra_env: dict[str, str] | None,
) -> dict[str, object]:
    probe_env = dict(extra_env or {})
    probe_env["LUAJIT_S390X_RECRET_LOG"] = "1"
    script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
set +e
{shell_env_prefix(probe_env)}timeout {PROBE_TIMEOUT_SECS} ./src/luajit -e 'package.path="./src/?.lua;./src/?/init.lua;"..package.path' -jv {shlex.quote(f"{remote_tmp}/{name}_handoff.lua")}
rc=$?
set -e
printf 'REMOTE_RC=%s\\n' "$rc"
"""
    proc = restamp.run_remote_command(
        host,
        script,
        stdout_path=raw_dir / f"{name}.stdout.log",
        stderr_path=raw_dir / f"{name}.stderr.log",
        label=f"{host} broader throughput handoff probe {name}",
    )
    counts = parse_handoff_counts(proc.stdout)
    remote_rc_match = re.search(r"REMOTE_RC=(\d+)", proc.stdout)
    counts["remote_rc"] = int(remote_rc_match.group(1)) if remote_rc_match else -1
    return counts


def focused_runtime_metrics(
    config: dict[str, Any],
    focused_jit_on: dict[str, dict[str, Any]],
    focused_joff: dict[str, dict[str, Any]],
    trace_counts: dict[str, dict[str, int | str]],
) -> dict[str, dict[str, object]]:
    metrics: dict[str, dict[str, object]] = {}
    for hot_key in config["hot_cases"]:
        workload = hot_key.split("/", 1)[0]
        jit_record = focused_jit_on[hot_key]
        joff_record = focused_joff[hot_key]
        jit_median = float(jit_record["median_runtime_sec"])
        joff_median = float(joff_record["median_runtime_sec"])
        texits = int(trace_counts[workload].get("TEXIT_COUNT", 0))
        trace_starts = int(trace_counts[workload].get("TRACE_START", 0))
        trace_aborts = int(trace_counts[workload].get("TRACE_ABORT", 0))
        work_items = int(config["work_items"][workload])
        remote_rc = int(trace_counts[workload].get("REMOTE_RC", 0)) if isinstance(trace_counts[workload].get("REMOTE_RC"), int) else None
        gap_sec = jit_median - joff_median
        gap_ratio = (jit_median / joff_median) if joff_median else None
        texits_per_work_item = texits / work_items if work_items else None
        trace_abort_rate = (trace_aborts / trace_starts) if trace_starts else None
        if remote_rc == 124:
            classification = "probe-timeout"
        elif texits_per_work_item is not None and texits_per_work_item >= 0.05:
            classification = "exit-dominated"
        elif texits == 0 and gap_ratio is not None and gap_ratio > 1.0:
            classification = "compiled-body-dominated"
        else:
            classification = "mixed"
        metrics[workload] = {
            "jit_median_sec": jit_median,
            "joff_median_sec": joff_median,
            "jit_gap_sec": gap_sec,
            "jit_gap_ratio": gap_ratio,
            "texits": texits,
            "texits_per_work_item": texits_per_work_item,
            "trace_abort_rate": trace_abort_rate,
            "remote_rc": remote_rc,
            "classification": classification,
        }
    return metrics


def derive_perf_metrics(
    config: dict[str, Any],
    trace_counts: dict[str, dict[str, int | str]],
    perf_stats: dict[str, dict[str, object]],
) -> dict[str, dict[str, object]]:
    metrics: dict[str, dict[str, object]] = {}
    for hot_key in config["hot_cases"]:
        workload = hot_key.split("/", 1)[0]
        info = perf_stats[workload]
        counters = info.get("counters", {}) if info.get("status") == "ok" else {}
        cycles = to_float_counter(counters.get("cycles")) if isinstance(counters, dict) else None
        instructions = to_float_counter(counters.get("instructions")) if isinstance(counters, dict) else None
        branches = to_float_counter(counters.get("branches")) if isinstance(counters, dict) else None
        branch_misses = to_float_counter(counters.get("branch-misses")) if isinstance(counters, dict) else None
        texits = int(trace_counts[workload].get("TEXIT_COUNT", 0))
        remote_rc = int(trace_counts[workload].get("REMOTE_RC", 0)) if isinstance(trace_counts[workload].get("REMOTE_RC"), int) else None
        cpi = (cycles / instructions) if cycles is not None and instructions not in (None, 0.0) else None
        branch_miss_rate = (branch_misses / branches) if branch_misses is not None and branches not in (None, 0.0) else None
        cycles_per_texit = (cycles / texits) if cycles is not None and texits else None
        metrics[workload] = {
            "cycles": cycles,
            "instructions": instructions,
            "branches": branches,
            "branch_misses": branch_misses,
            "cpi": cpi,
            "branch_miss_rate": branch_miss_rate,
            "cycles_per_texit": cycles_per_texit,
            "remote_rc": remote_rc,
        }
    return metrics


def render_summary(
    *,
    family: str,
    candidate: str,
    candidate_env: dict[str, str],
    config: dict[str, Any],
    output_dir: pathlib.Path,
    host: str,
    host_info: dict[str, str],
    repo: str,
    commit: str,
    jit_status: str,
    check_results: dict[str, str],
    jit_on_records: list[dict[str, Any]],
    joff_records: list[dict[str, Any]],
    focused_records: list[dict[str, Any]],
    focused_joff_records: list[dict[str, Any]],
    focused_status: str,
    trace_counts: dict[str, dict[str, int | str]],
    handoff_counts: dict[str, dict[str, object]],
    bnorm_counts: dict[str, dict[str, object]],
    perf_stats: dict[str, dict[str, object]],
    runtime_metrics: dict[str, dict[str, object]],
    perf_metrics: dict[str, dict[str, object]],
    pin_core: int | None,
    samples: int,
    warmup: int,
) -> str:
    jit_on_index = perf_index(jit_on_records)
    joff_index = perf_index(joff_records)
    focused_on_index = perf_index(focused_records)
    focused_off_index = perf_index(focused_joff_records)
    candidate_scope = CANDIDATE_SCOPE.get(candidate, {})
    scope_summary = family_scope_summary(candidate, config)
    workload_scope = scope_summary["workload_statuses"]
    lines = [
        f"# {family} Truth Pack",
        "",
        f"- Timestamp: `{dt.datetime.now().astimezone().strftime('%Y-%m-%d %H:%M:%S %Z')}`",
        f"- Host label: `{host}`",
        f"- Hostname: `{host_info.get('HOSTNAME_FQDN', host_info.get('HOSTNAME_SHORT', host))}`",
        f"- Machine type: `{host_info.get('MACHINE_TYPE', 'unknown')}` (`{host_info.get('GENERATION', 'unknown')}`)",
        f"- Model: `{host_info.get('MODEL', 'unknown')}`",
        f"- Repo: `{repo}`",
        f"- Commit: `{commit}`",
        f"- Candidate: `{candidate}`",
        f"- Candidate env: `{candidate_env if candidate_env else {}}`",
        f"- Benchmark: `{config['bench_file']}`",
        f"- Pinned core: `{pin_core if pin_core is not None else 'unbound'}`",
        f"- Samples: `{samples}`",
        f"- Warmup runs: `{warmup}`",
        "",
        "## Why This Family",
        "",
        f"- {config['selection_reason']}",
        "",
    ]
    if candidate_scope:
        lines.extend(
            [
                "## Candidate Scope",
                "",
                f"- {candidate_scope['summary']}",
                f"- family scope status: `{scope_summary['family_status']}`",
                f"- promotion action: `{scope_summary['promotion_action']}`",
            ]
        )
        for workload in scope_summary["hot_workloads"]:
            status = workload_scope.get(workload)
            if status:
                lines.append(f"- `{workload}` scope: `{status}`")
        lines.append("")
    lines.extend([
        "## Build And Smoke",
        "",
        f"- `jit.status()`: `{jit_status}`",
    ])
    for workload, output in check_results.items():
        lines.append(f"- `{workload}` check: `{output}`")
    lines.extend(["", "## Benchmark Medians", ""])
    for record in jit_on_records:
        key = f"{record['workload']}/{record['scale']}"
        off = joff_index.get(key)
        delta = None
        ratio = None
        if off is not None:
            delta = float(record["median_runtime_sec"]) - float(off["median_runtime_sec"])
            off_value = float(off["median_runtime_sec"])
            ratio = (float(record["median_runtime_sec"]) / off_value) if off_value else None
        lines.append(
            f"- `{key}`: JIT-on `{float(record['median_runtime_sec']):.6f}s`, "
            f"`-joff` `{float(off['median_runtime_sec']):.6f}s`" if off is not None else
            f"- `{key}`: JIT-on `{float(record['median_runtime_sec']):.6f}s`"
        )
        if off is not None:
            lines[-1] += f", gap `{delta:+.6f}s`, ratio `{ratio:.2f}x`"
    lines.extend(["", "## Focused Runtime Read", ""])
    if focused_status != "ok":
        lines.append(
            f"- focused reduced bench unavailable: `{focused_status}`; using official hot-row medians for this section"
        )
    for hot_key in config["hot_cases"]:
        workload = hot_key.split("/", 1)[0]
        runtime = runtime_metrics[workload]
        trace_info = trace_counts[workload]
        lines.append(f"- `{workload}`")
        lines.append(f"  - hot median: JIT-on `{runtime['jit_median_sec']:.6f}s`, `-joff` `{runtime['joff_median_sec']:.6f}s`, gap `{runtime['jit_gap_sec']:+.6f}s`, ratio `{runtime['jit_gap_ratio']:.2f}x`")
        lines.append(f"  - `REMOTE_RC {trace_info.get('REMOTE_RC', 'n/a')}`, `TRACE_START {int(trace_info.get('TRACE_START', 0))}`, `TRACE_STOP {int(trace_info.get('TRACE_STOP', 0))}`, `TRACE_ABORT {int(trace_info.get('TRACE_ABORT', 0))}`, `TEXIT_COUNT {int(trace_info.get('TEXIT_COUNT', 0))}`")
        lines.append(f"  - classification: `{runtime['classification']}`")
    lines.extend(["", "## perf stat", ""])
    for hot_key in config["hot_cases"]:
        workload = hot_key.split("/", 1)[0]
        metrics = perf_metrics[workload]
        stats = perf_stats[workload]
        if stats.get("status") != "ok":
            lines.append(f"- `{workload}`: `{stats.get('reason', 'unavailable')}`")
            continue
        lines.append(
            f"- `{workload}`: cycles `{metrics['cycles']}`, instructions `{metrics['instructions']}`, "
            f"branches `{metrics['branches']}`, branch-misses `{metrics['branch_misses']}`, "
            f"CPI `{metrics['cpi']:.4f}`" if metrics["cpi"] is not None else
            f"- `{workload}`: counters captured"
        )
    if handoff_counts:
        lines.extend(["", "## Reduced Handoff Probes", ""])
        for workload, counts in handoff_counts.items():
            lines.append(
                f"- `{workload}`: `REMOTE_RC {counts.get('remote_rc', 'n/a')}`, "
                f"`trace_loop_count {counts.get('trace_loop_count', 0)}`, "
                f"`trace_handoff_count {counts.get('trace_handoff_count', 0)}`, "
                f"`lua_intrace_return_count {counts.get('lua_intrace_return_count', 0)}`, "
                f"`lua_lower_frame_retf_count {counts.get('lua_lower_frame_retf_count', 0)}`, "
                f"`lua_lleave_count {counts.get('lua_lleave_count', 0)}`"
            )
    if bnorm_counts:
        lines.extend(["", "## asm_bnorm32 Sites", ""])
        for workload, info in bnorm_counts.items():
            lines.append(f"- `{workload}`: total `{info.get('total', 0)}`, `REMOTE_RC {info.get('remote_rc', 'n/a')}`")
            by_site = info.get("by_site", {})
            if isinstance(by_site, dict) and by_site:
                lines.append("  - by producer site:")
                for site, count in sorted(by_site.items(), key=lambda item: (-int(item[1]), item[0])):
                    lines.append(f"    - `{site}`: `{count}`")
            by_consumer = info.get("by_consumer", {})
            if isinstance(by_consumer, dict) and by_consumer:
                lines.append("  - by first non-bitop consumer:")
                for consumer, count in sorted(by_consumer.items(), key=lambda item: (-int(item[1]), item[0])):
                    lines.append(f"    - `{consumer}`: `{count}`")
            by_pair = info.get("by_pair", {})
            if isinstance(by_pair, dict) and by_pair:
                lines.append("  - by producer / first non-bitop consumer:")
                for site, consumers in sorted(by_pair.items(), key=lambda item: item[0]):
                    if not isinstance(consumers, dict):
                        continue
                    pair_parts = ", ".join(
                        f"`{consumer}`={count}"
                        for consumer, count in sorted(consumers.items(), key=lambda item: (-int(item[1]), item[0]))
                    )
                    lines.append(f"    - `{site}`: {pair_parts}")
    lines.extend(["", "## Artifacts", ""])
    lines.extend(
        [
            f"- JIT-on JSONL: `{output_dir / 'jit-on.jsonl'}`",
            f"- `-joff` JSONL: `{output_dir / 'joff.jsonl'}`",
            f"- Focused JIT-on JSONL: `{output_dir / 'focused-jit-on.jsonl'}`",
            f"- Focused `-joff` JSONL: `{output_dir / 'focused-joff.jsonl'}`",
            f"- Raw logs: `{output_dir / 'raw'}`",
        ]
    )
    if handoff_counts:
        lines.append(f"- Reduced handoff logs: `{output_dir / 'raw' / 'handoff'}`")
    if bnorm_counts:
        lines.append(f"- `asm_bnorm32` logs: `{output_dir / 'raw' / 'bnorm'}`")
    return "\n".join(lines) + "\n"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--family", choices=tuple(FAMILY_CONFIGS.keys()), required=True)
    parser.add_argument("--host", choices=restamp.HOST_LABELS, required=True)
    parser.add_argument("--candidate", choices=tuple(CANDIDATE_ENVS.keys()), default="retained_baseline")
    parser.add_argument("--repo", help="Override authoritative remote repo path.")
    parser.add_argument("--output-root", type=pathlib.Path, default=DEFAULT_OUTPUT_ROOT)
    parser.add_argument("--pin-core", type=int, default=restamp.DEFAULT_PIN_CORE)
    parser.add_argument("--samples", type=int, default=restamp.DEFAULT_SAMPLES)
    parser.add_argument("--warmup", type=int, default=restamp.DEFAULT_WARMUP)
    parser.add_argument(
        "--env",
        action="append",
        default=[],
        metavar="KEY=VALUE",
        help="Extra environment variable to set for remote benchmark and probe processes.",
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
    family = args.family
    candidate = args.candidate
    candidate_env = dict(CANDIDATE_ENVS[candidate])
    candidate_env.update(parse_env_overrides(args.env))
    config = FAMILY_CONFIGS[family]
    scope_summary = family_scope_summary(candidate, config)
    host = args.host
    repo = args.repo or restamp.AUTHORITATIVE_REPOS[host]
    output_dir = (
        args.output_root
        / f"{dt.datetime.now().astimezone().strftime('%Y%m%d')}-{host}-{family}-{candidate}-truth-pack"
    )
    raw_dir = output_dir / "raw"
    trace_dir = raw_dir / "trace-counts"
    perf_dir = raw_dir / "perf-stat"
    check_dir = raw_dir / "checks"
    handoff_dir = raw_dir / "handoff"
    bnorm_dir = raw_dir / "bnorm"
    output_dir.mkdir(parents=True, exist_ok=True)
    trace_dir.mkdir(parents=True, exist_ok=True)
    perf_dir.mkdir(parents=True, exist_ok=True)
    check_dir.mkdir(parents=True, exist_ok=True)
    handoff_dir.mkdir(parents=True, exist_ok=True)
    bnorm_dir.mkdir(parents=True, exist_ok=True)

    commit = restamp.current_commit()
    host_info = restamp.collect_host_info(host)
    remote_tmp = ""
    try:
        restamp.sync_tracked_files(host, repo)
        restamp.build_remote_repo(host, repo, raw_dir)
        jit_status = restamp.run_jit_status(host, repo, raw_dir)
        remote_tmp = restamp.prepare_remote_scripts(host)
        prepare_truth_scripts(host, remote_tmp, config)
        jit_on_records = run_family_bench(
            host=host,
            repo=repo,
            remote_tmp=remote_tmp,
            raw_dir=raw_dir,
            bench_file=config["bench_file"],
            mode_label="jit-on",
            pin_core=args.pin_core,
            samples=args.samples,
            warmup=args.warmup,
            joff=False,
            extra_env=candidate_env,
        )
        joff_records = run_family_bench(
            host=host,
            repo=repo,
            remote_tmp=remote_tmp,
            raw_dir=raw_dir,
            bench_file=config["bench_file"],
            mode_label="joff",
            pin_core=args.pin_core,
            samples=args.samples,
            warmup=args.warmup,
            joff=True,
            extra_env=candidate_env,
        )
        focused_status = "ok"
        try:
            focused_records = run_focused_bench(
                host=host,
                repo=repo,
                remote_tmp=remote_tmp,
                raw_dir=raw_dir,
                bench_file=config["bench_file"],
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
                bench_file=config["bench_file"],
                pin_core=args.pin_core,
                samples=args.samples,
                warmup=args.warmup,
                joff=True,
                extra_env=candidate_env,
            )
        except restamp.RestampError as exc:
            focused_status = str(exc)
            focused_records = hot_records_from_official(config, jit_on_records)
            focused_joff_records = hot_records_from_official(config, joff_records)
            write_jsonl(output_dir / "focused-jit-on.jsonl", focused_records)
            write_jsonl(output_dir / "focused-joff.jsonl", focused_joff_records)
        check_results = {
            name: run_check(host, repo, remote_tmp, check_dir, name, candidate_env)
            for name in config["check_scripts"].keys()
        }
        trace_counts = {
            name: run_trace_count(host, repo, remote_tmp, trace_dir, name, candidate_env)
            for name in config["trace_scripts"].keys()
        }
        handoff_counts = {
            name: run_handoff_probe(host, repo, remote_tmp, handoff_dir, name, candidate_env)
            for name in config.get("handoff_scripts", {}).keys()
        }
        bnorm_counts = {
            name: run_bnorm_probe(host, repo, remote_tmp, bnorm_dir, name, candidate_env)
            for name in config.get("bnorm_probe_scripts", {}).keys()
        }
        perf_stats = {
            name: run_perf_stat(host, repo, remote_tmp, perf_dir, name, args.pin_core, candidate_env)
            for name in config["trace_scripts"].keys()
        }
        runtime_metrics = focused_runtime_metrics(
            config,
            perf_index(focused_records),
            perf_index(focused_joff_records),
            trace_counts,
        )
        perf_metrics = derive_perf_metrics(config, trace_counts, perf_stats)
        metadata = {
            "family": family,
            "candidate": candidate,
            "candidate_env": candidate_env,
            "family_scope_status": scope_summary["family_status"],
            "promotion_action": scope_summary["promotion_action"],
            "hot_workload_scope": scope_summary["workload_statuses"],
            "focus_label": config["focus_label"],
            "selection_reason": config["selection_reason"],
            "host": host,
            "host_info": host_info,
            "repo": repo,
            "commit": commit,
            "benchmark_file": config["bench_file"],
            "pin_core": args.pin_core,
            "samples": args.samples,
            "warmup": args.warmup,
            "focused_status": focused_status,
            "timestamp": dt.datetime.now().astimezone().isoformat(),
        }
        write_json(output_dir / "metadata.json", metadata)
        write_json(output_dir / "trace-counts.json", trace_counts)
        write_json(output_dir / "handoff-counts.json", handoff_counts)
        write_json(output_dir / "bnorm-counts.json", bnorm_counts)
        write_json(output_dir / "perf-stat.json", perf_stats)
        write_json(output_dir / "runtime-metrics.json", runtime_metrics)
        write_json(output_dir / "perf-metrics.json", perf_metrics)
        summary = render_summary(
            family=family,
            candidate=candidate,
            candidate_env=candidate_env,
            config=config,
            output_dir=output_dir,
            host=host,
            host_info=host_info,
            repo=repo,
            commit=commit,
            jit_status=jit_status,
            check_results=check_results,
            jit_on_records=jit_on_records,
            joff_records=joff_records,
            focused_records=focused_records,
            focused_joff_records=focused_joff_records,
            focused_status=focused_status,
            trace_counts=trace_counts,
            handoff_counts=handoff_counts,
            bnorm_counts=bnorm_counts,
            perf_stats=perf_stats,
            runtime_metrics=runtime_metrics,
            perf_metrics=perf_metrics,
            pin_core=args.pin_core,
            samples=args.samples,
            warmup=args.warmup,
        )
        write_text(output_dir / "summary.md", summary)
        return 0
    except restamp.RestampError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    finally:
        if remote_tmp:
            restamp.cleanup_remote_scripts(host, remote_tmp)


if __name__ == "__main__":
    raise SystemExit(main())
