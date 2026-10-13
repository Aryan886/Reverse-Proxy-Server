# Reverse Proxy Implementation Plan for Sol

Student project handoff and progress review preparation

Prepared 13 October 2026 for the review on 14 October 2026.

Build a small C++17 reverse proxy that the team can run, test and explain. Complete the existing foundation first, then add one visible feature at a time. The near-term target is a request travelling through the proxy to a selected backend and back to the client. The later target adds non-blocking I/O, workers, caching, health checks and measured performance.

## 1 Current position

The repository contains an OOP foundation, not a running reverse proxy. The five planned foundation types exist, but their presence does not mean Phase 1 is fully verified. There is no application entry point, socket listener, HTTP parser, routing, cache, health checker or Docker setup yet.

| Area | Evidence in the current repository | Status |
| --- | --- | --- |
| Build and tests | CMake, GoogleTest 1.15.2, CTest | Configured; old build cache uses a previous path |
| BackendServer | Host, port, connection count and health fields | Implemented; dedicated tests missing |
| BackendPool | Owning unique pointers and borrowed pointer list | Five unit tests exist |
| SchedulingPolicy | Abstract interface and virtual destructor | Implemented |
| RoundRobinPolicy | Rotates through the input vector | Not in CMake; health filtering missing |
| LeastConnectionPolicy | Selection loop exists | Returns inside the loop; incorrect selection |
| Network and HTTP | No corresponding source files | Planned |

Verification on 13 October 2026: a fresh MinGW build in out/review-build succeeded and CTest passed all six registered tests: five BackendPool cases and one trivial build sanity check. A green result cannot prove the scheduling algorithms work because they are not built by the current CMake targets. The historical progress summary describes intended behavior that differs from the present LeastConnectionPolicy code.

## 2 What the review can honestly demonstrate

Show the class relationships, ownership model, five pool tests and a short algorithm walkthrough. If the fixes and proxy milestones below are completed before the review, demonstrate those separately as newly completed work. Do not label planned features as implemented or infer 50 percent completion from class count or phase count.

Use a working vertical slice as the proposed halfway checkpoint: validated foundation, a Linux listener, bounded HTTP parsing, forwarding to real backends, path routing and observable load balancing, with passing tests. Ask the faculty to assess that evidence against their rubric. This plan does not certify a completion percentage.

<!-- pagebreak -->

## 3 Instructions to give Sol

Use this section together with the milestone details in this document.

Implement this project incrementally in the existing repository. First inspect the source, CMake files and tests. Start with M0, then proceed in dependency order. Keep each milestone runnable and reviewable. Preserve user changes. Update docs/implementation-notes.md after meaningful design decisions and keep the learning guide consistent with the actual code.

Before each milestone, state the data flow, files to touch and key decision in three to five lines. Then implement, build and test it. Report the commands run, what passed, limitations and the next milestone. Do not claim a test was run unless it was actually executed. If the environment blocks Linux work, record the exact blocker and continue with portable parser, router or cache tests where possible.

Write code an SY student can explain. Use descriptive names, ordinary loops, small functions, existing snake_case filenames and PascalCase class names. Keep the existing trailing underscore convention for private members. Use explicit std:: names in new code; remove header-level using namespace std only in files already being changed. Do not rewrite unrelated code for style.

Use the standard library, POSIX sockets, existing CMake and GoogleTest. Do not add Boost.Asio, an HTTP framework, a dependency injection framework, coroutines, a generic event framework or custom template machinery. The project is meant to teach these mechanisms. Use a real parser library only if the scope later expands to general-purpose HTTP compatibility, and explain that trade-off first.

Keep useful correctness features: input limits, timeouts, RAII, clear error messages and tests. Student-friendly means fewer concepts and narrower scope, not broken code or deliberately poor naming. Use inheritance for scheduling policies; use composition for the rest unless a second implementation creates a real need for an interface.

### Deliver after every milestone

- A working change with a concise explanation of the request flow.
- At least two meaningful tests for new non-trivial behavior, including a failure case.
- A reproducible demonstration and its actual observed output.
- A short answer to who owns each new resource and when it is released.
- An updated list of completed, partially completed and pending features.

