#ifndef TYPES_HPP
#define TYPES_HPP

#include <string>
#include <map>
#include <set>
#include <vector>
#include <ctime>
#include <sys/types.h>

enum Phase {
	READING,
	RUNNING_CGI,
	WRITING
};

struct Request {
	std::string                        method;
	std::string                        path;
	std::string                        query;
	std::string                        version;
	std::map<std::string, std::string> headers;
	std::string                        body;
};

struct Response {
	int                                 status;
	std::map<std::string, std::string>  headers;
	std::string                         body;

	Response() : status(0) {}
};

struct Route {
	std::string            location;
	std::string            root;
	bool                   is_cgi;
	std::string            cgi_handler;
	std::string            cgi_script_name;
	std::string            cgi_script_path;
	std::set<std::string>  allowed_methods;
	int                    redirect_status;
	std::string            redirect_target;
	bool                   autoindex;
	std::string            index_file;
	std::string            upload_store;
	bool                   upload_enabled;
	bool                   session_demo;
	std::map<std::string, std::string> cgi_handlers;

	Route() : is_cgi(false), redirect_status(0), autoindex(false), upload_enabled(false), session_demo(false) {}
};

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

struct CgiJob {
	pid_t        pid;
	int          in_fd;
	int          out_fd;
	size_t       sent;
	std::string  output;
	bool         failed;
	time_t       started_at;

	CgiJob() : pid(-1), in_fd(-1), out_fd(-1), sent(0), failed(false), started_at(0) {}
};

struct Content {
	int          status;
	std::string  body;
	std::string  mime_type;
	std::string  location;
	int          file_fd;
	size_t       file_size;

	Content() : status(0), file_fd(-1), file_size(0) {}
};

struct Transaction {
	Request      request;
	Response     response;
	Route        route;
	CgiJob       cgi;
};

struct Connection {
	int          fd;
	size_t       server_index;
	Phase        phase;
	std::string  inbuf;
	std::string  outbuf;
	size_t       sent;
	bool         close_after_write;
	time_t       last_activity;
	std::string  remote_addr;
	int          body_fd;
	size_t       body_remaining;
	Transaction  txn;

	Connection() : fd(-1), server_index(0), phase(READING), sent(0), close_after_write(false),
		last_activity(0), body_fd(-1), body_remaining(0) {}
};

#endif
