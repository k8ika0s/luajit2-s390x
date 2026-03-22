#!/usr/bin/env python3
"""Emit tmux-safe shell commands for syncing a local patch to a remote host.

This helper exists for environments where the normal rsync/ssh path is blocked
locally, but a remote tmux session is still available. It avoids ad hoc remote
editing by producing a deterministic sequence of small `printf` appends that can
be pasted into a remote shell, then decodes and applies the exact local patch.
"""

from __future__ import annotations

import argparse
import base64
import pathlib
import shlex
import textwrap


def emit_commands(patch_path: pathlib.Path, remote_patch: str, chunk_size: int) -> list[str]:
    raw = patch_path.read_bytes()
    b64 = base64.b64encode(raw).decode("ascii")
    remote_b64 = f"{remote_patch}.b64"

    cmds = [f": > {shlex.quote(remote_b64)}"]
    for chunk in textwrap.wrap(b64, chunk_size):
        cmds.append(f"printf '%s' '{chunk}' >> {shlex.quote(remote_b64)}")
    cmds.extend(
        [
            "python3 - <<'PY'\n"
            "import base64, pathlib\n"
            f"src = pathlib.Path({remote_b64!r}).read_text()\n"
            f"pathlib.Path({remote_patch!r}).write_bytes(base64.b64decode(src))\n"
            f"print('patch-bytes', pathlib.Path({remote_patch!r}).stat().st_size)\n"
            "PY",
        ]
    )
    return cmds


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("patch", type=pathlib.Path, help="Local patch file to encode")
    parser.add_argument(
        "--remote-patch",
        default="/tmp/s390x-sync.patch",
        help="Remote patch path to write before applying",
    )
    parser.add_argument(
        "--chunk-size",
        type=int,
        default=3500,
        help="Base64 characters per printf append",
    )
    parser.add_argument(
        "--apply-dir",
        default="",
        help="Optional remote directory to run `git apply` in after decoding",
    )
    args = parser.parse_args()

    commands = emit_commands(args.patch, args.remote_patch, args.chunk_size)
    if args.apply_dir:
        commands.append(
            f"cd {shlex.quote(args.apply_dir)} && git apply {shlex.quote(args.remote_patch)} && git status --short"
        )

    for cmd in commands:
        print(cmd)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
