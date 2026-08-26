#ifndef TYPES_HPP
#define TYPES_HPP

#include <string>
#include <map>
#include <set>
#include <vector>
#include <ctime>
#include <sys/types.h>

// @note is this even needed?
// Connection lifecycle phase (see DEV_DOC.md §4).
enum Phase {
	READING,
	RUNNING_CGI,
	WRITING,
	IDLE
};

enum LogLevel {
	ERROR,
	DEBUG
};

// Parsed inbound HTTP message. Owned by HTTP.
struct Request {
	std::string                        method;
	std::string                        path;
	std::string                        query;
	std::string                        version;
	std::map<std::string, std::string> headers;
	std::string                        body;
};

// Outbound HTTP message being assembled. Owned by HTTP.
struct Response {
	int                                 status;
	std::map<std::string, std::string>  headers;
	std::string                         body;

	Response() : status(0) {}
};

// Resolved location for a request. Owned by Config.
struct Route {
	std::string            root;
	bool                   is_cgi;
	std::string            cgi_pass;
	std::set<std::string>  allowed_methods;

	Route() : is_cgi(false) {}
};

// One server{} block from the config file. Owned by Config.
struct ServerConfig {
	std::string          host;
	int                  port;
	std::string          server_name;
	std::vector<Route>   locations;

	ServerConfig() : port(0) {}
};

// CGI sub-state, meaningful only while Connection::phase == RUNNING_CGI. Owned by CGI.
struct CgiJob {
	pid_t        pid;
	int          in_fd;
	int          out_fd;
	size_t       sent;
	std::string  output;
	bool         done;

	CgiJob() : pid(-1), in_fd(-1), out_fd(-1), sent(0), done(false) {}
};

// Result of serving a static file. Owned by StaticFile.
struct Content {
	int          status;
	std::string  body;
	std::string  mime_type;

	Content() : status(0) {}
};

// One request -> response cycle; reset per request on keep-alive.
struct Transaction {
	Request      request;
	Response     response;
	Route        route;
	CgiJob       cgi;
	bool         headers_done;
	size_t       content_length;
	int          status;

	Transaction() : headers_done(false), content_length(0), status(0) {}
};

// Per-socket state; outlives individual requests on keep-alive connections.
struct Connection {
	int          fd;
	Phase        phase;
	std::string  inbuf;
	std::string  outbuf;
	size_t       sent;
	bool         keep_alive; // @note needed?
	time_t       last_activity;
	Transaction  txn;

	Connection() : fd(-1), phase(IDLE), sent(0), keep_alive(false), last_activity(0) {}
};

#endif
