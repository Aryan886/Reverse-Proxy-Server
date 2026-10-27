# Small Reverse Proxy — C++ OOP Project

A classroom project for learning how C++ objects work together. It forwards real
GET requests to local Python backends, but keeps the networking scope small.

The main topics are encapsulation, composition, inheritance and runtime
polymorphism. Start with [main.cpp](src/main.cpp), where the pools, policies and
proxy are created as ordinary objects.

## What it does

- Routes /api requests to two servers and /static requests to one server.
- Uses round robin to alternate between the API servers.
- Includes a second scheduling class, LeastConnectionPolicy, to demonstrate
  two implementations of the same abstract interface.
- Returns simple errors for a missing route, an unavailable backend or a
  method other than GET.

The application handles one request at a time.

~~~text
Browser -> ReverseProxy -> PathRouter -> BackendPool
                                      -> SchedulingPolicy selects a server
        <- backend response <- Socket sends a GET to that server
~~~

## Build and run

The live proxy needs Linux or Ubuntu in WSL, a C++17 compiler and CMake 3.16+.
Python 3 runs the example backends. Build without tests if you only want the demo;
this does not download GoogleTest.

~~~bash
cmake -S . -B out/oop-demo -DBUILD_TESTING=OFF
cmake --build out/oop-demo -j 4
~~~

On this machine, if WSL has no system CMake, use the existing local copy:

~~~bash
export PATH="$PWD/out/tooling/cmake-3.31.8-linux-x86_64/bin:$PATH"
~~~

Start each command in a separate Linux/WSL terminal:

~~~bash
python3 examples/backend_server.py --port 9001 --id api-1
python3 examples/backend_server.py --port 9002 --id api-2
python3 examples/backend_server.py --port 9003 --id static-1
out/oop-demo/reverse_proxy
~~~

In another terminal:

~~~bash
for request in 1 2 3 4; do curl -s http://127.0.0.1:8080/api/id; done
curl -i http://127.0.0.1:8080/static/item
curl -i http://127.0.0.1:8080/unknown
curl -i -X POST http://127.0.0.1:8080/api/id
~~~

The first command alternates api-1 and api-2. The others show static content,
404 and 405. You can also open /api/id or /static/item in a browser.
Press Ctrl+C in each server terminal when finished.

## Where the OOP concepts appear

| Concept | Example |
| --- | --- |
| Encapsulation | BackendServer keeps its address, port and connection count private. |
| Composition | BackendPool contains a vector of BackendServer objects; ReverseProxy contains a PathRouter. |
| Abstraction | SchedulingPolicy declares what selecting a server means. |
| Inheritance | RoundRobinPolicy and LeastConnectionPolicy derive from SchedulingPolicy. |
| Runtime polymorphism | The proxy calls selectBackend through a SchedulingPolicy pointer. |
| Object lifetime | main owns the pools and policies; local Socket objects close connections in their destructors. |

Networking is supporting code in src/network. The algorithms and class
relationships can be studied and tested without opening a socket.

## Configuration

Edit the small setup in [main.cpp](src/main.cpp), then rebuild. It specifies the
proxy port, server ports, routes and policy objects. There is no proxy command-line
configuration parser.

To try the other policy, include least_connection_policy.h and replace
RoundRobinPolicy apiPolicy with LeastConnectionPolicy apiPolicy. The rest of the
proxy code stays the same. Because requests run one at a time, connection counts
normally return to zero before the next selection, so least connections usually
chooses the first server. Its unit test uses counts 3, 1 and 2 to show its logic.

Routes use the first matching prefix in registration order. Put more specific
routes before broad ones. /api matches /api/id but does not match /apix.
Duplicate prefixes also use the first registered route. Create pools and policies
before the proxy, use one policy object per pool and finish adding servers before
run(). Routes borrow these objects; they do not own them. Socket setup checks the
proxy port when run() starts listening.

## Tests

Tests reuse GoogleTest v1.15.2; the first test configure needs internet unless the
source is already cached.

~~~bash
cmake -S . -B out/oop-tests -DBUILD_TESTING=ON
cmake --build out/oop-tests -j 4
ctest --test-dir out/oop-tests --output-on-failure
~~~

Stop the running demo before testing: the integration test uses ports 8080 and
9001-9003, starts its own processes and cleans them up afterward. It checks
alternation, routing, GET-only behavior, a request arriving in pieces, an
incomplete request timing out and a stopped backend.

On Windows, the class, routing and HTTP helper tests work with MinGW:

~~~powershell
cmake -S . -B out/oop-windows-tests -G "MinGW Makefiles"
cmake --build out/oop-windows-tests -j 4
ctest --test-dir out/oop-windows-tests --output-on-failure
~~~

Use a fresh build folder; older caches may refer to another checkout or toolchain.

## Deliberate limits

This version is a local classroom demo, with the listener bound to 127.0.0.1.

- GET only; no POST bodies, client-header forwarding, authentication or cookies.
  A fresh HTTP/1.0 GET carries the original path/query and the backend Host.
- Numeric IPv4 backend addresses only; no DNS lookup or IPv6 support.
- One request per client connection; no concurrency, keep-alive, caching or retries.
- Backends must send ordinary HTTP responses and close their connections, as the
  included Python example does. Replies are buffered and relayed unchanged.
  Only an empty reply is rejected; response parsing and header rewriting are
  outside this version.
- Every configured server stays in rotation. There is no health flag or automatic
  health checking; a stopped server causes a 502 when selected.
- Requests are limited to 8 KiB while reading headers and responses to 1 MiB.
  Socket operations have a five-second timeout, not a whole-request deadline.
- Ctrl+C ends the process. There is no custom graceful-shutdown system.
- Errors are kept simple: 400 for request read/parse failures, 404 for no route,
  405 for unsupported methods, 502 for backend failures and 503 for an empty pool.

See the [learning guide](docs/reverse-proxy-learning-guide.md) for class walkthroughs
and the [demo guide](docs/demo-guide.md) for a short presentation.
The older implementation plan, project context and Word documents describe the
previous larger scope; the current Markdown guides describe this version.
