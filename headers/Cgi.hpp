#ifndef CGI_HPP
#define CGI_HPP

#include "types.hpp"

// Forks/execs a script, wires up its I/O, collects output non-blockingly.
class Cgi {
	public:
		Cgi();
		~Cgi();

		CgiJob    start(const Request &request, const Route &route);
		bool      collect(CgiJob &cgi);
		bool      sendBody(CgiJob &job, const std::string &body); // true once fully written (and in_fd closed)
		Response  buildResponse(const CgiJob &job) const;
};

#endif
