# Engine Service

## Purpose

Engine receives image file paths from API and returns per-image damage labels.

## Active Runtime (Python YOLO)

- Main service file: `engine_service.py`
- Endpoint `/engine/analyze` runs real YOLO inference from `models/mbdd2025/best_mbdd_yolo.pt`.
- Endpoint `/engine/health` reports model load status.

### Run

```bash
cd BuildCheck/Engine
python -m pip install -r requirements.txt
python -m uvicorn engine_service:app --host 0.0.0.0 --port 9090
```

Optional env vars:

- `MODEL_PATH` for custom model file path.
- `YOLO_CONF` for confidence threshold (default `0.25`).

## Legacy C++ Route

- `src/routes/analyze_route.cpp` is still a stub and not used when running the Python runtime.
- Keep it only as placeholder until full C++ inference integration is implemented.

## Runtime

- Health endpoint: `GET /engine/health`
- Analyze endpoint: `POST /engine/analyze`
- Default port: `9090`

## Next Milestone

Stabilize inference output calibration and keep response shape compatible with `contracts/engine_api.json`.
