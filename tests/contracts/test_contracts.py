import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def _read_text(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8")


def _read_json(rel_path: str):
    return json.loads(_read_text(rel_path))


def _require_jsonschema():
    try:
        import jsonschema  # type: ignore
    except Exception as exc:  # pragma: no cover
        raise AssertionError(
            "Missing dependency 'jsonschema'. Install with: "
            "python -m pip install -r tests/requirements.txt"
        ) from exc
    return jsonschema


def test_client_api_openapi_endpoint_is_locked():
    openapi = _read_text("contracts/openapi.yaml")
    assert "/api/property/analyze:" in openapi
    assert "/api/accident/analyze:" not in openapi
    assert '"422":' in openapi


def test_client_api_request_schema_has_canonical_fields():
    schema = _read_json("contracts/schemas/analyze_request.schema.json")
    props = schema["properties"]
    assert "images" in props
    assert "property_type" not in props
    assert "year" not in props
    assert "vehicle_type" not in props
    assert set(schema["required"]) == {"images"}

    jsonschema = _require_jsonschema()
    jsonschema.validate(
        instance={"images": "binary-file-placeholder"},
        schema=schema,
    )
    jsonschema.validate(
        instance={"images": ["f1", "f2"]},
        schema=schema,
    )


def test_client_api_response_schema_allows_success_and_error():
    jsonschema = _require_jsonschema()
    schema = _read_json("contracts/schemas/analyze_response.schema.json")

    success_payload = {
        "ok": True,
        "request_id": "req_001",
        "results": [
            {
                "filename": "wall.jpg",
                "ok": True,
                "damage_types": ["crack"],
                "cost_min": 500,
                "cost_max": 1500,
            }
        ],
    }
    jsonschema.validate(instance=success_payload, schema=schema)

    error_payload = {
        "ok": False,
        "request_id": "req_002",
        "error": {"code": "INVALID_FIELD", "message": "Invalid input"},
    }
    jsonschema.validate(instance=error_payload, schema=schema)


def test_client_api_implementation_uses_canonical_contract():
    api_route = _read_text("BuildCheck/API/src/routes/analyze_route.cpp")
    client_js = _read_text("BuildCheck/Client/JS/app.js")

    assert '/api/property/analyze' in api_route
    assert '"/api/accident/analyze"' not in api_route
    assert 'form.has_field("property_type")' not in api_route
    assert 'form.has_field("vehicle_type")' not in api_route

    assert 'fd.append("property_type"' not in client_js
    assert 'fd.append("vehicle_type"' not in client_js
    assert "res.status === 422" in client_js
    assert "Array.isArray(json.results)" in client_js
    assert "send_json(res, final_res.ok ? 200 : 422" in api_route


def test_api_engine_contract_files_are_consistent():
    engine_contract = _read_json("contracts/engine_api.json")
    req_example = _read_json("contracts/examples/engine_request.json")
    resp_example = _read_json("contracts/examples/engine_response.json")

    analyze = engine_contract["endpoints"]["analyze"]
    assert analyze["method"] == "POST"
    assert analyze["path"] == "/engine/analyze"
    assert analyze["request"]["shape"]["request_id"] == "string"
    assert analyze["request"]["shape"]["paths"] == ["string"]

    assert "request_id" in req_example and isinstance(req_example["request_id"], str)
    assert "paths" in req_example and isinstance(req_example["paths"], list)
    assert all(isinstance(p, str) for p in req_example["paths"])

    assert "ok" in resp_example and isinstance(resp_example["ok"], bool)
    assert "results" in resp_example and isinstance(resp_example["results"], list)
    for item in resp_example["results"]:
        assert isinstance(item.get("ok"), bool)
        assert isinstance(item.get("path"), str)
        assert isinstance(item.get("damage_types"), list)
        assert all(isinstance(x, str) for x in item["damage_types"])


def test_engine_runtime_endpoint_and_shape_match_contract():
    engine_service = _read_text("BuildCheck/Engine/engine_service.py")
    assert '@app.post("/engine/analyze")' in engine_service
    assert "request_id" in engine_service
    assert "paths" in engine_service
    assert '"results"' in engine_service
    assert '"damage_types"' in engine_service
    assert "X-RateLimit-Key" in engine_service
    assert "ENGINE_RATE_LIMIT_BACKEND" in engine_service
    assert "redis://" in engine_service
