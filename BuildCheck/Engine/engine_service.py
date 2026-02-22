from __future__ import annotations

import os
import tempfile
import threading
import time
from collections import deque
from pathlib import Path
from typing import Any

from fastapi import FastAPI
from fastapi import Header
from fastapi import Request
from fastapi.responses import JSONResponse
from pydantic import BaseModel, Field
from ultralytics import YOLO


class AnalyzeRequest(BaseModel):
    request_id: str = ""
    paths: list[str] = Field(default_factory=list)


def _env_int(name: str, default: int, minimum: int | None = None, maximum: int | None = None) -> int:
    raw = os.getenv(name, "").strip()
    if not raw:
        value = default
    else:
        try:
            value = int(raw)
        except Exception:
            value = default
    if minimum is not None and value < minimum:
        value = minimum
    if maximum is not None and value > maximum:
        value = maximum
    return value


def _env_float(name: str, default: float, minimum: float | None = None, maximum: float | None = None) -> float:
    raw = os.getenv(name, "").strip()
    if not raw:
        value = default
    else:
        try:
            value = float(raw)
        except Exception:
            value = default
    if minimum is not None and value < minimum:
        value = minimum
    if maximum is not None and value > maximum:
        value = maximum
    return value


def _env_bool(name: str, default: bool) -> bool:
    raw = os.getenv(name, "").strip().lower()
    if not raw:
        return default
    return raw in {"1", "true", "yes", "on"}


def _resolve_allowed_roots() -> list[Path]:
    raw = os.getenv("ENGINE_ALLOWED_ROOTS", "").strip()
    if not raw:
        # support both docker-compose shared volume and local API temp directory
        return [
            Path("/shared-tmp").resolve(),
            (Path(tempfile.gettempdir()) / "buildcheck_api").resolve(),
        ]

    roots: list[Path] = []
    for token in raw.split(os.pathsep):
        token = token.strip()
        if not token:
            continue
        roots.append(Path(token).expanduser().resolve())
    return roots


def _is_path_within_allowed_roots(path: Path, allowed_roots: list[Path]) -> bool:
    try:
        resolved = path.resolve()
    except Exception:
        return False

    for root in allowed_roots:
        try:
            resolved.relative_to(root)
            return True
        except ValueError:
            continue
    return False


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


def _heuristic_damage_types(path: Path) -> list[str]:
    # Heuristic fallback for environments where a trained model file is unavailable.
    try:
        import cv2  # type: ignore
    except Exception:
        return []

    img = cv2.imread(str(path))
    if img is None:
        return []

    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    hsv = cv2.cvtColor(img, cv2.COLOR_BGR2HSV)
    edges = cv2.Canny(gray, 80, 170)

    edge_ratio = float((edges > 0).mean())
    sat = float(hsv[:, :, 1].mean()) / 255.0
    val = float(hsv[:, :, 2].mean()) / 255.0
    b = float(img[:, :, 0].mean()) / 255.0
    r = float(img[:, :, 2].mean()) / 255.0

    labels: list[str] = []
    if edge_ratio > 0.085 and sat < 0.36:
        labels.append("crack")
    if (b - r) > 0.05 and sat > 0.20:
        labels.append("leakage")
    if val > 0.70 and sat < 0.22 and edge_ratio < 0.12:
        labels.append("peeling")
    if edge_ratio > 0.16 and val < 0.56:
        labels.append("breakage")

    # Ensure a usable label for UX instead of a hard failure when image quality is poor.
    if not labels:
        labels.append("suspected_damage")

    return labels[:2]


