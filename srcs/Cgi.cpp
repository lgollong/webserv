#include "Cgi.hpp"
#include <cstdlib>

Cgi::Cgi() {}

Cgi::~Cgi() {}

CgiJob Cgi::start(const Request &request, const Route &route) {
	(void)request;
	(void)route;

	// mock: no real fork/execve yet. in_fd/out_fd stay -1 on purpose --
	// faking a positive fd number here would be dangerous, since Worker
	// would hand it straight to the poller and it could collide with a
	// real, already-open fd. Instead pretend the "script" finished
	// instantly so Worker can exercise the RUNNING_CGI -> collect path
	// without needing a real fd registered anywhere.
	CgiJob job;
	job.pid = 4242;
	job.in_fd = -1;
	job.out_fd = -1;
	job.output = "Content-Type: text/plain\r\n\r\nHello from CGI mock!\n";
	job.done = true;

	return job;
}

bool Cgi::collect(CgiJob &cgi) {
	// mock: start() already filled cgi.output and marked it done; a real
	// implementation would read(cgi.out_fd) here and append to cgi.output,
	// flipping done to true only once the child has exited.
	return cgi.done;
}

Response Cgi::buildResponse(const CgiJob &job) const {
	// real parsing against job.output -- CGI/1.1 scripts write a small
	// Key: Value header block, a blank line, then the body. Not the same
	// as an HTTP response (no status line), so we translate it into one.
	const std::string &raw = job.output;

	std::string::size_type sep = raw.find("\r\n\r\n");
	size_t sepLen = 4;
	if (sep == std::string::npos) {
		sep = raw.find("\n\n"); // some scripts use bare LF instead of CRLF
		sepLen = 2;
	}

	std::string headerBlock = (sep == std::string::npos) ? "" : raw.substr(0, sep);
	std::string body        = (sep == std::string::npos) ? raw : raw.substr(sep + sepLen);

	Response response;
	response.status = 200; // CGI/1.1 default when the script omits Status:
	response.body = body;

	std::string::size_type lineStart = 0;
	while (lineStart < headerBlock.size()) {
		std::string::size_type lineEnd = headerBlock.find('\n', lineStart);
		std::string line = (lineEnd == std::string::npos)
			? headerBlock.substr(lineStart)
			: headerBlock.substr(lineStart, lineEnd - lineStart);

		if (!line.empty() && line[line.size() - 1] == '\r')
			line.erase(line.size() - 1);

		std::string::size_type colon = line.find(':');
		if (colon != std::string::npos) {
			std::string key = line.substr(0, colon);
			std::string::size_type valueStart = line.find_first_not_of(' ', colon + 1);
			std::string value = (valueStart == std::string::npos) ? "" : line.substr(valueStart);

			if (key == "Status")
				response.status = std::atoi(value.c_str());
			else
				response.headers[key] = value;
		}

		if (lineEnd == std::string::npos)
			break;
		lineStart = lineEnd + 1;
	}

	return response;
}
