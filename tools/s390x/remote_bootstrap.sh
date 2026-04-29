#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 2 ]; then
  echo "usage: $0 <run-root> <repo-root>" >&2
  exit 2
fi

RUN_ROOT="$1"
REPO_ROOT="$2"
BOOTSTRAP_DIR="$RUN_ROOT/artifacts/bootstrap"

mkdir -p "$BOOTSTRAP_DIR"

log() {
  printf '[bootstrap] %s\n' "$*"
}

snapshot_host() {
  uname -a >"$BOOTSTRAP_DIR/uname.txt"
  cat /etc/os-release >"$BOOTSTRAP_DIR/os-release.txt"
  lscpu >"$BOOTSTRAP_DIR/lscpu.txt"
  cat /proc/sysinfo >"$BOOTSTRAP_DIR/sysinfo.txt" 2>/dev/null || true
  {
    echo "gcc: $(gcc --version | head -n 1 2>/dev/null || true)"
    echo "clang: $(clang --version | head -n 1 2>/dev/null || true)"
    echo "ld: $(ld --version | head -n 1 2>/dev/null || true)"
    echo "as: $(as --version | head -n 1 2>/dev/null || true)"
    echo "gdb: $(gdb --version | head -n 1 2>/dev/null || true)"
    echo "perf: $(perf --version 2>/dev/null || true)"
    echo "perl: $(perl -v | head -n 2 | tail -n 1 2>/dev/null || true)"
    echo "python3: $(python3 --version 2>/dev/null || true)"
  } >"$BOOTSTRAP_DIR/tool-versions.txt"
  dnf list installed >"$BOOTSTRAP_DIR/package-inventory.txt" 2>&1 || true
}

log "ensuring baseline system packages"
: >"$BOOTSTRAP_DIR/dnf-install.log"
for pkg in gcc clang make git rsync python3 perl perl-Test-Harness curl gdb perf binutils elfutils elfutils-libelf-devel libunwind-devel ccache diffutils which; do
  dnf install -y "$pkg" >>"$BOOTSTRAP_DIR/dnf-install.log" 2>&1 || {
    printf 'warning: package unavailable: %s\n' "$pkg" >>"$BOOTSTRAP_DIR/dnf-install.log"
  }
done

perl_source="not-required"
perl_ready="true"
if ! perl -MIPC::Open3 -MFile::Temp -MCwd -MSymbol -MExporter -e 1 >/dev/null 2>&1; then
  perl_ready="false"
  perl_source="missing-core"
  cat >"$BOOTSTRAP_DIR/perl-warning.txt" <<EOF
Perl is present, but the core modules required by t/TestLJ.pm are not usable on
this host. The in-repo Perl harness no longer depends on external CPAN modules,
so this warning indicates a broken core Perl environment rather than a missing
test dependency package.
EOF
fi

printf '{\n  "perl_dependency_source": "%s",\n  "perl_ready": %s\n}\n' "$perl_source" "$perl_ready" >"$BOOTSTRAP_DIR/perl-deps.json"
snapshot_host

cat >"$BOOTSTRAP_DIR/bootstrap-manifest.txt" <<EOF
run_root=$RUN_ROOT
repo_root=$REPO_ROOT
perl_dependency_source=$perl_source
perl_ready=$perl_ready
EOF
