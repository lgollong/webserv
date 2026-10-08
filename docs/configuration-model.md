# Runtime Configuration Model

`Config` parses the configuration file at startup and normalizes it into `ServerConfig` and `Route` records. `Worker` binds one listener for every parsed server record, then retains the accepting server index on each connection. `Config::route(serverIndex, request)` selects that server's longest matching location and returns a copy with request-specific CGI fields resolved.

An unreadable configuration and recognized syntax errors throw a startup exception; `main()` logs the message and exits. There is no fallback reference configuration.

## Supported Syntax

The parser accepts comments beginning with `#`, single- or double-quoted tokens, and `server { ... }` blocks containing `location /path { ... }` blocks.

Server directives:

| Directive | Meaning |
| --- | --- |
| `listen <port>;` | TCP listener port. |
| `host <IPv4-address>;` | Listener address. Missing host defaults to `0.0.0.0`. |
| `server_name <name>;` | Value provided to CGI as `SERVER_NAME`. It is not Host-header virtual-host routing. |
| `root <path>;` | Stored server-level root. It is used by the route fallback when no location matches; parsed locations currently default independently to `./contents`. |
| `client_max_body_size <bytes|k|m|g>;` | Decoded request-body limit passed to `Http`. Missing values default to 1 MiB. |
| `error_page <100-599> <path>;` | Maps an error status to a static error-page file. |

Location directives:

| Directive | Meaning |
| --- | --- |
| `root <path>;` | Filesystem root for this location. Missing values currently default to `./contents`. |
| `index <file>;` | Safe index file to serve from a directory. |
| `autoindex on|off;` | Enables or disables generated directory listings. |
| `allowed_methods <method> ...;` | Permitted request methods. `allow_methods` is accepted as an alias. Missing values default to `GET`. |
| `upload_store <path>;` | Enables Worker upload dispatch for POST and names its storage directory. |
| `upload on|off;` | Parsed and syntax-validated, but currently does not independently enable or disable uploads; `upload_store` controls runtime behavior. |
| `return [300-399] <target>;` | Configured redirect. The status defaults to `302`. |
| `cgi <extension> <handler>;` | Maps a script extension to an interpreter/executable path. |

The parser defaults a missing `listen` directive to port `8080` and inserts a GET-only `/` route rooted at `./contents` when a server has no locations. It accepts IPv4 listener addresses; invalid addresses are rejected when `Worker` creates the listener.

## Runtime Records

### `ServerConfig`

| Field | Runtime meaning |
| --- | --- |
| `host`, `port` | IPv4 interface and port bound by one listener. |
| `server_name` | CGI `SERVER_NAME`. |
| `root` | Server-level root used by the route fallback when no location matches. |
| `client_max_body_size` | Maximum decoded request-body size. |
| `error_pages` | Status-to-static-file mapping for empty error responses. |
| `locations` | Parsed `Route` records. |

### `Route`

| Field | Runtime meaning |
| --- | --- |
| `location` | Boundary-aware URL-prefix match. |
| `root` | Filesystem root used by static and CGI resolution. |
| `allowed_methods` | Method policy enforced before a handler runs. |
| `redirect_status`, `redirect_target` | A configured redirect, or status `0`. |
| `autoindex`, `index_file` | Directory-serving behavior. |
| `upload_store` | Storage directory that enables POST uploads. |
| `cgi_handlers` | Extension-to-handler mappings from the configuration. |
| `is_cgi`, `cgi_handler`, `cgi_script_name`, `cgi_script_path` | Per-request CGI selection derived during route resolution. `Cgi` canonicalizes the script and rejects it if it escapes the route root. |

`session_demo` remains in the shared structure for the existing session handler, but the parser has no directive for it and neither supplied configuration exposes `/session`.

## Supplied Configurations

`config/default.config` is the browser demonstration configuration:

| Listener | Routes |
| --- | --- |
| `0.0.0.0:8080` | `/`, `/files`, `/uploads`, `/cgi`, and `/old-page` |

`config/req.config` is a multi-listener fixture used by the updated configuration and connection-lifecycle tests. It defines servers on ports `8002`, `8003`, `8008`, and `8001` with static, upload, redirect, documentation, and CGI route examples.

## Current Limitations

- The parser does not validate malformed configuration through a dedicated test suite yet.
- `upload off` has no runtime effect.
- Parsed location roots do not inherit a server-level `root` value.
- Duplicate host/port pairs are not rejected before `Worker` attempts to bind them.
- `server_name` does not select a server from the HTTP `Host` header.
