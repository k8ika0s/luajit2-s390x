#!/usr/bin/env python3
"""Build focused acceleration truth packs for near-parity s390x rows."""

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

import probe_retained_jitter as jitter
import restamp_iterator_perf as restamp


ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT_ROOT = ROOT / "artifacts" / "s390x" / "truth-packs"
PROBE_TIMEOUT_SECS = 30
HASH_STAMP_PATHS = list(
    dict.fromkeys(
        restamp.AUTHORITATIVE_HASH_PATHS
        + [
            "tools/s390x/probe_retained_jitter.py",
            "tools/s390x/build_acceleration_truth_pack.py",
            "src/lj_asm_s390x.h",
            "tests/s390x/perf/ffi_cdata.lua",
            "tests/s390x/perf/ffi_fixed_call_pressure.lua",
            "tests/s390x/perf/iterator_table.lua",
            "tests/s390x/perf/be_helpers.lua",
            "tests/s390x/perf/be_helpers_localized.lua",
            "tests/s390x/jit_be/mulov_overflow_guard.lua",
            "tests/s390x/jit_loops/pairs_loop.lua",
        ]
    )
)


LUA_COMMON = """\
local testlib = dofile("tests/s390x/helpers/testlib.lua")
testlib.enable_repo_jit_modules()
local jit = require("jit")
jit.opt.start("hotloop=1", "hotexit=2")

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

local function ir_op_name(ot)
  local vmdef = require("jit.vmdef")
  local idx = math.floor(ot / 256) * 6
  return (string.sub(vmdef.irnames, idx + 1, idx + 6):gsub("%s+$", ""))
end

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

local function emit_traceir(limit)
  local util = require("jit.util")
  for tr = 1, limit do
    local info = util.traceinfo(tr)
    if info then
      for ins = 0, (tonumber(info.nins) or 0) - 1 do
        local ok, mode, ot, op1, op2, prev = pcall(util.traceir, tr, ins)
        if ok and mode then
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
end

local function run_with_counters(label, iterations, run, validate)
  run(20); run(20); run(20)
  local trace_cap = testlib.trace_counter_capture()
  local texit_cap = testlib.texit_counter_capture()
  local result = run(iterations)
  trace_cap.stop()
  texit_cap.stop()
  validate(result)
  print("RESULT_LABEL", label)
  print("RESULT", result)
  print("TRACE_START", trace_cap.start)
  print("TRACE_STOP", trace_cap.stop_count)
  print("TRACE_ABORT", trace_cap.abort)
  print("TRACE_TOTAL", trace_cap.total)
  print("TEXIT_COUNT", texit_cap.total)
  emit_hist("TRACE_HIST", trace_cap.hist)
  emit_hist("TEXIT_HIST", texit_cap.hist)
  emit_traceinfo(64)
  emit_traceir(64)
end

local function reference_result(run, iterations)
  local enabled = select(1, jit.status())
  jit.off(run, true)
  local result = run(iterations)
  if enabled then
    jit.on(run, true)
  end
  jit.flush()
  return result
end
"""


