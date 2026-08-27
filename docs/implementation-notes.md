# Implementation Notes

Running log of concrete decisions made during development.
The project context file (`reverse_proxy_claude_project_context.md`) stays
high-level; specifics that supersede or refine it go here, in order.

---

## 2026-08-27 — Project scaffolding

- **Build system:** CMake (>= 3.16), single root `CMakeLists.txt` for now.
  Will only split into per-subsystem `CMakeLists.txt` files if/when the
  root file becomes unwieldy — not preemptively.
- **Folder layout:** subsystem-based under `src/` (e.g. `src/backend/`),
  mirrored under `tests/` (e.g. `tests/backend/`). Chosen over type-based
  (`include/` + `src/`) because it mirrors Section 7's class groupings and
  scales better as HTTP/cache/health subsystems are added.
- **Test framework:** GoogleTest v1.15.2, pulled via CMake `FetchContent`
  (fetched from GitHub at configure time — requires network on first build).
  Chosen over hand-rolled asserts for industry-standard fixtures/mocking
  and because it matches what placement interviewers expect to see.
- **Warnings:** `-Wall -Wextra` enabled globally from the start (Section 16).
- **Verified:** `cmake -S . -B build` configures cleanly, `cmake --build build`
  compiles GoogleTest + a temporary sanity test with zero warnings, `ctest`
  discovers and passes it. Confirms the whole toolchain works before any
  real class exists.
- **Next:** Phase 1 — design `BackendServer`, `BackendPool`,
  `SchedulingPolicy`, `RoundRobinPolicy`, `LeastConnectionPolicy`
  (`src/backend/`), no networking yet.
