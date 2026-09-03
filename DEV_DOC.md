# DEV_DOC

A small HTTP/1.1 web server that serves static files and runs CGI scripts, driven by a parsed
nginx-style configuration file. It is modeled on nginx's architecture but deliberately keeps
only the parts that matter at single-machine scale. The code remains a work in progress; the
current limitations are recorded in `docs/architecture.md`.

---

## 1. How nginx works (the model we borrow from)

- **Process model:** one *master* process reads config and binds sockets; a pool of *worker*
  processes (one per core) handles all requests.
- **Concurrency:** each worker is a single-threaded, non-blocking **event loop** over
  `epoll`/`kqueue`, multiplexing thousands of connections. Each connection is a state machine
  advanced by readiness events, never a blocking thread.
- **Request lifecycle:** a request runs an ordered **phase pipeline** — pick server/location →
  rewrite → access/auth → content → log — then the response streams through an **output filter
  chain** (gzip, chunked, …).
- **Content:** static files are served directly from disk; dynamic requests are proxied over
  **FastCGI** to a separate, persistent process pool (e.g. php-fpm). nginx itself never forks
  per request.
- **Routing is declarative:** behaviour lives in `http → server → location` config blocks;
  location matching resolves which block (and thus which handler) applies.

---

## 2. Simplifications this project makes (and why)

| nginx | This project | Why it's safe to cut |
|---|---|---|
| Master + N worker processes | **Single process, one loop** | One machine, no multi-core scaling target. |
| `epoll` / `kqueue` | **`poll()`** | Subject allows "or equivalent"; simpler, fine at low connection counts. |
| Non-blocking all the way down (async upstreams, disk thread pool) | Non-blocking on sockets/pipes only; **disk reads are blocking** | Regular files are exempt from the readiness rule; keeps static serving as plain code. |
| FastCGI to a persistent upstream pool | **Classic CGI**: `fork` + `execve` per request | Subject requires running CGI scripts; a process pool is out of scope. |
| Output filter chain (gzip, SSI, …) | Build the response in one step | None of those transforms are required. |
| Pluggable modules registered into hook phases | **Compiled-in layers**; variation via config + a small dispatch | No runtime extensibility requirement. |
| One fused `request` struct carrying response too | **Separate `Request` / `Response`** | Clearer; we don't have the filter-context threading that motivates fusion. |

**What we keep from nginx:** the single-threaded readiness reactor, declarative
`server`/`location` config, the connection-vs-request state split, and the static-vs-dynamic
content branch.

---

## 3. Non-blocking discipline (hard rules)

- One `poll()` for **all** socket I/O, listening sockets included.
- Never `read`/`write` a socket or pipe without a prior readiness event from `poll()`.
- `poll()` watches read **and** write interest; POLLOUT is set only when output is queued.
- React on **return values only** — never inspect `errno` after a read/write.
- Regular disk files are exempt (may be read/written blocking).
- `fork` is used for **CGI only**; never block on `waitpid` inside the loop (poll the child /
  reap with `WNOHANG`).
- Every connection has a timeout so no request hangs indefinitely.

---

## 4. Architecture

The **Worker** owns the loop and is the only class that knows the request *flow*. Every other
class is a passive service it calls; services never call each other or back up into the Worker.
Dependency arrows point one way: `Worker → services`.

| Class | Responsibility | Owns (structs) | Key functions |
|---|---|---|---|
| **Worker** | Runs the loop; owns all connections; dispatches on phase; orchestrates. | `map<int, Connection>`, `SessionStore` | `run`, `acceptNew`, `onReadable`, `onWritable` |
| **Poller** | Thin `poll()` wrapper; maintains the fd set and per-fd interest. | `vector<pollfd>` | `add`, `remove`, `setEvents`, `poll` |
| **Http** | Byte stream ↔ structured message, both directions; renders status codes (incl. errors). | `Request`, `Response` | `parse`, `build` |
| **Config** | Parses the startup file and resolves a request to its listener-selected location. | `ServerConfig`, `Route` | `servers`, `route`, `bodyLimit`, `errorPage` |
| **Cgi** | Forks/execs a script, wires up its I/O, collects output non-blockingly. | `CgiJob` | `start`, `sendBody`, `collect`, `reap` |
| **StaticFile** | Reads a file from disk, resolves its MIME type, autoindex/upload/delete. | `Content` | `serve`, `upload`, `erase` |
| **Logger** | Error and diagnostic logging. Access logging is currently a stub. | — | `debug`, `error` |

