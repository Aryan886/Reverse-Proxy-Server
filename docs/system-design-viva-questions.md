# Reverse Proxy: System Design and Architecture Viva

Use these as short practice answers, then explain each example in your own words.

**Implementation checkpoint:** M0–M3 are complete. The current proxy handles requests sequentially. M4, the epoll event loop, is the next active milestone; workers, caching, and active health checks are planned.

## 1. Explain your project in one minute.

Our project is a C++17 reverse proxy. Clients connect to one address, and the proxy reads their HTTP requests, chooses the appropriate backend pool, selects a server, and forwards the request. It returns the backend's response to the client.

We currently support path routing, two load-balancing policies, bounded HTTP parsing, header rewriting, and error handling. Next, we are adding an epoll event loop for overlapping requests.

## 2. Why is this an L7 proxy if it uses TCP sockets?

TCP carries the bytes, but our routing decision depends on HTTP information such as `/api` or `/static`. Understanding application-level data makes it L7.

An L4 proxy could forward TCP traffic without understanding the HTTP path.

## 3. Walk me through an actual request.

For `GET /api/id`:

1. The listener accepts the client connection.
2. The parser reads and validates the HTTP request.
3. `PathRouter` finds the `/api` route.
4. Its scheduling policy selects an eligible backend from that pool.
5. A `BackendReservation` increments the backend's active count.
6. The proxy connects, rewrites headers, and sends the request.
7. It reads and validates the backend response.
8. The reservation releases the count.
9. The proxy rewrites and sends the response, then closes the connection.

**Cache lookup is part of the target design, not this current execution path.** Header rewriting happens when serializing outgoing messages.

See [reverse_proxy.cpp](../src/proxy/reverse_proxy.cpp) for the current orchestration.

## 4. What is the difference between routing and load balancing?

**Routing chooses the pool; load balancing chooses a server inside it.**

For example, `/api/users` goes to the API pool. Round robin then chooses either `api-1` or `api-2`. A `/static` request goes to a different pool.

Keeping these responsibilities separate lets us change the balancing algorithm without changing route matching.

## 5. How does your router choose between similar paths?

It chooses the longest matching prefix at a path-segment boundary.

If both `/api` and `/api/admin` exist, `/api/admin/users` selects `/api/admin`. However, `/apix` does not match `/api`.

The query string is ignored for route selection but preserved when forwarding.

## 6. How do your two scheduling algorithms work?

| Policy | Selection rule | Example |
| --- | --- | --- |
| Round robin | Rotate through eligible backends | A → B → C → A |
| Least connections | Choose the smallest active count | A=3, B=1, C=2 → B |

Both skip null and unhealthy entries. Both return `nullptr` when no eligible backend exists.

Least connections scans the pool in **O(n)** time. Round robin normally finds a choice quickly, but can also scan **O(n)** candidates when skipping unavailable entries.

## 7. Is least connections always better?

No. It helps when requests overlap and occupy backends for different durations. Connection count is only an estimate of load; it does not measure CPU usage or response speed.

In our **current sequential implementation**, counts normally return to zero before the next selection. Ties choose the first eligible backend, so least connections does not yet demonstrate its main advantage.

## 8. Does SchedulingPolicy actually forward the request?

No. It only returns the selected `BackendServer*`.

`ReverseProxy` performs the connection, request forwarding, response handling, and cleanup. Keeping networking outside the policy makes each algorithm easy to test without running servers.

## 9. Where are the OOP concepts in your project?

| Concept | Concrete example |
| --- | --- |
| Encapsulation | `BackendServer` keeps its fields private |
| Abstraction | `SchedulingPolicy` exposes backend selection |
| Inheritance | Both concrete policies derive from `SchedulingPolicy` |
| Polymorphism | Calling `selectBackend()` through the base pointer |
| Composition | `ReverseProxy` owns pools, policies, and its router |

Explain the example before giving the textbook definition.

## 10. Why does SchedulingPolicy have a virtual destructor?

We own concrete policies through `unique_ptr<SchedulingPolicy>`. A virtual destructor ensures that destroying an object through the base pointer correctly runs the derived destructor.

The pure virtual selection function defines what every policy must implement.

## 11. Who owns BackendServer, and why use unique_ptr?

`BackendPool` owns the backend objects. Policies borrow their addresses and must not delete them.

`unique_ptr` expresses that single owner and destroys the object automatically. `shared_ptr` would introduce shared ownership that this design does not need.

**BackendServer is an endpoint/state record; it is not the actual backend server process.**

## 12. Can the borrowed pointers become invalid?

Yes, if their backend is removed or the pool is destroyed.

Growing the vector of `unique_ptr` does not move the separately allocated backend objects. Removing an owner does destroy its object.

Our runtime keeps membership fixed after startup, which makes these lifetimes easier to guarantee. Dynamic removal would require a deliberate lifetime-management design.

## 13. How do you prevent resource leaks when something fails?

We use **RAII: resource cleanup follows object lifetime**.

`Socket` closes its descriptor in its destructor. `BackendReservation` decrements the active count when its scope ends, including during exception unwinding.

