#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

python -m pip install -r "$ROOT_DIR/tests/requirements.txt"
python -m pytest -q "$ROOT_DIR/tests/contracts"
