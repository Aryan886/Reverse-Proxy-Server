# Reverse Proxy Learning and Viva Guide

How the current code works and how the remaining features fit together

Prepared for the student team on 13 October 2026.

Read this guide alongside the source code. M0–M3 now run: validated backend objects, both scheduling policies, a sequential Linux listener, bounded HTTP forwarding, header rewriting and path routing. The epoll, worker, cache and health-check sections describe pending M4–M7 work. Use the README demonstration and implementation notes for commands and actual verification evidence.

## 1 Understand the project in one minute

A reverse proxy is a server that receives a request on behalf of other servers. The client sees one address. The proxy decides which backend should handle the request, forwards it, receives the answer and sends that answer to the client.

Think of a college office reception desk. A visitor describes what they need. The receptionist sends admission queries to admissions and fee queries to accounts. If two counters handle the same service, the receptionist chooses one. A closed counter should be skipped. A notice containing the same public answer for everyone can sometimes be reused.

| Project idea | Reception example | What the code must decide |
| --- | --- | --- |
| Routing | Choose a department | Which backend pool matches this path? |
| Load balancing | Choose a counter | Which eligible server in that pool? |
| Health checking | Check if a counter is open | Can this backend currently serve traffic? |
| Caching | Reuse a public notice | Can this exact response be reused safely? |
| Concurrency | Help multiple visitors in progress | Who owns each request and shared value? |

### L4 and L7

An L4 load balancer can forward TCP traffic without understanding an HTTP path. Our target is L7 because routing depends on application data such as /api or /static. It still uses TCP underneath. Merely using sockets does not make the project an HTTP-aware proxy; parsing and routing provide that behavior.

### The current request path

```text
Client -> Parser -> Router -> Pool policy -> Backend
Client <- Rewritten complete response <- Response parser
```

ReverseProxy handles one exchange at a time. A BackendReservation counts selected upstream work and releases it on success or exception. Local Socket objects own both TCP legs and close when their scopes finish. There is no cache lookup or event loop yet.

### The future request path with caching

```text
Client -> Parser -> Router -> Cache lookup
                               | hit -> Client
                               | miss
                               v
                    Backend -> Cache if eligible -> Client
```

Routing chooses a pool; scheduling chooses a server. A cache hit needs no backend connection. Explain this flow in your own words, then name the class responsible for each step.

<!-- pagebreak -->

## 2 Read the code that exists today

### BackendServer

Open src/backend/backend_server.h and backend_server.cpp. One object represents a backend endpoint and its state. host_ and port_ identify it. activeConnections_ starts at zero. isHealthy_ starts true. The constructor moves the supplied string into the object's member.

The fields are private. Callers ask for the host and port through getters, adjust the count through dedicated functions and change health through setHealthy. This is encapsulation: the object controls how its state changes. There is no arbitrary connection-count setter that could assign minus ten.

decrementConnections guards against a negative result. The constructor rejects an empty host or a port outside 1–65535. Dedicated tests cover defaults, validation, health toggles and connection counts. These M0 repairs are implemented.

### BackendPool

Open src/backend/backend_pool.h and backend_pool.cpp. backends_ is a vector of unique_ptr owners. rawPointers_ is a vector of borrowed addresses used by schedulers. addBackend gets an object's address before moving ownership into the pool. removeBackend removes the borrowed entry and destroys the owner. getBackends returns a const reference to the existing pointer list.

The two vectors describe the same set of servers and must stay synchronized. addBackend rejects null owners and reserves capacity in both vectors before changing membership. If either allocation fails, membership stays unchanged. Capacity grows geometrically so ordinary additions do not reallocate on every insert. Tests also show that surviving backend addresses remain stable after growth and removal.

### Scheduling classes

SchedulingPolicy declares selectBackend as a pure virtual function. RoundRobinPolicy stores nextIndex_. LeastConnectionPolicy should scan connection counts and does not need persistent algorithm state. Both return a borrowed BackendServer pointer, or nullptr when no eligible choice exists.

Both .cpp files now belong to the scheduling CMake library. Both policies skip null and unhealthy candidates. Round robin normalizes its cursor and searches at most one pass. Least connections examines all eligible servers and keeps the first on ties. Empty/all-ineligible lists return nullptr. Each running pool has its own policy instance.

