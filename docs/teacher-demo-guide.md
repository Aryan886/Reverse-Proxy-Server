# Reverse Proxy: Teacher Demonstration Guide

A practical runbook for a 10–12 minute SY first-semester project demonstration.

This guide describes the current implementation: a sequential C++17 HTTP reverse
proxy with path routing, configurable backend selection, bounded forwarding, and
automated tests. Commands below are rehearsal instructions, not a record that they
were executed when this guide was written.

## 1. What to explain first

Suggested introduction:

> “Our project is a C++ reverse proxy. A client sends requests to one address.
> The proxy parses the HTTP request, chooses a backend pool using the URL path,
> selects a server using a scheduling policy, and forwards the response.
> We demonstrate this using three small Python backend servers.”

```text
Client: curl or browser
          |
          | HTTP to 127.0.0.1:8080
          v
     C++ ReverseProxy
     Parse -> Route -> Select backend -> Forward
          |
          +-- /api ------+-- 127.0.0.1:9001  api-1
          |              +-- 127.0.0.1:9002  api-2
          |
          +-- /static ------ 127.0.0.1:9003  static-1
```

- **Routing** chooses a pool: `/api` or `/static`.
- **Load balancing** chooses a server within that pool.
- An **API backend** is an application server that handles API requests. Here it
  returns controlled demo responses; it does not implement a database application.
- All three backends run the same Python script with different ports and labels.
- `api-1`, `api-2`, and `static-1` identify responses. The proxy selects endpoints
  by host and port, not by these labels.
- The static backend returns fixed demo content; it is not a general file server.

## 2. Prepare before the review

Use Ubuntu/Linux or Ubuntu inside WSL. The proxy executable uses Linux socket APIs.
Building portable tests on Windows alone does not build the running proxy.

Use **Bash in Ubuntu/WSL for every command in this guide**, unless explicitly
marked PowerShell. Open five terminal tabs and enlarge the font.

In every tab, enter the repository directory. On this Windows machine, from WSL:

```bash
cd /mnt/c/Users/aryan/DEV_Maxxing/reverse_proxy
```

If your checkout is elsewhere, substitute its Linux path.

### Install prerequisites if needed

On Ubuntu:

```bash
sudo apt update
sudo apt install -y build-essential cmake git python3 curl
```

CMake must be at least 3.16, and the compiler must support C++17. The first CMake
configuration downloads GoogleTest, so prepare the build while internet is available.
Python must be available at configuration time for CMake to register the integration test.

If using the existing repo-local Linux CMake instead of installing CMake:

```bash
export PATH="$PWD/out/tooling/cmake-3.31.8-linux-x86_64/bin:$PATH"
```

Use that alternative only if the directory exists, and repeat it in terminal tabs
that need `cmake` or `ctest`.

### Build and rehearse the tests

Run from the repository root:

```bash
cmake -S . -B out/teacher-demo-build -DCMAKE_BUILD_TYPE=Debug
cmake --build out/teacher-demo-build -j 2
ctest --test-dir out/teacher-demo-build --output-on-failure
```

Stop and resolve failures before presenting. Use the fresh test count printed by
CTest; do not present an older result as a new run. Do not reuse a build cache
created for Windows or another checkout path.

To run just the live integration suite:

```bash
python3 tests/integration/proxy_integration_test.py \
  --proxy out/teacher-demo-build/reverse_proxy \
  --backend examples/backend_server.py
```

The suite starts and stops its own test processes on temporary ports. It is also
included in CTest when Python is found, so there is no need to run it twice during
the presentation.

### Checklist

- [ ] Build succeeds and tests pass.
- [ ] Ports 8080, 9001, 9002, and 9003 are available.
- [ ] Five Bash tabs are open in the repository directory.
- [ ] Source files from section 7 are ready in the editor.
- [ ] A screenshot of the fresh test result is saved.
- [ ] A short backup recording is available if the live environment fails.
- [ ] Both teammates can trace one complete request and explain one failure case.

## 3. Start the demonstration

Run one foreground process in each of the first four tabs. Leave those tabs open.

| Tab | Purpose |
| --- | --- |
| 1 | API backend 1 |
| 2 | API backend 2 |
| 3 | Static backend |
| 4 | C++ proxy and its logs |
| 5 | Client requests and test commands |

**Tab 1**

