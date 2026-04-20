#!/usr/bin/env python3
"""Find upstream-risky s390x fast paths in production sources.

This audit has two layers:

* identity: source references that make production recorder/trace behavior
  depend on benchmark file names, synthetic benchmark chunks, line numbers, or
  current trace-size fingerprints.
* semantic: default-on s390x recorder substitutions that recognize a loop
  family and replace it with a target helper/closed-form reducer.

The identity layer catches obvious benchmark-shaped code. The semantic layer is
the broader upstream-prep gate: even without file/chunk names, a production JIT
should not hide large benchmark-family rewrites in the core recorder unless the
optimization is justified as a generic IR/bytecode transformation.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys
from dataclasses import dataclass


ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_PATHS = (
    "src/lj_record.c",
    "src/lj_trace.c",
    "src/lj_trace.h",
    "src/lj_ircall.h",
)

IDENTITY_PATTERNS = (
    ("perf_chunk", re.compile(r"@tests/s390x/perf/")),
    ("synthetic_bench_chunk", re.compile(r"@numeric_ops_(?:div|sqrt|min|max|abs)")),
    ("trace_ir_fingerprint", re.compile(r"\bJ->cur\.(?:nins|nsnap|mcloop)\s*==")),
    ("proto_line_fingerprint", re.compile(r"\bpt->(?:firstline|numline)\s*==")),
)

SEMANTIC_PATTERNS = (
    (
        "semantic_reducer_definition",
        re.compile(
            r"^\s*static\s+int\s+lj_record_s390x_"
            r"(?:manual_find|[a-z0-9_]*(?:_sum|_accum4|_loop))\s*\("
        ),
    ),
    (
        "semantic_reducer_dispatch",
        re.compile(
            r"^\s*if\s*\(.*\blj_record_s390x_"
            r"(?:manual_find|[a-z0-9_]*(?:_sum|_accum4|_loop))\s*\("
        ),
    ),
    (
        "semantic_reducer_ircall",
        re.compile(
            r"\bIRCALL_(?:lj_trace_s390x_[a-z0-9_]*(?:_sum|_accum4|_loop)|"
            r"lj_str_[a-z0-9_]*(?:_sum|_cycle_sum|_lookup_sum|_slice_sum|_eq_sum))\b"
        ),
    ),
    (
        "semantic_reducer_callinfo",
        re.compile(
            r"_\(S390X,\s*(?:lj_trace_s390x_[a-z0-9_]*(?:_sum|_accum4|_loop)|"
            r"lj_str_[a-z0-9_]*(?:_sum|_cycle_sum|_lookup_sum|_slice_sum|_eq_sum))"
        ),
    ),
)

ALLOWLIST = (
    (
        "trace_ir_fingerprint",
        "src/lj_record.c",
        "J->cur.ir[ref-1].o != IR_PROF",
        "generic ITERN root-loop detection, not a benchmark matcher",
    ),
    (
        "trace_ir_fingerprint",
        "src/lj_record.c",
        "J->cur.snap[0].ref == J->cur.nins",
        "generic comparison snapshot PC fixup, not a benchmark matcher",
    ),
)


@dataclass(frozen=True)
class Finding:
    kind: str
    path: str
    line: int
    text: str


def is_allowlisted(kind: str, rel: str, text: str) -> bool:
    for allow_kind, allow_path, needle, _reason in ALLOWLIST:
        if kind == allow_kind and rel == allow_path and needle in text:
            return True
    return False


def active_patterns(scope: str) -> tuple[tuple[str, re.Pattern[str]], ...]:
    if scope == "identity":
        return IDENTITY_PATTERNS
    if scope == "semantic":
        return SEMANTIC_PATTERNS
    return IDENTITY_PATTERNS + SEMANTIC_PATTERNS


def scan_path(path: pathlib.Path, patterns: tuple[tuple[str, re.Pattern[str]], ...]) -> list[Finding]:
    findings: list[Finding] = []
    rel = path.relative_to(ROOT).as_posix()
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except FileNotFoundError:
        return findings
    for lineno, text in enumerate(lines, 1):
        for kind, pattern in patterns:
            if pattern.search(text) and not is_allowlisted(kind, rel, text):
                findings.append(Finding(kind, rel, lineno, text.strip()))
    return findings


def group_findings(findings: list[Finding]) -> dict[str, list[Finding]]:
    grouped: dict[str, list[Finding]] = {}
    for finding in findings:
        grouped.setdefault(finding.kind, []).append(finding)
    return grouped


def emit_text(findings: list[Finding], scope: str) -> None:
    grouped = group_findings(findings)
    total = len(findings)
    label = "benchmark-shaped" if scope == "identity" else "s390x upstream-risk"
    print(f"{label} source findings: {total}")
    for kind in sorted(grouped):
        print(f"\n## {kind}: {len(grouped[kind])}")
        for finding in grouped[kind]:
            print(f"{finding.path}:{finding.line}: {finding.text}")


def emit_json(findings: list[Finding]) -> None:
    print(json.dumps([finding.__dict__ for finding in findings], indent=2))


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Audit production s390x sources for upstream-risky fast paths.")
    parser.add_argument(
        "paths", nargs="*", default=list(DEFAULT_PATHS),
        help="Source paths to scan, relative to the repository root.")
    parser.add_argument("--json", action="store_true", help="Emit JSON.")
    parser.add_argument(
        "--scope",
        choices=("identity", "semantic", "upstream"),
        default="upstream",
        help=(
            "Audit scope. 'identity' is the old benchmark-name/line/fingerprint "
            "audit, 'semantic' catches closed-form reducer substitutions, and "
            "'upstream' runs both."
        ),
    )
    parser.add_argument(
        "--fail-on-findings", action="store_true",
        help="Exit non-zero if any source finding is present.")
    args = parser.parse_args(argv)

    patterns = active_patterns(args.scope)
    findings: list[Finding] = []
    for raw in args.paths:
        path = pathlib.Path(raw)
        if not path.is_absolute():
            path = ROOT / path
        if path.is_dir():
            for child in sorted(path.rglob("*")):
                if child.suffix in {".c", ".h", ".dasc"}:
                    findings.extend(scan_path(child, patterns))
        else:
            findings.extend(scan_path(path, patterns))

    if args.json:
        emit_json(findings)
    else:
        emit_text(findings, args.scope)
    return 1 if args.fail_on_findings and findings else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