### Work order

M0 foundation repairs → M1 Linux listener → M2 HTTP forwarding → M3 routing and balancing → M4 epoll → M5 workers → M6 cache → M7 health → M8 integration and measurement.

<!-- pagebreak -->

## 4 Architecture and scope decisions

Data flow: client socket → request parser → path router → optional cache lookup → backend selection and reservation → backend socket → response parser → optional cache insertion → client response. Header rewriting happens when creating the outgoing request or response, after validating the original message.

ReverseProxy owns configuration, pools, policies, router, cache and worker lifetime. A pool owns its BackendServer objects. Routes and active connections borrow pool or backend pointers. A connection owns its client and upstream sockets, buffers, send offsets and deadline. All pools outlive workers and health checking.

### Deliberate boundaries

Use Linux in WSL2 or Docker for network code. Keep portable foundation tests usable under MinGW. epoll is a Linux API; a Windows compiler cannot provide it. Add a clear CMake platform check around Linux executable targets rather than a partial WinSock port.

Start with one request per client connection and one upstream connection per request. Send Connection: close on both legs. Support GET and a small Content-Length POST to demonstrate body handling. Buffer complete bounded messages before forwarding. This makes errors and caching explainable; it trades large downloads and streaming for simpler control flow.

The first HTTP subset accepts origin-form targets such as /api/items?sort=new, a single valid Host and unambiguous framing. It rejects unsupported transfer encodings, upgrades, CONNECT, pipelining and persistent connections. This is a controlled educational HTTP subset, not full HTTP/1.1 interoperability. A client offering keep-alive still receives a close response; extra pipelined bytes are not processed as another request.

Initial limits are configuration defaults, not protocol limits: 16 KiB headers, 1 MiB request body, 1 MiB response body, 1 second upstream connect deadline, 5 seconds per I/O stage, 32 in-flight connections per worker, one worker initially and two later. Enforce a total stage deadline so one byte at a time cannot extend a request indefinitely.

### Choices that affect later work

Freeze routes and pool membership while running. Backend removal during active requests would invalidate borrowed pointers. Live reconfiguration is deferred; restart to change backends. Store settings in a plain ProxyConfig struct and keep example route setup in main.cpp. A configuration-file parser can be added later without changing HTTP or scheduling code.

Implement blocking forwarding first, then replace socket orchestration with a level-triggered epoll state machine. Reuse parser, router and policies. For multiple workers, choose one listener and epoll loop per worker using SO_REUSEPORT. Do not combine this with an acceptor-and-dispatch queue. That queue is a valid alternative but adds handoff and wakeup code here.

<!-- pagebreak -->

## 5 M0 Finish and verify the foundation

Touch the existing backend and scheduling files, both CMakeLists.txt files and mirrored tests. Do this before writing the proxy executable.

1. Move the return in LeastConnectionPolicy outside its loop. Start the best pointer at nullptr, skip null or unhealthy candidates, and keep the first candidate on equal counts. Return nullptr if there is no eligible backend.
2. Make RoundRobinPolicy examine at most the number of backends. Advance the cursor for each attempted position, skip null or unhealthy entries and return nullptr if none qualifies. Keep the cursor normalized to the current vector size; avoid relying on an indefinitely growing counter.
3. Rename decremenetConnections to decrementConnections in declaration, definition and all uses. Check decrement at zero. Include utility explicitly where std::move is used.
4. Reject an empty host, ports outside 1–65535 and a null unique_ptr passed to addBackend. Use clear invalid_argument messages for invalid setup. Expected runtime absence of a healthy backend still returns nullptr.
5. Make addBackend preserve both vectors if allocation fails. A simple approach is to reserve capacity in both vectors before either push, then insert the raw pointer and moved owner. Reserving may alter capacity, but cannot alter membership on failure. Do not leave a cached dangling pointer after an exception.
6. Add a scheduling library with its two .cpp files, expose its include directory and link it to backend. Link scheduling tests to scheduling and GTest::gtest_main. Use the existing CMake style.

### Tests that close real gaps