### What the green tests prove

The starting repository passed five pool tests and a sanity test while leaving policy implementations unbuilt. During M0, the new 3/1/2 regression failed against the old least-connections loop, then passed after the repair. The expanded suites cover backend state, policies, parsing, rewriting, routing, socket ownership, configuration and real HTTP exchanges; the implementation notes record final counts. The obsolete sanity test has been removed.

Viva check: “Can all tests pass while the program contains a bug?” Yes. Tests only establish the behavior they exercise, and unbuilt code may receive no checks at all.

<!-- pagebreak -->

## 3 Explain ownership and language syntax

### Who is responsible for destruction

An owner is responsible for keeping an object alive and eventually destroying it. A borrowed pointer allows access but does not own the object. In this project, BackendPool owns each BackendServer. A scheduler borrows its address to return a selection. The scheduler must not delete it.

```cpp
auto backend = std::make_unique<BackendServer>("127.0.0.1", 9001);
BackendServer* server = backend.get();
pool.addBackend(std::move(backend));
```

After this illustrative sequence, the pool owns the server; backend is empty. server still points to the same heap object. It becomes invalid when that object is removed or the pool is destroyed. unique_ptr protects ownership, not every borrowed pointer's lifetime.

A vector can move its elements when it grows. A vector of BackendServer objects could therefore move the servers themselves and invalidate their addresses. A vector of unique_ptr moves the owning pointer objects, while their separately allocated BackendServer objects keep their addresses. Erasing an owner still destroys its server.

shared_ptr would permit multiple owners. We do not need that here. The simpler rule is that the pool outlives all active requests. The plan freezes backend membership during operation to make this rule straightforward.

### Syntax you should be able to read aloud

| Syntax | Meaning in this project |
| --- | --- |
| const string& getHost() const | Return a borrowed read-only string; this function does not modify the object through this |
| BackendServer* | A server address; it can be nullptr and is non-owning here |
| std::move(backend) | Allow ownership transfer from backend; it is not a deep copy |
| = delete | Forbid the declared operation, such as copying a pool |
| override | Ask the compiler to check that a function overrides a virtual base function |
| virtual ... = 0 | Declare an interface function that a concrete subclass must implement |
| virtual ~SchedulingPolicy() | Allow correct derived destruction through a base pointer |

A const vector of pointers prevents changing the vector through that reference. It does not make the pointed-to BackendServer objects immutable. Constness of a container and constness of its pointees are different.

### RAII in the current network code

RAII ties resource cleanup to object lifetime. Socket owns one descriptor and closes it in its destructor. It is movable and non-copyable so two objects cannot own the same descriptor. BackendReservation similarly releases an upstream count on every exit path. M4 will additionally need epoll unregistration and connection-state cleanup; those concepts are not implemented yet.

<!-- pagebreak -->

## 4 Explain the OOP design without memorized definitions

Abstraction means a caller depends on what an object can do without needing all its implementation details. A caller knows SchedulingPolicy can select a backend. It does not need to know whether the implementation uses a rotating index or a minimum-count scan.

Inheritance supplies the shared policy relationship: RoundRobinPolicy and LeastConnectionPolicy are kinds of SchedulingPolicy. Polymorphism is the runtime behavior when the caller invokes the overridden method through the common base type.

```cpp
std::unique_ptr<SchedulingPolicy> policy =
    std::make_unique<RoundRobinPolicy>();
BackendServer* selected = policy->selectBackend(pool.getBackends());
```

This illustrative call uses the RoundRobinPolicy implementation even though the pointer's declared type is SchedulingPolicy. Replacing construction with LeastConnectionPolicy changes selection without changing this call. The plan adds synchronization around the complete selection operation once workers exist.

Composition means building a component from other components. ReverseProxy owns its configuration, router, pools, policies and resolved backend addresses. Workers are planned for M5. A reverse proxy is not a kind of router, so inheritance is not useful there. HttpRequest and HttpResponse are simple structs: data representation does not need an elaborate class hierarchy.

### Walk through both algorithms

