#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GO_DIR="${ROOT_DIR}/GO"
CXX_DIR="${ROOT_DIR}/CXX"
CXX_BUILD_DIR="${CXX_BUILD_DIR:-${ROOT_DIR}/CXX/build}"
CONFIG_PATH="${HARU_CONFIG:-${ROOT_DIR}/docs/configs/serve-web-ollama.json}"

MODEL="${HARU_MODEL:-qwen2.5-coder:3b}"
BASE_URL="${HARU_BASE_URL:-http://127.0.0.1:11434}"
DB_PATH="${HARU_DB_PATH:-${ROOT_DIR}/haruhidb-web.db}"
LISTEN="${HARU_LISTEN:-:8080}"
TIMEOUT="${HARU_TIMEOUT:-120s}"
STREAM="${HARU_STREAM:-false}"
ALLOW_WRITE="${HARU_ALLOW_WRITE:-true}"
OPEN_BROWSER="${HARU_OPEN_BROWSER:-true}"

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

if is_local_ollama_url "${BASE_URL}"; then
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
exec go run ./cmd/haruhidb serve \
  --config "${CONFIG_PATH}" \
  --db-path "${DB_PATH}" \
  --listen "${LISTEN}" \
  --timeout "${TIMEOUT}" \
  --model "${MODEL}" \
  --base-url "${BASE_URL}" \
  --stream="${STREAM}" \
  --allow-write="${ALLOW_WRITE}"
