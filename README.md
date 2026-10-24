*This project has been created as part of the 42 curriculum by lgollong, lorbke, sachmull.*

# webserv

## Description

`webserv` is a dependency-free HTTP server written in C++98 for the 42 curriculum. It runs one non-blocking, `poll()`-driven event loop which owns listener sockets, client sockets, and CGI pipes.

The implementation parses nginx-style `server` and `location` blocks from the configuration file passed on the command line. It currently supports static files, directory indexes and autoindex, uploads, `DELETE`, redirects, custom error pages, request-body limits, CGI handler mappings (Python and shell demos), HTTP/1.0 and HTTP/1.1 requests with persistent connections, chunked request bodies, and a cookie/session demonstration. It deliberately implements the subset of HTTP the subject asks for rather than the full RFCs.

## Instructions

### Prerequisites

- A C++ compiler that supports C++98.
- A POSIX-like environment with `poll()`, sockets, and standard build tools. The project is developed on macOS.
- Python 3 for `make test` and the CGI demos; `/bin/bash` for the shell CGI demo.

### Build

```sh
make
```

The normal build uses only `c++ -Wall -Wextra -Werror -std=c++98` and the project include path.

For an optional AddressSanitizer diagnostic build, rebuild with:

```sh
make SANITIZE=address re
```

`SANITIZE=address` instruments that build only; plain `make` remains the subject-compliant default.

### Run

The executable takes an optional configuration-file argument. Without one it loads `config/default.config`, relative to the working directory, so run it from the repository root. The default configuration serves the evaluation guide on `0.0.0.0:8080` and a second website on `127.0.0.1:8081`:

```sh
./webserv                         # same as ./webserv config/default.config
./webserv config/default.config
```

Logging is set by the top-level `log_level error|debug;` line in the configuration file (default `error`). Use `debug` to print a line for every connection, request, response, and CGI start.

Open the evaluation guide at:

```sh
open http://127.0.0.1:8080/
```

The guide follows the evaluation sheet section by section and runs each check (status codes, uploads, DELETE permissions, CGI errors and timeouts, redirects, sessions) from the browser. Terminal-only checks, such as the port conflict and siege, are listed as commands.

`config/conflict.config` puts two servers on the same interface and port; `./webserv config/conflict.config` must stop with `bind: Address already in use`. `config/req.config` is an additional multi-server example listening on `127.0.0.1:8002`, `:8003`, `:8008`, and `:8001`; it is not the configuration for the guide.

### Configuration

The file uses nginx-style blocks; `#` starts a comment. Supported directives:

| Scope | Directive | Example |
|---|---|---|
| top level | `log_level error\|debug` | `log_level debug;` |
| `server` | `listen <port>` | `listen 8080;` |
| | `host <IPv4>` | `host 127.0.0.1;` |
| | `server_name <name>` (passed to CGI) | `server_name localhost;` |
| | `root <dir>` (default for locations) | `root ./contents;` |
| | `client_max_body_size <bytes>[k\|m]` (default for locations) | `client_max_body_size 10M;` |
| | `error_page <code> <file>` | `error_page 404 ./contents/errors/404.html;` |
| `location <prefix>` | `root <dir>` | `root ./contents/files;` |
| | `index <file>` | `index index.html;` |
| | `autoindex on\|off` | `autoindex on;` |
| | `allowed_methods <methods...>` (default `GET`) | `allowed_methods GET POST DELETE;` |
| | `return [3xx] <target>` | `return 301 /;` |
| | `upload on\|off` + `upload_store <dir>` | `upload on; upload_store ./contents/uploads;` |
| | `client_max_body_size <bytes>[k\|m]` | `client_max_body_size 100;` |
| | `cgi <extension> <absolute interpreter path>` | `cgi .py /usr/bin/python3;` |
| | `cgi_require_script on\|off` (default `on`; `off` runs the handler for missing scripts) | `cgi_require_script off;` |
| | `session_demo on\|off` | `session_demo on;` |

The longest matching location prefix wins. A URL below a location maps into its root: with `location /files { root ./contents/files; }`, `/files/a/b.txt` serves `./contents/files/a/b.txt`.

### Tests

```sh
make test                          # or: python3 tests/webserv_test.py [--fast]
```

`tests/webserv_test.py` starts `./webserv config/default.config` and walks the evaluation sheet over real sockets: routes, error pages, body limits, uploads, DELETE permissions, CGI (including crashing and endless scripts), sessions, keep-alive, concurrent clients, and the port conflict. `--fast` skips the 15-second CGI timeout check.

### Cleanup

```sh
make clean
make fclean
make re
```

## Project Structure

| Path | Contents |
|---|---|
| `srcs/`, `headers/` | The server. `Worker` runs the `poll()` loop and owns every connection; `Http` parses requests and builds responses; `Config` parses the configuration and picks the location for a request; `StaticFile` serves files, autoindex, uploads, and `DELETE`; `Cgi` runs scripts through pipes; `Session` backs the cookie demo; `Poller` wraps `poll()`; `types.hpp` holds the shared structs; `utils` holds small string helpers. |
| `config/` | `default.config` (evaluation guide), `conflict.config`, `req.config`. |
| `contents/` | Served files: the evaluation guide, error pages, CGI demo scripts (`cgi/`), and the upload directory (`uploads/`). |
| `tests/webserv_test.py` | Integration test (see [Tests](#tests)). |
| [`DEV_DOC.md`](DEV_DOC.md) | Architecture: the nginx model it borrows from, class responsibilities, connection state, and the request flow. |

## Resources

- [RFC 9110: HTTP Semantics](https://www.rfc-editor.org/rfc/rfc9110)
- [RFC 9112: HTTP/1.1](https://www.rfc-editor.org/rfc/rfc9112)
- [RFC 3875: CGI Version 1.1](https://www.rfc-editor.org/rfc/rfc3875)
- [`poll()` specification](https://pubs.opengroup.org/onlinepubs/9699919799/functions/poll.html)
- [NGINX documentation](https://nginx.org/en/docs/)

### AI Use

AI assistance, including OpenAI Codex and Anthropic Claude, has been used for project planning, issue refinement, code and test drafting, refactoring and simplification of the codebase, documentation drafting, and code-review support. Contributors review generated changes, run the repository's verification commands, and retain responsibility for the resulting implementation. AI is not a runtime or build dependency of `webserv`.
