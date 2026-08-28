# DEV_DOC

A small HTTP/1.1 web server that serves static files and runs CGI scripts, driven by an
nginx-style configuration file. It is modeled on nginx's architecture but deliberately keeps
only the parts that matter at single-machine scale.

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
| **Worker** | Runs the loop; owns all connections; dispatches on phase; orchestrates. | `map<int, Connection>` | `start`, `on_readable`, `on_writable`, `accept_new`, `reset_for_keepalive` |
| **Poller** | Thin `poll()` wrapper; maintains the fd set and per-fd interest. | `vector<pollfd>` | `add`, `remove`, `set_interest`, `poll` |
| **HTTP** | Byte stream ↔ structured message, both directions; renders status codes (incl. errors). | `Request`, `Response` | `parse`, `build` |
| **Config** | Parses the config file (startup); resolves a request to its location (per request). | `ServerConfig`, `Route` | `route` |
| **CGI** | Forks/execs a script, wires up its I/O, collects output non-blockingly. | `CgiJob` | `start`, `collect` |
| **StaticFile** | Reads a file from disk, resolves its MIME type, autoindex/upload. | `Content` | `serve` |
| **Logger** | Access log (one line per completed request) and error/diagnostic log. | — | `access`, `error` |

### State objects (the nesting)

Mirrors nginx's `connection → request` nesting. Split rule: **survives keep-alive → `Connection`;
belongs to one request → `Transaction`.**

```cpp
struct Connection {           // per socket; outlives individual requests
    int          fd;
    Phase        phase;       // READING, RUNNING_CGI, WRITING, IDLE
    std::string  inbuf;       // unconsumed bytes (may straddle keep-alive requests)
    std::string  outbuf;
    size_t       sent;        // partial-write cursor
    bool         keep_alive;
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

Keep-alive reset is one line: `conn.txn = Transaction{};`.

### Ownership

- **Instances:** the Worker owns every `Connection` **by value** in `connections` (keyed by fd).
  Each `Connection` owns its `Transaction` **by value** (nested member). Create on accept
  (`emplace`), destroy on close (`erase`) — RAII cascades cleanup. Services receive pieces of a
  connection **by reference** and never take ownership. (`std::map` keeps element references
  stable across other inserts/erases — don't keep a reference to a connection you just erased.)
- **Types:** not global. Each data struct is declared in a header grouped with its concern
  (`Request`/`Response` in `Http.hpp`, `Route` in `Config.hpp`, …). Optionally, the pure data
  structs live in a dependency-free `types.hpp` as shared vocabulary, so classes own only their
  *functions* and private state.

---

## 5. Example request flow

The Worker, woken by the Poller, walks each connection through
`read → parse → route → (serve | run CGI) → build → write`, calling one service per step and
holding the result in the connection's `Transaction`. It pauses back to the loop whenever the
next step would block.

**Static — `GET /index.html`**

1. `Poller.poll()` → client fd readable.
2. `Connection.read(fd)` → raw bytes appended to `conn.inbuf`.
3. `HTTP.parse(inbuf, txn.request)` → `INCOMPLETE` (return to loop) or a filled `Request`.
4. `Config.route(txn.request)` → `Route` (root, `is_cgi=false`, allowed methods).
5. `StaticFile.serve(route, request)` → `Content{status, body, mime_type}` (or 404).
6. `HTTP.build(txn.response)` → response bytes into `conn.outbuf`; `phase = WRITING`, set POLLOUT.
7. On writable: `Connection.write(fd, outbuf, sent)` until fully sent → keep-alive reset or close.
8. `Logger.access(conn)` once, at completion.

**Dynamic — `POST /login.php`** (differs at the content step)

4. `Config.route` → `Route` with `is_cgi=true`, optional `cgi_handler`, URL `cgi_script_name`, and route-root `cgi_script_path` set.
5. `CGI.start(request, route)` → `CgiJob{pid, out_fd}`; **register `out_fd` in the Poller**,
   `phase = RUNNING_CGI`, return to loop (no blocking `waitpid`).
6. On `out_fd` events: `CGI.collect(txn.cgi)` accumulates output; on child exit, parse the
   script's `Status:` / `Content-Type:` header block into `txn.response`.
7. `HTTP.build` → bytes; then `WRITING` and `write` as above.

**Errors ride the same path:** any service returns a non-OK *status* (400/403/404/502);
the Worker feeds it to `HTTP.build`, which emits the configured `error_page` or a default page,
and the connection proceeds through the normal `WRITING` phase. One exit for success and error alike.
