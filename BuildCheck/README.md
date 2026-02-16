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

## Required Environment

API-to-Engine calls require `ENGINE_API_KEY` on both services.
Use a strong key (32+ chars) and set it consistently before running locally.

## Contact Admin Page

- Public form posts to `POST /api/contact`.
- Admin-only list is `GET /api/admin/contact/submissions` with header `X-Admin-Token`.
- Configure token with `BUILDCHECK_CONTACT_ADMIN_TOKEN`.
- Client admin page path: `BuildCheck/Client/html/admin-contacts.html`.
