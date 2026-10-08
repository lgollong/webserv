# Testing and Evaluation

## Automated Tests

Run the combined target with:

```sh
make test
```

Targets run sequentially because integration suites bind fixed loopback ports.

| Target | Coverage | Current status |
| --- | --- | --- |
| `make http-request-line-test` | Request line, headers, framing, fixed-length and chunked bodies. | Passing. |
| `make http-response-test` | Status lines, response headers, content length, empty responses. | Passing. |
| `make connection-timeout-test` | Timeout predicates. | Passing. |
| `make config-model-test` | Parsed `config/req.config` model and routing. | Passing. |
| `make config-parser-test` | Valid multi-server/multi-location configuration, parser defaults, and unreadable or malformed inputs. | Passing. |
| `make static-file-test` | Static files, MIME types, index/autoindex, uploads, DELETE, lexical traversal. | Passing. |
| `make session-store-test` | Cookie parsing, random identifiers, expiry. | Passing. |
| `make event-loop-stress-test` | Poll error handling and partial writes. | Passing. |
| `make cgi-lifecycle-test` | CGI process arguments and reaping. | Passing. |
| `make connection-lifecycle-test` | `config/req.config` listeners, routing, methods, errors, uploads, persistence. | Passing. |
| `make cgi-pipe-test` | CGI HTTP integration. | Stale: launches `config/req.config` but expects port-8080 mock routes. |
| `make cookie-session-test` | `/session` HTTP integration. | Stale: no parser directive/configured `/session` route exists. |
| `make resilience-test` | Client and CGI timeout integration. | Stale: launches `config/req.config` but expects the retired port-8080 fixture. |

As a result, `make test` is currently expected to stop at `cgi-pipe-test`, after the parser, static-file, event-loop, CGI-lifecycle, and connection-lifecycle targets pass. The passing focused targets are useful regression checks but are not a substitute for restoring the complete suite.

## Browser and Curl Checks

Build and start the default demonstration configuration:

```sh
make && ./webserv config/default.config
```

Then inspect these parser-backed routes:

```sh
curl -i http://127.0.0.1:8080/
curl -i http://127.0.0.1:8080/files
curl -i http://127.0.0.1:8080/old-page
curl -i http://127.0.0.1:8080/cgi/test.sh
```

The first serves the dashboard, `/files` produces the configured autoindex listing, `/old-page` returns a `301` redirect to `/`, and the CGI request requires the configured handler to exist on the machine.

To exercise the configured request-body limit, send a request above 10 MiB to a route that accepts POST and confirm `413 Payload Too Large`. To inspect the multi-server fixture instead, run `./webserv config/req.config` and connect to ports `8002`, `8003`, `8008`, and `8001`.

## Configuration Checks Still Needed

The parser should gain automated cases for:

- invalid body-size values, including signed values such as `-1` (tracked in issue #54), and malformed block forms not covered by the focused parser target;
- every supported directive, including the intended behavior of `upload off`;
- server-root inheritance, duplicate host/port rejection, and invalid listener addresses;
- the parser-backed CGI, session, and resilience integration fixture.

## Optional Comparison and Stress Checks

Use NGINX only as a behavioral comparison tool, not as a dependency. Compare status, content type, content length, redirects, and connection behavior for equivalent routes.

For bounded concurrent static requests against the default configuration:

```sh
for request in $(seq 1 50); do curl -fsS http://127.0.0.1:8080/files/index.html > /dev/null & done
wait
curl -i http://127.0.0.1:8080/files/index.html
```

This is a manual smoke test, not a replacement for load testing or the missing end-to-end parser coverage.