MODEL, MODEL_PATH_STR, MODEL_ERROR = _load_model()
CONF = _env_float("YOLO_CONF", 0.25, minimum=0.0, maximum=1.0)
MAX_PATHS = _env_int("ENGINE_MAX_PATHS", 20, minimum=1, maximum=200)
ALLOWED_ROOTS = _resolve_allowed_roots()
ENGINE_API_KEY = os.getenv("ENGINE_API_KEY", "").strip()
RATE_LIMIT_RPM = _env_int("ENGINE_RATE_LIMIT_RPM", 60, minimum=0, maximum=10000)
RATE_LIMIT_WINDOW_SEC = 60.0
RATE_LIMIT_BACKEND = os.getenv("ENGINE_RATE_LIMIT_BACKEND", "memory").strip().lower()
RATE_LIMIT_REDIS_URL = os.getenv("ENGINE_RATE_LIMIT_REDIS_URL", "redis://redis:6379/0").strip()
RATE_LIMIT_REDIS_PREFIX = os.getenv("ENGINE_RATE_LIMIT_REDIS_PREFIX", "buildcheck:rl").strip() or "buildcheck:rl"
RATE_LIMIT_BUCKETS: dict[str, deque[float]] = {}
RATE_LIMIT_LOCK = threading.Lock()
RATE_LIMIT_CLEANUP_INTERVAL_SEC = 120.0
RATE_LIMIT_LAST_CLEANUP = 0.0
RATE_LIMIT_REDIS_CLIENT: Any | None = None
RATE_LIMIT_REDIS_LOCK = threading.Lock()
ENGINE_ALLOW_HEURISTIC_FALLBACK = _env_bool("ENGINE_ALLOW_HEURISTIC_FALLBACK", True)

MIN_ENGINE_KEY_LEN = _env_int("ENGINE_MIN_KEY_LEN", 24, minimum=8, maximum=256)
WEAK_ENGINE_KEYS = {"", "change-me", "changeme", "default", "password", "123456"}

app = FastAPI(title="BuildCheck Engine", version="1.0.0")


def _rate_limit_ok(client_key: str) -> bool:
    if RATE_LIMIT_RPM <= 0:
        return True
    if RATE_LIMIT_BACKEND == "redis":
        return _redis_rate_limit_ok(client_key)
    return _memory_rate_limit_ok(client_key)


def _memory_rate_limit_ok(client_key: str) -> bool:
    now = time.monotonic()
    window_start = now - RATE_LIMIT_WINDOW_SEC
    with RATE_LIMIT_LOCK:
        global RATE_LIMIT_LAST_CLEANUP
        if now - RATE_LIMIT_LAST_CLEANUP >= RATE_LIMIT_CLEANUP_INTERVAL_SEC:
            stale_keys: list[str] = []
            for key, b in RATE_LIMIT_BUCKETS.items():
                while b and b[0] < window_start:
                    b.popleft()
                if not b:
                    stale_keys.append(key)
            for key in stale_keys:
                RATE_LIMIT_BUCKETS.pop(key, None)
            RATE_LIMIT_LAST_CLEANUP = now

        bucket = RATE_LIMIT_BUCKETS.get(client_key)
        if bucket is None:
            bucket = deque()
            RATE_LIMIT_BUCKETS[client_key] = bucket

        while bucket and bucket[0] < window_start:
            bucket.popleft()

        if len(bucket) >= RATE_LIMIT_RPM:
            return False

        bucket.append(now)
        return True


def _get_redis_client() -> Any | None:
    global RATE_LIMIT_REDIS_CLIENT
    if RATE_LIMIT_REDIS_CLIENT is not None:
        return RATE_LIMIT_REDIS_CLIENT

    with RATE_LIMIT_REDIS_LOCK:
        if RATE_LIMIT_REDIS_CLIENT is not None:
            return RATE_LIMIT_REDIS_CLIENT
        try:
            import redis  # type: ignore
            RATE_LIMIT_REDIS_CLIENT = redis.from_url(RATE_LIMIT_REDIS_URL, decode_responses=True)
            return RATE_LIMIT_REDIS_CLIENT
        except Exception:
            return None


def _redis_rate_limit_ok(client_key: str) -> bool:
    client = _get_redis_client()
    if client is None:
        # fallback if redis is not available/misconfigured
        return _memory_rate_limit_ok(client_key)

    key = f"{RATE_LIMIT_REDIS_PREFIX}:{client_key}"
    try:
        current = int(client.incr(key))
        if current == 1:
            client.expire(key, int(RATE_LIMIT_WINDOW_SEC) + 1)
        return current <= RATE_LIMIT_RPM
    except Exception:
        return _memory_rate_limit_ok(client_key)


def _is_engine_key_strong(key: str) -> bool:
    if len(key) < MIN_ENGINE_KEY_LEN:
        return False
    if key.lower() in WEAK_ENGINE_KEYS:
        return False
    return True