BackendServer: constructor values, invalid port or host, increment/decrement, zero lower bound and health toggles. Pool: null rejection, add/remove order and a surviving backend address staying stable after growth.

Round robin: A B C A sequence, empty list, single server, unhealthy middle server and all unhealthy. Least connections: counts 3/1/2 select B, ties select the first eligible server, empty list, and an unhealthy server with the smallest count is skipped. The 3/1/2 case catches the current early return bug.

Remove the sanity test only after real tests cover every Phase 1 class. Update the stale README layout. Leave the original historical notes intact; append corrections and verification results.

### Build commands on this Windows machine

The old build directory contains C:/Users/aryan/reverse_proxy paths. Use a new build folder rather than copying or editing generated CMake files.

```powershell
cmake -S . -B out/student-build -G "MinGW Makefiles"
cmake --build out/student-build -j 4
ctest --test-dir out/student-build --output-on-failure
```

Completion gate: all real foundation tests pass and both policy implementations are compiled with warnings enabled. Explain the failed regression test before showing its fix.

<!-- pagebreak -->

## 6 M1 Establish the Linux network path

Add src/network/socket.h and socket.cpp, src/proxy/reverse_proxy.h and reverse_proxy.cpp, plus src/main.cpp. Add targets to the root CMake file. Socket is a small move-only owner of an integer descriptor; its destructor closes a valid descriptor once. Avoid a hierarchy of socket factories or adapters.

Implement socket → bind → listen → accept. Use SO_REUSEADDR, clear startup errors and the configured listen port. At this milestone accept one client and send a fixed, valid HTTP response, then close that connection. This demonstrates the network path; it is not yet a reverse proxy.

Handle short writes with a send offset, interrupted calls with a retry, peer disconnects and broken pipes. On Linux use MSG_NOSIGNAL when sending. A successful send does not mean the whole message was sent. Treat recv returning zero as peer EOF. Include the operation and endpoint in errors; never catch and ignore failures. See references R4 and R5.

Use finite deadlines in the temporary sequential implementation. In particular, a blocking connect is not bounded by receive/send timeouts: use a non-blocking connect plus poll with a remaining deadline, then check SO_ERROR. M4 will replace this orchestration with epoll. See R7.

Completion gate: curl receives a fixed body, stopping the program frees the listening port, and a disconnected client does not terminate the process. Verify on Linux, not only with MinGW compilation.

## 7 M2 Parse and forward a bounded HTTP subset

Add src/http/http_request.h, http_response.h, http_parser.h and http_parser.cpp. Use plain structs for request and response data. A vector of name/value header pairs preserves repeated fields; a map alone would silently overwrite duplicates such as Set-Cookie.

Give the parser an explicit result: NeedMoreData, Complete or Error, plus bytes consumed and an error reason/status. A buffer may contain only half a header or part of a body. Keep parsing pure: no sockets, routing or logging inside it. Keep limits configurable. Use tests with raw byte strings before attaching it to the socket loop.

Parse the request line and CRLF-separated headers, then the body according to the validated length. Compare header names case-insensitively. Reject malformed names, control characters, folded headers, bad numeric lengths, repeated Content-Length, missing/duplicate Host and Transfer-Encoding with Content-Length. For this subset reject any Transfer-Encoding, Expect or upgrade request before contacting a backend. See R1 and R2.

Connect to one configured backend, serialize the validated request and forward its body. Parse and buffer the complete bounded backend response before sending client headers. M3 adds route selection. Add a small Python standard-library demo backend in examples/backend_server.py with /id, /echo, /slow and /health; return an exact Content-Length and a distinct server ID.

<!-- pagebreak -->

## 8 M2 Response handling and M3 routing

For the supported backend response subset, accept valid Content-Length framing or a close-delimited body. Handle bodyless final statuses such as 204 and 304. Reject unsupported Transfer-Encoding, protocol switching and informational responses with a documented 502 limitation in this controlled demo. Never assume requesting Connection: close prevents a backend from using chunked encoding. Reject an incomplete body or oversized response rather than forwarding partial data as success.

