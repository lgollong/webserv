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
- Takes a stable snapshot of ready events before callbacks remove fds, and has one cleanup path for a client and its registered CGI pipe fds.
- Handles client and CGI `POLLERR`, `POLLHUP`, and `POLLNVAL` paths without throwing from the event loop.
- `To Fix`: listening is hard-coded to `0.0.0.0:8080`; it does not use the configured host/port pairs or create multiple listeners.
- `To Fix`: accept until the listener would block and broaden stress coverage for non-blocking edge cases.
- `To Fix`: malformed requests, request timeouts, keep-alive behavior, and safe per-request reset are incomplete.
- `To Fix`: CGI state is not fully coordinated with connection phases or child lifecycle.

### `Poller`

`Implemented`, but minimal.

- Wraps a `std::vector<pollfd>` and supports adding, removing, and changing fd interests.
- Calls the single process-wide `poll()` used by `Worker` and returns its readiness result.
- `Partial`: `Worker` logs a failed wait, handles invalid/error events for managed connection fds, and attempts to recreate a failed listener.

### `Http`

`Partial`.

- Strictly parses a buffered HTTP/1.1 request line into method, origin-form path, query, and version state.
- Parses headers and a `Content-Length` body with only baseline validation.
- Returns positive consumed bytes for a complete request, `0` for an incomplete request, and `-1` for a malformed request line.
- Builds HTTP/1.1 responses with a default content type and calculated `Content-Length`.
- `To Fix`: validate header syntax and limits, body framing/limits, and malformed requests with appropriate status responses.
- `Planned`: support chunked request bodies and unchunk them before CGI input.
- `To Fix`: expand status handling and response headers to cover the subject's required behavior.

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

`Partial`.

- Creates stdin/stdout pipes, forks, runs a configured script with `execve`, and sets the parent pipe ends non-blocking.
- Builds CGI environment variables from the request and parses CGI response headers and body.
- `Worker` registers the CGI pipes in the same `Poller` as sockets and transfers the request body/output through readiness callbacks.
- Reports pipe, fork, and non-blocking setup failures through `CgiJob`, and the child exits if `execve` fails.
- Treats failed pipe reads/writes as a controlled CGI failure without inspecting `errno` after I/O; the response path produces `502` when no valid CGI result exists.
- `To Fix`: reap children with non-blocking `waitpid` and complete the remaining CGI cleanup paths.
- `Planned`: enforce CGI timeouts and fully support request-body edge cases, including chunked input and CGI EOF behavior.

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
- `Connection` owns socket identity, input/output buffers, partial-write position, last activity, and its current transaction.
- `CgiJob` owns child pid, stdin/stdout fds, write progress, output buffer, completion state, and failure state.
- `Partial`: client and registered CGI fds now have an explicit shared cleanup path; `Phase`, `keep_alive`, `last_activity`, and child-process cleanup remain incomplete.

## Current Request Flow

### Static Request

1. `Worker` receives a readiness event for a client socket and reads bytes into `Connection::inbuf`.
2. `Http` parses a complete `Request` when enough bytes are buffered.
3. `Config` returns a route.
4. `StaticFile` creates `Content`, currently placeholder HTML.
5. `Http` serializes a `Response` into `Connection::outbuf`.
6. `Worker` changes the client interest to `POLLOUT` and writes until the buffer is complete.

This establishes the intended ownership flow, but static file serving, route resolution, errors, and connection handling remain incomplete.

### CGI Request

1. `Worker` identifies a CGI route from `Config`.
2. `Cgi` starts the child and returns its pipe fds in `CgiJob`.
3. `Worker` registers CGI stdin for `POLLOUT` and stdout for `POLLIN` in the same `Poller`.
4. `Cgi` sends the request body and collects CGI output through readiness callbacks.
5. `Cgi` converts the CGI response into `Response`, which `Http` serializes for the client.

The flow is wired end to end, but it needs the reliability work listed above before it meets the subject's non-blocking and no-hang requirements.

## Non-Blocking Rules

- `Implemented`: listening sockets, client sockets, and the parent ends of CGI pipes are set non-blocking.
- `Implemented`: one `Poller` drives socket and CGI pipe readiness.
- `Implemented`: socket and pipe read/write paths use their return values and do not inspect `errno` after `read`, `recv`, `write`, or `send`.
- `Partial`: client and CGI error/hangup events close their managed fds without throwing, while partial writes retain their cursor. Broader stress behavior still needs work.
- `Planned`: add timeout handling for slow or incomplete clients and CGI jobs.

## Subject-Critical Gaps

The project still needs the following mandatory behavior:

1. Real configuration parsing, server/location matching, all required directives, and multiple configured listeners.
2. Complete HTTP validation, request-body limits, chunked-body decoding, accurate errors, and method restrictions.
3. Static file serving from configured roots, index files, autoindex, uploads, `POST`, and `DELETE`.
4. Redirection and configured error pages.
5. Hardened event-loop behavior for partial I/O, poll errors, disconnects, timeouts, and no unexpected termination.
6. Hardened CGI execution, child reaping, and full request-body/EOF handling.
7. Repeatable compliance tests and a subject-compliant README.

The optional bonus work remains cookie/session support and multiple CGI types.