FOCUSED_SCRIPTS = {
    "ffi_cdata_mixed_width": LUA_COMMON
    + """\
local ffi = require("ffi")
ffi.cdef[[
typedef struct { unsigned short a; unsigned int b; unsigned char c; } packed_u_t;
]]
local function run(n)
  local slot = ffi.new("packed_u_t[1]")
  local total = 0
  for i = 1, n do
    slot[0].a = i % 65535
    slot[0].b = (i % 4096) * 17
    slot[0].c = i % 251
    total = total + slot[0].a + slot[0].b + slot[0].c
  end
  return total
end
local expected = reference_result(run, 32000)
run_with_counters("ffi_cdata_mixed_width", 32000, run, function(result)
  testlib.eq(result, expected, "ffi_cdata_mixed_width")
end)
""",
    "ffi_fixed_gpr_pressure": LUA_COMMON
    + """\
local ffi = require("ffi")
local libpath = arg[1] or "tests/s390x/ffi_abi/build/liboracle.so"
ffi.cdef[[
uint64_t echo_u64(uint64_t value);
uint64_t sum7_u64(uint64_t a, uint64_t b, uint64_t c, uint64_t d,
                  uint64_t e, uint64_t f, uint64_t g);
]]
local lib = ffi.load(libpath)
local u64 = ffi.typeof("uint64_t")
local function run(n)
  local total = u64(0)
  for i = 1, n do
    local a = lib.echo_u64(u64(i))
    local b = lib.echo_u64(u64(i + 1))
    local c = lib.echo_u64(u64(i + 2))
    local d = lib.echo_u64(u64(i + 3))
    total = total + lib.sum7_u64(a, b, c, d, a, b, c)
  end
  return tonumber(total)
end
local expected = reference_result(run, 20000)
run_with_counters("ffi_fixed_gpr_pressure", 20000, run, function(result)
  testlib.eq(result, expected, "ffi_fixed_gpr_pressure")
end)
""",
    "iterator_pairs_loop_chain": LUA_COMMON
    + """\
local tab = {}
for i = 1, 100 do
  tab["a" .. i] = i
end
local function run(_)
  local total = 0
  for key in pairs(tab) do
    total = total + tab[key]
  end
  return total
end
run_with_counters("iterator_pairs_loop_chain", 1, run, function(result)
  testlib.eq(result, 5050, "iterator_pairs_loop_chain")
end)
""",
    "be_number_helper": LUA_COMMON
    + """\
local bit = require("bit")
local function run(n)
  local total = 0
  for i = 1, n do
    total = bit.tobit(total + i * 65537)
  end
  return bit.tobit(total)
end
local expected = reference_result(run, 64000)
run_with_counters("be_number_helper", 64000, run, function(result)
  testlib.eq(result, expected, "be_number_helper")
end)
""",
    "be_number_helper_local_tobit": LUA_COMMON
    + """\
local bit = require("bit")
local function run(n)
  local total = 0
  local tobit = bit.tobit
  for i = 1, n do
    total = tobit(total + i * 65537)
  end
  return tobit(total)
end
local expected = reference_result(run, 64000)
run_with_counters("be_number_helper_local_tobit", 64000, run, function(result)
  testlib.eq(result, expected, "be_number_helper_local_tobit")
end)
""",
    "low32_bitops_mix": LUA_COMMON
    + """\
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
  return bit.tobit(total)
end
local expected = reference_result(run, 20)
run_with_counters("low32_bitops_mix", 20, run, function(result)
  testlib.eq(result, expected, "low32_bitops_mix")
end)
""",
    "low32_logical_tail_add": LUA_COMMON
    + """\
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
  return bit.tobit(total)
end
local expected = reference_result(run, 20)
run_with_counters("low32_logical_tail_add", 20, run, function(result)
  testlib.eq(result, expected, "low32_logical_tail_add")
end)
""",
    "low32_logical_tail_store": LUA_COMMON
    + """\
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
      local y = sink[1]
      if x == y then
        total = total + 1
      end
    end
  end
  return bit.tobit(total + sink[1])
end
local expected = reference_result(run, 20)
run_with_counters("low32_logical_tail_store", 20, run, function(result)
  testlib.eq(result, expected, "low32_logical_tail_store")
end)
""",
    "lower_frame_lua_abs": LUA_COMMON
    + """\
local function run_lua_abs()
  local total = 0
  for i = 1, 80000 do
    local x = (i % 17) - 8
    if x < 0 then
      x = -x
    end
    total = total + x
  end
  return total
end
local function run(_)
  local out = 0
  for _ = 1, 4 do
    out = run_lua_abs()
  end
  return out
end
local expected = reference_result(run, 1)
run_with_counters("lower_frame_lua_abs", 1, run, function(result)
  testlib.eq(result, expected, "lower_frame_lua_abs")
end)
""",
    "string_scan_strto_cache": LUA_COMMON
    + """\
local values = { "1.25", "2.5", "3.75", "4.125" }
local function run(n)
  local total = 0
  for i = 1, n do
    total = total + tonumber(values[(i % #values) + 1])
  end
  return total
end
local expected = reference_result(run, 64000)
run_with_counters("string_scan_strto_cache", 64000, run, function(result)
  if math.abs(result - expected) > 1e-9 then
    error("string_scan_strto_cache: expected " .. tostring(expected) ..
          ", got " .. tostring(result))
  end
end)
""",
}


