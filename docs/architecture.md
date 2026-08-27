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
- `To Fix`: listening is hard-coded to `0.0.0.0:8080`; it does not use the configured host/port pairs or create multiple listeners.
- `To Fix`: accept until the listener would block and broaden stress coverage for non-blocking edge cases.
- Resets one completed transaction before beginning a later request already buffered on the same client connection. HTTP/1.1 connections persist by default; a case-insensitive `Connection: close` token marks only that response for close-after-flush and adds one matching response header.
- `To Fix`: CGI route selection still comes from the mock configuration rather than parsed server/location directives.

### `Poller`

`Implemented`, but minimal.

- Wraps a `std::vector<pollfd>` and supports adding, removing, and changing fd interests.
- Calls the single process-wide `poll()` used by `Worker` with a caller-supplied timeout and returns its readiness result.
- `Partial`: `Worker` logs a failed wait, handles invalid/error events for managed connection fds, and attempts to recreate a failed listener.

### `Http`

`Partial`.

- Strictly parses a buffered HTTP/1.1 request line into method, origin-form path, query, and version state.
- Strictly parses CRLF-delimited request headers with token field names, lowercase canonical names, and trimmed optional whitespace.
- Rejects malformed header syntax, more than 100 fields, headers larger than 16 KiB, duplicate `Content-Length`, and unsupported or conflicting transfer codings. It accepts only a sole `Transfer-Encoding: chunked` field.
- Parses `Content-Length` as one or more decimal digits with checked `size_t` conversion; signs, whitespace within the value, trailing data, and overflow are rejected.
- Waits for the complete declared body, returns the exact consumed count before any pipelined bytes, and rejects body/request sizes that cannot be represented by the parser contract.
- Decodes chunked bodies before assigning `Request::body`, supports chunk extensions and syntax-checked trailers, and applies the body limit to decoded bytes. Chunk trailers are not merged into request headers.
- Exposes a body-limit overload for future configuration integration. The current two-argument parser uses a temporary 10,000,000-byte default, matching the first server limit in `config/req.config`, until #5 supplies `client_max_body_size`.
- Returns positive consumed bytes for a complete request, `0` for an incomplete request, and `-1` for a malformed or unsupported request. Its error-status overload reports `400` for malformed syntax/framing, `413` for a declared body over the limit, and `431` for header limits.
- Builds a complete HTTP/1.1 response envelope for supported status codes, falling back to `500` for an unsupported status.
- Owns case-insensitive `Content-Type` selection and calculated `Content-Length`, suppresses unsafe or conflicting caller-supplied framing headers, preserves valid extension headers, and omits bodies for `204` and `304`.
- `Http::defaultErrorResponse()` creates deterministic HTML bodies for known error statuses; unknown or non-error inputs fall back to `500`.
- `Worker` queues parser failures through the normal output path with a default HTML error body and closes that connection after its response flushes. It also substitutes this default for an otherwise-empty static or CGI error response.
- `To Fix`: have parsed server/location configuration supply `client_max_body_size` to the limit-aware parser, and provide configured custom error pages.
- `To Fix`: add handler-specific response headers and complete status/error-page behavior with the relevant handler work.

### `Config`

`Partial`.

- Defines `ServerConfig` and `Route` data structures and exposes `route(const Request&)`.
- The current constructor ignores the supplied config path and creates mock values.
- The current resolver returns a mock static route, except for `.sh` paths which are treated as CGI.
- `Planned`: parse the required configuration file syntax, validate it, resolve server and location blocks, and support all mandatory directives.

### `StaticFile`

`Partial`.

- Provides a MIME-type map and a `serve()` API used by `Worker`.
- The current implementation returns generated placeholder HTML rather than files from disk.
- `Planned`: securely resolve paths under configured roots, read static files, implement index files and autoindex, and support upload and `DELETE` behavior.

### `Cgi`

`Partial`, with the event-loop pipe lifecycle implemented.

- Creates stdin/stdout pipes, forks, runs a configured script with `execve`, and sets the parent pipe ends non-blocking.
- Builds CGI environment variables from the request and parses CGI response headers and body.
- `Worker` registers the CGI pipes in the same `Poller` as sockets and transfers the request body/output through readiness callbacks.
- Reports pipe, fork, and non-blocking setup failures through `CgiJob`, and the child exits if `execve` fails.
- Treats failed pipe reads/writes, invalid pipe readiness, and CGI EOF before the complete request body is accepted as controlled CGI failures without inspecting `errno` after I/O; the response path produces `502` when no valid CGI result exists.
- Records CGI start and progress timestamps. `Worker` limits an active CGI job to 15 seconds, closes its pipes, sends `SIGTERM`, escalates to `SIGKILL` after two seconds if needed, and reaps it with `waitpid(..., WNOHANG)` before emitting a `502` response.
- Reaps normal completed children before resetting their transaction. On client disconnect, it retains the terminated PID and termination time in a worker-owned pending-reap map, where the same two-second `SIGKILL` escalation and non-blocking reap continue after the transaction is gone.
- `To Fix`: choose CGI handlers from parsed configuration and harden CGI response-header validation.

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
- `Partial`: client and registered CGI fds now have an explicit shared cleanup path. One response owns its transaction until it flushes; later buffered request bytes are retained and dispatched only after that reset. HTTP/1.1 persistence/close policy is active; remaining CGI protocol edge cases are incomplete.

## Current Request Flow

### Static Request