Use one small error-response helper with a correct Content-Length and Connection: close. Suggested mappings: malformed request 400, oversized body 413, oversized headers 431, unsupported Expect 417, unsupported method or transfer encoding 501, unmatched route 404, no healthy backend 503, upstream connection/protocol failure 502, upstream deadline 504, client read deadline 408. Log the specific cause without logging credentials or request bodies.

### Header rewriting

Add src/http/header_rewriter.h and header_rewriter.cpp. Strip Connection and the fields it names, plus known hop-by-hop fields on both directions. Validate framing first, remove old framing fields and serialize a correct Content-Length for the message you emit, respecting bodyless statuses. Preserve separate Set-Cookie fields.

Overwrite inbound X-Forwarded-For and X-Real-IP using the peer address in this single-proxy topology. Do not trust client-supplied values. Preserve the validated Host for the demo backends, append Via appropriately and add Connection: close. For unknown header edge cases, reject ambiguous messages rather than forwarding conflicting interpretations. R1 and R2 are the protocol references.

### Route and policy selection

Add src/routing/path_router.h and path_router.cpp. Use a small Route struct containing a prefix and a borrowed pool reference/pointer. Choose the longest matching path prefix with a segment boundary: /api matches /api and /api/users, but not /apix. Route using the path without its query, and forward the original target including the query. Keep the original prefix in the forwarded path; do not silently strip it.

Configure /api → API pool and /static → static pool. Each pool has its own policy instance so round-robin positions are independent. A root route is optional; leave it absent in the demo so unmatched paths visibly return 404. Keep paths case-sensitive and do not decode or normalize percent escapes during routing in this subset.

Completion gate: repeated /api/id requests alternate between two IDs, /static/id uses the other pool, /apix returns 404, POST /api/echo preserves its body, and stopping a selected backend yields a bounded 502 response. Least connections needs overlapping requests to demonstrate its advantage; a sequential demo often produces ties.

<!-- pagebreak -->

## 9 M4 Introduce non-blocking I O

This is the largest learning step. Add src/network/connection.h for per-client state and src/network/event_loop.h and event_loop.cpp for orchestration. Reuse the existing parser, router, rewriting and selection functions. Initially run exactly one event loop and one listener.

Use non-blocking sockets and level-triggered epoll. Edge-triggered mode is deferred because it adds drain-loop obligations without improving the course demonstration. Never put a blocking read, connect or DNS lookup in the event loop. Resolve configured backend hostnames at startup; this also supports Docker service names. References R6 and R7 explain readiness and connection completion.

| State | Work to attempt | Transition |
| --- | --- | --- |
| ReadingRequest | Append client bytes and parse | Complete request → connect upstream |
| ConnectingBackend | Await writable event and check SO_ERROR | Success → send request |
| SendingRequest | Send from saved offset | All bytes sent → read response |
| ReadingResponse | Accumulate and validate response | Complete response → send client reply |
| SendingResponse | Send from saved offset | All bytes sent → close both legs |

A cache hit later skips the upstream states. An error queues a local response if the client can still receive it, then closes. Reserve the backend once selection succeeds and release the reservation exactly once on upstream completion or any cleanup path.

Keep Connection objects in stable owning storage, such as unique_ptr values keyed by monotonically increasing connection IDs. epoll events carry connection ID plus client/upstream role, not a raw pointer into a movable vector. An event for an already-erased ID is ignored. This avoids stale events accidentally operating on a reused descriptor.

Subscribe to writable events only while connecting or while unsent output exists; otherwise a permanently writable socket can spin the CPU. EAGAIN means wait for readiness, not failure. Check error and hangup events, drain available readable bytes before treating EOF as final, and judge completeness using framing. Bound buffers and connection count; reject or close excess clients consistently.

Use steady_clock deadlines and a finite epoll_wait timeout. A simple scan of active connections for expired deadlines is acceptable at the demo scale. Make one cleanup path remove epoll registrations, close both sockets, erase state and release any backend reservation. Avoid detached threads and descriptor leaks.

Completion gate: one slow client does not freeze a second client; fragmented messages, small write chunks, upstream resets and timeouts behave correctly. Repeated disconnect tests leave connection counts at zero and descriptor usage approximately stable.

