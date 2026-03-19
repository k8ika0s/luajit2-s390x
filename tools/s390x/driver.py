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
import shlex
import subprocess
import sys
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
HOSTS = ("kdz", "zkd0")
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

SUITES = {
    "smoke": "Build and runtime smoke checks",
    "pure_lua": "Interpreter-focused Lua and non-JIT regression coverage",
    "ffi_abi": "Generated ABI oracle coverage for outgoing FFI calls",
    "callbacks": "FFI callback and unwind regression coverage",
    "jit_core": "Trace creation, exits, and basic JIT behavior",
    "jit_be": "Big-endian JIT-sensitive regression coverage",
    "jit_loops": "Loop tracing, iterator, and vararg JIT coverage",
    "soak": "Long-running mixed stress coverage",
}

STAGE_DEFAULT_SUITES = {
    "contract": ["smoke"],
    "interp": ["smoke", "pure_lua"],
    "ffi-call": ["smoke", "pure_lua", "ffi_abi"],
    "callback-unwind": ["smoke", "pure_lua", "ffi_abi", "callbacks"],
    "jit-bringup": ["smoke", "jit_core", "jit_loops"],
    "jit-correctness": ["smoke", "jit_core", "jit_loops", "jit_be", "soak"],
    "matrix": ["smoke", "pure_lua", "ffi_abi", "callbacks", "jit_core", "jit_loops", "jit_be"],
    "perf": ["smoke", "soak"],
}

STAGE_ORDER = list(STAGE_DEFAULT_SUITES)
PRE_JIT_STAGES = {"contract", "interp", "ffi-call", "callback-unwind"}

