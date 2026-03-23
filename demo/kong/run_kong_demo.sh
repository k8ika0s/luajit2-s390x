#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

PRIMARY_HOST="${S390X_PRIMARY_HOST:-kdz}"
FALLBACK_HOST="${S390X_FALLBACK_HOST:-zkd0}"
REMOTE_BASE="${S390X_KONG_REMOTE_BASE:-/root/luajit2-s390x}"
REMOTE_LABEL="${S390X_KONG_LABEL:-kong-demo-$(date -u +%Y%m%dT%H%M%SZ)}"
BASE_DEMO_ROOT="${S390X_BASE_DEMO_ROOT:-/root/luajit2-s390x/leadership-demo-20260322T205122Z}"
OPENRESTY_VERSION="${OPENRESTY_VERSION:-1.27.1.2}"
LUAROCKS_VERSION="${LUAROCKS_VERSION:-3.12.2}"
LIBYAML_VERSION="${LIBYAML_VERSION:-0.2.5}"
BUILD_KONG_OPENRESTY="${BUILD_KONG_OPENRESTY:-1}"
RUN_KONG_REQUIRE_PROBE="${RUN_KONG_REQUIRE_PROBE:-1}"
RUN_KONG_START="${RUN_KONG_START:-1}"
KONG_FORCE_JIT_OFF_IN_NGINX="${KONG_FORCE_JIT_OFF_IN_NGINX:-0}"
KONG_DELAYED_JIT_ON_IN_NGINX="${KONG_DELAYED_JIT_ON_IN_NGINX:-0}"
KONG_DELAYED_JIT_ON_SECS="${KONG_DELAYED_JIT_ON_SECS:-3}"
KONG_NGINX_RUN_AS_ROOT="${KONG_NGINX_RUN_AS_ROOT:-1}"
KONG_PROXY_PORT="${KONG_PROXY_PORT:-}"
KONG_ADMIN_PORT="${KONG_ADMIN_PORT:-}"

if [ -z "${KONG_PROXY_PORT}" ] || [ -z "${KONG_ADMIN_PORT}" ]; then
  read -r KONG_PROXY_PORT KONG_ADMIN_PORT <<EOF
$(python3 - <<'PY' "${REMOTE_LABEL}"
import hashlib
import sys

label = sys.argv[1]
offset = int(hashlib.sha256(label.encode()).hexdigest()[:6], 16) % 1000
print(18000 + offset, 19000 + offset)
PY
)
EOF
fi