### State objects (the nesting)

Mirrors nginx's `connection → request` nesting. Split rule: **survives keep-alive → `Connection`;
belongs to one request → `Transaction`.**

```cpp
struct Connection {           // per socket; outlives individual requests
    int          fd;
    size_t       server_index;
    Phase        phase;       // READING, RUNNING_CGI, WRITING, IDLE
    std::string  inbuf;       // unconsumed bytes (may straddle keep-alive requests)
    std::string  outbuf;
    size_t       sent;        // partial-write cursor
    bool         keep_alive;
    bool         close_after_write;
    time_t       last_activity;
    Transaction  txn;         // current request cycle; reset per request
};

struct Transaction {          // one request → response cycle
    Request   request;        // parsed inbound message
    Response  response;       // outbound message being assembled
    Route     route;          // resolved location
    CgiJob    cgi;            // meaningful only in RUNNING_CGI
    bool      headers_done;   // parse progress
    size_t    content_length;
    int       status;         // working status code
};
```

Keep-alive reset is one line: `conn.txn = Transaction();`.

### Ownership

- **Instances:** the Worker owns every `Connection` **by value** in `connections` (keyed by fd).
  Each `Connection` owns its `Transaction` **by value** (nested member). Create on accept with
  `connections[fd] = connection`, destroy on close with `erase`; services receive pieces of a
  connection **by reference** and never take ownership. (`std::map` keeps element references
  stable across other inserts/erases — don't keep a reference to a connection you just erased.)
- **Types:** the shared data structs live in dependency-free `types.hpp`; classes own behavior
  and private state rather than the public data vocabulary.

---

## 5. Example request flow

The Worker, woken by the Poller, walks each connection through
`read → parse → route → (serve | run CGI) → build → write`, calling one service per step and
holding the result in the connection's `Transaction`. It pauses back to the loop whenever the
next step would block.

**Static — `GET /index.html`**

1. `Poller.poll()` → client fd readable.
2. `Worker::onReadable()` → raw bytes appended to `conn.inbuf`.
3. `Http::parse(inbuf, txn.request, Config::bodyLimit(server_index))` → incomplete, malformed, or a filled `Request`.
4. `Config::route(server_index, txn.request)` → `Route` (root, optional CGI state, allowed methods).
5. `StaticFile::serve(route, request)` → `Content{status, body, mime_type}` (or an error status).
6. `Http::build(txn.response)` → response bytes into `conn.outbuf`; `phase = WRITING`, set `POLLOUT`.
7. On writable: `Worker::onWritable()` advances `sent` until fully sent, then resets for keep-alive or closes.

**Dynamic — `POST /login.php`** (differs at the content step)

4. `Config::route` → `Route` with `is_cgi=true`, optional `cgi_handler`, URL `cgi_script_name`, and route-root `cgi_script_path` set.
5. `Cgi::start(request, route, server)` → `CgiJob{pid, in_fd, out_fd}`; register the pipe fds in the Poller,
   set `phase = RUNNING_CGI`, and return to the loop without blocking in `waitpid`.
6. On pipe events, `Cgi::sendBody()` forwards POST data and `Cgi::collect()` accumulates output; on child exit, parse the
   script's `Status:` / `Content-Type:` header block into `txn.response`.
7. `Http::build` → bytes; then `WRITING` and partial writes as above.

**Errors ride the same path:** handlers return a non-OK status (400/403/404/502); `Worker` first
looks up the selected server's configured error page when the response body is empty, then passes
the result through `Http::build` and the normal `WRITING` phase.
