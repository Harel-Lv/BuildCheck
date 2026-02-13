# Architecture (Current)

```mermaid
flowchart LR
    U[User Browser]
    C[Client HTML/CSS/JS]
    A[API Service :8080]
    E[Engine Service :9090]
    T[(API tmp files)]

    U --> C
    C -->|multipart form-data| A
    A -->|write temp images| T
    A -->|POST /engine/analyze JSON paths| E
    E -->|JSON results| A
    A -->|JSON response| C
```

## Notes

- Client is static and calls API directly.
- API validates image files and request fields before calling Engine.
- Engine currently returns stub damage labels (not model-backed inference in C++ route yet).
