# Reverse Proxy Server (C++17)

A Semester 3 OOP project at Vishwakarma Institute of Technology, Pune.
This review checkpoint implements M0–M3 of the [implementation plan](docs/sol-implementation-plan.md):
a small, sequential Linux reverse proxy with bounded HTTP forwarding, path routing
and load balancing. The scope is suitable for explaining a first month of work by
two students; it is not a claim about historical effort or completion percentage.

## What works

- Validated backend objects and owning pools; tested round-robin and least-connections policies.
- Linux listener, move-only socket ownership, bounded connect/read/write stages and graceful shutdown.
- GET and Content-Length POST, fragmented input and complete bounded response validation.
- Longest path-prefix routing with segment boundaries; original prefix and query preserved.
- Independent policies for /api and /static; untrusted forwarding headers replaced with the TCP peer address.
- Correct local errors, hop-header removal, repeated Set-Cookie preservation and backend reservation cleanup.

Request flow:

```text
client -> bounded parser -> path router -> pool policy
       -> connect backend -> rewrite/send request -> validate response
       -> release backend reservation -> rewrite/send reply -> close sockets
```

Pools own their BackendServer objects through unique_ptr. Routes borrow pool and
policy pointers. ReverseProxy owns the pools, policies and startup-resolved addresses;
membership stays fixed until restart. Local Socket values close their descriptors
on every exit path. A small BackendReservation releases the selected server's count
even if connect, send or parsing fails.

## Build and test

Use a C++17 compiler, CMake >= 3.16 and internet on the first configure for
GoogleTest v1.15.2. Python 3 enables the Linux integration test and demo backends.

On Linux or Ubuntu in WSL, run from the repository directory:

```bash
cmake -S . -B out/linux-student-build -DCMAKE_BUILD_TYPE=Debug
cmake --build out/linux-student-build -j 4
ctest --test-dir out/linux-student-build --output-on-failure
```

This machine has a repo-local Linux CMake at
out/tooling/cmake-3.31.8-linux-x86_64/bin. If WSL has no system CMake, first run:

```bash
export PATH="$PWD/out/tooling/cmake-3.31.8-linux-x86_64/bin:$PATH"
```

On Windows, the portable backend, scheduling, HTTP and routing tests build with MinGW.
The POSIX executable and socket tests are deliberately Linux-only:

```powershell
cmake -S . -B out/student-build -G "MinGW Makefiles"
cmake --build out/student-build -j 4
ctest --test-dir out/student-build --output-on-failure
```

Do not reuse the old build/ cache, which references a previous checkout path.

The live suite starts and stops its own backends on temporary ports:

```bash
python3 tests/integration/proxy_integration_test.py \
  --proxy out/linux-student-build/reverse_proxy \
  --backend examples/backend_server.py
```

It exercises alternation, static routing, binary POST fragments, rewriting, invalid
framing, body/header limits, close-delimited responses, repeated cookies, disconnects,
dead backends, stage deadlines, descriptor cleanup, CLI errors and bounded shutdown.
CTest includes this suite when Python is found.

Verified on 13 October 2026: 35 portable MinGW tests and 42 Linux CTest checks
passed; the Linux total includes 41 unit cases and the nine-case integration suite.

## Five-minute live demonstration

In three Linux/WSL terminals, start one command in each:

```bash
python3 examples/backend_server.py --port 9001 --id api-1
python3 examples/backend_server.py --port 9002 --id api-2
python3 examples/backend_server.py --port 9003 --id static-1
```

In a fourth terminal:

```bash
out/linux-student-build/reverse_proxy
```

Then run these client commands from another terminal:

```bash
for request in 1 2 3 4; do curl -sS http://localhost:8080/api/id; done
curl -sS http://localhost:8080/static/id
curl -i http://localhost:8080/apix
curl -i --data-binary 'hello students' http://localhost:8080/api/echo
curl -i 'http://localhost:8080/api/slow?seconds=7'
```