Round robin with healthy A, B and C selects A, B, C, A, B, C. It needs nextIndex_ to remember where to continue. If B becomes unhealthy, the intended sequence skips it. Bound the search to one pass so all-unhealthy does not loop forever.

Least connections with A=3, B=1 and C=2 selects B. Start with no best candidate, inspect each eligible server and keep the smallest count. On ties this project's simple rule keeps the first eligible candidate. This is O(n) time and O(1) extra space; sorting would do unnecessary work.

The original bug returned after looking only at A. The regression test with counts 3/1/2 exposed it. return exits the entire function, not only the loop body. The repaired code returns after all candidates have been considered. Explain the old failure and the new loop together during the viva.

### What connection count means here

The count represents in-flight upstream work reserved for a backend, including connection establishment. The selected server is incremented by BackendReservation; its destructor decrements once on completion or failure. The current sequential implementation returns counts to zero before another selection, so least connections usually chooses the first server on ties. Overlapping requests need M4 before its advantage can be demonstrated. A future cache hit will have no upstream reservation.

Practice: for counts 2/2/0, predict the choice. Then mark C unhealthy and predict again. Explain why “fewest connections” does not necessarily mean “fastest CPU” or “shortest response time.”

<!-- pagebreak -->

## 5 Understand sockets before epoll

A socket is an operating-system communication endpoint. In Linux, the program refers to it using a small integer file descriptor. The descriptor is a handle, not the actual network connection or the port number.

The listening socket waits for new connections. accept creates a separate connected socket for one client. The proxy opens another connected socket to a backend. A proxied exchange therefore has two separate TCP legs: client to proxy and proxy to backend.

```text
Listening socket on port 8080
          |
        accept
          |
   client socket <-> proxy buffers <-> backend socket
```

socket creates the endpoint; bind assigns a local address; listen permits incoming connections; accept obtains a client connection. The proxy uses connect when it acts as a client of the backend. A process can have many connected sockets using the same listening port.

### TCP carries a byte stream

Sending an HTTP request in one call does not guarantee it arrives in one recv call. A request could arrive as “GET /ap”, then “i/id HTTP/1.1\r\nHo”, then the remaining headers. The parser must retain incomplete data and wait. A 4 KiB read buffer is a chunk size, not a maximum valid message size.

Similarly, send may accept only part of an output buffer. Keep bytesSent and resume from that offset. For a 1000-byte message, writes of 400 and 600 bytes together complete it. Advancing by the requested size instead of the returned size loses data. See the send and recv manual references in the plan.

### How to interpret outcomes

- Positive byte count: consume exactly that many bytes.
- recv returns zero: the peer closed its sending side; check whether the expected message is complete.
- EINTR: an interruption occurred; retry while respecting the deadline.
- EAGAIN on a non-blocking socket: no progress is possible now; wait for readiness.
- Other errors: record the operation and endpoint, then clean up or return an appropriate proxy error.

Blocking I/O can stop the current thread until progress is possible. This checkpoint uses non-blocking descriptors plus bounded poll waits in a sequential request handler. It still serves only one exchange at a time: polling with a deadline does not by itself create concurrency. The planned epoll state machine will preserve unfinished exchanges while processing other clients.

The network target is Linux. WSL2 runs a Linux environment on the Windows machine. Docker containers use their own networking: localhost inside the proxy container refers to that container, not to a sibling backend container.

<!-- pagebreak -->

## 6 Read an HTTP exchange

Here is a request in the implemented subset. The displayed \r\n markers represent carriage return and line feed bytes; they are not literal text sent to the backend.

```text
POST /api/echo?mode=small HTTP/1.1\r\n
Host: demo.local\r\n
Content-Length: 5\r\n
Connection: close\r\n
\r\n
hello
```

The first line supplies the method, request target and version. Headers follow. The empty line ends the headers. Content-Length gives a body length in bytes: five for hello. The query is part of the original target, but the router uses /api/echo to select a pool.

A response begins with a status line such as HTTP/1.1 200 OK, then headers, a blank line and any permitted body. Message framing tells the receiver where the message ends. The project starts with Content-Length and selected close-delimited backend responses. It explicitly rejects unsupported transfer encodings rather than pretending every response has a Content-Length. RFC 9112 is the message-format reference.

