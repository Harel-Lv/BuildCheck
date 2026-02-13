from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[2]


@pytest.mark.skipif(
    os.getenv("RUN_LIVE_E2E", "0") != "1",
    reason="Set RUN_LIVE_E2E=1 and start API+Engine before running this test.",
)
def test_live_e2e_smoke():
    script = ROOT / "scripts" / "smoke_e2e_live.py"
    proc = subprocess.run([sys.executable, str(script)], cwd=str(ROOT), capture_output=True, text=True)
    if proc.returncode != 0:
        raise AssertionError(
            f"live e2e smoke failed (code={proc.returncode})\n"
            f"stdout:\n{proc.stdout}\n"
            f"stderr:\n{proc.stderr}"
        )
