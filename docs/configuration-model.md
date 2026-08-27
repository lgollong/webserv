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
| `cgi_handlers` | File extension to CGI executable mapping. |
| `is_cgi`, `cgi_pass` | Per-request CGI result derived from the request extension and `cgi_handlers`. |

## Reference Mock

Until #4 parses configuration text, `Config` explicitly builds a reference in-memory model:

| Listener | Route | Covered settings |
| --- | --- | --- |
| `0.0.0.0:8080` | `/` | Root, GET/POST/DELETE, index, error pages, body limit, and `.sh` CGI. |
| `0.0.0.0:8080` | `/gallery` | Longest-prefix matching, index, and autoindex. |
| `0.0.0.0:8080` | `/uploads` | POST-only upload storage. |
| `0.0.0.0:8080` | `/redirect` | `302` redirect target. |
| `127.0.0.1:8081` | `/` | A second listener/server model. |

`Config::route(serverIndex, request)` resolves the longest matching location within the selected reference server, and `bodyLimit(serverIndex)` returns that same server's request limit. The compatibility overloads select server zero; `Worker` instead binds every accepted connection to its listener's server index and passes that index to both APIs.

`Config::errorPage(serverIndex, status)` returns the selected server's configured path for an error status. `Worker` reads that regular file through `StaticFile`, preserves the original response status, and uses the file's MIME type. Missing, non-regular, unreadable, or unconfigured pages retain the deterministic `Http::defaultErrorResponse()` fallback.

The mock is not a future parse-error fallback. Once #4 supplies parsing, an unreadable or invalid configuration must report an error instead of constructing this fixture.
