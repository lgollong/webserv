# Architecture

This document describes the implementation currently on `main`. It distinguishes connected behavior from known gaps so that the code, tests, and project plan do not drift apart.

Status labels:

- `Implemented`: connected to the running server.
- `Partial`: connected but incomplete, insufficiently verified, or needing hardening.
- `To Fix`: a known defect or a behavior that does not meet the intended contract.

## Runtime Model

`main()` constructs `Config`, `Http`, `Cgi`, `StaticFile`, and `Logger`, then passes them to `Worker`. `Config` parses the command-line file, or `config/default.config` when no argument is given, before the event loop starts. `Worker` owns the event loop, listener/client descriptors, connection state, and session store; other services are called by it.

```text
main
  -> Config (parse file)
  -> Worker
       -> Poller
       -> Connection per client fd
            -> Transaction per request
            -> CgiJob while CGI is active
```

`Worker` uses one `poll()` wrapper for listeners, clients, and CGI pipes. `connections` owns `Connection` values; `fdToConnection` maps every registered client or pipe fd back to the same connection.

## Components

### `Config` - Partial

- Parses `server` and `location` blocks, comments, quoted tokens, supported server directives, and supported route directives from the supplied file.
- Represents one parsed server as a listener, body limit, error-page map, and set of routes. `Worker` binds every parsed server and records the accepting server index on the client.
- Resolves boundary-aware longest-prefix locations and derives CGI handler/script information from the selected route's extension map.
- Throws descriptive startup errors for unreadable files, unclosed quotes, unknown directives, and several malformed directive forms. There is no mock fallback.
- `To Fix`: validate listener addresses and duplicate host/port pairs at parse time.

### `Worker` and `Poller` - Partial

- Creates non-blocking IPv4 listeners, accepts non-blocking client sockets, and manages them through one `poll()` loop.
- Buffers fragmented input, preserves pipelined bytes, handles partial writes, and keeps HTTP/1.1 connections open unless `Connection: close` is requested or an error requires closing after flush.
- Resolves the route using the accepted listener's server index, applies method policy, and dispatches static files, uploads, deletes, redirects, sessions, or CGI.
- Sweeps idle clients after 30 seconds and CGI jobs after 15 seconds; CGI cleanup escalates from `SIGTERM` to `SIGKILL` and uses non-blocking reaping.
- Treats `POLLHUP` together with `POLLIN` as "read until EOF" (macOS reports a client half-close that way), never discards a queued response because of `POLLHUP`, and ignores `SIGPIPE` so writes to a vanished peer fail through their return value. EOF while CGI is still running aborts the CGI, as nginx does for a client that closes its request.
- `To Fix`: accept until the listener would block; add graceful process shutdown that terminates/reaps active CGI children.

### `Http` - Partial

- Parses HTTP/1.x origin-form request lines (HTTP/1.0 and HTTP/1.1; a higher 1.x minor version is treated as 1.1) and CRLF header fields; other major versions receive `505`. HTTP/1.0 connections close after the response unless the client sends `Connection: keep-alive`.
- Enforces a 16 KiB header limit, 100 header fields, one `Content-Length`, and only `Transfer-Encoding: chunked` as a transfer coding.
- Parses fixed-length and chunked bodies, applies the selected server's decoded body limit, and leaves pipelined bytes for the next transaction.
- Builds responses with controlled status/reason phrases, one calculated `Content-Length`, a selected `Content-Type`, and validated extension headers.
- `To Fix`: require and validate HTTP/1.1 `Host` semantics if virtual-host support is introduced.

### `StaticFile` - Partial

- Resolves route-relative paths, rejects lexical `.` and `..` segments, identifies MIME types, serves regular files, and supports configured indexes/autoindex.
- Implements permitted static `DELETE`, exclusive raw-body uploads, and `multipart/form-data` browser uploads; upload-enabled routes serve and delete from their `upload_store`.
- Loads configured error-page files while preserving the original response status.
- Streams served files from disk in 64 KiB chunks, so memory use does not grow with file size.
- Never follows symlinks below a route root: static GET, index lookup, DELETE, and CGI scripts reject a path with a symlinked component (`403`), and autoindex hides symlinks. Detection uses `readdir()` `d_type` because `realpath`, `lstat`, and `readlink` are not authorized; file systems that report `DT_UNKNOWN` cannot be checked.

### `Cgi` - Partial

- Creates stdin/stdout pipes, forks only for CGI, and makes the parent pipe ends non-blocking.
- Separates configured handlers from scripts, constructs CGI/1.1 environment variables, keeps scripts beneath the route root through lexical `.`/`..` rejection, and runs the child from its script directory with the script addressed by its base name.
- Streams request bodies to CGI through readiness events, collects output, applies timeouts, and frames the result through `Http`.
- Validates CGI response framing, header syntax, and `Status`; malformed output becomes `502`.
- Closes all server-owned descriptors in the child before `execve()`, so a CGI never holds a listener port or another client's connection open.
- Caps collected CGI output at 16 MiB; larger output terminates the child and returns `502`.
- `To Fix`: ensure process-level shutdown cleans up active children.

### `SessionStore` - Implemented

- Stores opaque `/dev/urandom`-backed session identifiers in memory, produces `HttpOnly` cookies, and expires sessions during Worker sweeps.
- The handler is wired into `Worker` via the `session_demo on;` location directive; `config/default.config` enables it on `/session`, which responds with a visit counter and sets the session cookie on first visit.

### `Logger` - Partial

- Provides error and stream-style debug logging gated by the top-level `log_level none|error|debug;` directive (default `error`). Disabled debug streams allocate nothing, and full payload dumps are skipped unless debug is enabled.
- `To Fix`: implement access logging; debug level still logs complete request bodies and headers, so keep it for local troubleshooting.

## Request Flow

1. `Config` parses the supplied file and `Worker` binds each parsed listener.
2. `Worker::acceptNew()` assigns the listener's server index to a new `Connection`.
3. Readable client bytes append to `Connection::inbuf`; `Http` returns a complete request, more-input result, or controlled parse error.
4. `Worker` obtains `Config::route(serverIndex, request)`, rejects disallowed methods, then chooses DELETE, upload, session, redirect, CGI, or static handling.
5. Each handler produces a `Response`; `Http` serializes it into `Connection::outbuf`.
6. Writable events advance the partial-write cursor. When the response finishes, the transaction resets and any buffered next request is processed.

## Supplied Configurations

- `config/default.config`: browser demonstration on port `8080`, with `/`, `/files`, `/uploads` (and the `/upload.html` page), `/cgi` (Python and shell), `/old-page`, and the `/session` cookie demonstration.
- `config/req.config`: multi-listener fixture on ports `8002`, `8003`, `8008`, and `8001` used by parser and connection-lifecycle coverage.

## Verification Status

`make test` is **green**: all fourteen targets (HTTP parser/response, timeout predicates, logger, configuration model and parser, static files, session store, event loop, CGI lifecycle, connection lifecycle, CGI pipes, cookie sessions, and resilience) pass on macOS and on Linux with g++ and clang++. See [Testing and Evaluation](testing.md).
