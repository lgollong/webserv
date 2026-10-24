#ifndef CGI_HPP
#define CGI_HPP

#include <vector>
#include "types.hpp"

class Cgi {
	public:
		enum { MAX_OUTPUT_BYTES = 256 * 1024 * 1024 };

		Cgi();
		~Cgi();

		CgiJob    start(const Request &request, const Route &route, const ServerConfig &server,
			const std::string &remoteAddr, const std::vector<int> &serverFds);
		bool      collect(CgiJob &job);
		bool      sendBody(CgiJob &job, const std::string &body);
		void      stop(CgiJob &job) const;
		Response  buildResponse(const CgiJob &job) const;
};

#endif
