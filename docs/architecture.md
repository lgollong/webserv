# Architecture

This document describes the target architecture for `webserv`. The project subject in
`docs/subject/en.subject.md` is the source of truth for required behavior.

Status markers:

- `Implemented`: present in the current codebase.
- `Planned`: part of the intended architecture, not implemented yet.
- `To Fix`: implemented differently from the target architecture and should be corrected.

## Model

The server borrows the useful parts of nginx's architecture while staying small enough for the
42 subject:

- single process;
- one event loop;
- one `poll()` or equivalent readiness mechanism;
- declarative `server` and `location` configuration;
- separate connection state and request/response state;
- static content handled directly;
- CGI handled by `fork` and `execve`.

Unlike nginx, this project does not need a master/worker process pool, FastCGI upstreams, runtime
modules, gzip filters, or disk I/O offloading.

## Hard Non-Blocking Rules

- `Implemented`: sockets are intended to be managed through a `poll()`-based loop.
- `Planned`: CGI pipes must also be registered in the same event loop.
- `To Fix`: socket reads/writes must only happen after readiness from the event loop.
- `To Fix`: do not inspect `errno` after `read`, `recv`, `write`, or `send`.
- `Planned`: every connection needs a timeout so requests cannot hang indefinitely.
- `Planned`: CGI children must be reaped without blocking, using `waitpid(..., WNOHANG)`.
- `Implemented`: regular disk files may be read normally; the subject exempts them from readiness polling.

## Target Components

### `Worker`

`Planned`

The `Worker` owns the event loop and request flow. It is the only component that should orchestrate
other services.

Responsibilities:

- own all active connections;
- own the `Poller`;
- accept new clients;
- read client data after readiness;
- parse requests;
- resolve routes;
- dispatch to static or CGI handling;
- queue responses;
- write responses after readiness;
- reset connections for keep-alive or close them;
- run timeout cleanup.

Current equivalent:

- `ManageServer` currently owns the event loop and socket setup.
- `To Fix`: align `ManageServer` with the `Worker` role or rename/refactor it into `Worker`.

### `Poller`

`Planned`

Thin wrapper around `poll()`.

Responsibilities:

- store `pollfd` entries;
- add/remove descriptors;
- update read/write interest;
- expose readiness results to `Worker`.

Current equivalent:

- `ManageServer` currently stores `std::vector<pollfd>` directly.
- `To Fix`: extract this into a dedicated `Poller` so event-loop bookkeeping is isolated.

### `HTTP`

`Planned`

Responsible for byte stream to HTTP objects and HTTP objects to response bytes.

Responsibilities:

- parse request line;
- parse headers;
- parse query string;
- detect incomplete requests;
- reject malformed requests;
- handle `Content-Length`;
- support body parsing for `POST`;
- build HTTP responses;
- render status lines and default error responses.

Current equivalent:

- `HTTPRequest` parses part of the request.
- `HTTPResponse` builds and sends responses.
- `To Fix`: response building and socket sending should be separated so partial writes remain event-loop driven.
- `To Fix`: request parsing needs incomplete/malformed/body-aware behavior.

### `Config`

`Planned`

Responsible for startup config parsing and per-request route resolution.

Responsibilities:

- parse all `server` blocks;
- parse all `location` blocks;
- store listen host/port pairs;
- store server roots, indexes, max body sizes, and error pages;
- store route-specific methods, redirects, roots, autoindex, indexes, upload settings, and CGI settings;
- resolve a request to the correct server and location.

Current equivalent:

- `ConfigFile`, `ConfigParser`, `Server`, and `Location` implement parts of this.
- `Implemented`: server and location objects exist.
- `Implemented`: several config directives are parsed.
- `Planned`: route resolution for requests still needs to be completed.

### `StaticFile`

`Planned`

Responsible for static content and upload/delete behavior.

Responsibilities:

- map URL paths through the resolved route root;
- prevent invalid path traversal;
- read static files;
- determine MIME type;
- serve directory indexes;
- generate autoindex pages when enabled;
- enforce allowed methods;
- handle file uploads to configured storage;
- implement `DELETE`;
- return accurate errors.

Current equivalent:

- Static file behavior is currently inside `HTTPResponse`.
- `To Fix`: move static file concerns out of `HTTPResponse` into a dedicated service.
- `To Fix`: remove hardcoded absolute paths and resolve through config.

### `CGI`

`Planned`

Responsible for running CGI scripts and collecting their output without blocking the server.

Responsibilities:

