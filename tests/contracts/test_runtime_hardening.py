import importlib.util
import io
import json
from pathlib import Path
from urllib import error


ROOT = Path(__file__).resolve().parents[2]


def _read_text(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8")


def _load_module(rel_path: str, module_name: str):
    module_path = ROOT / rel_path
    spec = importlib.util.spec_from_file_location(module_name, module_path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_api_rate_limit_key_does_not_trust_proxy_headers_by_default():
    source = _read_text("BuildCheck/API/src/routes/analyze_route.cpp")
    assert "BUILDCHECK_TRUST_PROXY_HEADERS" in source
    assert "if (trust_proxy_headers())" in source
    assert 'req.get_header_value("X-Forwarded-For")' in source


def test_api_error_response_does_not_expose_internal_exception_message():
    source = _read_text("BuildCheck/API/src/routes/analyze_route.cpp")
    assert 'make_error_json(request_id, "INTERNAL_ERROR", "Internal server error")' in source
    assert "Failed to resolve temp directory: " not in source


def test_api_startup_hardening_for_key_strength_and_payload_limit():
    source = _read_text("BuildCheck/API/src/main.cpp")
    assert "is_engine_key_strong" in source
    assert "ENGINE_MIN_KEY_LEN" in source
    assert "server.set_payload_max_length" in source
    assert "BUILDCHECK_PAYLOAD_MAX_BYTES" in source


def test_api_analyze_route_hardening_for_temp_files_and_file_count_cap():
    source = _read_text("BuildCheck/API/src/routes/analyze_route.cpp")
    assert "kMaxFilesHardCap" in source
    assert "out.flush()" in source
    assert "file_size(tmp_path" in source


def test_engine_env_parsing_is_hardened():
    source = _read_text("BuildCheck/Engine/engine_service.py")
    assert "def _env_int(" in source
    assert "def _env_float(" in source
    assert 'RATE_LIMIT_RPM = _env_int("ENGINE_RATE_LIMIT_RPM"' in source
    assert 'MAX_PATHS = _env_int("ENGINE_MAX_PATHS"' in source
    assert 'CONF = _env_float("YOLO_CONF"' in source


def test_cpp_engine_stub_is_disabled_by_default():
    source = _read_text("BuildCheck/Engine/src/main.cpp")
    assert "ALLOW_CPP_ENGINE_STUB" in source
    assert "stub runtime is disabled by default" in source


def test_engine_status_is_propagated_back_to_client():
    source = _read_text("BuildCheck/API/src/routes/analyze_route.cpp")
    client_source = _read_text("BuildCheck/API/src/services/engine_client.cpp")
    assert "catch (const EngineClientError& e)" in source
    assert 'make_error_json(request_id, "ENGINE_ERROR", msg)' in source
    assert "throw EngineClientError(" in client_source


def test_compose_enables_trusted_proxy_headers():
    compose = _read_text("deploy/docker-compose.yml")
    assert "BUILDCHECK_TRUST_PROXY_HEADERS=1" in compose


def test_compose_requires_critical_admin_envs():
    compose = _read_text("deploy/docker-compose.yml")
    assert "BUILDCHECK_ADMIN_ALLOWED_ORIGINS=${BUILDCHECK_ADMIN_ALLOWED_ORIGINS:?" in compose
    assert "BUILDCHECK_CONTACT_DB_PATH=${BUILDCHECK_CONTACT_DB_PATH:?" in compose
    assert "BUILDCHECK_ADMIN_SESSION_DB_PATH=${BUILDCHECK_ADMIN_SESSION_DB_PATH:?" in compose


def test_contact_admin_endpoint_is_auth_protected_and_persistent():
    source = _read_text("BuildCheck/API/src/routes/register_routes.cpp")
    assert '/api/admin/contact/submissions' in source
    assert '/api/admin/login' in source
    assert "buildcheck_admin_session" in source
    assert "BUILDCHECK_ADMIN_USERNAME" in source
    assert "BUILDCHECK_ADMIN_PASSWORD" in source
    assert "BUILDCHECK_CONTACT_ADMIN_TOKEN" in source
    assert "persist_contacts_locked()" in source
    assert "BUILDCHECK_CONTACT_DB_PATH" in source


def test_smoke_post_analyze_accepts_http_error_payload(monkeypatch):
    smoke = _load_module("scripts/smoke_e2e_live.py", "smoke_e2e_live_module")
    img = ROOT / "BuildCheck" / "Engine" / "test.jpg"

    payload = {"ok": False, "request_id": "req_123", "results": [{"filename": "test.jpg", "ok": False, "error": "no damage"}]}
    body = json.dumps(payload).encode("utf-8")

    class _FakeHTTPError(error.HTTPError):
        def __init__(self):
            super().__init__(
                url=f"{smoke.API_BASE}/api/property/analyze",
                code=422,
                msg="Unprocessable Entity",
                hdrs=None,
                fp=io.BytesIO(body),
            )

    def _fake_urlopen(_req, timeout=120):  # noqa: ARG001
        raise _FakeHTTPError()

    monkeypatch.setattr(smoke.request, "urlopen", _fake_urlopen)
    status, parsed = smoke._post_analyze(img)

    assert status == 422
    assert parsed["request_id"] == "req_123"
    assert isinstance(parsed.get("results"), list)
