# Architecture

This document describes the implementation currently on `main`. It distinguishes connected behavior from known gaps so that the code, tests, and project plan do not drift apart.

Status labels:

- `Implemented`: connected to the running server.
- `Partial`: connected but incomplete, insufficiently verified, or needing hardening.
- `To Fix`: a known defect or a behavior that does not meet the intended contract.

## Runtime Model

`main()` constructs `Config`, `Http`, `Cgi`, `StaticFile`, and `Logger`, then passes them to `Worker`. `Config` parses the command-line file before the event loop starts. `Worker` owns the event loop, listener/client descriptors, connection state, and session store; other services are called by it.

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
- `To Fix`: add dedicated valid and malformed configuration tests; make location defaults inherit `server.root`; validate negative body sizes, addresses, and duplicate listeners at parse time; make `upload off` meaningful.

### `Worker` and `Poller` - Partial

- Creates non-blocking IPv4 listeners, accepts non-blocking client sockets, and manages them through one `poll()` loop.
- Buffers fragmented input, preserves pipelined bytes, handles partial writes, and keeps HTTP/1.1 connections open unless `Connection: close` is requested or an error requires closing after flush.
- Resolves the route using the accepted listener's server index, applies method policy, and dispatches static files, uploads, deletes, redirects, sessions, or CGI.
- Sweeps idle clients after 30 seconds and CGI jobs after 15 seconds; CGI cleanup escalates from `SIGTERM` to `SIGKILL` and uses non-blocking reaping.
- `To Fix`: accept until the listener would block; retain a queued response when `POLLIN` and `POLLHUP` arrive together; add graceful process shutdown that terminates/reaps active CGI children.

### `Http` - Partial

- Parses exact HTTP/1.1 origin-form request lines and CRLF header fields.
- Enforces a 16 KiB header limit, 100 header fields, one `Content-Length`, and only `Transfer-Encoding: chunked` as a transfer coding.
- Parses fixed-length and chunked bodies, applies the selected server's decoded body limit, and leaves pipelined bytes for the next transaction.
- Builds responses with controlled status/reason phrases, one calculated `Content-Length`, a selected `Content-Type`, and validated extension headers.
- `To Fix`: require and validate HTTP/1.1 `Host` semantics if virtual-host support is introduced.

### `StaticFile` - Partial

- Resolves route-relative paths, rejects lexical `.` and `..` segments, identifies MIME types, serves regular files, and supports configured indexes/autoindex.
- Implements permitted static `DELETE` and exclusive single-filename uploads.
- Loads configured error-page files while preserving the original response status.
- `To Fix`: canonicalize static paths to prevent symlinks beneath a root from exposing files outside it; avoid reading arbitrary static files fully into memory.

### `Cgi` - Partial

- Creates stdin/stdout pipes, forks only for CGI, and makes the parent pipe ends non-blocking.
- Separates configured handlers from scripts, constructs CGI/1.1 environment variables, canonicalizes scripts beneath the route root, and runs the child from its script directory.
- Streams request bodies to CGI through readiness events, collects output, applies timeouts, and frames the result through `Http`.
- `To Fix`: bound collected CGI output, validate CGI response headers more strictly, and ensure process-level shutdown cleans up active children.

### `SessionStore` - Implemented but Unconfigured

- Stores opaque `/dev/urandom`-backed session identifiers in memory, produces `HttpOnly` cookies, and expires sessions during Worker sweeps.
- The handler remains wired into `Worker`, but the parser has no `session_demo` directive and neither supplied configuration reaches it. The former `/session` demonstration is therefore not active.

### `Logger` - Partial

- Provides error and stream-style debug logging.
- `To Fix`: implement access logging and avoid logging complete request bodies or sensitive headers at the default debug level.

## Request Flow

1. `Config` parses the supplied file and `Worker` binds each parsed listener.
2. `Worker::acceptNew()` assigns the listener's server index to a new `Connection`.
3. Readable client bytes append to `Connection::inbuf`; `Http` returns a complete request, more-input result, or controlled parse error.
4. `Worker` obtains `Config::route(serverIndex, request)`, rejects disallowed methods, then chooses DELETE, upload, session, redirect, CGI, or static handling.
5. Each handler produces a `Response`; `Http` serializes it into `Connection::outbuf`.
6. Writable events advance the partial-write cursor. When the response finishes, the transaction resets and any buffered next request is processed.

## Supplied Configurations

- `config/default.config`: browser demonstration on port `8080`, with `/`, `/files`, `/uploads`, `/cgi`, and `/old-page`.
- `config/req.config`: multi-listener fixture on ports `8002`, `8003`, `8008`, and `8001` used by parser and connection-lifecycle coverage.

## Verification Status

Focused tests currently passing include HTTP parser/response tests, parsed configuration model and malformed-input tests, static-file tests, session-store tests, event-loop stress tests, CGI lifecycle tests, and connection-lifecycle tests.

`make test` is currently **not green**. It passes both parser targets and currently stops at `cgi-pipe-test`: that test, along with cookie-session and resilience integration coverage, still launches `config/req.config` while assuming retired port-8080 mock routes. These are tracked integration-test migrations, not evidence that the parser is still mock-backed.
