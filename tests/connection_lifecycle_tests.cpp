#include <arpa/inet.h>
#include <cstdio>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static const int kPort = 8080;
static const int kStartupTimeoutMs = 5000;
static const int kResponseTimeoutMs = 5000;
static const int kCloseTimeoutMs = 2000;

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

static bool writeDeleteFixture() {
	int fd = open("./contents/delete-lifecycle-fixture.txt", O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0)
		return false;
	const char body[] = "disposable lifecycle delete fixture\n";
	ssize_t written = write(fd, body, sizeof(body) - 1);
	close(fd);
	return written == static_cast<ssize_t>(sizeof(body) - 1);
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
		char received[4096];
		ssize_t result = recv(fd, received, sizeof(received), 0);
		if (result <= 0)
			return false;
		pending.append(received, static_cast<std::string::size_type>(result));
	}
	return false;
}

static bool waitForClose(int fd, std::string &pending) {
	long long deadline = nowMs() + kCloseTimeoutMs;
	while (nowMs() < deadline) {
		if (!waitForFd(fd, POLLIN, 200))
			continue;
		char received[256];
		ssize_t result = recv(fd, received, sizeof(received), 0);
		if (result == 0)
			return pending.empty();
		if (result < 0)
			return false;
		pending.append(received, static_cast<std::string::size_type>(result));
	}
	return false;
}

static std::string request(const std::string &path, const std::string &connection) {
	std::string value = "GET " + path + " HTTP/1.1\r\nHost: localhost\r\n";
	if (!connection.empty())
		value += "Connection: " + connection + "\r\n";
	return value + "\r\n";
}

static std::string requestWithMethod(const std::string &method, const std::string &path,
	const std::string &connection) {
	std::string value = method + " " + path + " HTTP/1.1\r\nHost: localhost\r\n";
	if (!connection.empty())
		value += "Connection: " + connection + "\r\n";
	return value + "\r\n";
}

static int countOccurrences(const std::string &value, const std::string &wanted) {
	int count = 0;
	std::string::size_type pos = 0;
	while ((pos = value.find(wanted, pos)) != std::string::npos) {
		++count;
		pos += wanted.size();
	}
	return count;
}

