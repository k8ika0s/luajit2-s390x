#!/usr/bin/env python3
"""Summarize s390x semantic reducer substitution debt.

The benchmark-fastpath audit intentionally flags every source line. This helper
turns the same class of debt into an actionable burn-down ledger keyed by
recorder reducer function.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys
from dataclasses import asdict, dataclass


ROOT = pathlib.Path(__file__).resolve().parents[2]
RECORD = ROOT / "src/lj_record.c"

FUNC_RE = re.compile(
    r"^\s*static\s+int\s+(lj_record_s390x_[a-z0-9_]*(?:_sum|_accum4|_loop))\s*\("
)
DISPATCH_RE = re.compile(
    r"\b(lj_record_s390x_[a-z0-9_]*(?:_sum|_accum4|_loop))\s*\("
)
IRCALL_RE = re.compile(r"\bIRCALL_([A-Za-z0-9_]+)\b")


@dataclass
class ReducerDebt:
    family: str
    name: str
    definition_line: int
    dispatch_lines: list[int]
    ircalls: list[str]


def classify(name: str) -> str:
    if any(s in name for s in ("string", "concat", "miss_find", "prefix", "manual_find", "byte_scan")):
        return "string"
    if "strto" in name:
        return "be_helpers"
    if any(s in name for s in ("mod", "numeric", "minmax", "abs_parity", "scaled_tobit", "fpmod")):
        return "numeric_mod"
    if name.startswith("lj_record_s390x_logic_"):
        return "logic_low32"
    if "iterator" in name or "mixed_noffi" in name:
        return "iterator_mixed"
    if any(s in name for s in ("ffi", "mixed_width", "pair_loop", "buffer_fref")):
        return "ffi_cdata"
    if "large_immediate" in name:
        return "large_immediates"
    if "lower_frame" in name:
        return "lower_frame"
    if "route_reducer" in name:
        return "route_reducer"
    return "other"


def load_debt() -> list[ReducerDebt]:
    lines = RECORD.read_text(encoding="utf-8", errors="replace").splitlines()
    defs: dict[str, int] = {}
    bodies: dict[str, list[str]] = {}

    current: str | None = None
    for lineno, text in enumerate(lines, 1):
        match = FUNC_RE.search(text)
        if match:
            current = match.group(1)
            defs[current] = lineno
            bodies[current] = []
        elif current and text.startswith("static ") and not text.startswith("static LJ_AINLINE"):
            current = None
        if current:
            bodies[current].append(text)

    dispatch: dict[str, list[int]] = {name: [] for name in defs}
    for lineno, text in enumerate(lines, 1):
        stripped = text.lstrip()
        if FUNC_RE.search(text) or "lj_ir_call" in text:
            continue
        if stripped.startswith(("static ", "return ")):
            continue
        for match in DISPATCH_RE.finditer(text):
            name = match.group(1)
            if name in dispatch:
                dispatch[name].append(lineno)

    debts: list[ReducerDebt] = []
    for name, line in sorted(defs.items(), key=lambda item: item[1]):
        ircalls = sorted({m.group(1) for body_line in bodies[name] for m in IRCALL_RE.finditer(body_line)})
        debts.append(ReducerDebt(classify(name), name, line, dispatch.get(name, []), ircalls))
    return debts


def emit_markdown(debts: list[ReducerDebt]) -> None:
    print("# s390x Semantic Reducer Debt")
    print()
    print(f"Total reducer matcher definitions: {len(debts)}")
    print()
    by_family: dict[str, list[ReducerDebt]] = {}
    for debt in debts:
        by_family.setdefault(debt.family, []).append(debt)
    print("| Family | Count |")
    print("| --- | ---: |")
    for family in sorted(by_family):
        print(f"| {family} | {len(by_family[family])} |")
    print()
    for family in sorted(by_family):
        print(f"## {family}")
        print()
        print("| Reducer | Definition | Dispatch | Helper calls |")
        print("| --- | ---: | --- | --- |")
        for debt in by_family[family]:
            dispatch = ", ".join(str(line) for line in debt.dispatch_lines) or "-"
            ircalls = ", ".join(debt.ircalls) or "-"
            print(f"| `{debt.name}` | {debt.definition_line} | {dispatch} | `{ircalls}` |")
        print()


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", action="store_true", help="Emit JSON instead of Markdown.")
    args = parser.parse_args(argv)

    debts = load_debt()
    if args.json:
        print(json.dumps([asdict(debt) for debt in debts], indent=2))
    else:
        emit_markdown(debts)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
