# Current scope — 27 October 2026

A second simplification keeps the same GET demo and OOP classes:

- Removed the manual backend health flag and the scheduling branches that skipped
  unhealthy servers. Round robin now selects one server and advances its index.
- Moved connection counting around the forwarding try/catch in handleClient().
  The count is decreased in one place after success or failure, and forwarding
  no longer catches an exception just to wrap and rethrow it.
- Removed the extra HTTP-format check on backend replies. Empty replies still
  fail, but local demo backends are responsible for valid HTTP responses.
- Duplicate route prefixes use the existing first-match rule. Port validation
  remains in socket setup instead of also running in the proxy constructor.
- Split the request-line and HTTP-version checks into separate, readable steps.
  Input validation, buffer limits, timeouts and complete reads/writes remain.
- Kept both scheduling policies to demonstrate inheritance and virtual dispatch.
  Removing least connections would be smaller, but would lose that useful example.
- Updated affected unit tests and guides. The live integration suite remains;
  the redundant proxy configuration test target was removed.

Use a separate policy object for each pool, configure it before run(), and use the
included local backends. This keeps the request flow simple without adding another
ownership helper or error-handling abstraction.

Verification on 27 October 2026:

- MinGW: all 20 portable CTest cases passed. The routing cases were rebuilt and
  passed again after adding an assertion for duplicate prefixes.
- Ubuntu WSL: all 25 CTest checks passed, including the six live integration
  cases for routing, round robin, GET-only requests, partial input, timeout
  recovery and a stopped backend.
- Both C++ builds completed with warnings enabled and no compiler warnings.
- C++ source/header files total 514 lines, down from 541 in the previous version.

## Previous simplification — 21 October 2026

The project now prioritizes a small OOP learning exercise. The entries below
this section describe the earlier, broader implementation, not the current scope.

- BackendPool stores server values in one vector. Runtime pool removal and the
  separate owning/borrowed pointer collections were removed.
- SchedulingPolicy now accepts a BackendPool reference. Both derived algorithms
  remain, with direct tests of virtual dispatch and server selection.
- main.cpp creates the pools, policy objects and proxy directly. The former
  ProxyConfig hierarchy and proxy CLI parser were removed.
- Routing uses the first matching registered prefix rather than selecting the
  longest prefix automatically. Add specific prefixes first.
- Networking uses a small blocking Socket class, numeric IPv4 addresses,
  loopback listening and five-second per-operation timeouts. DNS caching,
  poll-based deadlines and shutdown cancellation were removed.
- The HTTP scope is GET only. The proxy reads a request line, constructs a fresh
  backend request and relays a buffered response from a backend that closes.
  POST bodies, general header rewriting, detailed response framing validation,
  IPv6 Host validation and special HTTP status rules were removed.
- Basic limits, port/path validation, partial-read/write loops, disconnect errors
  and socket destruction remain. The connection count is released on forwarding
  success or failure without a separate reservation class.
- The README, learning guide and teacher demo guide now describe this scope.
  Older Word documents and the original plan/context are historical references.

Trade-offs: a vector of values is easier to teach but may invalidate element
pointers when it grows; configure membership before run(). Sequential blocking
I/O keeps control flow simple but one slow client delays others. Timeouts reset
for each operation, so they are not an overall request deadline. Local backends
are trusted to send complete ordinary responses.

The current design introduces no new library. GoogleTest remains optional at
build time; BUILD_TESTING=OFF builds the demo without fetching it.

Verification on 21 October 2026:

- MinGW: built out/oop-student-build and passed all 21 portable CTest cases.
- Ubuntu WSL: built out/oop-linux-build and passed all 28 CTest checks
  (27 unit cases plus one integration suite containing six live cases).
- The live cases covered alternation, routing, GET-only behavior, fragmented
  request input, incomplete-request timeout/recovery and stopped-backend recovery.
- Both test builds reused the existing GoogleTest source under out/our_build/_deps.
- Configured and built out/oop-demo with BUILD_TESTING=OFF, without fetching tests.
- The C++ builds used -Wall -Wextra with no warnings. Python bytecode compilation
  and git diff --check passed.
- C++ source/header line count decreased from 1,273 to 541. No automatic
  health-checking, throughput or full HTTP-compatibility claim is made.

---

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