log() {
  printf '[kong-demo] %s\n' "$*"
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

REMOTE_ROOT="$1"
OPENRESTY_VERSION="$2"
LUAROCKS_VERSION="$3"
LIBYAML_VERSION="$4"
BASE_DEMO_ROOT="$5"
BUILD_KONG_OPENRESTY="$6"
RUN_KONG_START="$7"
RUN_KONG_REQUIRE_PROBE="$8"
KONG_FORCE_JIT_OFF_IN_NGINX="$9"
KONG_DELAYED_JIT_ON_IN_NGINX="${10}"
KONG_DELAYED_JIT_ON_SECS="${11}"
KONG_NGINX_RUN_AS_ROOT="${12}"
KONG_PROXY_PORT="${13}"
KONG_ADMIN_PORT="${14}"

ROOT="${REMOTE_ROOT}"
REPO_ROOT="${ROOT}/repo"
BASE_OPENRESTY_PREFIX="${BASE_DEMO_ROOT}/openresty"
LOGS="${ROOT}/logs"
DEPS_PREFIX="${ROOT}/deps"
LIBYAML_TGZ="${ROOT}/libyaml-${LIBYAML_VERSION}.tar.gz"
LIBYAML_SRC="${ROOT}/libyaml-${LIBYAML_VERSION}"
ADA_VERSION=""
ADA_ZIP=""
ADA_SRC="${ROOT}/ada-singleheader"
ATC_ROUTER_REF=""
ATC_ROUTER_ROOT="${ROOT}/atc-router"
LUAROCKS_TGZ="${ROOT}/luarocks-${LUAROCKS_VERSION}.tar.gz"
LUAROCKS_SRC="${ROOT}/luarocks-${LUAROCKS_VERSION}"
KONG_ROOT="${ROOT}/kong"
KONG_OPENRESTY_PREFIX="${ROOT}/openresty"
OPENRESTY_TGZ="${ROOT}/openresty-${OPENRESTY_VERSION}.tar.gz"
OPENRESTY_SRC="${ROOT}/openresty-${OPENRESTY_VERSION}"
KONG_OPENRESTY_PATCH_DIR="${KONG_ROOT}/build/openresty/patches"
UPSTREAM_ROOT="${ROOT}/upstream"
KONG_PREFIX="${ROOT}/prefix"
KONG_ROCKS="${ROOT}/rocks"
KONG_LUAROCKS_BIN="${ROOT}/luarocks/bin/luarocks"
KONG_LUA_NOJIT="${ROOT}/luajit-nojit.sh"

mkdir -p "${ROOT}" "${LOGS}" "${DEPS_PREFIX}" "${UPSTREAM_ROOT}" "${KONG_PREFIX}"

cleanup_runtime() {
  pkill -f "python3 -m http.server 18090 --bind 127.0.0.1 --directory ${UPSTREAM_ROOT}" >/dev/null 2>&1 || true
  "${KONG_OPENRESTY_PREFIX}/nginx/sbin/nginx" -p "${KONG_PREFIX}" -c nginx.conf -s quit >/dev/null 2>&1 || true
}

trap cleanup_runtime EXIT

stage_set() {
  printf '%s\n' "$1" | tee "${LOGS}/stage-current.txt" >/dev/null
}

stage_log() {
  printf '[stage] %s\n' "$1" | tee -a "${LOGS}/stage-history.txt"
}

capture_failure() {
  local stage="$1"
  stage_set "${stage}"
  stage_log "${stage}:failed"
  coredumpctl info --reverse --no-pager >"${LOGS}/coredumpctl-info.txt" 2>&1 || true
}

run_require_probe() {
  local probe_log="${LOGS}/require-probe.log"
  local probe_stage="${LOGS}/require-probe-stage.txt"
  local probe_env="${LOGS}/require-probe-env.txt"
  local probe_cmd="${LOGS}/require-probe-command.sh"
  local last_ok="${LOGS}/require-probe-last-ok.txt"

  : > "${probe_log}"
  : > "${last_ok}"

  {
    printf 'ROOT=%s\n' "${ROOT}"
    printf 'KONG_ROOT=%s\n' "${KONG_ROOT}"
    printf 'PATH=%s\n' "${PATH}"
    printf 'LD_LIBRARY_PATH=%s\n' "${LD_LIBRARY_PATH}"
    printf 'KONG_LUA_PATH_OVERRIDE=%s\n' "${KONG_LUA_PATH_OVERRIDE}"
  } > "${probe_env}"

  cat > "${probe_cmd}" <<CMD
cd "${KONG_ROOT}"
resty -e '<probe command recorded in ${probe_log}>'
CMD

  run_probe_stage() {
    local stage="$1"
    local chunk="$2"
    printf '%s\n' "${stage}" > "${probe_stage}"
    printf '[probe] stage=%s\n' "${stage}" | tee -a "${probe_log}"
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
    ) >> "${probe_log}" 2>&1; then
      printf '%s\n' "${stage}" > "${last_ok}"
      return 0
    fi

    capture_failure "require_probe:${stage}"
    printf '[probe] failure stage=%s\n' "${stage}" | tee -a "${probe_log}"
    return 1
  }

  stage_set "require_probe"
  stage_log "require_probe:start"
  run_probe_stage "resty.openssl.version" 'require("resty.openssl.version"); io.stdout:write("after require\n")' || return 1
  run_probe_stage "ngx.errlog" 'require("ngx.errlog"); io.stdout:write("after require\n")' || return 1
  run_probe_stage "kong.tools.dns" 'require("kong.tools.dns"); io.stdout:write("after require\n")' || return 1
  run_probe_stage "kong.cmd.init" 'require("kong.cmd.init"); io.stdout:write("after require\n")' || return 1
  run_probe_stage "kong.cmd.init.collectgarbage" 'require("kong.cmd.init"); collectgarbage(); io.stdout:write("after collectgarbage\n")' || return 1
  stage_log "require_probe:ok"
}

