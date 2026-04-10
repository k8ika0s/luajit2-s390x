#!/usr/bin/env python3
"""Build a focused dispatch/side-exit truth pack on native s390x."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import pathlib
import re
import shlex
import statistics
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
    "LUAJIT_S390X_SUM_LOOP_FORL_BLACKLIST": "1",
    "LUAJIT_S390X_VARARG_SIBLING_FORL_BLACKLIST": "1",
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
}
HASH_STAMP_PATHS = list(
    dict.fromkeys(restamp.AUTHORITATIVE_HASH_PATHS + ["tools/s390x/build_dispatch_truth_pack.py"])
)
BENCH_FILE = "tests/s390x/perf/dispatch_trace.lua"
LOOP_NAMES = ("numeric_loop", "side_exit_loop", "hotexit_loop")
FOCUSED_ITERATIONS = 80000
TRACE_OBS_ITERATIONS = 2000
CURRENT_SEAM_NAME = "loop-body-entry-after-JFORI"
FOCUSED_WORK_ITEMS = {
    "numeric_loop": 80000,
    "side_exit_loop": 80000,
    "hotexit_loop": 80000,
}
PROOF_STOP = 40000
PROOF_OUTER = 2000

CHECK_SCRIPTS = {
    "numeric_loop": """\
local function run(n)
  local total = 0
  for i = 1, n do
    total = total + (i % 97)
  end
  return total
end
print("NUMERIC_LOOP", run(20))
""",
    "side_exit_loop": """\
local function run(n)
  local total = 0
  for i = 1, n do
    if i % 7 == 0 then
      total = total - (i % 97)
    else
      total = total + (i % 97)
    end
  end
  return total
end
print("SIDE_EXIT_LOOP", run(20))
""",
    "hotexit_loop": """\
local function run(n)
  local total = 0
  for i = 1, n do
    if i % 5 == 0 then
      total = total + ((i % 97) * 3)
    elseif i % 3 == 0 then
      total = total - (i % 97)
    else
      total = total + 1
    end
  end
  return total
end
print("HOTEXIT_LOOP", run(20))
""",
}

EXPECTED_CHECKS = {
    "numeric_loop": "NUMERIC_LOOP 210",
    "side_exit_loop": "SIDE_EXIT_LOOP 168",
    "hotexit_loop": "HOTEXIT_LOOP 113",
}

PROOF_DRIVER_C = """\
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern int32_t current_loop_control(int32_t stop);
extern int32_t bxle_loop_control(int32_t stop);

typedef int32_t (*loop_fn)(int32_t stop);

static uint64_t monotonic_ns(void)
{
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    perror("clock_gettime");
    exit(2);
  }
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int64_t run_many(loop_fn fn, int32_t outer, int32_t stop)
{
  int64_t total = 0;
  for (int32_t i = 0; i < outer; i++) {
    total += fn(stop);
  }
  return total;
}

int main(int argc, char **argv)
{
  if (argc != 4) {
    fprintf(stderr, "usage: %s <current|bxle> <outer> <stop>\\n", argv[0]);
    return 2;
  }

  const char *mode = argv[1];
  int32_t outer = (int32_t)strtol(argv[2], NULL, 0);
  int32_t stop = (int32_t)strtol(argv[3], NULL, 0);
  loop_fn fn = NULL;
  if (strcmp(mode, "current") == 0) {
    fn = current_loop_control;
  } else if (strcmp(mode, "bxle") == 0) {
    fn = bxle_loop_control;
  } else {
    fprintf(stderr, "unknown mode: %s\\n", mode);
    return 2;
  }

  uint64_t started = monotonic_ns();
  int64_t result = run_many(fn, outer, stop);
  uint64_t finished = monotonic_ns();
  int64_t expected = (int64_t)outer * ((int64_t)stop * (int64_t)(stop + 1) / 2);
  if (result != expected) {
    fprintf(stderr, "proof-result-mismatch mode=%s got=%lld expected=%lld\\n",
            mode, (long long)result, (long long)expected);
    return 3;
  }

  printf("MODE %s\\n", mode);
  printf("OUTER %d\\n", outer);
  printf("STOP %d\\n", stop);
  printf("RESULT %lld\\n", (long long)result);
  printf("EXPECTED %lld\\n", (long long)expected);
  printf("SECONDS %.9f\\n", (double)(finished - started) / 1000000000.0);
  return 0;
}
"""

PROOF_LOOP_CONTROL_S = """\
.text
.globl current_loop_control
.type current_loop_control,@function
current_loop_control:
  lgr %r5, %r2
  lhi %r3, 0
  lhi %r4, 1
  lhi %r2, 0
.Lcurrent_loop:
  ar %r3, %r4
  cr %r3, %r5
  jh .Lcurrent_done
  ar %r2, %r3
  j .Lcurrent_loop
.Lcurrent_done:
  br %r14

.globl bxle_loop_control
.type bxle_loop_control,@function
bxle_loop_control:
  lgr %r5, %r2
  lhi %r2, 0
  lhi %r3, 0
  lhi %r4, 1
  larl %r1, .Lbxle_body
  bxle %r2,%r4,0(%r1)
  lgr %r2, %r3
  br %r14
.Lbxle_body:
  ar %r3, %r2
  bxle %r2,%r4,0(%r1)
  lgr %r2, %r3
  br %r14
"""

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
  local function run(n)
    local total = 0
    for i = 1, n do
      total = total + (i % 97)
    end
    return total
  end
  add_case("numeric_loop", run(80000), run)
end

do
  local function run(n)
    local total = 0
    for i = 1, n do
      if i % 7 == 0 then
        total = total - (i % 97)
      else
        total = total + (i % 97)
      end
    end
    return total
  end
  add_case("side_exit_loop", run(80000), run)
end

do
  local function run(n)
    local total = 0
    for i = 1, n do
      if i % 5 == 0 then
        total = total + ((i % 97) * 3)
      elseif i % 3 == 0 then
        total = total - (i % 97)
      else
        total = total + 1
      end
    end
    return total
  end
  add_case("hotexit_loop", run(80000), run)
end

bench.run_suite({ family = "dispatch_truth_pack", cases = cases })
"""

