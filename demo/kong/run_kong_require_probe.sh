#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

PRIMARY_HOST="${S390X_PRIMARY_HOST:-kdz}"
FALLBACK_HOST="${S390X_FALLBACK_HOST:-zkd0}"
REMOTE_ROOT="${S390X_KONG_RUNTIME_ROOT:-${1:-}}"

log() {
  printf '[kong-probe] %s\n' "$*"
}

pick_host() {
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

remote_script() {
  cat <<'EOF'
set -euo pipefail

ROOT="$1"
LOGS="${ROOT}/logs"
KONG_ROOT="${ROOT}/kong"
KONG_OPENRESTY_PREFIX="${ROOT}/openresty"
KONG_LUAROCKS_BIN="${ROOT}/luarocks/bin/luarocks"
KONG_LUA_NOJIT="${ROOT}/luajit-nojit.sh"
PROBE_LOG="${LOGS}/require-probe.log"
PROBE_STAGE="${LOGS}/require-probe-stage.txt"
PROBE_ENV="${LOGS}/require-probe-env.txt"
PROBE_CMD="${LOGS}/require-probe-command.sh"
LAST_OK="${LOGS}/require-probe-last-ok.txt"

mkdir -p "${LOGS}"

PATH="${KONG_OPENRESTY_PREFIX}/bin:${KONG_OPENRESTY_PREFIX}/nginx/sbin:${KONG_OPENRESTY_PREFIX}/luajit/bin:${ROOT}/luarocks/bin:${PATH}"
eval "$(LUAROCKS_CONFIG="${ROOT}/luarocks-config.lua" "${KONG_LUA_NOJIT}" "${KONG_LUAROCKS_BIN}" path)"
export LD_LIBRARY_PATH="${ROOT}/deps/lib:${KONG_OPENRESTY_PREFIX}/luajit/lib:${LD_LIBRARY_PATH:-}"
export KONG_LUA_PATH_OVERRIDE="${KONG_ROOT}/?.lua;${KONG_ROOT}/?/init.lua;"

{
  printf 'ROOT=%s\n' "${ROOT}"
  printf 'PATH=%s\n' "${PATH}"
  printf 'LD_LIBRARY_PATH=%s\n' "${LD_LIBRARY_PATH}"
  printf 'KONG_LUA_PATH_OVERRIDE=%s\n' "${KONG_LUA_PATH_OVERRIDE}"
} > "${PROBE_ENV}"

cat > "${PROBE_CMD}" <<CMD
cd "${KONG_ROOT}"
resty -e '<probe command recorded per-stage in ${PROBE_LOG}>'
CMD

: > "${PROBE_LOG}"
: > "${LAST_OK}"

run_probe() {
  local stage="$1"
  local chunk="$2"
  printf '%s\n' "${stage}" > "${PROBE_STAGE}"
  printf '[probe] stage=%s\n' "${stage}" | tee -a "${PROBE_LOG}"
  if (
    cd "${KONG_ROOT}"
    KONG_PROBE_STAGE="${stage}" \
    KONG_PROBE_CHUNK="${chunk}" \
    resty -e '
      require("jit").off()
      package.path = (os.getenv("KONG_LUA_PATH_OVERRIDE") or "") .. "./?.lua;./?/init.lua;" .. package.path
      local stage = os.getenv("KONG_PROBE_STAGE") or "unknown"
      local chunk = assert(os.getenv("KONG_PROBE_CHUNK"), "missing KONG_PROBE_CHUNK")
      local orig_require = require
      _G.require = function(name)
        io.stdout:write("enter ", name, "\n")
        local mod = orig_require(name)
        io.stdout:write("ok ", name, "\n")
        return mod
      end
      io.stdout:write("stage ", stage, "\n")
      assert(loadstring(chunk))()
    '
  ) >> "${PROBE_LOG}" 2>&1; then
    printf '%s\n' "${stage}" > "${LAST_OK}"
    return 0
  fi

  printf '[probe] failed stage=%s\n' "${stage}" | tee -a "${PROBE_LOG}"
  coredumpctl info --reverse --no-pager > "${LOGS}/require-probe-coredumpctl.txt" 2>&1 || true
  return 1
}

run_probe "resty.openssl.version" 'require("resty.openssl.version"); io.stdout:write("after require\n")'
run_probe "ngx.errlog" 'require("ngx.errlog"); io.stdout:write("after require\n")'
run_probe "kong.tools.dns" 'require("kong.tools.dns"); io.stdout:write("after require\n")'
run_probe "kong.cmd.init" 'require("kong.cmd.init"); io.stdout:write("after require\n")'
run_probe "kong.cmd.init.collectgarbage" 'require("kong.cmd.init"); collectgarbage(); io.stdout:write("after collectgarbage\n")'

printf '[probe] complete log=%s\n' "${PROBE_LOG}"
EOF
}

main() {
  if [ -z "${REMOTE_ROOT}" ]; then
    log "set S390X_KONG_RUNTIME_ROOT or pass the remote runtime root as the first argument"
    exit 1
  fi

  local host
  if ! host="$(pick_host)"; then
    log "no reachable remote s390x host"
    exit 1
  fi

  log "selected host ${host}"
  log "probing ${host}:${REMOTE_ROOT}"
  ssh "${host}" "bash -s -- '${REMOTE_ROOT}'" <<<"$(remote_script)"
}

main "$@"
