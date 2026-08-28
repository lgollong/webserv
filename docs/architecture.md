# Architecture

This document describes the implementation currently on `main` and identifies the work still required for the 42 `webserv` subject. It is a source-of-truth companion to the code, not a description of the intended final server only.

Status labels used here:

- `Implemented`: code exists and is connected to the running server.
- `Partial`: code exists but is intentionally incomplete or does not yet meet the subject requirement.
- `Planned`: no meaningful implementation exists yet.
- `To Fix`: the current code conflicts with a project requirement or needs hardening.

## Runtime Model

`main.cpp` constructs the service objects and passes them to `Worker`. `Worker` owns the event loop, all client connections, and the `Poller`; the other components are passive services called by `Worker`.

```text
main
  -> Config, Http, Cgi, StaticFile, Logger
  -> Worker
       -> Poller
       -> Connection per client fd
            -> Transaction per request
            -> CgiJob while CGI is active
```

`Worker` uses one `poll()` wrapper for the listening socket, client sockets, and CGI pipe file descriptors. Connection state is stored in `std::map<int, Connection>` and a second map associates every registered fd with its owning connection.

## Components

### `Worker`

`Implemented`, with important `To Fix` items.

- Creates a non-blocking listening socket and runs the central event loop.
- Accepts clients, registers client and CGI pipe fds with `Poller`, and dispatches readable and writable events.
- Buffers incoming request bytes in `Connection::inbuf` and queued output in `Connection::outbuf`.
- Tracks partial client writes with `Connection::sent`.
- Wakes from `poll()` at least once per second and closes client sockets that make no accepted/read/write progress for 30 seconds.
- Takes a stable snapshot of ready events before callbacks remove fds, and has one cleanup path for a client and its registered CGI pipe fds.
- Handles client and CGI `POLLERR`, `POLLHUP`, and `POLLNVAL` paths without throwing from the event loop.
- Creates one non-blocking listening socket for every configured `host`/`port` pair and records the associated server index on each accepted connection.
- `To Fix`: accept until the listener would block and broaden end-to-end network-load coverage beyond the focused readiness tests.
- Resets one completed transaction before beginning a later request already buffered on the same client connection. HTTP/1.1 connections persist by default; a case-insensitive `Connection: close` token marks only that response for close-after-flush and adds one matching response header.
- Rejects a request absent from the selected non-empty route method set with a framed `405 Method Not Allowed` response and a deterministic `Allow` header before redirect, CGI, or static dispatch.
- Dispatches a route-approved `DELETE` to `StaticFile`, which removes only a route-relative regular disk file and returns `204 No Content`; directories and rejected paths remain errors.
- Dispatches POST to a route with `upload_store` to exclusive, safe single-filename disk storage; success returns `201 Created` and duplicate or invalid targets are rejected.
- Queues a selected route's configured 3xx response with `Location` before it can enter CGI or static-file handling.
- `To Fix`: CGI route selection still comes from the mock configuration rather than parsed server/location directives.

### `Poller`

`Implemented`, but minimal.

- Wraps a `std::vector<pollfd>` and supports adding, removing, and changing fd interests.
- Calls the single process-wide `poll()` used by `Worker` with a caller-supplied timeout and returns its readiness result.
- `Partial`: `Worker` logs a failed wait, handles invalid/error events for managed connection fds, and recreates a failed listener for the same configured server.

### `Http`

`Partial`.

- Strictly parses a buffered HTTP/1.1 request line into method, origin-form path, query, and version state.
- Strictly parses CRLF-delimited request headers with token field names, lowercase canonical names, and trimmed optional whitespace.
- Rejects malformed header syntax, more than 100 fields, headers larger than 16 KiB, duplicate `Content-Length`, and unsupported or conflicting transfer codings. It accepts only a sole `Transfer-Encoding: chunked` field.
- Parses `Content-Length` as one or more decimal digits with checked `size_t` conversion; signs, whitespace within the value, trailing data, and overflow are rejected.
- Waits for the complete declared body, returns the exact consumed count before any pipelined bytes, and rejects body/request sizes that cannot be represented by the parser contract.
- Decodes chunked bodies before assigning `Request::body`, supports chunk extensions and syntax-checked trailers, and applies the body limit to decoded bytes. Chunk trailers are not merged into request headers.
- Receives the accepted connection's selected server `client_max_body_size` from `Worker` through its limit-aware parse overload before a body is buffered.
- Returns positive consumed bytes for a complete request, `0` for an incomplete request, and `-1` for a malformed or unsupported request. Its error-status overload reports `400` for malformed syntax/framing, `413` for a declared body over the limit, and `431` for header limits.
- Builds a complete HTTP/1.1 response envelope for supported status codes, falling back to `500` for an unsupported status.
- Owns case-insensitive `Content-Type` selection and calculated `Content-Length`, suppresses unsafe or conflicting caller-supplied framing headers, preserves valid extension headers, and omits bodies for `204` and `304`.
- `Http::defaultErrorResponse()` creates deterministic HTML bodies for known error statuses; unknown or non-error inputs fall back to `500`.
- `Worker` loads an otherwise-empty error response body from the selected server's configured `error_pages` path through `StaticFile`, preserving its status and using the file MIME type. An absent, missing, non-regular, unreadable, or failed page load uses `Http::defaultErrorResponse()` instead.
- `To Fix`: have parsed server/location configuration preserve selected-server body limits and error-page mappings.
- `To Fix`: add handler-specific response headers and complete status/error-page behavior with the relevant handler work.