<!-- pagebreak -->

## 10 M5 Add two workers with clear ownership

Add src/proxy/worker_thread.h and worker_thread.cpp only now. Each worker owns a listener with SO_REUSEPORT, an epoll instance and its connection map. Set SO_REUSEPORT on every listener before bind. Start with two workers; do not promise equal request distribution by the kernel. The main thread owns and joins them. Reference R8 covers the Linux socket option.

Pool membership, endpoints and routes stay fixed. Protect shared backend health, active counts and each pool's scheduling cursor with one mutex per pool. Add straightforward acquireBackend(policy), releaseBackend(server) and setBackendHealthy(server, value) operations to BackendPool. Selection and count increment happen under the same lock; otherwise two workers can both select from a stale minimum. Existing policy methods remain usable by unit tests.

Keep ordinary ints and bools behind that mutex instead of mixing atomics with incomplete multi-step locking. All runtime reads of those fields must follow the same rule. Do not let workers bypass the pool through unsynchronized getters. getBackends is for setup/tests or pool-internal protected operations while running. Never hold the pool lock during network I/O.

For shutdown, block SIGINT and SIGTERM before creating threads so they inherit the mask. Let main use sigtimedwait with short waits and update a shared atomic stop flag in normal code. This avoids complex asynchronous signal handlers and lets main also check reported worker failures. Workers stop accepting, finish or time out in-flight requests within a drain deadline, clean up and join. Fatal worker exceptions are caught at the thread boundary, logged and reported to the owner.

Completion gate: parallel slow requests reach multiple backends, reservations return to zero after failures, Ctrl+C finishes within the shutdown deadline and repeated starts reuse the port. Do not claim thread safety merely because counters are atomic or tests ran once.

## 11 M6 Add a small LRU and TTL cache

Add src/cache/response_cache.h and response_cache.cpp. Use unordered_map for lookup and list for most-recently-used order. Store a complete response value, expiry and list iterator. A hit moves the list node to the front; insertion beyond capacity evicts the tail. Expired entries are misses. Use steady_clock and pass a time point into cache operations for deterministic tests.

Use one cache mutex; return a response copy while protected and release the lock before socket writes. Start with 32 entries, 64 KiB maximum per cached response and a 2 MiB total byte cap. Store sanitized end-to-end response data, then regenerate per-connection framing and diagnostics when serving it. Do not store socket state, borrowed temporary buffers or a previous client's forwarding fields.

<!-- pagebreak -->

## 12 M6 Cache policy and M7 health checks

Keep cache eligibility deliberately narrow. Enable it only for a controlled /static demo route with identical content on all replicas. Use a key containing route identity, validated Host and the complete path plus query. Different query strings must not collide. The demo backend must return the same representation for all admitted request headers; bypass Accept-Encoding and other representation negotiation inputs until variants are implemented.

Cache only complete GET 200 responses with an explicit positive public max-age. Bypass requests with Authorization, Cookie, Range, conditional headers, Cache-Control or Pragma. Bypass responses with Set-Cookie, Vary, private, no-store, no-cache or unsupported cache directives. If freshness metadata cannot be interpreted, bypass rather than guess. Account for origin Date/Age and residence time, cap freshness at the configured TTL and send a correct updated Age on hits. This is a conservative educational cache, not full HTTP cache compliance. See R3.

Tests: hit/miss, replacement, capacity eviction, access refreshing LRU order, expiry at the exact boundary, zero capacity, oversized entry, different Host/query keys and private responses bypassing storage. Do not use sleep in unit tests. Cache disabling must leave forwarding functional.

### Active health checking

Add src/health/health_checker.h and health_checker.cpp. One owned background thread probes immutable endpoint snapshots. Begin with TCP connect checks; then use the same bounded HTTP subset for GET /health and require 200. Record elapsed probe time separately from health; an RTT failure threshold is optional and disabled by default to avoid unstable local demos.