prepare_kong_prefix() {
  (
    cd "${KONG_ROOT}"
    resty -e "require(\"jit\").off(); package.path=(os.getenv(\"KONG_LUA_PATH_OVERRIDE\") or \"\") .. \"./?.lua;./?/init.lua;\" .. package.path; require(\"kong.cmd.init\")(\"prepare\", { conf = \"${ROOT}/kong.conf\" })" >"${LOGS}/kong-prepare.txt" 2>&1
  )
}

patch_kong_nginx_bridge() {
  python3 - <<'PY' "${KONG_PREFIX}/nginx-kong.conf" "${KONG_PREFIX}/nginx.conf" "${KONG_FORCE_JIT_OFF_IN_NGINX}" "${KONG_DELAYED_JIT_ON_IN_NGINX}" "${KONG_DELAYED_JIT_ON_SECS}" "${KONG_NGINX_RUN_AS_ROOT}"
from pathlib import Path
import sys

kong_conf = Path(sys.argv[1])
main_conf = Path(sys.argv[2])
force_jit_off = sys.argv[3] == "1"
delayed_jit_on = sys.argv[4] == "1"
delayed_jit_secs = sys.argv[5]
run_as_root = sys.argv[6] == "1"

text = kong_conf.read_text()
if force_jit_off and "require('jit').off()" not in text:
    old1 = "init_by_lua_block {\n    Kong = require 'kong'\n    Kong.init()\n}"
    new1 = "init_by_lua_block {\n    require('jit').off()\n    Kong = require 'kong'\n    Kong.init()\n}"
    old2 = "init_worker_by_lua_block {\n    Kong.init_worker()\n}"
    new2 = "init_worker_by_lua_block {\n    require('jit').off()\n    Kong.init_worker()\n}"
    if old1 not in text or old2 not in text:
        raise SystemExit("expected init blocks not found in nginx-kong.conf")
    text = text.replace(old1, new1, 1).replace(old2, new2, 1)
    kong_conf.write_text(text)
elif delayed_jit_on and "ngx.timer.at(" not in text:
    old1 = "init_by_lua_block {\n    Kong = require 'kong'\n    Kong.init()\n}"
    new1 = "init_by_lua_block {\n    local jit = require('jit')\n    jit.off()\n    Kong = require 'kong'\n    Kong.init()\n}"
    old2 = "init_worker_by_lua_block {\n    Kong.init_worker()\n}"
    new2 = (
        "init_worker_by_lua_block {\n"
        "    local jit = require('jit')\n"
        "    jit.off()\n"
        "    Kong.init_worker()\n"
        f"    ngx.timer.at({delayed_jit_secs}, function() require('jit').on() end)\n"
        "}"
    )
    if old1 not in text or old2 not in text:
        raise SystemExit("expected init blocks not found in nginx-kong.conf")
    text = text.replace(old1, new1, 1).replace(old2, new2, 1)
    kong_conf.write_text(text)

main = main_conf.read_text()
if run_as_root and "user root root;" not in main:
    marker = "pid pids/nginx.pid;\n"
    if marker not in main:
        raise SystemExit("expected pid directive not found in nginx.conf")
    main = main.replace(marker, marker + "user root root;\n", 1)
    main_conf.write_text(main)
PY
}

printf '[remote-kong] root %s\n' "${ROOT}"
printf '[remote-kong] installing system packages\n'
stage_set "system_packages"
stage_log "system_packages:start"
dnf install -y gcc gcc-c++ make git patch perl curl tar gzip unzip which pcre pcre-devel zlib zlib-devel openssl openssl-devel readline-devel libxslt libxslt-devel gd gd-devel expat expat-devel python3 cargo rust >/dev/null
stage_log "system_packages:ok"

if [ ! -d "${KONG_ROOT}" ]; then
  printf '[remote-kong] cloning kong source\n'
  stage_set "clone_kong"
  git clone --depth 1 https://github.com/Kong/kong "${KONG_ROOT}" >"${LOGS}/git-kong.log" 2>&1
fi