TARGETS: dict[str, dict[str, Any]] = {
    "low32_home": {
        "summary": "low32-home bitop/add/PHI state contract and normalization-boundary attribution",
        "families": ["bitops_mix", "logical_chain_tail_add", "logical_chain_tail_store"],
        "focus": ["low32_bitops_mix", "low32_logical_tail_add", "low32_logical_tail_store"],
        "oracle": False,
        "mechanism_env": {
            "LUAJIT_S390X_ADDHOME_LOG": "1",
            "LUAJIT_S390X_BNORM_LOG": "1",
            "LUAJIT_S390X_LOW32CMP_LOG": "1",
            "LUAJIT_S390X_LOW32HOME_LOG": "1",
        },
        "target_rows": [
            "bitops_mix/mix_bits/hot",
            "logical_chain_tail_add/chain_tail_add/hot",
            "logical_chain_tail_store/chain_tail_store/hot",
        ],
    },
    "lower_frame_body": {
        "summary": "lower-frame lua_abs compiled-body MOD/SUBOV/CONV/ADD attribution",
        "families": ["lower_frame_same_callsite"],
        "focus": ["lower_frame_lua_abs"],
        "oracle": False,
        "target_rows": [
            "lower_frame_same_callsite/lua_abs_same_callsite/hot",
            "lower_frame_same_callsite/const_same_callsite/hot",
        ],
    },
    "string_scan": {
        "summary": "string-to-number scan/cache helper and semantic string parsing attribution",
        "families": ["be_helpers"],
        "focus": ["string_scan_strto_cache"],
        "oracle": False,
        "target_rows": [
            "be_helpers/strto_loop/hot",
            "be_helpers/num_aload_loop/hot",
        ],
    },
    "ffi_cdata_width": {
        "summary": "cdata width MOD, narrow XSTORE/XLOAD, CONV, ADDOV/PHI attribution",
        "families": ["ffi_cdata"],
        "focus": ["ffi_cdata_mixed_width"],
        "oracle": False,
        "target_rows": [
            "ffi_cdata/mixed_width_loop/hot",
            "ffi_cdata/pair_loop/hot",
            "ffi_cdata/buffer_fref_loop/hot",
        ],
    },
    "ffi_fixed_gpr": {
        "summary": "fixed FFI GPR call setup, spill, return-value handoff attribution",
        "families": ["ffi_fixed_call_pressure", "ffi_fixed_struct_calls"],
        "focus": ["ffi_fixed_gpr_pressure"],
        "oracle": True,
        "target_rows": [
            "ffi_fixed_call_pressure/gpr_pressure/hot",
            "ffi_fixed_call_pressure/fpr_pressure/hot",
        ],
    },
    "iterator_safety": {
        "summary": "unsafe ITERN/JLOOP/TGETV safety-debt attribution",
        "families": ["iterator_table", "mixed_noffi"],
        "focus": ["iterator_pairs_loop_chain"],
        "oracle": False,
        "target_rows": [
            "iterator_table/pairs_sum/hot",
            "iterator_table/pairs_array_sum/hot",
            "mixed_noffi/mixed_loop/hot",
        ],
    },
    "be_number_helper": {
        "summary": "number-helper and localized tobit lowering residual attribution",
        "families": ["be_helpers", "be_helpers_localized"],
        "focus": ["be_number_helper", "be_number_helper_local_tobit"],
        "oracle": False,
        "target_rows": [
            "be_helpers/number_helper_loop/hot",
            "be_helpers_localized/number_helper_loop_local_tobit/hot",
        ],
    },
}