1. `Worker` receives a readiness event for a client socket and reads bytes into `Connection::inbuf`.
2. `Http` parses a complete `Request` when enough bytes are buffered.
3. `Config` returns a route.
4. `StaticFile` creates `Content`, currently placeholder HTML.
5. `Http` serializes a `Response` into `Connection::outbuf`.
6. `Worker` changes the client interest to `POLLOUT` and writes until the buffer is complete.

If later request bytes were already buffered on that client socket, they remain in `Connection::inbuf` while this response owns the current `Transaction`. After the last response byte flushes, `Worker` resets that transaction and dispatches one complete buffered next request without waiting for another client write.

HTTP/1.1 persistence is active by default. If the normalized request `Connection` header contains a comma-delimited `close` token, `Worker` adds `Connection: close` to that response and closes that client only after the response flushes. Parser-error responses always use the same close-after-flush path. The worker applies this policy to static and CGI responses and does not let CGI-provided `Connection` headers override it.

This establishes the intended ownership flow, but static file serving and route resolution remain incomplete.

### Parser Failure

1. `Http` returns `-1` and an error status to `Worker`.
2. `Worker` drops the malformed input, creates the matching default HTML error `Response`, appends its serialized bytes, marks the connection to close after its output flushes, and waits for `POLLOUT`.
3. `Worker` closes only that client fd after the normal non-blocking write path completes.

Configured custom error-page bodies remain planned work; they will replace the default only when configuration selects one.

### CGI Request

1. `Worker` identifies a CGI route from `Config`.
2. `Cgi` starts the child and returns its pipe fds in `CgiJob`.
3. `Worker` closes CGI stdin immediately for an empty request body; otherwise it registers stdin for `POLLOUT`. It always registers CGI stdout for `POLLIN` in the same `Poller`.
4. `Cgi` sends the parsed body, already decoded when the request used chunked transfer coding, and collects CGI output through readiness callbacks. It closes stdin after the entire body is accepted. Stdout EOF before that point, plus pipe error or invalid-fd readiness, marks the job as failed instead of accepting a partial-body CGI response.
5. `Cgi` records its start time and `Worker` checks its 15-second lifetime during the periodic maintenance sweep. On normal completion, `Worker` reaps the child non-blockingly before converting the CGI output into a `Response`. On timeout or failure, it closes CGI pipes, sends `SIGTERM`, escalates to `SIGKILL` after two seconds if needed, reaps the child, then sends the default `502` response.

The pipe/body/EOF flow is wired and covered end to end. Configuration-driven handler selection and CGI response validation remain before full subject compliance.

## Resilience Verification

`make resilience-test` builds and runs a repository-local C++98 loopback integration suite against `./webserv`. It starts and reaps its own server process, keeps a silent client and an incomplete request connected until the 30-second client deadline, confirms normal requests still receive `200 OK`, exercises the test CGI fixture's controlled `?stall` mode until the 15-second CGI deadline produces `502 Bad Gateway`, and confirms the listener serves another request. It also disconnects an active stalled CGI client, verifies the recorded child PID disappears within the two-second termination grace plus polling allowance, and checks that the listener remains available. The suite is bounded and exits non-zero for any missed deadline, wrong status, early server exit, or unreaped CGI child.

## Connection Lifecycle Verification

`make connection-lifecycle-test` builds and runs a separate fast C++98 loopback suite. It starts and reaps its own server, verifies fragmented requests receive no early response, checks sequential default-persistent requests and a concurrent second client, and reads two responses from one buffered CGI-plus-static request sequence. It also verifies that static, CGI, and parser-error close cases return exactly one `Connection: close` header and EOF only after their complete response. The command exits non-zero for an incorrect response boundary, missing/early EOF, unavailable listener, or server failure.

## CGI Pipe Verification

`make cgi-pipe-test` builds and runs a focused C++98 loopback suite. It verifies CGI request-body forwarding, delayed output, output larger than one 4096-byte collection read, and a CGI response with no CGI-provided `Content-Length` (the HTTP serializer frames it). It also makes the fixture close stdin before accepting a 256 KiB request body, verifies the client receives the controlled `502 Bad Gateway` response, and then confirms the listener serves a later request.

## Non-Blocking Rules

- `Implemented`: listening sockets, client sockets, and the parent ends of CGI pipes are set non-blocking.
- `Implemented`: one `Poller` drives socket and CGI pipe readiness.
- `Implemented`: socket and pipe read/write paths use their return values and do not inspect `errno` after `read`, `recv`, `write`, or `send`.
- `Implemented`: CGI body writes and output reads run only after pipe readiness. A stdout EOF is accepted only after the complete body was sent; CGI pipe error, hangup, or invalid-fd events enter the controlled failure/reap path and preserve a connected client for its `502` response.
- `To Fix`: broaden non-blocking stress coverage and complete configuration-driven CGI selection.

## Subject-Critical Gaps

The project still needs the following mandatory behavior:

1. Real configuration parsing, server/location matching, all required directives, and multiple configured listeners.
2. Finish configuration-driven request-body limits, accurate errors, and method restrictions.
3. Static file serving from configured roots, index files, autoindex, uploads, `POST`, and `DELETE`.
4. Redirection and configured error pages.
5. Hardened event-loop behavior for partial I/O, poll errors, disconnects, and no unexpected termination.
6. Hardened CGI execution and full request-body/EOF handling.
7. Repeatable compliance tests.

The optional bonus work remains cookie/session support and multiple CGI types.
