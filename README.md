# BuildCheck Workspace

This repository is organized as a workspace around the `BuildCheck` app.

## Current Status

- `Client`: working demo UI for property-damage upload and result display.
- `API`: working C++ HTTP service with multipart validation and engine forwarding.
- `Engine`: production analyze runtime is Python YOLO (`BuildCheck/Engine/engine_service.py`).
- `Engine` C++ route remains a legacy stub and should not be used for analyze in local/dev.
- `docs/contracts/deploy/training`: now documented at minimum level; several parts are placeholders for next stages.

## Workspace Layout

- `BuildCheck/`: application source code.
- `BuildCheck/Client/`: static site (HTML/CSS/JS).
- `BuildCheck/API/`: C++ API service on port `8080`.
- `BuildCheck/Engine/`: Engine module (`engine_service.py` is the active runtime on `9090`).
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

Set a strong engine key before starting services:

```bash
export ENGINE_API_KEY='replace-with-32-plus-char-secret'
```

PowerShell:

```powershell
$env:ENGINE_API_KEY='replace-with-32-plus-char-secret'
```

Optional hardening envs:
- `ENGINE_MIN_KEY_LEN` (default `24`).
- `BUILDCHECK_PAYLOAD_MAX_BYTES` (default `268435456`, ~256MB).
- `BUILDCHECK_ENV=production` disables local fallback credentials in `scripts/local_stack.ps1`.

1. Run Python Engine (`BuildCheck/Engine/engine_service.py`) on `9090`.
2. Run API (`8080`).
3. Open `BuildCheck/Client/html/index.html` in a browser or serve the `BuildCheck/Client` folder.

See `scripts/build_all.sh` and `scripts/run_local.sh` for Linux/macOS flow.
On Windows, prefer `scripts/local_stack.ps1` to avoid process/env conflicts.

## Admin Contact Environment

For `/api/admin/login` and `/api/admin/contact/submissions`:

- `BUILDCHECK_ADMIN_USERNAME` and `BUILDCHECK_ADMIN_PASSWORD` (required for login flow).
- `BUILDCHECK_CONTACT_ADMIN_TOKEN` (optional API token alternative).
- `BUILDCHECK_ADMIN_ALLOWED_ORIGINS` (comma-separated browser origins allowed for admin credentials/CORS).
- `BUILDCHECK_CONTACT_DB_PATH` (contact submissions JSON file path).
- `BUILDCHECK_ADMIN_SESSION_DB_PATH` (admin sessions JSON file path; keeps sessions across API restarts).

## Production Env Setup (Step 1)

1. Create production env file from template:
```powershell
Copy-Item deploy/env/.env.production.template deploy/env/.env.production
```
2. Fill all `REPLACE_WITH_*` values in `deploy/env/.env.production`.
3. Set real domain(s) in `BUILDCHECK_ADMIN_ALLOWED_ORIGINS`.
4. Start with explicit env file:
```powershell
docker compose --env-file deploy/env/.env.production -f deploy/docker-compose.yml up -d --build
```

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

## Windows Local Stack (Recommended)

Use one script to avoid process conflicts and bad env setup:

```powershell
# status
powershell -ExecutionPolicy Bypass -File scripts/local_stack.ps1 -Action status

# start API + Python Engine safely
powershell -ExecutionPolicy Bypass -File scripts/local_stack.ps1 -Action start

# run live smoke tests
powershell -ExecutionPolicy Bypass -File scripts/local_stack.ps1 -Action smoke

# stop by pid file
powershell -ExecutionPolicy Bypass -File scripts/local_stack.ps1 -Action stop

# force stop anything on :8080/:9090 if needed
powershell -ExecutionPolicy Bypass -File scripts/local_stack.ps1 -Action stop -Force
```

## Known Gaps

- Engine route uses fake damage labels and does not load model inference in C++ path.
- Several deploy/training artifacts are templates/placeholders.
- Live integration tests are opt-in and require running services.

## Next Step After This Stage

Implement real inference path in `Engine` and align output contract with `contracts/openapi.yaml`.
