#!/usr/bin/env python3
"""Generate static s390x closure coverage inventory artifacts."""

from __future__ import annotations

import argparse
import json
import pathlib
import re
from typing import Dict, List, Optional, Tuple


RISK_TRACKS = {
    "numeric_helpers": [
        "asm_abs",
        "asm_fpdiv",
        "asm_fpmath",
        "asm_tobit",
        "asm_min",
        "asm_max",
    ],
    "reference_string_barrier": [
        "asm_fref",
        "asm_strref",
        "asm_obar",
        "asm_strto",
    ],
    "vm_runtime": [
        "vm_mod fast path",
        "compiled vararg function path",
    ],
    "feature_gated_debug": [
        "asm_prof",
    ],
}

RISK_POLICIES = {
    "asm_prof": "feature-gated",
}

BC_GROUP_PATTERNS = [
    ("comparison", {"ISLT", "ISGE", "ISLE", "ISGT", "ISEQV", "ISNEV", "ISEQS", "ISNES", "ISEQN", "ISNEN", "ISEQP", "ISNEP"}),
    ("unary", {"ISTC", "ISFC", "IST", "ISF", "ISTYPE", "ISNUM", "MOV", "NOT", "UNM", "LEN"}),
    ("arithmetic", {"ADDVN", "SUBVN", "MULVN", "DIVVN", "MODVN", "ADDNV", "SUBNV", "MULNV", "DIVNV", "MODNV", "ADDVV", "SUBVV", "MULVV", "DIVVV", "MODVV", "POW", "CAT"}),
    ("constants", {"KSTR", "KCDATA", "KSHORT", "KNUM", "KPRI", "KNIL"}),
    ("upvalues_functions", {"UGET", "USETV", "USETS", "USETN", "USETP", "UCLO", "FNEW"}),
    ("table_global_string", {"TNEW", "TDUP", "GGET", "GSET", "TGETV", "TGETS", "TGETB", "TGETR", "TSETV", "TSETS", "TSETB", "TSETM", "TSETR"}),
    ("calls_varargs", {"CALLM", "CALL", "CALLMT", "CALLT", "ITERC", "ITERN", "VARG", "ISNEXT"}),
    ("returns", {"RETM", "RET", "RET0", "RET1"}),
    ("loops_iterators", {"FORI", "JFORI", "FORL", "IFORL", "JFORL", "ITERL", "IITERL", "JITERL", "LOOP", "ILOOP", "JLOOP", "JMP"}),
    ("function_headers", {"FUNCF", "IFUNCF", "JFUNCF", "FUNCV", "IFUNCV", "JFUNCV", "FUNCC", "FUNCCW"}),
]

IR_STUB_PATTERN = re.compile(r"ASM_S390X_STUB_IR\(([^)]+)\)")


def ordered_known_risks() -> List[str]:
    ordered: List[str] = []
    for names in RISK_TRACKS.values():
        ordered.extend(names)
    return ordered


