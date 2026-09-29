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
	std::string            location;
	std::string            root;
	bool                   is_cgi;
	std::string            cgi_pass;
	std::string            cgi_script_name;
	std::set<std::string>  allowed_methods;
	int                    redirect_status;
	std::string            redirect_target;
	bool                   autoindex;
	std::string            index_file;
	std::string            upload_store;
	std::map<std::string, std::string> cgi_handlers;

	Route() : is_cgi(false), redirect_status(0), autoindex(false) {}
};

// One server{} block from the config file. Owned by Config.
struct ServerConfig {
	std::string          host;
	int                  port;
	std::string          server_name;
	std::string          root;
	size_t               client_max_body_size;
	std::map<int, std::string> error_pages;
	std::vector<Route>   locations;

	ServerConfig() : port(0), client_max_body_size(0) {}
};

// CGI sub-state, meaningful only while Connection::phase == RUNNING_CGI. Owned by CGI.
struct CgiJob {
	pid_t        pid;
	int          in_fd;
	int          out_fd;
	size_t       sent;
	std::string  output;
	bool         done;
	bool         failed;
	time_t       started_at;
	time_t       last_activity;
	bool         termination_requested;
	time_t       termination_requested_at;

	CgiJob() : pid(-1), in_fd(-1), out_fd(-1), sent(0), done(false), failed(false),
		started_at(0), last_activity(0), termination_requested(false), termination_requested_at(0) {}

	bool hasTimedOut(time_t now, time_t timeout) const {
		if (pid <= 0 || started_at == 0 || now < started_at)
			return false;
		return now - started_at >= timeout;
	}

	bool hasTerminationGraceExpired(time_t now, time_t grace) const {
		if (!termination_requested || termination_requested_at == 0 || now < termination_requested_at)
			return false;
		return now - termination_requested_at >= grace;
	}
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
	size_t       server_index;
	Phase        phase;
	std::string  inbuf;
	std::string  outbuf;
	size_t       sent;
	bool         keep_alive;
	bool         close_after_write;
	time_t       last_activity;
	Transaction  txn;

	Connection() : fd(-1), server_index(0), phase(IDLE), sent(0), keep_alive(false), close_after_write(false), last_activity(0) {}

	bool hasClientTimedOut(time_t now, time_t timeout) const {
		if (phase == RUNNING_CGI || last_activity == 0 || now < last_activity)
			return false;
		return now - last_activity >= timeout;
	}
};

#endif
