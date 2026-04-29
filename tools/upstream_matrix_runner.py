#!/usr/bin/env python3
"""Run the upstream validation/performance matrix on a local or SSH target."""

from __future__ import annotations

import argparse
import dataclasses
import datetime as dt
import json
import os
import pathlib
import platform
import shlex
import shutil
import signal
import subprocess
import sys
import tarfile
import tempfile
import time
from typing import Any


ROOT = pathlib.Path(__file__).resolve().parents[1]


def env_path(name: str, default: pathlib.Path) -> pathlib.Path:
    raw = os.environ.get(name)
    if not raw:
        return default
    path = pathlib.Path(raw).expanduser()
    return path if path.is_absolute() else ROOT / path


MATRIX_FILE = env_path(
    "LUAJIT_UPSTREAM_MATRIX_FILE",
    ROOT / "tests" / "matrix" / "upstream_validation_perf_matrix.json",
)
ARTIFACTS_ROOT = env_path(
    "LUAJIT_UPSTREAM_MATRIX_ARTIFACTS_ROOT",
    ROOT / "artifacts" / "s390x",
)

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
    "tests/s390x/jit_core/ffi_cdata_u32_forwarding.lua",
    "tests/s390x/jit_core/ffi_cdata_trace.lua",
    "tests/s390x/jit_core/ffi_complex_vararg_call_trace.lua",
    "tests/s390x/jit_core/ffi_fixed_call_pressure_trace.lua",
    "tests/s390x/jit_core/ffi_fixed_complex_call_trace.lua",
    "tests/s390x/jit_core/ffi_fixed_struct_call_trace.lua",
    "tests/s390x/jit_core/ffi_fp_struct_vararg_call_trace.lua",
    "tests/s390x/jit_core/ffi_fp_vararg_call_trace.lua",
    "tests/s390x/jit_core/ffi_large_struct_vararg_call_trace.lua",
    "tests/s390x/jit_core/ffi_literal_stop_same_callsite.lua",
    "tests/s390x/jit_core/ffi_mixed_vararg_call_trace.lua",
    "tests/s390x/jit_core/ffi_pointer_vararg_call_trace.lua",
    "tests/s390x/jit_core/ffi_promotion_vararg_call_trace.lua",
    "tests/s390x/jit_core/ffi_ptr_call_trace.lua",
    "tests/s390x/jit_core/ffi_stack_call_trace.lua",
    "tests/s390x/jit_core/ffi_string_vararg_call_trace.lua",
    "tests/s390x/jit_core/ffi_struct_vararg_call_trace.lua",
    "tests/s390x/jit_core/ffi_vararg_call_trace.lua",
    "tests/s390x/jit_core/ffi_width_vararg_call_trace.lua",
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

