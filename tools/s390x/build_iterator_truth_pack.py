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
MICRO_NAMES = ("hash_value", "hash_key", "array_value")

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
) -> list[dict[str, object]]:
    remote_json = f"{remote_tmp}/focused-jit-on.jsonl"
    script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
rm -f {shlex.quote(remote_json)}
{restamp.remote_env_prefix(jsonl_path=remote_json, samples=samples, warmup=warmup)} {f"taskset -c {pin_core} " if pin_core is not None else ""}./src/luajit {shlex.quote(f"{remote_tmp}/focused_bench.lua")}
"""
    restamp.run_remote_command(
        host,
        script,
        stdout_path=raw_dir / "focused-jit-on.stdout.log",
        stderr_path=raw_dir / "focused-jit-on.stderr.log",
        label=f"{host} focused hot medians",
    )
    json_text = restamp.fetch_remote_file(host, remote_json)
    local_json = raw_dir.parent / "focused-jit-on.jsonl"
    write_text(local_json, json_text)
    return restamp.parse_jsonl_records(local_json)


def run_mcode_dump(host: str, repo: str, remote_tmp: str, raw_dir: pathlib.Path, name: str) -> None:
    script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
./src/luajit -jdump=im {shlex.quote(f"{remote_tmp}/{name}.lua")}
"""
    restamp.run_remote_command(
        host,
        script,
        stdout_path=raw_dir / f"{name}.stdout.log",
        stderr_path=raw_dir / f"{name}.stderr.log",
        label=f"{host} mcode dump {name}",
    )


def run_trace_count(host: str, repo: str, remote_tmp: str, raw_dir: pathlib.Path, name: str) -> dict[str, int | str]:
    script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
./src/luajit {shlex.quote(f"{remote_tmp}/{name}_trace.lua")}
"""
    proc = restamp.run_remote_command(
        host,
        script,
        stdout_path=raw_dir / f"{name}.stdout.log",
        stderr_path=raw_dir / f"{name}.stderr.log",
        label=f"{host} trace count {name}",
    )
    return parse_key_value_lines(proc.stdout)


def run_perf_stat(host: str, repo: str, remote_tmp: str, raw_dir: pathlib.Path, name: str, pin_core: int | None) -> dict[str, object]:
    taskset = f"taskset -c {pin_core} " if pin_core is not None else ""
    script = f"""
set -euo pipefail
cd {shlex.quote(repo)}
export LUA_PATH="./src/?.lua;./src/jit/?.lua;;"
if ! command -v perf >/dev/null 2>&1; then
  echo "PERF_STATUS unavailable:perf-not-found"
  exit 0
fi
perf stat -x, -e cycles,instructions,branches,branch-misses -- {taskset}./src/luajit {shlex.quote(f"{remote_tmp}/{name}_perf.lua")}
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
    trace_counts: dict[str, dict[str, int | str]],
    perf_stats: dict[str, dict[str, object]],
    pin_core: int | None,
    samples: int,
    warmup: int,
    freeze_branch: str,
) -> str:
    iterator_jit_on = restamp.perf_index(iterator_jit_on_records)
    iterator_joff = restamp.perf_index(iterator_joff_records)
    focused = restamp.perf_index(focused_records)
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
        lines.append(
            f"- `{name}/hot` median `{record['median_runtime_sec']:.6f}s`, "
            f"p95 `{record['p95_runtime_sec']:.6f}s`, samples `{restamp.format_samples(record['samples_sec'])}`"
        )

    lines.extend(["", "## Trace / Exit Counts After Warmup", ""])
    lines.extend(trace_lines)
    lines.append(f"- Decision: `{decision}`")

    lines.extend(["", "## perf stat", ""])
    for name in MICRO_NAMES:
        info = perf_stats[name]
        if info["status"] != "ok":
            lines.append(f"- `{name}`: unavailable (`{info['reason']}`)")
            continue
        counters = info["counters"]
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
            f"- Iterator baseline JSONL: `{output_dir / 'iterator-jit-on.jsonl'}`",
            f"- Iterator `-joff` JSONL: `{output_dir / 'iterator-joff.jsonl'}`",
            f"- Focused medians JSONL: `{output_dir / 'focused-jit-on.jsonl'}`",
            f"- Trace counts: `{output_dir / 'raw' / 'trace-counts'}`",
            f"- Owner logs: `{output_dir / 'raw' / 'owner'}`",
            f"- IR + mcode dumps: `{output_dir / 'raw' / 'dump'}`",
            f"- perf stat logs: `{output_dir / 'raw' / 'perf-stat'}`",
        ]
    )
    return "\n".join(lines) + "\n"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build a focused iterator truth pack from the frozen baseline.")
    parser.add_argument("--host", choices=restamp.HOST_LABELS, required=True)
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
    repo = args.repo or restamp.AUTHORITATIVE_REPOS[host]
    timestamp = dt.datetime.now().astimezone().strftime("%Y%m%d-%H%M%S")
    output_dir = pathlib.Path(args.output_dir).expanduser().resolve() if args.output_dir else (DEFAULT_OUTPUT_ROOT / f"{timestamp}-{host}-frozen-baseline").resolve()
    raw_dir = output_dir / "raw"
    owner_dir = raw_dir / "owner"
    dump_dir = raw_dir / "dump"
    trace_dir = raw_dir / "trace-counts"
    perf_dir = raw_dir / "perf-stat"
    output_dir.mkdir(parents=True, exist_ok=True)
    owner_dir.mkdir(parents=True, exist_ok=True)
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
        oneshot_results = restamp.run_oneshot_checks(host, repo, remote_tmp, raw_dir)
        micro_results = {
            "hash_value": restamp.run_micro(host, repo, remote_tmp, raw_dir, "hash_value"),
            "hash_key": restamp.run_micro(host, repo, remote_tmp, raw_dir, "hash_key"),
            "array_value": restamp.run_micro(host, repo, remote_tmp, raw_dir, "array_value"),
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
        )

        trace_counts: dict[str, dict[str, int | str]] = {}
        perf_stats: dict[str, dict[str, object]] = {}
        for name in MICRO_NAMES:
            restamp.run_owner_logs(host, repo, remote_tmp, owner_dir, name)
            run_mcode_dump(host, repo, remote_tmp, dump_dir, name)
            trace_counts[name] = run_trace_count(host, repo, remote_tmp, trace_dir, name)
            perf_stats[name] = run_perf_stat(host, repo, remote_tmp, perf_dir, name, args.pin_core)

        metadata = {
            "timestamp": dt.datetime.now(dt.timezone.utc).isoformat(),
            "git_commit": commit,
            "freeze_branch": args.freeze_branch,
            "host_label": host,
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
            trace_counts=trace_counts,
            perf_stats=perf_stats,
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
