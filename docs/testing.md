# Testing and Evaluation

## Automated Suite

Run the full repository suite with:

```sh
make test
```

The target runs each suite sequentially. This matters because several loopback suites bind ports `8080` and `8081`. It includes the slow resilience suite, which waits for the real client-idle and CGI deadlines.

| Target | Coverage |
| --- | --- |
| `make http-request-line-test` | Request-line, headers, body framing, chunked decoding, and parser error statuses. |
| `make http-response-test` | Status lines, framing, safe response headers, redirects, and default error responses. |
| `make connection-timeout-test` | Pure connection timeout-state boundaries. |
| `make config-model-test` | Reference runtime-model routes, methods, listeners, CGI selection, redirects, uploads, errors, and sessions. It does not parse a configuration file. |
| `make static-file-test` | Static files, MIME types, traversal rejection, indexes, autoindex, uploads, deletes, and error-page reads. |
| `make session-store-test` | Cookie parsing, session creation, expiry, and cleanup. |
| `make event-loop-stress-test` | `poll()` error handling and partial non-blocking writes. |
| `make cgi-lifecycle-test` | CGI startup, handler/direct-executable argument construction, and termination. |
| `make connection-lifecycle-test` | Listeners, persistence, methods, autoindex, redirects, uploads, errors, and CGI/static sequencing. |
| `make cgi-pipe-test` | CGI request context, chunked-body forwarding, two CGI types, pipe failures, and response framing. |
| `make cookie-session-test` | Cookie-backed reference-mock session route over HTTP. |
| `make resilience-test` | Client-idle timeout, CGI timeout/reaping, disconnect cleanup, and listener survival. |

The suite uses the subject-compliant default build. For local memory diagnostics, rebuild before running tests:

```sh
make SANITIZE=address re
make test
```

Return to the normal build afterward with `make re`.

## Current Configuration Boundary

The runtime reference model is intentionally covered by `make config-model-test`. Real configuration-file parsing and malformed-configuration coverage are not yet implemented; they remain owned by #4, #5, and #6 and are excluded from this compliance suite until the parser replaces the mock.

## Manual Checks

Start the server in one terminal:

```sh
./webserv config/req.config
```

The current reference model listens on ports `8080` and `8081` and does not read configuration-file contents yet.

### Browser and Curl

Open `http://127.0.0.1:8080/files/index.html` in a browser, then check a static response and an autoindex route:

```sh
curl -i http://127.0.0.1:8080/files/index.html
curl -i http://127.0.0.1:8080/gallery
```

Both should return `200`; the latter has an HTML directory listing. Check a redirect and CGI response:

```sh
curl -i http://127.0.0.1:8080/redirect
curl -i http://127.0.0.1:8080/cgi/test.sh
curl -i http://127.0.0.1:8080/cgi/test.cgi
```

The redirect returns `302` with `Location: /gallery`. Both CGI paths return `200`; `.sh` uses `/bin/sh`, while `.cgi` is directly executable.

### Telnet

Connect with `telnet 127.0.0.1 8080`, then enter the following request followed by an empty line:

```text
GET /files/index.html HTTP/1.1
Host: localhost

```

Confirm a complete `200 OK` response with `Content-Length`. Repeat with a malformed request line to confirm a controlled `400` response and connection close.

### Optional NGINX Comparison

Configure a local NGINX server with an equivalent document root and route behavior on another port, then compare a static response:

```sh
curl -si http://127.0.0.1:8080/files/index.html > /tmp/webserv-response.txt
curl -si http://127.0.0.1:8082/files/index.html > /tmp/nginx-response.txt
diff -u /tmp/nginx-response.txt /tmp/webserv-response.txt
```

Compare status, content type, length, redirect headers, and connection behavior. Header ordering and server-identification headers may differ.

### Bounded Stress Check

With the server running, send concurrent static requests and then confirm it remains responsive:

```sh
for request in $(seq 1 50); do curl -fsS http://127.0.0.1:8080/files/index.html > /dev/null & done
wait
curl -i http://127.0.0.1:8080/files/index.html
```

For automated timeout and cleanup stress coverage, use `make resilience-test`.
