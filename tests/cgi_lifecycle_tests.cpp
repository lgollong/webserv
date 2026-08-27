#include "../headers/Cgi.hpp"

#include <iostream>
#include <poll.h>
#include <string>
#include <unistd.h>

static int failures = 0;

static void expect(bool condition, const std::string &name) {
	if (condition)
		return;
	std::cerr << "FAIL: " << name << std::endl;
	++failures;
}

static bool waitForReap(Cgi &cgi, CgiJob &job) {
	for (int attempt = 0; attempt < 3; ++attempt) {
		if (cgi.reap(job))
			return true;
		sleep(1);
	}
	return false;
}

int main() {
	Cgi cgi;

	CgiJob timing;
	timing.pid = 42;
	timing.started_at = static_cast<time_t>(100);
	expect(!timing.hasTimedOut(static_cast<time_t>(114), static_cast<time_t>(15)),
		"cgi remains active before deadline");
	expect(timing.hasTimedOut(static_cast<time_t>(115), static_cast<time_t>(15)),
		"cgi expires at deadline");
	expect(!timing.hasTimedOut(static_cast<time_t>(99), static_cast<time_t>(15)),
		"backwards clock does not expire cgi");
	timing.termination_requested = true;
	timing.termination_requested_at = static_cast<time_t>(115);
	expect(!timing.hasTerminationGraceExpired(static_cast<time_t>(116), static_cast<time_t>(2)),
		"cgi termination grace holds before deadline");
	expect(timing.hasTerminationGraceExpired(static_cast<time_t>(117), static_cast<time_t>(2)),
		"cgi termination grace expires at deadline");

	Request request;
	Route script;
	script.cgi_pass = "./contents/cgi/test.sh";
	CgiJob completed = cgi.start(request, script);
	expect(!completed.failed && completed.pid > 0, "cgi script starts");
	if (completed.in_fd >= 0) {
		close(completed.in_fd);
		completed.in_fd = -1;
	}
	for (int attempt = 0; attempt < 3 && !completed.done; ++attempt) {
		pollfd event;
		event.fd = completed.out_fd;
		event.events = POLLIN;
		event.revents = 0;
		if (poll(&event, 1, 1000) > 0 && (event.revents & (POLLIN | POLLHUP)))
			cgi.collect(completed);
	}
	expect(completed.done && !completed.failed, "cgi script output completes");
	expect(waitForReap(cgi, completed), "completed cgi is reaped without blocking");
	if (completed.out_fd >= 0)
		close(completed.out_fd);

	Route shell;
	shell.cgi_pass = "/bin/sh";
	CgiJob stalled = cgi.start(request, shell);
	expect(!stalled.failed && stalled.pid > 0, "stalled cgi shell starts");
	expect(!cgi.reap(stalled), "running cgi is not reaped early");
	cgi.terminate(stalled);
	expect(stalled.termination_requested, "cgi termination is recorded");
	expect(stalled.termination_requested_at != 0, "cgi termination time is recorded");
	if (stalled.in_fd >= 0) {
		close(stalled.in_fd);
		stalled.in_fd = -1;
	}
	if (stalled.out_fd >= 0) {
		close(stalled.out_fd);
		stalled.out_fd = -1;
	}
	expect(waitForReap(cgi, stalled), "terminated cgi is reaped without blocking");

	if (failures == 0)
		std::cout << "cgi lifecycle tests passed" << std::endl;
	return failures == 0 ? 0 : 1;
}
