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

## 2026-10-13 — M0 foundation checkpoint

- Both scheduling implementations now build with warnings enabled. The new 3/1/2
  regression failed against the old early-return loop, then passed after repair.
- Policies skip null/unhealthy entries; round robin searches at most one pass and
  normalizes its cursor. Least connections keeps the first eligible backend on ties.
- Backend endpoints and null pool insertions are validated. Both pool vectors reserve
  before membership changes; owning pointers keep surviving backend addresses stable.
- Renamed decrementConnections and checked its zero lower bound. Removed the sanity
  test after adding real coverage. MinGW build and all 17 foundation tests passed.
- Scope agreed for this review: M0–M3. epoll, workers, cache and health probes are pending.

## 2026-10-13 — M1 Linux listener

- Added a move-only Socket, Linux-only CMake targets, sequential ReverseProxy and main.
  The listener owns its descriptor; each accepted Socket closes at the end of its exchange.
- Bounded poll waits keep a total steady_clock deadline, retry interrupted calls, and
  check shutdown. Send offsets handle short writes; MSG_NOSIGNAL prevents broken pipes
  from terminating the process. SIGINT/SIGTERM only update a sig_atomic_t flag.
- Ubuntu WSL lacked CMake. Downloaded Kitware CMake 3.31.8 into ignored out/tooling
  instead of installing system packages. Docker's daemon was unavailable; Docker is
  outside this checkpoint. Linux GCC 15.2 and MinGW GCC 16.2 were used.
- Actual temporary M1 demo output: "200 OK, listener ready"; ten disconnected clients
  left the process alive; shutdown and restart reused the listening port.

## 2026-10-13 — M2 bounded HTTP forwarding

- Pure parsers return NeedMoreData/Complete/Error, consumed bytes and an explicit error
  status. Header pairs preserve duplicates; conflicting framing is rejected before I/O.
- Reused bounded socket operations for one upstream. Resolve DNS once at startup so
  request connect deadlines do not contain an unbounded resolver call.
- Buffer entire bounded messages before forwarding. This trades streaming and general
  interoperability for a request path a student can explain. Chunked, informational
  responses, upgrades and Expect are rejected. Close-delimited replies need EOF.
- Request rewriting preserves Host/target/body, removes connection fields and replaces
  client-supplied forwarding identity. Responses retain separate Set-Cookie fields.
- MinGW passed 31 tests; WSL passed 34 tests at this milestone. Actual live observations:
  GET returned "200 api-1"; a fragmented 6-byte binary POST echoed exactly; Host/query
  survived rewriting, spoofed identity and named hop fields did not; duplicate framing
  returned 400. examples/backend_server.py owns its Python HTTP server and closes it
  on normal/keyboard shutdown. Temporary milestone demos live only under ignored out/.

## 2026-10-13 — M3 routing and per-pool balancing

- PathRouter matches the longest case-sensitive segment prefix without query decoding.
  Routes borrow a pool and its policy; ReverseProxy owns both and freezes membership.
  No root route is configured in main. /api has two replicas and /static one.
- Each pool has an independent policy instance. BackendReservation increments once,
  then decrements by RAII on every upstream completion, timeout or exception. No
  network operation holds a pool lock: the runtime has only one request handler.
- CLI overrides live in main.cpp; ProxyConfig remains a plain struct. No configuration
  parser/framework or Windows socket port was introduced. Least connections is
  functional but normally sees ties in this sequential checkpoint.
- Before the final edge-case additions, all 35 portable tests and nine real Linux
  integration cases passed. Live observations included alternating api-2/api-1 IDs,
  static-1, /apix -> 404, a stopped backend -> bounded 502, client timeout -> 408,
  upstream/trickle timeout -> 504, reservation logs active=0, and descriptor count 4
  before/after repeated requests and disconnects.
- The final self-check tightens URI characters and 205 bodyless handling, adds
  configuration checks and a small-send-buffer test. Final verification is recorded
  below after rerunning the relevant builds and suites.

### Checkpoint feature status

| Status | Evidence/scope |
| --- | --- |
| Completed | M0–M3 foundation, Linux listener, bounded parser/forwarder, header rewrite, routing and balancing |
| Limited | Sequential orchestration; GET/POST close-only HTTP subset; health is setup state; least-connections demo sees ties |
| Pending | M4 epoll, M5 workers, M6 cache, M7 probes, M8 Docker and measured performance |

No throughput or completion-percentage claim is made. The original historical
scaffolding entry above is retained; this log corrects its earlier status.

### Final verification on 13 October 2026

- `cmake --build out/student-build -j 4` and
  `ctest --test-dir out/student-build --output-on-failure`: 35/35 portable tests passed.
- In Ubuntu WSL, with the repo-local CMake bin directory on PATH:
  `cmake --build out/linux-student-build -j 4` and
  `ctest --test-dir out/linux-student-build --output-on-failure`: 42/42 CTest checks
  passed (41 unit cases plus one suite containing nine live HTTP integration cases).
- The final builds used -Wall -Wextra and emitted no compiler warnings. Additional
  self-checks verified invalid descriptors and correct Via metadata for HTTP/1.0
  backend responses. The small-send-buffer test preserved all 128 KiB, including NUL.
- `python -m py_compile examples/backend_server.py
  tests/integration/proxy_integration_test.py` succeeded. `git diff --check` found
  no whitespace errors. README and the Markdown learning guide now describe the
  implemented checkpoint. The existing Word documents remain original artifacts.
- M0–M3 are complete for the agreed subset. The next implementation step is M4:
  preserve unfinished exchanges in a level-triggered epoll state machine, reusing
  the current parser, router, policies and rewriting code.
