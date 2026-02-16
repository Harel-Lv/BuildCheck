#!/usr/bin/env python3
from __future__ import annotations

import json
import mimetypes
import os
import uuid
from pathlib import Path
from urllib import error, request


ROOT = Path(__file__).resolve().parents[1]
API_BASE = os.getenv("API_BASE", "http://127.0.0.1:8080")
ENGINE_BASE = os.getenv("ENGINE_BASE", "http://127.0.0.1:9090")
DEFAULT_IMAGE = ROOT / "BuildCheck" / "Engine" / "test.jpg"


def _get_json(url: str) -> dict:
    with request.urlopen(url, timeout=20) as resp:
        return json.loads(resp.read().decode("utf-8"))


def _build_multipart(image_path: Path) -> tuple[bytes, str]:
    boundary = f"----BuildCheckBoundary{uuid.uuid4().hex}"
    filename = image_path.name
    content_type = mimetypes.guess_type(filename)[0] or "application/octet-stream"
    image_bytes = image_path.read_bytes()

    parts: list[bytes] = []

    parts.append(f"--{boundary}\r\n".encode())
    parts.append(
        (
            f'Content-Disposition: form-data; name="images"; filename="{filename}"\r\n'
            f"Content-Type: {content_type}\r\n\r\n"
        ).encode()
    )
    parts.append(image_bytes)
    parts.append(b"\r\n")
    parts.append(f"--{boundary}--\r\n".encode())

    return b"".join(parts), boundary


def _post_analyze(image_path: Path) -> tuple[int, dict]:
    body, boundary = _build_multipart(image_path)
    req = request.Request(
        f"{API_BASE}/api/property/analyze",
        data=body,
        method="POST",
        headers={"Content-Type": f"multipart/form-data; boundary={boundary}"},
    )
    try:
        with request.urlopen(req, timeout=120) as resp:
            return resp.getcode(), json.loads(resp.read().decode("utf-8"))
    except error.HTTPError as exc:
        body = exc.read().decode("utf-8", errors="replace")
        try:
            payload = json.loads(body)
        except Exception:
            payload = {"ok": False, "error": {"code": "HTTP_ERROR", "message": body}}
        return exc.code, payload


def _validate_analyze_payload(payload: dict) -> None:
    if not isinstance(payload.get("ok"), bool):
        raise RuntimeError("Invalid response: missing/invalid 'ok'")
    if not isinstance(payload.get("request_id"), str):
        raise RuntimeError("Invalid response: missing/invalid 'request_id'")
    if "results" in payload:
        if not isinstance(payload["results"], list) or not payload["results"]:
            raise RuntimeError("Invalid response: 'results' must be a non-empty list")
        first = payload["results"][0]
        if not isinstance(first.get("filename"), str):
            raise RuntimeError("Invalid response: result missing 'filename'")
        if not isinstance(first.get("ok"), bool):
            raise RuntimeError("Invalid response: result missing 'ok'")
        if first.get("ok"):
            if not isinstance(first.get("damage_types"), list):
                raise RuntimeError("Invalid response: successful result missing 'damage_types'")
        else:
            if not isinstance(first.get("error"), str):
                raise RuntimeError("Invalid response: failed result missing 'error'")
    elif "error" in payload:
        err = payload["error"]
        if not isinstance(err, dict) or not isinstance(err.get("code"), str) or not isinstance(err.get("message"), str):
            raise RuntimeError("Invalid response: malformed 'error'")
    else:
        raise RuntimeError("Invalid response: expected either 'results' or 'error'")


def main() -> int:
    image_path = Path(os.getenv("E2E_IMAGE", str(DEFAULT_IMAGE))).resolve()
    if not image_path.exists():
        print(f"[e2e] image not found: {image_path}")
        return 2

    try:
        engine_health = _get_json(f"{ENGINE_BASE}/engine/health")
        api_health = _get_json(f"{API_BASE}/health")
    except error.URLError as exc:
        print(f"[e2e] failed to connect to services: {exc}")
        return 2

    print(f"[e2e] engine health: {engine_health}")
    print(f"[e2e] api health: {api_health}")

    if not engine_health.get("ok", False):
        print("[e2e] engine is not ready (model not loaded)")
        return 2
    if api_health.get("status") != "ok":
        print("[e2e] api health is not ok")
        return 2

    try:
        status, payload = _post_analyze(image_path)
        if status not in (200, 422):
            raise RuntimeError(f"unexpected analyze status: {status}")
        _validate_analyze_payload(payload)
    except Exception as exc:
        print(f"[e2e] analyze failed: {exc}")
        return 1

    first = payload.get("results", [{}])[0] if isinstance(payload.get("results"), list) else {}
    print(f"[e2e] success. request_id={payload.get('request_id')} first_result={first}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