This avoids duplicating cleanup across every success, timeout, and error branch.

## 14. You already use non-blocking sockets. Why do you still need epoll?

Non-blocking sockets alone do not make the whole program concurrent.

Currently, the proxy handles one exchange using bounded `poll` waits before returning to accept another client. M4 will preserve unfinished exchanges and let other ready connections progress.

`epoll` reports readiness; **our event loop and connection state determine what work happens next**.

Reference: [Linux epoll documentation](https://man7.org/linux/man-pages/man7/epoll.7.html).

## 15. What state must you preserve for each connection?

The client and backend sockets, input/output buffers, bytes already sent, selected backend, deadline, and current processing stage.

The planned stages are:

```text
Read request → Connect backend → Send request
             → Read response → Send response → Close
```

Without saved state, we could lose partial input, resend bytes, or release the backend reservation incorrectly.

We are starting with **level-triggered epoll** because it is easier to reason about. Edge-triggered operation adds stricter requirements around draining available I/O before waiting again.

Reference: [Linux epoll documentation](https://man7.org/linux/man-pages/man7/epoll.7.html).

## 16. Can one recv() read the entire HTTP request?

Sometimes, but we cannot rely on it. TCP is a byte stream: headers and bodies can arrive across multiple reads.

Our parser distinguishes `NeedMoreData`, `Complete`, and `Error`. Similarly, a write can send only part of a buffer, so we advance by the number of bytes actually written.

## 17. Why rewrite HTTP headers?

The client-to-proxy and proxy-to-backend connections are separate connections.

We remove hop-by-hop fields and rebuild outgoing framing. We also replace client-supplied forwarding identity with the TCP peer address, preventing a client from choosing the IP address our proxy reports.

Our current subset uses `Connection: close`.

## 18. What could go wrong when you add worker threads?

Two workers could both observe the same least-loaded backend before either increments its count.

Protecting only the increment is insufficient. **Selection and reservation must happen together under the same synchronization rule.**

The implementation plan proposes one mutex per pool for selection, counts, and health state. Network I/O must happen outside that lock.

## 19. Why do you need both LRU and TTL in the cache?

They solve different problems:

- **LRU** decides what to evict when capacity is full.
- **TTL** decides whether an entry is still fresh.

A frequently accessed entry can still expire. A fresh entry can still be evicted because the cache is full.

The planned structure combines an `unordered_map` for lookup with a list for usage order.

## 20. Can you cache every GET response with status 200?

No. A successful GET response may contain personalized or non-cacheable content.

The planned cache is deliberately narrow: controlled public content, explicit freshness, and bypass rules for authentication, cookies, and unsupported variants. Its key includes route identity, Host, and the complete path plus query.

**GET + 200 is a starting condition, not sufficient permission to cache.**

Reference: [HTTP caching standard, RFC 9111](https://www.rfc-editor.org/rfc/rfc9111.html).

## 21. What happens if a backend crashes?

**Today:** if that backend is selected, the request can fail with a bounded `502`, or `504` for a timeout. The policies understand health flags, but active probes do not update them yet.

**After HealthChecker:** repeated probe failures will mark the backend unhealthy, excluding it from new selections. Recovery probes can restore it.

We should not automatically replay a partially sent POST because the backend might already have performed the operation.

## 22. What is the difference between 502, 503, and 504?

| Status | Meaning in this project |
| --- | --- |
| **502** | A selected backend failed to connect or returned an invalid/unsupported response |
| **503** | The matched pool had no eligible backend |
| **504** | An upstream stage exceeded its deadline |
| **404** | No route matched |
| **408** | The client request-read deadline expired |

A crashed backend does **not automatically mean 503**; its health flag may still be true when selected.

## 23. What are the current bottlenecks and limitations?

The main bottleneck is sequential request handling: one slow exchange delays other clients.

We also fully buffer bounded messages, support a restricted HTTP subset, and close connections after each exchange. The single proxy process is a single point of failure.

Epoll addresses overlapping I/O; workers address parallel execution. Multiple proxy instances and a failover entry point would be a separate availability extension.

## 24. How would you prove the design works?

Use evidence at different levels:

- **Unit tests:** selection order, unhealthy entries, fragmented HTTP, route boundaries.
- **Integration tests:** real client → proxy → backend exchanges.
- **Failure tests:** stopped backends, deadlines, malformed messages, disconnects.
- **M4 acceptance test:** a slow client must not prevent a second client from completing.
- **Resource checks:** connection counts return to zero and descriptors do not keep growing.

The [implementation notes](implementation-notes.md) record the existing verification. Passing those checks does not establish performance or correctness for features that are still planned.

## 25. Why build this instead of using NGINX?

For deployment, an established proxy would usually be the practical choice. We are building this to understand sockets, HTTP framing, resource ownership, scheduling, and concurrency in C++.

Our objective is a small system we can test and explain, rather than claiming feature parity with a production proxy.

## Practice method

For each answer: **state the responsibility → trace one example → explain the trade-off or current limitation.**
