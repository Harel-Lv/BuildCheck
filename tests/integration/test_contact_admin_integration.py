from __future__ import annotations

import json
import os
import socket
import subprocess
import sys
import time
from http.cookiejar import CookieJar
from pathlib import Path
from urllib import error, request

import pytest


ROOT = Path(__file__).resolve().parents[2]


def _pick_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        s.listen(1)
        return int(s.getsockname()[1])


def _api_binary_path() -> Path:
    override = os.getenv("API_BIN", "").strip()
    if override:
        return Path(override).resolve()

    candidates = [
        ROOT / "BuildCheck" / "API" / "build" / "Release" / "api_server.exe",
        ROOT / "BuildCheck" / "API" / "build" / "api_server",
        ROOT / "BuildCheck" / "API" / "build" / "api_server.exe",
    ]
    for c in candidates:
        if c.exists():
            return c
    return candidates[0]


def _wait_for_health(base: str, timeout_sec: float = 15.0) -> None:
    deadline = time.time() + timeout_sec
    while time.time() < deadline:
        try:
            with request.urlopen(f"{base}/health", timeout=1.5) as resp:
                if resp.status == 200:
                    return
        except Exception:
            time.sleep(0.2)
    raise RuntimeError("API did not become healthy in time")


def _json_request(method: str, url: str, payload: dict | None = None, headers: dict[str, str] | None = None, opener=None):
    body = None
    req_headers = {}
    if payload is not None:
        body = json.dumps(payload).encode("utf-8")
        req_headers["Content-Type"] = "application/json"
    if headers:
        req_headers.update(headers)

    req = request.Request(url, data=body, method=method, headers=req_headers)
    open_fn = opener.open if opener is not None else request.urlopen
    try:
        with open_fn(req, timeout=5) as resp:
            return resp.status, json.loads(resp.read().decode("utf-8"))
    except error.HTTPError as exc:
        raw = exc.read().decode("utf-8", errors="replace")
        try:
            data = json.loads(raw)
        except Exception:
            data = {"ok": False, "error": {"code": "HTTP_ERROR", "message": raw}}
        return exc.code, data


@pytest.mark.skipif(
    os.getenv("RUN_LIVE_E2E", "0") != "1",
    reason="Set RUN_LIVE_E2E=1 to run API integration tests.",
)
def test_contact_admin_auth_and_persistence(tmp_path: Path):
    api_bin = _api_binary_path()
    if not api_bin.exists():
        pytest.skip(f"API binary not found: {api_bin}")

    db_path = tmp_path / "contact_submissions.json"
    port = _pick_free_port()
    base = f"http://127.0.0.1:{port}"

    env = os.environ.copy()
    env["ENGINE_API_KEY"] = env.get("ENGINE_API_KEY", "integration-engine-key-1234567890")
    env["BUILDCHECK_CONTACT_ADMIN_TOKEN"] = ""
    env["BUILDCHECK_ADMIN_USERNAME"] = "admin"
    env["BUILDCHECK_ADMIN_PASSWORD"] = "integration-password-123"
    env["BUILDCHECK_CONTACT_DB_PATH"] = str(db_path)
    env["API_PORT"] = str(port)

    cookie_jar = CookieJar()
    opener = request.build_opener(request.HTTPCookieProcessor(cookie_jar))

    proc = subprocess.Popen([str(api_bin)], cwd=str(ROOT), env=env)
    try:
        _wait_for_health(base)

        status, created = _json_request(
            "POST",
            f"{base}/api/contact",
            payload={
                "name": "Integration User",
                "phone": "050-1234567",
                "message": "Testing contact persistence",
            },
        )
        assert status == 201
        assert created.get("ok") is True
        assert isinstance(created.get("item", {}).get("registered_at"), str)

        status, _ = _json_request("GET", f"{base}/api/admin/contact/submissions")
        assert status == 401

        status, _ = _json_request(
            "POST",
            f"{base}/api/admin/login",
            payload={"username": "admin", "password": "bad"},
            opener=opener,
        )
        assert status == 401

        status, login_data = _json_request(
            "POST",
            f"{base}/api/admin/login",
            payload={"username": "admin", "password": "integration-password-123"},
            opener=opener,
        )
        assert status == 200
        assert login_data.get("ok") is True

        status, data = _json_request(
            "GET",
            f"{base}/api/admin/contact/submissions",
            opener=opener,
        )
        assert status == 200
        assert data.get("ok") is True
        assert len(data.get("items", [])) >= 1
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=5)

    proc2 = subprocess.Popen([str(api_bin)], cwd=str(ROOT), env=env)
    try:
        cookie_jar2 = CookieJar()
        opener2 = request.build_opener(request.HTTPCookieProcessor(cookie_jar2))
        _wait_for_health(base)
        status, login2 = _json_request(
            "POST",
            f"{base}/api/admin/login",
            payload={"username": "admin", "password": "integration-password-123"},
            opener=opener2,
        )
        assert status == 200
        assert login2.get("ok") is True
        status, data2 = _json_request(
            "GET",
            f"{base}/api/admin/contact/submissions",
            opener=opener2,
        )
        assert status == 200
        assert data2.get("ok") is True
        assert len(data2.get("items", [])) >= 1
    finally:
        proc2.terminate()
        try:
            proc2.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc2.kill()
            proc2.wait(timeout=5)