python3 - <<'PY' "${KONG_ROOT}/kong/pdk/nginx.lua"
from pathlib import Path
import sys
path = Path(sys.argv[1])
text = path.read_text()
old = 'if arch == "x64" or arch == "arm64" then'
new = 'if arch == "x64" or arch == "arm64" or arch == "s390x" then'
if old not in text:
    raise SystemExit("expected arch stanza not found in kong/pdk/nginx.lua")
path.write_text(text.replace(old, new, 1))
PY

ADA_VERSION="$(sed -n 's/^ADA=\([^ #]*\).*$/\1/p' "${KONG_ROOT}/.requirements")"
ADA_ZIP="${ROOT}/ada-${ADA_VERSION}-singleheader.zip"
ATC_ROUTER_REF="$(sed -n 's/^ATC_ROUTER=\([^ #]*\).*$/\1/p' "${KONG_ROOT}/.requirements")"

printf '[remote-kong] building libyaml %s\n' "${LIBYAML_VERSION}"
stage_set "build_libyaml"
stage_log "build_libyaml:start"
curl -fL "https://github.com/yaml/libyaml/archive/refs/tags/${LIBYAML_VERSION}.tar.gz" -o "${LIBYAML_TGZ}" >"${LOGS}/curl-libyaml.log" 2>&1
rm -rf "${LIBYAML_SRC}"
tar -xzf "${LIBYAML_TGZ}" -C "${ROOT}"
(
  cd "${LIBYAML_SRC}"
  ./bootstrap >"${LOGS}/libyaml-bootstrap.log" 2>&1
  ./configure --prefix="${DEPS_PREFIX}" >"${LOGS}/libyaml-configure.log" 2>&1
  make -j"$(nproc)" >"${LOGS}/libyaml-make.log" 2>&1
  make install >"${LOGS}/libyaml-install.log" 2>&1
)
stage_log "build_libyaml:ok"

printf '[remote-kong] building ada %s\n' "${ADA_VERSION}"
stage_set "build_ada"
stage_log "build_ada:start"
curl -fL "https://github.com/ada-url/ada/releases/download/v${ADA_VERSION}/singleheader.zip" -o "${ADA_ZIP}" >"${LOGS}/curl-ada.log" 2>&1
rm -rf "${ADA_SRC}"
unzip -q "${ADA_ZIP}" -d "${ADA_SRC}"
mkdir -p "${KONG_ROCKS}/lib/lua/5.1"
g++ -std=c++17 -O2 -fPIC -shared "${ADA_SRC}/ada.cpp" -o "${KONG_ROCKS}/lib/lua/5.1/libada.so" >"${LOGS}/build-ada.log" 2>&1
stage_log "build_ada:ok"

printf '[remote-kong] building atc-router %s\n' "${ATC_ROUTER_REF}"
stage_set "build_atc_router"
stage_log "build_atc_router:start"
rm -rf "${ATC_ROUTER_ROOT}"
git clone https://github.com/Kong/atc-router "${ATC_ROUTER_ROOT}" >"${LOGS}/git-atc-router.log" 2>&1
(
  cd "${ATC_ROUTER_ROOT}"
  git fetch --depth 1 origin "${ATC_ROUTER_REF}" >"${LOGS}/git-atc-router-fetch.log" 2>&1
  git checkout "${ATC_ROUTER_REF}" >"${LOGS}/git-atc-router-checkout.log" 2>&1
  cargo build --release >"${LOGS}/build-atc-router.log" 2>&1
)
mkdir -p "${KONG_ROCKS}/share/lua/5.1/resty/router" "${KONG_ROCKS}/lib/lua/5.1"
cp -a "${ATC_ROUTER_ROOT}/lib/resty/router/." "${KONG_ROCKS}/share/lua/5.1/resty/router/"
cp -a "${ATC_ROUTER_ROOT}/target/release/libatc_router.so" "${KONG_ROCKS}/lib/lua/5.1/"
stage_log "build_atc_router:ok"