- detect CGI routes by configured extension;
- create stdin/stdout pipes;
- set pipe descriptors non-blocking;
- `fork`;
- set CGI environment variables;
- `execve` the configured interpreter/script;
- send request body to CGI stdin;
- collect CGI stdout through the event loop;
- parse CGI response headers such as `Status` and `Content-Type`;
- handle EOF as the end of CGI output when no `Content-Length` is present.

Current equivalent:

- No dedicated CGI service exists yet.
- `Planned`: implement CGI as its own service and register CGI pipe fds in the same `poll()` loop.

### `Logger`

`Planned`

Responsible for access and diagnostic logs.

Responsibilities:

- log completed requests;
- log errors and diagnostics;
- avoid mixing logging logic into request handling.

Current equivalent:

- Logging is currently mostly direct `std::cout` / `std::cerr`.
- `To Fix`: centralize logging behind a small logger service.

## Target State Objects

### `Connection`

`Planned`

Per socket. Survives across keep-alive requests.

```cpp
struct Connection {
    int          fd;
    Phase        phase;
    std::string  inbuf;
    std::string  outbuf;
    size_t       sent;
    bool         keep_alive;
    time_t       last_activity;
    Transaction  txn;
};
```

Responsibilities:

- keep unconsumed input bytes;
- keep queued output bytes;
- track partial writes;
- track keep-alive state;
- track timeout data;
- own the current request transaction.

Current equivalent:

- `Client` stores socket/address/id only.
- `To Fix`: extend connection state beyond socket identity.

### `Transaction`

`Planned`

Per request/response cycle. Reset after each completed request.

```cpp
struct Transaction {
    Request   request;
    Response  response;
    Route     route;
    CgiJob    cgi;
    bool      headers_done;
    size_t    content_length;
    int       status;
};
```

Responsibilities:

- own the parsed request;
- own the response being assembled;
- store resolved route information;
- store CGI state when applicable;
- track parsing progress and working status.

Current equivalent:

- Request and response state currently live as short-lived `HTTPRequest` and `HTTPResponse` objects.
- `To Fix`: persist transaction state across partial reads, CGI waits, and partial writes.

## Target Request Flow

### Static Request

`Planned`

1. `Poller` reports a client fd readable.
2. `Worker` reads available bytes into `Connection::inbuf`.
3. `HTTP` parses a complete request or reports incomplete/malformed input.
4. `Config` resolves the matching server/location.
5. `StaticFile` serves content or returns an error status.
6. `HTTP` builds response bytes into `Connection::outbuf`.
7. `Poller` enables write interest for that client fd.
8. `Worker` writes queued bytes after write readiness.
9. `Worker` resets the transaction for keep-alive or closes the connection.
10. `Logger` records the completed request.

Current state:

- `Implemented`: listening sockets and clients are polled.
- `Implemented`: basic request receive path exists.
- `Implemented`: simple response send path exists.
- `To Fix`: request parsing, route resolution, static serving, partial writes, keep-alive, and logging need target behavior.

### CGI Request

`Planned`

1. Static request flow runs through route resolution.
2. `Config` marks the route as CGI based on extension/location.
3. `CGI` creates pipes and forks the script.
4. CGI stdin/stdout fds are registered with `Poller`.
5. Request body is written to CGI stdin through write readiness.
6. CGI stdout is collected through read readiness.
7. Child exit is reaped without blocking.
8. CGI headers/body are translated into a `Response`.
9. Response bytes are queued and written through the normal client write path.

Current state:

- `Planned`: CGI execution is not implemented yet.

## Ownership Rules

`Planned`

- `Worker` owns connections by value.
- Each `Connection` owns its current `Transaction`.
- Services receive references and do not own connections.
- Config objects are read-mostly after startup.
- File descriptors are closed by the owner that registered and tracks them.

Current state:

- `ManageServer` owns server/client vectors and poll fds.
- `To Fix`: make fd ownership and connection lifetime explicit enough to support partial writes, CGI pipes, and timeouts.

## Implementation Priorities

1. `To Fix`: define persistent `Connection` and `Transaction` state.
2. `To Fix`: separate response building from socket sending.
3. `To Fix`: make all socket writes partial-write safe.
4. `Planned`: implement robust HTTP parsing.
5. `Planned`: implement request routing from parsed config.
6. `Planned`: move static file behavior into `StaticFile`.
7. `Planned`: implement upload and `DELETE`.
8. `Planned`: implement CGI with non-blocking pipes.
9. `Planned`: add timeouts and access/error logging.

Every implementation step must be checked against `docs/subject/en.subject.md`.