### `Config`

`Partial`.

- Defines normalized `ServerConfig` and `Route` data structures for listeners, server defaults, location prefixes, methods, redirects, directory behavior, uploads, error pages, request limits, and CGI extension handlers.
- The explicit reference mock contains two listener/server records and routes for static content, CGI, autoindex/index, uploads, redirects, and the optional GET-only `/session` demonstration. `make config-model-test` verifies that contract and longest-prefix resolution on the first reference server.
- The default root document is an implemented browser presentation dashboard with static CSS, JavaScript, and a request-path image. It links to active routes and fetches same-origin endpoint responses; it is static content served through the normal `StaticFile` path, not a separate runtime or control plane.
- The resolver accepts an explicit server index, resolves that server's longest matching location, and derives the current CGI handler and URL script name from the request extension and that route's handler map. A suffix after the script name remains available as CGI `PATH_INFO`. `Worker` assigns that index from the listener that accepted the connection and uses it for request-body limits, route resolution, and CGI server context.
- `Planned`: #4 must parse and validate configuration text into this same normalized model. Invalid parser input must not fall back to the reference mock.

### `StaticFile`

`Partial`.

- Resolves route-relative request paths under `Route::root`, rejects lexical `.` and `..` traversal segments before disk access, and serves regular disk files through `Content`.
- Reads configured error-page paths only when they resolve to a regular file, returning its MIME type so `Worker` can preserve the original error status while replacing its body.
- For a directory, serves the configured safe index filename when it is a regular file; otherwise returns a deterministic, escaped HTML listing only when autoindex is enabled.
- Uses filename-based MIME detection, returns `404` for missing files, `403` for rejected, non-regular, unopenable, or non-autoindexed directories, and `500` for a disk read failure.
- `make static-file-test` verifies text and binary reads, MIME detection, route-root and directory indexes, autoindex, missing/directory/traversal errors, location-prefix stripping, and location/root rejection.
- Accepts route-authorized POST uploads into the configured storage directory with an exclusive create, a safe single-file name, and `201 Created`; duplicate, invalid, or unauthorized targets are rejected. `make static-file-test` and `make connection-lifecycle-test` cover the storage and route-dispatch behavior.

### `Cgi`

`Partial`, with the event-loop pipe lifecycle implemented.

- Creates stdin/stdout pipes, resolves a route-root CGI script target and its optional configured handler independently, verifies the canonical script remains beneath the route root, changes the child directory to the script directory, runs either `handler script` or the executable script with `execve`, and sets the parent pipe ends non-blocking.
- Builds CGI/1.1 request context from the parsed request, selected route, and selected server: method, URL script name, path info, query, protocol, body metadata, server name/port, and normalized request headers.
- `Worker` registers the CGI pipes in the same `Poller` as sockets and transfers the request body/output through readiness callbacks.
- Reports pipe, fork, and non-blocking setup failures through `CgiJob`, and the child exits if `execve` fails.
- Treats failed pipe reads/writes, invalid pipe readiness, and CGI EOF before the complete request body is accepted as controlled CGI failures without inspecting `errno` after I/O; the response path produces `502` when no valid CGI result exists.
- Records CGI start and progress timestamps. `Worker` limits an active CGI job to 15 seconds, closes its pipes, sends `SIGTERM`, escalates to `SIGKILL` after two seconds if needed, and reaps it with `waitpid(..., WNOHANG)` before emitting a `502` response.
- Reaps normal completed children before resetting their transaction. On client disconnect, it retains the terminated PID and termination time in a worker-owned pending-reap map, where the same two-second `SIGKILL` escalation and non-blocking reap continue after the transaction is gone.
- `To Fix`: parse CGI handler selection from configuration and harden CGI response-header validation. The reference mock demonstrates `.sh` through `/bin/sh` and a directly executable `.cgi` fixture.