```bash
python3 examples/backend_server.py --port 9001 --id api-1
```

Expected startup message: `Backend api-1 listening on port 9001`.

**Tab 2**

```bash
python3 examples/backend_server.py --port 9002 --id api-2
```

**Tab 3**

```bash
python3 examples/backend_server.py --port 9003 --id static-1
```

**Tab 4**

```bash
out/teacher-demo-build/reverse_proxy
```

Expected startup message:

```text
Proxy listening on port 8080 (sequential HTTP demo)
```

Keep tabs 4 and 5 visible side by side. The following requests all run in tab 5.
Example output below shows the relevant fields; dates and header order can differ.

## 4. Live demo script

### A. Forwarding and round robin

```bash
for request in 1 2 3 4; do
  curl -sS http://127.0.0.1:8080/api/id
done
```

Expected on a fresh proxy:

```text
api-1
api-2
api-1
api-2
```

Earlier API requests may change which identity appears first. Alternation is what
matters. Avoid concurrent browser requests during this demonstration.

Say:

> “Every request goes to the same client-facing address. The proxy distributes
> successive API requests between two backend processes using round robin.”

Show the proxy log lines naming ports 9001 and 9002. Then show a complete response:

```bash
curl -i http://127.0.0.1:8080/api/id
```

Point out `200 OK`, `X-Backend-ID`, and the response body.

### B. Path routing to a different pool

```bash
curl -i http://127.0.0.1:8080/static/id
curl -i http://127.0.0.1:8080/static/item
```

Expected: `200 OK`, identity `static-1`, and content:

```text
Shared static demo content
```

Say:

> “The `/static` path selects a different pool. Our current configuration gives
> that pool one backend on port 9003.”

### C. Forward a POST body

```bash
curl -i --data-binary 'hello teacher' http://127.0.0.1:8080/api/echo
```

Expected: `200 OK` and the exact body `hello teacher`.

Say:

> “The proxy forwards request bodies as well as GET requests. The backend echoes
> the bytes, demonstrating the request and response path.”

The echo endpoint does not add a newline, so the next shell prompt may appear
immediately after its body.

### D. Reject an unmatched path

```bash
curl -i http://127.0.0.1:8080/apix
```

Expected: `404 Not Found`.

Say:

> “Matching respects path-segment boundaries. `/api/id` matches `/api`, but
> `/apix` does not.”

### E. Show header rewriting

```bash
curl -sS 'http://127.0.0.1:8080/api/headers?demo=teacher' \
  -H 'X-Forwarded-For: 1.2.3.4' \
  -H 'Connection: X-Remove' \
  -H 'X-Remove: should-disappear'
```

Expected JSON values include:

```json
{
  "target": "/api/headers?demo=teacher",
  "host": "127.0.0.1:8080",
  "forwarded_for": "127.0.0.1",
  "real_ip": "127.0.0.1",
  "removed_header": null
}
```

Say:

> “The path and query survive forwarding. We replace a client-supplied forwarding
> identity with the TCP peer address and remove connection-specific headers.”

These address values assume the local command above. Remote requests can produce
different peer addresses.

### F. Handle a slow backend

```bash
curl -i --max-time 10 'http://127.0.0.1:8080/api/slow?seconds=7'
```

Expected: `504 Gateway Timeout`, around five seconds into the upstream
response-read stage, using the default settings.

Say:

> “A slow backend cannot hold this exchange indefinitely. The proxy applies a
> stage deadline and returns a controlled error.”

The Python server may subsequently log a client disconnect because its delayed
response finishes after the proxy closes the upstream connection. That is expected.

### G. Handle a stopped backend

Press **Ctrl+C in tab 2 only** to stop `api-2`. Then run in tab 5:

```bash
for request in 1 2 3 4; do
  curl -sS -o /dev/null -w '%{http_code}\n' http://127.0.0.1:8080/api/id
done
```

Expected: two `200` responses and two `502` responses, alternating.

Say:

> “Connections to the stopped backend produce 502 errors, but the proxy stays
> running and requests selected for the surviving backend still succeed.
> Automatic health detection and retry are future work.”

Do not call this automatic failover: the failed backend remains in rotation.

Restart it in tab 2:

```bash
python3 examples/backend_server.py --port 9002 --id api-2
```

