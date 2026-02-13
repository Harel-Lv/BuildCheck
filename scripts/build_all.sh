#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
API_DIR="$ROOT_DIR/BuildCheck/API"
ENGINE_DIR="$ROOT_DIR/BuildCheck/Engine"

echo "[build] API"
cmake -S "$API_DIR" -B "$API_DIR/build"
cmake --build "$API_DIR/build" --config Release

echo "[build] Engine"
cmake -S "$ENGINE_DIR" -B "$ENGINE_DIR/build"
cmake --build "$ENGINE_DIR/build" --config Release

echo "[build] done"
