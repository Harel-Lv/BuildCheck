# BuildCheck App

`BuildCheck` is a property inspection and damage-analysis app split into 3 parts:

- `Client`: browser UI for image upload and result display.
- `API`: C++ gateway that validates uploads and calls Engine.
- `Engine`: C++ service that returns analysis output (currently stub behavior).
  - Recommended runtime now is Python YOLO service at `Engine/engine_service.py`.

## Ports

- API: `8080`
- Engine: `9090`

## Canonical Analyze Endpoint

- `POST /api/property/analyze`
- Required multipart fields:
  - `images` (file, one or many)

## Important Note

`Engine/src/routes/analyze_route.cpp` is a stub. For real detection, run `Engine/engine_service.py` (FastAPI + Ultralytics YOLO) on port `9090`.
