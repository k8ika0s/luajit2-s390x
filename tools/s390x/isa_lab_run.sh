#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HOST="${S390X_ISA_HOST:-kdz1}"
ARTIFACTS_ROOT="${S390X_ISA_ARTIFACTS_ROOT:-$ROOT/artifacts/s390x-isa}"
REMOTE_ROOT="${S390X_ISA_ROOT:-/root/luajit2-s390x-isa}"

export LUAJIT_S390X_TEXT_PATTERN_MODE="${LUAJIT_S390X_TEXT_PATTERN_MODE:-span8}"
export LUAJIT_S390X_TEXT_TRANSFORM_MODE="${LUAJIT_S390X_TEXT_TRANSFORM_MODE:-generic}"

exec python3 "$ROOT/tools/s390x/driver.py" \
  --host "$HOST" \
  --hosts "$HOST" \
  --artifacts-root "$ARTIFACTS_ROOT" \
  --remote-base "$REMOTE_ROOT/runs" \
  --stream-label "isa-lab" \
  "$@"
