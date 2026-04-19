#!/usr/bin/env python3
"""Native s390x LuaJIT bring-up driver.

This driver keeps all repo edits local, syncs the workspace to a native
remote s390x host, runs stage-specific build/test flows there, and copies the
artifacts back to the local repo under artifacts/s390x/<run-id>/.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import pathlib
import platform
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import textwrap
import time
import traceback
from dataclasses import asdict, dataclass
from typing import Dict, Iterable, List, Optional


ROOT = pathlib.Path(__file__).resolve().parents[2]
ARTIFACTS_ROOT = ROOT / "artifacts" / "s390x"
REMOTE_BASE = "/root/luajit2-s390x"
REMOTE_REPO_NAME = "repo"
REMOTE_ARTIFACTS_NAME = "artifacts"
HOSTS = ("kdz", "kdz1", "zkd0")
LATEST_LINK = ARTIFACTS_ROOT / "latest"

PURE_LUA_T_FILES = [
    "cli-errors.t",
    "isempty.t",
    "nkeys.t",
    "table-clone.t",
    "exdata.t",
    "exdata2.t",
    "prngstate.t",
    "isarr-interp.t",
]

JIT_T_FILES = {
    "jit_core": ["isarr-jit.t"],
    "jit_loops": ["iter.t"],
}

JIT_CORE_LUA_FILES = [
    "tests/s390x/jit_core/isarray_root_loop.lua",
    "tests/s390x/jit_core/bitops_trace.lua",
    "tests/s390x/jit_core/ffi_cdata_trace.lua",
    "tests/s390x/jit_core/mod_trace.lua",
    "tests/s390x/jit_core/numeric_helpers.lua",
    "tests/s390x/jit_core/profile_loop.lua",
    "tests/s390x/jit_core/trace_event_postloop.lua",
]

JIT_CORE_FFI_LUA_FILES = {
    "tests/s390x/jit_core/ffi_call_trace.lua",
    "tests/s390x/jit_core/ffi_cdata_trace.lua",
    "tests/s390x/jit_core/ffi_ptr_call_trace.lua",
    "tests/s390x/jit_core/trace_event_postloop.lua",
}

JIT_LOOPS_LUA_FILES = [
    "tests/s390x/jit_loops/compiled_vararg.lua",
    "tests/s390x/jit_loops/explicit_next.lua",
    "tests/s390x/jit_loops/vararg_correctness.lua",
    "tests/s390x/jit_loops/vararg_trace.lua",
]

TRACE_TOOLS_LUA_FILES = [
    "tests/s390x/trace_tools/trace_attach_root.lua",
    "tests/s390x/trace_tools/texit_observer.lua",
    "tests/s390x/trace_tools/traceinfo_lifecycle.lua",
    "tests/s390x/trace_tools/jit_module_loading.lua",
]

CALLBACK_FFI_LUA_FILES = {
    "tests/s390x/callbacks/run.lua",
    "tests/s390x/callbacks/stress.lua",
}

JIT_BE_FFI_LUA_FILES = {
    "tests/s390x/jit_be/mixed_width_ffi.lua",
}

SOAK_FFI_LUA_FILES = {
    "tests/s390x/soak/mixed_stress.lua",
}

PERF_FFI_LUA_FILES = {
    "tests/s390x/perf/ffi_calls.lua",
    "tests/s390x/perf/ffi_calls_static_stop.lua",
    "tests/s390x/perf/ffi_cdata.lua",
    "tests/s390x/perf/ffi_fixed_call_pressure.lua",
    "tests/s390x/perf/ffi_fixed_struct_calls.lua",
    "tests/s390x/perf/mixed_ffi.lua",
}

PERF_TOP_CROSS_ARCH_COUNT = 5
PERF_RETAINED_ENV = {}

PERF_FAMILY_METADATA = {
    "dispatch_trace": {
        "default_gate": True,
        "promotion_order": 0,
        "status": "baseline-gate",
        "priority": "hotspot-active",
        "notes": "Primary release-stable dispatch, side-exit, and hotexit perf gate.",
    },
    "iterator_table": {
        "default_gate": False,
        "promotion_order": 1,
        "status": "probe-only",
        "priority": "promote-next",
        "notes": "Iterator/pairs hotspot under active remediation; use --perf-family iterator_table for focused restamps.",
    },
    "bitops_mix": {
        "default_gate": False,
        "promotion_order": 2,
        "status": "probe-only",
        "priority": "tracked-follow-up",
        "notes": "Bitops/tobit perf probe kept out of the default lane until release-stable.",
    },
    "be_helpers_localized": {
        "default_gate": False,
        "promotion_order": 3,
        "status": "probe-only",
        "priority": "tracked-follow-up",
        "notes": "Localized big-endian helper and tobit lowering probe.",
    },
    "vararg_paths": {
        "default_gate": False,
        "promotion_order": 4,
        "status": "candidate",
        "priority": "promote-after-native-rerank",
        "notes": "Vararg select/return paths now compile without retained vararg blacklists; keep as focused native rerank family before default promotion.",
    },
    "ffi_calls": {
        "default_gate": False,
        "promotion_order": 5,
        "status": "probe-only",
        "priority": "tracked-follow-up",
        "notes": "Traced direct and stored FFI call probe.",
    },
    "ffi_calls_static_stop": {
        "default_gate": False,
        "promotion_order": 6,
        "status": "probe-only",
        "priority": "tracked-follow-up",
        "notes": "Static-stop direct and stored FFI call probe.",
    },
    "ffi_cdata": {
        "default_gate": False,
        "promotion_order": 7,
        "status": "probe-only",
        "priority": "tracked-follow-up",
        "notes": "Cdata load/store perf probe.",
    },
    "ffi_fixed_call_pressure": {
        "default_gate": False,
        "promotion_order": 8,
        "status": "probe-only",
        "priority": "tracked-follow-up",
        "notes": "High-pressure fixed FFI call argument/return probe.",
    },
    "ffi_fixed_struct_calls": {
        "default_gate": False,
        "promotion_order": 9,
        "status": "probe-only",
        "priority": "tracked-follow-up",
        "notes": "Fixed struct-by-value FFI call ABI probe.",
    },
    "be_helpers": {
        "default_gate": False,
        "promotion_order": 10,
        "status": "probe-only",
        "priority": "tracked-follow-up",
        "notes": "Big-endian helper and pack/unpack probe.",
    },
    "int_add_phi_only": {
        "default_gate": False,
        "promotion_order": 11,
        "status": "probe-only",
        "priority": "tracked-follow-up",
        "notes": "Integer ADD PHI recurrence codegen probe.",
    },
    "large_immediates": {
        "default_gate": False,
        "promotion_order": 12,
        "status": "probe-only",
        "priority": "tracked-follow-up",
        "notes": "Large-immediate compare/add/sub/addressing codegen probe.",
    },
    "logic_add_phi_noboundary": {
        "default_gate": False,
        "promotion_order": 13,
        "status": "probe-only",
        "priority": "tracked-follow-up",
        "notes": "Low-bit demanded PHI recurrence probe without boundary churn.",
    },
    "logical_chain_tail_add": {
        "default_gate": False,
        "promotion_order": 14,
        "status": "probe-only",
        "priority": "tracked-follow-up",
        "notes": "Logical chain tail-add lowering probe.",
    },
    "logical_chain_tail_store": {
        "default_gate": False,
        "promotion_order": 15,
        "status": "probe-only",
        "priority": "tracked-follow-up",
        "notes": "Logical chain tail-store lowering probe.",
    },
    "lower_frame_same_callsite": {
        "default_gate": False,
        "promotion_order": 16,
        "status": "probe-only",
        "priority": "tracked-follow-up",
        "notes": "Lower-frame same-callsite recorder/backend probe.",
    },
    "mixed_noffi": {
        "default_gate": False,
        "promotion_order": 17,
        "status": "probe-only",
        "priority": "tracked-follow-up",
        "notes": "Mixed JIT-heavy workload without FFI.",
    },
    "mixed_ffi": {
        "default_gate": False,
        "promotion_order": 18,
        "status": "probe-only",
        "priority": "tracked-follow-up",
        "notes": "Mixed Lua + FFI workload.",
    },
    "numeric_ops": {
        "default_gate": False,
        "promotion_order": 19,
        "status": "probe-only",
        "priority": "tracked-follow-up",
        "notes": "Numeric helper, division, modulo, sqrt, and min/max lowering probe.",
    },
    "promotion_core_static_stop": {
        "default_gate": False,
        "promotion_order": 20,
        "status": "probe-only",
        "priority": "tracked-follow-up",
        "notes": "Promotion-core static-stop route-around reduction probe.",
    },
    "route_around_reducers": {
        "default_gate": False,
        "promotion_order": 21,
        "status": "probe-only",
        "priority": "tracked-follow-up",
        "notes": "Route-around reducer truth-pack perf probe.",
    },
    "string_heavy": {
        "default_gate": False,
        "promotion_order": 22,
        "status": "probe-only",
        "priority": "tracked-follow-up",
        "notes": "String compare/search/scan and string-key lookup workload.",
    },
}

PERF_BENCH_LUA_FILE_BY_FAMILY = {
    family: f"tests/s390x/perf/{family}.lua"
    for family in PERF_FAMILY_METADATA
}
PERF_BENCH_LUA_FILE_BY_FAMILY["route_around_reducers"] = "tests/s390x/perf/route_around_reducers.lua"

PERF_BENCH_LUA_FILES = [
    PERF_BENCH_LUA_FILE_BY_FAMILY[family]
    for family, meta in sorted(
        PERF_FAMILY_METADATA.items(),
        key=lambda item: (item[1]["promotion_order"], item[0]),
    )
    if meta["default_gate"]
]

PERF_STAT_BENCH_LUA_FILES = [
    PERF_BENCH_LUA_FILE_BY_FAMILY["dispatch_trace"],
]

SUITES = {
    "smoke": "Build and runtime smoke checks",
    "pure_lua": "Interpreter-focused Lua and non-JIT regression coverage",
    "ffi_abi": "Generated ABI oracle coverage for outgoing FFI calls",
    "callbacks": "FFI callback and unwind regression coverage",
    "jit_core": "Trace creation, exits, and basic JIT behavior",
    "jit_be": "Big-endian JIT-sensitive regression coverage",
    "jit_loops": "Loop tracing, iterator, and vararg JIT coverage",
    "trace_tools": "Trace observer, traceinfo, and jit.v/jit.dump tooling coverage",
    "soak": "Long-running mixed stress coverage",
    "coverage_audit": "Static inventory of s390x opcode, IR, VM, and helper coverage",
    "downstream": "Native OpenResty and Kong downstream runtime gates",
    "perf_bench": "Structured performance benchmarks with JSON metrics",
}

STAGE_DEFAULT_SUITES = {
    "contract": ["smoke"],
    "interp": ["smoke", "pure_lua"],
    "ffi-call": ["smoke", "pure_lua", "ffi_abi"],
    "callback-unwind": ["smoke", "pure_lua", "ffi_abi", "callbacks"],
    "jit-bringup": ["smoke", "jit_core", "jit_loops"],
    "jit-correctness": ["smoke", "jit_core", "jit_loops", "trace_tools", "jit_be", "soak"],
    "matrix": ["smoke", "pure_lua", "ffi_abi", "callbacks", "jit_core", "jit_loops", "trace_tools", "jit_be"],
    "perf": ["smoke", "soak", "perf_bench"],
    "closure": [
        "smoke",
        "pure_lua",
        "ffi_abi",
        "callbacks",
        "jit_core",
        "jit_loops",
        "trace_tools",
        "jit_be",
        "soak",
        "coverage_audit",
        "downstream",
        "perf_bench",
    ],
}

STAGE_ORDER = list(STAGE_DEFAULT_SUITES)
PRE_JIT_STAGES = {"contract", "interp", "ffi-call", "callback-unwind"}
LOCAL_SUITES = {"coverage_audit", "downstream"}

RSYNC_EXCLUDES = [
    ".git",
    ".DS_Store",
    ".AppleDouble",
    "._*",
    "__pycache__",
    "artifacts/s390x",
    "*.pyc",
    "*.pyo",
    "*.o",
    "*.obj",
    "*.a",
    "*.so",
    "*.dylib",
    "*.dll",
    "*.gcda",
    "*.gcno",
    "src/luajit",
    "src/libluajit.so",
    "src/libluajit.a",
    "src/host/buildvm",
    "src/host/minilua",
    "src/lj_vm.S",
    "src/host/buildvm_arch.h",
]

CONTRACT_FILE = ROOT / "docs" / "s390x" / "contract.md"
CONTRACT_MARKERS = {
    "base_register": "- [x] BASE register contract: interpreter and JIT use `r13` as `BASE`.",
    "fp_registers": "- [x] FP argument register contract: outbound FFI arguments use `f0`, `f2`, `f4`, and `f6`.",
    "exit_state_width": "- [x] Exit-state contract: `ExitState.gpr[]` must store full pointer-width `intptr_t` values.",
    "unwind_ra": "- [x] Unwind return-address contract: the architectural return-address register is `r14`, and the DWARF CFA RA register number is `14`.",
    "jit_constant_tables": "- [x] Shared JIT constant-table contract: s390x must be wired into the common VM exit constant tables before `LJ_ARCH_NOJIT` is removed.",
}
CONTRACT_FILE_MARKERS = [
    "`src/lj_arch.h`",
    "`src/lj_target_s390x.h`",
    "`src/vm_s390x.dasc`",
    "`src/lj_ccall.h`",
    "`src/lj_ccall.c`",
    "`src/lj_ccallback.c`",
    "`src/lj_trace.c`",
    "`src/lj_jit.h`",
    "`src/lj_err.c`",
    "`src/lj_frame.h`",
    "`src/host/buildvm.c`",
    "`src/host/buildvm_asm.c`",
    "`dynasm/dasm_s390x.lua`",
    "`dynasm/dasm_s390x.h`",
    "`src/jit/dis_s390x.lua`",
]


class DriverError(RuntimeError):
    """Driver failure."""


@dataclass
class Variant:
    compiler: str
    mode: str
    jit: str
    ffi: str = "on"
    build_style: str = "mixed"
    tuning: str = "baseline"

    def key(self) -> str:
        return "-".join(
            [
                self.compiler,
                self.mode,
                f"jit{self.jit}",
                f"ffi{self.ffi}",
                self.build_style,
                self.tuning,
            ]
        )


@dataclass
class StepResult:
    step: str
    suite: str
    host: str
    variant: Dict[str, str]
    exit_code: int
    duration_sec: float
    remote_step_dir: str
    local_step_dir: str
    command: str


class CommandLogger:
    def __init__(self, path: pathlib.Path):
        self.path = path
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._fh = self.path.open("a", encoding="utf-8")

    def write(
        self,
        *,
        host: str,
        cwd: str,
        argv: List[str],
        exit_code: int,
        duration_sec: float,
        env_diff: Optional[Dict[str, str]] = None,
        artifacts: Optional[Dict[str, str]] = None,
    ) -> None:
        record = {
            "timestamp": dt.datetime.now(dt.timezone.utc).isoformat(),
            "host": host,
            "cwd": cwd,
            "argv": argv,
            "command": " ".join(shlex.quote(part) for part in argv),
            "exit_code": exit_code,
            "duration_sec": round(duration_sec, 6),
            "env_diff": env_diff or {},
            "artifacts": artifacts or {},
        }
        self._fh.write(json.dumps(record, sort_keys=True) + "\n")
        self._fh.flush()

    def close(self) -> None:
        self._fh.close()


class Context:
    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.run_id, self.local_run_dir = self._init_run_dir()
        self.local_remote_dir = self.local_run_dir / "remote"
        self.local_binaries_dir = self.local_run_dir / "binaries"
        self.local_metadata_dir = self.local_run_dir / "metadata"
        self.local_perf_dir = self.local_run_dir / "perf"
        self.local_coverage_dir = self.local_run_dir / "coverage"
        self.local_downstream_dir = self.local_run_dir / "downstream"
        self.lock_path = self.local_run_dir / ".lock"
        self.lock_fd: Optional[int] = None
        self._acquire_local_lock()
        self.local_remote_dir.mkdir(parents=True, exist_ok=self.args.resume)
        self.local_binaries_dir.mkdir(parents=True, exist_ok=self.args.resume)
        self.local_metadata_dir.mkdir(parents=True, exist_ok=self.args.resume)
        self.local_perf_dir.mkdir(parents=True, exist_ok=self.args.resume)
        self.local_coverage_dir.mkdir(parents=True, exist_ok=self.args.resume)
        self.local_downstream_dir.mkdir(parents=True, exist_ok=self.args.resume)
        self.logger = CommandLogger(self.local_run_dir / "commands.ndjson")
        self.host: Optional[str] = None
        self.primary_host: Optional[str] = None
        self.failover_from: Optional[str] = None
        self.remote_run_root = f"{REMOTE_BASE}/{self.run_id}"
        self.remote_repo_root = f"{self.remote_run_root}/{REMOTE_REPO_NAME}"
        self.remote_artifacts_root = f"{self.remote_run_root}/{REMOTE_ARTIFACTS_NAME}"
        self.results: List[StepResult] = []
        self.failures: List[dict] = []
        self.perf_records: List[dict] = []
        self.perf_comparisons: List[dict] = []
        self.perf_notes: List[str] = []
        self.manifest_path = self.local_run_dir / "manifest.json"
        self.manifest = {
            "run_id": self.run_id,
            "stage": self.args.stage,
            "suite": self.args.suite,
            "compiler": self.args.compiler,
            "mode": self.args.mode,
            "jit": self.args.jit,
            "resume": self.args.resume,
            "keep_remote": self.args.keep_remote,
            "started_at": dt.datetime.now(dt.timezone.utc).isoformat(),
            "root": str(ROOT),
            "host": None,
            "primary_host": None,
            "failover_from": None,
            "results": [],
            "failures": [],
            "perf_records": [],
            "perf_comparisons": [],
            "perf_notes": [],
        }

    @staticmethod
    def _auto_run_id() -> str:
        now = dt.datetime.now(dt.timezone.utc)
        return f"{now.strftime('%Y%m%dT%H%M%S.%fZ')}-p{os.getpid()}"

    def _init_run_dir(self) -> tuple[str, pathlib.Path]:
        ARTIFACTS_ROOT.mkdir(parents=True, exist_ok=True)
        if self.args.run_id == "auto":
            return self._create_unique_auto_run_dir()

        run_dir = ARTIFACTS_ROOT / self.args.run_id
        if self.args.resume:
            if not run_dir.exists():
                raise DriverError(f"cannot resume missing run directory: {run_dir}")
            if not (run_dir / "manifest.json").exists():
                raise DriverError(f"cannot resume without manifest: {run_dir / 'manifest.json'}")
            return self.args.run_id, run_dir

        try:
            run_dir.mkdir(parents=True, exist_ok=False)
        except FileExistsError as exc:
            raise DriverError(
                f"run-id already exists: {self.args.run_id}; use --resume to reuse an existing run"
            ) from exc
        return self.args.run_id, run_dir

    def _create_unique_auto_run_dir(self) -> tuple[str, pathlib.Path]:
        for _ in range(32):
            run_id = self._auto_run_id()
            run_dir = ARTIFACTS_ROOT / run_id
            try:
                run_dir.mkdir(parents=True, exist_ok=False)
                return run_id, run_dir
            except FileExistsError:
                time.sleep(0.001)
        raise DriverError("unable to allocate a unique auto run-id after repeated collisions")

    def _acquire_local_lock(self) -> None:
        flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
        try:
            self.lock_fd = os.open(self.lock_path, flags, 0o644)
        except FileExistsError as exc:
            raise DriverError(f"run directory is already locked: {self.local_run_dir}") from exc
        lock_payload = {
            "pid": os.getpid(),
            "run_id": self.run_id,
            "started_at": dt.datetime.now(dt.timezone.utc).isoformat(),
            "resume": self.args.resume,
        }
        os.write(self.lock_fd, (json.dumps(lock_payload, sort_keys=True) + "\n").encode("utf-8"))

    def release_local_lock(self) -> None:
        if self.lock_fd is not None:
            os.close(self.lock_fd)
            self.lock_fd = None
        try:
            self.lock_path.unlink()
        except FileNotFoundError:
            pass

    def save_manifest(self) -> None:
        self.manifest["host"] = self.host
        self.manifest["primary_host"] = self.primary_host
        self.manifest["failover_from"] = self.failover_from
        self.manifest["results"] = [asdict(result) for result in self.results]
        self.manifest["failures"] = self.failures
        self.manifest["perf_records"] = self.perf_records
        self.manifest["perf_comparisons"] = self.perf_comparisons
        self.manifest["perf_notes"] = self.perf_notes
        self.manifest_path.write_text(json.dumps(self.manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run the native s390x bring-up loop.")
    parser.add_argument("--host", choices=("auto", "kdz", "kdz1", "zkd0"), default="auto")
    parser.add_argument("--stage", choices=tuple(STAGE_ORDER), required=True)
    parser.add_argument("--suite", choices=("all", *SUITES), default="all")
    parser.add_argument("--compiler", choices=("gcc", "clang", "both"), default="gcc")
    parser.add_argument("--mode", choices=("debug", "release", "both"), default="debug")
    parser.add_argument("--jit", choices=("off", "on", "both"), default="off")
    parser.add_argument("--run-id", default="auto")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--keep-remote", action="store_true")
    parser.add_argument(
        "--perf-family",
        action="append",
        choices=("all", *tuple(sorted(PERF_FAMILY_METADATA))),
        default=[],
        help="Run only the selected perf family or families instead of the default gate set; use 'all' for the full perf matrix.",
    )
    return parser.parse_args()


def shell_join(parts: Iterable[str]) -> str:
    return " ".join(shlex.quote(part) for part in parts)


def write_text(path: pathlib.Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def run_local(
    ctx: Context,
    argv: List[str],
    *,
    cwd: Optional[pathlib.Path] = None,
    env: Optional[Dict[str, str]] = None,
    check: bool = False,
    echo_output: bool = True,
    artifacts: Optional[Dict[str, str]] = None,
) -> subprocess.CompletedProcess:
    start = time.time()
    proc = subprocess.run(
        argv,
        cwd=str(cwd or ROOT),
        env={**os.environ, **(env or {})},
        text=True,
        capture_output=True,
    )
    duration = time.time() - start
    ctx.logger.write(
        host="local",
        cwd=str(cwd or ROOT),
        argv=argv,
        exit_code=proc.returncode,
        duration_sec=duration,
        env_diff=env,
        artifacts=artifacts,
    )
    if echo_output and proc.stdout:
        sys.stdout.write(proc.stdout)
    if echo_output and proc.stderr:
        sys.stderr.write(proc.stderr)
    if check and proc.returncode != 0:
        raise DriverError(f"local command failed: {' '.join(argv)}")
    return proc


def run_local_shell(
    ctx: Context,
    command: str,
    *,
    cwd: Optional[pathlib.Path] = None,
    check: bool = False,
    echo_output: bool = True,
    env: Optional[Dict[str, str]] = None,
    artifacts: Optional[Dict[str, str]] = None,
) -> subprocess.CompletedProcess:
    argv = ["/bin/bash", "-lc", command]
    start = time.time()
    proc = subprocess.run(
        argv,
        cwd=str(cwd or ROOT),
        env={**os.environ, **(env or {})},
        text=True,
        capture_output=True,
    )
    duration = time.time() - start
    ctx.logger.write(
        host="local",
        cwd=str(cwd or ROOT),
        argv=argv,
        exit_code=proc.returncode,
        duration_sec=duration,
        env_diff=env,
        artifacts=artifacts,
    )
    if echo_output and proc.stdout:
        sys.stdout.write(proc.stdout)
    if echo_output and proc.stderr:
        sys.stderr.write(proc.stderr)
    if check and proc.returncode != 0:
        raise DriverError(f"local shell command failed: {command}")
    return proc


def run_ssh(
    ctx: Context,
    host: str,
    command: str,
    *,
    check: bool = False,
    artifacts: Optional[Dict[str, str]] = None,
) -> subprocess.CompletedProcess:
    argv = ["ssh", "-o", "BatchMode=yes", host, f"bash -lc {shlex.quote(command)}"]
    start = time.time()
    proc = subprocess.run(argv, cwd=str(ROOT), text=True, capture_output=True)
    duration = time.time() - start
    ctx.logger.write(
        host=host,
        cwd="~",
        argv=argv,
        exit_code=proc.returncode,
        duration_sec=duration,
        artifacts=artifacts,
    )
    if proc.stdout:
        sys.stdout.write(proc.stdout)
    if proc.stderr:
        sys.stderr.write(proc.stderr)
    if check and proc.returncode != 0:
        raise DriverError(f"ssh command failed on {host}: {command}")
    return proc


def choose_host(ctx: Context) -> str:
    requested = ctx.args.host
    if requested != "auto":
        proc = run_ssh(ctx, requested, "true")
        if proc.returncode != 0:
            raise DriverError(f"requested host {requested} is unavailable")
        ctx.primary_host = requested
        ctx.host = requested
        return requested

    available: List[str] = []
    for host in HOSTS:
        proc = run_ssh(ctx, host, "true")
        if proc.returncode == 0:
            available.append(host)
    if not available:
        raise DriverError("no configured s390x host is reachable")

    ctx.primary_host = HOSTS[0]
    ctx.host = available[0]
    if ctx.host != ctx.primary_host:
        ctx.failover_from = ctx.primary_host
    return ctx.host


def ensure_remote_dirs(ctx: Context, host: str) -> None:
    command = shell_join(
        [
            "mkdir",
            "-p",
            ctx.remote_run_root,
            ctx.remote_repo_root,
            ctx.remote_artifacts_root,
        ]
    )
    run_ssh(ctx, host, command, check=True)


def sync_repo(ctx: Context, host: str) -> None:
    remote_prep = (
        f"rm -rf {shlex.quote(ctx.remote_repo_root)}/* "
        f"{shlex.quote(ctx.remote_repo_root)}/.[!.]* "
        f"{shlex.quote(ctx.remote_repo_root)}/..?* 2>/dev/null || true"
    )
    run_ssh(ctx, host, remote_prep, check=True)
    start = time.time()
    file_list = subprocess.run(
        ["git", "ls-files", "-z"],
        cwd=str(ROOT),
        text=False,
        capture_output=True,
    )
    duration = time.time() - start
    ctx.logger.write(
        host="local",
        cwd=str(ROOT),
        argv=["git", "ls-files", "-z"],
        exit_code=file_list.returncode,
        duration_sec=duration,
        artifacts={"sync_mode": "git-ls-files"},
    )
    if file_list.stderr:
        sys.stderr.write(file_list.stderr.decode("utf-8", errors="replace"))
    if file_list.returncode != 0:
        raise DriverError("failed to enumerate tracked files for repo sync")
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
    remote_cmd = f"tar -xf - -C {shlex.quote(ctx.remote_repo_root)}"
    ssh_cmd = f"ssh -o BatchMode=yes {shlex.quote(host)} {shlex.quote(f'bash -lc {shlex.quote(remote_cmd)}')}"
    start = time.time()
    proc = subprocess.run(
        ["/bin/bash", "-lc", f"COPYFILE_DISABLE=1 COPY_EXTENDED_ATTRIBUTES_DISABLE=1 {shell_join(tar_parts)} | {ssh_cmd}"],
        cwd=str(ROOT),
        input=file_list.stdout,
        text=False,
        capture_output=True,
    )
    duration = time.time() - start
    ctx.logger.write(
        host="local",
        cwd=str(ROOT),
        argv=["/bin/bash", "-lc", f"COPYFILE_DISABLE=1 COPY_EXTENDED_ATTRIBUTES_DISABLE=1 {shell_join(tar_parts)} | {ssh_cmd}"],
        exit_code=proc.returncode,
        duration_sec=duration,
        artifacts={
            "remote_repo_root": ctx.remote_repo_root,
            "transfer": "tar-ssh",
            "sync_mode": "git-ls-files",
        },
    )
    if proc.stdout:
        sys.stdout.write(proc.stdout.decode("utf-8", errors="replace"))
    if proc.stderr:
        sys.stderr.write(proc.stderr.decode("utf-8", errors="replace"))
    if proc.returncode != 0:
        raise DriverError("tar-over-ssh repo sync failed")


def collect_remote_artifacts(
    ctx: Context,
    host: str,
    *,
    required: bool = False,
    failure_type: str = "artifact-collection",
    ) -> bool:
    ctx.local_remote_dir.mkdir(parents=True, exist_ok=True)
    remote_cmd = f"tar -C {shlex.quote(ctx.remote_artifacts_root)} -cf - ."
    ssh_cmd = f"ssh -o BatchMode=yes {shlex.quote(host)} {shlex.quote(f'bash -lc {shlex.quote(remote_cmd)}')}"
    pipeline = f"{ssh_cmd} | tar -xf - -C {shlex.quote(str(ctx.local_remote_dir))}"
    proc = run_local_shell(
        ctx,
        pipeline,
        check=False,
        artifacts={"remote_artifacts_root": ctx.remote_artifacts_root, "transfer": "tar-ssh"},
    )
    if proc.returncode != 0:
        ctx.failures.append(
            {
                "type": failure_type,
                "host": host,
                "remote_artifacts_root": ctx.remote_artifacts_root,
                "local_remote_dir": str(ctx.local_remote_dir),
                "exit_code": proc.returncode,
            }
        )
        ctx.save_manifest()
        if required:
            raise DriverError(f"failed to collect remote artifacts from {ctx.remote_artifacts_root}")
        return False
    return True


def collect_remote_binaries(ctx: Context, host: str, variant: Variant) -> None:
    variant_dir = ctx.local_binaries_dir / variant.key()
    variant_dir.mkdir(parents=True, exist_ok=True)
    includes = [
        "src/luajit",
        "src/libluajit.a",
        "src/libluajit.so",
        "src/luajit.h",
    ]
    for rel_path in includes:
        remote_path = f"{ctx.remote_repo_root}/{rel_path}"
        remote_cmd = f"test -r {shlex.quote(remote_path)} && cat {shlex.quote(remote_path)}"
        local_path = variant_dir / pathlib.Path(rel_path).name
        ssh_cmd = f"ssh -o BatchMode=yes {shlex.quote(host)} {shlex.quote(f'bash -lc {shlex.quote(remote_cmd)}')}"
        pipeline = f"{ssh_cmd} > {shlex.quote(str(local_path))}"
        proc = run_local_shell(
            ctx,
            pipeline,
            check=False,
            artifacts={"binary": rel_path, "variant": variant.key(), "transfer": "ssh-cat"},
        )
        if proc.returncode != 0 and local_path.exists():
            local_path.unlink()


def record_local_git_state(ctx: Context) -> None:
    sha = run_local(ctx, ["git", "rev-parse", "HEAD"], check=True, echo_output=False)
    status = run_local(ctx, ["git", "status", "--short", "--branch"], check=True, echo_output=False)
    diff = run_local(ctx, ["git", "diff", "--binary", "HEAD"], check=True, echo_output=False)
    write_text(ctx.local_metadata_dir / "git-sha.txt", sha.stdout)
    write_text(ctx.local_metadata_dir / "git-status.txt", status.stdout)
    write_text(ctx.local_metadata_dir / "dirty.patch", diff.stdout)


def check_contract_doc(ctx: Context) -> None:
    if not CONTRACT_FILE.exists():
        raise DriverError(f"missing contract document: {CONTRACT_FILE}")
    content = CONTRACT_FILE.read_text(encoding="utf-8")
    missing = []
    for marker in CONTRACT_MARKERS.values():
        if marker not in content:
            missing.append(marker)
    for marker in CONTRACT_FILE_MARKERS:
        if marker not in content:
            missing.append(marker)
    unchecked = [line.strip() for line in content.splitlines() if line.lstrip().startswith("- [ ]")]
    if missing or unchecked:
        failure = {
            "type": "contract",
            "missing_markers": missing,
            "unchecked_items": unchecked,
        }
        ctx.failures.append(failure)
        ctx.save_manifest()
        raise DriverError("contract document gate failed")


def expand_choice(value: str, ordered: List[str]) -> List[str]:
    if value == "both":
        return ordered
    return [value]


def build_variants(
    stage: str,
    compiler_choice: str,
    mode_choice: str,
    jit_choice: str,
    *,
    perf_family_selected: bool = False,
) -> List[Variant]:
    compilers = expand_choice(compiler_choice, ["gcc", "clang"])
    modes = expand_choice(mode_choice, ["debug", "release"])
    jits = expand_choice(jit_choice, ["off", "on"])
    include_z13 = os.environ.get("S390X_DRIVER_INCLUDE_Z13") == "1"
    variants: List[Variant] = []
    if stage == "matrix":
        for compiler in compilers:
            for mode in modes:
                for jit in jits:
                    for ffi in ("on", "off"):
                        for build_style in ("static", "dynamic"):
                            variants.append(Variant(compiler=compiler, mode=mode, jit=jit, ffi=ffi, build_style=build_style))
        return variants
    if stage == "perf":
        for compiler in compilers:
            for mode in modes:
                if perf_family_selected and jit_choice == "on":
                    variants.append(Variant(compiler=compiler, mode=mode, jit="on", tuning="baseline"))
                    if include_z13 and compiler == "gcc":
                        variants.append(Variant(compiler=compiler, mode=mode, jit="on", tuning="z13"))
                    variants.append(Variant(compiler=compiler, mode=mode, jit="off", tuning="baseline"))
                    continue
                for jit in jits:
                    variants.append(Variant(compiler=compiler, mode=mode, jit=jit, tuning="baseline"))
                    if include_z13 and jit == "on" and compiler == "gcc":
                        variants.append(Variant(compiler=compiler, mode=mode, jit=jit, tuning="z13"))
        return variants
    for compiler in compilers:
        for mode in modes:
            for jit in jits:
                variants.append(Variant(compiler=compiler, mode=mode, jit=jit))
    return variants


def suites_for_stage(stage: str, requested_suite: str) -> List[str]:
    if requested_suite == "all":
        return STAGE_DEFAULT_SUITES[stage]
    return [requested_suite]


def make_var_string(variant: Variant) -> str:
    vars_map: Dict[str, str] = {
        "CC": variant.compiler,
        "HOST_CC": variant.compiler,
        "BUILDMODE": variant.build_style,
    }
    xcflags: List[str] = []
    if variant.jit == "off":
        xcflags.append("-DLUAJIT_DISABLE_JIT")
    elif variant.jit == "on":
        xcflags.append("-DLUAJIT_ENABLE_S390X_JIT")
    if variant.ffi == "off":
        xcflags.append("-DLUAJIT_DISABLE_FFI")
    if variant.mode == "debug":
        vars_map["CCDEBUG"] = "-g3"
        vars_map["CCOPT"] = "-O0"
        xcflags.append("-DLUA_USE_ASSERT")
    if variant.tuning == "z13":
        vars_map["TARGET_CFLAGS"] = "-march=z13 -mtune=z13 -mvx -mzvector -mmvcle -mfused-madd"
        vars_map["HOST_CFLAGS"] = "-march=z13 -mtune=z13"
    if xcflags:
        vars_map["XCFLAGS"] = " ".join(xcflags)
    return " ".join(f"{key}={shlex.quote(value)}" for key, value in vars_map.items())


def build_command(variant: Variant) -> str:
    make_vars = make_var_string(variant)
    return textwrap.dedent(
        f"""
        set -euo pipefail
        export PATH="$PWD/src:$PATH"
        make -C src clean {make_vars}
        make -C src {make_vars}
        """
    ).strip()


def testlj_caps(stage: str, variant: Variant) -> str:
    caps: List[str] = []
    if variant.ffi == "on":
        caps.append("ffi")
    if stage not in PRE_JIT_STAGES or variant.jit == "on":
        caps.append("compiler")
    if variant.jit == "on":
        caps.append("trace")
    return ",".join(caps)


def shell_skip_condition(paths: Iterable[str]) -> str:
    items = list(paths)
    if not items:
        return ""
    joined = " || ".join(f'[ "$test" = {shlex.quote(path)} ]' for path in items)
    return f"if {joined}; then\n    continue\n  fi"


def selected_perf_families(args: argparse.Namespace) -> List[str]:
    if args.perf_family:
        if "all" in args.perf_family:
            return [
                family
                for family, _meta in sorted(
                    PERF_FAMILY_METADATA.items(),
                    key=lambda item: (item[1]["promotion_order"], item[0]),
                )
            ]
        return list(dict.fromkeys(args.perf_family))
    return [
        family
        for family, meta in sorted(
            PERF_FAMILY_METADATA.items(),
            key=lambda item: (item[1]["promotion_order"], item[0]),
        )
        if meta["default_gate"]
    ]


def selected_perf_bench_files(args: argparse.Namespace) -> List[str]:
    return [PERF_BENCH_LUA_FILE_BY_FAMILY[family] for family in selected_perf_families(args)]


def suite_command(ctx: Context, stage: str, suite: str, variant: Variant) -> Optional[str]:
    expect_ffi = "1" if variant.ffi == "on" else "0"
    expect_jit = "1" if variant.jit == "on" else "0"
    if suite == "smoke":
        return textwrap.dedent(
            f"""
            set -euo pipefail
            export PATH="$PWD/src:$PATH"
            export S390X_EXPECT_FFI={expect_ffi}
            export S390X_EXPECT_JIT={expect_jit}
            ./src/luajit -v
            ./src/luajit -e 'print(1)'
            ./src/luajit -e 'local jit=require("jit"); assert(type(jit.status)=="function"); local expect_jit = os.getenv("S390X_EXPECT_JIT") == "1"; local enabled = select(1, jit.status()); assert(enabled == expect_jit, ("jit.status mismatch: expected %s got %s"):format(tostring(expect_jit), tostring(enabled))); if expect_jit then local okopt, opt = pcall(require, "jit.opt"); assert(okopt, tostring(opt)); assert(type(opt.start) == "function"); opt.start("hotloop=1"); end; jit.off(); local ok, ffi = pcall(require, "ffi"); local want = os.getenv("S390X_EXPECT_FFI") == "1"; assert(ok == want, tostring(ffi)); print("jit=" .. tostring(enabled)); print(ok and "ffi=ok" or "ffi=disabled")'
            """
        ).strip()
    if suite == "pure_lua":
        caps = testlj_caps(stage, variant)
        perl_caps = caps
        if stage == "closure":
            perl_caps = ",".join(part for part in caps.split(",") if part and part != "trace")
        perl_steps = "\n".join(
            [
                f'printf "%s\\n" {shlex.quote(f"t/{test}")} > "$S390X_STEP_DIR/current_test.txt"\n'
                f'TEST_LJ_BIN="$PWD/src/luajit" TEST_LJ_CAPS={shlex.quote(perl_caps)} PATH="$PWD/src:$PATH" perl {shlex.quote(f"t/{test}")}'
                for test in PURE_LUA_T_FILES
            ]
        )
        full_prove = ""
        if stage == "closure":
            full_prove = textwrap.dedent(
                f"""
                printf "%s\\n" "prove -v t/*.t" > "$S390X_STEP_DIR/current_test.txt"
                TEST_LJ_BIN="$PWD/src/luajit" TEST_LJ_CAPS={shlex.quote(perl_caps)} PATH="$PWD/src:$PATH" prove -v t/*.t
                """
            ).strip()
        return textwrap.dedent(
            f"""
            set -euo pipefail
            export PATH="$PWD/src:$PATH"
            export TEST_LJ_CAPS={shlex.quote(caps)}
            for test in tests/s390x/pure_lua/*.lua; do
              [ -e "$test" ] || continue
              printf "%s\\n" "$test" > "$S390X_STEP_DIR/current_test.txt"
              ./src/luajit "$test"
            done
            {perl_steps}
            {full_prove}
            """
        ).strip()
    if suite == "ffi_abi":
        if variant.ffi == "off":
            return None
        return textwrap.dedent(
            f"""
            set -euo pipefail
            export PATH="$PWD/src:$PATH"
            CC={variant.compiler} sh tests/s390x/build_oracles.sh
            printf "%s\\n" "tests/s390x/ffi_abi/run.lua" > "$S390X_STEP_DIR/current_test.txt"
            ./src/luajit tests/s390x/ffi_abi/run.lua
            """
        ).strip()
    if suite == "callbacks":
        if variant.ffi == "off":
            return None
        return textwrap.dedent(
            f"""
            set -euo pipefail
            export PATH="$PWD/src:$PATH"
            CC={variant.compiler} sh tests/s390x/build_oracles.sh
            for test in tests/s390x/callbacks/*.lua; do
              [ -e "$test" ] || continue
              printf "%s\\n" "$test" > "$S390X_STEP_DIR/current_test.txt"
              ./src/luajit "$test"
            done
            """
        ).strip()
    if suite == "jit_core":
        caps = testlj_caps(stage, variant)
        core_tests = [
            test for test in JIT_CORE_LUA_FILES
            if variant.ffi == "on" or test not in JIT_CORE_FFI_LUA_FILES
        ]
        lua_steps = "\n".join(
            [
                f'printf "%s\\n" {shlex.quote(test)} > "$S390X_STEP_DIR/current_test.txt"\n'
                f'./src/luajit {shlex.quote(test)}'
                for test in core_tests
            ]
        )
        skip_core = shell_skip_condition(JIT_CORE_FFI_LUA_FILES if variant.ffi == "off" else [])
        perl_steps = "\n".join(
            [
                f'printf "%s\\n" {shlex.quote(f"t/{test}")} > "$S390X_STEP_DIR/current_test.txt"\n'
                f'TEST_LJ_BIN="$PWD/src/luajit" TEST_LJ_CAPS={shlex.quote(caps)} PATH="$PWD/src:$PATH" perl {shlex.quote(f"t/{test}")}'
                for test in JIT_T_FILES["jit_core"]
            ]
        )
        return textwrap.dedent(
            f"""
            set -euo pipefail
            export PATH="$PWD/src:$PATH"
            {lua_steps}
            for test in tests/s390x/jit_core/*.lua; do
              [ -e "$test" ] || continue
              if [ "$test" = "tests/s390x/jit_core/isarray_root_loop.lua" ]; then
                continue
              fi
              {skip_core}
              printf "%s\\n" "$test" > "$S390X_STEP_DIR/current_test.txt"
              ./src/luajit "$test"
            done
            {perl_steps}
            """
        ).strip()
    if suite == "jit_loops":
        caps = testlj_caps(stage, variant)
        lua_steps = "\n".join(
            [
                f'printf "%s\\n" {shlex.quote(test)} > "$S390X_STEP_DIR/current_test.txt"\n'
                f'./src/luajit {shlex.quote(test)}'
                for test in JIT_LOOPS_LUA_FILES
            ]
        )
        perl_steps = "\n".join(
            [
                f'printf "%s\\n" {shlex.quote(f"t/{test}")} > "$S390X_STEP_DIR/current_test.txt"\n'
                f'TEST_LJ_BIN="$PWD/src/luajit" TEST_LJ_CAPS={shlex.quote(caps)} PATH="$PWD/src:$PATH" perl {shlex.quote(f"t/{test}")}'
                for test in JIT_T_FILES["jit_loops"]
            ]
        )
        return textwrap.dedent(
            f"""
            set -euo pipefail
            export PATH="$PWD/src:$PATH"
            {lua_steps}
            for test in tests/s390x/jit_loops/*.lua; do
              [ -e "$test" ] || continue
              if [ "$test" = "tests/s390x/jit_loops/explicit_next.lua" ]; then
                continue
              fi
              printf "%s\\n" "$test" > "$S390X_STEP_DIR/current_test.txt"
              ./src/luajit "$test"
            done
            {perl_steps}
            """
        ).strip()
    if suite == "jit_be":
        skip_be = shell_skip_condition(JIT_BE_FFI_LUA_FILES if variant.ffi == "off" else [])
        return textwrap.dedent(
            f"""
            set -euo pipefail
            export PATH="$PWD/src:$PATH"
            for test in tests/s390x/jit_be/*.lua; do
              [ -e "$test" ] || continue
              {skip_be}
              printf "%s\\n" "$test" > "$S390X_STEP_DIR/current_test.txt"
              ./src/luajit "$test"
            done
            """
        ).strip()
    if suite == "trace_tools":
        include_condition = " || ".join([f'[ \"$test\" = {shlex.quote(test)} ]' for test in TRACE_TOOLS_LUA_FILES])
        return textwrap.dedent(
            f"""
            set -euo pipefail
            export PATH="$PWD/src:$PATH"
            for test in tests/s390x/trace_tools/*.lua; do
              [ -e "$test" ] || continue
              if ! ({include_condition}); then
                continue
              fi
              printf "%s\\n" "$test" > "$S390X_STEP_DIR/current_test.txt"
              ./src/luajit "$test"
            done
            """
        ).strip()
    if suite == "soak":
        if variant.jit == "off":
            return None
        skip_soak = shell_skip_condition(SOAK_FFI_LUA_FILES if variant.ffi == "off" else [])
        return textwrap.dedent(
            f"""
            set -euo pipefail
            export PATH="$PWD/src:$PATH"
            for test in tests/s390x/soak/*.lua; do
              [ -e "$test" ] || continue
              {skip_soak}
              printf "%s\\n" "$test" > "$S390X_STEP_DIR/current_test.txt"
              ./src/luajit "$test"
            done
            """
        ).strip()
    if suite == "perf_bench":
        bench_files = [
            test for test in selected_perf_bench_files(ctx.args)
            if variant.ffi == "on" or test not in PERF_FFI_LUA_FILES
        ]
        retained_exports = [
            f"export {name}={shlex.quote(value)}"
            for name, value in sorted(PERF_RETAINED_ENV.items())
        ]
        bench_steps: List[str] = [
            'mkdir -p "$S390X_STEP_DIR/bench-logs" "$S390X_STEP_DIR/perf-stat"',
            'bench_json="$S390X_STEP_DIR/benchmarks.jsonl"',
            'rm -f "$bench_json"',
            *retained_exports,
        ]
        if variant.ffi == "on" and any(test in PERF_FFI_LUA_FILES for test in bench_files):
            bench_steps.append(f"CC={variant.compiler} sh tests/s390x/build_oracles.sh")
        for test in bench_files:
            stem = pathlib.Path(test).stem
            stdout_path = f'$S390X_STEP_DIR/bench-logs/{stem}.stdout.log'
            stderr_path = f'$S390X_STEP_DIR/bench-logs/{stem}.stderr.log'
            perf_stat_csv = f'$S390X_STEP_DIR/perf-stat/{stem}.csv'
            perf_stat_stdout = f'$S390X_STEP_DIR/perf-stat/{stem}.stdout.log'
            perf_stat_stderr = f'$S390X_STEP_DIR/perf-stat/{stem}.stderr.log'
            bench_steps.extend(
                [
                    'before_count=$(test -f "$bench_json" && wc -l < "$bench_json" || echo 0)',
                    f'printf "%s\\n" {shlex.quote(test)} > "$S390X_STEP_DIR/current_test.txt"',
                    (
                        f'S390X_PERF_OUTPUT_JSONL="$bench_json" '
                        f'S390X_PERF_WARMUP=1 '
                        f'S390X_PERF_SAMPLES=5 '
                        f'S390X_PERF_BENCH_FILE={shlex.quote(test)} '
                        f'./src/luajit {shlex.quote(test)} > {stdout_path} 2> {stderr_path}'
                    ),
                    'after_count=$(test -f "$bench_json" && wc -l < "$bench_json" || echo 0)',
                    'if [ "$after_count" -le "$before_count" ]; then',
                    f'  echo "benchmark emitted no metrics: {test}" >&2',
                    '  exit 1',
                    'fi',
                ]
            )
            if (
                variant.mode == "release"
                and variant.jit == "on"
                and variant.ffi == "on"
                and variant.tuning == "baseline"
                and test in PERF_STAT_BENCH_LUA_FILES
            ):
                bench_steps.extend(
                    [
                        'if ! command -v perf >/dev/null 2>&1; then',
                        '  echo "perf is required for release perf benchmarks" >&2',
                        '  exit 1',
                        'fi',
                        (
                            "S390X_PERF_OUTPUT_JSONL= "
                            "S390X_PERF_WARMUP=1 "
                            "S390X_PERF_SAMPLES=1 "
                            f"S390X_PERF_BENCH_FILE={shlex.quote(test)} "
                            "perf stat -x, "
                            "-o "
                            f"{perf_stat_csv} "
                            "-e cycles,instructions,branches,branch-misses,cache-references,cache-misses "
                            f"./src/luajit {shlex.quote(test)} > {perf_stat_stdout} 2> {perf_stat_stderr}"
                        ),
                    ]
                )
        return textwrap.dedent(
            f"""
            set -euo pipefail
            export PATH="$PWD/src:$PATH"
            {'\n'.join(bench_steps)}
            """
        ).strip()
    raise DriverError(f"unknown suite: {suite}")


def perf_record_identity(record: dict) -> tuple[str, str, str]:
    return (
        str(record.get("family", "")),
        str(record.get("workload", "")),
        str(record.get("scale", "")),
    )


def load_json_file(path: pathlib.Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def parse_perf_stat_csv(path: pathlib.Path) -> Dict[str, float]:
    metrics: Dict[str, float] = {}
    if not path.exists():
        return metrics
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line:
            continue
        parts = [part.strip() for part in line.split(",")]
        if len(parts) < 3:
            continue
        value, _, event = parts[:3]
        if value in ("<not counted>", "<not supported>"):
            continue
        try:
            metrics[event] = float(value)
        except ValueError:
            continue
    return metrics


def enrich_perf_record(
    ctx: Context,
    *,
    host: str,
    variant: Variant,
    bench_file: str,
    record: dict,
    commit: str,
    control_arch: Optional[str] = None,
) -> dict:
    enriched = dict(record)
    enriched["commit"] = commit
    enriched["host"] = host
    enriched["compiler"] = variant.compiler
    enriched["mode"] = variant.mode
    enriched["jit"] = variant.jit
    enriched["ffi"] = variant.ffi
    enriched["build_style"] = variant.build_style
    enriched["tuning"] = variant.tuning
    enriched["bench_file"] = bench_file
    if control_arch:
        enriched["control_arch"] = control_arch
    return enriched


def parse_perf_records_for_step(ctx: Context, result: StepResult) -> None:
    if result.suite != "perf_bench" or result.exit_code != 0:
        return
    local_step = pathlib.Path(result.local_step_dir)
    jsonl_path = local_step / "benchmarks.jsonl"
    if not jsonl_path.exists():
        ctx.failures.append(
            {
                "type": "perf-parse",
                "step": result.step,
                "message": f"missing benchmark metrics file: {jsonl_path}",
            }
        )
        ctx.save_manifest()
        return
    commit = (ctx.local_metadata_dir / "git-sha.txt").read_text(encoding="utf-8").strip()
    variant = Variant(**result.variant)
    stat_dir = local_step / "perf-stat"
    records_before = len(ctx.perf_records)
    malformed = []
    for line_no, raw_line in enumerate(jsonl_path.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw_line.strip()
        if not line:
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError as exc:
            malformed.append({"line": line_no, "message": str(exc), "text": line})
            continue
        bench_file = str(record.get("bench_file") or record.get("source_file") or "")
        if not bench_file:
            malformed.append({"line": line_no, "message": "missing bench_file", "text": line})
            continue
        stem = pathlib.Path(bench_file).stem
        record["perf_stat"] = parse_perf_stat_csv(stat_dir / f"{stem}.csv")
        ctx.perf_records.append(
            enrich_perf_record(
                ctx,
                host=result.host,
                variant=variant,
                bench_file=bench_file,
                record=record,
                commit=commit,
            )
        )
    if malformed:
        ctx.failures.append(
            {
                "type": "perf-parse",
                "step": result.step,
                "message": "malformed benchmark metrics",
                "entries": malformed,
            }
        )
    if len(ctx.perf_records) == records_before:
        ctx.failures.append(
            {
                "type": "perf-parse",
                "step": result.step,
                "message": "no benchmark records parsed from perf bench step",
            }
        )
    ctx.save_manifest()


def median(values: List[float]) -> float:
    ordered = sorted(values)
    mid = len(ordered) // 2
    if len(ordered) % 2:
        return ordered[mid]
    return (ordered[mid - 1] + ordered[mid]) / 2.0


def percentile(values: List[float], pct: float) -> float:
    ordered = sorted(values)
    if not ordered:
        return 0.0
    index = max(0, min(len(ordered) - 1, int((len(ordered) - 1) * pct + 0.999999)))
    return ordered[index]


def perf_baseline_selector(record: dict) -> bool:
    return (
        record.get("host") == "kdz"
        and record.get("compiler") == "gcc"
        and record.get("mode") == "release"
        and record.get("jit") == "on"
        and record.get("ffi") == "on"
        and record.get("tuning") == "baseline"
    )


def perf_authoritative_selector(record: dict) -> bool:
    return (
        record.get("host") in ("kdz", "kdz1")
        and record.get("compiler") == "gcc"
        and record.get("mode") == "release"
        and record.get("ffi") == "on"
    )


def perf_suite_key(record: dict) -> tuple[str, str, str]:
    return (
        str(record.get("family", "")),
        str(record.get("workload", "")),
        str(record.get("scale", "")),
    )


def build_perf_baseline_index(records: List[dict], selector) -> Dict[tuple[str, str, str], dict]:
    index: Dict[tuple[str, str, str], dict] = {}
    for record in records:
        if selector(record):
            index[perf_suite_key(record)] = record
    return index


def maybe_add_comparison(ctx: Context, comparison_type: str, left: dict, right: dict, left_label: str, right_label: str) -> None:
    left_runtime = float(left.get("median_runtime_sec", 0.0))
    right_runtime = float(right.get("median_runtime_sec", 0.0))
    if left_runtime <= 0 or right_runtime <= 0:
        return
    ctx.perf_comparisons.append(
        {
            "type": comparison_type,
            "family": left.get("family"),
            "workload": left.get("workload"),
            "scale": left.get("scale"),
            "left_label": left_label,
            "right_label": right_label,
            "left_runtime_sec": left_runtime,
            "right_runtime_sec": right_runtime,
            "speedup_ratio": right_runtime / left_runtime,
        }
    )


def generate_perf_comparisons(ctx: Context) -> None:
    ctx.perf_comparisons = []
    baseline_index = build_perf_baseline_index(ctx.perf_records, perf_baseline_selector)
    for record in ctx.perf_records:
        baseline = baseline_index.get(perf_suite_key(record))
        if not baseline:
            continue
        baseline_runtime = float(baseline.get("median_runtime_sec", 0.0))
        record_runtime = float(record.get("median_runtime_sec", 0.0))
        if baseline_runtime > 0 and record_runtime > 0:
            record["relative_speedup_vs_baseline"] = baseline_runtime / record_runtime

    def record_index(selector) -> Dict[tuple[str, str, str], dict]:
        return build_perf_baseline_index(ctx.perf_records, selector)

    on_index = record_index(lambda r: perf_authoritative_selector(r) and r.get("jit") == "on" and r.get("tuning") == "baseline")
    off_index = record_index(lambda r: perf_authoritative_selector(r) and r.get("jit") == "off" and r.get("tuning") == "baseline")
    for key, on_record in on_index.items():
        off_record = off_index.get(key)
        if off_record:
            maybe_add_comparison(ctx, "jit_on_vs_off", on_record, off_record, "jit=on", "jit=off")

    z13_index = record_index(lambda r: perf_authoritative_selector(r) and r.get("jit") == "on" and r.get("tuning") == "z13")
    base_index = record_index(lambda r: perf_authoritative_selector(r) and r.get("jit") == "on" and r.get("tuning") == "baseline")
    for key, tuned_record in z13_index.items():
        base_record = base_index.get(key)
        if base_record:
            maybe_add_comparison(ctx, "z13_vs_baseline", tuned_record, base_record, "z13", "baseline")

    clang_index = record_index(lambda r: r.get("host") in ("kdz", "kdz1") and r.get("compiler") == "clang" and r.get("mode") == "release" and r.get("jit") == "on" and r.get("ffi") == "on" and r.get("tuning") == "baseline")
    gcc_index = record_index(lambda r: perf_authoritative_selector(r) and r.get("jit") == "on" and r.get("tuning") == "baseline")
    for key, clang_record in clang_index.items():
        gcc_record = gcc_index.get(key)
        if gcc_record:
            maybe_add_comparison(ctx, "clang_vs_gcc", clang_record, gcc_record, "clang", "gcc")

    control_index = record_index(lambda r: r.get("host") == "local-control")
    for key, control_record in control_index.items():
        baseline = baseline_index.get(key)
        if baseline:
            maybe_add_comparison(ctx, "cross_arch_vs_kdz", control_record, baseline, "local-control", "kdz")


def build_perf_family_status(ctx: Context) -> List[dict]:
    family_records: Dict[str, List[dict]] = {}
    for record in ctx.perf_records:
        family = str(record.get("family", ""))
        family_records.setdefault(family, []).append(record)

    statuses = []
    for family, meta in sorted(
        PERF_FAMILY_METADATA.items(),
        key=lambda item: (item[1]["promotion_order"], item[0]),
    ):
        records = family_records.get(family, [])
        hosts = sorted({str(record.get("host", "")) for record in records if record.get("host")})
        compilers = sorted({str(record.get("compiler", "")) for record in records if record.get("compiler")})
        modes = sorted({str(record.get("mode", "")) for record in records if record.get("mode")})
        jit_modes = sorted({str(record.get("jit", "")) for record in records if record.get("jit")})
        tunings = sorted({str(record.get("tuning", "")) for record in records if record.get("tuning")})
        statuses.append(
            {
                "family": family,
                "default_gate": meta["default_gate"],
                "promotion_order": meta["promotion_order"],
                "status": meta["status"],
                "priority": meta["priority"],
                "notes": meta["notes"],
                "records": len(records),
                "hosts": hosts,
                "compilers": compilers,
                "modes": modes,
                "jit_modes": jit_modes,
                "tunings": tunings,
                "has_primary_baseline": any(perf_baseline_selector(record) for record in records),
                "has_jit_off": any(record.get("jit") == "off" for record in records),
                "has_z13": any(record.get("tuning") == "z13" for record in records),
            }
        )
    return statuses


def build_perf_hotspots(ctx: Context) -> List[dict]:
    baseline_records = [record for record in ctx.perf_records if perf_baseline_selector(record)]
    off_index = build_perf_baseline_index(
        ctx.perf_records,
        lambda r: (
            perf_authoritative_selector(r)
            and r.get("jit") == "off"
            and r.get("tuning") == "baseline"
        ),
    )
    z13_index = build_perf_baseline_index(
        ctx.perf_records,
        lambda r: (
            perf_authoritative_selector(r)
            and r.get("jit") == "on"
            and r.get("tuning") == "z13"
        ),
    )
    hotspots = []
    for record in baseline_records:
        key = perf_suite_key(record)
        off_record = off_index.get(key)
        z13_record = z13_index.get(key)
        on_runtime = float(record.get("median_runtime_sec", 0.0))
        off_runtime = float(off_record.get("median_runtime_sec", 0.0)) if off_record else 0.0
        z13_runtime = float(z13_record.get("median_runtime_sec", 0.0)) if z13_record else 0.0
        jit_on_over_off = (on_runtime / off_runtime) if off_runtime > 0 else None
        z13_speedup = (on_runtime / z13_runtime) if z13_runtime > 0 else None
        hotspots.append(
            {
                "family": record.get("family"),
                "workload": record.get("workload"),
                "scale": record.get("scale"),
                "baseline_median_runtime_sec": on_runtime,
                "baseline_p95_runtime_sec": float(record.get("p95_runtime_sec", 0.0)),
                "jit_off_median_runtime_sec": off_runtime or None,
                "jit_on_over_off_ratio": jit_on_over_off,
                "z13_median_runtime_sec": z13_runtime or None,
                "z13_speedup_vs_baseline": z13_speedup,
                "classification": "jit-regression" if jit_on_over_off and jit_on_over_off > 1.0 else "baseline",
            }
        )
    hotspots.sort(
        key=lambda entry: (
            float(entry.get("jit_on_over_off_ratio") or 0.0),
            float(entry.get("baseline_median_runtime_sec") or 0.0),
        ),
        reverse=True,
    )
    return hotspots


def write_perf_artifacts(ctx: Context) -> None:
    ctx.local_perf_dir.mkdir(parents=True, exist_ok=True)
    family_status = build_perf_family_status(ctx)
    hotspots = build_perf_hotspots(ctx)
    write_text(ctx.local_perf_dir / "benchmarks.json", json.dumps(ctx.perf_records, indent=2, sort_keys=True) + "\n")
    write_text(ctx.local_perf_dir / "comparisons.json", json.dumps(ctx.perf_comparisons, indent=2, sort_keys=True) + "\n")
    write_text(ctx.local_perf_dir / "family-status.json", json.dumps(family_status, indent=2, sort_keys=True) + "\n")
    write_text(ctx.local_perf_dir / "hotspots.json", json.dumps(hotspots, indent=2, sort_keys=True) + "\n")

    baseline_records = [record for record in ctx.perf_records if perf_baseline_selector(record)]
    baseline_sorted = sorted(baseline_records, key=lambda record: float(record.get("median_runtime_sec", 0.0)), reverse=True)
    slowest = baseline_sorted[:5]
    jit_wins = sorted(
        [entry for entry in ctx.perf_comparisons if entry.get("type") == "jit_on_vs_off"],
        key=lambda entry: float(entry.get("speedup_ratio", 0.0)),
        reverse=True,
    )[:5]
    z13_wins = sorted(
        [entry for entry in ctx.perf_comparisons if entry.get("type") == "z13_vs_baseline"],
        key=lambda entry: float(entry.get("speedup_ratio", 0.0)),
        reverse=True,
    )[:5]
    clang_deltas = sorted(
        [entry for entry in ctx.perf_comparisons if entry.get("type") == "clang_vs_gcc"],
        key=lambda entry: abs(float(entry.get("speedup_ratio", 1.0)) - 1.0),
        reverse=True,
    )[:5]
    cross_arch = sorted(
        [entry for entry in ctx.perf_comparisons if entry.get("type") == "cross_arch_vs_kdz"],
        key=lambda entry: float(entry.get("right_runtime_sec", 0.0)),
        reverse=True,
    )[:5]
    regressions = [entry for entry in hotspots if entry.get("classification") == "jit-regression"][:5]

    lines = [
        "# s390x Performance Summary",
        "",
        f"- Run ID: `{ctx.run_id}`",
        f"- Total benchmark records: `{len(ctx.perf_records)}`",
        f"- Total comparisons: `{len(ctx.perf_comparisons)}`",
        "",
        "## Family Status",
        "",
    ]
    for entry in family_status:
        lines.append(
            f"- `{entry['family']}`: `{entry['status']}`, "
            f"default gate `{entry['default_gate']}`, "
            f"records `{entry['records']}`, "
            f"primary baseline `{entry['has_primary_baseline']}`"
        )

    lines.extend([
        "",
        "## Slowest Baseline Workloads",
        "",
    ])
    if slowest:
        for record in slowest:
            lines.append(
                f"- `{record['family']}/{record['workload']}/{record['scale']}` "
                f"median `{record['median_runtime_sec']:.6f}s`, p95 `{record['p95_runtime_sec']:.6f}s`"
            )
    else:
        lines.append("- No primary baseline records captured.")

    lines.extend(["", "## Top JIT-On Regressions vs JIT-Off", ""])
    if regressions:
        for entry in regressions:
            lines.append(
                f"- `{entry['family']}/{entry['workload']}/{entry['scale']}` "
                f"on/off `{entry['jit_on_over_off_ratio']:.3f}x`, "
                f"baseline `{entry['baseline_median_runtime_sec']:.6f}s`"
            )
    else:
        lines.append("- No JIT-on regressions relative to JIT-off are currently captured.")

    lines.extend(["", "## JIT On vs Off", ""])
    if jit_wins:
        for entry in jit_wins:
            lines.append(
                f"- `{entry['family']}/{entry['workload']}/{entry['scale']}` "
                f"speedup `{entry['speedup_ratio']:.3f}x`"
            )
    else:
        lines.append("- No jit on/off comparisons available.")

    lines.extend(["", "## z13 vs Baseline", ""])
    if z13_wins:
        for entry in z13_wins:
            lines.append(
                f"- `{entry['family']}/{entry['workload']}/{entry['scale']}` "
                f"speedup `{entry['speedup_ratio']:.3f}x`"
            )
    else:
        lines.append("- No z13 comparisons available.")

    lines.extend(["", "## Clang vs GCC", ""])
    if clang_deltas:
        for entry in clang_deltas:
            lines.append(
                f"- `{entry['family']}/{entry['workload']}/{entry['scale']}` "
                f"ratio `{entry['speedup_ratio']:.3f}x`"
            )
    else:
        lines.append("- No clang vs gcc comparisons available.")

    lines.extend(["", "## Cross-Arch Control", ""])
    if cross_arch:
        for entry in cross_arch:
            lines.append(
                f"- `{entry['family']}/{entry['workload']}/{entry['scale']}` "
                f"ratio `{entry['speedup_ratio']:.3f}x`"
            )
    else:
        lines.append("- No cross-arch control comparisons available.")

    if ctx.perf_notes:
        lines.extend(["", "## Perf Notes", ""])
        for note in ctx.perf_notes:
            lines.append(f"- {note}")

    write_text(ctx.local_perf_dir / "perf-summary.md", "\n".join(lines) + "\n")


def local_control_variant() -> Variant:
    compiler = os.environ.get("CC") or ("clang" if shutil.which("clang") else "cc")
    return Variant(compiler=compiler, mode="release", jit="on", ffi="on", build_style="static", tuning="local-control")


def local_control_env() -> Dict[str, str]:
    env: Dict[str, str] = {}
    if sys.platform == "darwin" and not os.environ.get("MACOSX_DEPLOYMENT_TARGET"):
        version = platform.mac_ver()[0]
        parts = version.split(".")
        if len(parts) >= 2:
            env["MACOSX_DEPLOYMENT_TARGET"] = f"{parts[0]}.{parts[1]}"
    return env


def run_local_control_perf(ctx: Context) -> None:
    baseline_records = [record for record in ctx.perf_records if perf_baseline_selector(record)]
    if not baseline_records:
        return
    family_order = []
    seen = set()
    for record in sorted(baseline_records, key=lambda item: float(item.get("median_runtime_sec", 0.0)), reverse=True):
        family = str(record.get("family"))
        if family and family not in seen:
            seen.add(family)
            family_order.append(family)
        if len(family_order) >= PERF_TOP_CROSS_ARCH_COUNT:
            break
    bench_map = PERF_BENCH_LUA_FILE_BY_FAMILY
    selected_files = [bench_map[family] for family in family_order if family in bench_map]
    if not selected_files:
        return

    control_root = pathlib.Path(tempfile.mkdtemp(prefix="local-control-", dir=str(ctx.local_perf_dir)))
    source_root = control_root / "src-tree"
    source_root.mkdir(parents=True, exist_ok=True)
    tracked_files = subprocess.run(
        ["git", "ls-files", "-z"],
        cwd=str(ROOT),
        capture_output=True,
        text=False,
        check=False,
    )
    if tracked_files.returncode != 0:
        ctx.perf_notes.append("Local cross-arch control skipped: failed to enumerate tracked files.")
        ctx.save_manifest()
        return
    for rel in tracked_files.stdout.decode("utf-8", errors="replace").split("\0"):
        if not rel:
            continue
        src = ROOT / rel
        dst = source_root / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
    control_variant = local_control_variant()
    control_env = local_control_env()
    build_cmd = (
        f"make -C src clean BUILDMODE=static CC={shlex.quote(control_variant.compiler)} "
        f"HOST_CC={shlex.quote(control_variant.compiler)} && "
        f"make -C src BUILDMODE=static CC={shlex.quote(control_variant.compiler)} "
        f"HOST_CC={shlex.quote(control_variant.compiler)}"
    )
    build_proc = run_local_shell(
        ctx,
        build_cmd,
        cwd=source_root,
        check=False,
        env=control_env,
        artifacts={"perf_control_build": str(source_root)},
    )
    if build_proc.returncode != 0:
        ctx.perf_notes.append(
            "Local cross-arch control skipped: local workstation build failed; native s390x results remain authoritative."
        )
        ctx.save_manifest()
        return

    control_jsonl = control_root / "benchmarks.jsonl"
    control_logs = control_root / "logs"
    control_logs.mkdir(parents=True, exist_ok=True)
    commit = (ctx.local_metadata_dir / "git-sha.txt").read_text(encoding="utf-8").strip()
    arch = os.uname().machine
    for bench_file in selected_files:
        stem = pathlib.Path(bench_file).stem
        env = {
            "S390X_PERF_OUTPUT_JSONL": str(control_jsonl),
            "S390X_PERF_WARMUP": "1",
            "S390X_PERF_SAMPLES": "5",
            "S390X_PERF_BENCH_FILE": bench_file,
        }
        proc = subprocess.run(
            ["./src/luajit", bench_file],
            cwd=str(source_root),
            env={**os.environ, **control_env, **env},
            text=True,
            capture_output=True,
        )
        write_text(control_logs / f"{stem}.stdout.log", proc.stdout)
        write_text(control_logs / f"{stem}.stderr.log", proc.stderr)
        if proc.returncode != 0:
            ctx.perf_notes.append(
                f"Local cross-arch control skipped: benchmark {bench_file} exited {proc.returncode}."
            )
            ctx.save_manifest()
            return
    if not control_jsonl.exists():
        ctx.perf_notes.append("Local cross-arch control skipped: no benchmark metrics were emitted.")
        ctx.save_manifest()
        return
    for line_no, raw_line in enumerate(control_jsonl.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw_line.strip()
        if not line:
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError as exc:
            ctx.perf_notes.append(
                f"Local cross-arch control skipped: malformed metric at line {line_no} ({exc})."
            )
            ctx.save_manifest()
            return
        bench_file = str(record.get("bench_file") or record.get("source_file") or "")
        ctx.perf_records.append(
            enrich_perf_record(
                ctx,
                host="local-control",
                variant=control_variant,
                bench_file=bench_file,
                record=record,
                commit=commit,
                control_arch=arch,
            )
        )
    ctx.save_manifest()


def finalize_perf_stage(ctx: Context) -> None:
    if not any(result.suite == "perf_bench" for result in ctx.results):
        return
    if not ctx.perf_records:
        ctx.failures.append({"type": "perf", "message": "no perf benchmark records collected"})
        ctx.save_manifest()
        return
    generate_perf_comparisons(ctx)
    write_perf_artifacts(ctx)
    if not ctx.failures:
        run_local_control_perf(ctx)
        generate_perf_comparisons(ctx)
        write_perf_artifacts(ctx)
    ctx.save_manifest()


def remote_step_dir(suite: str, variant: Variant) -> str:
    return f"{REMOTE_ARTIFACTS_NAME}/steps/{suite}/{variant.key()}"


def slugify_label(value: str) -> str:
    slug = re.sub(r"[^A-Za-z0-9._-]+", "-", value)
    return slug.strip("-") or "run"


def run_local_step(
    ctx: Context,
    *,
    suite: str,
    step_name: str,
    command: str,
    env: Optional[Dict[str, str]] = None,
) -> StepResult:
    local_step = ctx.local_run_dir / "local-steps" / suite / step_name
    local_step.mkdir(parents=True, exist_ok=True)
    stdout_path = local_step / "stdout.log"
    stderr_path = local_step / "stderr.log"
    start = time.time()
    proc = subprocess.run(
        ["/bin/bash", "-lc", command],
        cwd=str(ROOT),
        env={**os.environ, **(env or {})},
        text=True,
        capture_output=True,
    )
    duration = time.time() - start
    write_text(stdout_path, proc.stdout)
    write_text(stderr_path, proc.stderr)
    ctx.logger.write(
        host="local",
        cwd=str(ROOT),
        argv=["/bin/bash", "-lc", command],
        exit_code=proc.returncode,
        duration_sec=duration,
        env_diff=env,
        artifacts={"suite": suite, "local_step_dir": str(local_step)},
    )
    if proc.stdout:
        sys.stdout.write(proc.stdout)
    if proc.stderr:
        sys.stderr.write(proc.stderr)
    result = StepResult(
        step=step_name,
        suite=suite,
        host="local",
        variant={},
        exit_code=proc.returncode,
        duration_sec=duration,
        remote_step_dir="",
        local_step_dir=str(local_step),
        command=command,
    )
    ctx.results.append(result)
    if proc.returncode != 0:
        ctx.failures.append(
            {
                "step": step_name,
                "suite": suite,
                "host": "local",
                "local_step_dir": str(local_step),
                "exit_code": proc.returncode,
            }
        )
    ctx.save_manifest()
    return result


def run_coverage_audit(ctx: Context) -> StepResult:
    command = shell_join(
        [
            "python3",
            "tools/s390x/coverage_inventory.py",
            "--root",
            str(ROOT),
            "--out",
            str(ctx.local_coverage_dir),
        ]
    )
    return run_local_step(ctx, suite="coverage_audit", step_name="coverage-audit", command=command)


def downstream_remote_root(prefix: str, run_id: str) -> str:
    return f"{REMOTE_BASE}/{slugify_label(f'{prefix}-{run_id}')}"


def run_downstream(ctx: Context) -> StepResult:
    if not ctx.host:
        raise DriverError("host not selected for downstream suite")
    fallback_host = next((candidate for candidate in HOSTS if candidate != ctx.host), ctx.host)
    openresty_label = slugify_label(f"closure-openresty-{ctx.run_id}")
    kong_label = slugify_label(f"closure-kong-{ctx.run_id}")
    openresty_root = downstream_remote_root("closure-openresty", ctx.run_id)
    kong_root = downstream_remote_root("closure-kong", ctx.run_id)
    openresty_json = ctx.local_downstream_dir / "openresty.json"
    kong_json = ctx.local_downstream_dir / "kong.json"
    command = textwrap.dedent(
        f"""
        set -euo pipefail
        export S390X_PRIMARY_HOST={shlex.quote(ctx.host)}
        export S390X_FALLBACK_HOST={shlex.quote(fallback_host)}
        export S390X_DEMO_LABEL={shlex.quote(openresty_label)}
        export S390X_KONG_LABEL={shlex.quote(kong_label)}
        export KEEP_RUNNING=0
        export RUN_WRK=0
        export KONG_DELAYED_JIT_ON_IN_NGINX=1
        export KONG_DELAYED_JIT_ON_SECS=3
        {shell_join(["bash", "demo/openresty/run_demo.sh"])}
        {shell_join(["bash", "demo/kong/run_kong_demo.sh"])}
        export S390X_KONG_RUNTIME_ROOT={shlex.quote(kong_root)}
        {shell_join(["bash", "demo/kong/run_kong_require_probe.sh", kong_root])}
        """
    ).strip()
    result = run_local_step(ctx, suite="downstream", step_name="downstream", command=command)
    if result.exit_code == 0:
        write_text(
            openresty_json,
            json.dumps(
                {
                    "host": ctx.host,
                    "remote_root": openresty_root,
                    "label": openresty_label,
                    "build_start_ok": True,
                    "request_path_ok": True,
                    "jit_endpoint_ok": True,
                },
                indent=2,
                sort_keys=True,
            )
            + "\n",
        )
        write_text(
            kong_json,
            json.dumps(
                {
                    "host": ctx.host,
                    "remote_root": kong_root,
                    "label": kong_label,
                    "prepare_ok": True,
                    "nginx_start_ok": True,
                    "status_ok": True,
                    "demo_ok": True,
                    "jit_startup_guard": "delayed-init-worker-reenable",
                    "require_probe_ok": True,
                    "collectgarbage_probe_ok": True,
                },
                indent=2,
                sort_keys=True,
            )
            + "\n",
        )
    return result


def run_remote_step(ctx: Context, variant: Variant, suite: str, command: str) -> StepResult:
    host = ctx.host
    if host is None:
        raise DriverError("host not selected")

    step_name = f"{suite}-{variant.key()}"
    remote_step = f"{ctx.remote_artifacts_root}/steps/{suite}/{variant.key()}"
    local_step = ctx.local_remote_dir / "steps" / suite / variant.key()
    local_step.mkdir(parents=True, exist_ok=True)
    wrapped = shell_join(
        [
            f"{ctx.remote_repo_root}/tools/s390x/remote_run.sh",
            step_name,
            remote_step,
            ctx.remote_repo_root,
            command,
        ]
    )
    proc = run_ssh(
        ctx,
        host,
        wrapped,
        artifacts={
            "suite": suite,
            "variant": variant.key(),
            "remote_step_dir": remote_step,
        },
    )
    collect_remote_artifacts(ctx, host, required=True, failure_type="bootstrap-artifact-collection")
    metadata_path = local_step / "metadata.json"
    duration = 0.0
    if metadata_path.exists():
        try:
            duration = float(json.loads(metadata_path.read_text(encoding="utf-8")).get("duration_sec", 0.0))
        except (ValueError, json.JSONDecodeError):
            duration = 0.0
    result = StepResult(
        step=step_name,
        suite=suite,
        host=host,
        variant=asdict(variant),
        exit_code=proc.returncode,
        duration_sec=duration,
        remote_step_dir=remote_step,
        local_step_dir=str(local_step),
        command=command,
    )
    ctx.results.append(result)
    if proc.returncode != 0:
        ctx.failures.append(
            {
                "step": step_name,
                "suite": suite,
                "variant": asdict(variant),
                "remote_step_dir": remote_step,
                "local_step_dir": str(local_step),
                "exit_code": proc.returncode,
            }
        )
    ctx.save_manifest()
    return result


def bootstrap_remote(ctx: Context, host: str) -> None:
    command = shell_join(
        [
            f"{ctx.remote_repo_root}/tools/s390x/remote_bootstrap.sh",
            ctx.remote_run_root,
            ctx.remote_repo_root,
        ]
    )
    proc = run_ssh(
        ctx,
        host,
        command,
        artifacts={"stage": "bootstrap", "remote_run_root": ctx.remote_run_root},
    )
    collect_remote_artifacts(ctx, host, required=True, failure_type="step-artifact-collection")
    if proc.returncode != 0:
        if host == "kdz":
            ctx.failover_from = "kdz"
            ctx.host = "zkd0"
            ensure_remote_dirs(ctx, ctx.host)
            sync_repo(ctx, ctx.host)
            proc = run_ssh(
                ctx,
                ctx.host,
                command,
                artifacts={"stage": "bootstrap", "remote_run_root": ctx.remote_run_root},
            )
            collect_remote_artifacts(ctx, ctx.host, required=True, failure_type="bootstrap-artifact-collection")
        if proc.returncode != 0:
            raise DriverError("remote bootstrap failed on all configured hosts")


def write_summary(ctx: Context) -> None:
    suites = sorted({result.suite for result in ctx.results})
    success = all(result.exit_code == 0 for result in ctx.results) and not ctx.failures
    next_stage = None
    if success:
        try:
            idx = STAGE_ORDER.index(ctx.args.stage)
            next_stage = STAGE_ORDER[idx + 1]
        except (ValueError, IndexError):
            next_stage = None
    else:
        next_stage = ctx.args.stage
    summary = textwrap.dedent(
        f"""
        # s390x Bring-Up Run Summary

        - Run ID: `{ctx.run_id}`
        - Stage: `{ctx.args.stage}`
        - Requested suite: `{ctx.args.suite}`
        - Host used: `{ctx.host}`
        - Primary host: `{ctx.primary_host}`
        - Failover from: `{ctx.failover_from or "none"}`
        - Success: `{str(success).lower()}`
        - Executed suites: `{", ".join(suites) if suites else "none"}`
        - Failure count: `{len(ctx.failures)}`
        - Local artifacts: `{ctx.local_run_dir}`
        - Remote run root: `{ctx.remote_run_root}`
        - Next stage: `{next_stage or "none"}`
        """
    ).strip()
    write_text(ctx.local_run_dir / "summary.md", summary + "\n")

    report_lines = [
        "# Stage Report",
        "",
        f"- Stage: `{ctx.args.stage}`",
        f"- Host: `{ctx.host}`",
        f"- Variants executed: `{len({json.dumps(result.variant, sort_keys=True) for result in ctx.results})}`",
        f"- Suites executed: `{', '.join(suites) if suites else 'none'}`",
        f"- Failures: `{len(ctx.failures)}`",
        "",
        "## Step Results",
        "",
    ]
    if ctx.results:
        for result in ctx.results:
            status = "PASS" if result.exit_code == 0 else "FAIL"
            variant_label = Variant(**result.variant).key() if result.variant else "local"
            report_lines.append(
                f"- `{status}` `{result.step}` on `{result.host}` "
                f"variant `{variant_label}` "
                f"logs at `{result.local_step_dir}`"
            )
    else:
        report_lines.append("- No remote steps were executed.")
    report_lines.extend(["", "## Open Items", ""])
    if ctx.failures:
        for failure in ctx.failures:
            report_lines.append(f"- `{failure.get('step', failure.get('type', 'unknown'))}`")
    else:
        report_lines.append("- No recorded failures.")
    if ctx.perf_records:
        report_lines.extend(
            [
                "",
                "## Performance Artifacts",
                "",
                f"- Benchmark records: `{len(ctx.perf_records)}`",
                f"- Comparisons: `{len(ctx.perf_comparisons)}`",
                f"- Perf summary: `{ctx.local_perf_dir / 'perf-summary.md'}`",
            ]
        )
    report_lines.extend(["", "## Next Gate", "", f"- `{next_stage or 'none'}`"])
    write_text(ctx.local_run_dir / "stage-report.md", "\n".join(report_lines) + "\n")
    write_text(ctx.local_run_dir / "failures.json", json.dumps(ctx.failures, indent=2, sort_keys=True) + "\n")


def update_latest_symlink(run_dir: pathlib.Path) -> None:
    ARTIFACTS_ROOT.mkdir(parents=True, exist_ok=True)
    temp_link = ARTIFACTS_ROOT / f".latest.tmp.{os.getpid()}"
    try:
        if temp_link.exists() or temp_link.is_symlink():
            temp_link.unlink()
        temp_link.symlink_to(run_dir.name)
        os.replace(temp_link, LATEST_LINK)
    finally:
        if temp_link.exists() or temp_link.is_symlink():
            temp_link.unlink()


def record_exception(ctx: Context, failure_type: str, exc: BaseException) -> None:
    tb = "".join(traceback.format_exception(type(exc), exc, exc.__traceback__))
    ctx.failures.append(
        {
            "type": failure_type,
            "message": str(exc),
            "traceback": tb,
        }
    )
    write_text(ctx.local_run_dir / "unexpected-error.txt", tb)


def finalize_run(ctx: Context) -> None:
    write_summary(ctx)
    ctx.save_manifest()
    update_latest_symlink(ctx.local_run_dir)


def maybe_cleanup_remote(ctx: Context) -> None:
    if ctx.args.keep_remote or not ctx.host:
        return
    command = shell_join(["rm", "-rf", ctx.remote_run_root])
    run_ssh(ctx, ctx.host, command, check=False)


def run_stage(ctx: Context) -> None:
    if ctx.args.stage != "contract":
        check_contract_doc(ctx)

    suites = suites_for_stage(ctx.args.stage, ctx.args.suite)
    remote_suites = [suite for suite in suites if suite not in LOCAL_SUITES]
    local_suites = [suite for suite in suites if suite in LOCAL_SUITES]
    variants = build_variants(
        ctx.args.stage,
        ctx.args.compiler,
        ctx.args.mode,
        ctx.args.jit,
        perf_family_selected=bool(ctx.args.perf_family),
    )
    need_host = bool(remote_suites) or "downstream" in local_suites
    if need_host:
        host = choose_host(ctx)
    else:
        host = None
    if remote_suites and host is not None:
        ensure_remote_dirs(ctx, host)
        sync_repo(ctx, host)
        bootstrap_remote(ctx, host)

    if ctx.args.stage == "contract":
        check_contract_doc(ctx)

    if remote_suites:
        for variant in variants:
            build_result = run_remote_step(ctx, variant, "build", build_command(variant))
            if build_result.exit_code != 0:
                collect_remote_artifacts(ctx, ctx.host, required=True, failure_type="step-artifact-collection")
                continue
            collect_remote_binaries(ctx, ctx.host, variant)
            for suite in remote_suites:
                command = suite_command(ctx, ctx.args.stage, suite, variant)
                if command is None:
                    continue
                result = run_remote_step(ctx, variant, suite, command)
                if suite == "perf_bench":
                    parse_perf_records_for_step(ctx, result)
    for suite in local_suites:
        if suite == "coverage_audit":
            run_coverage_audit(ctx)
        elif suite == "downstream":
            run_downstream(ctx)
        else:
            raise DriverError(f"unknown local suite: {suite}")
    if any(result.suite == "perf_bench" for result in ctx.results):
        finalize_perf_stage(ctx)


def main() -> int:
    args = parse_args()
    ctx = Context(args)
    record_local_git_state(ctx)
    ctx.save_manifest()
    try:
        run_stage(ctx)
        finalize_run(ctx)
        return 0 if not ctx.failures else 1
    except KeyboardInterrupt as exc:
        record_exception(ctx, "interrupt", exc)
        finalize_run(ctx)
        return 130
    except DriverError as exc:
        ctx.failures.append({"type": "driver", "message": str(exc)})
        finalize_run(ctx)
        return 1
    except Exception as exc:  # Keep unexpected harness failures visible in artifacts.
        record_exception(ctx, "exception", exc)
        finalize_run(ctx)
        return 1
    finally:
        collect_remote_artifacts(ctx, ctx.host) if ctx.host else None
        maybe_cleanup_remote(ctx)
        ctx.logger.close()
        ctx.release_local_lock()


if __name__ == "__main__":
    sys.exit(main())
