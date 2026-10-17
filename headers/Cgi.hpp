#ifndef CGI_HPP
#define CGI_HPP

#include <vector>
#include "types.hpp"

class Cgi {
	public:
		// Collected CGI output is capped; a larger response becomes a 502.
		enum { MAX_OUTPUT_BYTES = 16 * 1024 * 1024 };

		Cgi();
		~Cgi();

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
