#ifndef CGI_HPP
#define CGI_HPP

#include "types.hpp"

// Forks/execs a script, wires up its I/O, collects output non-blockingly.
class Cgi {
	public:
		Cgi();
		~Cgi();

		CgiJob    start(const Request &request, const Route &route, const ServerConfig &server);
		bool      collect(CgiJob &cgi);
		bool      sendBody(CgiJob &job, const std::string &body); // true once fully written (and in_fd closed)
		void      terminate(CgiJob &job) const;
		void      forceTerminate(CgiJob &job) const;
		void      forceTerminate(pid_t pid) const;
		bool      reap(CgiJob &job) const;
		bool      reap(pid_t &pid) const;
		Response  buildResponse(const CgiJob &job) const;
};

#endif