### Parser outcomes

NeedMoreData means the bytes could become a valid complete message. Complete means the parser has enough bytes and reports how many were consumed. Error means the supplied message violates the supported rules or limits. Incomplete and invalid are different: a split header is not automatically a bad request.

Tests should split a valid request at many byte positions. The parser must return the same final request whether bytes arrive all at once or gradually. Additional tests should provide invalid lengths, duplicate framing headers, too-large input and premature EOF.

### Why restrict the protocol

Supporting every HTTP feature would dominate this OOP project. The plan uses one request per connection, complete bounded buffering and a controlled backend. Chunked bodies, keep-alive, WebSocket upgrades and CONNECT are extensions. State this limitation during the presentation; a successful curl example is not proof of general HTTP compatibility.

### Error codes in your own words

| Code | Meaning in this project |
| --- | --- |
| 400 | The client's request is malformed |
| 404 | The proxy has no matching route |
| 413 or 431 | The body or headers exceed the configured limit |
| 502 | The selected upstream failed or returned an unsupported/bad reply |
| 503 | No eligible backend is available |
| 504 | The upstream did not complete within the deadline |

The proxy should send its own small valid response on an error. A log records the detailed cause; the HTTP status gives the client a stable outcome.

<!-- pagebreak -->

## 7 Follow routing and header changes

Suppose the configuration contains /api → pool A and /static → pool B. A request for /api/users?sort=name goes to pool A. The selected backend still receives /api/users?sort=name because this plan preserves the prefix and query.

If /api/admin is also configured, it is more specific than /api. Longest-prefix matching chooses it for /api/admin/users. Match whole path segments: /api must not accidentally match /apix. Start with a loop over a small route list; a trie adds complexity without a benefit for two or three routes.

An unmatched route returns 404. A matching route with no healthy backend returns 503. These outcomes describe different problems, so routing and backend availability should not be collapsed into one generic failure.

### Why headers change at a proxy

The client-to-proxy connection and proxy-to-backend connection are different. Some headers describe only one connection and cannot simply be carried across. HeaderRewriter removes those fields and serializes headers appropriate for the new leg. The Connection header can name additional connection-specific fields, so removing only a fixed list is insufficient. RFC 9110 explains this distinction.

X-Forwarded-For and X-Real-IP help the backend see the original peer address. serializeRequest overwrites client-supplied values using the socket peer address and drops the untrusted Forwarded field. Otherwise a client could invent its own supposed identity. These headers are diagnostic context, not authentication.

Store headers as name/value pairs so repeated fields can be preserved where appropriate. For example, two Set-Cookie fields must not be silently overwritten by one map entry. Header names are case-insensitive; URL paths are not generally interchangeable by case.

### A complete uncached request trace

1. Read the client bytes until the parser returns Complete.
2. Find the route using the path without the query.
3. Select and reserve one healthy backend from that route's pool.
4. Build the outgoing request with safe forwarding headers.
5. Connect, send from the current offset and read the backend reply.
6. Validate the entire supported response before sending client headers.
7. Release the upstream reservation; send the response and close the sockets.

Each failure path must also release resources. If a request body was partially forwarded before failure, the backend might already have acted on it. Retrying a POST automatically can repeat an operation. The first implementation returns an error and does not replay it.

Practice: distinguish “no route,” “no healthy server,” “connection refused” and “response took too long.” Name the component and expected status for each.

<!-- pagebreak -->

## 8 Understand the event loop and state machine

This section describes pending M4 work. The current code has no epoll or Connection state machine.

epoll asks Linux which registered descriptors are ready for an operation. Readable does not mean an entire HTTP request has arrived. Writable does not mean an entire response fits. Readiness is an opportunity to attempt progress. The epoll manual is reference R6 in the plan.

A Connection remembers where one exchange stopped. It contains its current state, two owned sockets, input/output buffers, send offsets, selected backend and deadline. Without this saved state, a function that returns on EAGAIN would forget what remains to do.

