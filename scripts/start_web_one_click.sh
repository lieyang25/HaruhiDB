#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GO_DIR="${ROOT_DIR}/GO"
CXX_DIR="${ROOT_DIR}/CXX"
CXX_BUILD_DIR="${CXX_BUILD_DIR:-${ROOT_DIR}/CXX/build}"
CONFIG_PATH="${HARU_CONFIG:-${ROOT_DIR}/docs/configs/serve-web.json}"

OPEN_BROWSER="${HARU_OPEN_BROWSER:-true}"

CFG_LISTEN=""
CONFIG_PARSE_OK=false

if [[ -f "${CONFIG_PATH}" ]] && command -v jq >/dev/null 2>&1; then
  CFG_LISTEN="$(jq -r '.serve.listen // empty' "${CONFIG_PATH}" 2>/dev/null || true)"
  CONFIG_PARSE_OK=true
fi

DB_PATH="${HARU_DB_PATH:-}"
LISTEN="${HARU_LISTEN:-${CFG_LISTEN:-:8080}}"
TIMEOUT="${HARU_TIMEOUT:-}"
ALLOW_WRITE="${HARU_ALLOW_WRITE:-}"

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

is_enabled() {
  local value="${1,,}"
  [[ "${value}" != "false" && "${value}" != "0" && "${value}" != "no" && "${value}" != "off" ]]
}

is_set() {
  local name="$1"
  [[ -n "${!name+x}" ]]
}

has_capi_artifacts() {
  local dir="$1"
  [[ -f "${dir}/libharuhidb_capi.so" || -f "${dir}/libharuhidb_capi.dylib" ]]
}

ensure_capi() {
  if [[ -n "${HARU_CAPI_DIR:-}" ]]; then
    if has_capi_artifacts "${CAPI_DIR}"; then
      echo "[1/3] Using custom C API dir: ${CAPI_DIR}"
      return
    fi
    echo "HARU_CAPI_DIR is set but no C API artifacts found in: ${CAPI_DIR}" >&2
    exit 1
  fi

  if has_capi_artifacts "${CAPI_DIR}"; then
    echo "[1/3] C API library already exists"
    return
  fi

  require_cmd cmake
  echo "[1/3] Building C API shared library"
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

require_cmd go

ensure_capi

echo "[2/3] Prepare runtime library path"
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

echo "[3/3] Start HaruhiDB Web"
echo "        UI: ${UI_URL}"
if [[ -n "${DB_PATH}" ]]; then
  echo "        db_path(override)=${DB_PATH}"
else
  echo "        db_path=from config"
fi
echo "        listen=${LISTEN}"
if [[ -n "${TIMEOUT}" ]]; then
  echo "        timeout(override)=${TIMEOUT}"
else
  echo "        timeout=from config"
fi
if [[ -n "${ALLOW_WRITE}" ]]; then
  echo "        allow_write(override)=${ALLOW_WRITE}"
else
  echo "        allow_write=from config"
fi
echo "        capi_dir=${CAPI_DIR}"
echo "        config=${CONFIG_PATH}"
echo "        config_parse_with_jq=${CONFIG_PARSE_OK}"

echo "        cgo_cflags=${CGO_CFLAGS}"
echo "        cgo_ldflags=${CGO_LDFLAGS}"

open_ui_if_needed

cd "${GO_DIR}"
SERVE_ARGS=(--config "${CONFIG_PATH}")

# Config-first: only pass CLI flags when user explicitly sets HARU_* overrides.
if is_set HARU_DB_PATH && [[ -n "${DB_PATH}" ]]; then
  SERVE_ARGS+=(--db-path "${DB_PATH}")
fi
if is_set HARU_LISTEN && [[ -n "${LISTEN}" ]]; then
  SERVE_ARGS+=(--listen "${LISTEN}")
fi
if is_set HARU_TIMEOUT && [[ -n "${TIMEOUT}" ]]; then
  SERVE_ARGS+=(--timeout "${TIMEOUT}")
fi
if is_set HARU_ALLOW_WRITE && [[ -n "${ALLOW_WRITE}" ]]; then
  SERVE_ARGS+=(--allow-write="${ALLOW_WRITE}")
fi

exec go run ./cmd/haruhidb serve "${SERVE_ARGS[@]}"
