*This project has been created as part of the 42 curriculum by sachmull.*

# webserv

## Description

`webserv` is a dependency-free HTTP server written in C++98 for the 42 curriculum. The project is being built around one non-blocking, `poll()`-driven event loop that owns listener sockets, client sockets, and CGI pipes.

The current implementation includes HTTP/1.1 request parsing and response serialization, persistent connection handling, readiness-driven CGI pipes, CGI timeout/reaping behavior, and focused loopback tests. It is not yet feature-complete for the subject: configuration parsing, configured listeners and routing, real static-file serving, uploads, `DELETE`, redirects, and configured error pages remain in progress. The source-accurate status is documented in [Architecture](docs/architecture.md).

## Instructions

### Prerequisites

- A C++ compiler that supports C++98.
- A POSIX-like environment with `poll()`, sockets, and standard build tools. The project is developed on macOS.

### Build

```sh
make
```

The normal build uses `c++ -Wall -Wextra -Werror -std=c++98` together with the repository's current development flags.

### Run

The executable currently requires one configuration-file argument:

```sh
./webserv config/req.config
```

At this stage, the `Config` implementation still uses mock routing data and the worker listens on port `8080`; the supplied configuration files document the target configuration format but are not parsed yet. A basic development request can be made with:

```sh
curl -i http://127.0.0.1:8080/
```

### Tests

```sh
make connection-lifecycle-test
make cgi-pipe-test
make resilience-test
```

The resilience test includes real client-idle and CGI-timeout windows, so it takes longer than the other two targets. See [Architecture](docs/architecture.md) for each suite's coverage.

### Cleanup

```sh
make clean
make fclean
make re
```

## Project Documentation

- [Project subject](docs/subject/en.subject.md)
- [Architecture and implementation status](docs/architecture.md)
- [Code walkthrough](docs/code-walkthrough.md)
- [Contribution workflow for coding agents](AGENTS.md)

## Resources

- [RFC 9110: HTTP Semantics](https://www.rfc-editor.org/rfc/rfc9110)
- [RFC 9112: HTTP/1.1](https://www.rfc-editor.org/rfc/rfc9112)
- [RFC 3875: CGI Version 1.1](https://www.rfc-editor.org/rfc/rfc3875)
- [`poll()` specification](https://pubs.opengroup.org/onlinepubs/9699919799/functions/poll.html)
- [NGINX documentation](https://nginx.org/en/docs/)

### AI Use

AI assistance, including OpenAI Codex, has been used for project planning, issue refinement, code and test drafting, documentation drafting, and code-review support. Contributors review generated changes, run the repository's verification commands, and retain responsibility for the resulting implementation. AI is not a runtime or build dependency of `webserv`.
