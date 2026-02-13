# BuildCheck Workspace

This repository is organized as a workspace around the `BuildCheck` app.

## Current Status

- `Client`: working demo UI for property-damage upload and result display.
- `API`: working C++ HTTP service with multipart validation and engine forwarding.
- `Engine`: currently a stub that returns fake `damage_types` (not real inference).
- `docs/contracts/deploy/training`: now documented at minimum level; several parts are placeholders for next stages.

## Workspace Layout

- `BuildCheck/`: application source code.
- `BuildCheck/Client/`: static site (HTML/CSS/JS).
- `BuildCheck/API/`: C++ API service on port `8080`.
- `BuildCheck/Engine/`: C++ Engine service on port `9090`.
- `contracts/`: API contracts and request/response schemas.
- `docs/`: architecture and decisions.
- `deploy/`: deployment templates (not production-ready yet).
- `scripts/`: local helper scripts.
- `training/`: ML training/export placeholders.

## API Summary

- Health: `GET /health`
- Analyze: `POST /api/property/analyze`
- CORS preflight: `OPTIONS /api/property/analyze`

Analyze request requires image files only (`images`).

## Local Run (Manual)

1. Run Engine (`9090`).
2. Run API (`8080`).
3. Open `BuildCheck/Client/html/index.html` in a browser or serve the `BuildCheck/Client` folder.

See `scripts/build_all.sh` and `scripts/run_local.sh` for example flow.

## Contract Tests

```bash
scripts/test_contracts.sh
```

The suite validates:
- Client/API contract files and canonical field names.
- API/Engine contract files and examples.
- Implementation references in `Client`, `API`, and Python engine runtime.

CI runs the same suite automatically on every `push` and `pull_request` via `.github/workflows/contract-tests.yml`.

## Live E2E Smoke (API + Engine Running)

Run after both services are up:

```bash
python scripts/smoke_e2e_live.py
```

Optional environment variables:
- `API_BASE` (default: `http://127.0.0.1:8080`)
- `ENGINE_BASE` (default: `http://127.0.0.1:9090`)
- `E2E_IMAGE` (default: `BuildCheck/Engine/test.jpg`)

Pytest variant (opt-in):

```bash
RUN_LIVE_E2E=1 python -m pytest -q tests/integration/test_live_e2e.py
```

PowerShell variant:

```powershell
$env:RUN_LIVE_E2E='1'
python -m pytest -q tests/integration/test_live_e2e.py
```

## Known Gaps

- Engine route uses fake damage labels and does not load model inference in C++ path.
- Several deploy/training artifacts are templates/placeholders.
- No automated test suite wired yet.

## Next Step After This Stage

Implement real inference path in `Engine` and align output contract with `contracts/openapi.yaml`.
