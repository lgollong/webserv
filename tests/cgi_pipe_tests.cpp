#include <arpa/inet.h>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static const int kPort = 8002;
static const int kStartupTimeoutMs = 5000;
static const int kResponseTimeoutMs = 6000;
static int failures = 0;

static void expect(bool condition, const char *message) {
	if (condition)
		return;
	std::cerr << "failure: " << message << std::endl;
	++failures;
}

static long long nowMs() {
	timespec value;
	if (clock_gettime(CLOCK_MONOTONIC, &value) != 0)
		return 0;
	return static_cast<long long>(value.tv_sec) * 1000 + value.tv_nsec / 1000000;
}

static bool waitForFd(int fd, short events, int timeoutMs) {
	pollfd event;
	event.fd = fd;
	event.events = events;
	event.revents = 0;
	return poll(&event, 1, timeoutMs) > 0 && (event.revents & (events | POLLERR | POLLHUP | POLLNVAL));
}

static bool serverRunning(pid_t &pid) {
	if (pid <= 0)
		return false;
	int status = 0;
	pid_t result = waitpid(pid, &status, WNOHANG);
	if (result == 0)
		return true;
	pid = -1;
	return false;
}

static int connectToServer() {
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;

	sockaddr_in address;
	std::memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons(kPort);
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static bool waitForServer(pid_t &server) {
	long long deadline = nowMs() + kStartupTimeoutMs;
	while (nowMs() < deadline) {
		if (!serverRunning(server))
			return false;
		int fd = connectToServer();
		if (fd >= 0) {
			close(fd);
			return true;
		}
		usleep(100000);
	}
	return false;
}

static pid_t startServer() {
	pid_t server = fork();
	if (server != 0)
		return server;

	int nullFd = open("/dev/null", O_WRONLY);
	if (nullFd >= 0) {
		dup2(nullFd, STDOUT_FILENO);
		dup2(nullFd, STDERR_FILENO);
		close(nullFd);
	}
	char *argv[] = {const_cast<char *>("./webserv"), const_cast<char *>("config/req.config"), NULL};
	execv(argv[0], argv);
	_exit(127);
}

static void stopServer(pid_t &server) {
	if (server <= 0)
		return;
	kill(server, SIGTERM);
	long long deadline = nowMs() + 2000;
	while (nowMs() < deadline) {
		if (!serverRunning(server))
			return;
		usleep(100000);
	}
	kill(server, SIGKILL);
	int status = 0;
	waitpid(server, &status, 0);
	server = -1;
}

static bool sendAll(int fd, const std::string &data) {
	std::string::size_type sent = 0;
	long long deadline = nowMs() + kResponseTimeoutMs;
	while (sent < data.size() && nowMs() < deadline) {
		if (!waitForFd(fd, POLLOUT, 200))
			continue;
		ssize_t result = send(fd, data.data() + sent, data.size() - sent, 0);
		if (result <= 0)
			return false;
		sent += static_cast<std::string::size_type>(result);
	}
	return sent == data.size();
}

static bool parseContentLength(const std::string &buffer, std::string::size_type headerEnd, size_t &length) {
	std::string::size_type start = buffer.find("Content-Length: ");
	if (start == std::string::npos || start >= headerEnd)
		return false;
	start += std::strlen("Content-Length: ");
	std::string::size_type end = buffer.find("\r\n", start);
	if (end == std::string::npos || end > headerEnd || start == end)
		return false;

	length = 0;
	for (std::string::size_type i = start; i < end; ++i) {
		if (buffer[i] < '0' || buffer[i] > '9')
			return false;
		length = length * 10 + static_cast<size_t>(buffer[i] - '0');
	}
	return true;
}

static bool readResponse(int fd, std::string &response) {
	long long deadline = nowMs() + kResponseTimeoutMs;
	while (nowMs() < deadline) {
		std::string::size_type headerEnd = response.find("\r\n\r\n");
		if (headerEnd != std::string::npos) {
			size_t contentLength = 0;
			if (!parseContentLength(response, headerEnd, contentLength))
				return false;
			if (response.size() >= headerEnd + 4 + contentLength)
				return response.size() == headerEnd + 4 + contentLength;
		}

		if (!waitForFd(fd, POLLIN, 200))
			continue;
		char buffer[4096];
		ssize_t result = recv(fd, buffer, sizeof(buffer), 0);
		if (result <= 0)
			return false;
		response.append(buffer, static_cast<std::string::size_type>(result));
	}
	return false;
}

static bool takeResponse(int fd, std::string &pending, std::string &response) {
	long long deadline = nowMs() + kResponseTimeoutMs;
	while (nowMs() < deadline) {
		std::string::size_type headerEnd = pending.find("\r\n\r\n");
		if (headerEnd != std::string::npos) {
			size_t contentLength = 0;
			if (!parseContentLength(pending, headerEnd, contentLength))
				return false;
			size_t responseLength = headerEnd + 4 + contentLength;
			if (pending.size() >= responseLength) {
				response.assign(pending, 0, responseLength);
				pending.erase(0, responseLength);
				return true;
			}
		}

		if (!waitForFd(fd, POLLIN, 200))
			continue;
		char buffer[4096];
		ssize_t result = recv(fd, buffer, sizeof(buffer), 0);
		if (result <= 0)
			return false;
		pending.append(buffer, static_cast<std::string::size_type>(result));
	}
	return false;
}

static bool sendUntilClosed(int fd, const std::string &data) {
	std::string::size_type sent = 0;
	long long deadline = nowMs() + kResponseTimeoutMs;
	while (sent < data.size() && nowMs() < deadline) {
		if (!waitForFd(fd, POLLOUT, 200))
			continue;
		size_t remaining = data.size() - sent;
		size_t chunk = remaining < 65536 ? remaining : 65536;
		ssize_t result = send(fd, data.data() + sent, chunk, 0);
		if (result <= 0)
			break;
		sent += static_cast<std::string::size_type>(result);
	}
	deadline = nowMs() + kResponseTimeoutMs;
	while (nowMs() < deadline) {
		pollfd event;
		event.fd = fd;
		event.events = POLLIN;
		event.revents = 0;
		if (poll(&event, 1, 200) <= 0)
			continue;
		if (event.revents & (POLLHUP | POLLERR | POLLNVAL))
			return true;
		if (!(event.revents & POLLIN))
			continue;
		char byte;
		ssize_t result = recv(fd, &byte, 1, 0);
		if (result == 0)
			return true;
		if (result < 0)
			return true;
	}
	return false;
}

static bool request(const std::string &bytes, std::string &response) {
	int fd = connectToServer();
	if (fd < 0)
		return false;
	bool success = sendAll(fd, bytes) && readResponse(fd, response);
	close(fd);
	return success;
}

static std::string getRequest(const std::string &path) {
	return "GET " + path + " HTTP/1.1\r\nHost: localhost\r\n\r\n";
}

int main() {
	// A server may close a connection while the test is still sending; that
	// must surface as a failed send(), not kill this process and orphan the
	// server it started.
	signal(SIGPIPE, SIG_IGN);

	pid_t server = startServer();
	bool started = server > 0 && waitForServer(server);
	expect(started, "server starts on configured loopback port 8002");
	if (!started) {
		stopServer(server);
		return 1;
	}

	std::string response;
	std::string body = "body through a non-blocking CGI pipe";
	std::string post = "POST /cgi/test.sh HTTP/1.1\r\nHost: localhost\r\nContent-Length: ";
	char length[32];
	snprintf(length, sizeof(length), "%lu", static_cast<unsigned long>(body.size()));
	post += length;
	post += "\r\n\r\n" + body;
	expect(request(post, response) && response.find("HTTP/1.1 200 OK") == 0 &&
		response.find("You sent: " + body) != std::string::npos,
		"CGI receives and returns a request body");

	response.clear();
	std::string contextBody = "full request context";
	std::string context = "POST /cgi/test.sh/extra?context HTTP/1.1\r\nHost: client.example\r\n"
		"Content-Type: text/plain\r\nX-Cgi-Test: accepted\r\nContent-Length: ";
	snprintf(length, sizeof(length), "%lu", static_cast<unsigned long>(contextBody.size()));
	context += length;
	context += "\r\n\r\n" + contextBody;
	expect(request(context, response) && response.find("HTTP/1.1 200 OK") == 0 &&
		response.find("REQUEST_METHOD=POST\n") != std::string::npos &&
		response.find("SCRIPT_NAME=/cgi/test.sh\n") != std::string::npos &&
		response.find("PATH_INFO=/extra\n") != std::string::npos &&
		response.find("QUERY_STRING=context\n") != std::string::npos &&
		response.find("SERVER_NAME=mywebsite.com\n") != std::string::npos &&
		response.find("SERVER_PORT=8002\n") != std::string::npos &&
		response.find("SERVER_PROTOCOL=HTTP/1.1\n") != std::string::npos &&
		response.find("CONTENT_TYPE=text/plain\n") != std::string::npos &&
		response.find("HTTP_X_CGI_TEST=accepted\n") != std::string::npos &&
		response.find("BODY=" + contextBody + "\n") != std::string::npos &&
		response.find("RELATIVE_FILE=CGI working directory is correct.\n") != std::string::npos,
		"CGI receives complete request context and runs in its script directory");

	int idleClients[4];
	for (int i = 0; i < 4; ++i)
		idleClients[i] = connectToServer();
	response.clear();
	expect(request(getRequest("/cgi/test.py?fds"), response) && response.find("HTTP/1.1 200 OK") == 0 &&
		response.find("FDS=0 1 2\n") != std::string::npos,
		"CGI child inherits only stdin, stdout, and stderr while other clients are connected");
	for (int i = 0; i < 4; ++i) {
		if (idleClients[i] >= 0)
			close(idleClients[i]);
	}

	response.clear();
	expect(request(getRequest("/cgi/test.sh?malformed"), response) &&
		response.find("HTTP/1.1 502 Bad Gateway") == 0 &&
		response.find("no CGI header block") == std::string::npos,
		"CGI output without a header block is a 502 without leaking the output");
	response.clear();
	expect(request(getRequest("/cgi/test.sh?huge"), response) &&
		response.find("HTTP/1.1 502 Bad Gateway") == 0 && response.size() < 4096,
		"CGI output above the 16 MiB cap is a 502 instead of being buffered");
	response.clear();
	expect(request(getRequest("/cgi/test.sh?teapot"), response) &&
		response.find("HTTP/1.1 502 Bad Gateway") == 0,
		"CGI Status the server cannot serialize is a 502");
	response.clear();
	expect(request(getRequest("/cgi/test.sh"), response) && response.find("HTTP/1.1 200 OK") == 0,
		"server keeps serving CGI after malformed CGI responses");

	response.clear();
	expect(request(getRequest("/cgi/test.py?name=webserv"), response) && response.find("HTTP/1.1 200 OK") == 0 &&
		response.find("Content-Type: text/html\r\n") != std::string::npos &&
		response.find("<li>Method: GET</li>") != std::string::npos &&
		response.find("<li>Query string: name=webserv</li>") != std::string::npos,
		"Python CGI receives the query string of a GET request");

	response.clear();
	std::string pythonPost = "POST /cgi/test.py HTTP/1.1\r\nHost: localhost\r\nContent-Length: ";
	snprintf(length, sizeof(length), "%lu", static_cast<unsigned long>(body.size()));
	pythonPost += length;
	pythonPost += "\r\n\r\n" + body;
	expect(request(pythonPost, response) && response.find("HTTP/1.1 200 OK") == 0 &&
		response.find("<li>Body: " + body + "</li>") != std::string::npos,
		"Python CGI receives a Content-Length request body");

	const std::string chunkedBody = "7\r\nchunked\r\n5\r\n body\r\n0\r\n\r\n";
	response.clear();
	expect(request("POST /cgi/test.py HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n" +
		chunkedBody, response) && response.find("HTTP/1.1 200 OK") == 0 &&
		response.find("<li>Body: chunked body</li>") != std::string::npos,
		"Python CGI receives an un-chunked body");
	response.clear();
	expect(request("POST /cgi/test.sh HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n" +
		chunkedBody, response) && response.find("HTTP/1.1 200 OK") == 0 &&
		response.find("You sent: chunked body") != std::string::npos,
		"shell CGI receives an un-chunked body");

	response.clear();
	expect(request(getRequest("/cgi/test.py/extra?context"), response) && response.find("HTTP/1.1 200 OK") == 0 &&
		response.find("SCRIPT_NAME=/cgi/test.py\n") != std::string::npos &&
		response.find("SCRIPT_FILENAME=test.py\n") != std::string::npos &&
		response.find("PATH_INFO=/extra\n") != std::string::npos &&
		response.find("REQUEST_URI=/cgi/test.py/extra?context\n") != std::string::npos &&
		response.find("REMOTE_ADDR=127.0.0.1\n") != std::string::npos &&
		response.find("REDIRECT_STATUS=200\n") != std::string::npos &&
		response.find("RELATIVE_FILE=CGI working directory is correct.\n") != std::string::npos,
		"Python CGI receives the extended environment and runs in its script directory");

	response.clear();
	expect(request(getRequest("/cgi/test.sh?delayed"), response) && response.find("HTTP/1.1 200 OK") == 0 &&
		response.find("You sent:") != std::string::npos,
		"CGI delayed output completes through readiness events");

	int pipelined = connectToServer();
	expect(pipelined >= 0, "CGI pipelined client connects");
	if (pipelined >= 0) {
		std::string pending;
		std::string firstResponse;
		std::string secondResponse;
		expect(sendAll(pipelined, getRequest("/cgi/test.sh?delayed")),
			"delayed CGI request is sent before the pipelined follow-up");
		usleep(200000);
		expect(sendAll(pipelined, getRequest("/static/index.html")),
			"complete follow-up request is sent while CGI is running");
		expect(takeResponse(pipelined, pending, firstResponse) &&
			firstResponse.find("HTTP/1.1 200 OK") == 0 &&
			firstResponse.find("You sent:") != std::string::npos,
			"CGI response is emitted before a complete pipelined follow-up");
		expect(takeResponse(pipelined, pending, secondResponse) &&
			secondResponse.find("HTTP/1.1 200 OK") == 0 &&
			secondResponse.find("<h1>HI</h1>") != std::string::npos,
			"complete pipelined follow-up is served after CGI");
		close(pipelined);
	}

	int fragmentedPipeline = connectToServer();
	expect(fragmentedPipeline >= 0, "fragmented CGI pipeline client connects");
	if (fragmentedPipeline >= 0) {
		std::string pending;
		std::string firstResponse;
		std::string secondResponse;
		expect(sendAll(fragmentedPipeline, getRequest("/cgi/test.sh?delayed")),
			"second delayed CGI request is sent before the fragmented follow-up");
		usleep(200000);
		expect(sendAll(fragmentedPipeline,
			"GET /static/index.html HTTP/1.1\r\nHost: localhost\r\n"),
			"fragmented follow-up prefix is sent while CGI is running");
		usleep(200000);
		expect(sendAll(fragmentedPipeline, "\r\n"),
			"fragmented follow-up is completed while CGI is running");
		expect(takeResponse(fragmentedPipeline, pending, firstResponse) &&
			firstResponse.find("HTTP/1.1 200 OK") == 0 &&
			firstResponse.find("You sent:") != std::string::npos,
			"CGI response is emitted before a fragmented pipelined follow-up");
		expect(takeResponse(fragmentedPipeline, pending, secondResponse) &&
			secondResponse.find("HTTP/1.1 200 OK") == 0 &&
			secondResponse.find("<h1>HI</h1>") != std::string::npos,
			"fragmented pipelined follow-up is served after CGI");
		close(fragmentedPipeline);
	}

	int oversizedPipeline = connectToServer();
	expect(oversizedPipeline >= 0, "oversized CGI pipeline client connects");
	if (oversizedPipeline >= 0) {
		expect(sendAll(oversizedPipeline, getRequest("/cgi/test.sh?delayed")),
			"delayed CGI request is sent before oversized deferred input");
		usleep(200000);
		std::string oversizedInput(17 * 1024 * 1024, 'x');
		expect(sendUntilClosed(oversizedPipeline, oversizedInput),
			"deferred CGI input is bounded and the connection is closed");
		close(oversizedPipeline);
	}
	response.clear();
	expect(request(getRequest("/static/index.html"), response) &&
		response.find("HTTP/1.1 200 OK") == 0,
		"server remains available after oversized deferred CGI input");

	response.clear();
	expect(request(getRequest("/cgi/test.sh?large"), response) && response.find("HTTP/1.1 200 OK") == 0 &&
		response.size() >= 20000 && response.find(std::string(128, 'x')) != std::string::npos,
		"CGI output larger than one read is fully framed without a CGI Content-Length");

	response.clear();
	std::string closedInputBody(262144, 'x');
	std::string closedInput = "POST /cgi/test.sh?close-input HTTP/1.1\r\nHost: localhost\r\nContent-Length: ";
	snprintf(length, sizeof(length), "%lu", static_cast<unsigned long>(closedInputBody.size()));
	closedInput += length;
	closedInput += "\r\n\r\n" + closedInputBody;
	expect(request(closedInput, response) && response.find("HTTP/1.1 502 Bad Gateway") == 0,
		"early CGI stdin closure returns the controlled 502 response");

	response.clear();
	expect(serverRunning(server) && request(getRequest("/static/index.html"), response) &&
		response.find("HTTP/1.1 200 OK") == 0,
		"server survives CGI pipe failure and serves a later request");

	stopServer(server);
	if (failures == 0)
		std::cout << "CGI pipe integration tests passed" << std::endl;
	return failures == 0 ? 0 : 1;
}
