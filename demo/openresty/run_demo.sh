#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

PRIMARY_HOST="${S390X_PRIMARY_HOST:-kdz}"
FALLBACK_HOST="${S390X_FALLBACK_HOST:-zkd0}"
REMOTE_BASE="${S390X_DEMO_REMOTE_BASE:-/root/luajit2-s390x}"
REMOTE_LABEL="${S390X_DEMO_LABEL:-leadership-demo-$(date -u +%Y%m%dT%H%M%SZ)}"
OPENRESTY_VERSION="${OPENRESTY_VERSION:-1.27.1.2}"
OPENRESTY_URL="${OPENRESTY_URL:-https://openresty.org/download/openresty-${OPENRESTY_VERSION}.tar.gz}"
LUAJIT_XCFLAGS="${LUAJIT_XCFLAGS:--DLUAJIT_ENABLE_S390X_JIT}"
REMOTE_HTTP_PORT="${REMOTE_HTTP_PORT:-8080}"
REMOTE_WRK_CONNECTIONS="${REMOTE_WRK_CONNECTIONS:-32}"
REMOTE_WRK_THREADS="${REMOTE_WRK_THREADS:-2}"
REMOTE_WRK_DURATION="${REMOTE_WRK_DURATION:-10s}"
WARMUP_REQUESTS="${WARMUP_REQUESTS:-12}"
RUN_WRK="${RUN_WRK:-0}"
KEEP_RUNNING="${KEEP_RUNNING:-1}"
LOCAL_ONLY="${LOCAL_ONLY:-0}"

log() {
  printf '[leadership-demo] %s\n' "$*"
}

pick_host() {
  if [ "${LOCAL_ONLY}" = "1" ]; then
    return 1
  fi

  if ssh -o BatchMode=yes -o ConnectTimeout=8 "${PRIMARY_HOST}" "true" >/dev/null 2>&1; then
    printf '%s\n' "${PRIMARY_HOST}"
    return 0
  fi

  if ssh -o BatchMode=yes -o ConnectTimeout=8 "${FALLBACK_HOST}" "true" >/dev/null 2>&1; then
    printf '%s\n' "${FALLBACK_HOST}"
    return 0
  fi

  return 1
}

fallback_commands() {
  cat <<EOF
Fallback native LuaJIT proofs:

ssh ${PRIMARY_HOST} 'cd /root/luajit2-s390x/clean-loop-20260321 && src/luajit tests/s390x/jit_core/ffi_call_trace.lua'
ssh ${PRIMARY_HOST} 'cd /root/luajit2-s390x/clean-loop-20260321 && src/luajit tests/s390x/jit_core/ffi_ptr_call_trace.lua'
ssh ${PRIMARY_HOST} 'cd /root/luajit2-s390x/clean-loop-20260321 && src/luajit -e "local jit=require(\"jit\"); print(jit.status()); print(jit.arch)"'
EOF
}

sync_repo() {
  local host="$1"
  local remote_root="$2"

  log "syncing repo contents to ${host}:${remote_root}/repo"
  COPYFILE_DISABLE=1 git -C "${REPO_ROOT}" ls-files --cached --others --exclude-standard -z \
    | COPYFILE_DISABLE=1 tar -C "${REPO_ROOT}" --no-mac-metadata --no-xattrs --null -czf - -T - \
    | ssh "${host}" "rm -rf '${remote_root}/repo' && mkdir -p '${remote_root}/repo' && tar -xzf - -C '${remote_root}/repo'"
}

