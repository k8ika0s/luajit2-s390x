#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HOST="${S390X_ISA_HOST:-kdz1}"
REMOTE_ROOT="${S390X_ISA_ROOT:-/root/luajit2-s390x-isa}"
REMOTE_CANON_ROOT="$REMOTE_ROOT/canon"
REMOTE_REPO_ROOT="$REMOTE_CANON_ROOT/repo"
REMOTE_RUNS_ROOT="$REMOTE_ROOT/runs"
REMOTE_ARCHIVE_ROOT="$REMOTE_ROOT/archive"
ARTIFACTS_ROOT="${S390X_ISA_ARTIFACTS_ROOT:-$ROOT/artifacts/s390x-isa}"
RUN_ID="${1:-bootstrap-$(date -u +%Y%m%dT%H%M%SZ)}"
LOCAL_RUN_DIR="$ARTIFACTS_ROOT/bootstrap/$RUN_ID"
REMOTE_BOOTSTRAP_ROOT="$REMOTE_ARCHIVE_ROOT/$RUN_ID"

mkdir -p "$LOCAL_RUN_DIR"

ssh -o BatchMode=yes "$HOST" "mkdir -p '$REMOTE_REPO_ROOT' '$REMOTE_RUNS_ROOT' '$REMOTE_ARCHIVE_ROOT' '$REMOTE_BOOTSTRAP_ROOT'"

git -C "$ROOT" ls-files -z \
  | COPYFILE_DISABLE=1 COPY_EXTENDED_ATTRIBUTES_DISABLE=1 \
    tar --disable-copyfile --no-mac-metadata --no-xattrs --no-acls --no-fflags \
      -C "$ROOT" --null -T - -cf - \
  | ssh -o BatchMode=yes "$HOST" "bash -lc 'rm -rf \"$REMOTE_REPO_ROOT\"/* \"$REMOTE_REPO_ROOT\"/.[!.]* \"$REMOTE_REPO_ROOT\"/..?* 2>/dev/null || true; tar -xf - -C \"$REMOTE_REPO_ROOT\"'"

ssh -o BatchMode=yes "$HOST" "bash -lc '$REMOTE_REPO_ROOT/tools/s390x/remote_bootstrap.sh \"$REMOTE_BOOTSTRAP_ROOT\" \"$REMOTE_REPO_ROOT\"'"

scp -rq "$HOST:$REMOTE_BOOTSTRAP_ROOT/artifacts/bootstrap/." "$LOCAL_RUN_DIR/"

ssh -o BatchMode=yes "$HOST" bash <<'EOF' >"$LOCAL_RUN_DIR/host-summary.txt"
set -euo pipefail
hostname
hostname -f 2>/dev/null || true
uname -a
awk -F: '/^Type:|^Model:/{gsub(/^[ \t]+/, "", $2); print $1 "=" $2}' /proc/sysinfo 2>/dev/null || true
cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null | sed 's/^/perf_event_paranoid=/' || true
EOF

cat >"$LOCAL_RUN_DIR/local-manifest.txt" <<EOF
host=$HOST
remote_root=$REMOTE_ROOT
remote_repo_root=$REMOTE_REPO_ROOT
remote_runs_root=$REMOTE_RUNS_ROOT
remote_archive_root=$REMOTE_ARCHIVE_ROOT
remote_bootstrap_root=$REMOTE_BOOTSTRAP_ROOT
local_run_dir=$LOCAL_RUN_DIR
EOF

printf '%s\n' "$LOCAL_RUN_DIR"
