#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CXX_SOURCE_DIR="${ROOT_DIR}/CXX"
CXX_BUILD_DIR="${CXX_BUILD_DIR:-${ROOT_DIR}/CXX/build}"
GO_SOURCE_DIR="${ROOT_DIR}/Go"

if ! command -v cmake >/dev/null 2>&1; then
  echo "cmake is required but not found in PATH" >&2
  exit 1
fi

if ! command -v ctest >/dev/null 2>&1; then
  echo "ctest is required but not found in PATH" >&2
  exit 1
fi

if ! command -v go >/dev/null 2>&1; then
  echo "go is required but not found in PATH" >&2
  exit 1
fi

echo "[1/4] Configure CMake build: ${CXX_BUILD_DIR}"
cmake -S "${CXX_SOURCE_DIR}" -B "${CXX_BUILD_DIR}" -DTEST=ON

echo "[2/4] Build C API shared library and C API test target"
cmake --build "${CXX_BUILD_DIR}" --target haruhidb_capi capi_test

echo "[3/4] Run C API ctest cases"
ctest --test-dir "${CXX_BUILD_DIR}" --output-on-failure -R "^CApiTest\\."

echo "[4/4] Run Go integration tests"
(
  cd "${GO_SOURCE_DIR}"
  go test ./...
)

echo "API gate passed."
