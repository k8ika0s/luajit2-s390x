#!/usr/bin/env python3
"""Sync the tracked local tree into the canonical nongit remote mirror."""

from __future__ import annotations

import argparse
import json
import pathlib
import sys


THIS_DIR = pathlib.Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

import restamp_iterator_perf as restamp


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Sync tracked files into the canonical s390x remote mirror.",
    )
    parser.add_argument("--host", choices=restamp.HOST_LABELS, required=True)
    parser.add_argument(
        "--repo",
        help="Remote mirror path. Defaults to the canonical mirror for the selected host.",
    )
    parser.add_argument(
        "--verify-path",
        action="append",
        default=[],
        help="Relative path that must exist after sync. Repeat as needed.",
    )
    parser.add_argument(
        "--hash-path",
        action="append",
        default=[],
        help="Relative path whose remote SHA256 should be printed. Defaults to the verified paths when omitted.",
    )
    parser.add_argument(
        "--skip-sync",
        action="store_true",
        help="Only print and verify the resolved layout without copying files.",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Emit the result payload as JSON.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repo = args.repo or restamp.AUTHORITATIVE_REPOS[args.host]
    layout = restamp.ensure_remote_layout(args.host, repo)
    if not args.skip_sync:
        restamp.sync_tracked_files(args.host, repo)
    verification = restamp.verify_remote_paths(args.host, repo, args.verify_path)
    hash_paths = args.hash_path or list(args.verify_path)
    remote_hashes = restamp.remote_file_hashes(args.host, repo, hash_paths)
    payload = {
        "host": args.host,
        "repo": repo,
        "layout": layout,
        "synced": not args.skip_sync,
        "verified_paths": verification,
        "remote_hashes": remote_hashes,
    }
    if args.json:
        print(json.dumps(payload, indent=2, sort_keys=True))
    else:
        print(f"host: {args.host}")
        print(f"repo: {repo}")
        print(f"canon: {layout['canon']}")
        print(f"runs: {layout['runs']}")
        print(f"archive: {layout['archive']}")
        print(f"sync: {'skipped' if args.skip_sync else 'completed'}")
        for relpath, ok in verification.items():
            print(f"verify {relpath}: {'ok' if ok else 'missing'}")
        for relpath, digest in remote_hashes.items():
            print(f"sha256 {relpath}: {digest if digest else 'missing'}")
    if verification and not all(verification.values()):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
