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
`Engine/src/main.cpp` now exits by default unless `ALLOW_CPP_ENGINE_STUB=1`, to avoid accidental use of the stub runtime.

## Required Environment

API-to-Engine calls require `ENGINE_API_KEY` on both services.
Use a strong key (32+ chars) and set it consistently before running locally.

## Contact Admin Page

- Public form posts to `POST /api/contact`.
- Admin login endpoint: `POST /api/admin/login` with JSON body `{ "username": "...", "password": "..." }`.
- Admin session is kept in HttpOnly cookie and used by `GET /api/admin/contact/submissions`.
- Configure credentials with `BUILDCHECK_ADMIN_USERNAME` and `BUILDCHECK_ADMIN_PASSWORD`.
- Optional backward-compat token: `BUILDCHECK_CONTACT_ADMIN_TOKEN` via header `X-Admin-Token`.
- Client admin page path: `BuildCheck/Client/html/admin-contacts.html`.
- For `file://` local testing, you can override API base with query param:
  - `index.html?api_base=http://127.0.0.1:8080`
  - `admin-contacts.html?api_base=http://127.0.0.1:8080`
