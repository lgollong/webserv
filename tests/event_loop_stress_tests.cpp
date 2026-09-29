#include "../headers/Worker.hpp"

#include <cstring>
#include <iostream>
#include <poll.h>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>

static int failures = 0;

static void expect(bool condition, const char *message) {
	if (condition)
		return;
	std::cerr << "failure: " << message << std::endl;
	++failures;
}

static bool setNonBlocking(int fd) {
	int flags = fcntl(fd, F_GETFL, 0);
	return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static bool waitForWritable(int fd) {
	pollfd event;
	event.fd = fd;
	event.events = POLLOUT;
	event.revents = 0;
	return poll(&event, 1, 1000) > 0 && (event.revents & POLLOUT);
}

static void drainReadable(int fd, std::string &received) {
	while (true) {
		pollfd event;
		event.fd = fd;
		event.events = POLLIN;
		event.revents = 0;
		if (poll(&event, 1, 0) <= 0 || !(event.revents & POLLIN))
			return;
		char buffer[4096];
		ssize_t count = read(fd, buffer, sizeof(buffer));
		if (count <= 0)
			return;
		received.append(buffer, static_cast<size_t>(count));
	}
}

class WorkerEventLoopTests {
	public:
		static bool clientErrorClosesOnlyClient(short revents) {
			int sockets[2] = {-1, -1};
			if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0)
				return false;

			std::ostringstream log;
			Config config("ignored-by-reference-mock");
			Http http;
			Cgi cgi;
			StaticFile files;
			Logger logger(log, log, ERROR);
			Worker worker(config, http, cgi, files, logger);

			Connection connection;
			connection.fd = sockets[0];
			connection.phase = READING;
			worker.connections[sockets[0]] = connection;
			worker.fdToConnection[sockets[0]] = &worker.connections[sockets[0]];
			worker.poller.add(sockets[0], POLLIN);

			std::vector<pollfd> ready(1);
			ready[0].fd = sockets[0];
			ready[0].events = POLLIN;
			ready[0].revents = revents;
			std::map<int, size_t> listeners;
			worker.dispatchReadyEvents(ready, listeners, config.servers());

			bool cleaned = worker.connections.empty() && worker.fdToConnection.empty() &&
				worker.poller.events().empty();
			close(sockets[1]);
			return cleaned;
		}

		static bool cgiErrorQueuesResponse(short revents) {
			int sockets[2] = {-1, -1};
			int pipeFds[2] = {-1, -1};
			if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0)
				return false;
			if (pipe(pipeFds) != 0) {
				close(sockets[0]);
				close(sockets[1]);
				return false;
			}

			std::ostringstream log;
			Config config("ignored-by-reference-mock");
			Http http;
			Cgi cgi;
			StaticFile files;
			Logger logger(log, log, ERROR);
			Worker worker(config, http, cgi, files, logger);

			Connection connection;
			connection.fd = sockets[0];
			connection.phase = RUNNING_CGI;
			connection.txn.cgi.out_fd = pipeFds[0];
			worker.connections[sockets[0]] = connection;
			Connection *stored = &worker.connections[sockets[0]];
			worker.fdToConnection[sockets[0]] = stored;
			worker.fdToConnection[pipeFds[0]] = stored;
			worker.poller.add(sockets[0], POLLIN);
			worker.poller.add(pipeFds[0], POLLIN);

			std::vector<pollfd> ready(1);
			ready[0].fd = pipeFds[0];
			ready[0].events = POLLIN;
			ready[0].revents = revents;
			std::map<int, size_t> listeners;
			worker.dispatchReadyEvents(ready, listeners, config.servers());

			std::map<int, Connection>::iterator found = worker.connections.find(sockets[0]);
			bool queued = found != worker.connections.end() && found->second.phase == WRITING &&
				found->second.txn.response.status == 502 && found->second.txn.cgi.out_fd == -1 &&
				worker.fdToConnection.find(pipeFds[0]) == worker.fdToConnection.end() &&
				worker.fdToConnection.find(sockets[0]) != worker.fdToConnection.end() &&
				worker.poller.events().size() == 1 && worker.poller.events()[0].fd == sockets[0] &&
				worker.poller.events()[0].events == POLLOUT;
			if (found != worker.connections.end())
				worker.closeConnection(found->second);
			close(pipeFds[1]);
			close(sockets[1]);
			return queued;
		}

		static bool shortWriteResumesUntilComplete() {
			int sockets[2] = {-1, -1};
			if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0)
				return false;
			if (!setNonBlocking(sockets[0]) || !setNonBlocking(sockets[1])) {
				close(sockets[0]);
				close(sockets[1]);
				return false;
			}
			int sendBuffer = 1024;
			if (setsockopt(sockets[0], SOL_SOCKET, SO_SNDBUF, &sendBuffer, sizeof(sendBuffer)) != 0) {
				close(sockets[0]);
				close(sockets[1]);
				return false;
			}

			std::ostringstream log;
			Config config("ignored-by-reference-mock");
			Http http;
			Cgi cgi;
			StaticFile files;
			Logger logger(log, log, ERROR);
			Worker worker(config, http, cgi, files, logger);

			const std::string payload(131072, 'w');
			Connection connection;
			connection.fd = sockets[0];
			connection.phase = WRITING;
			connection.outbuf = payload;
			worker.connections[sockets[0]] = connection;
			Connection &stored = worker.connections[sockets[0]];
			worker.fdToConnection[sockets[0]] = &stored;
			worker.poller.add(sockets[0], POLLOUT);

			if (!waitForWritable(sockets[0])) {
				worker.closeConnection(stored);
				close(sockets[1]);
				return false;
			}
			worker.onWritable(stored);
			bool wasShort = stored.phase == WRITING && stored.sent > 0 && stored.sent < payload.size();

			std::string received;
			for (int attempts = 0; stored.phase == WRITING && attempts < 256; ++attempts) {
				drainReadable(sockets[1], received);
				if (!waitForWritable(sockets[0]))
					break;
				worker.onWritable(stored);
			}
			drainReadable(sockets[1], received);

			bool complete = stored.phase == READING && received == payload;
			std::map<int, Connection>::iterator remaining = worker.connections.find(sockets[0]);
			if (remaining != worker.connections.end())
				worker.closeConnection(remaining->second);
			close(sockets[1]);
			return wasShort && complete;
		}
};