TRACE_COUNT_SCRIPTS = {
    "numeric_loop": """\
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
local function run(n)
  local total = 0
  for i = 1, n do
    total = total + (i % 97)
  end
  return total
end
run(20); run(20); run(20)
local trace_cap = testlib.trace_counter_capture()
local texit_cap = testlib.texit_counter_capture()
print("RESULT", run(2000))
trace_cap.stop()
texit_cap.stop()
print("TRACE_START", trace_cap.start)
print("TRACE_STOP", trace_cap.stop_count)
print("TRACE_ABORT", trace_cap.abort)
print("TEXIT_COUNT", texit_cap.total)
emit_hist("TRACE_HIST", trace_cap.hist)
emit_hist("TEXIT_HIST", texit_cap.hist)
""",
    "side_exit_loop": """\
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
local function run(n)
  local total = 0
  for i = 1, n do
    if i % 7 == 0 then
      total = total - (i % 97)
    else
      total = total + (i % 97)
    end
  end
  return total
end
run(20); run(20); run(20)
local trace_cap = testlib.trace_counter_capture()
local texit_cap = testlib.texit_counter_capture()
print("RESULT", run(2000))
trace_cap.stop()
texit_cap.stop()
print("TRACE_START", trace_cap.start)
print("TRACE_STOP", trace_cap.stop_count)
print("TRACE_ABORT", trace_cap.abort)
print("TEXIT_COUNT", texit_cap.total)
emit_hist("TRACE_HIST", trace_cap.hist)
emit_hist("TEXIT_HIST", texit_cap.hist)
""",
    "hotexit_loop": """\
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
local function run(n)
  local total = 0
  for i = 1, n do
    if i % 5 == 0 then
      total = total + ((i % 97) * 3)
    elseif i % 3 == 0 then
      total = total - (i % 97)
    else
      total = total + 1
    end
  end
  return total
end
run(20); run(20); run(20)
local trace_cap = testlib.trace_counter_capture()
local texit_cap = testlib.texit_counter_capture()
print("RESULT", run(2000))
trace_cap.stop()
texit_cap.stop()
print("TRACE_START", trace_cap.start)
print("TRACE_STOP", trace_cap.stop_count)
print("TRACE_ABORT", trace_cap.abort)
print("TEXIT_COUNT", texit_cap.total)
emit_hist("TRACE_HIST", trace_cap.hist)
emit_hist("TEXIT_HIST", texit_cap.hist)
""",
}

PERF_STAT_SCRIPTS = {
    name: script.replace('local testlib = dofile("tests/s390x/helpers/testlib.lua")\n', "").replace("testlib.enable_repo_jit_modules()\n", "").replace("local trace_cap = testlib.trace_counter_capture()\n", "").replace("local texit_cap = testlib.texit_counter_capture()\n", "").replace("trace_cap.stop()\n", "").replace("texit_cap.stop()\n", "").replace('print("TRACE_START", trace_cap.start)\n', "").replace('print("TRACE_STOP", trace_cap.stop_count)\n', "").replace('print("TRACE_ABORT", trace_cap.abort)\n', "").replace('print("TEXIT_COUNT", texit_cap.total)\n', "").replace('emit_hist("TRACE_HIST", trace_cap.hist)\n', "").replace('emit_hist("TEXIT_HIST", texit_cap.hist)\n', "")
    for name, script in TRACE_COUNT_SCRIPTS.items()
}

EXIT_LOG_SCRIPTS = {
    name: script.replace('print("TRACE_START", trace_cap.start)\n', "").replace('print("TRACE_STOP", trace_cap.stop_count)\n', "").replace('print("TRACE_ABORT", trace_cap.abort)\n', "").replace('print("TEXIT_COUNT", texit_cap.total)\n', "").replace('emit_hist("TRACE_HIST", trace_cap.hist)\n', "").replace('emit_hist("TEXIT_HIST", texit_cap.hist)\n', "")
    for name, script in TRACE_COUNT_SCRIPTS.items()
}

TRACE_START_RE = re.compile(r"^---- TRACE (\d+) start(?: ([0-9]+)/([0-9]+))?")
TRACE_ABORT_RE = re.compile(r"^---- TRACE (\d+) abort .* -- (.+)$")
TRACE_STOP_RE = re.compile(r"^---- TRACE (\d+) stop -> (\w+)$")
JLOOP_EXIT_RE = re.compile(
    r"^S390X_JLOOP_EXIT parent=(\d+) exit=(\d+) pc=([^ ]+) op=(\d+) target=(\d+) target_exec=(\d+) .* retop=(\d+) trace=(\d+) link=(\d+) linktype=(\d+) .* target_startop=(\d+) .* target_resumevalid=(\d+) target_resumechild=(\d+) .*$"
)
JLOOP_EXIT_PHASE_RE = re.compile(
    r"^S390X_JLOOP_EXIT phase=([^ ]+) parent=(\d+) exit=(\d+) trace=(\d+) retop=(\d+) state=(\d+)$"
)


def load_bc_op_names() -> list[str]:
    text = (ROOT / "src" / "lj_bc.h").read_text(encoding="utf-8")
    start = text.index("#define BCDEF(_)")
    end = text.index("/* Bytecode opcode numbers. */")
    names: list[str] = []
    for raw_line in text[start:end].splitlines():
        line = raw_line.strip()
        if line.startswith("_("):
            names.append(line.split("(", 1)[1].split(",", 1)[0])
    return names


BC_OP_NAMES = load_bc_op_names()


def bc_op_name(value: object) -> str | None:
    if not isinstance(value, int):
        return None
    if value < 0 or value >= len(BC_OP_NAMES):
        return None
    return BC_OP_NAMES[value]


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


def parse_token_value(value: str) -> object:
    try:
        return int(value, 0)
    except ValueError:
        return value


def parse_prefixed_kv_line(line: str, prefix: str) -> dict[str, object] | None:
    if not line.startswith(prefix):
        return None
    payload = line[len(prefix):].strip()
    info: dict[str, object] = {}
    for token in payload.split():
        if "=" not in token:
            continue
        key, value = token.split("=", 1)
        parsed = parse_token_value(value)
        info[key] = parsed
        if key.endswith("op"):
            op_name = bc_op_name(parsed)
            if op_name is not None:
                info[f"{key}_name"] = op_name
    return info


def remote_env_prefix(*, jsonl_path: str | None, samples: int, warmup: int,
                      extra_env: dict[str, str] | None = None) -> str:
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
    if extra_env:
        env.extend(f"{key}={shlex.quote(value)}" for key, value in sorted(extra_env.items()))
    return "env " + " ".join(env)


