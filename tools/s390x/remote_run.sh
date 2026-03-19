#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 4 ]; then
  echo "usage: $0 <step-name> <step-dir> <workdir> <command>" >&2
  exit 2
fi

STEP_NAME="$1"
STEP_DIR="$2"
WORKDIR="$3"
COMMAND="$4"

mkdir -p "$STEP_DIR"
cd "$WORKDIR"

export S390X_STEP_NAME="$STEP_NAME"
export S390X_STEP_DIR="$STEP_DIR"
export S390X_REPO_ROOT="$WORKDIR"
export PATH="$WORKDIR/src:$PATH"
export S390X_TIMEOUT_SEC="${S390X_TIMEOUT_SEC:-1800}"
ulimit -c unlimited || true

printf '%s\n' "$COMMAND" >"$STEP_DIR/command.txt"
env | sort >"$STEP_DIR/env.txt"
pwd >"$STEP_DIR/cwd.txt"

start_epoch="$(date +%s)"
set +e
if command -v timeout >/dev/null 2>&1; then
  timeout --preserve-status "$S390X_TIMEOUT_SEC" bash -lc "$COMMAND" >"$STEP_DIR/stdout.log" 2>"$STEP_DIR/stderr.log"
else
  bash -lc "$COMMAND" >"$STEP_DIR/stdout.log" 2>"$STEP_DIR/stderr.log"
fi
rc="$?"
set -e
end_epoch="$(date +%s)"
timed_out=false
if [ "$rc" -eq 124 ]; then
  timed_out=true
fi

mkdir -p "$STEP_DIR"
cat >"$STEP_DIR/metadata.json" <<EOF
{
  "step_name": "$STEP_NAME",
  "workdir": "$WORKDIR",
  "exit_code": $rc,
  "start_epoch": $start_epoch,
  "end_epoch": $end_epoch,
  "duration_sec": $((end_epoch - start_epoch)),
  "timeout_sec": $S390X_TIMEOUT_SEC,
  "timed_out": $timed_out
}
EOF

if [ "$rc" -ne 0 ]; then
  if [ -x "$WORKDIR/tools/s390x/collect.sh" ]; then
    mkdir -p "$STEP_DIR"
    "$WORKDIR/tools/s390x/collect.sh" "$STEP_DIR" "$WORKDIR/src/luajit" || true
  fi
fi

exit "$rc"