```text
ReadingRequest -> ConnectingBackend -> SendingRequest
       ^                                    |
       |                               ReadingResponse
       |                                    |
  new client                          SendingResponse
                                            |
                                          Closed
```

The arrow back to a new client is conceptual: a finished connection is removed and a newly accepted client receives a new Connection object. We do not reuse the client connection for a second request in this subset.

When connect reports EINPROGRESS, wait for readiness and then inspect SO_ERROR. A writable event alone is not proof that connection establishment succeeded. Register EPOLLOUT only while connection establishment or sending requires it; otherwise the socket may be continuously reported writable and waste CPU.

### Level triggered and edge triggered

Level-triggered readiness can be reported again while a condition remains true. This is the initial choice because it is easier to reason about. Edge-triggered behavior reports readiness transitions and requires particularly careful draining and rearming. It is a later extension, not an essential demonstration of OOP.

### Cleanup is part of the algorithm

On completion, timeout or disconnect, remove event registrations, close owned sockets, release any backend reservation and erase connection state. Perform this once. If a late event refers to a removed connection ID, ignore it. Descriptor numbers are reusable, which is why an old event should not be trusted merely because its integer resembles a current descriptor.

The worker checks deadlines with steady_clock, whose elapsed-time behavior is appropriate for timeouts. It should not wait forever in epoll_wait because shutdown and expired requests need attention. Buffer and connection caps keep a slow or oversized request from consuming unlimited memory.

A single event loop already supports many I/O operations in progress. Multiple threads are a separate step. Concurrency means multiple tasks can make progress over overlapping time; parallelism means work actually executes at the same instant on different execution resources.

<!-- pagebreak -->

## 9 Explain threads and shared state

The plan starts with one event loop, then two worker threads. Each worker owns its connection objects, buffers, listener and epoll instance. SO_REUSEPORT lets Linux distribute incoming connections among listeners on the same address and port. There is no separate acceptor dispatch queue in this chosen design.

Pools and their state are shared. Consider A=0 and B=0. Worker 1 reads A as the least busy, then Worker 2 reads the same values before Worker 1 increments A. Both may select A. Protecting only the increments does not make the combined selection-and-reservation operation indivisible.

### The pool lock protects a complete decision

```text
lock the pool
select an eligible backend
increment its reserved connection count
unlock the pool
perform network I/O
lock briefly again to release the reservation
```

The health checker uses the same pool lock when publishing health changes. Every concurrent reader or writer of those protected fields must follow the rule. A mutex around one method is not enough if another path accesses the same fields without synchronization.

Never hold that lock while waiting for a network response. A slow backend would prevent unrelated workers from selecting any backend in the pool. The shared cache has its own short-lived mutex and returns a copy rather than a reference that might be erased after unlocking.

| Data | Ownership or synchronization rule |
| --- | --- |
| Socket buffers and offsets | One worker owns them |
| Routes and endpoints | Initialized before workers; immutable while running |
| Counts, health and scheduling cursor | One mutex per pool |
| Cache map and LRU list | One cache mutex |
| Worker stop request | Atomic flag set by normal coordinating code |

std::atomic makes supported operations on one atomic object safe from data races. It does not make a vector or a multi-step scheduling decision safe. It is also not guaranteed to be lock-free on every platform. This project uses a mutex where several values must agree.

### Shutdown and object lifetime

Stop accepting new clients, allow bounded draining, close remaining connections and join worker/checker threads before destroying pools or cache. join waits for a thread to finish. A detached thread could continue using an already-destroyed pool, so detaching is not a shortcut for this design.

Practice: list every shared mutable field before writing a thread. If the team cannot name its lock or owner, the concurrency design is incomplete.

<!-- pagebreak -->

## 10 Explain the cache and health checker

### LRU answers which entry to evict

LRU means least recently used. With capacity two: request A, request B, request A again, then insert C. A was recently used, so B is evicted. A list records recency and a hash map finds entries efficiently. A hit updates list order; merely storing insertion order would implement a different eviction policy.

TTL answers when an entry becomes too old. If an entry's usable lifetime is five seconds, reading it does not restart that lifetime. LRU recency and TTL expiry are independent. At the expiry boundary, the entry is a miss.