Repeat the four-request loop. After startup, all four requests should return `200`.

### H. Demonstrate graceful shutdown

Press **Ctrl+C in tab 4**. Expected:

```text
Proxy stopped; sockets released
```

Restart it:

```bash
out/teacher-demo-build/reverse_proxy
```

Then request `/api/id` again. This demonstrates that the listener can release and
reuse its port. At the end of the review, stop the proxy and all three backends
with Ctrl+C in their respective tabs.

## 5. Show test evidence

Run:

```bash
ctest --test-dir out/teacher-demo-build --output-on-failure
```

Explain examples rather than reading every test name:

| Area | What the tests establish |
| --- | --- |
| Backend and pool | Valid configuration, ownership, membership, connection counts |
| Scheduling | Rotation, minimum-count selection, unhealthy entries, ties |
| HTTP | Fragmented input, invalid framing, body limits, response validation |
| Routing | Longest matching prefix and path boundaries |
| Networking | Bounded operations and socket behavior |
| Integration | Real forwarding, backend errors, deadlines, cleanup, shutdown |

For a focused least-connections demonstration:

```bash
ctest --test-dir out/teacher-demo-build \
  -R 'LeastConnectionPolicyTest.SelectsMinimumAfterExaminingEveryBackend' -V
```

Open the corresponding test in
[scheduling_policy_test.cpp](../tests/scheduling/scheduling_policy_test.cpp).
It assigns counts **3, 1, 2** and checks that the second backend is selected.

Explain:

> “Least-connections selection is implemented and tested. In this sequential
> runtime, requests usually encounter zero active counts, so ties select the
> first eligible backend. Its advantage needs overlapping requests.”

Optional configuration demonstration: stop the proxy, run it with
`--policy least-connections`, and request `/api/id`. Restore the default
round-robin command before repeating the alternation demo.

## 6. HTTP methods and demo endpoints

The C++ proxy accepts **GET and POST using HTTP/1.1**. Other methods such as PUT,
DELETE, HEAD, and OPTIONS return `501 Not Implemented` for well-formed requests.
POST bodies use Content-Length; chunked requests are unsupported.

| Method | Demo path | Purpose |
| --- | --- | --- |
| GET | `/api/id`, `/static/id` | Identify the selected backend |
| GET | `/api/health` | Return the fixed text `healthy` |
| GET | `/static/item` | Return fixed content |
| GET | `/api/headers` | Inspect the received target and selected headers |
| GET | `/api/slow?seconds=7` | Simulate an upstream delay |
| POST | `/api/echo`, `/static/echo` | Echo the request body |

The common Python script accepts these GET operations under either `/api` or
`/static`. The proxy preserves the prefix; the backend interprets it. The health
endpoint does not mean the proxy performs active health checks.

Optional unsupported-method demonstration:

```bash
curl -i -X PUT http://127.0.0.1:8080/api/echo
```

Expected: `501 Not Implemented`.

## 7. Explain the source and OOP design

| File | What to show |
| --- | --- |
| [main.cpp](../src/main.cpp) | Default ports, route setup, CLI configuration |
| [backend_server.py](../examples/backend_server.py) | `--id`, `server_id`, GET/POST demo handlers |
| [backend_server.h](../src/backend/backend_server.h) | Encapsulation of endpoint and server state |
| [backend_pool.h](../src/backend/backend_pool.h) | Composition and ownership through `unique_ptr` |
| [scheduling_policy.h](../src/scheduling/scheduling_policy.h) | Abstract interface and virtual destructor |
| [round_robin_policy.cpp](../src/scheduling/round_robin_policy.cpp) | Rotation through eligible backends |
| [least_connection_policy.cpp](../src/scheduling/least_connection_policy.cpp) | Select the minimum connection count |
| [path_router.cpp](../src/routing/path_router.cpp) | Longest segment-prefix matching |
| [http_parser.cpp](../src/http/http_parser.cpp) | Validate and parse HTTP messages |
| [header_rewriter.cpp](../src/http/header_rewriter.cpp) | Construct forwarded messages |
| [socket.h](../src/network/socket.h) | Move-only socket ownership and RAII |
| [reverse_proxy.cpp](../src/proxy/reverse_proxy.cpp) | Complete request flow and reservation cleanup |

Suggested explanation:

> “Inheritance and runtime polymorphism let the proxy use different scheduling
> policies through one interface. Composition connects the other components.
> Pools own backend objects; sockets own file descriptors. RAII releases
> resources when scopes end, including error paths.”

Be ready for these questions:

| Question | Answer |
| --- | --- |
| Why use a proxy? | It gives clients one entry point and centralizes routing and backend selection. |
| Why Python backends? | They provide controlled test responses while the project implementation remains the C++ proxy. |
| Is this L4 or L7? | L7: it understands HTTP and routes using paths, although its transport is TCP. |
| Does non-blocking socket I/O make it concurrent? | No. The current orchestration completes one client exchange before handling the next. |
| How would you add another algorithm? | Implement SchedulingPolicy and wire it into configuration. |
| Where are the server identities? | The Python `--id` argument becomes `server.server_id` and appears in the body/header. |
| What happens if a backend fails? | Selected requests receive a bounded error; automatic exclusion is not implemented. |

## 8. Presentation timing and scope

| Time | What to demonstrate |
| --- | --- |
| 0–1 minute | Problem and architecture |
| 1–4 minutes | Alternation, static routing, POST, unmatched path |
| 4–6 minutes | Header rewriting and timeout |
| 6–8 minutes | Stopped backend, recovery, shutdown/restart |
| 8–10 minutes | Class relationships and one algorithm test |
| 10–12 minutes | Test summary, limitations, questions |

For a five-minute slot, show alternation, static routing, POST, one failure case,
and the prepared test result. Keep the rest ready for questions.

One teammate can explain backend ownership, scheduling, and routing; the other
can explain sockets, parsing, rewriting, and failure handling. Both should
understand the whole request path.

Accurate closing statement:

> “The current version implements and tests the core sequential proxy:
> forwarding, routing, scheduling, header rewriting, timeouts, and cleanup.
> Concurrency, caching, active health checks, and deployment/performance work
> are remaining extensions.”

Do not claim high throughput, automatic failover, caching, full HTTP support, or
90% completion of the original full scope based only on this demonstration.
Use the teacher-approved requirements to assess completion.

## 9. Troubleshooting

| Symptom | Check or fix |
| --- | --- |
| `cmake: command not found` | Install prerequisites or use the existing repo-local Linux CMake path above. |
| No `reverse_proxy` executable after building | Configure and build from Linux/WSL, not Windows CMake. |
| CMake refers to another checkout or generator | Choose a new build directory instead of reusing that cache. |
| `Address already in use` | Find the process using the port with `ss -ltnp`; stop your own previous demo process in its terminal. |
| `Connection refused` on port 8080 | Confirm the proxy started successfully and is still running. |
| `502` from `/api/id` | Confirm both API backends are running on the configured ports; inspect the proxy error log. |
| Browser at `/` shows `404` | There is no root route. Open `http://127.0.0.1:8080/api/id`. |
| IDs start with `api-2` | Earlier requests advanced the policy. Check alternation rather than the initial label. |
| IDs always show `api-1` | Check whether the proxy was started with least-connections; restore the default command. |
| Slow demo seems to pause other requests | Expected: the current request orchestration is sequential. |
| Bash loop fails in PowerShell | Run it in Ubuntu/WSL. For individual Windows requests, use `curl.exe`. |
| CTest does not list the integration suite | Install Python 3 and rerun CMake configuration. |

To inspect listening ports from Bash:

```bash
ss -ltnp
```

If port 8080 is unavailable, start the proxy with `--port 8081` and change all
client URLs accordingly. The backend ports remain unchanged.

## 10. If demonstrating a deployed instance

Keep the same architecture and replace `127.0.0.1:8080` in client URLs with the
deployed host and exposed proxy port. The backend addresses remain local to the
server if all processes run on one VM.

For example, in Windows PowerShell after substituting the actual address:

```powershell
curl.exe -i http://VM_PUBLIC_IP:8080/api/id
curl.exe -i http://VM_PUBLIC_IP:8080/static/item
curl.exe -i --data-binary "hello teacher" http://VM_PUBLIC_IP:8080/api/echo
```

Confirm remote access before class. A deployed service should have process
supervision so it survives closing SSH. Keep backend ports private and retain
the local WSL demonstration as a fallback.

Deployment is optional for this local runbook; these instructions do not imply
that hosting or persistent services have already been configured.
