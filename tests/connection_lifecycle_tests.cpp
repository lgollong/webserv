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

static const int kPort = 8002;
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

static bool serverRunning(pid_t pid) {
	if (pid <= 0)
		return false;
	int status = 0;
	return waitpid(pid, &status, WNOHANG) == 0;
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

static int connectToServer(int port = kPort) {
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;

	sockaddr_in address;
	std::memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons(port);
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static bool waitForServer(pid_t server) {
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
	char *argv[] = {const_cast<char *>("./webserv"), const_cast<char *>("./config/req.config"), NULL};
	execv(argv[0], argv);
	_exit(127);
}

static void stopServer(pid_t &server) {
	if (server <= 0)
		return;
	kill(server, SIGTERM);
	long long deadline = nowMs() + 2000;
	while (nowMs() < deadline) {
		if (!serverRunning(server)) {
			server = -1;
			return;
		}
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

static std::string requestWithMethod(const std::string &method, const std::string &path, const std::string &connection) {
	std::string value = method + " " + path + " HTTP/1.1\r\nHost: localhost\r\n";
	if (!connection.empty())
		value += "Connection: " + connection + "\r\n";
	return value + "\r\n";
}

static std::string oversizedRequest() {
	return "POST /upload/too-large.txt HTTP/1.1\r\nHost: localhost\r\nContent-Length: 11000000\r\n\r\n";
}

static std::string uploadRequest(const std::string &path, const std::string &body) {
	std::ostringstream length;
	length << body.size();
	return "POST " + path + " HTTP/1.1\r\nHost: localhost\r\nContent-Length: " + length.str() +
		"\r\n\r\n" + body;
}

static bool storedBodyMatches(const std::string &path, const std::string &expected) {
	int fd = open(path.c_str(), O_RDONLY);
	if (fd < 0)
		return false;
	char buffer[256];
	ssize_t count = read(fd, buffer, sizeof(buffer));
	close(fd);
	return count == static_cast<ssize_t>(expected.size()) &&
		std::string(buffer, static_cast<size_t>(count)) == expected;
}

static bool fileDoesNotExist(const std::string &path) {
	return access(path.c_str(), F_OK) != 0;
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
	expect(started, "server starts on loopback port 8002");
	if (!started) {
		stopServer(server);
		std::remove("./contents/delete-lifecycle-fixture.txt");
		return 1;
	}

	std::string pending;
	std::string response;
	int primaryRoot = connectToServer();
	expect(primaryRoot >= 0, "primary configured listener accepts a root client");
	if (primaryRoot >= 0) {
		expect(sendAll(primaryRoot, request("/", "")), "primary root request is sent");
		expect(takeResponse(primaryRoot, pending, response) && response.find("HTTP/1.1 200 OK") == 0 &&
			response.find("browser-served project dashboard") != std::string::npos,
			"primary listener resolves the primary server root");
		close(primaryRoot);
	}

	int secondary = connectToServer(8003);
	expect(secondary >= 0, "secondary configured listener accepts the same root path");
	pending.clear();
	if (secondary >= 0) {
		expect(sendAll(secondary, request("/", "")), "secondary listener request is sent");
		expect(takeResponse(secondary, pending, response) && response.find("HTTP/1.1 200 OK") == 0 &&
			response.find("browser-served project dashboard") != std::string::npos,
			"same root path resolves to the secondary server root");
		expect(sendAll(secondary, request("/missing.txt", "")), "secondary missing-file request is sent");
		expect(takeResponse(secondary, pending, response) && response.find("HTTP/1.1 404 Not Found") == 0 &&
			response.find("Primary custom 404") != std::string::npos,
			"secondary listener serves its configured 404 page");
		close(secondary);
	}

	int inherited = connectToServer(8008);
	expect(inherited >= 0, "inherited-root listener accepts a client");
	pending.clear();
	if (inherited >= 0) {
		expect(sendAll(inherited, request("/inherited/index.html", "")),
			"inherited-root request is sent");
		expect(takeResponse(inherited, pending, response) && response.find("HTTP/1.1 200 OK") == 0 &&
			response.find("<h1>HI</h1>") != std::string::npos,
			"location without root serves from its configured server root");
		close(inherited);
	}

	const std::string disabledUploadPath = "./contents/uploads/disabled-upload.txt";
	std::remove(disabledUploadPath.c_str());
	int disabledUpload = connectToServer(8008);
	expect(disabledUpload >= 0, "disabled-upload listener accepts a client");
	pending.clear();
	if (disabledUpload >= 0) {
		expect(sendAll(disabledUpload,
			uploadRequest("/uploads-disabled/disabled-upload.txt", "must not be stored")),
			"disabled-upload request is sent");
		expect(takeResponse(disabledUpload, pending, response) &&
			response.find("HTTP/1.1 404 Not Found") == 0 && fileDoesNotExist(disabledUploadPath),
			"upload off prevents a write even with upload_store configured");
		close(disabledUpload);
	}
	std::remove(disabledUploadPath.c_str());

	int fragmented = connectToServer();
	expect(fragmented >= 0, "fragmented client connects");
	if (fragmented >= 0) {
		expect(sendAll(fragmented, "GET /static/index.html HTTP/1.1\r\nHost: localhost\r\n"),
			"fragmented request prefix is sent");
		expect(!waitForFd(fragmented, POLLIN, 300), "fragmented request receives no early response");
		expect(sendAll(fragmented, "\r\n"), "fragmented request is completed");
		expect(takeResponse(fragmented, pending, response) && response.find("HTTP/1.1 200 OK") == 0 &&
			response.find("<h1>HI</h1>") != std::string::npos,
			"completed fragmented request receives one response");
		close(fragmented);
	}

	int oversized = connectToServer();
	expect(oversized >= 0, "oversized-body client connects");
	pending.clear();
	if (oversized >= 0) {
		expect(sendAll(oversized, oversizedRequest()), "oversized body declaration is sent");
		expect(takeResponse(oversized, pending, response) && response.find("HTTP/1.1 413 Payload Too Large") == 0 &&
			countOccurrences(response, "Connection: close\r\n") == 1,
			"configured body limit rejects an oversized declaration before a handler runs");
		expect(waitForClose(oversized, pending), "oversized-body response closes after flushing");
		close(oversized);
	}

	int persistent = connectToServer();
	expect(persistent >= 0, "persistent client connects");
	pending.clear();
	if (persistent >= 0) {
		expect(sendAll(persistent, request("/static/index.html", "")), "first persistent request is sent");
		expect(takeResponse(persistent, pending, response) && response.find("<h1>HI</h1>") != std::string::npos,
			"first persistent request receives a response");

		expect(sendAll(persistent, request("/static/index.html", "")), "second persistent request is sent");
		expect(takeResponse(persistent, pending, response) && response.find("<h1>HI</h1>") != std::string::npos,
			"second persistent request receives a response");

		expect(sendAll(persistent, requestWithMethod("POST", "/static", "")), "disallowed POST request is sent");
		expect(takeResponse(persistent, pending, response) && response.find("HTTP/1.1 405 Method Not Allowed") == 0 &&
			countOccurrences(response, "Allow: GET\r\n") == 1,
			"disallowed static-route method returns one deterministic Allow header");

		expect(sendAll(persistent, requestWithMethod("DELETE", "/static", "")), "disallowed DELETE request is sent");
		expect(takeResponse(persistent, pending, response) && response.find("HTTP/1.1 405 Method Not Allowed") == 0 &&
			countOccurrences(response, "Allow: GET\r\n") == 1,
			"disallowed DELETE request is rejected before static handling");

		expect(sendAll(persistent, requestWithMethod("GET", "/uploads", "")), "disallowed upload-route GET is sent");
		expect(takeResponse(persistent, pending, response) && response.find("HTTP/1.1 405 Method Not Allowed") == 0 &&
			countOccurrences(response, "Allow: DELETE, POST\r\n") == 1,
			"disallowed upload-route method returns its route policy");

		expect(sendAll(persistent, requestWithMethod("PUT", "/", "")), "disallowed root-route PUT is sent");
		expect(takeResponse(persistent, pending, response) && response.find("HTTP/1.1 405 Method Not Allowed") == 0 &&
			countOccurrences(response, "Allow: DELETE, GET, POST\r\n") == 1,
			"multi-method route returns one deterministically ordered Allow header");

		expect(sendAll(persistent, requestWithMethod("GET", "/redirect-test", "")), "redirect request is sent");
		expect(takeResponse(persistent, pending, response) && response.find("HTTP/1.1 301 Moved Permanently") == 0 &&
			response.find("Location: /") != std::string::npos,
			"configured redirect is framed before static handling");

		expect(sendAll(persistent, request("/static/index.html", "")), "post-redirect request is sent");
		expect(takeResponse(persistent, pending, response) && response.find("<h1>HI</h1>") != std::string::npos,
			"persistent connection serves a request after redirecting");

		expect(sendAll(persistent, requestWithMethod("POST", "/uploads", "")), "allowed upload-route POST is sent");
		expect(takeResponse(persistent, pending, response) && response.find("HTTP/1.1 400 Bad Request") == 0,
			"allowed upload route still validates the target name");

		const std::string uploadBody = "lifecycle upload body";
		expect(sendAll(persistent, uploadRequest("/uploads/lifecycle-upload.txt", uploadBody)),
			"authorized upload request is sent");
		expect(takeResponse(persistent, pending, response) && response.find("HTTP/1.1 201 Created") == 0 &&
			response.find("Content-Length: 0\r\n") != std::string::npos &&
			storedBodyMatches("./contents/uploads/lifecycle-upload.txt", uploadBody),
			"authorized upload stores the complete body");
		expect(sendAll(persistent, uploadRequest("/uploads/lifecycle-upload.txt", uploadBody)),
			"duplicate upload request is sent");
		expect(takeResponse(persistent, pending, response) && response.find("HTTP/1.1 403 Forbidden") == 0,
			"duplicate upload target is not overwritten");

		expect(sendAll(persistent, requestWithMethod("DELETE", "/delete-lifecycle-fixture.txt", "")),
			"permitted DELETE request is sent");
		expect(takeResponse(persistent, pending, response) && response.find("HTTP/1.1 204 No Content") == 0 &&
			response.find("Content-Length: 0\r\n") != std::string::npos,
			"permitted DELETE removes a file with an empty response");

		expect(sendAll(persistent, request("/delete-lifecycle-fixture.txt", "")),
			"post-delete GET request is sent");
		expect(takeResponse(persistent, pending, response) && response.find("HTTP/1.1 404 Not Found") == 0 &&
			response.find("Primary custom 404") != std::string::npos,
			"deleted file uses the primary configured 404 page");

		expect(sendAll(persistent, requestWithMethod("DELETE", "/static", "")), "directory DELETE request is sent");
		expect(takeResponse(persistent, pending, response) && response.find("HTTP/1.1 405 Method Not Allowed") == 0 &&
			countOccurrences(response, "Allow: GET\r\n") == 1,
			"disallowed directory delete is rejected by the route policy");

		expect(sendAll(persistent, request("/static", "")), "autoindex request is sent");
		expect(takeResponse(persistent, pending, response) && response.find("HTTP/1.1 200 OK") == 0 &&
			response.find("Index of /static") != std::string::npos,
			"configured autoindex route receives an HTML directory listing");

		std::string buffered = request("/static/index.html", "") + request("/static", "");
		expect(sendAll(persistent, buffered), "buffered static requests are sent together");
		expect(takeResponse(persistent, pending, response) && response.find("<h1>HI</h1>") != std::string::npos,
			"buffered static request receives its first response");
		expect(takeResponse(persistent, pending, response) && response.find("Index of /static") != std::string::npos,
			"buffered autoindex request receives its distinct response");
		close(persistent);
	}

	int closing = connectToServer();
	expect(closing >= 0, "close-policy client connects");
	pending.clear();
	if (closing >= 0) {
		expect(sendAll(closing, request("/static/index.html", "keep-alive, Close")), "close-policy request is sent");
		expect(takeResponse(closing, pending, response) && countOccurrences(response, "Connection: close\r\n") == 1,
			"close-policy response contains one close header");
		expect(waitForClose(closing, pending), "close-policy connection closes after its response");
		close(closing);
	}

	int redirectClosing = connectToServer();
	expect(redirectClosing >= 0, "redirect close-policy client connects");
	pending.clear();
	if (redirectClosing >= 0) {
		expect(sendAll(redirectClosing, request("/redirect-test", "close")), "redirect close-policy request is sent");
		expect(takeResponse(redirectClosing, pending, response) && response.find("HTTP/1.1 301 Moved Permanently") == 0 &&
			response.find("Location: /\r\n") != std::string::npos &&
			countOccurrences(response, "Connection: close\r\n") == 1,
			"redirect close-policy response contains Location and one close header");
		expect(waitForClose(redirectClosing, pending), "redirect close-policy connection closes after its response");
		close(redirectClosing);
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
	std::remove("./contents/uploads/lifecycle-upload.txt");
	if (failures == 0)
		std::cout << "connection lifecycle tests passed" << std::endl;
	return failures == 0 ? 0 : 1;
}
