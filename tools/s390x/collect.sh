#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -lt 1 ]; then
  echo "usage: $0 <step-dir> [binary]" >&2
  exit 2
fi

STEP_DIR="$1"
BINARY="${2:-}"
mkdir -p "$STEP_DIR/diagnostics"

if [ -n "$BINARY" ] && [ -x "$BINARY" ]; then
  readelf -a "$BINARY" >"$STEP_DIR/diagnostics/readelf.txt" 2>&1 || true
  nm -an "$BINARY" >"$STEP_DIR/diagnostics/nm.txt" 2>&1 || true
  objdump -dr "$BINARY" >"$STEP_DIR/diagnostics/objdump.txt" 2>&1 || true
fi

if [ -f "$STEP_DIR/current_test.txt" ]; then
  failing_input="$(cat "$STEP_DIR/current_test.txt")"
  printf '%s\n' "$failing_input" >"$STEP_DIR/diagnostics/failing-input.txt"
  if [ -f "$failing_input" ]; then
    cp "$failing_input" "$STEP_DIR/diagnostics/" || true
  fi
fi

find . -maxdepth 3 -type f \( -name 'core' -o -name 'core.*' \) -print >"$STEP_DIR/diagnostics/core-files.txt" || true
if [ -s "$STEP_DIR/diagnostics/core-files.txt" ] && [ -n "$BINARY" ] && [ -x "$BINARY" ]; then
  while IFS= read -r core_file; do
    core_base="$(basename "$core_file")"
    gdb -batch \
      -ex "set pagination off" \
      -ex "thread apply all bt full" \
      "$BINARY" "$core_file" >"$STEP_DIR/diagnostics/${core_base}.gdb.txt" 2>&1 || true
  done <"$STEP_DIR/diagnostics/core-files.txt"
fi

exit_code=""
if [ -f "$STEP_DIR/metadata.json" ]; then
  exit_code="$(sed -n 's/.*"exit_code":[[:space:]]*\([0-9][0-9]*\).*/\1/p' "$STEP_DIR/metadata.json" | head -n1)"
fi

if [ -z "$exit_code" ] && [ -f "$STEP_DIR/stderr.log" ]; then
  if grep -qi "segmentation fault\|core dumped\|illegal instruction\|bus error\|trace/breakpoint trap\|aborted" "$STEP_DIR/stderr.log"; then
    exit_code=139
  fi
fi

if [ -n "$BINARY" ] && [ -x "$BINARY" ] && [ -f "$STEP_DIR/diagnostics/failing-input.txt" ]; then
  failing_input="$(cat "$STEP_DIR/diagnostics/failing-input.txt")"
  if [ -n "$exit_code" ] && [ "$exit_code" -ge 128 ] && [ -f "$failing_input" ]; then
    case "$failing_input" in
      *.lua)
        gdb -batch \
          -ex "set pagination off" \
          -ex run \
          -ex "thread apply all bt full" \
          --args "$BINARY" "$failing_input" \
          >"$STEP_DIR/diagnostics/replay.gdb.txt" 2>&1 || true
        ;;
    esac
  fi
fi