### `Logger`

`Partial`.

- Supports error logging and a C++98-compatible stream-style debug logger.
- `Planned`: implement access-log records and use consistent logging for request, disconnect, and failure paths.

## State Objects

`Implemented`, with incomplete lifecycle use.

- `Request` holds method, path, query, HTTP version, headers, and body.
- `Response` holds status, headers, and body.
- `Route` holds a root, CGI settings, and allowed methods.
- `Transaction` groups the parsed request, response, resolved route, and CGI job for one request.
- `Connection` owns socket identity, input/output buffers, partial-write position, per-response persistence/close state, last activity, and its current transaction. Client read/write phases use its last-activity timestamp for the 30-second client timeout.
- `CgiJob` owns child pid, stdin/stdout fds, write progress, output buffer, completion/failure state, start/progress timestamps, and termination state.
- `SessionStore` is process-local state owned by `Worker`. It parses the `webserv_session` request cookie, creates opaque `/dev/urandom`-backed identifiers, builds one `HttpOnly` cookie value with a 30-minute lifetime, and removes expired entries during the existing maintenance sweep. The GET-only reference-mock `/session` route creates a session on a missing/invalid/expired cookie and resumes a valid session counter; persistence and authentication are intentionally absent.
- `Partial`: client and registered CGI fds now have an explicit shared cleanup path. One response owns its transaction until it flushes; later buffered request bytes are retained and dispatched only after that reset. HTTP/1.1 persistence/close policy is active; remaining CGI protocol edge cases are incomplete.

## Current Request Flow

### Static Request

1. `Worker` receives a readiness event for a client socket and reads bytes into `Connection::inbuf`.
2. `Http` parses a complete `Request` when enough bytes are buffered.
3. `Config` returns a route.
4. A configured 3xx route is serialized with its `Location` header; otherwise `StaticFile` resolves the request relative to the selected route root and creates `Content` from a regular disk file or an error status.
5. `Http` serializes a `Response` into `Connection::outbuf`.
6. `Worker` changes the client interest to `POLLOUT` and writes until the buffer is complete.

If later request bytes were already buffered on that client socket, they remain in `Connection::inbuf` while this response owns the current `Transaction`. After the last response byte flushes, `Worker` resets that transaction and dispatches one complete buffered next request without waiting for another client write.

HTTP/1.1 persistence is active by default. If the normalized request `Connection` header contains a comma-delimited `close` token, `Worker` adds `Connection: close` to that response and closes that client only after the response flushes. Parser-error responses always use the same close-after-flush path. The worker applies this policy to redirect, static, and CGI responses and does not let CGI-provided `Connection` headers override it.

This establishes the intended ownership flow, but parsed route/server resolution remains incomplete.

### Parser Failure

1. `Http` returns `-1` and an error status to `Worker`.
2. `Worker` drops the malformed input, uses the selected server's configured error page when it is a readable regular file, otherwise creates the matching default HTML error `Response`, appends its serialized bytes, marks the connection to close after its output flushes, and waits for `POLLOUT`.
3. `Worker` closes only that client fd after the normal non-blocking write path completes.

### CGI Request

1. `Worker` identifies a CGI route and the selected `ServerConfig` from `Config`.
2. `Cgi` derives `SCRIPT_NAME` and `PATH_INFO` from the URL, resolves the configured handler separately from the route-root script target, verifies the canonical target remains below the route root, changes to the script directory, then starts the child and returns its pipe fds in `CgiJob`.
3. `Worker` closes CGI stdin immediately for an empty request body; otherwise it registers stdin for `POLLOUT`. It always registers CGI stdout for `POLLIN` in the same `Poller`.
4. `Cgi` sends the parsed body, already decoded when the request used chunked transfer coding, and collects CGI output through readiness callbacks. It closes stdin after the entire body is accepted. Stdout EOF before that point, plus pipe error or invalid-fd readiness, marks the job as failed instead of accepting a partial-body CGI response.
5. `Cgi` records its start time and `Worker` checks its 15-second lifetime during the periodic maintenance sweep. On normal completion, `Worker` reaps the child non-blockingly before converting the CGI output into a `Response`. On timeout or failure, it closes CGI pipes, sends `SIGTERM`, escalates to `SIGKILL` after two seconds if needed, reaps the child, then sends the default `502` response.

The pipe/body/EOF flow, request context, handler/script separation, route-root script validation, and two CGI types are wired and covered end to end. Configuration-file-driven handler selection and CGI response validation remain before full subject compliance.

