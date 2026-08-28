#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <sstream>
#include <string>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static const int kPort = 8080;
static const int kStartupTimeoutMs = 5000;
static const int kClientTimeoutMs = 35000;
static const int kCgiTimeoutMs = 22000;
static const int kCleanupTimeoutMs = 5000;
static const char *kCgiPidFile = "/tmp/webserv-cgi-stall.pid";

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

static bool sendAll(int fd, const std::string &data, int timeoutMs) {
	std::string::size_type sent = 0;
	long long deadline = nowMs() + timeoutMs;
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

static bool readResponse(int fd, int timeoutMs, std::string &response) {
	long long deadline = nowMs() + timeoutMs;
	while (nowMs() < deadline) {
		if (!waitForFd(fd, POLLIN, 200))
			continue;
		char buffer[4096];
		ssize_t result = recv(fd, buffer, sizeof(buffer), 0);
		if (result <= 0)
			return !response.empty();
		response.append(buffer, static_cast<std::string::size_type>(result));
		if (response.find("\r\n\r\n") != std::string::npos)
			return true;
	}
	return false;
}

static bool requestHasStatus(const std::string &request, const std::string &status, int timeoutMs) {
	int fd = connectToServer();
	if (fd < 0)
		return false;
	bool sent = sendAll(fd, request, 1000);
	std::string response;
	bool received = sent && readResponse(fd, timeoutMs, response);
	close(fd);
	return received && response.find(status) == 0;
}

static bool waitForClose(int fd, int timeoutMs) {
	long long deadline = nowMs() + timeoutMs;
	while (nowMs() < deadline) {
		if (!waitForFd(fd, POLLIN, 500))
			continue;
		char byte;
		ssize_t result = recv(fd, &byte, 1, 0);
		if (result == 0)
			return true;
		if (result < 0)
			return false;
	}
	return false;
}

static pid_t readCgiPid() {
	std::ifstream input(kCgiPidFile);
	pid_t pid = -1;
	input >> pid;
	return pid;
}

static bool waitForCgiPid(pid_t &server, pid_t &cgiPid) {
	long long deadline = nowMs() + kCleanupTimeoutMs;
	while (nowMs() < deadline) {
		if (!serverRunning(server))
			return false;
		cgiPid = readCgiPid();
		if (cgiPid > 0)
			return true;
		usleep(100000);
	}
	return false;
}

static bool waitForProcessExit(pid_t &server, pid_t cgiPid) {
	long long deadline = nowMs() + kCleanupTimeoutMs;
	while (nowMs() < deadline) {
		if (!serverRunning(server))
			return false;
		if (kill(cgiPid, 0) < 0 && errno == ESRCH)
			return true;
		usleep(100000);
	}
	return false;
}

int main() {
	unlink(kCgiPidFile);
	pid_t server = startServer();
	expect(server > 0 && waitForServer(server), "server starts on loopback port 8080");
	if (server <= 0) {
		unlink(kCgiPidFile);
		return 1;
	}

	int silentClient = connectToServer();
	int partialClient = connectToServer();
	expect(silentClient >= 0 && partialClient >= 0, "idle clients connect");
	if (partialClient >= 0)
		expect(sendAll(partialClient, "GET / HTTP/1.1\r\nHost: localhost\r\n", 1000),
			"partial request is sent");

	expect(requestHasStatus("GET /files/index.html HTTP/1.1\r\nHost: localhost\r\n\r\n", "HTTP/1.1 200 OK", 3000),
		"normal request succeeds while idle clients are pending");
	if (silentClient >= 0)
		expect(waitForClose(silentClient, kClientTimeoutMs), "silent client expires after idle deadline");
	if (partialClient >= 0)
		expect(waitForClose(partialClient, 2000), "partial request expires after idle deadline");
	if (silentClient >= 0)
		close(silentClient);
	if (partialClient >= 0)
		close(partialClient);

	expect(serverRunning(server), "server survives client timeout cleanup");
	expect(requestHasStatus("GET /files/index.html HTTP/1.1\r\nHost: localhost\r\n\r\n", "HTTP/1.1 200 OK", 3000),
		"server accepts a request after client expiry");

	expect(requestHasStatus("GET /cgi/test.sh?stall HTTP/1.1\r\nHost: localhost\r\n\r\n", "HTTP/1.1 502 Bad Gateway", kCgiTimeoutMs),
		"stalled CGI is terminated and returns 502");
	expect(serverRunning(server), "server survives CGI timeout cleanup");
	expect(requestHasStatus("GET /files/index.html HTTP/1.1\r\nHost: localhost\r\n\r\n", "HTTP/1.1 200 OK", 3000),
		"server accepts a request after CGI expiry");

	unlink(kCgiPidFile);
	int cgiClient = connectToServer();
	expect(cgiClient >= 0, "CGI disconnect client connects");
	if (cgiClient >= 0) {
		expect(sendAll(cgiClient, "GET /cgi/test.sh?stall HTTP/1.1\r\nHost: localhost\r\n\r\n", 1000),
			"CGI disconnect request is sent");
		pid_t cgiPid = -1;
		expect(waitForCgiPid(server, cgiPid), "stalled CGI records its pid");
		close(cgiClient);
		if (cgiPid > 0)
			expect(waitForProcessExit(server, cgiPid), "disconnected CGI child is reaped");
	}

	expect(serverRunning(server), "server survives disconnected CGI cleanup");
	expect(requestHasStatus("GET /files/index.html HTTP/1.1\r\nHost: localhost\r\n\r\n", "HTTP/1.1 200 OK", 3000),
		"server accepts a request after CGI disconnect");

	stopServer(server);
	unlink(kCgiPidFile);
	if (failures == 0)
		std::cout << "resilience integration tests passed" << std::endl;
	return failures == 0 ? 0 : 1;
}
