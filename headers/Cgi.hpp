#ifndef CGI_HPP
#define CGI_HPP

#include <vector>
#include "types.hpp"

// Forks/execs a script, wires up its I/O, collects output non-blockingly.
class Cgi {
	public:
		Cgi();
		~Cgi();

		// serverFds lists every descriptor the server owns; the child closes
		// them so listeners, clients, and other CGI pipes do not survive execve().
		CgiJob    start(const Request &request, const Route &route, const ServerConfig &server,
			const std::string &remoteAddr, const std::vector<int> &serverFds);
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