## Resilience Verification

`make test` runs all repository test targets sequentially, including HTTP parsing/response, reference-model, static-file, session, event-loop, CGI, connection-lifecycle, and resilience coverage. Sequential execution prevents the loopback suites from contending for their fixed test listeners on ports `8080` and `8081`. [Testing and Evaluation](testing.md) maps the suite and manual checks to the current subject coverage; parsed configuration-file verification remains pending with #4, #5, and #6.

`make resilience-test` builds and runs a repository-local C++98 loopback integration suite against `./webserv`. It starts and reaps its own server process, keeps a silent client and an incomplete request connected until the 30-second client deadline, confirms normal requests still receive `200 OK`, exercises the test CGI fixture's controlled `?stall` mode until the 15-second CGI deadline produces `502 Bad Gateway`, and confirms the listener serves another request. It also disconnects an active stalled CGI client, verifies the recorded child PID disappears within the two-second termination grace plus polling allowance, and checks that the listener remains available. The suite is bounded and exits non-zero for any missed deadline, wrong status, early server exit, or unreaped CGI child.

`make event-loop-stress-test` builds a focused C++98 fd-level suite. It verifies that the real `poll()` wrapper reports `POLLNVAL` for a closed registered descriptor, dispatches client and CGI `POLLERR`/`POLLNVAL` paths through the production dispatcher, and uses a non-blocking socket pair with a restricted send buffer to prove that a partial client write resumes until the entire response is delivered. Test-only access is friend-scoped to the dispatch owner; production interfaces do not gain test hooks.

## Connection Lifecycle Verification

`make connection-lifecycle-test` builds and runs a separate fast C++98 loopback suite. It starts and reaps its own server, verifies that the same `GET /` resolves to the primary root on port `8080` and the secondary root on port `8081`, checks fragmented input, sequential default-persistent requests, and a concurrent second client, verifies the configured `/gallery` autoindex response and the `/redirect` response with `302 Found`, `Location: /gallery`, empty framing, and persistence, and reads two responses from one buffered CGI-plus-static request sequence. It also verifies that static, redirect, CGI, and parser-error close cases return exactly one `Connection: close` header and EOF only after their complete response. The command exits non-zero for an incorrect response boundary, missing/early EOF, unavailable listener, or server failure.

## CGI Pipe Verification

`make cgi-pipe-test` builds and runs a focused C++98 loopback suite. It verifies CGI request-body forwarding; CGI/1.1 method, URL script name, path info, query, server, protocol, content type, and custom-header context; and a fixture file opened relative to the script directory. It also covers delayed output, output larger than one 4096-byte collection read, a CGI response with no CGI-provided `Content-Length` (the HTTP serializer frames it), early CGI stdin closure with a controlled `502 Bad Gateway`, and listener survival afterward.

## Session Store Verification

`make session-store-test` builds a focused C++98 unit suite for the process-local session foundation. It covers cookie parsing and whitespace, missing/empty/duplicate cookie rejection, entropy-backed 256-bit identifier creation, `Set-Cookie` construction, request lookup, expiry, and periodic cleanup. The separate cookie-session suite covers its `/session` route integration.

`make cookie-session-test` builds and runs a C++98 loopback suite for the GET-only reference-mock `/session` route. It verifies the initial `Set-Cookie` response and state, a second request that resumes the same session without replacing its cookie, an invalid cookie that safely starts a clean session, and later normal static serving. This is a process-local bonus demonstration, not authentication or configuration-file support.

## Non-Blocking Rules

- `Implemented`: listening sockets, client sockets, and the parent ends of CGI pipes are set non-blocking.
- `Implemented`: one `Poller` drives socket and CGI pipe readiness.
- `Implemented`: socket and pipe read/write paths use their return values and do not inspect `errno` after `read`, `recv`, `write`, or `send`.
- `Implemented`: CGI body writes and output reads run only after pipe readiness. A stdout EOF is accepted only after the complete body was sent; CGI pipe error, hangup, or invalid-fd events enter the controlled failure/reap path and preserve a connected client for its `502` response.
- `To Fix`: broaden end-to-end network-load coverage and complete configuration-driven CGI selection.

## Subject-Critical Gaps

The project still needs the following mandatory behavior:

1. Real configuration parsing and all required server/location directives.
2. Hardened event-loop behavior for partial I/O, poll errors, disconnects, and no unexpected termination.
3. Configuration-file-driven CGI handler selection and CGI response-header validation.
4. Repeatable compliance tests.

The optional bonus work remaining is multiple CGI types.
