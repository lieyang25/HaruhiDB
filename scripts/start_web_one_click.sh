#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GO_DIR="${ROOT_DIR}/GO"
CXX_DIR="${ROOT_DIR}/CXX"
CXX_BUILD_DIR="${CXX_BUILD_DIR:-${ROOT_DIR}/CXX/build}"
CONFIG_PATH="${HARU_CONFIG:-${ROOT_DIR}/docs/configs/serve-web-ollama.json}"

OPEN_BROWSER="${HARU_OPEN_BROWSER:-true}"

DEFAULT_MODEL="qwen2.5-coder:3b"
DEFAULT_BASE_URL="http://127.0.0.1:11434"
DEFAULT_DB_PATH="${ROOT_DIR}/haruhidb-web.db"
DEFAULT_LISTEN=":8080"
DEFAULT_TIMEOUT="120s"
DEFAULT_STREAM="false"
DEFAULT_ALLOW_WRITE="true"

CFG_MODEL=""
CFG_BASE_URL=""
CFG_DB_PATH=""
CFG_LISTEN=""
CFG_TIMEOUT=""
CFG_STREAM=""
CFG_ALLOW_WRITE=""
CONFIG_PARSE_OK=false

if [[ -f "${CONFIG_PATH}" ]] && command -v jq >/dev/null 2>&1; then
  CFG_MODEL="$(jq -r '.ollama.model // empty' "${CONFIG_PATH}" 2>/dev/null || true)"
  CFG_BASE_URL="$(jq -r '.ollama.base_url // empty' "${CONFIG_PATH}" 2>/dev/null || true)"
  CFG_DB_PATH="$(jq -r '.common.db_path // empty' "${CONFIG_PATH}" 2>/dev/null || true)"
  CFG_LISTEN="$(jq -r '.serve.listen // empty' "${CONFIG_PATH}" 2>/dev/null || true)"
  CFG_TIMEOUT="$(jq -r '.common.timeout // empty' "${CONFIG_PATH}" 2>/dev/null || true)"
  CFG_STREAM="$(jq -r 'if .ollama.stream == null then "" else (.ollama.stream|tostring) end' "${CONFIG_PATH}" 2>/dev/null || true)"
  CFG_ALLOW_WRITE="$(jq -r 'if .common.allow_write == null then "" else (.common.allow_write|tostring) end' "${CONFIG_PATH}" 2>/dev/null || true)"
  CONFIG_PARSE_OK=true
fi

MODEL="${HARU_MODEL:-${CFG_MODEL:-${DEFAULT_MODEL}}}"
BASE_URL="${HARU_BASE_URL:-${CFG_BASE_URL:-${DEFAULT_BASE_URL}}}"
DB_PATH="${HARU_DB_PATH:-${CFG_DB_PATH:-${DEFAULT_DB_PATH}}}"
LISTEN="${HARU_LISTEN:-${CFG_LISTEN:-${DEFAULT_LISTEN}}}"
TIMEOUT="${HARU_TIMEOUT:-${CFG_TIMEOUT:-${DEFAULT_TIMEOUT}}}"
STREAM="${HARU_STREAM:-${CFG_STREAM:-${DEFAULT_STREAM}}}"
ALLOW_WRITE="${HARU_ALLOW_WRITE:-${CFG_ALLOW_WRITE:-${DEFAULT_ALLOW_WRITE}}}"

CAPI_DIR="${HARU_CAPI_DIR:-${ROOT_DIR}/CXX/build/src/capi}"
CAPI_RUNTIME_DIR="${HARU_CAPI_RUNTIME_DIR:-${CAPI_DIR}}"

if [[ -n "${HARU_UI_URL:-}" ]]; then
  UI_URL="${HARU_UI_URL}"
elif [[ "${LISTEN}" =~ ^:([0-9]+)$ ]]; then
  UI_URL="http://127.0.0.1:${BASH_REMATCH[1]}/ui"
else
  UI_URL="http://127.0.0.1:8080/ui"
fi

require_cmd() {
  local name="$1"
  if ! command -v "${name}" >/dev/null 2>&1; then
    echo "${name} is required but not found in PATH" >&2
    exit 1
  fi
}