def prepare_truth_scripts(host: str, remote_tmp: str) -> None:
    lines = ["set -euo pipefail"]
    lines.extend(
        [
            f'cat >"{remote_tmp}/focused_bench.lua" <<\'EOF\'',
            FOCUSED_BENCH_SCRIPT.rstrip(),
            "EOF",
        ]
    )
    for group in (CHECK_SCRIPTS, TRACE_COUNT_SCRIPTS, PERF_STAT_SCRIPTS, EXIT_LOG_SCRIPTS):
        suffix = {
            id(CHECK_SCRIPTS): "",
            id(TRACE_COUNT_SCRIPTS): "_trace",
            id(PERF_STAT_SCRIPTS): "_perf",
            id(EXIT_LOG_SCRIPTS): "_exitlog",
        }[id(group)]
        for name, content in group.items():
            lines.extend(
                [
                    f'cat >"{remote_tmp}/{name}{suffix}.lua" <<\'EOF\'',
                    content.rstrip(),
                    "EOF",
                ]
            )
    lines.extend(
        [
            f'cat >"{remote_tmp}/loop_control_proof.c" <<\'EOF\'',
            PROOF_DRIVER_C.rstrip(),
            "EOF",
            f'cat >"{remote_tmp}/loop_control_proof.s" <<\'EOF\'',
            PROOF_LOOP_CONTROL_S.rstrip(),
            "EOF",
        ]
    )
    proc = restamp.run_ssh_script(host, "\n".join(lines) + "\n")
    restamp.require_ok(proc, f"{host} dispatch truth-pack script setup")


def compile_loop_control_proof(host: str, remote_tmp: str, raw_dir: pathlib.Path) -> None:
    script = f"""
set -euo pipefail
cd {shlex.quote(remote_tmp)}
cc -O2 loop_control_proof.c loop_control_proof.s -o loop_control_proof
"""
    restamp.run_remote_command(
        host,
        script,
        stdout_path=raw_dir / "compile.stdout.log",
        stderr_path=raw_dir / "compile.stderr.log",
        label=f"{host} loop-control proof compile",
    )


def parse_proof_run_output(text: str) -> dict[str, object]:
    data: dict[str, object] = {}
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        parts = line.split(None, 1)
        if len(parts) != 2:
            continue
        key, value = parts
        if key in {"OUTER", "STOP", "RESULT", "EXPECTED"}:
            data[key.lower()] = int(value)
        elif key == "SECONDS":
            data["seconds"] = float(value)
        elif key == "MODE":
            data["mode"] = value
    return data


def run_loop_control_proof(
    *,
    host: str,
    remote_tmp: str,
    raw_dir: pathlib.Path,
    pin_core: int | None,
    samples: int,
    warmup: int,
    outer: int,
    stop: int,
) -> dict[str, object]:
    compile_loop_control_proof(host, remote_tmp, raw_dir)
    proof: dict[str, object] = {
        "status": "ok",
        "requires_extra_shims": False,
        "outer": outer,
        "stop": stop,
        "modes": {},
    }
    taskset = f"taskset -c {pin_core} " if pin_core is not None else ""
    for mode in ("current", "bxle"):
        sample_seconds: list[float] = []
        sample_results: list[int] = []
        expected_result: int | None = None
        for index in range(warmup + samples):
            stage = "warmup" if index < warmup else "sample"
            number = index + 1 if stage == "warmup" else index - warmup + 1
            script = f"""
set -euo pipefail
{taskset}{shlex.quote(f"{remote_tmp}/loop_control_proof")} {shlex.quote(mode)} {outer} {stop}
"""
            proc = restamp.run_remote_command(
                host,
                script,
                stdout_path=raw_dir / f"{mode}.{stage}{number}.stdout.log",
                stderr_path=raw_dir / f"{mode}.{stage}{number}.stderr.log",
                label=f"{host} loop-control proof {mode} {stage}{number}",
            )
            parsed = parse_proof_run_output(proc.stdout)
            result = parsed.get("result")
            expected = parsed.get("expected")
            seconds = parsed.get("seconds")
            if not isinstance(result, int) or not isinstance(expected, int) or not isinstance(seconds, float):
                raise restamp.RestampError(f"{host} loop-control proof {mode} parse failure")
            if expected_result is None:
                expected_result = expected
            elif expected_result != expected:
                raise restamp.RestampError(f"{host} loop-control proof {mode} expected drift")
            if stage == "sample":
                sample_seconds.append(seconds)
                sample_results.append(result)
        if not sample_seconds or expected_result is None:
            raise restamp.RestampError(f"{host} loop-control proof {mode} missing samples")
        proof["modes"][mode] = {
            "expected": expected_result,
            "sample_seconds": sample_seconds,
            "sample_results": sample_results,
            "median_seconds": statistics.median(sample_seconds),
            "min_seconds": min(sample_seconds),
            "max_seconds": max(sample_seconds),
        }
    current = proof["modes"]["current"]["median_seconds"]
    bxle = proof["modes"]["bxle"]["median_seconds"]
    proof["winner"] = "bxle" if bxle < current else "current"
    proof["speedup_vs_current"] = (current / bxle) if bxle else None
    proof["bxle_wins"] = bxle < current
    return proof


def run_dispatch_bench(
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
) -> tuple[list[dict[str, object]], dict[str, object]]:
    json_name = f"{mode_label}.jsonl"
    remote_json = f"{remote_tmp}/{json_name}"
    luajit_bin = shlex.quote(f"{repo}/src/luajit")
    luajit_args = ["-joff", BENCH_FILE] if joff else [BENCH_FILE]
    script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
rm -f {shlex.quote(remote_json)}
{remote_env_prefix(jsonl_path=remote_json, samples=samples, warmup=warmup, extra_env=extra_env)} {f"taskset -c {pin_core} " if pin_core is not None else ""}{luajit_bin} {' '.join(shlex.quote(arg) for arg in luajit_args)}
"""
    stdout_path = raw_dir / f"{mode_label}.stdout.log"
    stderr_path = raw_dir / f"{mode_label}.stderr.log"
    proc = restamp.run_ssh_script(host, script)
    write_text(stdout_path, proc.stdout)
    write_text(stderr_path, proc.stderr)
    if (
        proc.returncode != 0
        and "taskset: failed to execute ./src/luajit: No such file or directory" in proc.stderr
    ):
        retry = restamp.run_ssh_script(host, script)
        retry_stdout = proc.stdout + "\n=== RETRY dispatch bench ===\n" + retry.stdout
        retry_stderr = proc.stderr + "\n=== RETRY dispatch bench ===\n" + retry.stderr
        write_text(stdout_path, retry_stdout)
        write_text(stderr_path, retry_stderr)
        proc = retry
    fetch_script = f"""
set -euo pipefail
if [ -f {shlex.quote(remote_json)} ]; then
  cat {shlex.quote(remote_json)}
