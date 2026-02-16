#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
API_BUILD="$ROOT_DIR/BuildCheck/API/build"
ENGINE_DIR="$ROOT_DIR/BuildCheck/Engine"

if [[ -z "${ENGINE_API_KEY:-}" ]]; then
  ENGINE_API_KEY="local-dev-engine-key-please-change-123456"
  export ENGINE_API_KEY
  echo "[run] ENGINE_API_KEY not set; using local dev fallback key."
fi

if [[ ! -x "$API_BUILD/api_server" && ! -x "$API_BUILD/Release/api_server.exe" ]]; then
  echo "[run] API binary not found. Run scripts/build_all.sh first."
  exit 1
fi

if ! python -c "import fastapi, uvicorn, ultralytics" >/dev/null 2>&1; then
  echo "[run] Engine Python dependencies missing. Run:"
  echo "      python -m pip install -r $ENGINE_DIR/requirements.txt"
  exit 1
fi

echo "[run] starting Engine (:9090)"
(
  cd "$ENGINE_DIR"
  python -m uvicorn engine_service:app --host 0.0.0.0 --port 9090
) &
ENGINE_PID=$!

echo "[run] starting API (:8080)"
if [[ -x "$API_BUILD/api_server" ]]; then
  "$API_BUILD/api_server" &
else
  "$API_BUILD/Release/api_server.exe" &
fi
API_PID=$!

trap 'echo "[run] stopping"; kill $API_PID $ENGINE_PID' EXIT

echo "[run] services up"
echo "[run] client file: $ROOT_DIR/BuildCheck/Client/html/index.html"
wait
