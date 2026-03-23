#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GO_DIR="${ROOT_DIR}/GO"
CXX_DIR="${ROOT_DIR}/CXX"
CXX_BUILD_DIR="${CXX_BUILD_DIR:-${ROOT_DIR}/CXX/build}"
CONFIG_PATH="${ROOT_DIR}/docs/configs/serve-web-ollama.json"

MODEL="${HARU_MODEL:-qwen2.5-coder:3b}"
BASE_URL="${HARU_BASE_URL:-http://127.0.0.1:11434}"
DB_PATH="${HARU_DB_PATH:-/tmp/haruhidb-web.db}"
LISTEN="${HARU_LISTEN:-:8080}"
TIMEOUT="${HARU_TIMEOUT:-60s}"
STREAM="${HARU_STREAM:-true}"
ALLOW_WRITE="${HARU_ALLOW_WRITE:-true}"
OPEN_BROWSER="${HARU_OPEN_BROWSER:-true}"

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

is_enabled() {
  local value="${1,,}"
  [[ "${value}" != "false" && "${value}" != "0" && "${value}" != "no" && "${value}" != "off" ]]
}

require_cmd go
require_cmd ollama
require_cmd cmake

ensure_ollama_service() {
  if command -v curl >/dev/null 2>&1; then
    if curl -fsS "${BASE_URL}/api/tags" >/dev/null 2>&1; then
      echo "[1/5] Ollama service is running"
      return
    fi
    echo "[1/5] Starting Ollama service in background"
    nohup ollama serve >/tmp/ollama-serve.log 2>&1 &
    sleep 2
    return
  fi

  echo "[1/5] curl not found, skip probe and continue"
}

ensure_capi() {
  local capi_dir="${ROOT_DIR}/CXX/build/src/capi"
  if [[ -f "${capi_dir}/libharuhidb_capi.so" || -f "${capi_dir}/libharuhidb_capi.dylib" ]]; then
    echo "[3/5] C API shared library already exists"
    return
  fi

  echo "[3/5] Building C API shared library"
  cmake -S "${CXX_DIR}" -B "${CXX_BUILD_DIR}" -DTEST=OFF -DEXAMPLE=OFF
  cmake --build "${CXX_BUILD_DIR}" --target haruhidb_capi
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

ensure_ollama_service

echo "[2/5] Pull model: ${MODEL}"
ollama pull "${MODEL}"

ensure_capi

echo "[4/5] Prepare runtime library path"
export LD_LIBRARY_PATH="${ROOT_DIR}/CXX/build/src/capi:${LD_LIBRARY_PATH:-}"
export DYLD_LIBRARY_PATH="${ROOT_DIR}/CXX/build/src/capi:${DYLD_LIBRARY_PATH:-}"

echo "[5/5] Start HaruhiDB Web"
echo "        UI: ${UI_URL}"
echo "        db_path=${DB_PATH}"
echo "        listen=${LISTEN}"
echo "        timeout=${TIMEOUT}"
echo "        model=${MODEL}"
echo "        base_url=${BASE_URL}"
echo "        stream=${STREAM}"
echo "        allow_write=${ALLOW_WRITE}"

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