fi
"""
    fetch_proc = restamp.run_ssh_script(host, fetch_script)
    restamp.require_ok(fetch_proc, f"{host} fetch dispatch_trace {mode_label} jsonl")
    records: list[dict[str, object]] = []
    if fetch_proc.stdout.strip():
        local_json = raw_dir.parent.parent / json_name
        write_text(local_json, fetch_proc.stdout)
        records = restamp.parse_jsonl_records(local_json)
    status: dict[str, object] = {
        "status": "ok" if proc.returncode == 0 else "failed",
        "returncode": proc.returncode,
    }
    stderr_lines = [line.strip() for line in proc.stderr.splitlines() if line.strip()]
    if stderr_lines:
        status["message"] = stderr_lines[-1]
    return records, status


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
) -> tuple[list[dict[str, object]], dict[str, object]]:
    mode = "focused-jit-on" if not joff else "focused-joff"
    remote_json = f"{remote_tmp}/{mode}.jsonl"
    luajit_bin = shlex.quote(f"{repo}/src/luajit")
    luajit_cmd = f"{f'taskset -c {pin_core} ' if pin_core is not None else ''}{luajit_bin} {'-joff ' if joff else ''}{shlex.quote(f'{remote_tmp}/focused_bench.lua')}"
    script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
rm -f {shlex.quote(remote_json)}
{remote_env_prefix(jsonl_path=remote_json, samples=samples, warmup=warmup, extra_env=extra_env)} {luajit_cmd}
"""
    proc = restamp.run_ssh_script(host, script)
    write_text(raw_dir / f"{mode}.stdout.log", proc.stdout)
    write_text(raw_dir / f"{mode}.stderr.log", proc.stderr)
    fetch_script = f"""
set -euo pipefail
if [ -f {shlex.quote(remote_json)} ]; then
  cat {shlex.quote(remote_json)}
fi
"""
    fetch_proc = restamp.run_ssh_script(host, fetch_script)
    restamp.require_ok(fetch_proc, f"{host} fetch {mode} jsonl")
    records: list[dict[str, object]] = []
    if fetch_proc.stdout.strip():
        local_json = raw_dir.parent.parent / f"{mode}.jsonl"
        write_text(local_json, fetch_proc.stdout)
        records = restamp.parse_jsonl_records(local_json)
    status: dict[str, object] = {
        "status": "ok" if proc.returncode == 0 else "failed",
        "returncode": proc.returncode,
    }
    stderr_lines = [line.strip() for line in proc.stderr.splitlines() if line.strip()]
    if stderr_lines:
        status["message"] = stderr_lines[-1]
    return records, status


def run_check(host: str, repo: str, remote_tmp: str, raw_dir: pathlib.Path, name: str,
              extra_env: dict[str, str] | None = None) -> str:
    env_prefix = remote_env_prefix(jsonl_path=None, samples=0, warmup=0, extra_env=extra_env)
    script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
{env_prefix} ./src/luajit {shlex.quote(f"{remote_tmp}/{name}.lua")}
"""
    proc = restamp.run_remote_command(
        host,
        script,
        stdout_path=raw_dir / f"{name}.stdout.log",
        stderr_path=raw_dir / f"{name}.stderr.log",
        label=f"{host} dispatch check {name}",
    )
    return proc.stdout.strip()


def run_trace_count(host: str, repo: str, remote_tmp: str, raw_dir: pathlib.Path, name: str,
                    extra_env: dict[str, str] | None = None) -> dict[str, int | str]:
    env_prefix = remote_env_prefix(jsonl_path=None, samples=0, warmup=0, extra_env=extra_env)
    script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
{env_prefix} ./src/luajit {shlex.quote(f"{remote_tmp}/{name}_trace.lua")}
"""
    proc = restamp.run_remote_command(
        host,
        script,
        stdout_path=raw_dir / f"{name}.stdout.log",
        stderr_path=raw_dir / f"{name}.stderr.log",
        label=f"{host} dispatch trace count {name}",
    )
    return parse_key_value_lines(proc.stdout)


def run_mcode_dump(host: str, repo: str, remote_tmp: str, raw_dir: pathlib.Path, name: str,
                   extra_env: dict[str, str] | None = None) -> None:
    env_prefix = remote_env_prefix(jsonl_path=None, samples=0, warmup=0, extra_env=extra_env)
    script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
{env_prefix} ./src/luajit -jdump=ism {shlex.quote(f"{remote_tmp}/{name}_perf.lua")}
"""
    restamp.run_remote_command(
        host,
        script,
        stdout_path=raw_dir / f"{name}.stdout.log",
        stderr_path=raw_dir / f"{name}.stderr.log",
        label=f"{host} dispatch mcode dump {name}",
    )


def run_perf_stat(host: str, repo: str, remote_tmp: str, raw_dir: pathlib.Path, name: str,
                  pin_core: int | None, extra_env: dict[str, str] | None = None) -> dict[str, object]:
    taskset = f"taskset -c {pin_core} " if pin_core is not None else ""
    luajit_bin = shlex.quote(f"{repo}/src/luajit")
    env_prefix = remote_env_prefix(jsonl_path=None, samples=0, warmup=0, extra_env=extra_env)
    script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
if ! command -v perf >/dev/null 2>&1; then
  echo "PERF_STATUS unavailable:perf-not-found"
  exit 0
fi
perf stat -x, -e cycles,instructions,branches,branch-misses -- {env_prefix} {taskset}{luajit_bin} {shlex.quote(f"{remote_tmp}/{name}_perf.lua")}
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
        counters[event] = value
    return {"status": "ok", "counters": counters}


def run_exit_focus_log(
    host: str,
    repo: str,
    remote_tmp: str,
    raw_dir: pathlib.Path,
    name: str,
    parent: int,
    exitno: int,
    extra_env: dict[str, str] | None = None,
) -> None:
    env_items = {
        "LUAJIT_S390X_TRACE_START_LOG": "1",
        "LUAJIT_S390X_TRACE_ABORT_LOG": "1",
        "LUAJIT_S390X_JLOOP_EXIT_LOG": "1",
        "LUAJIT_S390X_STOP_LOG": "1",
        "LUAJIT_S390X_SIDE_FOCUS": "1",
        "LUAJIT_S390X_SIDE_FOCUS_PARENT": str(parent),
        "LUAJIT_S390X_SIDE_FOCUS_EXIT": str(exitno),
        "LUAJIT_S390X_SIDE_REPLAY_PARENT": str(parent),
        "LUAJIT_S390X_SIDE_REPLAY_EXIT": str(exitno),
        "LUAJIT_S390X_JLOOP_EXIT_PARENT": str(parent),
        "LUAJIT_S390X_JLOOP_EXIT_EXIT": str(exitno),
        "LUAJIT_S390X_HOTSIDE_FOCUS": "1",
        "LUAJIT_S390X_HOTSIDE_FOCUS_PARENT": str(parent),
        "LUAJIT_S390X_HOTSIDE_FOCUS_EXIT": str(exitno),
    }
    if extra_env:
        env_items.update(extra_env)
    env_prefix = "env " + " ".join(
        f"{key}={shlex.quote(value)}" for key, value in sorted(env_items.items())
    )
    script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
{env_prefix} ./src/luajit {shlex.quote(f"{remote_tmp}/{name}_exitlog.lua")}
"""
    restamp.run_remote_command(
        host,
        script,
        stdout_path=raw_dir / f"{name}.stdout.log",
        stderr_path=raw_dir / f"{name}.stderr.log",
        label=f"{host} dispatch exit focus {name} {parent}:{exitno}",
    )


