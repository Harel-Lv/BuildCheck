# Engineering Decisions

## Naming

- Canonical domain naming moved from accident/vehicle wording to property wording.
- Canonical endpoint is `/api/property/analyze`.
- Analyze request is image-only (`images` field).

## Service Split

- Keep separation between `Client`, `API`, and `Engine`.
- API owns input validation and response envelope.
- Engine owns analysis logic and returns per-image results.

## Current Technical Debt

- Engine route still uses fake labels for end-to-end verification.
- Contracts and deployment files were placeholders and are now minimal templates.
- Training/export scripts are placeholders and not wired to runtime yet.

## Near-Term Priorities

1. Replace engine stub with real inference flow.
2. Lock response format against `contracts/openapi.yaml`.
3. Add smoke tests for `/health` and `/api/property/analyze`.
