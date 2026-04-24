# Recovery From `7c4610b5`

This note is kept only as a short historical marker.

The earlier recovery effort around commit `7c4610b5` mattered during bring-up
because it re-established a trusted source checkpoint after a period of noisy
validation. That recovery is no longer an active workflow and should not be
treated as the current operating procedure.

## Current Rule

Use these documents instead of this note for live work:

- [state-of-project.md](state-of-project.md) for current status,
- [runbook.md](runbook.md) for validation workflow,
- [perf.md](perf.md) for benchmark policy,
- [upstream-cleanup.md](upstream-cleanup.md) for remaining cleanup tasks.

## Why This File Still Exists

The recovery checkpoint still appears in branch history and older notes. This
short file preserves that context without keeping the old host-specific command
sequence or artifact log alive in the public docs.