def write_text(path: pathlib.Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def shell_env_prefix(env: dict[str, str] | None) -> str:
    if not env:
        return ""
    return "env " + " ".join(f"{key}={shlex.quote(value)}" for key, value in sorted(env.items())) + " "


def prepare_focused_scripts(host: str, remote_tmp: str, names: list[str]) -> None:
    parts = ["set -euo pipefail", f"mkdir -p {shlex.quote(remote_tmp)}"]
    for name in names:
        parts.extend(
            [
                f"cat > {shlex.quote(f'{remote_tmp}/{name}.lua')} <<'EOF'",
                FOCUSED_SCRIPTS[name].rstrip(),
                "EOF",
            ]
        )
    proc = restamp.run_ssh_script(host, "\n".join(parts) + "\n")
    restamp.require_ok(proc, f"{host} prepare acceleration scripts")


def parse_key_value_lines(text: str) -> dict[str, int | str]:
    data: dict[str, int | str] = {}
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        parts = line.split(None, 1)
        if len(parts) != 2:
            continue
        key, value = parts
        if key.isupper() or key in {"REMOTE_RC", "PERF_RC"}:
            try:
                data[key] = int(value)
            except ValueError:
                data[key] = value
    return data


def run_capture(
    host: str,
    script: str,
    *,
    stdout_path: pathlib.Path,
    stderr_path: pathlib.Path,
) -> tuple[int, str, str]:
    proc = restamp.run_ssh_script(host, script)
    write_text(stdout_path, proc.stdout)
    write_text(stderr_path, proc.stderr)
    return proc.returncode, proc.stdout, proc.stderr


def run_official_ab(
    *,
    host: str,
    repo: str,
    target: dict[str, Any],
    output_dir: pathlib.Path,
    raw_dir: pathlib.Path,
    samples: int,
    warmup: int,
    passes: int,
    pin_core: int | None,
    retained_env: dict[str, str],
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    bench_files = [jitter.BENCH_FILES[family] for family in target["families"]]
    remote_tmp = f"/tmp/{host}-accel-official-{dt.datetime.now().strftime('%Y%m%d%H%M%S')}"
    proc = restamp.run_ssh_script(host, f"mkdir -p {shlex.quote(remote_tmp)}")
    restamp.require_ok(proc, f"{host} acceleration official tmp")
    pass_rows: list[dict[str, Any]] = []
    all_records: list[dict[str, Any]] = []
    try:
        for pass_no in range(1, passes + 1):
            order = "jit-first" if pass_no % 2 else "joff-first"
            jit_json = f"{remote_tmp}/pass{pass_no}-jit.jsonl"
            joff_json = f"{remote_tmp}/pass{pass_no}-joff.jsonl"
            if order == "jit-first":
                jit_records = jitter.run_mode(
                    host=host,
                    repo=repo,
                    remote_json=jit_json,
                    local_json=output_dir / f"pass{pass_no}-jit.jsonl",
                    stdout_log=raw_dir / f"pass{pass_no}-jit.stdout.log",
                    stderr_log=raw_dir / f"pass{pass_no}-jit.stderr.log",
                    bench_files=bench_files,
                    samples=samples,
                    warmup=warmup,
                    pin_core=pin_core,
                    joff=False,
                    extra_env=retained_env,
                )
                joff_records = jitter.run_mode(
                    host=host,
                    repo=repo,
                    remote_json=joff_json,
                    local_json=output_dir / f"pass{pass_no}-joff.jsonl",
                    stdout_log=raw_dir / f"pass{pass_no}-joff.stdout.log",
                    stderr_log=raw_dir / f"pass{pass_no}-joff.stderr.log",
                    bench_files=bench_files,
                    samples=samples,
                    warmup=warmup,
                    pin_core=pin_core,
                    joff=True,
                    extra_env=retained_env,
                )
            else:
                joff_records = jitter.run_mode(
                    host=host,
                    repo=repo,
                    remote_json=joff_json,
                    local_json=output_dir / f"pass{pass_no}-joff.jsonl",
                    stdout_log=raw_dir / f"pass{pass_no}-joff.stdout.log",
                    stderr_log=raw_dir / f"pass{pass_no}-joff.stderr.log",
                    bench_files=bench_files,
                    samples=samples,
                    warmup=warmup,
                    pin_core=pin_core,
                    joff=True,
                    extra_env=retained_env,
                )
                jit_records = jitter.run_mode(
                    host=host,
                    repo=repo,
                    remote_json=jit_json,
                    local_json=output_dir / f"pass{pass_no}-jit.jsonl",
                    stdout_log=raw_dir / f"pass{pass_no}-jit.stdout.log",
                    stderr_log=raw_dir / f"pass{pass_no}-jit.stderr.log",
                    bench_files=bench_files,
                    samples=samples,
                    warmup=warmup,
                    pin_core=pin_core,
                    joff=False,
                    extra_env=retained_env,
                )
            pass_rows.extend(
                jitter.summarize_pass(
                    pass_no=pass_no,
                    order=order,
                    jit_records=jit_records,
                    joff_records=joff_records,
                )
            )
            all_records.extend(jit_records)
            all_records.extend(joff_records)
            print(f"pass {pass_no} {order}")
    finally:
        restamp.run_ssh_script(host, f"rm -rf {shlex.quote(remote_tmp)}")
    aggregate = jitter.summarize_all(pass_rows, threshold=1.01)
    write_text(output_dir / "official-pass-rows.json", json.dumps(pass_rows, indent=2, sort_keys=True) + "\n")
    write_text(output_dir / "official-aggregate.json", json.dumps(aggregate, indent=2, sort_keys=True) + "\n")
    return pass_rows, aggregate


def run_trace_count(
    *,
    host: str,
    repo: str,
    remote_tmp: str,
    name: str,
    raw_dir: pathlib.Path,
    retained_env: dict[str, str],
) -> dict[str, int | str]:
    script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
set +e
{shell_env_prefix(retained_env)}timeout {PROBE_TIMEOUT_SECS} ./src/luajit {shlex.quote(f"{remote_tmp}/{name}.lua")}
rc=$?
set -e
printf 'REMOTE_RC %s\\n' "$rc"
"""
    _, stdout, _ = run_capture(
        host,
        script,
        stdout_path=raw_dir / f"{name}.stdout.log",
        stderr_path=raw_dir / f"{name}.stderr.log",
    )
    return parse_key_value_lines(stdout)


def run_dump(
    *,
    host: str,
    repo: str,
    remote_tmp: str,
    name: str,
    raw_dir: pathlib.Path,
    retained_env: dict[str, str],
) -> None:
    script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
set +e
{shell_env_prefix(retained_env)}timeout {PROBE_TIMEOUT_SECS} ./src/luajit -jdump=ism {shlex.quote(f"{remote_tmp}/{name}.lua")}
rc=$?
set -e
printf 'REMOTE_RC %s\\n' "$rc"
"""
    run_capture(
        host,
        script,
        stdout_path=raw_dir / f"{name}.stdout.log",
        stderr_path=raw_dir / f"{name}.stderr.log",
    )


def run_perf_stat(
    *,
    host: str,
    repo: str,
    remote_tmp: str,
    name: str,
    raw_dir: pathlib.Path,
    pin_core: int | None,
    retained_env: dict[str, str],
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
perf stat -x, -e cycles,instructions,branches,branch-misses -- {shell_env_prefix(retained_env)}timeout {PROBE_TIMEOUT_SECS} {taskset}./src/luajit {shlex.quote(f"{remote_tmp}/{name}.lua")}
rc=$?
set -e
printf 'PERF_RC %s\\n' "$rc"
"""
    rc, stdout, stderr = run_capture(
        host,
        script,
        stdout_path=raw_dir / f"{name}.stdout.log",
        stderr_path=raw_dir / f"{name}.stderr.log",
    )
    if rc != 0:
        return {"status": "unavailable", "reason": f"exit-{rc}"}
    if "PERF_STATUS unavailable:" in stdout:
        return {"status": "unavailable", "reason": stdout.strip().split(":", 1)[1]}
    counters: dict[str, str] = {}
    for raw_line in stderr.splitlines():
        parts = [part.strip() for part in raw_line.split(",")]
        if len(parts) >= 3 and parts[0] and not parts[0].startswith("#"):
            counters[parts[2]] = parts[0]
    return {"status": "ok", "counters": counters, "stdout": stdout.strip()}


def run_official_dumps(
    *,
    host: str,
    repo: str,
    target: dict[str, Any],
    raw_dir: pathlib.Path,
    retained_env: dict[str, str],
) -> None:
    dump_env = dict(retained_env)
    dump_env.update(
        {
            "S390X_PERF_SAMPLES": "1",
            "S390X_PERF_WARMUP": "0",
        }
    )
    for family in target["families"]:
        bench_file = jitter.BENCH_FILES[family]
        env = dict(dump_env)
        env["S390X_PERF_BENCH_FILE"] = bench_file
        script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
set +e
{shell_env_prefix(env)}timeout {PROBE_TIMEOUT_SECS} ./src/luajit -jdump=ism {shlex.quote(bench_file)}
rc=$?
set -e
printf 'REMOTE_RC %s\\n' "$rc"
"""
        run_capture(
            host,
            script,
            stdout_path=raw_dir / f"{family}.stdout.log",
            stderr_path=raw_dir / f"{family}.stderr.log",
        )


def classify_trace(counts: dict[str, int | str]) -> str:
    remote_rc = int(counts.get("REMOTE_RC", 0)) if isinstance(counts.get("REMOTE_RC"), int) else -1
    if remote_rc == 124:
        return "timeout"
    if remote_rc != 0:
        return "failed"
    texits = int(counts.get("TEXIT_COUNT", 0)) if isinstance(counts.get("TEXIT_COUNT"), int) else 0
    aborts = int(counts.get("TRACE_ABORT", 0)) if isinstance(counts.get("TRACE_ABORT"), int) else 0
    if texits:
        return "exit-or-guard-dominated"
    if aborts:
        return "abort-influenced"
    return "compiled-body-dominated"


def write_summary(
    *,
    output_dir: pathlib.Path,
    host: str,
    repo: str,
    target_name: str,
    target: dict[str, Any],
    commit: str,
    remote_hashes: dict[str, str | None],
    pass_rows: list[dict[str, Any]],
    aggregate: list[dict[str, Any]],
    trace_counts: dict[str, dict[str, int | str]],
    perf_stats: dict[str, dict[str, object]],
    mechanism_env: dict[str, str],
) -> None:
    target_rows = set(target["target_rows"])
    lines = [
        "# s390x Acceleration Truth Pack",
        "",
        f"- Host: `{host}`",
        f"- Repo: `{repo}`",
        f"- Commit: `{commit}`",
        f"- Target: `{target_name}`",
        f"- Target summary: {target['summary']}",
        f"- Generated: `{dt.datetime.now().astimezone().isoformat()}`",
        "",
        "## Mechanism Environment",
        "",
    ]
    for key, value in sorted(mechanism_env.items()):
        lines.append(f"- `{key}={value}`")
    lines.extend(
        [
            "",
            "## Delivered File Hashes",
            "",
        ]
    )
    for relpath in HASH_STAMP_PATHS:
        lines.append(f"- `{relpath}`: `{remote_hashes.get(relpath) or 'missing'}`")
    lines.extend(["", "## Official Retained A/B", ""])
    lines.append("| row | median ratio | red passes | median jit | median joff | ratios |")
    lines.append("| --- | ---: | ---: | ---: | ---: | --- |")
    rows_by_name = {row["row"]: row for row in aggregate}
    for row_name in target["target_rows"]:
        row = rows_by_name.get(row_name)
        if row is None:
            continue
        ratios = ", ".join(f"{ratio:.4f}" for ratio in row["ratios"])
        lines.append(
            f"| `{row_name}` | `{row['median_ratio']:.4f}` | "
            f"`{row['positive']}/{row['passes']}` | `{row['median_jit']:.6f}` | "
            f"`{row['median_joff']:.6f}` | `{ratios}` |"
        )
    other_hot = [row for row in aggregate if row["row"] not in target_rows]
    if other_hot:
        lines.extend(["", "## Sibling Hot Rows", ""])
        for row in other_hot:
            lines.append(
                f"- `{row['row']}` median ratio `{row['median_ratio']:.4f}`, "
                f"red `{row['positive']}/{row['passes']}`"
            )
    lines.extend(["", "## Focused Mechanism Classification", ""])
    for name, counts in trace_counts.items():
        classification = classify_trace(counts)
        lines.append(
            f"- `{name}`: `{classification}`, `REMOTE_RC {counts.get('REMOTE_RC', 'n/a')}`, "
            f"`TRACE_START {counts.get('TRACE_START', 'n/a')}`, "
            f"`TRACE_STOP {counts.get('TRACE_STOP', 'n/a')}`, "
            f"`TRACE_ABORT {counts.get('TRACE_ABORT', 'n/a')}`, "
            f"`TEXIT_COUNT {counts.get('TEXIT_COUNT', 'n/a')}`"
        )
        if counts.get("TRACE_HIST"):
            lines.append(f"  - trace hist `{counts['TRACE_HIST']}`")
        if counts.get("TEXIT_HIST"):
            lines.append(f"  - exit hist `{counts['TEXIT_HIST']}`")
    lines.extend(["", "## perf stat", ""])
    for name, info in perf_stats.items():
        if info.get("status") != "ok":
            lines.append(f"- `{name}`: unavailable (`{info.get('reason', 'unknown')}`)")
            continue
        counters = info.get("counters", {})
        lines.append(
            f"- `{name}`: cycles `{counters.get('cycles', 'n/a')}`, "
            f"instructions `{counters.get('instructions', 'n/a')}`, "
            f"branches `{counters.get('branches', 'n/a')}`, "
            f"branch-misses `{counters.get('branch-misses', 'n/a')}`"
        )
    lines.extend(
        [
            "",
            "## Raw Artifacts",
            "",
            f"- Official pass rows: `{output_dir / 'official-pass-rows.json'}`",
            f"- Official aggregate: `{output_dir / 'official-aggregate.json'}`",
            f"- Trace counts: `{output_dir / 'raw' / 'trace-counts'}`",
            f"- IR/snapshot/mcode dumps: `{output_dir / 'raw' / 'dump'}`",
            f"- Official benchmark dumps: `{output_dir / 'raw' / 'official-dump'}`",
            f"- perf stat logs: `{output_dir / 'raw' / 'perf-stat'}`",
        ]
    )
    write_text(output_dir / "summary.md", "\n".join(lines) + "\n")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", choices=restamp.HOST_LABELS, default="kdz")
    parser.add_argument("--repo")
    parser.add_argument("--target", choices=sorted(TARGETS), default="ffi_cdata_width")
    parser.add_argument("--output-dir", type=pathlib.Path)
    parser.add_argument("--samples", type=int, default=5)
    parser.add_argument("--warmup", type=int, default=2)
    parser.add_argument("--passes", type=int, default=3)
    parser.add_argument("--pin-core", type=int, default=restamp.DEFAULT_PIN_CORE)
    parser.add_argument("--skip-sync", action="store_true")
    parser.add_argument("--skip-build", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    host = args.host
    target = TARGETS[args.target]
    repo = args.repo or restamp.AUTHORITATIVE_REPOS[host]
    timestamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    output_dir = (args.output_dir or DEFAULT_OUTPUT_ROOT / f"{timestamp}-{host}-{args.target}-accel-truth-pack").resolve()
    raw_dir = output_dir / "raw"
    trace_dir = raw_dir / "trace-counts"
    dump_dir = raw_dir / "dump"
    perf_dir = raw_dir / "perf-stat"
    official_dump_dir = raw_dir / "official-dump"
    build_dir = raw_dir / "build"
    for directory in (trace_dir, dump_dir, perf_dir, official_dump_dir, build_dir):
        directory.mkdir(parents=True, exist_ok=True)

    retained_env = dict(restamp.RETAINED_BASELINE_ENV)
    mechanism_env = dict(retained_env)
    mechanism_env.update(target.get("mechanism_env", {}))
    commit = restamp.current_commit()
    if not args.skip_sync:
        restamp.sync_tracked_files(host, repo)
    if not args.skip_build:
        restamp.build_remote_repo(host, repo, build_dir)
    if target["oracle"]:
        jitter.build_remote_oracles(host, repo, build_dir)
    remote_hashes = restamp.remote_file_hashes(host, repo, HASH_STAMP_PATHS)

    remote_tmp = f"/tmp/{host}-{args.target}-accel-{timestamp}"
    try:
        prepare_focused_scripts(host, remote_tmp, target["focus"])
        pass_rows, aggregate = run_official_ab(
            host=host,
            repo=repo,
            target=target,
            output_dir=output_dir,
            raw_dir=build_dir,
            samples=args.samples,
            warmup=args.warmup,
            passes=args.passes,
            pin_core=args.pin_core,
            retained_env=retained_env,
        )
        trace_counts: dict[str, dict[str, int | str]] = {}
        perf_stats: dict[str, dict[str, object]] = {}
        for name in target["focus"]:
            trace_counts[name] = run_trace_count(
                host=host,
                repo=repo,
                remote_tmp=remote_tmp,
                name=name,
                raw_dir=trace_dir,
                retained_env=mechanism_env,
            )
            run_dump(
                host=host,
                repo=repo,
                remote_tmp=remote_tmp,
                name=name,
                raw_dir=dump_dir,
                retained_env=mechanism_env,
            )
            perf_stats[name] = run_perf_stat(
                host=host,
                repo=repo,
                remote_tmp=remote_tmp,
                name=name,
                raw_dir=perf_dir,
                pin_core=args.pin_core,
                retained_env=retained_env,
            )
        run_official_dumps(
            host=host,
            repo=repo,
            target=target,
            raw_dir=official_dump_dir,
            retained_env=mechanism_env,
        )
        write_summary(
            output_dir=output_dir,
            host=host,
            repo=repo,
            target_name=args.target,
            target=target,
            commit=commit,
            remote_hashes=remote_hashes,
            pass_rows=pass_rows,
            aggregate=aggregate,
            trace_counts=trace_counts,
            perf_stats=perf_stats,
            mechanism_env=target.get("mechanism_env", {}),
        )
    finally:
        restamp.run_ssh_script(host, f"rm -rf {shlex.quote(remote_tmp)}")

    print(f"summary={output_dir / 'summary.md'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
