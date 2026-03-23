#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GO_DIR="${ROOT_DIR}/GO"
CONFIG_PATH="${ROOT_DIR}/docs/configs/serve-ollama.json"

MODEL="${HARU_MODEL:-qwen2.5-coder:0.5b}"
DB_PATH="${HARU_DB_PATH:-/tmp/haruhidb-ollama.db}"
LISTEN="${HARU_LISTEN:-:8080}"
TIMEOUT="${HARU_TIMEOUT:-60s}"

if ! command -v go >/dev/null 2>&1; then
  echo "go is required but not found in PATH" >&2
  exit 1
fi
if ! command -v ollama >/dev/null 2>&1; then
  echo "ollama is required but not found in PATH" >&2
  exit 1
fi

echo "[1/4] Check Ollama service"
if command -v curl >/dev/null 2>&1; then
  if ! curl -fsS "http://127.0.0.1:11434/api/tags" >/dev/null 2>&1; then
    echo "      starting ollama serve in background (log: /tmp/ollama-serve.log)"
    nohup ollama serve >/tmp/ollama-serve.log 2>&1 &
    sleep 2
  else
    echo "      ollama service is running"
  fi
else
  echo "      curl not found, skip service probe"
fi

echo "[2/4] Pull model: ${MODEL}"
ollama pull "${MODEL}"

echo "[3/4] Start HaruhiDB"
echo "      db_path=${DB_PATH}"
echo "      listen=${LISTEN}"
echo "      timeout=${TIMEOUT}"
echo "      model=${MODEL}"

cd "${GO_DIR}"
echo "[4/4] Serving..."
exec go run ./cmd/haruhidb serve \
  --config "${CONFIG_PATH}" \
  --model "${MODEL}" \
  --db-path "${DB_PATH}" \
  --listen "${LISTEN}" \
  --timeout "${TIMEOUT}"
