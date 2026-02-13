from __future__ import annotations

import os
from pathlib import Path
from typing import Any

from fastapi import FastAPI
from fastapi.responses import JSONResponse
from pydantic import BaseModel, Field
from ultralytics import YOLO


class AnalyzeRequest(BaseModel):
    request_id: str = ""
    paths: list[str] = Field(default_factory=list)


def _resolve_model_path() -> Path:
    env_path = os.getenv("MODEL_PATH", "").strip()
    if env_path:
        return Path(env_path).expanduser().resolve()
    return (Path(__file__).resolve().parent / "models" / "mbdd2025" / "best_mbdd_yolo.pt").resolve()


def _load_model() -> tuple[YOLO | None, str, str | None]:
    model_path = _resolve_model_path()
    if not model_path.exists():
        return None, str(model_path), f"Model file not found: {model_path}"
    try:
        return YOLO(str(model_path)), str(model_path), None
    except Exception as exc:  # pragma: no cover - runtime dependency
        return None, str(model_path), f"Failed to load model: {exc}"


def _label_for_class_id(names: Any, class_id: int) -> str:
    if isinstance(names, dict):
        if class_id in names:
            return str(names[class_id])
        str_key = str(class_id)
        if str_key in names:
            return str(names[str_key])
        return str(class_id)
    if isinstance(names, list) and 0 <= class_id < len(names):
        return str(names[class_id])
    return str(class_id)


def _extract_damage_types(result: Any, names: Any) -> list[str]:
    boxes = getattr(result, "boxes", None)
    if boxes is None or getattr(boxes, "cls", None) is None:
        return []

    class_ids = [int(x) for x in boxes.cls.tolist()]
    ordered_unique: list[str] = []
    seen: set[int] = set()
    for class_id in class_ids:
        if class_id in seen:
            continue
        seen.add(class_id)
        ordered_unique.append(_label_for_class_id(names, class_id))
    return ordered_unique


MODEL, MODEL_PATH_STR, MODEL_ERROR = _load_model()
CONF = float(os.getenv("YOLO_CONF", "0.25"))

app = FastAPI(title="BuildCheck Engine", version="1.0.0")


@app.get("/engine/health")
def health() -> dict[str, Any]:
    payload: dict[str, Any] = {
        "ok": MODEL is not None,
        "service": "engine",
        "model_path": MODEL_PATH_STR,
    }
    if MODEL_ERROR:
        payload["error"] = MODEL_ERROR
    return payload


@app.post("/engine/analyze")
def analyze(req: AnalyzeRequest) -> JSONResponse:
    if MODEL is None:
        return JSONResponse(status_code=500, content={"ok": False, "error": MODEL_ERROR or "model unavailable"})

    if not req.paths:
        return JSONResponse(status_code=400, content={"ok": False, "error": "missing paths array"})

    results: list[dict[str, Any]] = []
    names = MODEL.names

    for raw_path in req.paths:
        path = Path(raw_path)
        if not path.exists():
            results.append({"ok": False, "path": str(path), "damage_types": [], "error": "file not found"})
            continue

        try:
            pred = MODEL.predict(source=str(path), conf=CONF, verbose=False)
            damage_types = _extract_damage_types(pred[0], names) if pred else []
            ok = len(damage_types) > 0
            item: dict[str, Any] = {"ok": ok, "path": str(path), "damage_types": damage_types}
            if not ok:
                item["error"] = "no damage detected"
            results.append(item)
        except Exception as exc:  # pragma: no cover - runtime dependency
            results.append({"ok": False, "path": str(path), "damage_types": [], "error": str(exc)})

    return JSONResponse(status_code=200, content={"ok": any(r.get("ok", False) for r in results), "results": results})
