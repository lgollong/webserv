#include "Cgi.hpp"

Cgi::Cgi() {}

Cgi::~Cgi() {}

CgiJob Cgi::start(const Request& request, const Route& route) {
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

bool Cgi::collect(CgiJob& cgi) {
	// mock: start() already filled cgi.output and marked it done; a real
	// implementation would read(cgi.out_fd) here and append to cgi.output,
	// flipping done to true only once the child has exited.
	return cgi.done;
}
