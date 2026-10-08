*This project has been created as part of the 42 curriculum by lgollong, lorbke, sachmull.*

# webserv

## Description

`webserv` is a dependency-free HTTP server written in C++98 for the 42 curriculum. It runs one non-blocking, `poll()`-driven event loop which owns listener sockets, client sockets, and CGI pipes.

The implementation parses nginx-style `server` and `location` blocks from the configuration file passed on the command line. It currently supports static files, directory indexes and autoindex, uploads, `DELETE`, redirects, custom error pages, request-body limits, CGI handler mappings, persistent HTTP/1.1 connections, and focused loopback tests. The parser and several handlers still have known limitations; [Architecture](docs/architecture.md) records the current source-backed status.

## Instructions

### Prerequisites

- A C++ compiler that supports C++98.
- A POSIX-like environment with `poll()`, sockets, and standard build tools. The project is developed on macOS.

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

The executable requires one configuration-file argument. The default demonstration configuration listens on `0.0.0.0:8080`:

```sh
./webserv config/default.config
```

Open the dashboard at:

```sh
open http://127.0.0.1:8080/
```

`config/req.config` is a multi-server integration fixture. It listens on `127.0.0.1:8002`, `:8003`, `:8008`, and `:8001`; it is not the configuration for the dashboard command above. See [Runtime Configuration Model](docs/configuration-model.md) for supported directives and their current semantics.

### Tests

```sh
make test
```

The test target is sequential because several suites bind fixed loopback ports. **Current status:** parser model, CGI-pipe, and resilience coverage use parser-backed fixtures. The combined target still stops at `cookie-session-test`, because that optional session demonstration intentionally remains tied to the retired mock configuration. [Testing and Evaluation](docs/testing.md) records the affected target and useful passing focused tests.

### Cleanup

```sh
make clean
make fclean
make re
```

## Project Documentation

- [Project subject](docs/subject/en.subject.md)
- [Architecture and implementation status](docs/architecture.md)
- [Runtime configuration model](docs/configuration-model.md)
- [Code walkthrough](docs/code-walkthrough.md)
- [Testing and evaluation](docs/testing.md)
- [Contribution workflow for coding agents](AGENTS.md)

## Resources

- [RFC 9110: HTTP Semantics](https://www.rfc-editor.org/rfc/rfc9110)
- [RFC 9112: HTTP/1.1](https://www.rfc-editor.org/rfc/rfc9112)
- [RFC 3875: CGI Version 1.1](https://www.rfc-editor.org/rfc/rfc3875)
- [`poll()` specification](https://pubs.opengroup.org/onlinepubs/9699919799/functions/poll.html)
- [NGINX documentation](https://nginx.org/en/docs/)

### AI Use

AI assistance, including OpenAI Codex, has been used for project planning, issue refinement, code and test drafting, documentation drafting, and code-review support. Contributors review generated changes, run the repository's verification commands, and retain responsibility for the resulting implementation. AI is not a runtime or build dependency of `webserv`.