The planned cache is intentionally restricted to a controlled public static route. It admits only simple eligible responses, distinguishes Host/path/query keys and bypasses private or variant responses. Storing any GET 200 would be unsafe: GET can still return personalized data. The implementation plan specifies bypass rules and freshness handling; RFC 9111 explains the broader HTTP caching model.

For a cache test, supply time values directly: insert at t=0, read at t=4 and read at t=5. The first is a hit and the second is expired for a five-second lifetime. This is faster and more repeatable than sleeping for five real seconds.

### Health checks answer whether to select a backend

A TCP probe checks whether a connection can be established. A successful HTTP /health probe adds an application response check. Neither guarantees the very next user request will succeed. Health state is a recent observation, not a promise.

The proposed checker uses two consecutive failures to mark a backend unhealthy and two successes to restore it. This hysteresis prevents one transient result from constantly flipping state. With probes every two seconds there is a delay before a failure is excluded. Recovery also takes time.

RTT is elapsed probe time. A slow probe can be useful evidence, but a strict threshold can mislabel a busy local machine as unhealthy. Record RTT first and make any exclusion threshold optional.

### Failure walkthrough

Backend B stops. A request selected before detection may return 502 or 504. The checker then observes enough failed probes and marks B unhealthy under the pool lock. New selections skip B. If all servers are unhealthy, requests receive 503. After B restarts and passes enough probes, new requests can use it again.

Cached public content may still be returned while a backend is unavailable if it remains eligible and fresh. This design does not serve expired entries. Cache behavior and backend availability should be demonstrated separately so one does not hide the other.

<!-- pagebreak -->

## 11 Rehearse and verify understanding

### Questions with short answer checks

1. Why is SchedulingPolicy abstract? It provides one selection contract while concrete policies choose different algorithms.
2. Where does runtime polymorphism occur? At a virtual call through a SchedulingPolicy pointer or reference.
3. Why does the pool use unique_ptr? It has sole ownership and needs stable backend addresses while its vector grows.
4. When is a returned backend pointer invalid? After that backend is removed or its owning pool is destroyed.
5. Why return nullptr instead of throwing for no healthy backend? It is an expected operational outcome that the proxy maps to 503.
6. Can one recv read half a header? Yes; TCP is a stream, so the parser must preserve incomplete input.
7. Does epoll parse HTTP? No; it reports readiness. HttpParser interprets bytes.
8. Why can least connections choose the same server repeatedly? Sequential requests can finish before the next selection, producing repeated ties.
9. Why is an atomic counter insufficient? Choosing the minimum and reserving it involves several related operations.
10. Why is a cache hit not always valid? The entry may be expired or the request may not qualify for reuse.
11. Why can health checking miss a just-failed backend? Checks are periodic observations with a detection delay.
12. What does a successful build establish? The built targets compiled and linked; correctness still requires behavioral tests.

### A 75 minute study session

Spend 15 minutes on the current source and ownership, 15 on algorithm traces and tests, 15 on socket/HTTP flow, 15 on the event-loop and shared-state diagrams, and 15 explaining the project aloud. Read the cache and health sections after you can trace one uncached request comfortably.

Each teammate should explain one class, one failed test and one full request path. Useful practice inputs are an empty backend list, counts 3/1/2, a split HTTP header, an unknown path, a dead backend and an expired cache entry. Predict the result before running the relevant test.

### A short honest presentation opening

“Our C++ reverse proxy now forwards bounded GET and POST requests through a Linux listener. It routes /api and /static to separate pools, alternates two API replicas, rewrites forwarding headers and returns clear errors for malformed requests, failed backends and timeouts. Unit tests and live HTTP tests verify that request path. It currently handles one exchange at a time; epoll, workers, caching and health probes are the next milestones.”

Use the README to run the live examples before presenting. Explain the repaired early-return issue as a regression that exposed a test/build gap. Do not say the proxy handles concurrent requests, caches responses or performs automatic failover: those milestones remain pending.

For detailed milestone acceptance tests and primary technical references, use the companion Sol Implementation Plan. Existing source files remain the reference for current behavior; this guide explains the proposed later behavior as well.
