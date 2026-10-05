# Runtime Configuration Model

This document defines the normalized runtime configuration contract used by `Worker` and future request handlers. It is intentionally independent of configuration-file syntax: #4 will parse text and produce these same values, so handler code does not need to be rewritten when parsing replaces the reference mock.

## `ServerConfig`

Each `ServerConfig` represents one listener and its server-level defaults.

| Field | Runtime meaning |
| --- | --- |
| `host`, `port` | Interface and port bound by one worker listener. |
| `server_name` | Server identity used by future request selection. |
| `root` | Default document root. |
| `client_max_body_size` | Maximum decoded request-body size. |
| `error_pages` | HTTP status to configured error-page file path mapping. `Worker` selects from the accepted connection's server; an unavailable file falls back to the default error body. |
| `locations` | Normalized `Route` records for this server. |

## `Route`

Each `Route` is normalized before a handler receives it. A parser may apply server defaults while constructing a route, but consumers use only the resolved fields below.

| Field | Runtime meaning |
| --- | --- |
| `location` | URL-prefix match used for route selection. |
| `root` | Resolved document root for the route. |
| `allowed_methods` | Configured accepted HTTP methods. |
| `redirect_status`, `redirect_target` | Redirect response, or status `0` when no redirect is configured. |
| `autoindex`, `index_file` | Directory-serving behavior. |
| `upload_store` | Empty when uploads are disabled; otherwise the configured storage directory. |
| `session_demo` | Reference-mock-only flag selecting the optional GET `/session` demonstration. It is not a configuration-file directive. |
| `cgi_handlers` | File extension to configured CGI handler/interpreter mapping. An empty mapped value means the matching script is directly executable. |
| `is_cgi`, `cgi_handler`, `cgi_script_name`, `cgi_script_path` | Per-request CGI result derived from `cgi_handlers`: optional handler/interpreter, URL script portion used for CGI `SCRIPT_NAME`, and route-root-resolved filesystem script target. A suffix after `cgi_script_name` is preserved as `PATH_INFO`. `Cgi` canonicalizes the final target and rejects one outside `root` before execution. |

## Reference Mock

Until #4 parses configuration text, `Config` explicitly builds a reference in-memory model:

| Listener | Route | Covered settings |
| --- | --- | --- |
| `0.0.0.0:8080` | `/` | Root, GET/POST/DELETE, index, error pages, body limit, `.sh` CGI through `/bin/sh`, and directly executable `.cgi` CGI. |
| `0.0.0.0:8080` | `/gallery` | Longest-prefix matching, index, and autoindex. |
| `0.0.0.0:8080` | `/uploads` | POST-only upload storage. |
| `0.0.0.0:8080` | `/redirect` | `302` redirect target. |
| `0.0.0.0:8080` | `/session` | GET-only, process-local cookie/session bonus demonstration. |
| `127.0.0.1:8081` | `/` | A second listener/server model. |

`Config::route(serverIndex, request)` resolves the longest matching location within the selected reference server. For a configured CGI extension, it resolves the optional handler, URL script portion, and a lexical route-root script path: `/cgi/test.sh/extra` selects `/bin/sh` with `/cgi/test.sh` as the script and `/extra` available to CGI as path information, while `/cgi/test.cgi` selects the directly executable fixture with no handler. Traversal segments reject CGI selection. `Cgi` then canonicalizes the script target and ensures it remains below the route root before execution. The `.cgi` fixture is built from `tests/cgi_direct_fixture.cpp` as part of `make`; it has no external runtime dependency. `bodyLimit(serverIndex)` returns that same server's request limit. The compatibility overloads select server zero; `Worker` instead binds every accepted connection to its listener's server index and passes that index to both APIs.

`Config::errorPage(serverIndex, status)` returns the selected server's configured path for an error status. `Worker` reads that regular file through `StaticFile`, preserves the original response status, and uses the file's MIME type. Missing, non-regular, unreadable, or unconfigured pages retain the deterministic `Http::defaultErrorResponse()` fallback.

The mock is not a future parse-error fallback. Once #4 supplies parsing, an unreadable or invalid configuration must report an error instead of constructing this fixture.
