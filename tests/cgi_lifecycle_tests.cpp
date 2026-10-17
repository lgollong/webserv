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

static Response responseFor(const Cgi &cgi, const std::string &output) {
	CgiJob job;
	job.output = output;
	return cgi.buildResponse(job);
}

static void expectBadGateway(const Cgi &cgi, const std::string &output, const std::string &name) {
	Response response = responseFor(cgi, output);
	expect(response.status == 502 && response.body.empty() && response.headers.empty(), name + " is a 502");
}

static void testResponseParsing(const Cgi &cgi) {
	Response document = responseFor(cgi, "content-TYPE: text/plain\r\nX-Extra:  kept  \r\n\r\nbody\r\n\r\nmore");
	expect(document.status == 200 && document.headers["Content-Type"] == "text/plain" &&
		document.headers["X-Extra"] == "kept" && document.body == "body\r\n\r\nmore",
		"document response matches CGI fields case-insensitively and keeps the body verbatim");

	Response lf = responseFor(cgi, "Content-Type: text/html\nStatus: 404 Not Found\n\n<p>missing</p>");
	expect(lf.status == 404 && lf.body == "<p>missing</p>", "LF-only output with a Status is accepted");

	Response bareStatus = responseFor(cgi, "Status: 201\r\nContent-Type: text/plain\r\n\r\n");
	expect(bareStatus.status == 201 && bareStatus.body.empty(), "Status without a reason phrase is accepted");

	Response redirect = responseFor(cgi, "Location: https://example.test/next\r\n\r\n");
	expect(redirect.status == 302 && redirect.headers["Location"] == "https://example.test/next",
		"Location without Status is a 302 redirect");

	Response framed = responseFor(cgi, "Content-Type: text/plain\r\nContent-Length: 999\r\n"
		"Transfer-Encoding: chunked\r\nConnection: close\r\n\r\nshort");
	expect(framed.status == 200 && framed.body == "short" && framed.headers.size() == 1,
		"CGI cannot inject server-controlled framing headers");

	Response earlyLf = responseFor(cgi, "Content-Type: text/plain\n\nbody with\r\n\r\ninside");
	expect(earlyLf.status == 200 && earlyLf.body == "body with\r\n\r\ninside",
		"the first empty line ends the headers even when CRLF CRLF appears later");

	expectBadGateway(cgi, "", "empty output");
	expectBadGateway(cgi, "plain text without headers", "output without a header separator");
	expectBadGateway(cgi, "Content-Type: text/plain\r\n", "headers without a terminating empty line");
	expectBadGateway(cgi, "X-Only: value\r\n\r\nbody", "output without Content-Type or Location");
	expectBadGateway(cgi, "Content-Type text/plain\r\n\r\nbody", "header line without a colon");
	expectBadGateway(cgi, "Bad Name: x\r\nContent-Type: text/plain\r\n\r\n", "invalid header name");
	expectBadGateway(cgi, "Content-Type: text/plain\r\n folded: continuation\r\n\r\n", "folded header line");
	expectBadGateway(cgi, std::string("Content-Type: text/plain\r\nX-Bad: a\0b\r\n\r\n", 40), "control character in a value");
	expectBadGateway(cgi, "Content-Type: text/plain\r\nStatus: 20x OK\r\n\r\n", "non-numeric Status");
	expectBadGateway(cgi, "Content-Type: text/plain\r\nStatus: 2000\r\n\r\n", "four-digit Status");
	expectBadGateway(cgi, "Content-Type: text/plain\r\nStatus: 100 Continue\r\n\r\n", "informational Status");
	expectBadGateway(cgi, "Content-Type: text/plain\r\nStatus: 200\r\nstatus: 404\r\n\r\n", "duplicate Status");
	expectBadGateway(cgi, "Content-Type: text/plain\r\ncontent-type: text/html\r\n\r\n", "duplicate Content-Type");

	CgiJob failed;
	failed.output = "Content-Type: text/plain\r\n\r\nok";
	failed.failed = true;
	expect(cgi.buildResponse(failed).status == 502, "failed CGI job is a 502 even with valid output");
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
	ServerConfig server;
	server.server_name = "localhost";
	server.port = 8080;
	Route script;
	script.root = "./contents";
	script.cgi_handler = "/bin/sh";
	script.cgi_script_name = "/cgi/test.sh";
	script.cgi_script_path = "./contents/cgi/test.sh";
	CgiJob completed = cgi.start(request, script, server, "127.0.0.1");
	expect(!completed.failed && completed.pid > 0, "interpreter-backed cgi script starts");
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
	shell.root = "./contents";
	shell.cgi_script_name = "/cgi/test.sh";
	shell.cgi_script_path = "./contents/cgi/test.sh";
	request.query = "stall";
	CgiJob stalled = cgi.start(request, shell, server, "127.0.0.1");
	expect(!stalled.failed && stalled.pid > 0, "directly executable cgi script starts");
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

	testResponseParsing(cgi);

	if (failures == 0)
		std::cout << "cgi lifecycle tests passed" << std::endl;
	return failures == 0 ? 0 : 1;
}
