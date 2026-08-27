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

static const int kPort = 8080;
static const int kStartupTimeoutMs = 5000;
static const int kResponseTimeoutMs = 5000;
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

static bool sendAll(int fd, const std::string &bytes) {
	std::string::size_type sent = 0;
	long long deadline = nowMs() + kResponseTimeoutMs;
	while (sent < bytes.size() && nowMs() < deadline) {
		if (!waitForFd(fd, POLLOUT, 200))
			continue;
		ssize_t result = send(fd, bytes.data() + sent, bytes.size() - sent, 0);
		if (result <= 0)
			return false;
		sent += static_cast<std::string::size_type>(result);
	}
	return sent == bytes.size();
}

static bool parseContentLength(const std::string &response, std::string::size_type headerEnd,
	size_t &length) {
	std::string::size_type start = response.find("Content-Length: ");
	if (start == std::string::npos || start >= headerEnd)
		return false;
	start += std::strlen("Content-Length: ");
	std::string::size_type end = response.find("\r\n", start);
	if (end == std::string::npos || end > headerEnd || start == end)
		return false;
	length = 0;
	for (std::string::size_type i = start; i < end; ++i) {
		if (response[i] < '0' || response[i] > '9')
			return false;
		length = length * 10 + static_cast<size_t>(response[i] - '0');
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

static bool request(const std::string &bytes, std::string &response) {
	int fd = connectToServer();
	if (fd < 0)
		return false;
	bool success = sendAll(fd, bytes) && readResponse(fd, response);
	close(fd);
	return success;
}

static std::string getRequest(const std::string &path, const std::string &cookie) {
	std::string request = "GET " + path + " HTTP/1.1\r\nHost: localhost\r\n";
	if (!cookie.empty())
		request += "Cookie: " + cookie + "\r\n";
	return request + "\r\n";
}

static std::string sessionCookiePair(const std::string &response) {
	std::string::size_type start = response.find("Set-Cookie: ");
	if (start == std::string::npos)
		return "";
	start += std::strlen("Set-Cookie: ");
	std::string::size_type end = response.find(';', start);
	if (end == std::string::npos)
		return "";
	return response.substr(start, end - start);
}

int main() {
	pid_t server = startServer();
	bool started = server > 0 && waitForServer(server);
	expect(started, "server starts on loopback port 8080");
	if (!started) {
		stopServer(server);
		return 1;
	}

	std::string first;
	expect(request(getRequest("/session", ""), first) && first.find("HTTP/1.1 200 OK") == 0 &&
		first.find("Session visits: 1\n") != std::string::npos,
		"first session request receives observable initial state");
	std::string cookie = sessionCookiePair(first);
	expect(cookie.compare(0, std::strlen("webserv_session="), "webserv_session=") == 0 &&
		first.find("HttpOnly") != std::string::npos, "first session request receives one HttpOnly cookie");

	std::string resumed;
	expect(!cookie.empty() && request(getRequest("/session", cookie), resumed) &&
		resumed.find("HTTP/1.1 200 OK") == 0 && resumed.find("Session visits: 2\n") != std::string::npos &&
		resumed.find("Set-Cookie:") == std::string::npos,
		"cookie-bearing request resumes the same session without replacing its cookie");

	std::string invalid;
	expect(request(getRequest("/session", "webserv_session=not-a-token"), invalid) &&
		invalid.find("HTTP/1.1 200 OK") == 0 && invalid.find("Session visits: 1\n") != std::string::npos &&
		!sessionCookiePair(invalid).empty(), "invalid cookie starts a clean session");

	std::string staticResponse;
	expect(serverRunning(server) && request(getRequest("/files/index.html", ""), staticResponse) &&
		staticResponse.find("HTTP/1.1 200 OK") == 0 && staticResponse.find("<h1>HI</h1>") != std::string::npos,
		"session demonstration leaves normal static handling available");

	stopServer(server);
	if (failures == 0)
		std::cout << "cookie session integration tests passed" << std::endl;
	return failures == 0 ? 0 : 1;
}
