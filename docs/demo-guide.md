# Five-Minute OOP Demonstration

This runbook matches the simplified GET-only project. Run commands in Linux or
Ubuntu in WSL. The full build instructions are in the README.

## 1. Prepare before class

Build the demo and tests:

~~~bash
cmake -S . -B out/oop-tests
cmake --build out/oop-tests -j 4
ctest --test-dir out/oop-tests --output-on-failure
~~~

If WSL has no system CMake, first run:

~~~bash
export PATH="$PWD/out/tooling/cmake-3.31.8-linux-x86_64/bin:$PATH"
~~~

The integration test needs ports 8080 and 9001-9003 free. Run it before starting
the demonstration processes.

Open four terminals, with one command in each:

~~~bash
python3 examples/backend_server.py --port 9001 --id api-1
python3 examples/backend_server.py --port 9002 --id api-2
python3 examples/backend_server.py --port 9003 --id static-1
out/oop-tests/reverse_proxy
~~~

Use a fifth terminal for requests. You can also open
http://127.0.0.1:8080/api/id in a browser.

## 2. Explain the problem — 30 seconds

A reverse proxy gives clients one entry point. It chooses a group of servers
based on the requested path, then chooses a server from that group.

The project uses this problem to show how classes collaborate. It handles a
small local GET demo, not a complete web proxy.

## 3. Show the behavior — 90 seconds

~~~bash
for request in 1 2 3 4; do curl -s http://127.0.0.1:8080/api/id; done
curl -s http://127.0.0.1:8080/static/item
curl -i http://127.0.0.1:8080/unknown
~~~

Expected: alternating api-1/api-2, static text, then 404.
Earlier browser requests may change which API server appears first.

For one failure example, stop api-2 with Ctrl+C in its own terminal. Request
/api/id twice. One request succeeds and the other returns 502. The proxy stays
running. Restart api-2 afterward.

~~~bash
curl -i http://127.0.0.1:8080/api/id
curl -i http://127.0.0.1:8080/api/id
~~~

Explain that the stopped server remains in rotation: automatic detection and
retry are outside this version.

## 4. Show the classes — two minutes

Open these files in order:

| File | Explain |
| --- | --- |
| src/main.cpp | Creation of objects and how they are connected |
| src/backend/backend_server.h | Private state and public methods |
| src/backend/backend_pool.h | Composition using a vector of server objects |
| src/scheduling/scheduling_policy.h | Abstract class and pure virtual function |
| src/scheduling/round_robin_policy.cpp | Select one server and advance the index |
| src/scheduling/least_connection_policy.cpp | A second implementation of the same interface |
| src/proxy/reverse_proxy.cpp | The policy call through a base-class pointer |

Explain the object lifetimes: main owns pools and policies; routes refer to them.
ReverseProxy contains the router. Socket objects close connections when their
scopes end during normal execution.

In handleClient(), point out that the connection count increases before forwarding
and decreases after the try/catch. Both the success and failure paths reach that
same line. There is no separate health state to track.

## 5. Show one test — one minute

~~~bash
ctest --test-dir out/oop-tests -R SchedulingPolicyTest -V
ctest --test-dir out/oop-tests -R LeastConnectionPolicyTest.ChoosesTheSmallestCount -V
~~~

Open the corresponding C++ test. It assigns connection counts 3, 1 and 2, then
checks that the second server is selected. The polymorphism test calls both
derived classes through a SchedulingPolicy pointer.

Explain why this is a prepared algorithm example: the sequential live proxy
normally sees zero active connections before every new request.

## Division between two students

One student can explain BackendServer, BackendPool and both scheduling algorithms.
The other can explain main, routing, ReverseProxy and the request walkthrough.
Both should understand the polymorphic call and be able to change a test.

## Limits to mention if asked

GET only; numeric IPv4; local listener; one request at a time. No custom request
header forwarding, POST bodies, automatic health checks, concurrency or caching.
The Python backends are controlled examples that close each response.

For unsupported methods:

~~~bash
curl -i -X POST http://127.0.0.1:8080/api/id
~~~

Expected: 405 Method Not Allowed.

## Common problems

| Symptom | Fix |
| --- | --- |
| No executable on Windows | Build and run the live program inside Linux/WSL. Windows builds the portable tests. |
| Port already in use | Stop your earlier demo processes. Ports are configured in main.cpp. |
| 502 on alternating requests | Check that both API backends are running. |
| Browser at / shows 404 | Use /api/id or /static/item. There is no root route. |
| Always api-1 | Check whether you changed the API policy to least connections. |
| Integration test reports occupied ports | Stop the demo, then rerun CTest. |
| Old CLI flags no longer work | Configure the objects in main.cpp and rebuild. |

Press Ctrl+C in each of the four server terminals when finished. Do not change
the listener address for this demonstration; it is intentionally local.
