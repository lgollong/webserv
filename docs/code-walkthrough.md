# Code Walkthrough

This walkthrough follows the code that currently runs on `main`, beginning at `main()` and ending at static and CGI responses. It describes parser-backed configuration rather than the retired reference mock.

## 1. Startup

[`main()`](../srcs/main.cpp) requires one configuration-file argument, constructs `Config`, `Http`, `Cgi`, `StaticFile`, `Logger`, and `Worker`, then calls `Worker::run()`.

`Config(configPath)` reads and tokenizes the file before the event loop exists. It accepts comments, quotes, `server` blocks, and nested `location` blocks. Any unreadable file or recognized syntax error throws to `main()`, which logs the error and exits. No fallback configuration is constructed.

## 2. Parsed Configuration

[`Config`](../srcs/Config.cpp) stores every parsed server in `std::vector<ServerConfig>`. A server contains its host, port, CGI-facing name, body limit, custom error pages, and locations. A location contains a URL prefix, static root, method set, directory behavior, upload store, redirect, and CGI extension handlers.

The default demonstration file opens `0.0.0.0:8080`; `config/req.config` instead defines four loopback listeners for integration coverage. `Worker` does not select a server from the HTTP `Host` header. It uses the server index associated with the listener that accepted the client.

`Config::route(serverIndex, request)` chooses the longest location prefix with a path boundary. It returns a route copy and, when a request path matches a configured CGI extension, fills in the handler path, URL script name, and route-root script path. A suffix after the script becomes CGI `PATH_INFO`.

## 3. Starting the Event Loop

[`Worker::run()`](../srcs/Worker.cpp) creates one listener per `ServerConfig`, marks it non-blocking, and registers it with [`Poller`](../srcs/Poller.cpp) for `POLLIN`.

`poll()` waits until a listener, client socket, or CGI pipe is ready. A stable copy of the ready `pollfd` entries is dispatched because callbacks may remove descriptors from the live poller. The loop wakes at least once per second for client, CGI, and session cleanup.

## 4. Accepting and Reading a Client

When a listener is readable, `Worker::acceptNew()` accepts one connection, marks it non-blocking, sets its phase to `READING`, and records the accepting listener's server index.

Readable client data is appended to `Connection::inbuf`. A TCP read does not correspond to one HTTP request, so `Worker::processBufferedRequest()` asks `Http::parse()` whether a complete request is available:

```text
positive value  complete request; value is consumed byte count
0               more bytes are required
-1              malformed request; the parser supplies an HTTP error status
```

The parser validates the request line and headers, supports `Content-Length` and chunked bodies, and applies the accepted server's configured body limit before a complete body is copied. Later pipelined bytes stay in `inbuf` while the current transaction owns its response.

## 5. Route and Handler Selection

After parsing, `Worker` asks `Config` for the route using the connection's server index. It rejects methods outside the route's `allowed_methods` set with `405 Method Not Allowed` and an `Allow` header.

For allowed requests, dispatch order is:

1. `DELETE` goes to `StaticFile::erase()`.
2. `POST` on a route with both `upload on` and `upload_store` goes to `StaticFile::upload()`.
3. A configured `session_demo` route goes to `SessionStore`; no supplied parsed configuration currently uses this flag.
4. A configured `return` becomes a redirect response.
5. A resolved CGI route starts a `CgiJob`.
6. All other requests go to `StaticFile::serve()`.

The parser preserves the upload authorization flag, so `upload off` prevents writes even when a storage path is present.

## 6. Static Responses

[`StaticFile`](../srcs/StaticFile.cpp) removes the location prefix, rejects lexical traversal components, and combines the remainder with the route root. It serves regular files, optionally serves a safe configured index file, or builds an escaped directory listing when `autoindex` is enabled. Missing files are `404`; rejected paths and non-autoindexed directories are `403`.

Permitted uploads use an exclusive create in `upload_store`, allowing only one safe filename. Permitted DELETE removes only a regular file. Static content and configured error pages are presently collected into memory before `Http` serializes the response.

## 7. CGI

[`Cgi::start()`](../srcs/Cgi.cpp) resolves the script with `realpath`, verifies it lies beneath the route root, creates stdin/stdout pipes, forks, and uses `execve()` for either the configured handler plus script or the script itself. The child receives CGI variables including method, path info, query, protocol, server name/port, content type, content length, and normalized request headers.

The parent registers CGI stdin for `POLLOUT` when there is a request body and CGI stdout for `POLLIN`. `Cgi::sendBody()` and `Cgi::collect()` advance through readiness callbacks without blocking the event loop. At EOF and after non-blocking reaping, the CGI output is converted to a `Response` and passes through the same HTTP serializer as static content.

CGI jobs time out after 15 seconds, receive `SIGTERM`, then `SIGKILL` after a two-second grace period if necessary. Client-disconnect cleanup follows the same reap path. Process termination of the whole server does not yet perform this cleanup.

## 8. Writing and Persistence

Every handler result becomes `Response`, then `Http::build()` produces `Connection::outbuf`. It supplies one `Content-Length`, chooses a content type, and filters invalid extension headers.

`Worker::onWritable()` tracks partial writes with `Connection::sent`. Once all bytes are written, a `Connection: close` response closes the client; otherwise the transaction resets to `READING` and one buffered pipelined request can begin immediately.

## 9. Current Verification State

The focused HTTP, parser-model/malformed-config, static-file, event-loop, CGI lifecycle, CGI-pipe, resilience, and connection-lifecycle targets exercise the paths above. The full `make test` target currently reaches `cookie-session-test` before stopping because the optional session demonstration still has mock-era configuration expectations. See [Testing and Evaluation](testing.md) for the exact status and outstanding coverage.
