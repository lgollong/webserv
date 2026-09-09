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
| `Request` | Incoming method, path, query, HTTP version, headers, and body. | One `Transaction`; filled by `Http`. |
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

`POLLIN` means data may be read without waiting. `POLLOUT` means queued output may be written without waiting. The current [`Poller::poll()`](../srcs/Poller.cpp) calls the system `poll()` once for its whole vector with a caller-supplied timeout and returns the readiness count; `Poller::events()` exposes the resulting fd entries to `Worker`.

The intended discipline is:

```text
register fd -> wait in poll() -> inspect revents -> perform one appropriate action
```

`Worker` copies the resulting fd entries before dispatching callbacks because a callback may remove an fd. It handles managed client/CGI error, hangup, and invalid-fd events by cleaning up the owning connection; it attempts to recreate a failed listener. It gives `poll()` a one-second timeout and sweeps client connections after a wait timeout or after ready events have been dispatched. The sweep collects expired fds before using the same cleanup path, so it does not invalidate map iteration or a ready-event snapshot. CGI connections are excluded here because #35 owns their deadline and child lifecycle.

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

It calls `accept()` to obtain a client socket, marks that client fd non-blocking, and registers it with `POLLIN`. It then creates a `Connection`, assigns the fd, records the current time as client activity, sets its phase to `READING`, stores it in `connections`, and stores a pointer in `fdToConnection`.

At this point the server is not reading or writing application data. It is only waiting for the client socket to become readable. A client in `READING` or `WRITING` that makes no accepted/read/write progress for 30 seconds is closed by the sweep; the same timeout intentionally does not apply while `RUNNING_CGI`.

## 6. Reading and Buffering an HTTP Request

When a client fd reports `POLLIN`, [`Worker::onReadable()`](../srcs/Worker.cpp) reads up to 4096 bytes into a stack buffer and appends the result to `Connection::inbuf`. A positive read refreshes `last_activity`; a zero or negative read result closes only that managed connection, and the code does not inspect `errno` after the read.

The input buffer matters because one TCP read is not one HTTP request. A request can arrive in several packets, and multiple requests can arrive together. `Worker` therefore calls:

```cpp
ssize_t req_size = http.parse(conn.inbuf, conn.txn.request);
```

[`Http::parse()`](../srcs/Http.cpp) does not read the socket and does not mutate `inbuf`. Its current contract is:

```text
positive value  a full request was parsed; value is bytes consumed
0               more input is needed
-1              malformed or unsupported request data
```

It looks for the HTTP header terminator, `\r\n\r\n`, and rejects an unfinished header block once it grows beyond 16 KiB. When present, it strictly validates an exact `method SP origin-form-target SP HTTP/1.1` request line, separates a `?query` from the path, and stores the HTTP version. Each header name must be a token, and the parser canonicalizes it to lowercase, trims optional outer whitespace from its value, and rejects malformed line endings and control bytes. It limits a request to 100 header fields, rejects duplicate `Content-Length`, and accepts only a sole `Transfer-Encoding: chunked` field. A `Content-Length` must be one or more decimal digits and fit in `size_t`; the parser then waits until exactly that body is buffered. For chunked input, it validates hexadecimal chunk sizes, ignores chunk extensions, decodes each chunk into the request body, validates trailers without merging them into request headers, and ends the request at the zero chunk. Its returned byte count ends after that body or trailer block, so subsequent pipelined request bytes remain in `Connection::inbuf`.

The normal call currently uses a temporary 10,000,000-byte body limit. `Http` also exposes `parse(inbuf, request, maxBodyBytes)` so #5 can pass a parsed `client_max_body_size` before the body is buffered; the parser rejects an over-limit declaration without copying body bytes.

`Worker` uses the error-status overload. On a `-1` result, it receives `400` for malformed syntax/framing, `413` for a body over the parser limit, or `431` for a header limit. It clears the unprocessable input, asks `Http::defaultErrorResponse()` for a matching HTML `Response`, serializes it through `Http::build()`, marks `Connection::close_after_write`, and switches the fd to `POLLOUT`. This prevents malformed data from leaving the connection waiting in `POLLIN` forever while still returning a complete HTTP response.

`Worker` removes exactly that many bytes from `conn.inbuf` only after a complete result. This is the key ownership rule: **`Worker` owns the byte buffer; `Http` interprets it.**

Current-state note: request-line, header syntax/framing, fixed-limit `Content-Length` and chunked-body assembly, and default parser-error responses are implemented. Configuration-driven body-size policy and configured custom error-page bodies remain incomplete.

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