The IDs alternate api-1, api-2, api-1, api-2. Static returns static-1. The boundary
example returns 404, POST echoes its body, and the seven-second backend delay
returns 504 after the default five-second response-read stage deadline.
PowerShell users should use curl.exe.

Stop the api-2 backend and send four more /api/id requests. Selected requests to
that endpoint return 502; the survivor still returns 200. This version does not
probe health or automatically retry a failed request. Restart api-2 to restore
successful forwarding. Ctrl+C stops the proxy and releases its listening port.

## Configuration and boundaries

```bash
out/linux-student-build/reverse_proxy --help
out/linux-student-build/reverse_proxy --port 8081 \
  --api localhost:9101 --api localhost:9102 --static localhost:9103 \
  --policy round-robin --io-timeout-ms 5000 --connect-timeout-ms 1000
```

The first --api or --static replaces that pool's defaults; repeated arguments add
replicas. --policy least-connections uses the same selection interface. In this
sequential version, requests usually see zero counts and ties select the first
eligible backend. Its advantage requires overlapping requests in M4.

ProxyConfig and the example setup in main.cpp keep settings simple. Limits default
to 16 KiB of headers and 1 MiB for each body; timeouts apply to the whole stage,
rather than restarting after each byte. Backend DNS resolution happens once at
startup, outside request deadlines. Restart after endpoint/DNS changes.
An empty or entirely unhealthy matched pool maps to 503; unmatched paths map to 404.
Health values are setup/test state until active probes are added.

This educational HTTP subset uses one request and one upstream connection per
client, fully buffers messages, and sends Connection: close. HTTP/1.1 origin-form
GET/POST requests need one Host; the Host subset accepts ordinary DNS names or
bracketed hexadecimal IPv6 with an optional numeric port. It does not admit every
URI host form. It rejects transfer encodings, Expect, upgrades, CONNECT,
informational upstream replies, ambiguous framing and buffered pipelining.
Later pipelined bytes are never treated as a second request. Upstream HTTP/1.0
and HTTP/1.1 responses can use Content-Length or close-delimited framing.
204/205/304 responses carry no body. Unsupported upstream replies produce 502.

Error mapping: 400 malformed request, 404 no route, 408 client read deadline,
413 body limit, 417 Expect, 431 header limit, 501 unsupported client feature,
502 upstream connection/protocol failure, 503 no eligible backend, 504 upstream deadline.

M4 epoll, M5 workers, M6 cache, M7 active health checks and M8 Docker/performance
measurement remain pending. This checkpoint handles one active exchange at a time;
a slow client can delay other clients until its stage deadline. It provides no TLS,
streaming, chunked decoding, keep-alive or live reconfiguration.

Protocol choices follow [RFC 9112](https://www.rfc-editor.org/rfc/rfc9112.html) and
[RFC 9110](https://www.rfc-editor.org/rfc/rfc9110.html). Socket completion and writes
follow the Linux [connect](https://man7.org/linux/man-pages/man2/connect.2.html)
and [send](https://man7.org/linux/man-pages/man2/send.2.html) documentation.

## Source and study map

```text
src/backend/     BackendServer and owning BackendPool
src/scheduling/  SchedulingPolicy and two selection algorithms
src/http/        Request/response structs, pure parser, header rewriting
src/network/     Linux Socket, listener, address resolution and bounded I/O
src/routing/     Longest segment-prefix matching
src/proxy/       Sequential request orchestration and ProxyConfig
src/main.cpp     Demo routes, CLI and stop signals
tests/           Mirrored unit tests plus real HTTP integration tests
examples/        Controlled Python backend
docs/            Plan, implementation notes and learning/viva guide
```

Read [implementation notes](docs/implementation-notes.md) for actual verification
evidence and [the learning guide](docs/reverse-proxy-learning-guide.md) for ownership,
algorithm traces and a request walkthrough. For rehearsal, one student can explain
backend ownership, policies and routing; the other can explain sockets, HTTP and
failure handling. Both should trace an entire POST exchange and explain one test.