Suggested settings: probe every 2 seconds, connect/probe deadline 500 ms, two consecutive failed probes to mark unhealthy and two successes to recover. These are tunable project defaults. Publish state under the relevant pool mutex; do all network work outside it. Use stop-aware waiting and finite probe deadlines, then join before destroying pools.

Request forwarding errors can fail the current request without changing hysteresis counters; let the checker own health transitions initially. Once marked unhealthy, a backend is excluded from new selections. In-flight connections follow their existing deadline. All unhealthy gives 503. Do not automatically replay a partially sent POST; the backend may already have acted on it. A healthy selection can still fail before the next probe, which remains a 502 or 504 case.

Completion gate: stop one backend, observe its logged health transition, send new requests only to the survivor, restart it and observe recovery. Expect several seconds of detection delay; record the measured time instead of promising immediate failover.

<!-- pagebreak -->

## 13 M8 Package and measure the project

Add a Linux Dockerfile, compose.yaml, .dockerignore and examples/backend_server.py if not already added in M2. Compose runs the proxy and three backend processes: two API replicas and one static service. Address other containers by service name, not localhost. Use a pinned base-image version, install only necessary build/runtime tools and keep the initial setup understandable. Existing GoogleTest fetching needs network on first configure unless dependencies are cached.

Keep root CMakeLists.txt and tests/CMakeLists.txt as the build entry points. Add subsystem targets only as implemented. Link thread-using targets to Threads::Threads. Record Linux build commands in README, separate from portable Windows unit-test commands.

```bash
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Debug
cmake --build build-linux -j 4
ctest --test-dir build-linux --output-on-failure
docker compose up --build
curl -i http://localhost:8080/api/id
curl -i http://localhost:8080/static/item
```

The Docker and curl commands become executable demonstrations only after their targets and routes exist. Run them from the Linux project directory. For PowerShell use curl.exe explicitly where curl may be an alias.

### Verification matrix

| Layer | Required evidence |
| --- | --- |
| Unit | Policies, pool, parser, router, header rewrite, cache and health transitions |
| HTTP integration | Split headers/body, invalid framing, POST echo, headers and correct error codes |
| Concurrency | Slow and fast clients together, backend reservations, bounded shutdown |
| Failure | Dead backend, timeout, all unhealthy, recovery and abrupt client disconnect |
| Resources | Bounded buffers/cache, repeated connections, no obvious descriptor growth |
| Performance | Direct backend versus proxy, cache off/on, same hardware and request body |

Use wrk in Linux only after correctness passes. Run a short warmup and three equal-duration measurements, for example 30 seconds with 2 threads and 20 connections. Record throughput, latency distribution, HTTP errors, socket errors, build mode, backend delay and response size. Connection: close changes the cost profile; state it in the report. Never invent or extrapolate a throughput result.

Completion gate: another teammate can follow the README, run tests, demonstrate routing/cache/failure recovery and explain one complete request. TLS, IP hash, host/method routing, chunked encoding, keep-alive, live configuration and edge-triggered epoll remain explicit extensions.

<!-- pagebreak -->


### References and starting evidence

Repository inspected at commit f4396da on 13 October 2026. Starting inputs: progress_summary03.md, reverse_proxy_claude_project_context (1).md, the source tree, both CMake files and existing tests. The historical teaching instructions in the context file are background; this handoff supplies the present implementation task.

R1 HTTP message framing and parsing: https://www.rfc-editor.org/rfc/rfc9112.html — sections 2, 5, 6 and 9.

R2 HTTP semantics and forwarding: https://www.rfc-editor.org/rfc/rfc9110.html — sections 5, 7.6 and 15.

R3 HTTP caching: https://www.rfc-editor.org/rfc/rfc9111.html — sections 3, 4 and 5.2.

R4 Sending bytes: https://man7.org/linux/man-pages/man2/send.2.html

R5 Receiving bytes: https://man7.org/linux/man-pages/man2/recv.2.html

R6 Event readiness: https://man7.org/linux/man-pages/man7/epoll.7.html

R7 Connection completion: https://man7.org/linux/man-pages/man2/connect.2.html

R8 Socket options: https://man7.org/linux/man-pages/man7/socket.7.html