def write_json(path: pathlib.Path, payload: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def extract_macro_block(text: str, macro_name: str) -> str:
    start = text.find(macro_name)
    if start < 0:
        return text
    rest = text[start:]
    match = re.search(r"\n\n(?:typedef|/\*)", rest)
    if not match:
        return rest
    return rest[: match.start()]


def parse_bc(root: pathlib.Path) -> Dict[str, object]:
    text = (root / "src" / "lj_bc.h").read_text(encoding="utf-8")
    block = extract_macro_block(text, "#define BCDEF(_)")
    ops = re.findall(r"_\((\w+),", block)
    grouped: Dict[str, List[str]] = {}
    seen = set()
    for group, names in BC_GROUP_PATTERNS:
        grouped[group] = [name for name in ops if name in names]
        seen.update(grouped[group])
    grouped["other"] = [name for name in ops if name not in seen]
    return {"ops": ops, "groups": grouped}


def parse_ir(root: pathlib.Path) -> Dict[str, object]:
    ir_text = (root / "src" / "lj_ir.h").read_text(encoding="utf-8")
    asm_text = (root / "src" / "lj_asm.c").read_text(encoding="utf-8")
    s390x_text = (root / "src" / "lj_asm_s390x.h").read_text(encoding="utf-8")
    block = extract_macro_block(ir_text, "#define IRDEF(_)")
    ops = re.findall(r"_\((\w+),", block)
    asm_dispatch = {
        name: target
        for name, target in re.findall(r"case IR_(\w+):(?: case IR_\w+:)*\s+([a-zA-Z0-9_]+)\(as, ir", asm_text)
    }
    stubbed = sorted({name for name in IR_STUB_PATTERN.findall(s390x_text) if name != "name"})
    helper_fallbacks = []
    for line_no, line in enumerate(asm_text.splitlines(), start=1):
        if "IRCALL_" in line:
            match = re.search(r"IRCALL_[A-Za-z0-9_]+", line)
            if match:
                helper_fallbacks.append({"line": line_no, "call": match.group(0), "text": line.strip()})
    op_entries = []
    for op in ops:
        op_entries.append(
            {
                "op": op,
                "dispatch": asm_dispatch.get(op, ""),
                "s390x_stub": op in stubbed,
            }
        )
    return {
        "ops": op_entries,
        "stubbed_ops": stubbed,
        "helper_fallbacks": helper_fallbacks,
    }


def parse_vm(root: pathlib.Path) -> Dict[str, object]:
    text = (root / "src" / "vm_s390x.dasc").read_text(encoding="utf-8")
    bc_handlers = sorted(set(re.findall(r"case BC_(\w+):", text)))
    vm_labels = sorted(set(re.findall(r"->(vm_\w+):", text)))
    continuations = sorted(set(re.findall(r"->(cont_\w+):", text)))
    explicit_nyi = []
    for line_no, line in enumerate(text.splitlines(), start=1):
        if "NYI" in line or "TODO:" in line:
            explicit_nyi.append({"line": line_no, "text": line.strip()})
    return {
        "bc_handlers": bc_handlers,
        "vm_labels": vm_labels,
        "continuations": continuations,
        "explicit_nyi": explicit_nyi,
    }


def parse_remaining_stubs(root: pathlib.Path) -> Dict[str, object]:
    s390x_text = (root / "src" / "lj_asm_s390x.h").read_text(encoding="utf-8")
    vm_text = (root / "src" / "vm_s390x.dasc").read_text(encoding="utf-8")
    stub_lines = []
    for line_no, line in enumerate(s390x_text.splitlines(), start=1):
        if "ASM_S390X_STUB_IR(" in line and "#define" not in line:
            stub_lines.append({"line": line_no, "text": line.strip()})
    vm_risks = []
    for line_no, line in enumerate(vm_text.splitlines(), start=1):
        if "NYI" in line or "TODO:" in line:
            vm_risks.append({"line": line_no, "text": line.strip()})

    def find_track(name: str) -> str:
        for track, names in RISK_TRACKS.items():
            if name in names:
                return track
        return "other"

    def asm_status(name: str) -> Tuple[str, Optional[int], str]:
        stub_match = re.search(rf"ASM_S390X_STUB_IR\({re.escape(name)}\)", s390x_text)
        if stub_match:
            line_no = s390x_text[:stub_match.start()].count("\n") + 1
            return ("stubbed", line_no, f"src/lj_asm_s390x.h:{line_no}")
        impl_match = re.search(rf"\b(?:static\s+void|#define)\s+{re.escape(name)}\b", s390x_text)
        if impl_match:
            line_no = s390x_text[:impl_match.start()].count("\n") + 1
            return ("implemented", line_no, f"src/lj_asm_s390x.h:{line_no}")
        return ("missing", None, "")

    def vm_status(name: str) -> Tuple[str, List[Dict[str, object]]]:
        matches: List[Dict[str, object]] = []
        if name == "vm_mod fast path":
            patterns = [r"->vm_mod:", r"TODO: implement fast mod operation"]
        elif name == "compiled vararg function path":
            patterns = [r"compiled vararg functions", r"case BC_FUNCV", r"case BC_JFUNCV"]
        else:
            patterns = []
        for line_no, line in enumerate(vm_text.splitlines(), start=1):
            if any(re.search(pattern, line) for pattern in patterns):
                matches.append({"line": line_no, "text": line.strip()})
        if not matches:
            return ("implemented", matches)
        if any("NYI" in entry["text"] or "TODO:" in entry["text"] for entry in matches):
            return ("nyi", matches)
        return ("tracked", matches)

    risk_items = []
    tracks: Dict[str, List[dict]] = {track: [] for track in RISK_TRACKS}
    for name in ordered_known_risks():
        track = find_track(name)
        policy = RISK_POLICIES.get(name, "closure-blocker")
        if name.startswith("asm_"):
            status, line_no, source = asm_status(name)
            item = {
                "name": name,
                "track": track,
                "policy": policy,
                "kind": "asm",
                "status": status,
                "source": source,
            }
            if line_no is not None:
                item["line"] = line_no
        else:
            status, matches = vm_status(name)
            item = {
                "name": name,
                "track": track,
                "policy": policy,
                "kind": "vm",
                "status": status,
                "matches": matches,
            }
        risk_items.append(item)
        tracks.setdefault(track, []).append(item)

    return {
        "known_risks": ordered_known_risks(),
        "risk_tracks": RISK_TRACKS,
        "risk_items": risk_items,
        "tracks": tracks,
        "asm_stubs": stub_lines,
        "vm_nyi": vm_risks,
    }


def parse_helper_calls(root: pathlib.Path) -> Dict[str, object]:
    asm_text = (root / "src" / "lj_asm.c").read_text(encoding="utf-8")
    helper_map = []
    current = ""
    for line_no, line in enumerate(asm_text.splitlines(), start=1):
        func_match = re.match(r"static void (asm_\w+)\(", line)
        if func_match:
            current = func_match.group(1)
        if "IRCALL_" in line:
            for call in re.findall(r"IRCALL_[A-Za-z0-9_]+", line):
                helper_map.append(
                    {
                        "asm_func": current,
                        "call": call,
                        "line": line_no,
                        "text": line.strip(),
                    }
                )
    return {"helper_calls": helper_map}


def write_report(out_dir: pathlib.Path, bc: Dict[str, object], ir: Dict[str, object], vm: Dict[str, object], stubs: Dict[str, object], helper_calls: Dict[str, object]) -> None:
    stubbed_ops = ir["stubbed_ops"]
    asm_stub_lines = stubs["asm_stubs"]
    vm_nyi = stubs["vm_nyi"]
    risk_items = stubs["risk_items"]
    blocking_items = [
        item
        for item in risk_items
        if item["status"] in {"stubbed", "nyi", "missing", "tracked"} and item["policy"] != "feature-gated"
    ]
    lines = [
        "# s390x Closure Coverage Report",
        "",
        "## Summary",
        "",
        f"- Bytecode opcodes inventoried: `{len(bc['ops'])}`",
        f"- IR ops inventoried: `{len(ir['ops'])}`",
        f"- VM handlers inventoried: `{len(vm['bc_handlers'])}`",
        f"- VM labels inventoried: `{len(vm['vm_labels'])}`",
        f"- Explicit s390x asm stubs: `{len(asm_stub_lines)}`",
        f"- Stubbed IR ops: `{len(stubbed_ops)}`",
        f"- VM NYI/TODO markers: `{len(vm_nyi)}`",
        f"- Helper fallback references: `{len(helper_calls['helper_calls'])}`",
        f"- Active closure backlog items: `{len(blocking_items)}`",
        "",
        "## Closure Policy",
        "",
        "- Any exercised backend/runtime stub or NYI must be implemented before the closure stage is called green.",
        "- Any intentionally unreachable item must be backed by a guard test and documented here.",
        "- Generic helper fallback use is tracked explicitly so measured hotspots can be separated from correctness blockers.",
        "",
        "## Frozen Closure Backlog",
        "",
    ]
    for track, items in stubs["tracks"].items():
        lines.append(f"### `{track}`")
        lines.append("")
        for item in items:
            suffix = ""
            if item["kind"] == "asm" and item.get("source"):
                suffix = f" ({item['source']})"
            if item["kind"] == "vm" and item.get("matches"):
                first = item["matches"][0]
                suffix = f" (src/vm_s390x.dasc:{first['line']})"
            lines.append(
                f"- `{item['name']}`: `{item['status']}`"
                f", policy `{item['policy']}`{suffix}"
            )
        lines.append("")
    lines.extend(["", "## Current Stub Inventory", ""])
    if asm_stub_lines:
        for entry in asm_stub_lines:
            lines.append(f"- `src/lj_asm_s390x.h:{entry['line']}` {entry['text']}")
    else:
        lines.append("- No explicit `ASM_S390X_STUB_IR(...)` markers remain.")
    lines.extend(["", "## Current VM NYI Inventory", ""])
    if vm_nyi:
        for entry in vm_nyi:
            lines.append(f"- `src/vm_s390x.dasc:{entry['line']}` {entry['text']}")
    else:
        lines.append("- No explicit VM `NYI` or `TODO` markers remain.")
    lines.extend(["", "## Bytecode Families", ""])
    for group, ops in bc["groups"].items():
        lines.append(f"- `{group}`: `{len(ops)}` ops")
    lines.extend(["", "## Notes", ""])
    if any(item["call"] == "IRCALL_lj_vm_modi" for item in helper_calls["helper_calls"]):
        lines.append("- `IR_MOD -> IRCALL_lj_vm_modi` remains the first measured post-closure optimization target.")
    else:
        lines.append("- No `IRCALL_lj_vm_modi` fallback was found in the current source scan.")
    if any(item["name"] == "asm_tobit" and item["status"] == "implemented" for item in risk_items):
        lines.append("- `asm_tobit` is implemented and should no longer be treated as a closure stub.")
    if any(item["name"] == "asm_prof" for item in risk_items):
        lines.append("- `asm_prof` is tracked as a feature-gated/debug surface until a support-surface exercise test proves it closure-critical.")
    (out_dir / "report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate s390x closure coverage inventory artifacts.")
    parser.add_argument("--root", type=pathlib.Path, required=True)
    parser.add_argument("--out", type=pathlib.Path, required=True)
    args = parser.parse_args()

    root = args.root.resolve()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)

    bc = parse_bc(root)
    ir = parse_ir(root)
    vm = parse_vm(root)
    stubs = parse_remaining_stubs(root)
    helper_calls = parse_helper_calls(root)

    write_json(out / "bc-opcodes.json", bc)
    write_json(out / "ir-ops.json", ir)
    write_json(out / "vm-handlers.json", vm)
    write_json(out / "remaining-stubs.json", stubs)
    write_json(out / "helper-calls.json", helper_calls)
    write_report(out, bc, ir, vm, stubs, helper_calls)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