[`Http::build()`](../srcs/Http.cpp) serializes `Response` into HTTP bytes. It supplies `200` when no status is given and falls back to `500` for an unsupported status. It selects one case-insensitive `Content-Type` or uses `text/html`, calculates and owns one `Content-Length`, filters malformed header names or values, preserves valid extension headers, then writes the status line, headers, separator, and body. `204` and `304` responses discard body bytes before calculating the length. For an error status with no body, the worker first uses `Http::defaultErrorResponse()`; this produces fixed HTML for known 4xx/5xx statuses and falls back to `500` for any other input.

The completed response bytes are appended to `Connection::outbuf`, and `poller.setEvents(conn.fd, POLLOUT)` changes the client interest from reading to writing.

Current-state note: real filesystem resolution, configured roots, index files, autoindex, uploads, `DELETE`, redirects, and configured error pages are not implemented yet.

## 9. Writing a Response

When the client socket reports `POLLOUT`, [`Worker::onWritable()`](../srcs/Worker.cpp) writes from:

```text
conn.outbuf.data() + conn.sent
```

`conn.sent` is the partial-write cursor. A positive write refreshes the client's activity time. If a write transfers only part of the response, the cursor advances and the rest stays queued for a later `POLLOUT`. When all bytes have been written, the worker clears the output buffer and resets `conn.sent`. For a parser failure, `close_after_write` is set and the worker closes only that connection at this point. Otherwise it resets `conn.txn`, returns the phase to `READING`, and switches the client interest back to `POLLIN`.

This is the foundation for non-blocking output: no response assumes it can be sent in one system call.

If a write returns zero or a negative value, `onWritable()` closes only that managed connection without inspecting `errno`. Client inactivity timeout handling is implemented; complete keep-alive policy and CGI timeout handling remain unfinished.

## 10. CGI Response Path

A mocked `.sh` route takes the CGI branch instead of `StaticFile`.

[`Cgi::start()`](../srcs/Cgi.cpp) creates two pipes:

```text
Worker writes request body -> CGI stdin
CGI stdout -> Worker reads response
```

It validates pipe creation and `fork()`, then calls `fork()`. The child duplicates the read end of the input pipe to standard input and the write end of the output pipe to standard output, builds CGI environment variables from `Request`, and calls `execve()` for the script. If setup or `execve()` fails, the child exits rather than returning to the server loop. The parent closes the child-only pipe ends, stores the child pid and remaining pipe fds in `CgiJob`, and marks those parent pipe ends non-blocking.

Back in `Worker::onReadable()`, the CGI stdin fd is registered for `POLLOUT` and the CGI stdout fd for `POLLIN`. Both map back to the same client `Connection` through `fdToConnection`.

When CGI stdin is writable, [`Worker::onCgiWritable()`](../srcs/Worker.cpp) calls `Cgi::sendBody()`. For a chunked request, this is the decoded `Request::body`, not the wire chunk framing. It advances `CgiJob::sent` until the request body is fully written, then removes and closes CGI stdin so the script sees EOF. A failed pipe read/write marks `CgiJob` as failed without checking `errno` after I/O.

When CGI stdout is readable, [`Worker::onCgiReadable()`](../srcs/Worker.cpp) calls `Cgi::collect()`, which accumulates output in `CgiJob::output`. At EOF, `Cgi::buildResponse()` separates CGI headers from the body, copies `Content-Type`, `Status`, and other headers into a `Response`, and sends that response through the same `Http::build()` and client `POLLOUT` path used for static responses. If the CGI result is an error with no body, `Worker` replaces it with `Http::defaultErrorResponse()` before serialization.

Current-state note: CGI pipe setup and return-value-only I/O failures now produce a controlled `502` response with a default HTML body. CGI is still only partially complete: it lacks non-blocking child reaping, timeouts, full configuration-driven handler selection, and complete EOF/body-edge-case behavior.

## 11. Logging and Exceptions

[`Logger`](../srcs/Logger.cpp) provides a stream-style debug logger. Expressions such as:

```cpp
logger.debug() << "Worker: fd " << conn.fd;
```

build a temporary `LogStream`; its destructor flushes the accumulated message at the end of the expression. `main()` catches exceptions that leave `Worker::run()` and sends their messages to `Logger::error()`.

Current-state note: access logging is still a stub. Managed socket and pipe error paths now clean up their affected connection, malformed requests receive a response before their connection closes, and inactive client connections expire after 30 seconds. CGI timeout behavior and full resilience coverage still need work.

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