int main() {
	expect(writeDeleteFixture(), "creates a disposable delete lifecycle fixture");
	pid_t server = startServer();
	bool started = server > 0 && waitForServer(server);
	expect(started, "server starts on loopback port 8080");
	if (!started) {
		stopServer(server);
		std::remove("./contents/delete-lifecycle-fixture.txt");
		return 1;
	}

	int fragmented = connectToServer();
	expect(fragmented >= 0, "fragmented client connects");
	std::string pending;
	std::string response;
	if (fragmented >= 0) {
		expect(sendAll(fragmented, "GET /files/index.html HTTP/1.1\r\nHost: localhost\r\n"),
			"fragmented request prefix is sent");
		expect(!waitForFd(fragmented, POLLIN, 300), "fragmented request receives no early response");
		expect(sendAll(fragmented, "\r\n"), "fragmented request is completed");
		expect(takeResponse(fragmented, pending, response) && response.find("HTTP/1.1 200 OK") == 0 &&
			response.find("<h1>HI</h1>") != std::string::npos,
			"completed fragmented request receives one response");
		close(fragmented);
	}

	int persistent = connectToServer();
	expect(persistent >= 0, "persistent client connects");
	pending.clear();
	if (persistent >= 0) {
		expect(sendAll(persistent, request("/files/index.html", "")), "first persistent request is sent");
		expect(takeResponse(persistent, pending, response) && response.find("<h1>HI</h1>") != std::string::npos,
			"first persistent request receives a response");

		int secondClient = connectToServer();
		expect(secondClient >= 0, "second client connects while persistent client remains open");
		if (secondClient >= 0) {
			std::string secondPending;
			expect(sendAll(secondClient, request("/files/index.html", "")), "second client request is sent");
			expect(takeResponse(secondClient, secondPending, response) &&
				response.find("<h1>HI</h1>") != std::string::npos,
				"second client remains served while persistent client is open");
			close(secondClient);
		}

		expect(sendAll(persistent, request("/files/index.html", "")), "second persistent request is sent");
		expect(takeResponse(persistent, pending, response) && response.find("<h1>HI</h1>") != std::string::npos,
			"second persistent request receives a response");

		expect(sendAll(persistent, requestWithMethod("POST", "/gallery", "")), "disallowed POST request is sent");
		expect(takeResponse(persistent, pending, response) && response.find("HTTP/1.1 405 Method Not Allowed") == 0 &&
			countOccurrences(response, "Allow: GET\r\n") == 1,
			"disallowed static-route method returns one deterministic Allow header");

		expect(sendAll(persistent, requestWithMethod("DELETE", "/gallery", "")), "disallowed DELETE request is sent");
		expect(takeResponse(persistent, pending, response) && response.find("HTTP/1.1 405 Method Not Allowed") == 0 &&
			countOccurrences(response, "Allow: GET\r\n") == 1,
			"disallowed DELETE request is rejected before static handling");

		expect(sendAll(persistent, requestWithMethod("GET", "/uploads", "")), "disallowed upload-route GET is sent");
		expect(takeResponse(persistent, pending, response) && response.find("HTTP/1.1 405 Method Not Allowed") == 0 &&
			countOccurrences(response, "Allow: POST\r\n") == 1,
			"disallowed upload-route method returns its route policy");

		expect(sendAll(persistent, requestWithMethod("PUT", "/files/index.html", "")), "disallowed root-route PUT is sent");
		expect(takeResponse(persistent, pending, response) && response.find("HTTP/1.1 405 Method Not Allowed") == 0 &&
			countOccurrences(response, "Allow: DELETE, GET, POST\r\n") == 1,
			"multi-method route returns one deterministically ordered Allow header");

		expect(sendAll(persistent, requestWithMethod("POST", "/redirect", "")), "disallowed redirect POST is sent");
		expect(takeResponse(persistent, pending, response) && response.find("HTTP/1.1 405 Method Not Allowed") == 0 &&
			response.find("Location:") == std::string::npos,
			"method rejection runs before redirect handling");

		expect(sendAll(persistent, requestWithMethod("POST", "/uploads", "")), "allowed upload-route POST is sent");
		expect(takeResponse(persistent, pending, response) && response.find("HTTP/1.1 403 Forbidden") == 0,
			"allowed methods continue to their existing downstream handler");

		expect(sendAll(persistent, requestWithMethod("DELETE", "/delete-lifecycle-fixture.txt", "")),
			"permitted DELETE request is sent");
		expect(takeResponse(persistent, pending, response) && response.find("HTTP/1.1 204 No Content") == 0 &&
			response.find("Content-Length: 0\r\n") != std::string::npos,
			"permitted DELETE removes a file with an empty response");

		expect(sendAll(persistent, request("/delete-lifecycle-fixture.txt", "")),
			"post-delete GET request is sent");
		expect(takeResponse(persistent, pending, response) && response.find("HTTP/1.1 404 Not Found") == 0,
			"deleted file is no longer served");

		expect(sendAll(persistent, requestWithMethod("DELETE", "/files", "")), "directory DELETE request is sent");
		expect(takeResponse(persistent, pending, response) && response.find("HTTP/1.1 403 Forbidden") == 0,
			"directory DELETE is rejected without removing the directory");

		expect(sendAll(persistent, request("/gallery", "")), "autoindex request is sent");
		expect(takeResponse(persistent, pending, response) && response.find("HTTP/1.1 200 OK") == 0 &&
			response.find("Index of /gallery") != std::string::npos &&
			response.find("href=\"/gallery/alpha.txt\"") != std::string::npos,
			"configured autoindex route receives an HTML directory listing");

		expect(sendAll(persistent, request("/redirect", "")), "redirect request is sent");
		expect(takeResponse(persistent, pending, response) && response.find("HTTP/1.1 302 Found") == 0 &&
			response.find("Location: /gallery\r\n") != std::string::npos &&
			response.find("Content-Length: 0\r\n") != std::string::npos &&
			response.find("<h1>HI</h1>") == std::string::npos,
			"configured redirect is framed before static handling");

		expect(sendAll(persistent, request("/files/index.html", "")), "post-redirect request is sent");
		expect(takeResponse(persistent, pending, response) && response.find("<h1>HI</h1>") != std::string::npos,
			"persistent connection serves a request after redirecting");

		std::string buffered = request("/test.sh", "") + request("/files/index.html", "");
		expect(sendAll(persistent, buffered), "buffered CGI and static requests are sent together");
		expect(takeResponse(persistent, pending, response) && response.find("You sent:") != std::string::npos,
			"buffered CGI request receives its response first");
		expect(takeResponse(persistent, pending, response) && response.find("<h1>HI</h1>") != std::string::npos,
			"buffered static request receives its distinct response");
		close(persistent);
	}

	int closing = connectToServer();
	expect(closing >= 0, "close-policy client connects");
	pending.clear();
	if (closing >= 0) {
		expect(sendAll(closing, request("/files/index.html", "keep-alive, Close")), "close-policy request is sent");
		expect(takeResponse(closing, pending, response) && countOccurrences(response, "Connection: close\r\n") == 1,
			"close-policy response contains one close header");
		expect(waitForClose(closing, pending), "close-policy connection closes after its response");
		close(closing);
	}

	int redirectClosing = connectToServer();
	expect(redirectClosing >= 0, "redirect close-policy client connects");
	pending.clear();
	if (redirectClosing >= 0) {
		expect(sendAll(redirectClosing, request("/redirect", "close")), "redirect close-policy request is sent");
		expect(takeResponse(redirectClosing, pending, response) && response.find("HTTP/1.1 302 Found") == 0 &&
			response.find("Location: /gallery\r\n") != std::string::npos &&
			countOccurrences(response, "Connection: close\r\n") == 1,
			"redirect close-policy response contains Location and one close header");
		expect(waitForClose(redirectClosing, pending), "redirect close-policy connection closes after its response");
		close(redirectClosing);
	}

	int cgiClosing = connectToServer();
	expect(cgiClosing >= 0, "CGI close-policy client connects");
	pending.clear();
	if (cgiClosing >= 0) {
		expect(sendAll(cgiClosing, request("/test.sh", "close")), "CGI close-policy request is sent");
		expect(takeResponse(cgiClosing, pending, response) && response.find("You sent:") != std::string::npos &&
			countOccurrences(response, "Connection: close\r\n") == 1,
			"CGI close-policy response contains one close header");
		expect(waitForClose(cgiClosing, pending), "CGI close-policy connection closes after its response");
		close(cgiClosing);
	}

	int malformed = connectToServer();
	expect(malformed >= 0, "malformed-request client connects");
	pending.clear();
	if (malformed >= 0) {
		expect(sendAll(malformed, "BROKEN\r\n\r\n"), "malformed request is sent");
		expect(takeResponse(malformed, pending, response) && response.find("HTTP/1.1 400 Bad Request") == 0 &&
			countOccurrences(response, "Connection: close\r\n") == 1,
			"malformed request receives one closing error response");
		expect(waitForClose(malformed, pending), "malformed-request connection closes after its error response");
		close(malformed);
	}

	expect(serverRunning(server), "server survives connection lifecycle cases");
	stopServer(server);
	std::remove("./contents/delete-lifecycle-fixture.txt");
	if (failures == 0)
		std::cout << "connection lifecycle tests passed" << std::endl;
	return failures == 0 ? 0 : 1;
}