JIT_BE_FFI_LUA_FILES = {
    "tests/s390x/jit_be/ffi_cdata_mixed_width_loop_sum.lua",
    "tests/s390x/jit_be/ffi_cdata_pair_loop_sum.lua",
    "tests/s390x/jit_be/mixed_width_ffi.lua",
    "tests/s390x/jit_be/numeric_minmax_loop_sum.lua",
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

BUILD_TIMEOUT_SEC = 900
VALIDATION_TIMEOUT_SEC = 300
PERFORMANCE_TIMEOUT_SEC = 600

SCRUBBED_BUILD_ENV_KEYS = {
    "ARCHFLAGS",
    "CC",
    "CFLAGS",
    "CPPFLAGS",
    "CROSS",
    "HOST_CC",
    "HOST_CFLAGS",
    "HOST_LDFLAGS",
    "HOST_LIBS",
    "LDFLAGS",
    "LIBS",
    "MACOSX_DEPLOYMENT_TARGET",
}


class RunnerError(RuntimeError):
    pass


@dataclasses.dataclass
class Variant:
    id: str
    compiler: str
    mode: str
    jit: str
    ffi: str
    build_style: str
    env: dict[str, str] = dataclasses.field(default_factory=dict)


S390X_NATIVE_TARGET_CFLAGS = "-march=native -mtune=native"
S390X_NATIVE_HOST_CFLAGS = "-march=native -mtune=native"


def now_utc() -> str:
    return dt.datetime.now(dt.timezone.utc).isoformat()


def shell_join(parts: list[str]) -> str:
    return " ".join(shlex.quote(part) for part in parts)


def write_text(path: pathlib.Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def load_matrix(path: pathlib.Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def run(
    argv: list[str],
    *,
    cwd: pathlib.Path | None = None,
    env: dict[str, str] | None = None,
    capture: bool = True,
    check: bool = False,
) -> subprocess.CompletedProcess[str]:
    proc = subprocess.run(
        argv,
        cwd=str(cwd) if cwd else None,
        env={**os.environ, **(env or {})},
        text=True,
        capture_output=capture,
    )
    if check and proc.returncode != 0:
        raise RunnerError(f"command failed: {' '.join(argv)}")
    return proc


def run_shell(
    command: str,
    *,
    cwd: pathlib.Path | None = None,
    env: dict[str, str] | None = None,
    check: bool = False,
) -> subprocess.CompletedProcess[str]:
    return run(["/bin/bash", "-lc", command], cwd=cwd, env=env, capture=True, check=check)


def ssh(host: str, command: str, *, check: bool = False) -> subprocess.CompletedProcess[str]:
    return run(["ssh", "-o", "BatchMode=yes", host, f"bash -lc {shlex.quote(command)}"], check=check)


def git_snapshot_paths() -> list[pathlib.Path]:
    proc = run(
        ["git", "ls-files", "--cached", "--modified", "--others", "--exclude-standard"],
        cwd=ROOT,
        check=True,
    )
    paths = []
    for raw in proc.stdout.splitlines():
        rel = raw.strip()
        if not rel:
            continue
        if rel == ".DS_Store":
            continue
        if rel.startswith("artifacts/"):
            continue
        if rel.startswith(".git/"):
            continue
        paths.append(ROOT / rel)
    return paths


def build_snapshot_tar(paths: list[pathlib.Path], output_path: pathlib.Path) -> None:
    with tarfile.open(output_path, "w") as tf:
        for path in paths:
            if not path.exists():
                continue
            tf.add(path, arcname=str(path.relative_to(ROOT)))


def build_snapshot_tar_from_rev(rev: str, output_path: pathlib.Path, overlay_paths: list[pathlib.Path]) -> None:
    with tarfile.open(output_path, "w") as tf:
        proc = subprocess.run(
            ["git", "archive", "--format=tar", rev],
            cwd=str(ROOT),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=False,
        )
        if proc.returncode != 0:
            raise RunnerError(
                f"git archive failed for {rev}: {proc.stderr.decode('utf-8', errors='replace')}"
            )
        with tempfile.NamedTemporaryFile() as tfh:
            tfh.write(proc.stdout)
            tfh.flush()
            with tarfile.open(tfh.name, "r:") as archive:
                for member in archive.getmembers():
                    extracted = archive.extractfile(member) if member.isfile() else None
                    tf.addfile(member, extracted)
        for path in overlay_paths:
            if not path.exists():
                raise RunnerError(f"overlay path missing for git-rev snapshot: {path}")
            tf.add(path, arcname=str(path.relative_to(ROOT)))


def linux_sysinfo() -> dict[str, str]:
    out: dict[str, str] = {}
    sysinfo = pathlib.Path("/proc/sysinfo")
    if not sysinfo.exists():
        return out
    for raw in sysinfo.read_text(encoding="utf-8", errors="replace").splitlines():
        if ":" not in raw:
            continue
        key, value = raw.split(":", 1)
        norm = key.strip().lower().replace(" ", "_")
        if norm in {"manufacturer", "type", "model", "sequence_code", "plant"}:
            out[f"platform_machine_{norm}"] = value.strip()
    return out


def detect_target_info_local() -> dict[str, str]:
    target = {
        "arch": platform.machine(),
        "system": platform.system(),
        "endianness": sys.byteorder,
        "platform_uname": " ".join(platform.uname()),
    }
    target.update(linux_sysinfo())
    return target


def detect_target_info_ssh(host: str) -> dict[str, str]:
    proc = ssh(
        host,
        (
            "python3 -c "
            + shlex.quote(
                "import json, platform, sys; "
                "print(json.dumps({'arch': platform.machine(), 'system': platform.system(), 'endianness': sys.byteorder}))"
            )
        ),
        check=True,
    )
    return json.loads(proc.stdout)


def applicability_matches(applicability: dict[str, Any], target: dict[str, str], variant: Variant) -> tuple[bool, str]:
    arch_rule = applicability.get("architectures", "all")
    if arch_rule != "all":
        allowed = set(arch_rule)
        if target["arch"] not in allowed:
            return False, f"arch={target['arch']}"
    endian_rule = applicability.get("endianness", "all")
    endian_value = "big" if target["endianness"] == "big" else "little"
    if endian_rule != "all" and endian_value != endian_rule:
        return False, f"endianness={endian_value}"
    jit_rule = applicability.get("jit")
    if jit_rule and variant.jit != jit_rule:
        return False, f"jit={variant.jit}"
    ffi_rule = applicability.get("ffi")
    if ffi_rule and variant.ffi != ffi_rule:
        return False, f"ffi={variant.ffi}"
    return True, "applicable"


def native_build_config(target: dict[str, str]) -> dict[str, str]:
    if target["arch"].lower() == "s390x":
        return {
            "target_tuning": "native",
            "target_cflags": S390X_NATIVE_TARGET_CFLAGS,
            "host_cflags": S390X_NATIVE_HOST_CFLAGS,
        }
    return {"target_tuning": "default"}


def make_var_string(variant: Variant, target: dict[str, str]) -> str:
    vars_map: dict[str, str] = {
        "CC": variant.compiler,
        "HOST_CC": variant.compiler,
        "BUILDMODE": variant.build_style,
    }
    xcflags: list[str] = []
    if variant.ffi == "off":
        xcflags.append("-DLUAJIT_DISABLE_FFI")
    if variant.mode == "debug":
        vars_map["CCDEBUG"] = "-g3"
        vars_map["CCOPT"] = "-O0"
        xcflags.append("-DLUA_USE_ASSERT")
    build_config = native_build_config(target)
    if build_config.get("target_cflags"):
        vars_map["TARGET_CFLAGS"] = build_config["target_cflags"]
    if build_config.get("host_cflags"):
        vars_map["HOST_CFLAGS"] = build_config["host_cflags"]
    if xcflags:
        vars_map["XCFLAGS"] = " ".join(xcflags)
    return " ".join(f"{key}={shlex.quote(value)}" for key, value in vars_map.items())


def native_expected_ljarch(target: dict[str, str]) -> str | None:
    arch = target["arch"].lower()
    mapping = {
        "aarch64": "arm64",
        "amd64": "x64",
        "arm64": "arm64",
        "i386": "x86",
        "i686": "x86",
        "mips": "mips",
        "mips64": "mips64",
        "powerpc": "ppc",
        "powerpc64": "ppc",
        "powerpc64le": "ppc",
        "ppc": "ppc",
        "ppc64": "ppc",
        "ppc64le": "ppc",
        "s390x": "s390x",
        "x86_64": "x64",
    }
    return mapping.get(arch)


def sanitized_build_env() -> dict[str, str]:
    env = dict(os.environ)
    for key in list(env):
        if key.startswith("TARGET_") or key in SCRUBBED_BUILD_ENV_KEYS:
            env.pop(key, None)
    return env


def variant_step_env(base_env: dict[str, str], variant: Variant) -> dict[str, str]:
    merged = dict(base_env)
    merged.update(variant.env or {})
    return merged


def build_selection_command(variant: Variant, expected_ljarch: str | None, target: dict[str, str]) -> str:
    make_vars = make_var_string(variant, target)
    lines = [
        "echo 'MATRIX_BUILD_ENV_BEGIN'",
        "env | LC_ALL=C sort | grep -E '^(ARCHFLAGS|CC=|CFLAGS=|CPPFLAGS=|CROSS=|HOST_CC=|HOST_CFLAGS=|HOST_LDFLAGS=|HOST_LIBS=|LDFLAGS=|LIBS=|MACOSX_DEPLOYMENT_TARGET=|TARGET_)' || true",
        "echo 'MATRIX_BUILD_ENV_END'",
        f"make_dump=$(make -C src -pn {make_vars})",
        r"""target_ljarch=$(awk -F' = ' '/^TARGET_LJARCH = /{print $2; exit}' <<< "$make_dump")""",
        r"""dasm_arch=$(awk -F' = ' '/^DASM_ARCH = /{print $2; exit}' <<< "$make_dump")""",
        r"""target_sys=$(awk -F' = ' '/^TARGET_SYS = /{print $2; exit}' <<< "$make_dump")""",
        r"""target_cc=$(awk -F' = ' '/^TARGET_CC = /{print $2; exit}' <<< "$make_dump")""",
        'case "${dasm_arch:-}" in ""|"\\$(TARGET_LJARCH)") dasm_arch="${target_ljarch:-}";; esac',
        'case "${target_sys:-}" in ""|"\\$(HOST_SYS)") target_sys="$(uname -s)";; esac',
        f'case "${{target_cc:-}}" in ""|"\\$(STATIC_CC)") target_cc="{variant.compiler}";; esac',
        'compiler_triplet=""',
        'if [ -n "${target_cc}" ]; then',
        '  compiler_triplet=$(eval "$target_cc -dumpmachine" 2>/dev/null || true)',
        "fi",
        r"""printf 'MATRIX_BUILD_SELECTION TARGET_LJARCH=%s DASM_ARCH=%s TARGET_SYS=%s TARGET_CC=%s TARGET_TRIPLE=%s\n' "${target_ljarch:-}" "${dasm_arch:-}" "${target_sys:-}" "${target_cc:-}" "${compiler_triplet:-}" """,
    ]
    if expected_ljarch is not None:
        lines.extend(
            [
                f'if [ "${{target_ljarch:-}}" != "{expected_ljarch}" ]; then',
                r"""  printf 'MATRIX_BUILD_TARGET_MISMATCH expected=%s actual=%s\n' """ + f'"{expected_ljarch}" "${{target_ljarch:-<empty>}}" >&2',
                "  exit 97",
                "fi",
            ]
        )
    return "\n".join(lines) + "\n"


def build_command(variant: Variant, expected_ljarch: str | None, target: dict[str, str]) -> str:
    make_vars = make_var_string(variant, target)
    return (
        "set -euo pipefail\n"
        + "for key in ARCHFLAGS CC CFLAGS CPPFLAGS CROSS HOST_CC HOST_CFLAGS HOST_LDFLAGS HOST_LIBS LDFLAGS LIBS MACOSX_DEPLOYMENT_TARGET; do\n"
        + '  unset "$key" || true\n'
        + "done\n"
        + "while IFS='=' read -r name _; do\n"
        + '  case "$name" in\n'
        + '    TARGET_*) unset "$name" || true ;;\n'
        + "  esac\n"
        + "done < <(env)\n"
        + 'if [ "$(uname -s)" = "Darwin" ] && [ -z "${MACOSX_DEPLOYMENT_TARGET:-}" ]; then\n'
        + '  export MACOSX_DEPLOYMENT_TARGET="$(sw_vers -productVersion | awk -F. \'{print $1 "." $2}\')"\n'
        + "fi\n"
        + 'export PATH="$PWD/src:$PATH"\n'
        + build_selection_command(variant, expected_ljarch, target)
        + f"make -C src clean {make_vars}\n"
        + f"make -C src {make_vars}\n"
    )


def luajit_cmd(variant: Variant, script_path: str) -> str:
    parts = ["./src/luajit"]
    if variant.jit == "off":
        parts.append("-joff")
    parts.append(script_path)
    return shell_join(parts)


def luajit_prefix(variant: Variant) -> str:
    return "./src/luajit -joff" if variant.jit == "off" else "./src/luajit"


def shell_skip_condition(paths: set[str]) -> str:
    if not paths:
        return ""
    joined = " || ".join(f'[ \"$test\" = {shlex.quote(path)} ]' for path in sorted(paths))
    return f"if {joined}; then\n  continue\nfi"


def validation_command(lane_id: str, variant: Variant) -> str | None:
    expect_ffi = "1" if variant.ffi == "on" else "0"
    expect_jit = "1" if variant.jit == "on" else "0"
    if lane_id == "smoke":
        return (
            "set -euo pipefail\n"
            'export PATH="$PWD/src:$PATH"\n'
            f"export MATRIX_EXPECT_FFI={expect_ffi}\n"
            f"export MATRIX_EXPECT_JIT={expect_jit}\n"
            + shell_join(["./src/luajit"] + (["-joff"] if variant.jit == "off" else []) + ["-v"])
            + "\n"
            + shell_join(["./src/luajit"] + (["-joff"] if variant.jit == "off" else []) + ["-e", "print(1)"])
            + "\n"
            + shell_join(
                ["./src/luajit"]
                + (["-joff"] if variant.jit == "off" else [])
                + [
                    "-e",
                    "local jit=require(\"jit\"); "
            "assert(type(jit.status)==\"function\"); "
            "local expect_jit = os.getenv(\"MATRIX_EXPECT_JIT\") == \"1\"; "
            "local enabled = select(1, jit.status()); "
            "assert(enabled == expect_jit, (\"jit.status mismatch: expected %s got %s\"):format(tostring(expect_jit), tostring(enabled))); "
            "if expect_jit then local okopt, opt = pcall(require, \"jit.opt\"); assert(okopt, tostring(opt)); assert(type(opt.start) == \"function\"); opt.start(\"hotloop=1\"); end; "
            "jit.off(); "
            "local ok, ffi = pcall(require, \"ffi\"); "
            "local want = os.getenv(\"MATRIX_EXPECT_FFI\") == \"1\"; "
            "assert(ok == want, tostring(ffi)); "
            "print(\"jit=\" .. tostring(enabled)); "
                    "print(ok and \"ffi=ok\" or \"ffi=disabled\")",
                ]
            )
            + "\n"
        )
    if lane_id == "pure_lua":
        perl_steps = "\n".join(
            [
                f'TEST_LJ_BIN="$PWD/src/luajit" TEST_LJ_CAPS=ffi PATH="$PWD/src:$PATH" perl {shlex.quote(f"t/{test}")}'
                for test in PURE_LUA_T_FILES
            ]
        )
        return (
            "set -euo pipefail\n"
            'export PATH="$PWD/src:$PATH"\n'
            "for test in tests/s390x/pure_lua/*.lua; do\n"
            '  [ -e "$test" ] || continue\n'
            f'  {luajit_prefix(variant)} "$test"\n'
            "done\n"
            f"{perl_steps}\n"
        )
    if lane_id == "ffi_abi":
        if variant.ffi == "off":
            return None
        return (
            "set -euo pipefail\n"
            'export PATH="$PWD/src:$PATH"\n'
            f"CC={variant.compiler} sh tests/s390x/build_oracles.sh\n"
            f"{luajit_cmd(variant, 'tests/s390x/ffi_abi/run.lua')}\n"
        )
    if lane_id == "callbacks":
        if variant.ffi == "off":
            return None
        return (
            "set -euo pipefail\n"
            'export PATH="$PWD/src:$PATH"\n'
            f"CC={variant.compiler} sh tests/s390x/build_oracles.sh\n"
            "for test in tests/s390x/callbacks/*.lua; do\n"
            '  [ -e "$test" ] || continue\n'
            f'  {luajit_prefix(variant)} "$test"\n'
            "done\n"
        )
    if lane_id == "jit_core":
        core_tests = [
            test
            for test in JIT_CORE_LUA_FILES
            if variant.ffi == "on" or test not in JIT_CORE_FFI_LUA_FILES
        ]
        skip_core = shell_skip_condition(JIT_CORE_FFI_LUA_FILES if variant.ffi == "off" else set())
        lua_steps = "\n".join([luajit_cmd(variant, test) for test in core_tests])
        perl_steps = "\n".join(
            [
                f'TEST_LJ_BIN="$PWD/src/luajit" TEST_LJ_CAPS=ffi,compiler,trace PATH="$PWD/src:$PATH" perl {shlex.quote(f"t/{test}")}'
                for test in JIT_T_FILES["jit_core"]
            ]
        )
        return (
            "set -euo pipefail\n"
            'export PATH="$PWD/src:$PATH"\n'
            f"{lua_steps}\n"
            "for test in tests/s390x/jit_core/*.lua; do\n"
            '  [ -e "$test" ] || continue\n'
            '  if [ "$test" = "tests/s390x/jit_core/isarray_root_loop.lua" ]; then continue; fi\n'
            f"  {skip_core}\n"
            f'  {luajit_prefix(variant)} "$test"\n'
            "done\n"
            f"{perl_steps}\n"
        )
    if lane_id == "jit_loops":
        lua_steps = "\n".join([luajit_cmd(variant, test) for test in JIT_LOOPS_LUA_FILES])
        perl_steps = "\n".join(
            [
                f'TEST_LJ_BIN="$PWD/src/luajit" TEST_LJ_CAPS=ffi,compiler,trace PATH="$PWD/src:$PATH" perl {shlex.quote(f"t/{test}")}'
                for test in JIT_T_FILES["jit_loops"]
            ]
        )
        return (
            "set -euo pipefail\n"
            'export PATH="$PWD/src:$PATH"\n'
            f"{lua_steps}\n"
            "for test in tests/s390x/jit_loops/*.lua; do\n"
            '  [ -e "$test" ] || continue\n'
            '  if [ "$test" = "tests/s390x/jit_loops/explicit_next.lua" ]; then continue; fi\n'
            f'  {luajit_prefix(variant)} "$test"\n'
            "done\n"
            f"{perl_steps}\n"
        )
    if lane_id == "trace_tools":
        include_condition = " || ".join([f'[ \"$test\" = {shlex.quote(test)} ]' for test in TRACE_TOOLS_LUA_FILES])
        return (
            "set -euo pipefail\n"
            'export PATH="$PWD/src:$PATH"\n'
            "for test in tests/s390x/trace_tools/*.lua; do\n"
            '  [ -e "$test" ] || continue\n'
            f"  if ! ({include_condition}); then continue; fi\n"
            f'  {luajit_prefix(variant)} "$test"\n'
            "done\n"
        )
    if lane_id == "soak":
        if variant.jit == "off":
            return None
        skip_soak = shell_skip_condition(SOAK_FFI_LUA_FILES if variant.ffi == "off" else set())
        return (
            "set -euo pipefail\n"
            'export PATH="$PWD/src:$PATH"\n'
            "for test in tests/s390x/soak/*.lua; do\n"
            '  [ -e "$test" ] || continue\n'
            f"  {skip_soak}\n"
            f'  {luajit_prefix(variant)} "$test"\n'
            "done\n"
        )
    if lane_id == "jit_backend":
        skip_be = shell_skip_condition(JIT_BE_FFI_LUA_FILES if variant.ffi == "off" else set())
        return (
            "set -euo pipefail\n"
            'export PATH="$PWD/src:$PATH"\n'
            "for test in tests/s390x/jit_be/*.lua; do\n"
            '  [ -e "$test" ] || continue\n'
            f"  {skip_be}\n"
            f'  {luajit_prefix(variant)} "$test"\n'
            "done\n"
        )
    raise RunnerError(f"unknown validation lane: {lane_id}")


def perf_command(family_source_path: str, variant: Variant, output_jsonl: str) -> str:
    steps = [
        "set -euo pipefail",
        'export PATH="$PWD/src:$PATH"',
        f'export S390X_PERF_OUTPUT_JSONL={shlex.quote(output_jsonl)}',
        "export S390X_PERF_WARMUP=1",
        "export S390X_PERF_SAMPLES=5",
        f'export S390X_PERF_BENCH_FILE={shlex.quote(family_source_path)}',
    ]
    if variant.ffi == "on" and family_source_path in PERF_FFI_LUA_FILES:
        steps.append(f"CC={variant.compiler} sh tests/s390x/build_oracles.sh")
    steps.append(luajit_cmd(variant, family_source_path))
    return "\n".join(steps) + "\n"


def enrich_benchmark_record(record: dict[str, Any], target: dict[str, str], variant: Variant, family_id: str) -> dict[str, Any]:
    out = dict(record)
    build_config = native_build_config(target)
    out["target_arch"] = target["arch"]
    out["target_endianness"] = "big" if target["endianness"] == "big" else "little"
    out["target_system"] = target["system"]
    out["target_tuning"] = build_config["target_tuning"]
    if build_config.get("target_cflags"):
        out["target_cflags"] = build_config["target_cflags"]
    if build_config.get("host_cflags"):
        out["host_cflags"] = build_config["host_cflags"]
    for key, value in target.items():
        if key.startswith("platform_"):
            out[key] = value
    out["variant"] = variant.id
    out["compiler"] = variant.compiler
    out["mode"] = variant.mode
    out["jit"] = variant.jit
    out["ffi"] = variant.ffi
    out["build_style"] = variant.build_style
    out["family"] = family_id
    return out


def execute_step(
    command: str,
    *,
    cwd: pathlib.Path,
    stdout_path: pathlib.Path,
    stderr_path: pathlib.Path,
    timeout_sec: int,
    env: dict[str, str] | None = None,
) -> tuple[int, float, bool]:
    start = time.time()
    with stdout_path.open("w", encoding="utf-8") as stdout_fh, stderr_path.open("w", encoding="utf-8") as stderr_fh:
        proc = subprocess.Popen(
            ["/bin/bash", "-lc", command],
            cwd=str(cwd),
            env=env,
            text=True,
            stdout=stdout_fh,
            stderr=stderr_fh,
            start_new_session=True,
        )
        try:
            returncode = proc.wait(timeout=timeout_sec)
            return returncode, time.time() - start, False
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGTERM)
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
                proc.wait()
            stderr_fh.write(f"\n[MATRIX TIMEOUT] step exceeded {timeout_sec}s\n")
            stderr_fh.flush()
            return 124, time.time() - start, True


def execute_target(matrix_path: pathlib.Path, output_dir: pathlib.Path, target_label: str) -> int:
    matrix = load_matrix(matrix_path)
    target = detect_target_info_local()
    expected_ljarch = native_expected_ljarch(target)
    step_env = sanitized_build_env()
    output_dir.mkdir(parents=True, exist_ok=True)
    write_text(output_dir / "matrix.json", json.dumps(matrix, indent=2, sort_keys=True) + "\n")
    write_text(output_dir / "target.json", json.dumps(target, indent=2, sort_keys=True) + "\n")

    validation_results: list[dict[str, Any]] = []
    benchmark_records: list[dict[str, Any]] = []
    failures: list[dict[str, Any]] = []

    variant_sets = {
        name: [Variant(**entry) for entry in entries]
        for name, entries in matrix["variant_sets"].items()
    }

    all_variants = []
    seen_variant_ids = set()
    for lane in matrix["validation_lanes"]:
        for variant in variant_sets[lane["variants"]]:
            if variant.id not in seen_variant_ids:
                all_variants.append(variant)
                seen_variant_ids.add(variant.id)
    for family in matrix["performance_families"]:
        for variant in variant_sets[family["variants"]]:
            if variant.id not in seen_variant_ids:
                all_variants.append(variant)
                seen_variant_ids.add(variant.id)

    validation_root = output_dir / "validation"
    performance_root = output_dir / "performance"
    perf_rows: list[dict[str, Any]] = []
    build_status: dict[str, dict[str, Any]] = {}
    build_root = output_dir / "builds"
    for variant in all_variants:
        step_variant_env = variant_step_env(step_env, variant)
        variant_dir = build_root / variant.id
        stdout_path = variant_dir / "stdout.log"
        stderr_path = variant_dir / "stderr.log"
        variant_dir.mkdir(parents=True, exist_ok=True)
        exit_code, duration, timed_out = execute_step(
            build_command(variant, expected_ljarch, target),
            cwd=ROOT,
            stdout_path=stdout_path,
            stderr_path=stderr_path,
            timeout_sec=BUILD_TIMEOUT_SEC,
            env=step_variant_env,
        )
        build_status[variant.id] = {
            "variant": dataclasses.asdict(variant),
            "exit_code": exit_code,
            "duration_sec": round(duration, 6),
            "timed_out": timed_out,
            "stdout_path": str(stdout_path.relative_to(output_dir)),
            "stderr_path": str(stderr_path.relative_to(output_dir)),
        }
        if exit_code != 0:
            failures.append({"type": "build", "variant": variant.id})

        variant_lanes = [lane for lane in matrix["validation_lanes"] if variant.id in {v.id for v in variant_sets[lane["variants"]]}]
        for lane in variant_lanes:
            applicable, reason = applicability_matches(lane["applicability"], target, variant)
            result = {
                "lane": lane["id"],
                "variant": variant.id,
                "coverage_tags": lane["coverage_tags"],
                "source_paths": lane["source_paths"],
                "applicability": reason,
            }
            if not applicable:
                result["status"] = "not-applicable"
                validation_results.append(result)
                continue
            if exit_code != 0:
                result["status"] = "build-failed"
                validation_results.append(result)
                continue
            command = validation_command(lane["id"], variant)
            if command is None:
                result["status"] = "not-requested"
                validation_results.append(result)
                continue
            step_dir = validation_root / variant.id / lane["id"]
            step_stdout = step_dir / "stdout.log"
            step_stderr = step_dir / "stderr.log"
            step_dir.mkdir(parents=True, exist_ok=True)
            step_exit, step_duration, step_timed_out = execute_step(
                command,
                cwd=ROOT,
                stdout_path=step_stdout,
                stderr_path=step_stderr,
                timeout_sec=VALIDATION_TIMEOUT_SEC,
                env=step_variant_env,
            )
            result["status"] = "passed" if step_exit == 0 else ("timeout" if step_timed_out else "failed")
            result["exit_code"] = step_exit
            result["timed_out"] = step_timed_out
            result["duration_sec"] = round(step_duration, 6)
            result["stdout_path"] = str(step_stdout.relative_to(output_dir))
            result["stderr_path"] = str(step_stderr.relative_to(output_dir))
            validation_results.append(result)
            if step_exit != 0:
                failures.append({"type": "validation", "lane": lane["id"], "variant": variant.id})

        variant_families = [family for family in matrix["performance_families"] if variant.id in {v.id for v in variant_sets[family["variants"]]}]
        for family in variant_families:
            applicable, reason = applicability_matches(family["applicability"], target, variant)
            if not applicable:
                perf_rows.append(
                    {
                        "family": family["id"],
                        "variant": variant.id,
                        "workload": "*",
                        "scale": "*",
                        "status": "not-applicable",
                        "coverage_tags": family["coverage_tags"],
                        "source_path": family["source_path"],
                    }
                )
                continue
            if exit_code != 0:
                perf_rows.append(
                    {
                        "family": family["id"],
                        "variant": variant.id,
                        "workload": "*",
                        "scale": "*",
                        "status": "build-failed",
                        "coverage_tags": family["coverage_tags"],
                        "source_path": family["source_path"],
                    }
                )
                continue
            step_dir = performance_root / variant.id / family["id"]
            step_stdout = step_dir / "stdout.log"
            step_stderr = step_dir / "stderr.log"
            jsonl_path = step_dir / "benchmarks.jsonl"
            step_dir.mkdir(parents=True, exist_ok=True)
            command = perf_command(family["source_path"], variant, str(jsonl_path))
            step_exit, step_duration, step_timed_out = execute_step(
                command,
                cwd=ROOT,
                stdout_path=step_stdout,
                stderr_path=step_stderr,
                timeout_sec=PERFORMANCE_TIMEOUT_SEC,
                env=step_variant_env,
            )
            if step_exit != 0:
                perf_rows.append(
                    {
                        "family": family["id"],
                        "variant": variant.id,
                        "workload": "*",
                        "scale": "*",
                        "status": "timeout" if step_timed_out else "failed",
                        "coverage_tags": family["coverage_tags"],
                        "source_path": family["source_path"],
                    }
                )
                failures.append({"type": "performance", "family": family["id"], "variant": variant.id})
                continue
            step_records = []
            for raw in jsonl_path.read_text(encoding="utf-8").splitlines():
                if raw.strip():
                    step_records.append(json.loads(raw))
            if not step_records:
                perf_rows.append(
                    {
                        "family": family["id"],
                        "variant": variant.id,
                        "workload": "*",
                        "scale": "*",
                        "status": "no-metrics",
                        "coverage_tags": family["coverage_tags"],
                        "source_path": family["source_path"],
                    }
                )
                failures.append({"type": "performance", "family": family["id"], "variant": variant.id, "message": "no metrics"})
                continue
            for record in step_records:
                enriched = enrich_benchmark_record(record, target, variant, family["id"])
                enriched["status"] = "passed"
                enriched["coverage_tags"] = family["coverage_tags"]
                enriched["source_path"] = family["source_path"]
                enriched["duration_sec"] = round(step_duration, 6)
                benchmark_records.append(enriched)
                perf_rows.append(
                    {
                        "family": family["id"],
                        "variant": variant.id,
                        "workload": enriched.get("workload", ""),
                        "scale": enriched.get("scale", ""),
                        "status": "passed",
                        "median_runtime_sec": enriched.get("median_runtime_sec"),
                        "p95_runtime_sec": enriched.get("p95_runtime_sec"),
                        "coverage_tags": family["coverage_tags"],
                        "source_path": family["source_path"],
                    }
                )

    validation_results.sort(key=lambda row: (row["lane"], row["variant"]))
    perf_rows.sort(key=lambda row: (row["family"], row["variant"], row["workload"], row["scale"]))
    benchmark_records.sort(key=lambda row: (row.get("family", ""), row.get("variant", ""), row.get("workload", ""), row.get("scale", "")))
    write_text(output_dir / "validation" / "results.json", json.dumps(validation_results, indent=2, sort_keys=True) + "\n")
    write_text(output_dir / "performance" / "benchmarks.json", json.dumps(benchmark_records, indent=2, sort_keys=True) + "\n")
    write_text(output_dir / "performance" / "rows.json", json.dumps(perf_rows, indent=2, sort_keys=True) + "\n")

    summary = {
        "target_label": target_label,
        "target": target,
        "build_config": native_build_config(target),
        "matrix": matrix["name"],
        "started_at": now_utc(),
        "failures": failures,
        "builds": build_status,
        "validation_rows": len(validation_results),
        "performance_rows": len(perf_rows),
        "benchmark_records": len(benchmark_records),
    }
    write_text(output_dir / "manifest.json", json.dumps(summary, indent=2, sort_keys=True) + "\n")

    lines = [
        "# Upstream Validation And Performance Matrix",
        "",
        f"- Target label: `{target_label}`",
        f"- Architecture: `{target['arch']}`",
        f"- Endianness: `{'big' if target['endianness'] == 'big' else 'little'}`",
        f"- System: `{target['system']}`",
        f"- Target tuning: `{native_build_config(target)['target_tuning']}`",
        f"- Validation rows: `{len(validation_results)}`",
        f"- Performance rows: `{len(perf_rows)}`",
        f"- Benchmark records: `{len(benchmark_records)}`",
        f"- Failures: `{len(failures)}`",
        "",
        "## Validation",
        "",
        "| Lane | Variant | Status | Applicability | Coverage Tags | Source Paths |",
        "| --- | --- | --- | --- | --- | --- |",
    ]
    for row in validation_results:
        lines.append(
            f"| {row['lane']} | {row['variant']} | {row['status']} | {row['applicability']} | "
            f"{', '.join(row['coverage_tags'])} | {', '.join(row['source_paths'])} |"
        )
    lines.extend(
        [
            "",
            "## Performance",
            "",
            "| Family | Variant | Workload | Scale | Status | Median (s) | P95 (s) | Coverage Tags | Source Path |",
            "| --- | --- | --- | --- | --- | --- | --- | --- | --- |",
        ]
    )
    for row in perf_rows:
        median = row.get("median_runtime_sec")
        p95 = row.get("p95_runtime_sec")
        median_text = "n/a" if median is None else f"{float(median):.6f}"
        p95_text = "n/a" if p95 is None else f"{float(p95):.6f}"
        lines.append(
            f"| {row['family']} | {row['variant']} | {row['workload']} | {row['scale']} | {row['status']} | "
            f"{median_text} | {p95_text} | {', '.join(row['coverage_tags'])} | {row['source_path']} |"
        )
    write_text(output_dir / "report.md", "\n".join(lines) + "\n")
    return 0 if not failures else 1


def orchestrate_run(
    host: str | None,
    target_label: str,
    output_root: pathlib.Path,
    matrix_path: pathlib.Path,
    git_rev: str | None,
) -> int:
    output_root.mkdir(parents=True, exist_ok=True)
    target_dir = output_root / "targets" / target_label
    if target_dir.exists():
        raise RunnerError(f"target artifact dir already exists: {target_dir}")
    target_dir.mkdir(parents=True, exist_ok=False)

    try:
        matrix_rel = matrix_path.resolve().relative_to(ROOT.resolve())
    except ValueError as exc:
        raise RunnerError(f"matrix path must live under repo root: {matrix_path}") from exc

    snapshot_tar = output_root / f"{target_label}.tar"
    if git_rev:
        build_snapshot_tar_from_rev(git_rev, snapshot_tar, [ROOT / "tools" / "upstream_matrix_runner.py", ROOT / matrix_rel])
    else:
        snapshot_paths = git_snapshot_paths()
        build_snapshot_tar(snapshot_paths, snapshot_tar)

    workspace_name = f"luajit-upstream-matrix-{output_root.name}-{target_label}"
    if host:
        remote_workspace = f"/tmp/{workspace_name}"
        ssh(host, f"rm -rf {shlex.quote(remote_workspace)} && mkdir -p {shlex.quote(remote_workspace)}", check=True)
        with snapshot_tar.open("rb") as fh:
            proc = subprocess.run(
                ["ssh", "-o", "BatchMode=yes", host, f"bash -lc {shlex.quote(f'tar -xf - -C {remote_workspace}') }"],
                stdin=fh,
                text=False,
                capture_output=True,
            )
        if proc.returncode != 0:
            raise RunnerError(f"remote extract failed for {host}: {proc.stderr.decode('utf-8', errors='replace')}")
        exec_cmd = (
            f"cd {shlex.quote(remote_workspace)} && "
            "python3 tools/upstream_matrix_runner.py execute-target "
            f"--matrix {shlex.quote(str(matrix_rel))} "
            f"--output-dir {shlex.quote('.matrix-output')} "
            f"--target-label {shlex.quote(target_label)}"
        )
        proc = ssh(host, exec_cmd)
        write_text(target_dir / "remote.stdout.log", proc.stdout)
        write_text(target_dir / "remote.stderr.log", proc.stderr)
        fetch_proc = subprocess.run(
            [
                "ssh",
                "-o",
                "BatchMode=yes",
                host,
                f"bash -lc {shlex.quote(f'tar -C {remote_workspace}/.matrix-output -cf - .')}",
            ],
            text=False,
            capture_output=True,
        )
        if fetch_proc.returncode != 0:
            raise RunnerError(f"remote artifact fetch failed for {host}: {fetch_proc.stderr.decode('utf-8', errors='replace')}")
        with tempfile.NamedTemporaryFile() as tf:
            tf.write(fetch_proc.stdout)
            tf.flush()
            with tarfile.open(tf.name, "r:") as archive:
                archive.extractall(target_dir)
        ssh(host, f"rm -rf {shlex.quote(remote_workspace)}", check=False)
        snapshot_tar.unlink(missing_ok=True)
        return proc.returncode

    local_workspace = pathlib.Path(tempfile.mkdtemp(prefix=f"{workspace_name}-"))
    try:
        with tarfile.open(snapshot_tar, "r") as archive:
            archive.extractall(local_workspace)
        proc = run(
            [
                "python3",
                "tools/upstream_matrix_runner.py",
                "execute-target",
                "--matrix",
                str(matrix_rel),
                "--output-dir",
                ".matrix-output",
                "--target-label",
                target_label,
            ],
            cwd=local_workspace,
        )
        write_text(target_dir / "local.stdout.log", proc.stdout)
        write_text(target_dir / "local.stderr.log", proc.stderr)
        source_output = local_workspace / ".matrix-output"
        if source_output.exists():
            for child in source_output.iterdir():
                destination = target_dir / child.name
                if child.is_dir():
                    shutil.copytree(child, destination)
                else:
                    shutil.copy2(child, destination)
        return proc.returncode
    finally:
        snapshot_tar.unlink(missing_ok=True)
        shutil.rmtree(local_workspace, ignore_errors=True)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="cmd", required=True)

    run_target = sub.add_parser("run-target")
    run_target.add_argument("--target-label", required=True)
    run_target.add_argument("--host")
    run_target.add_argument("--output-root", type=pathlib.Path, required=True)
    run_target.add_argument("--matrix", type=pathlib.Path, default=MATRIX_FILE)
    run_target.add_argument("--git-rev")

    exec_target = sub.add_parser("execute-target")
    exec_target.add_argument("--matrix", type=pathlib.Path, default=MATRIX_FILE)
    exec_target.add_argument("--output-dir", type=pathlib.Path, required=True)
    exec_target.add_argument("--target-label", required=True)

    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.cmd == "execute-target":
        return execute_target(args.matrix, args.output_dir, args.target_label)
    return orchestrate_run(args.host, args.target_label, args.output_root, args.matrix, args.git_rev)


if __name__ == "__main__":
    raise SystemExit(main())