remote_script() {
  cat <<'EOF'
set -euo pipefail

REMOTE_ROOT="$1"
OPENRESTY_VERSION="$2"
OPENRESTY_URL="$3"
LUAJIT_XCFLAGS="$4"
REMOTE_HTTP_PORT="$5"
REMOTE_WRK_THREADS="$6"
REMOTE_WRK_CONNECTIONS="$7"
REMOTE_WRK_DURATION="$8"
WARMUP_REQUESTS="$9"
RUN_WRK="${10}"
KEEP_RUNNING="${11}"

REPO_ROOT="${REMOTE_ROOT}/repo"
RUNTIME_DIR="${REMOTE_ROOT}/runtime"
OPENRESTY_TARBALL="${REMOTE_ROOT}/openresty-${OPENRESTY_VERSION}.tar.gz"
OPENRESTY_SRC_DIR="${REMOTE_ROOT}/openresty-${OPENRESTY_VERSION}"
OPENRESTY_PREFIX="${REMOTE_ROOT}/openresty"
LOG_DIR="${REMOTE_ROOT}/logs"

mkdir -p "${REMOTE_ROOT}" "${LOG_DIR}"

printf '[remote-demo] stopping prior leadership-demo nginx instances\n'
pkill -f '/root/luajit2-s390x/leadership-demo-.*/openresty/nginx/sbin/nginx' >/dev/null 2>&1 || true
sleep 1

printf '[remote-demo] installing build tools\n'
dnf install -y gcc gcc-c++ make perl curl tar gzip which unzip pcre pcre-devel zlib zlib-devel openssl openssl-devel readline-devel libxslt libxslt-devel gd gd-devel perl-ExtUtils-Embed >/dev/null

printf '[remote-demo] downloading openresty %s\n' "${OPENRESTY_VERSION}"
rm -rf "${OPENRESTY_SRC_DIR}" "${OPENRESTY_PREFIX}" "${RUNTIME_DIR}"
mkdir -p "${RUNTIME_DIR}"
curl -fL "${OPENRESTY_URL}" -o "${OPENRESTY_TARBALL}"
tar -xzf "${OPENRESTY_TARBALL}" -C "${REMOTE_ROOT}"

bundle_luajit="$(find "${OPENRESTY_SRC_DIR}/bundle" -maxdepth 1 -type d -name 'LuaJIT-*' | head -n 1)"
bundle_name="$(basename "${bundle_luajit}")"
rm -rf "${bundle_luajit}"
cp -a "${REPO_ROOT}" "${OPENRESTY_SRC_DIR}/bundle/${bundle_name}"

printf '[remote-demo] configuring openresty with custom luajit bundle\n'
(
  cd "${OPENRESTY_SRC_DIR}"
  ./configure \
    --prefix="${OPENRESTY_PREFIX}" \
    --with-pcre-jit \
    --with-luajit-xcflags="${LUAJIT_XCFLAGS}" \
    >"${LOG_DIR}/configure.log" 2>&1
)

printf '[remote-demo] building openresty\n'
(
  cd "${OPENRESTY_SRC_DIR}"
  make -j"$(nproc)" >"${LOG_DIR}/make.log" 2>&1
  make install >"${LOG_DIR}/install.log" 2>&1
)

printf '[remote-demo] preparing runtime tree\n'
mkdir -p "${RUNTIME_DIR}/lua" "${RUNTIME_DIR}/logs"
cp "${REPO_ROOT}/demo/openresty/nginx.conf" "${RUNTIME_DIR}/nginx.conf"
cp "${REPO_ROOT}/demo/openresty/lua/"*.lua "${RUNTIME_DIR}/lua/"

printf '[remote-demo] starting openresty\n'
"${OPENRESTY_PREFIX}/nginx/sbin/nginx" -p "${RUNTIME_DIR}/" -c nginx.conf

sleep 1

printf '[remote-demo] smoke: /__demo\n'
curl -fsS "http://127.0.0.1:${REMOTE_HTTP_PORT}/__demo" | tee "${LOG_DIR}/demo.txt"

printf '\n[remote-demo] smoke: /__jit before warmup\n'
curl -fsS "http://127.0.0.1:${REMOTE_HTTP_PORT}/__jit" | tee "${LOG_DIR}/jit-before.json"

printf '\n[remote-demo] warmup: %s payment requests\n' "${WARMUP_REQUESTS}"
warm_body='{"amount":42,"country":"US","merchant":"grocery","token_age_s":7200}'
warmup_rc=0
for _ in $(seq 1 "${WARMUP_REQUESTS}"); do
  set +e
  curl -fsS -X POST "http://127.0.0.1:${REMOTE_HTTP_PORT}/payments/authorize" \
    -H 'content-type: application/json' \
    -d "${warm_body}" >/dev/null
  curl_rc=$?
  set -e
  if [ "${curl_rc}" -ne 0 ]; then
    warmup_rc="${curl_rc}"
    break
  fi
done

if [ "${warmup_rc}" -ne 0 ]; then
  printf '[remote-demo] gateway warmup failed with rc=%s; collecting fallback proof\n' "${warmup_rc}" | tee "${LOG_DIR}/gateway-fallback.txt"
  sed -n '1,160p' "${RUNTIME_DIR}/logs/error.log" | tee "${LOG_DIR}/gateway-error.log"
  fallback_luajit="${OPENRESTY_PREFIX}/luajit/bin/luajit-2.1.ROLLING"
  if [ -x "${fallback_luajit}" ]; then
    (
      cd "${REPO_ROOT}"
      "${fallback_luajit}" tests/s390x/jit_core/ffi_call_trace.lua
      printf 'fallback ffi_call_trace ok\n'
    ) | tee "${LOG_DIR}/fallback-ffi-call-trace.txt"
    (
      cd "${REPO_ROOT}"
      "${fallback_luajit}" tests/s390x/jit_core/ffi_ptr_call_trace.lua
      printf 'fallback ffi_ptr_call_trace ok\n'
    ) | tee "${LOG_DIR}/fallback-ffi-ptr-call-trace.txt"
  fi
  if [ "${KEEP_RUNNING}" != "1" ]; then
    "${OPENRESTY_PREFIX}/nginx/sbin/nginx" -p "${RUNTIME_DIR}/" -c nginx.conf -s quit >/dev/null 2>&1 || true
  fi
  exit 1
fi

printf '[remote-demo] sample low-risk request\n'
curl -i -fsS -X POST "http://127.0.0.1:${REMOTE_HTTP_PORT}/payments/authorize" \
  -H 'content-type: application/json' \
  -d '{"amount":42,"country":"US","merchant":"grocery","token_age_s":7200}' \
  | tee "${LOG_DIR}/approve-response.txt"

printf '\n[remote-demo] sample high-risk request\n'
curl -i -fsS -X POST "http://127.0.0.1:${REMOTE_HTTP_PORT}/payments/authorize" \
  -H 'content-type: application/json' \
  -d '{"amount":960,"country":"NG","merchant":"crypto","token_age_s":45}' \
  | tee "${LOG_DIR}/review-response.txt"

printf '\n[remote-demo] /__jit after warmup\n'
curl -fsS "http://127.0.0.1:${REMOTE_HTTP_PORT}/__jit" | tee "${LOG_DIR}/jit-after.json"

printf '\n[remote-demo] wrk compare\n'
if [ "${RUN_WRK}" = "1" ] && command -v wrk >/dev/null 2>&1; then
  cat >"${REMOTE_ROOT}/post.lua" <<WRK
wrk.method = "POST"
wrk.body   = '{"amount":42,"country":"US","merchant":"grocery","token_age_s":7200}'
wrk.headers["Content-Type"] = "application/json"
WRK
  wrk -t"${REMOTE_WRK_THREADS}" -c"${REMOTE_WRK_CONNECTIONS}" -d"${REMOTE_WRK_DURATION}" \
    -s "${REMOTE_ROOT}/post.lua" "http://127.0.0.1:${REMOTE_HTTP_PORT}/payments/authorize" \
    | tee "${LOG_DIR}/wrk-jit.txt"
  wrk -t"${REMOTE_WRK_THREADS}" -c"${REMOTE_WRK_CONNECTIONS}" -d"${REMOTE_WRK_DURATION}" \
    -s "${REMOTE_ROOT}/post.lua" "http://127.0.0.1:${REMOTE_HTTP_PORT}/payments/authorize_interp" \
    | tee "${LOG_DIR}/wrk-interp.txt"
else
  printf 'wrk disabled or not installed; skipping load comparison\n' | tee "${LOG_DIR}/wrk-skipped.txt"
fi

printf '\n[remote-demo] leadership commands\n'
printf 'curl -s http://127.0.0.1:%s/__demo\n' "${REMOTE_HTTP_PORT}"
printf 'curl -s http://127.0.0.1:%s/__jit\n' "${REMOTE_HTTP_PORT}"
printf "curl -i -X POST http://127.0.0.1:%s/payments/authorize -H 'content-type: application/json' -d '{\"amount\":42,\"country\":\"US\",\"merchant\":\"grocery\",\"token_age_s\":7200}'\n" "${REMOTE_HTTP_PORT}"
printf "curl -i -X POST http://127.0.0.1:%s/payments/authorize -H 'content-type: application/json' -d '{\"amount\":960,\"country\":\"NG\",\"merchant\":\"crypto\",\"token_age_s\":45}'\n" "${REMOTE_HTTP_PORT}"
printf "curl -i -X POST http://127.0.0.1:%s/payments/authorize_interp -H 'content-type: application/json' -d '{\"amount\":42,\"country\":\"US\",\"merchant\":\"grocery\",\"token_age_s\":7200}'\n" "${REMOTE_HTTP_PORT}"
printf "nginx stop command: %s -p %s/ -c nginx.conf -s quit\n" "${OPENRESTY_PREFIX}/nginx/sbin/nginx" "${RUNTIME_DIR}"

printf '\n[remote-demo] artifacts: %s\n' "${LOG_DIR}"
if [ "${KEEP_RUNNING}" != "1" ]; then
  "${OPENRESTY_PREFIX}/nginx/sbin/nginx" -p "${RUNTIME_DIR}/" -c nginx.conf -s quit >/dev/null 2>&1 || true
fi
EOF
}

main() {
  local host
  if ! host="$(pick_host)"; then
    log "no reachable remote s390x host. build script not executed."
    fallback_commands
    exit 1
  fi

  local remote_root="${REMOTE_BASE}/${REMOTE_LABEL}"
  log "selected host ${host}"
  log "remote workdir ${remote_root}"

  ssh "${host}" "rm -rf '${remote_root}' && mkdir -p '${remote_root}'"
  sync_repo "${host}" "${remote_root}"

  log "running remote build and demo flow"
  ssh "${host}" "bash -s -- '${remote_root}' '${OPENRESTY_VERSION}' '${OPENRESTY_URL}' '${LUAJIT_XCFLAGS}' '${REMOTE_HTTP_PORT}' '${REMOTE_WRK_THREADS}' '${REMOTE_WRK_CONNECTIONS}' '${REMOTE_WRK_DURATION}' '${WARMUP_REQUESTS}' '${RUN_WRK}' '${KEEP_RUNNING}'" \
    <<<"$(remote_script)"

  log "demo completed on ${host}"
  log "remote logs: ${remote_root}/logs"
}

main "$@"
