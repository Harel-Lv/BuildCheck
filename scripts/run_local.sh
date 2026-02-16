#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
API_BUILD="$ROOT_DIR/BuildCheck/API/build"
ENGINE_DIR="$ROOT_DIR/BuildCheck/Engine"

is_port_busy() {
  local port="$1"
  if command -v ss >/dev/null 2>&1; then
    ss -ltn "( sport = :$port )" 2>/dev/null | tail -n +2 | grep -q .
    return $?
  fi
  if command -v netstat >/dev/null 2>&1; then
    netstat -ltn 2>/dev/null | awk '{print $4}' | grep -E "(^|:)$port$" -q
    return $?
  fi
  return 1
}

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

if is_port_busy 8080 || is_port_busy 9090; then
  echo "[run] ports 8080/9090 are already in use. Stop existing services first."
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