if [ "${BUILD_KONG_OPENRESTY}" = "1" ]; then
  printf '[remote-kong] cloning kong nginx modules\n'
  stage_set "clone_kong_nginx_modules"
  rm -rf "${ROOT}/lua-kong-nginx-module" "${ROOT}/lua-resty-lmdb" "${ROOT}/lua-resty-events"
  git clone --depth 1 --recurse-submodules https://github.com/Kong/lua-kong-nginx-module "${ROOT}/lua-kong-nginx-module" >"${LOGS}/git-kong-nginx-module.log" 2>&1
  git clone --depth 1 --recurse-submodules https://github.com/Kong/lua-resty-lmdb "${ROOT}/lua-resty-lmdb" >"${LOGS}/git-lmdb.log" 2>&1
  git clone --depth 1 --recurse-submodules https://github.com/Kong/lua-resty-events "${ROOT}/lua-resty-events" >"${LOGS}/git-events.log" 2>&1

  cd "${KONG_ROOT}"
  LUA_KONG_NGINX_MODULE="$(sed -n 's/.*LUA_KONG_NGINX_MODULE=\([^ #]*\).*/\1/p' .requirements)"
  LUA_RESTY_LMDB="$(sed -n 's/.*LUA_RESTY_LMDB=\([^ #]*\).*/\1/p' .requirements)"
  LUA_RESTY_EVENTS="$(sed -n 's/.*LUA_RESTY_EVENTS=\([^ #]*\).*/\1/p' .requirements)"
  cd "${ROOT}/lua-kong-nginx-module" && git fetch --depth 1 origin "${LUA_KONG_NGINX_MODULE}" >/dev/null 2>&1 && git checkout "${LUA_KONG_NGINX_MODULE}" >/dev/null 2>&1 && git submodule update --init --recursive >/dev/null 2>&1
  cd "${ROOT}/lua-resty-lmdb" && git fetch --depth 1 origin "${LUA_RESTY_LMDB}" >/dev/null 2>&1 && git checkout "${LUA_RESTY_LMDB}" >/dev/null 2>&1 && git submodule update --init --recursive >/dev/null 2>&1
  cd "${ROOT}/lua-resty-events" && git fetch --depth 1 origin "${LUA_RESTY_EVENTS}" >/dev/null 2>&1 && git checkout "${LUA_RESTY_EVENTS}" >/dev/null 2>&1 && git submodule update --init --recursive >/dev/null 2>&1

  printf '[remote-kong] building kong-capable openresty %s\n' "${OPENRESTY_VERSION}"
  stage_set "build_openresty"
  stage_log "build_openresty:start"
  curl -fL "https://openresty.org/download/openresty-${OPENRESTY_VERSION}.tar.gz" -o "${OPENRESTY_TGZ}" >"${LOGS}/curl-openresty.log" 2>&1
  rm -rf "${OPENRESTY_SRC}" "${KONG_OPENRESTY_PREFIX}"
  tar -xzf "${OPENRESTY_TGZ}" -C "${ROOT}"
  : > "${LOGS}/apply-openresty-patches.log"
  while IFS= read -r patch_file; do
    printf '[remote-kong] applying patch %s\n' "$(basename "${patch_file}")"
    (
      cd "${OPENRESTY_SRC}"
      patch -p1 < "${patch_file}"
    ) >>"${LOGS}/apply-openresty-patches.log" 2>&1
  done < <(find "${KONG_OPENRESTY_PATCH_DIR}" -maxdepth 1 -type f -name '*.patch' | sort)
  bundle_luajit="$(find "${OPENRESTY_SRC}/bundle" -maxdepth 1 -type d -name 'LuaJIT-*' | head -n 1)"
  bundle_name="$(basename "${bundle_luajit}")"
  rm -rf "${bundle_luajit}"
  cp -a "${REPO_ROOT}" "${OPENRESTY_SRC}/bundle/${bundle_name}"
  (
    cd "${OPENRESTY_SRC}/bundle/${bundle_name}"
    rm -f src/luajit src/*.o src/*.obj src/*.lib src/*.exp src/*.dll src/*.exe
    rm -f src/host/minilua src/host/buildvm src/host/*.o src/host/*.obj
    rm -f src/host/buildvm_arch.h src/lj_vm.S jit/vmdef.lua
  )
  (
    cd "${OPENRESTY_SRC}"
    ./configure \
      --prefix="${KONG_OPENRESTY_PREFIX}" \
      --with-pcre-jit \
      --with-http_ssl_module \
      --with-http_realip_module \
      --with-http_stub_status_module \
      --with-http_v2_module \
      --with-stream_realip_module \
      --with-stream_ssl_preread_module \
      --with-luajit-xcflags='-DLUAJIT_ENABLE_S390X_JIT' \
      --with-install-prefix="${ROOT}" \
      --add-module="${ROOT}/lua-kong-nginx-module" \
      --add-module="${ROOT}/lua-kong-nginx-module/stream" \
      --add-module="${ROOT}/lua-resty-lmdb" \
      --add-module="${ROOT}/lua-resty-events" \
      >"${LOGS}/configure-openresty.log" 2>&1
    make -j"$(nproc)" >"${LOGS}/make-openresty.log" 2>&1
    make install >"${LOGS}/install-openresty.log" 2>&1
  )
  mkdir -p "${KONG_OPENRESTY_PREFIX}/lualib"
  for module_root in \
    "${ROOT}/lua-kong-nginx-module" \
    "${ROOT}/lua-resty-lmdb" \
    "${ROOT}/lua-resty-events"; do
    for lua_tree in "${module_root}/lualib" "${module_root}/lib"; do
      if [ -d "${lua_tree}" ]; then
        cp -a "${lua_tree}/." "${KONG_OPENRESTY_PREFIX}/lualib/"
      fi
    done
  done
  stage_log "build_openresty:ok"
else
  KONG_OPENRESTY_PREFIX="${BASE_OPENRESTY_PREFIX}"
fi

printf '[remote-kong] building luarocks %s\n' "${LUAROCKS_VERSION}"
stage_set "build_luarocks"
stage_log "build_luarocks:start"
curl -fL "https://luarocks.org/releases/luarocks-${LUAROCKS_VERSION}.tar.gz" -o "${LUAROCKS_TGZ}" >"${LOGS}/curl-luarocks.log" 2>&1
rm -rf "${LUAROCKS_SRC}" "${ROOT}/luarocks"
tar -xzf "${LUAROCKS_TGZ}" -C "${ROOT}"
cat > "${KONG_LUA_NOJIT}" <<SH
#!/usr/bin/env bash
exec "${KONG_OPENRESTY_PREFIX}/luajit/bin/luajit-2.1.ROLLING" -joff "\$@"
SH
chmod +x "${KONG_LUA_NOJIT}"
(
  cd "${LUAROCKS_SRC}"
  ./configure \
    --prefix="${ROOT}/luarocks" \
    --with-lua="${KONG_OPENRESTY_PREFIX}/luajit" \
    --with-lua-include="${KONG_OPENRESTY_PREFIX}/luajit/include/luajit-2.1" \
    --lua-suffix="jit-2.1.ROLLING" \
    >"${LOGS}/luarocks-configure.log" 2>&1
  make build >"${LOGS}/luarocks-make.log" 2>&1
  make install >"${LOGS}/luarocks-install.log" 2>&1
)
stage_log "build_luarocks:ok"

printf '[remote-kong] installing kong runtime dependencies\n'
stage_set "install_kong_runtime_deps"
stage_log "install_kong_runtime_deps:start"
cat > "${ROOT}/luarocks-config.lua" <<CFG
rocks_trees = { { name = [[user]], root = [[${KONG_ROCKS}]] } }
variables = {
  LUA = [[${KONG_LUA_NOJIT}]],
  LUA_BINDIR = [[${KONG_OPENRESTY_PREFIX}/luajit/bin]],
  LUA_DIR = [[${KONG_OPENRESTY_PREFIX}/luajit]],
  LUA_INCDIR = [[${KONG_OPENRESTY_PREFIX}/luajit/include/luajit-2.1]],
}
lua_interpreter = "luajit"
lua_version = "5.1"
CFG
(
  cd "${KONG_ROOT}"
  KONG_VERSION="$(bash scripts/grep-kong-version.sh)"
  TEMP_ROCKSPEC="kong-${KONG_VERSION}-0.rockspec"
  cp kong-latest.rockspec "${TEMP_ROCKSPEC}"
  sed -i "s/^version = \".*\"/version = \"${KONG_VERSION}-0\"/" "${TEMP_ROCKSPEC}"
  LUA="${KONG_LUA_NOJIT}" \
  LUAROCKS_CONFIG="${ROOT}/luarocks-config.lua" \
  PKG_CONFIG_PATH="${DEPS_PREFIX}/lib/pkgconfig" \
  LD_LIBRARY_PATH="${DEPS_PREFIX}/lib:${KONG_OPENRESTY_PREFIX}/luajit/lib:${LD_LIBRARY_PATH:-}" \
  "${KONG_LUA_NOJIT}" "${KONG_LUAROCKS_BIN}" \
    make --only-deps "${TEMP_ROCKSPEC}" \
    LUA="${KONG_LUA_NOJIT}" \
    OPENSSL_DIR=/usr \
    CRYPTO_DIR=/usr \
    EXPAT_DIR=/usr \
    YAML_DIR="${DEPS_PREFIX}" \
    >"${LOGS}/luarocks-deps.log" 2>&1
)
stage_log "install_kong_runtime_deps:ok"

PATH="${KONG_OPENRESTY_PREFIX}/bin:${KONG_OPENRESTY_PREFIX}/nginx/sbin:${KONG_OPENRESTY_PREFIX}/luajit/bin:${ROOT}/luarocks/bin:${PATH}"
eval "$(LUAROCKS_CONFIG="${ROOT}/luarocks-config.lua" "${KONG_LUA_NOJIT}" "${KONG_LUAROCKS_BIN}" path)"
export LD_LIBRARY_PATH="${DEPS_PREFIX}/lib:${KONG_OPENRESTY_PREFIX}/luajit/lib:${LD_LIBRARY_PATH:-}"
export KONG_LUA_PATH_OVERRIDE="${KONG_ROOT}/?.lua;${KONG_ROOT}/?/init.lua;"

printf '[remote-kong] kong version smoke\n'
stage_set "kong_version_smoke"
stage_log "kong_version_smoke:start"
(
  cd "${KONG_ROOT}"
  resty -e 'require("jit").off(); package.path=(os.getenv("KONG_LUA_PATH_OVERRIDE") or "") .. "./?.lua;./?/init.lua;" .. package.path; require("kong.cmd.init")("version", {})' >"${LOGS}/kong-version.txt" 2>&1
)
cat "${LOGS}/kong-version.txt"
stage_log "kong_version_smoke:ok"

if [ "${RUN_KONG_REQUIRE_PROBE}" = "1" ]; then
  printf '[remote-kong] running staged require probe\n'
  if ! run_require_probe; then
    sed -n '1,160p' "${LOGS}/require-probe.log" || true
    sed -n '1,80p' "${LOGS}/coredumpctl-info.txt" || true
    exit 1
  fi
fi

if [ "${RUN_KONG_START}" = "1" ]; then
  printf '[remote-kong] preparing db-less config\n'
  stage_set "kong_start_prep"
  stage_log "kong_start_prep:start"
  cat > "${UPSTREAM_ROOT}/index.html" <<UP
native kong upstream on s390x
UP
  pkill -f "python3 -m http.server 18090" >/dev/null 2>&1 || true
  nohup python3 -m http.server 18090 --bind 127.0.0.1 --directory "${UPSTREAM_ROOT}" >"${LOGS}/upstream.log" 2>&1 &

  cat > "${ROOT}/kong.yml" <<YAML
_format_version: "3.0"
services:
  - name: demo
    url: http://127.0.0.1:18090
    routes:
      - name: demo-route
        paths:
          - /demo
YAML

  cat > "${ROOT}/kong.conf" <<CONF
database = off
declarative_config = ${ROOT}/kong.yml
prefix = ${KONG_PREFIX}
proxy_listen = 127.0.0.1:${KONG_PROXY_PORT}
admin_listen = 127.0.0.1:${KONG_ADMIN_PORT}
admin_gui_listen = off
nginx_worker_processes = 1
log_level = notice
anonymous_reports = off
request_debug = false
plugins = bundled
CONF
  stage_log "kong_start_prep:ok"

  printf '[remote-kong] preparing kong prefix\n'
  stage_set "kong_prepare"
  stage_log "kong_prepare:start"
  if ! prepare_kong_prefix; then
    capture_failure "kong_prepare"
    sed -n '1,120p' "${LOGS}/kong-prepare.txt" || true
    sed -n '1,80p' "${LOGS}/coredumpctl-info.txt" || true
    exit 1
  fi
  stage_log "kong_prepare:ok"

  if [ "${KONG_FORCE_JIT_OFF_IN_NGINX}" = "1" ] || [ "${KONG_DELAYED_JIT_ON_IN_NGINX}" = "1" ] || [ "${KONG_NGINX_RUN_AS_ROOT}" = "1" ]; then
    printf '[remote-kong] patching generated nginx config for demo stability\n'
    stage_set "kong_patch_nginx"
    stage_log "kong_patch_nginx:start"
    patch_kong_nginx_bridge
    stage_log "kong_patch_nginx:ok"
  fi

  printf '[remote-kong] starting kong nginx\n'
  stage_set "kong_start"
  stage_log "kong_start:start"
  pkill -f '/root/luajit2-s390x/kong-demo-.*/openresty/nginx/sbin/nginx .* -c nginx.conf' >/dev/null 2>&1 || true
  pkill -f "${KONG_OPENRESTY_PREFIX}/nginx/sbin/nginx -p ${KONG_PREFIX}" >/dev/null 2>&1 || true
  if (
    cd "${KONG_ROOT}"
    "${KONG_OPENRESTY_PREFIX}/nginx/sbin/nginx" -p "${KONG_PREFIX}" -c nginx.conf >"${LOGS}/kong-start.txt" 2>&1
  ); then
    printf '[remote-kong] verifying admin and proxy endpoints\n'
    stage_set "kong_verify_endpoints"
    curl -fsS "http://127.0.0.1:${KONG_ADMIN_PORT}/status" >"${LOGS}/admin-status.json"
    curl -fsS "http://127.0.0.1:${KONG_PROXY_PORT}/demo" >"${LOGS}/proxy-demo.txt"
    cat "${LOGS}/admin-status.json"
    printf '\n'
    cat "${LOGS}/proxy-demo.txt"
    printf '\n'
    stage_log "kong_verify_endpoints:ok"
  else
    printf '[remote-kong] kong start failed, capturing diagnostics\n'
    capture_failure "kong_start"
    sed -n '1,120p' "${LOGS}/kong-prepare.txt" || true
    sed -n '1,120p' "${LOGS}/kong-start.txt" || true
    sed -n '1,80p' "${LOGS}/coredumpctl-info.txt" || true
    exit 1
  fi
fi

printf '[remote-kong] logs: %s\n' "${LOGS}"
EOF
}