static bool pollerReportsInvalidFd() {
	int pipeFds[2] = {-1, -1};
	if (pipe(pipeFds) != 0)
		return false;
	Poller poller;
	poller.add(pipeFds[0], POLLIN);
	close(pipeFds[0]);
	int ready = poller.poll(0);
	bool invalid = ready == 1 && poller.events().size() == 1 &&
		(poller.events()[0].revents & POLLNVAL);
	close(pipeFds[1]);
	return invalid;
}

int main() {
	expect(pollerReportsInvalidFd(), "poller reports POLLNVAL for a closed registered descriptor");
	expect(WorkerEventLoopTests::clientErrorClosesOnlyClient(POLLERR),
		"client POLLERR removes only the managed client state");
	expect(WorkerEventLoopTests::clientErrorClosesOnlyClient(POLLHUP),
		"client POLLHUP removes only the managed client state");
	expect(WorkerEventLoopTests::clientErrorClosesOnlyClient(POLLNVAL),
		"client POLLNVAL removes only the managed client state");
	expect(WorkerEventLoopTests::cgiErrorQueuesResponse(POLLERR),
		"CGI POLLERR queues a controlled 502 response");
	expect(WorkerEventLoopTests::cgiErrorQueuesResponse(POLLNVAL),
		"CGI POLLNVAL queues a controlled 502 response");
	expect(WorkerEventLoopTests::shortWriteResumesUntilComplete(),
		"short client writes retain their cursor until every byte is delivered");

	if (failures == 0)
		std::cout << "event loop stress tests passed" << std::endl;
	return failures == 0 ? 0 : 1;
}
