# Reverse Proxy Server (C++17)

A custom L7 reverse proxy with load balancing, built as a Semester 3 OOP
course project at Vishwakarma Institute of Technology, Pune.

See `docs/implementation-notes.md` for the running log of design decisions
made during development.

---

## Prerequisites

- CMake >= 3.16
- A C++17 compiler (developed against g++ 13.3; g++ 9+ should work)
- Internet access on first configure (CMake's `FetchContent` pulls
  GoogleTest v1.15.2 from GitHub — after that it's cached locally)

Check your versions:

```bash
g++ --version
cmake --version
```

## Building

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
```

## Running tests

```bash
cd build
ctest --output-on-failure
```

## Project layout

```
src/            Subsystem-based source folders (e.g. src/backend/)
tests/          Mirrors src/ (e.g. tests/backend/), one GoogleTest binary per subsystem
docs/           implementation-notes.md — design decision log
```

Currently only `src/backend/` and `tests/backend/` exist (Phase 1: OOP
foundation — `BackendServer`, `BackendPool`, `SchedulingPolicy` and its
concrete policies). Further subsystems (`http/`, `cache/`, `health/`, etc.)
are added phase by phase — see `docs/implementation-notes.md`.

## Team workflow

- Work off feature branches, open PRs into `main`.
- Every design decision that isn't obvious from the code should get a
  short entry in `docs/implementation-notes.md`.
- Run `ctest` before pushing.