def parse_exit_focus_details(path: pathlib.Path) -> dict[str, object]:
    info: dict[str, object] = {
        "phase_counts": {},
        "first_jloop": None,
        "first_phase": None,
        "first_trace_start": None,
        "first_trace_stop": None,
        "first_trace_abort": None,
        "first_hotside_before": None,
        "first_side_enter": None,
        "first_extra_loop_check": None,
        "first_extra_loop_narrow": None,
        "first_side_after_sidecheck": None,
        "seam_attribution": None,
    }
    phase_counts: dict[str, int] = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line:
            continue
        hotside = parse_prefixed_kv_line(line, "S390X_HOTSIDE_FOCUS ")
        if isinstance(hotside, dict) and hotside.get("phase") == "before":
            if info["first_hotside_before"] is None:
                hotside["line"] = line
                info["first_hotside_before"] = hotside
            continue
        side_focus = parse_prefixed_kv_line(line, "S390X_SIDE_FOCUS ")
        if isinstance(side_focus, dict):
            site = side_focus.get("site")
            side_focus["line"] = line
            if site == "enter" and info["first_side_enter"] is None:
                info["first_side_enter"] = side_focus
            elif site == "extra_loop_check" and info["first_extra_loop_check"] is None:
                info["first_extra_loop_check"] = side_focus
            elif site == "extra_loop_narrow" and info["first_extra_loop_narrow"] is None:
                info["first_extra_loop_narrow"] = side_focus
            elif site == "after_sidecheck" and info["first_side_after_sidecheck"] is None:
                info["first_side_after_sidecheck"] = side_focus
            continue
        match = JLOOP_EXIT_RE.match(line)
        if match and info["first_jloop"] is None:
            info["first_jloop"] = {
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
        if match:
            phase = match.group(1)
            phase_counts[phase] = phase_counts.get(phase, 0) + 1
            if info["first_phase"] is None:
                info["first_phase"] = {
                    "phase": phase,
                    "parent": int(match.group(2)),
                    "exit": int(match.group(3)),
                    "trace": int(match.group(4)),
                    "retop": int(match.group(5)),
                    "state": int(match.group(6)),
                    "line": line,
                }
            continue
        match = TRACE_START_RE.match(line)
        if match and info["first_trace_start"] is None:
            info["first_trace_start"] = {
                "trace": int(match.group(1)),
                "parent": int(match.group(2)) if match.group(2) else None,
                "exit": int(match.group(3)) if match.group(3) else None,
                "line": line,
            }
            continue
        match = TRACE_STOP_RE.match(line)
        if match and info["first_trace_stop"] is None:
            info["first_trace_stop"] = {
                "trace": int(match.group(1)),
                "target": match.group(2),
                "line": line,
            }
            continue
        match = TRACE_ABORT_RE.match(line)
        if match and info["first_trace_abort"] is None:
            info["first_trace_abort"] = {
                "trace": int(match.group(1)),
                "reason": match.group(2),
                "line": line,
            }
            continue
    info["phase_counts"] = phase_counts
    first_side_enter = info.get("first_side_enter")
    if isinstance(first_side_enter, dict):
        if (
            first_side_enter.get("op_name") == "MODVN"
            and first_side_enter.get("prevop_name") == "JFORI"
            and first_side_enter.get("startop_name") == "JMP"
        ):
            info["seam_attribution"] = "loop-body-entry-after-JFORI"
        else:
            info["seam_attribution"] = "unclassified-side-entry"
    return info


def focused_runtime_metrics(
    focused_jit_on: dict[str, dict[str, object]],
    focused_joff: dict[str, dict[str, object]],
    trace_counts: dict[str, dict[str, int | str]],
) -> dict[str, dict[str, object]]:
    metrics: dict[str, dict[str, object]] = {}
    for name in LOOP_NAMES:
        jit_record = focused_jit_on.get(f"{name}/hot")
        joff_record = focused_joff.get(f"{name}/hot")
        jit_median = (
            float(jit_record["median_runtime_sec"])
            if isinstance(jit_record, dict) and "median_runtime_sec" in jit_record
            else None
        )
        joff_median = (
            float(joff_record["median_runtime_sec"])
            if isinstance(joff_record, dict) and "median_runtime_sec" in joff_record
            else None
        )
        texits = int(trace_counts[name].get("TEXIT_COUNT", 0))
        work_items = FOCUSED_WORK_ITEMS[name]
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
        gap_sec = (jit_median - joff_median) if jit_median is not None and joff_median is not None else None
        gap_ratio = (jit_median / joff_median) if jit_median is not None and joff_median not in (None, 0.0) else None
        runtime_ns_per_texit = (jit_median * 1.0e9 / texits) if jit_median is not None and texits else None
        if jit_median is None or joff_median is None:
            classification = "unavailable"
        elif steady_exit_share is not None and steady_exit_share >= 0.90 and texits_per_outer_iter >= 0.10:
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
    for name in LOOP_NAMES:
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


def render_summary(
    *,
    output_dir: pathlib.Path,
    host: str,
    candidate: str,
    host_info: dict[str, str],
    repo: str,
    commit: str,
    jit_status: str,
    remote_hashes: dict[str, str | None],
    check_results: dict[str, str],
    dispatch_jit_on_records: list[dict[str, object]],
    dispatch_jit_on_status: dict[str, object],
    dispatch_joff_records: list[dict[str, object]],
    dispatch_joff_status: dict[str, object],
    focused_records: list[dict[str, object]],
    focused_jit_on_status: dict[str, object],
    focused_joff_records: list[dict[str, object]],
    focused_joff_status: dict[str, object],
    trace_counts: dict[str, dict[str, int | str]],
    exit_focus: dict[str, dict[str, object]],
    perf_stats: dict[str, dict[str, object]],
    runtime_metrics: dict[str, dict[str, object]],
    perf_metrics: dict[str, dict[str, object]],
    proof_result: dict[str, object] | None,
    pin_core: int | None,
    samples: int,
    warmup: int,
) -> str:
    dispatch_jit_on = restamp.perf_index(dispatch_jit_on_records)
    dispatch_joff = restamp.perf_index(dispatch_joff_records)
    focused = restamp.perf_index(focused_records)
    focused_joff = restamp.perf_index(focused_joff_records)
    lines = [
        "# Dispatch Truth Pack",
        "",
        f"- Timestamp: `{dt.datetime.now().astimezone().strftime('%Y-%m-%d %H:%M:%S %Z')}`",
        f"- Host label: `{host}`",
        f"- Hostname: `{host_info.get('HOSTNAME_FQDN', '')}`",
        f"- Machine type: `{host_info.get('MACHINE_TYPE', '')}` (`{host_info.get('GENERATION', 'unknown')}`)",
        f"- Model: `{host_info.get('MODEL', '')}`",
        f"- Candidate: `{candidate}`",
        f"- Repo: `{repo}`",
        f"- Commit: `{commit}`",
        f"- Benchmark: `{BENCH_FILE}`",
        f"- Pinned core: `{pin_core if pin_core is not None else 'none'}`",
        f"- Samples: `{samples}`",
        f"- Warmup runs: `{warmup}`",
        f"- `jit.status()`: `{jit_status}`",
        "",
        "## Checks",
        "",
    ]
    for name in LOOP_NAMES:
        lines.append(f"- `{name}`: `{check_results[name]}`")

    lines.extend(["", "## Delivered Remote Hashes", ""])
    for relpath, digest in sorted(remote_hashes.items()):
        lines.append(f"- `{relpath}`: `{digest or 'missing'}`")

    lines.extend(["", "## Dispatch Trace Baseline", ""])
    for name in LOOP_NAMES:
        jit_key = f"{name}/hot"
        jit_record = dispatch_jit_on.get(jit_key)
        joff_record = dispatch_joff.get(jit_key)
        if isinstance(jit_record, dict):
            lines.append(
                f"- `{name}/hot` JIT-on median `{jit_record['median_runtime_sec']:.6f}s`"
            )
        else:
            lines.append(
                f"- `{name}/hot` JIT-on unavailable (`{dispatch_jit_on_status.get('status')}`, rc `{dispatch_jit_on_status.get('returncode')}`)"
            )
        if isinstance(joff_record, dict):
            lines.append(
                f"  - `-joff` median `{joff_record['median_runtime_sec']:.6f}s`"
            )
        else:
            lines.append(
                f"  - `-joff` unavailable (`{dispatch_joff_status.get('status')}`, rc `{dispatch_joff_status.get('returncode')}`)"
            )
    if dispatch_jit_on_status.get("status") != "ok":
        lines.append(
            f"- JIT-on suite failure: `{dispatch_jit_on_status.get('message', 'unknown')}`"
        )
    if dispatch_joff_status.get("status") != "ok":
        lines.append(
            f"- `-joff` suite failure: `{dispatch_joff_status.get('message', 'unknown')}`"
        )

    lines.extend(["", "## Focused Hot Medians", ""])
    for name in LOOP_NAMES:
        record = focused.get(f"{name}/hot")
        joff_record = focused_joff.get(f"{name}/hot")
        if isinstance(record, dict):
            lines.append(
                f"- `{name}/hot` median `{record['median_runtime_sec']:.6f}s`, p95 `{record['p95_runtime_sec']:.6f}s`, samples `{restamp.format_samples(record['samples_sec'])}`"
            )
        else:
            lines.append(f"- `{name}/hot` JIT-on focused median unavailable")
        if isinstance(record, dict) and isinstance(joff_record, dict):
            lines.append(
                f"  - `-joff` median `{joff_record['median_runtime_sec']:.6f}s`, gap `{record['median_runtime_sec'] - joff_record['median_runtime_sec']:+.6f}s`, ratio `{record['median_runtime_sec'] / joff_record['median_runtime_sec']:.2f}x`"
            )
        elif isinstance(joff_record, dict):
            lines.append(
                f"  - `-joff` median `{joff_record['median_runtime_sec']:.6f}s`"
            )
        else:
            lines.append("  - `-joff` focused median unavailable")
    if focused_jit_on_status.get("status") != "ok":
        lines.append(
            f"- JIT-on focused suite failure: `{focused_jit_on_status.get('message', 'unknown')}`"
        )
    if focused_joff_status.get("status") != "ok":
        lines.append(
            f"- `-joff` focused suite failure: `{focused_joff_status.get('message', 'unknown')}`"
        )

    lines.extend(["", "## Trace / Exit Counts After Warmup", ""])
    for name in LOOP_NAMES:
        info = trace_counts[name]
        lines.append(
            f"- `{name}`: trace starts `{info.get('TRACE_START', 0)}`, aborts `{info.get('TRACE_ABORT', 0)}`, texits `{info.get('TEXIT_COUNT', 0)}`"
        )
        lines.append(f"  - trace histogram `{info.get('TRACE_HIST', '(none)')}`")
        lines.append(f"  - texit histogram `{info.get('TEXIT_HIST', '(none)')}`")

    lines.extend(["", "## Focused Exit Attribution", ""])
    for name in LOOP_NAMES:
        focus = exit_focus[name]
        metrics = runtime_metrics[name]
        lines.append(
            f"- `{name}`: steady exit `{metrics['steady_exit_site'] or 'n/a'}`, classification `{metrics['classification']}`"
        )
        seam = focus.get("seam_attribution")
        if isinstance(seam, str) and seam:
            lines.append(f"  - named seam `{seam}`")
        phase_counts = focus.get("phase_counts", {})
        if phase_counts:
            parts = [f"{phase}={count}" for phase, count in sorted(phase_counts.items())]
            lines.append(f"  - focused phases `{', '.join(parts)}`")
        first_hotside = focus.get("first_hotside_before")
        if isinstance(first_hotside, dict):
            lines.append(
                f"  - first hotside before `pc={first_hotside.get('op_name', first_hotside.get('op'))}` `snap={first_hotside.get('snapop_name', first_hotside.get('snapop'))}` `start={first_hotside.get('startop_name', first_hotside.get('startop'))}`"
            )
        first_side = focus.get("first_side_enter")
        if isinstance(first_side, dict):
            lines.append(
                f"  - first side enter `pc={first_side.get('op_name', first_side.get('op'))}` `prev={first_side.get('prevop_name', first_side.get('prevop'))}` `start={first_side.get('startop_name', first_side.get('startop'))}` `parent_start={first_side.get('parent_startop_name', first_side.get('parent_startop'))}`"
            )
        extra_loop_check = focus.get("first_extra_loop_check")
        if isinstance(extra_loop_check, dict):
            lines.append(
                f"  - extra-loop check `prev_is_jfori={extra_loop_check.get('prev_is_jfori')}` `fori_target={extra_loop_check.get('fori_target')}` `target_match={extra_loop_check.get('target_match')}`"
            )
        extra_loop_narrow = focus.get("first_extra_loop_narrow")
        if isinstance(extra_loop_narrow, dict):
            lines.append(
                f"  - extra-loop narrow fires at `pc={extra_loop_narrow.get('op_name', extra_loop_narrow.get('op'))}` `prev={extra_loop_narrow.get('prevop_name', extra_loop_narrow.get('prevop'))}`"
            )
        after_sidecheck = focus.get("first_side_after_sidecheck")
        if isinstance(after_sidecheck, dict):
            lines.append(
                f"  - first side after-sidecheck `pc={after_sidecheck.get('op_name', after_sidecheck.get('op'))}` `start={after_sidecheck.get('startop_name', after_sidecheck.get('startop'))}`"
            )
        first_phase = focus.get("first_phase")
        if isinstance(first_phase, dict):
            lines.append(
                f"  - first phase `{first_phase['phase']}` on trace `{first_phase['trace']}` retop `{first_phase['retop']}`"
            )
        first_jloop = focus.get("first_jloop")
        if isinstance(first_jloop, dict):
            lines.append(
                f"  - first JLOOP exit target `{first_jloop['target']}` exec `{first_jloop['target_exec']}` linktype `{first_jloop['linktype']}` target_startop `{first_jloop['target_startop']}`"
            )
        first_start = focus.get("first_trace_start")
        if isinstance(first_start, dict):
            lines.append(f"  - first trace start `{first_start['line']}`")
        first_stop = focus.get("first_trace_stop")
        if isinstance(first_stop, dict):
            lines.append(f"  - first trace stop `{first_stop['line']}`")
        first_abort = focus.get("first_trace_abort")
        if isinstance(first_abort, dict):
            lines.append(f"  - first trace abort `{first_abort['line']}`")

    lines.extend(["", "## perf stat", ""])
    for name in LOOP_NAMES:
        info = perf_stats[name]
        counters = info.get("counters", {}) if info.get("status") == "ok" else {}
        metrics = perf_metrics[name]
        if info.get("status") != "ok":
            lines.append(f"- `{name}`: unavailable (`{info.get('reason', 'unknown')}`)")
            continue
        lines.append(
            f"- `{name}`: cycles `{counters.get('cycles', 'n/a')}`, instructions `{counters.get('instructions', 'n/a')}`, branches `{counters.get('branches', 'n/a')}`, branch-misses `{counters.get('branch-misses', 'n/a')}`"
        )
        lines.append(
            f"  - CPI `{metrics['cpi']:.3f}`" if isinstance(metrics.get("cpi"), float) else "  - CPI `n/a`"
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

    lines.extend(["", "## Runtime Fallback Attribution", ""])
    for name in LOOP_NAMES:
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

    if proof_result is not None:
        lines.extend(["", "## Loop-Control Proof", ""])
        lines.append(
            f"- stop `{proof_result['stop']}`, outer `{proof_result['outer']}`, extra shims required `{'yes' if proof_result['requires_extra_shims'] else 'no'}`"
        )
        for mode in ("current", "bxle"):
            mode_info = proof_result["modes"][mode]
            lines.append(
                f"- `{mode}` median `{mode_info['median_seconds']:.6f}s`, min `{mode_info['min_seconds']:.6f}s`, max `{mode_info['max_seconds']:.6f}s`, samples `{mode_info['sample_seconds']}`"
            )
        lines.append(
            f"- winner `{proof_result['winner']}`"
            + (
                f", speedup `{proof_result['speedup_vs_current']:.3f}x`"
                if isinstance(proof_result.get("speedup_vs_current"), float)
                else ""
            )
        )

    lines.extend(
        [
            "",
            "## Raw Artifacts",
            "",
            f"- Dispatch baseline JSONL: `{output_dir / 'dispatch-jit-on.jsonl'}`",
            f"- Dispatch `-joff` JSONL: `{output_dir / 'dispatch-joff.jsonl'}`",
            f"- Focused JIT-on JSONL: `{output_dir / 'focused-jit-on.jsonl'}`",
            f"- Focused `-joff` JSONL: `{output_dir / 'focused-joff.jsonl'}`",
            f"- Trace counts: `{output_dir / 'raw' / 'trace-counts'}`",
            f"- Exit focus logs: `{output_dir / 'raw' / 'exit-focus'}`",
            f"- IR + snapshot + mcode dumps: `{output_dir / 'raw' / 'dump'}`",
            f"- perf stat logs: `{output_dir / 'raw' / 'perf-stat'}`",
            f"- Loop-control proof logs: `{output_dir / 'raw' / 'proof'}`",
        ]
    )
    return "\n".join(lines) + "\n"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", choices=restamp.HOST_LABELS, default="kdz")
    parser.add_argument("--candidate", choices=tuple(CANDIDATE_ENVS.keys()), default="retained_baseline")
    parser.add_argument("--repo")
    parser.add_argument("--output-dir", type=pathlib.Path)
    parser.add_argument("--pin-core", type=int, default=restamp.DEFAULT_PIN_CORE)
    parser.add_argument("--samples", type=int, default=restamp.DEFAULT_SAMPLES)
    parser.add_argument("--warmup", type=int, default=restamp.DEFAULT_WARMUP)
    parser.add_argument("--proof-loop-control", action="store_true")
    parser.add_argument("--proof-stop", type=int, default=PROOF_STOP)
    parser.add_argument("--proof-outer", type=int, default=PROOF_OUTER)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    host = args.host
    candidate = args.candidate
    candidate_env = CANDIDATE_ENVS[candidate]
    repo = args.repo or restamp.AUTHORITATIVE_REPOS[host]
    timestamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    output_dir = args.output_dir or DEFAULT_OUTPUT_ROOT / f"{timestamp}-{host}-{candidate}-dispatch-truth-pack"
    raw_dir = output_dir / "raw"
    build_dir = raw_dir / "build"
    check_dir = raw_dir / "checks"
    trace_dir = raw_dir / "trace-counts"
    exit_dir = raw_dir / "exit-focus"
    dump_dir = raw_dir / "dump"
    perf_dir = raw_dir / "perf-stat"
    proof_dir = raw_dir / "proof"

    for directory in (build_dir, check_dir, trace_dir, exit_dir, dump_dir, perf_dir, proof_dir):
        directory.mkdir(parents=True, exist_ok=True)

    remote_tmp = ""
    try:
        commit = restamp.current_commit()
        restamp.sync_tracked_files(host, repo)
        remote_tmp = restamp.prepare_remote_scripts(host)
        prepare_truth_scripts(host, remote_tmp)
        host_info = restamp.collect_host_info(host)
        restamp.build_remote_repo(host, repo, build_dir)
        jit_status = restamp.run_jit_status(host, repo, build_dir)
        remote_hashes = restamp.remote_file_hashes(host, repo, HASH_STAMP_PATHS)

        check_results: dict[str, str] = {}
        for name in LOOP_NAMES:
            check_results[name] = run_check(host, repo, remote_tmp, check_dir, name, candidate_env)

        dispatch_jit_on, dispatch_jit_on_status = run_dispatch_bench(
            host=host,
            repo=repo,
            remote_tmp=remote_tmp,
            raw_dir=build_dir,
            mode_label="dispatch-jit-on",
            pin_core=args.pin_core,
            samples=args.samples,
            warmup=args.warmup,
            joff=False,
            extra_env=candidate_env,
        )
        dispatch_joff, dispatch_joff_status = run_dispatch_bench(
            host=host,
            repo=repo,
            remote_tmp=remote_tmp,
            raw_dir=build_dir,
            mode_label="dispatch-joff",
            pin_core=args.pin_core,
            samples=args.samples,
            warmup=args.warmup,
            joff=True,
            extra_env=candidate_env,
        )
        focused_records, focused_jit_on_status = run_focused_bench(
            host=host,
            repo=repo,
            remote_tmp=remote_tmp,
            raw_dir=build_dir,
            pin_core=args.pin_core,
            samples=args.samples,
            warmup=args.warmup,
            joff=False,
            extra_env=candidate_env,
        )
        focused_joff_records, focused_joff_status = run_focused_bench(
            host=host,
            repo=repo,
            remote_tmp=remote_tmp,
            raw_dir=build_dir,
            pin_core=args.pin_core,
            samples=args.samples,
            warmup=args.warmup,
            joff=True,
            extra_env=candidate_env,
        )

        trace_counts: dict[str, dict[str, int | str]] = {}
        exit_focus: dict[str, dict[str, object]] = {}
        perf_stats: dict[str, dict[str, object]] = {}
        for name in LOOP_NAMES:
            trace_counts[name] = run_trace_count(host, repo, remote_tmp, trace_dir, name, candidate_env)
            hist = parse_histogram(trace_counts[name].get("TEXIT_HIST", ""))
            if hist:
                steady_exit_site = max(hist.items(), key=lambda item: item[1])[0]
                parent, exitno = (int(part) for part in steady_exit_site.split(":", 1))
                run_exit_focus_log(host, repo, remote_tmp, exit_dir, name, parent, exitno, candidate_env)
                exit_focus[name] = parse_exit_focus_details(exit_dir / f"{name}.stderr.log")
            else:
                exit_focus[name] = {"phase_counts": {}}
            run_mcode_dump(host, repo, remote_tmp, dump_dir, name, candidate_env)
            perf_stats[name] = run_perf_stat(host, repo, remote_tmp, perf_dir, name, args.pin_core, candidate_env)

        numeric_seam = exit_focus["numeric_loop"].get("seam_attribution")
        if numeric_seam != CURRENT_SEAM_NAME:
            raise restamp.RestampError(
                f"{host} dispatch seam drift: expected {CURRENT_SEAM_NAME}, got {numeric_seam!r}"
            )

        proof_result = None
        if args.proof_loop_control:
            proof_result = run_loop_control_proof(
                host=host,
                remote_tmp=remote_tmp,
                raw_dir=proof_dir,
                pin_core=args.pin_core,
                samples=args.samples,
                warmup=args.warmup,
                outer=args.proof_outer,
                stop=args.proof_stop,
            )

        runtime_metrics = focused_runtime_metrics(
            restamp.perf_index(focused_records),
            restamp.perf_index(focused_joff_records),
            trace_counts,
        )
        perf_metrics = derive_perf_metrics(trace_counts, perf_stats)

        metadata = {
            "timestamp": dt.datetime.now(dt.timezone.utc).isoformat(),
            "host": host,
            "candidate": candidate,
            "candidate_env": candidate_env,
            "host_info": host_info,
            "repo": repo,
            "commit": commit,
            "benchmark": BENCH_FILE,
            "sample_count": args.samples,
            "warmup_runs": args.warmup,
            "pin_core": args.pin_core,
            "dispatch_suite_status": {
                "jit_on": dispatch_jit_on_status,
                "joff": dispatch_joff_status,
            },
            "focused_suite_status": {
                "jit_on": focused_jit_on_status,
                "joff": focused_joff_status,
            },
            "delivered_remote_hashes": remote_hashes,
            "focused_loops": list(LOOP_NAMES),
            "runtime_fallback_metrics": runtime_metrics,
            "derived_perf_metrics": perf_metrics,
            "proof_loop_control": proof_result,
        }
        write_json(output_dir / "metadata.json", metadata)
        summary = render_summary(
            output_dir=output_dir,
            host=host,
            candidate=candidate,
            host_info=host_info,
            repo=repo,
            commit=commit,
            jit_status=jit_status,
            remote_hashes=remote_hashes,
            check_results=check_results,
            dispatch_jit_on_records=dispatch_jit_on,
            dispatch_jit_on_status=dispatch_jit_on_status,
            dispatch_joff_records=dispatch_joff,
            dispatch_joff_status=dispatch_joff_status,
            focused_records=focused_records,
            focused_jit_on_status=focused_jit_on_status,
            focused_joff_records=focused_joff_records,
            focused_joff_status=focused_joff_status,
            trace_counts=trace_counts,
            exit_focus=exit_focus,
            perf_stats=perf_stats,
            runtime_metrics=runtime_metrics,
            perf_metrics=perf_metrics,
            proof_result=proof_result,
            pin_core=args.pin_core,
            samples=args.samples,
            warmup=args.warmup,
        )
        write_text(output_dir / "summary.md", summary)
        return 0
    finally:
        if remote_tmp:
            restamp.cleanup_remote_scripts(host, remote_tmp)


if __name__ == "__main__":
    raise SystemExit(main())
