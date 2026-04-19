#!/usr/bin/env python3
"""Find benchmark-shaped logic in production s390x sources.

This audit is intentionally conservative: it flags source references that make
production recorder/trace behavior depend on benchmark file names, synthetic
benchmark chunks, or current trace-size fingerprints. Those patterns are useful
for bring-up attribution, but they are not suitable for an upstreamable JIT
implementation.
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

PATTERNS = (
    ("perf_chunk", re.compile(r"@tests/s390x/perf/")),
    ("synthetic_bench_chunk", re.compile(r"@numeric_ops_(?:div|sqrt|min|max|abs)")),
    ("trace_ir_fingerprint", re.compile(r"\bJ->cur\.(?:nins|nsnap|mcloop)\s*==")),
    ("proto_line_fingerprint", re.compile(r"\bpt->(?:firstline|numline)\s*==")),
)


@dataclass(frozen=True)
class Finding:
    kind: str
    path: str
    line: int
    text: str


def scan_path(path: pathlib.Path) -> list[Finding]:
    findings: list[Finding] = []
    rel = path.relative_to(ROOT).as_posix()
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except FileNotFoundError:
        return findings
    for lineno, text in enumerate(lines, 1):
        for kind, pattern in PATTERNS:
            if pattern.search(text):
                findings.append(Finding(kind, rel, lineno, text.strip()))
    return findings


def group_findings(findings: list[Finding]) -> dict[str, list[Finding]]:
    grouped: dict[str, list[Finding]] = {}
    for finding in findings:
        grouped.setdefault(finding.kind, []).append(finding)
    return grouped


def emit_text(findings: list[Finding]) -> None:
    grouped = group_findings(findings)
    total = len(findings)
    print(f"benchmark-shaped source findings: {total}")
    for kind in sorted(grouped):
        print(f"\n## {kind}: {len(grouped[kind])}")
        for finding in grouped[kind]:
            print(f"{finding.path}:{finding.line}: {finding.text}")


def emit_json(findings: list[Finding]) -> None:
    print(json.dumps([finding.__dict__ for finding in findings], indent=2))


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Audit production s390x sources for benchmark-shaped fast paths.")
    parser.add_argument(
        "paths", nargs="*", default=list(DEFAULT_PATHS),
        help="Source paths to scan, relative to the repository root.")
    parser.add_argument("--json", action="store_true", help="Emit JSON.")
    parser.add_argument(
        "--fail-on-findings", action="store_true",
        help="Exit non-zero if any benchmark-shaped source finding is present.")
    args = parser.parse_args(argv)

    findings: list[Finding] = []
    for raw in args.paths:
        path = pathlib.Path(raw)
        if not path.is_absolute():
            path = ROOT / path
        if path.is_dir():
            for child in sorted(path.rglob("*")):
                if child.suffix in {".c", ".h", ".dasc"}:
                    findings.extend(scan_path(child))
        else:
            findings.extend(scan_path(path))

    if args.json:
        emit_json(findings)
    else:
        emit_text(findings)
    return 1 if args.fail_on_findings and findings else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
