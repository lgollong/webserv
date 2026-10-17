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
| `make logger-test` | `none`/`error`/`debug` level matrix and debug-stream flushing. | Passing. |
| `make config-model-test` | Parsed `config/req.config` model and routing. | Passing. |
| `make config-parser-test` | Valid multi-server/multi-location configuration, parser defaults, and unreadable or malformed inputs. | Passing. |
| `make static-file-test` | Static files, streamed file descriptors, MIME types, index/autoindex, raw and multipart uploads, DELETE, filesystem status codes, lexical traversal, symlink confinement. | Passing. |
| `make session-store-test` | Cookie parsing, random identifiers, expiry. | Passing. |
| `make event-loop-stress-test` | Poll error handling, partial writes, and half-closed clients. | Passing. |
| `make cgi-lifecycle-test` | CGI process arguments, reaping, symlinked scripts, and CGI response parsing. | Passing. |
| `make connection-lifecycle-test` | `config/req.config` listeners, routing, methods (`405`/`501`), HTTP/1.0, errors, uploads, half-close, large-file streaming, persistence. | Passing. |
| `make cgi-pipe-test` | CGI HTTP integration through the `8002` Python and shell routes: bodies, chunked bodies, environment, descriptor isolation, malformed and oversized output. | Passing. |
| `make cookie-session-test` | `/session` HTTP integration through the `session_demo on;` route in `config/default.config`. | Passing. |
| `make resilience-test` | Client and CGI timeouts, disconnect cleanup, and listener rebinding after shutdown through the `8002` listener. | Passing. |

`make test` runs all fourteen targets and passes on macOS and on Linux with g++ and clang++.

## Browser and Curl Checks

Build and start the default configuration:

```sh
make && ./webserv config/default.config
```

Open `http://127.0.0.1:8080/`. The evaluation guide follows the evaluation sheet and runs each check from the browser, marking it green when the status code matches. Or inspect these routes with curl:

```sh
curl -i http://127.0.0.1:8080/
curl -i http://127.0.0.1:8080/files
curl -i http://127.0.0.1:8080/old-page
curl -i http://127.0.0.1:8080/cgi/test.sh
curl -i "http://127.0.0.1:8080/cgi/test.py?name=webserv"
curl -i -d "message=hi" http://127.0.0.1:8080/cgi/test.py
```

The first serves the guide, `/files` produces the configured autoindex listing, `/old-page` returns a `301` redirect to `/`, and the CGI requests run `contents/cgi/test.sh` through `/bin/bash` and `contents/cgi/test.py` through `/usr/bin/python3`, the only two handlers in the supplied configurations.

Upload, list, and delete files from a browser at `http://127.0.0.1:8080/upload.html`, or with curl:

```sh
curl -i -F "file=@README.md" http://127.0.0.1:8080/uploads          # 201, Location: /uploads/README.md
curl http://127.0.0.1:8080/uploads/README.md                        # the uploaded bytes
curl -i -X DELETE http://127.0.0.1:8080/uploads/README.md           # 204
```

CGI error handling: `/cgi/crash.py` and `/cgi/test.sh?malformed` return `502`, and `/cgi/loop.py` returns `502` after the 15-second CGI timeout while other requests keep being served.

To exercise the request-body limit, use the second website's 32-byte limit:

```sh
curl -i -X POST --data "short" http://127.0.0.1:8081/cgi/test.sh                                                    # 200
curl -i -X POST -H "Content-Type: plain/text" --data "BODY IS HERE write something shorter or longer than body limit" http://127.0.0.1:8081/cgi/test.sh   # 413
```

To inspect the multi-server fixture instead, run `./webserv config/req.config` and connect to ports `8002`, `8003`, `8008`, and `8001`.

## Configuration Checks Still Needed

Duplicate host/port pairs and invalid listener addresses are rejected when `Worker` creates the listeners at startup (`bind` fails, or `getaddrinfo` rejects the address), not by the parser, and have no automated test yet. Check it manually with `./webserv config/conflict.config`, which must exit with `bind: Address already in use`.

## Optional Comparison and Stress Checks

Use NGINX only as a behavioral comparison tool, not as a dependency. Compare status, content type, content length, redirects, and connection behavior for equivalent routes.

For bounded concurrent static requests against the default configuration:

```sh
for request in $(seq 1 50); do curl -fsS http://127.0.0.1:8080/files/index.html > /dev/null & done
wait
curl -i http://127.0.0.1:8080/files/index.html
```

This is a manual smoke test. For load, ApacheBench works with and without keep-alive:

```sh
ab -n 1000 -c 50 http://127.0.0.1:8080/
ab -k -n 1000 -c 50 http://127.0.0.1:8080/
```
