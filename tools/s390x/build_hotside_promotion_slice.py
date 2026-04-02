#!/usr/bin/env python3
"""Run the filtered hotside candidate across the first-enable throughput slice."""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys

THIS_DIR = pathlib.Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

import build_throughput_truth_pack as throughput


DEFAULT_CANDIDATE = "hotside_canon_share_uget_looproot"


def promotion_core_families(candidate: str) -> list[str]:
    families: list[str] = []
    for family, config in throughput.FAMILY_CONFIGS.items():
        scope = throughput.family_scope_summary(candidate, config)
        if scope["family_status"] == "promotion_core":
            families.append(family)
    return families


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", choices=throughput.restamp.HOST_LABELS, required=True)
    parser.add_argument("--candidate", default=DEFAULT_CANDIDATE)
    parser.add_argument("--include-baseline", action="store_true")
    parser.add_argument("--family", action="append", dest="families")
    parser.add_argument("--output-root", type=pathlib.Path, default=throughput.DEFAULT_OUTPUT_ROOT)
    parser.add_argument("--pin-core", type=int, default=throughput.restamp.DEFAULT_PIN_CORE)
    parser.add_argument("--samples", type=int, default=throughput.restamp.DEFAULT_SAMPLES)
    parser.add_argument("--warmup", type=int, default=throughput.restamp.DEFAULT_WARMUP)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    available = promotion_core_families(args.candidate)
    families = args.families or available
    unknown = [family for family in families if family not in available]
    if unknown:
        print(
            "Requested families are not in the first-enable slice for "
            f"{args.candidate}: {', '.join(unknown)}",
            file=sys.stderr,
        )
        return 2

    candidates = ["baseline", args.candidate] if args.include_baseline else [args.candidate]
    helper = THIS_DIR / "build_throughput_truth_pack.py"
    for candidate in candidates:
        for family in families:
            cmd = [
                sys.executable,
                str(helper),
                "--family",
                family,
                "--host",
                args.host,
                "--candidate",
                candidate,
                "--output-root",
                str(args.output_root),
                "--pin-core",
                str(args.pin_core),
                "--samples",
                str(args.samples),
                "--warmup",
                str(args.warmup),
            ]
            print(f"==> {' '.join(cmd)}", flush=True)
            result = subprocess.run(cmd)
            if result.returncode != 0:
                return result.returncode
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