RSYNC_EXCLUDES = [
    ".git",
    ".DS_Store",
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
        self.run_id = args.run_id if args.run_id != "auto" else self._auto_run_id()
        self.local_run_dir = ARTIFACTS_ROOT / self.run_id
        self.local_remote_dir = self.local_run_dir / "remote"
        self.local_binaries_dir = self.local_run_dir / "binaries"
        self.local_metadata_dir = self.local_run_dir / "metadata"
        self.local_run_dir.mkdir(parents=True, exist_ok=True)
        self.local_remote_dir.mkdir(parents=True, exist_ok=True)
        self.local_binaries_dir.mkdir(parents=True, exist_ok=True)
        self.local_metadata_dir.mkdir(parents=True, exist_ok=True)
        self.logger = CommandLogger(self.local_run_dir / "commands.ndjson")
        self.host: Optional[str] = None
        self.primary_host: Optional[str] = None
        self.failover_from: Optional[str] = None
        self.remote_run_root = f"{REMOTE_BASE}/{self.run_id}"
        self.remote_repo_root = f"{self.remote_run_root}/{REMOTE_REPO_NAME}"
        self.remote_artifacts_root = f"{self.remote_run_root}/{REMOTE_ARTIFACTS_NAME}"
        self.results: List[StepResult] = []
        self.failures: List[dict] = []
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
        }

    @staticmethod
    def _auto_run_id() -> str:
        return dt.datetime.now().strftime("%Y%m%dT%H%M%SZ")

    def save_manifest(self) -> None:
        self.manifest["host"] = self.host
        self.manifest["primary_host"] = self.primary_host
        self.manifest["failover_from"] = self.failover_from
        self.manifest["results"] = [asdict(result) for result in self.results]
        self.manifest["failures"] = self.failures
        self.manifest_path.write_text(json.dumps(self.manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run the native s390x bring-up loop.")
    parser.add_argument("--host", choices=("auto", "kdz", "zkd0"), default="auto")
    parser.add_argument("--stage", choices=tuple(STAGE_ORDER), required=True)
    parser.add_argument("--suite", choices=("all", *SUITES), default="all")
    parser.add_argument("--compiler", choices=("gcc", "clang", "both"), default="gcc")
    parser.add_argument("--mode", choices=("debug", "release", "both"), default="debug")
    parser.add_argument("--jit", choices=("off", "on", "both"), default="off")
    parser.add_argument("--run-id", default="auto")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--keep-remote", action="store_true")
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
    if proc.stdout:
        sys.stdout.write(proc.stdout)
    if proc.stderr:
        sys.stderr.write(proc.stderr)
    if check and proc.returncode != 0:
        raise DriverError(f"local command failed: {' '.join(argv)}")
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
    argv = ["rsync", "-a", "--delete"]
    for pattern in RSYNC_EXCLUDES:
        argv.extend(["--exclude", pattern])
    argv.extend([f"{ROOT}/", f"{host}:{ctx.remote_repo_root}/"])
    proc = run_local(
        ctx,
        argv,
        check=True,
        artifacts={"remote_repo_root": ctx.remote_repo_root},
    )
    if proc.returncode != 0:
        raise DriverError("rsync failed")


def collect_remote_artifacts(
    ctx: Context,
    host: str,
    *,
    required: bool = False,
    failure_type: str = "artifact-collection",
) -> bool:
    ctx.local_remote_dir.mkdir(parents=True, exist_ok=True)
    argv = [
        "rsync",
        "-a",
        f"{host}:{ctx.remote_artifacts_root}/",
        f"{ctx.local_remote_dir}/",
    ]
    proc = run_local(ctx, argv, check=False, artifacts={"remote_artifacts_root": ctx.remote_artifacts_root})
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
        argv = ["rsync", "-a", f"{host}:{ctx.remote_repo_root}/{rel_path}", f"{variant_dir}/"]
        run_local(ctx, argv, check=False, artifacts={"binary": rel_path, "variant": variant.key()})


def record_local_git_state(ctx: Context) -> None:
    sha = run_local(ctx, ["git", "rev-parse", "HEAD"], check=True)
    status = run_local(ctx, ["git", "status", "--short", "--branch"], check=True)
    diff = run_local(ctx, ["git", "diff", "--binary", "HEAD"], check=True)
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


def build_variants(stage: str, compiler_choice: str, mode_choice: str, jit_choice: str) -> List[Variant]:
    compilers = expand_choice(compiler_choice, ["gcc", "clang"])
    modes = expand_choice(mode_choice, ["debug", "release"])
    jits = expand_choice(jit_choice, ["off", "on"])
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
                for jit in jits:
                    variants.append(Variant(compiler=compiler, mode=mode, jit=jit, tuning="baseline"))
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


def suite_command(stage: str, suite: str, variant: Variant) -> Optional[str]:
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
        perl_steps = "\n".join(
            [
                f'printf "%s\\n" {shlex.quote(f"t/{test}")} > "$S390X_STEP_DIR/current_test.txt"\n'
                f'TEST_LJ_BIN="$PWD/src/luajit" TEST_LJ_CAPS={shlex.quote(caps)} PATH="$PWD/src:$PATH" perl {shlex.quote(f"t/{test}")}'
                for test in PURE_LUA_T_FILES
            ]
        )
        return textwrap.dedent(
            f"""
            set -euo pipefail
            export PATH="$PWD/src:$PATH"
            for test in tests/s390x/pure_lua/*.lua; do
              [ -e "$test" ] || continue
              printf "%s\\n" "$test" > "$S390X_STEP_DIR/current_test.txt"
              ./src/luajit "$test"
            done
            {perl_steps}
            """
        ).strip()
    if suite == "ffi_abi":
        if variant.ffi == "off":
            return None
        return textwrap.dedent(
            f"""
            set -euo pipefail
            export PATH="$PWD/src:$PATH"
            mkdir -p tests/s390x/ffi_abi/build
            printf "%s\\n" "tests/s390x/ffi_abi/run.lua" > "$S390X_STEP_DIR/current_test.txt"
            {variant.compiler} -shared -fPIC -std=c11 -O0 -g -o tests/s390x/ffi_abi/build/liboracle.so tests/s390x/ffi_abi/oracle.c -lm
            ./src/luajit tests/s390x/ffi_abi/run.lua tests/s390x/ffi_abi/build/liboracle.so
            """
        ).strip()
    if suite == "callbacks":
        if variant.ffi == "off":
            return None
        return textwrap.dedent(
            f"""
            set -euo pipefail
            export PATH="$PWD/src:$PATH"
            mkdir -p tests/s390x/callbacks/build
            printf "%s\\n" "tests/s390x/callbacks/run.lua" > "$S390X_STEP_DIR/current_test.txt"
            {variant.compiler} -shared -fPIC -std=c11 -O0 -g -o tests/s390x/callbacks/build/libcallback_oracle.so tests/s390x/callbacks/callback_oracle.c -lm
            ./src/luajit tests/s390x/callbacks/run.lua tests/s390x/callbacks/build/libcallback_oracle.so
            """
        ).strip()
    if suite == "jit_core":
        caps = testlj_caps(stage, variant)
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
            for test in tests/s390x/jit_core/*.lua; do
              [ -e "$test" ] || continue
              printf "%s\\n" "$test" > "$S390X_STEP_DIR/current_test.txt"
              ./src/luajit "$test"
            done
            {perl_steps}
            """
        ).strip()
    if suite == "jit_loops":
        caps = testlj_caps(stage, variant)
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
            for test in tests/s390x/jit_loops/*.lua; do
              [ -e "$test" ] || continue
              printf "%s\\n" "$test" > "$S390X_STEP_DIR/current_test.txt"
              ./src/luajit "$test"
            done
            {perl_steps}
            """
        ).strip()
    if suite == "jit_be":
        return textwrap.dedent(
            """
            set -euo pipefail
            export PATH="$PWD/src:$PATH"
            for test in tests/s390x/jit_be/*.lua; do
              [ -e "$test" ] || continue
              printf "%s\\n" "$test" > "$S390X_STEP_DIR/current_test.txt"
              ./src/luajit "$test"
            done
            """
        ).strip()
    if suite == "soak":
        return textwrap.dedent(
            """
            set -euo pipefail
            export PATH="$PWD/src:$PATH"
            for test in tests/s390x/soak/*.lua; do
              [ -e "$test" ] || continue
              printf "%s\\n" "$test" > "$S390X_STEP_DIR/current_test.txt"
              ./src/luajit "$test"
            done
            """
        ).strip()
    raise DriverError(f"unknown suite: {suite}")


def remote_step_dir(suite: str, variant: Variant) -> str:
    return f"{REMOTE_ARTIFACTS_NAME}/steps/{suite}/{variant.key()}"


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
            report_lines.append(
                f"- `{status}` `{result.step}` on `{result.host}` "
                f"variant `{Variant(**result.variant).key()}` "
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
    report_lines.extend(["", "## Next Gate", "", f"- `{next_stage or 'none'}`"])
    write_text(ctx.local_run_dir / "stage-report.md", "\n".join(report_lines) + "\n")
    write_text(ctx.local_run_dir / "failures.json", json.dumps(ctx.failures, indent=2, sort_keys=True) + "\n")


def update_latest_symlink(run_dir: pathlib.Path) -> None:
    if LATEST_LINK.exists() or LATEST_LINK.is_symlink():
        LATEST_LINK.unlink()
    LATEST_LINK.symlink_to(run_dir.name)


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
    variants = build_variants(ctx.args.stage, ctx.args.compiler, ctx.args.mode, ctx.args.jit)
    host = choose_host(ctx)
    ensure_remote_dirs(ctx, host)
    sync_repo(ctx, host)
    bootstrap_remote(ctx, host)

    if ctx.args.stage == "contract":
        check_contract_doc(ctx)

    for variant in variants:
        build_result = run_remote_step(ctx, variant, "build", build_command(variant))
        if build_result.exit_code != 0:
            collect_remote_artifacts(ctx, ctx.host, required=True, failure_type="step-artifact-collection")
            continue
        collect_remote_binaries(ctx, ctx.host, variant)
        for suite in suites:
            command = suite_command(ctx.args.stage, suite, variant)
            if command is None:
                continue
            run_remote_step(ctx, variant, suite, command)


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


if __name__ == "__main__":
    sys.exit(main())