@app.get("/engine/health")
def health() -> dict[str, Any]:
    auth_configured = bool(ENGINE_API_KEY)
    auth_strong = _is_engine_key_strong(ENGINE_API_KEY) if auth_configured else False
    fallback_mode = MODEL is None and ENGINE_ALLOW_HEURISTIC_FALLBACK
    errors: list[str] = []
    payload: dict[str, Any] = {
        "ok": MODEL is not None or fallback_mode,
        "service": "engine",
        "model_loaded": MODEL is not None,
        "inference_mode": "model" if MODEL is not None else ("heuristic_fallback" if fallback_mode else "unavailable"),
        "auth_enabled": auth_configured,
        "auth_strong": auth_strong,
        "rate_limit_rpm": RATE_LIMIT_RPM,
        "rate_limit_backend": RATE_LIMIT_BACKEND,
    }
    if auth_configured and not auth_strong:
        errors.append(f"ENGINE_API_KEY is weak; must be at least {MIN_ENGINE_KEY_LEN} chars")
    if MODEL_ERROR and not fallback_mode:
        errors.append(MODEL_ERROR)
    elif MODEL_ERROR and fallback_mode:
        payload["warning"] = MODEL_ERROR
    if errors:
        payload["error"] = "; ".join(errors)
    return payload


@app.post("/engine/analyze")
def analyze(req: AnalyzeRequest, request: Request, x_engine_key: str | None = Header(default=None)) -> JSONResponse:
    if not ENGINE_API_KEY:
        return JSONResponse(status_code=503, content={"ok": False, "error": "engine auth not configured"})
    if not _is_engine_key_strong(ENGINE_API_KEY):
        return JSONResponse(status_code=503, content={"ok": False, "error": "engine auth key is weak"})
    if x_engine_key != ENGINE_API_KEY:
        return JSONResponse(status_code=401, content={"ok": False, "error": "unauthorized"})
    rl_key_header = request.headers.get("X-RateLimit-Key", "")
    client_host = request.client.host if request.client and request.client.host else "unknown"
    rate_limit_key = rl_key_header.strip() or client_host or "unknown"
    if len(rate_limit_key) > 128:
        rate_limit_key = rate_limit_key[:128]
    if not _rate_limit_ok(rate_limit_key):
        return JSONResponse(status_code=429, content={"ok": False, "error": "rate limit exceeded"})

    if MODEL is None and not ENGINE_ALLOW_HEURISTIC_FALLBACK:
        return JSONResponse(status_code=500, content={"ok": False, "error": MODEL_ERROR or "model unavailable"})

    if not req.paths:
        return JSONResponse(status_code=400, content={"ok": False, "error": "missing paths array"})
    if len(req.paths) > MAX_PATHS:
        return JSONResponse(status_code=400, content={"ok": False, "error": f"too many paths (max {MAX_PATHS})"})

    results: list[dict[str, Any]] = []
    names = MODEL.names if MODEL is not None else {}

    for raw_path in req.paths:
        path = Path(raw_path).expanduser()
        if not _is_path_within_allowed_roots(path, ALLOWED_ROOTS):
            results.append({
                "ok": False,
                "path": str(path),
                "damage_types": [],
                "error": "path not allowed",
                "inference_mode": "heuristic_fallback" if MODEL is None else "model",
            })
            continue
        if not path.exists() or not path.is_file():
            results.append({
                "ok": False,
                "path": str(path),
                "damage_types": [],
                "error": "file not found",
                "inference_mode": "heuristic_fallback" if MODEL is None else "model",
            })
            continue

        try:
            if MODEL is None:
                damage_types = _heuristic_damage_types(path)
            else:
                pred = MODEL.predict(source=str(path), conf=CONF, verbose=False)
                damage_types = _extract_damage_types(pred[0], names) if pred else []
            ok = len(damage_types) > 0
            item: dict[str, Any] = {
                "ok": ok,
                "path": str(path),
                "damage_types": damage_types,
                "inference_mode": "heuristic_fallback" if MODEL is None else "model",
            }
            if not ok:
                item["error"] = "no damage detected"
            results.append(item)
        except Exception:  # pragma: no cover - runtime dependency
            results.append({
                "ok": False,
                "path": str(path),
                "damage_types": [],
                "error": "inference failed",
                "inference_mode": "heuristic_fallback" if MODEL is None else "model",
            })

    return JSONResponse(status_code=200, content={"ok": any(r.get("ok", False) for r in results), "results": results})
