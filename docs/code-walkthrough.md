# Code Walkthrough

This walkthrough follows the code that currently runs on `main`, beginning at `main()` and ending at the static-file and CGI response paths. It explains the current implementation, including its deliberately incomplete parts. For the broader target design and remaining work, see [Architecture](architecture.md).

## 1. Process Entry: `main()`

The program begins in [`srcs/main.cpp`](../srcs/main.cpp). It requires exactly one argument: the configuration-file path.

```text
./webserv config/default.config
```

`main()` creates five long-lived services:

```text
Config -> configuration and route lookup
Http -> request parsing and response serialization
Cgi -> CGI process and pipe handling
StaticFile -> static response content and MIME lookup
Logger -> debug and error output
```

It then passes references to all of them into `Worker` and calls `Worker::run()`. `Worker` is the orchestrator: services do not call one another and they do not own client connections.

Current-state note: `main()` accepts a config-file argument, but the current `Config` constructor does not yet parse that file. It constructs mock configuration instead.

## 2. The State That Travels Through the Server

Before reading the loop, it helps to know the data structs in [`headers/types.hpp`](../headers/types.hpp).

| Struct | Meaning | Lifetime / owner |
| --- | --- | --- |
| `Request` | Incoming method, path, query, headers, and body. | One `Transaction`; filled by `Http`. |
| `Response` | Outgoing status, headers, and body. | One `Transaction`; serialized by `Http`. |
| `Route` | Resolved root, CGI settings, and allowed methods. | One `Transaction`; returned by `Config`. |
| `CgiJob` | Child pid, stdin/stdout fds, output, write progress, and failure state. | One `Transaction` while a CGI request runs. |
| `Transaction` | One request-to-response cycle. | Nested inside `Connection`. |
| `Connection` | Client fd, input/output buffers, partial-write cursor, and current transaction. | Owned by `Worker` for one client socket. |

The important split is **connection versus transaction**. A connection can outlive one request, so its input buffer, output buffer, and fd live in `Connection`. A parsed request, route, response, and CGI state belong to its `Transaction`.

`Worker` owns connections by value in `std::map<int, Connection> connections`. A second map, `fdToConnection`, maps a client fd and any CGI pipe fds back to the owning connection. This lets one client connection be found when `poll()` reports activity on its socket or a related CGI pipe.

## 3. Why the Server Uses `poll()`

Networking calls can block: `accept()`, `read()`, and `write()` may wait indefinitely for another client or more kernel-buffer space. A single-threaded server instead asks the operating system which file descriptors are ready, then acts only on those descriptors.

[`Poller`](../headers/Poller.hpp) owns a `std::vector<pollfd>`. Each `pollfd` has:

```text
fd       the socket or pipe descriptor
events   activity the server wants to observe, such as POLLIN or POLLOUT
revents  activity reported by poll()
```

`POLLIN` means data may be read without waiting. `POLLOUT` means queued output may be written without waiting. The current [`Poller::poll()`](../srcs/Poller.cpp) calls the system `poll()` once for its whole vector and returns the readiness count; `Poller::events()` exposes the resulting fd entries to `Worker`.

The intended discipline is:

```text
register fd -> wait in poll() -> inspect revents -> perform one appropriate action
```

`Worker` copies the resulting fd entries before dispatching callbacks because a callback may remove an fd. It handles managed client/CGI error, hangup, and invalid-fd events by cleaning up the owning connection; it attempts to recreate a failed listener. Timeout handling and broader stress coverage remain unfinished.

## 4. Starting the Worker Loop

[`Worker::run()`](../srcs/Worker.cpp) creates a listening TCP socket through `setupListener()`.

1. `socket()` creates an IPv4 TCP socket.
2. `setsockopt(... SO_REUSEADDR ...)` allows rebinding after a restart.
3. `bind()` attaches it to `INADDR_ANY:8080`.
4. `listen()` changes it into a passive listening socket.
5. `fcntl(... O_NONBLOCK)` makes that socket non-blocking.
6. `poller.add(listen_fd, POLLIN)` asks `poll()` to report pending client connections.

The worker then loops forever. Each iteration gets the poll vector and checks `revents` for every entry:

```text
listening socket + POLLIN -> acceptNew()
CGI stdout fd             -> onCgiReadable()
CGI stdin fd              -> onCgiWritable()
client socket + POLLIN    -> onReadable()
client socket + POLLOUT   -> onWritable()
```

Before dispatching a non-listener event, `Worker` looks it up in `fdToConnection` with `find()` rather than `operator[]`. This avoids accidentally creating a null map entry for a stale fd. `closeConnection()` removes the client socket and any registered CGI pipe fds from `Poller` and `fdToConnection` before closing them.

Current-state note: the listener is hard-coded to port `8080` and `INADDR_ANY`; configured host/port pairs and multiple listeners are not implemented yet.

## 5. Accepting a Client

[`Worker::acceptNew()`](../srcs/Worker.cpp) runs after the listening socket has reported `POLLIN`.

It calls `accept()` to obtain a client socket, marks that client fd non-blocking, and registers it with `POLLIN`. It then creates a default `Connection`, assigns the fd, stores it in `connections`, and stores a pointer in `fdToConnection`.

At this point the server is not reading or writing application data. It is only waiting for the client socket to become readable.

## 6. Reading and Buffering an HTTP Request

When a client fd reports `POLLIN`, [`Worker::onReadable()`](../srcs/Worker.cpp) reads up to 4096 bytes into a stack buffer and appends the result to `Connection::inbuf`. A zero or negative read result closes only that managed connection; the code does not inspect `errno` after the read.

The input buffer matters because one TCP read is not one HTTP request. A request can arrive in several packets, and multiple requests can arrive together. `Worker` therefore calls:

```cpp
ssize_t req_size = http.parse(conn.inbuf, conn.txn.request);
```

[`Http::parse()`](../srcs/Http.cpp) does not read the socket and does not mutate `inbuf`. Its current contract is:

```text
positive value  a full request was parsed; value is bytes consumed
0               more input is needed
-1              malformed request line
```

It looks for the HTTP header terminator, `\r\n\r\n`. When present, it splits the request line into method, target, and version; separates a `?query` from the path; collects header fields; then uses `Content-Length` to decide whether the body is complete. On success, it assigns a local parsed request to `conn.txn.request` and returns the number of bytes that belong to that request.

`Worker` removes exactly that many bytes from `conn.inbuf` only after a complete result. This is the key ownership rule: **`Worker` owns the byte buffer; `Http` interprets it.**

Current-state note: request-line/header validation, safe `Content-Length` handling, chunked bodies, and parser-error responses are still incomplete on this branch.

## 7. Resolving the Route

For each complete request, `Worker` calls:

```cpp
conn.txn.route = config.route(conn.txn.request);
```

The returned `Route` tells the worker whether the request takes the static or CGI path. In the target design, this comes from parsed server and location blocks.

On the current branch, [`Config::route()`](../srcs/Config.cpp) is a mock: it returns `./contents` as the root and treats a path ending in `.sh` as CGI using `./contents/cgi/test.sh`. It does not yet match a real configuration file or enforce configured methods.

## 8. Static Response Path

For a non-CGI route, `Worker` calls:

```text
StaticFile::serve(route, request)
  -> Content { status, body, mime_type }
  -> Response { status, headers, body }
  -> Http::build(response)
  -> Connection::outbuf
```

[`StaticFile::serve()`](../srcs/StaticFile.cpp) currently has a MIME-type map, but it does not read from disk. It creates placeholder HTML that includes the requested path. `Worker` places that body and MIME type in `conn.txn.response`.

[`Http::build()`](../srcs/Http.cpp) serializes `Response` into HTTP bytes. It supplies a default `200` status when needed, adds a default `Content-Type`, calculates `Content-Length`, writes the HTTP status line and headers, then appends the body.

The completed response bytes are appended to `Connection::outbuf`, and `poller.setEvents(conn.fd, POLLOUT)` changes the client interest from reading to writing.

Current-state note: real filesystem resolution, configured roots, index files, autoindex, uploads, `DELETE`, redirects, and configured error pages are not implemented yet.

## 9. Writing a Response

When the client socket reports `POLLOUT`, [`Worker::onWritable()`](../srcs/Worker.cpp) writes from:

```text
conn.outbuf.data() + conn.sent
```

`conn.sent` is the partial-write cursor. If a write transfers only part of the response, the cursor advances and the rest stays queued for a later `POLLOUT`. When all bytes have been written, the worker clears the output buffer, resets `conn.sent`, resets `conn.txn` to a fresh transaction, and switches the client interest back to `POLLIN`.

This is the foundation for non-blocking output: no response assumes it can be sent in one system call.

If a write returns zero or a negative value, `onWritable()` closes only that managed connection without inspecting `errno`. Complete keep-alive policy and timeout handling remain unfinished.

## 10. CGI Response Path

A mocked `.sh` route takes the CGI branch instead of `StaticFile`.

[`Cgi::start()`](../srcs/Cgi.cpp) creates two pipes:

```text
Worker writes request body -> CGI stdin
CGI stdout -> Worker reads response
```

It validates pipe creation and `fork()`, then calls `fork()`. The child duplicates the read end of the input pipe to standard input and the write end of the output pipe to standard output, builds CGI environment variables from `Request`, and calls `execve()` for the script. If setup or `execve()` fails, the child exits rather than returning to the server loop. The parent closes the child-only pipe ends, stores the child pid and remaining pipe fds in `CgiJob`, and marks those parent pipe ends non-blocking.

Back in `Worker::onReadable()`, the CGI stdin fd is registered for `POLLOUT` and the CGI stdout fd for `POLLIN`. Both map back to the same client `Connection` through `fdToConnection`.

When CGI stdin is writable, [`Worker::onCgiWritable()`](../srcs/Worker.cpp) calls `Cgi::sendBody()`. It advances `CgiJob::sent` until the request body is fully written, then removes and closes CGI stdin so the script sees EOF. A failed pipe read/write marks `CgiJob` as failed without checking `errno` after I/O.

When CGI stdout is readable, [`Worker::onCgiReadable()`](../srcs/Worker.cpp) calls `Cgi::collect()`, which accumulates output in `CgiJob::output`. At EOF, `Cgi::buildResponse()` separates CGI headers from the body, copies `Content-Type`, `Status`, and other headers into a `Response`, and sends that response through the same `Http::build()` and client `POLLOUT` path used for static responses.

Current-state note: CGI pipe setup and return-value-only I/O failures now produce a controlled `502` response. CGI is still only partially complete: it lacks non-blocking child reaping, timeouts, full configuration-driven handler selection, and complete EOF/body-edge-case behavior.

## 11. Logging and Exceptions

[`Logger`](../srcs/Logger.cpp) provides a stream-style debug logger. Expressions such as:

```cpp
logger.debug() << "Worker: fd " << conn.fd;
```

build a temporary `LogStream`; its destructor flushes the accumulated message at the end of the expression. `main()` catches exceptions that leave `Worker::run()` and sends their messages to `Logger::error()`.

Current-state note: access logging is still a stub. Managed socket and pipe error paths now clean up their affected connection, but malformed-request handling, timeout behavior, and full resilience coverage still need work.

## 12. Reading the Code in Order

For a first pass through the repository, read in this order:

1. [`srcs/main.cpp`](../srcs/main.cpp): object construction and the single call to `Worker::run()`.
2. [`headers/types.hpp`](../headers/types.hpp): the state vocabulary shared between services.
3. [`headers/Worker.hpp`](../headers/Worker.hpp) and [`srcs/Worker.cpp`](../srcs/Worker.cpp): fd ownership and lifecycle dispatch.
4. [`headers/Poller.hpp`](../headers/Poller.hpp) and [`srcs/Poller.cpp`](../srcs/Poller.cpp): readiness registration.
5. [`headers/Http.hpp`](../headers/Http.hpp) and [`srcs/Http.cpp`](../srcs/Http.cpp): protocol bytes in and out.
6. [`srcs/Config.cpp`](../srcs/Config.cpp) and [`srcs/StaticFile.cpp`](../srcs/StaticFile.cpp): current mock routing/content behavior.
7. [`srcs/Cgi.cpp`](../srcs/Cgi.cpp): the alternative non-blocking pipe path.

That order mirrors a request's actual journey and makes it easier to distinguish the existing skeleton from the remaining subject work.