main() {
  local host
  if ! host="$(pick_host)"; then
    log "no reachable remote s390x host"
    exit 1
  fi

  local remote_root="${REMOTE_BASE}/${REMOTE_LABEL}"
  log "selected host ${host}"
  log "remote workdir ${remote_root}"
  log "ports proxy=${KONG_PROXY_PORT} admin=${KONG_ADMIN_PORT}"

  ssh "${host}" "rm -rf '${remote_root}' && mkdir -p '${remote_root}/repo'"
  log "syncing local repo to ${host}:${remote_root}/repo"
  rsync -az --delete \
    --exclude '.git' \
    --exclude '.cache' \
    --exclude 'artifacts' \
    --exclude 'dist' \
    --exclude 'deploy' \
    --exclude 'docs/s390x/artifacts' \
    "${REPO_ROOT}/" "${host}:${remote_root}/repo/"
  log "running remote kong bootstrap"
  ssh "${host}" "bash -s -- '${remote_root}' '${OPENRESTY_VERSION}' '${LUAROCKS_VERSION}' '${LIBYAML_VERSION}' '${BASE_DEMO_ROOT}' '${BUILD_KONG_OPENRESTY}' '${RUN_KONG_START}' '${RUN_KONG_REQUIRE_PROBE}' '${KONG_FORCE_JIT_OFF_IN_NGINX}' '${KONG_DELAYED_JIT_ON_IN_NGINX}' '${KONG_DELAYED_JIT_ON_SECS}' '${KONG_NGINX_RUN_AS_ROOT}' '${KONG_PROXY_PORT}' '${KONG_ADMIN_PORT}'" \
    <<<"$(remote_script)"
}

main "$@"