is_set() {
  local name="$1"
  [[ -n "${!name+x}" ]]
}

resolve_db_path_for_serve() {
  local raw="$1"
  if [[ "${raw}" = /* ]]; then
    printf '%s\n' "${raw}"
    return
  fi
  printf '%s/%s\n' "${GO_DIR}" "${raw}"
}

is_local_ollama_url() {
  local url="${1,,}"
  [[ "${url}" =~ ^https?://(127\.0\.0\.1|localhost|\[::1\])(:[0-9]+)?(/|$) ]]
}

probe_ollama_tags() {
  local tags_url="${BASE_URL%/}/api/tags"
  if command -v curl >/dev/null 2>&1; then
    curl -fsS "${tags_url}" >/dev/null 2>&1
    return
  fi
  if command -v wget >/dev/null 2>&1; then
    wget -qO- "${tags_url}" >/dev/null 2>&1
    return
  fi
  return 1
}

is_enabled() {
  local value="${1,,}"
  [[ "${value}" != "false" && "${value}" != "0" && "${value}" != "no" && "${value}" != "off" ]]
}

ensure_db_path() {
  DB_PATH="$(resolve_db_path_for_serve "${DB_PATH}")"

  local db_dir
  db_dir="$(dirname "${DB_PATH}")"

  if [[ ! -d "${db_dir}" ]]; then
    mkdir -p "${db_dir}" || {
      echo "failed to create db directory: ${db_dir}" >&2
      exit 1
    }
  fi

  if [[ ! -w "${db_dir}" ]]; then
    echo "db directory is not writable: ${db_dir}" >&2
    exit 1
  fi

  local probe_file="${db_dir}/.haruhidb-write-test.$$"
  if ! : >"${probe_file}" 2>/dev/null; then
    echo "db directory write test failed: ${db_dir}" >&2
    exit 1
  fi
  rm -f "${probe_file}"

  DB_PATH="$(cd "${db_dir}" && pwd)/$(basename "${DB_PATH}")"
}

require_cmd go
if is_local_ollama_url "${BASE_URL}"; then
  require_cmd ollama
fi

ensure_ollama_service() {
  if [[ "${CONFIG_PARSE_OK}" != "true" ]] && ! is_set HARU_BASE_URL; then
    echo "[1/5] Skip Ollama preflight (cannot parse config; set HARU_BASE_URL to enable preflight)"
    return
  fi

  if probe_ollama_tags; then
    echo "[1/5] Ollama service is reachable: ${BASE_URL}"
    return
  fi

  if ! is_local_ollama_url "${BASE_URL}"; then
    echo "remote Ollama is not reachable: ${BASE_URL}" >&2
    echo "tip: verify network and run: curl ${BASE_URL%/}/api/tags" >&2
    exit 1
  fi

  if ollama list >/dev/null 2>&1; then
    echo "[1/5] Ollama service is running"
    return
  fi

  echo "[1/5] Starting Ollama service in background"
  nohup ollama serve >/tmp/ollama-serve.log 2>&1 &
  sleep 2
  if ! probe_ollama_tags; then
    echo "failed to start local Ollama service at ${BASE_URL}" >&2
    exit 1
  fi
}

has_capi_artifacts() {
  local dir="$1"
  [[ -f "${dir}/libharuhidb_capi.so" || -f "${dir}/libharuhidb_capi.dylib" ]]
}

ensure_capi() {
  if [[ -n "${HARU_CAPI_DIR:-}" ]]; then
    if has_capi_artifacts "${CAPI_DIR}"; then
      echo "[3/5] Using custom C API dir: ${CAPI_DIR}"
      return
    fi
    echo "HARU_CAPI_DIR is set but no C API artifacts found in: ${CAPI_DIR}" >&2
    exit 1
  fi

  if has_capi_artifacts "${CAPI_DIR}"; then
    echo "[3/5] C API library already exists"
    return
  fi

  require_cmd cmake
  echo "[3/5] Building C API shared library"
  cmake -S "${CXX_DIR}" -B "${CXX_BUILD_DIR}" -DTEST=OFF -DEXAMPLE=OFF
  cmake --build "${CXX_BUILD_DIR}" --target haruhidb_capi
}

write_db_hint() {
  local hint_path="${ROOT_DIR}/.haruhidb_db_path"
  printf '%s\n' "${DB_PATH}" >"${hint_path}"
}

open_ui_if_needed() {
  if ! is_enabled "${OPEN_BROWSER}"; then
    return
  fi

  if command -v xdg-open >/dev/null 2>&1; then
    (sleep 1; xdg-open "${UI_URL}" >/dev/null 2>&1 || true) &
    return
  fi
  if command -v open >/dev/null 2>&1; then
    (sleep 1; open "${UI_URL}" >/dev/null 2>&1 || true) &
  fi
}

ensure_db_path
write_db_hint
ensure_ollama_service

if [[ "${CONFIG_PARSE_OK}" != "true" ]] && ! is_set HARU_BASE_URL; then
  echo "[2/5] Skip model pull (cannot parse config; set HARU_MODEL/HARU_BASE_URL to enable)"
elif is_local_ollama_url "${BASE_URL}"; then
  echo "[2/5] Pull model: ${MODEL}"
  ollama pull "${MODEL}"
else
  echo "[2/5] Skip local model pull (remote Ollama): ${BASE_URL}"
fi

ensure_capi

echo "[4/5] Prepare runtime library path"
export LD_LIBRARY_PATH="${CAPI_RUNTIME_DIR}:${LD_LIBRARY_PATH:-}"
export DYLD_LIBRARY_PATH="${CAPI_RUNTIME_DIR}:${DYLD_LIBRARY_PATH:-}"

if [[ -n "${HARU_CGO_CFLAGS:-}" ]]; then
  export CGO_CFLAGS="${HARU_CGO_CFLAGS}"
else
  export CGO_CFLAGS="-I${ROOT_DIR}/CXX/src/include"
fi

if [[ -n "${HARU_CGO_LDFLAGS:-}" ]]; then
  export CGO_LDFLAGS="${HARU_CGO_LDFLAGS}"
else
  export CGO_LDFLAGS="-L${CAPI_DIR} -lharuhidb_capi"
fi

echo "[5/5] Start HaruhiDB Web"
echo "        UI: ${UI_URL}"
echo "        db_path=${DB_PATH}"
echo "        db_path_hint_file=${ROOT_DIR}/.haruhidb_db_path"
echo "        listen=${LISTEN}"
echo "        timeout=${TIMEOUT}"
echo "        model=${MODEL}"
echo "        base_url=${BASE_URL}"
echo "        stream=${STREAM}"
echo "        allow_write=${ALLOW_WRITE}"
echo "        capi_dir=${CAPI_DIR}"
echo "        config=${CONFIG_PATH}"

echo "        cgo_cflags=${CGO_CFLAGS}"
echo "        cgo_ldflags=${CGO_LDFLAGS}"

open_ui_if_needed

cd "${GO_DIR}"
SERVE_ARGS=(--config "${CONFIG_PATH}")

# Config-first: only pass CLI flags when user explicitly sets HARU_* overrides.
if is_set HARU_DB_PATH; then
  SERVE_ARGS+=(--db-path "${DB_PATH}")
elif [[ -z "${CFG_DB_PATH}" ]]; then
  # db_path is required; keep backward-compatible fallback only when config omits it.
  SERVE_ARGS+=(--db-path "${DB_PATH}")
fi
if is_set HARU_LISTEN; then
  SERVE_ARGS+=(--listen "${LISTEN}")
fi
if is_set HARU_TIMEOUT; then
  SERVE_ARGS+=(--timeout "${TIMEOUT}")
fi
if is_set HARU_MODEL; then
  SERVE_ARGS+=(--model "${MODEL}")
fi
if is_set HARU_BASE_URL; then
  SERVE_ARGS+=(--base-url "${BASE_URL}")
fi
if is_set HARU_STREAM; then
  SERVE_ARGS+=(--stream="${STREAM}")
fi
if is_set HARU_ALLOW_WRITE; then
  SERVE_ARGS+=(--allow-write="${ALLOW_WRITE}")
fi

exec go run ./cmd/haruhidb serve "${SERVE_ARGS[@]}"
