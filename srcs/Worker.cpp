#include "../headers/Worker.hpp"
#include "../headers/StaticFile.hpp"
#include "../headers/Logger.hpp"
#include "../headers/Cgi.hpp"
#include "../headers/Http.hpp"
#include "../headers/Config.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <iostream>

// @note is error handling rigorous enough? memset etc. not handled?
// @todo handle malformed requests
// @todo timeout sweep missing (look at DEV_DOC). here we probably need the connection phases
// @note are we following the norm everywhere? e.g. no copy operator implemented etc.
// @todo whole keep-alive shit isnt handled
// @todo cgi logic incomplete

Worker::Worker(Config &config, Http &http, Cgi &cgi, StaticFile &files, Logger &logger)
: config(config), http(http), cgi(cgi), files(files), logger(logger) {}

Worker::~Worker() {}

static int setupListener(int port) {
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		throw std::runtime_error(std::string("socket: ") + strerror(errno));

	int opt = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
		throw std::runtime_error(std::string("setsockopt: ") + strerror(errno));

	sockaddr_in addr;
	std::memset(&addr, 0, sizeof(addr));

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(port);

	if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
		throw std::runtime_error(std::string("bind: ") + strerror(errno));
	if (listen(fd, SOMAXCONN) < 0)
		throw std::runtime_error(std::string("listen: ") + strerror(errno));
		
	if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0)
		throw std::runtime_error(std::string("fcntl: ") + strerror(errno));

	return fd;
}

void Worker::run() {
	// setup listener
	// @todo take port from config
	// @todo server must be able to listen on multiple ports at the same time
	const int port = 8080;
	int listen_fd = setupListener(port);
	poller.add(listen_fd, POLLIN);
	logger.debug() << "Worker: " << "fd: " << listen_fd << " listening on :" << port;

	// loop
		// poll to get ready fds
		// loop over ready fds
			// if ready fd = listen fd && ready fd revents = POLLIN
				// accept new connection
			// else if ready fd revents = POLLIN, POLLHUP, POLLERR
				// read logic
			// else if ready fd revents = POLLOUT
				// write logic
	while (true) {
		int ready_count = poller.poll();
		if (ready_count < 0) {
			logger.error("poll failed");
			continue;
		}
		if (ready_count == 0)
			continue;

		// Callbacks can remove fds, so iterate a stable snapshot of poll results.
		std::vector<pollfd> ready_fds = poller.events();

		for (size_t i = 0; i < ready_fds.size(); i++) {
			if (ready_fds[i].revents == 0)
				continue;
			if (ready_fds[i].fd == listen_fd) {
				if (ready_fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
					logger.error("listener received a poll error event");
					continue;
				}
				if (ready_fds[i].revents & POLLIN) {
					logger.debug() << "Worker: " << "fd: " << listen_fd << " listener: incoming connection found! accepting...";
					acceptNew(listen_fd);
				}
				continue;
			}

			std::map<int, Connection*>::iterator found = fdToConnection.find(ready_fds[i].fd);
			if (found == fdToConnection.end()) {
				poller.remove(ready_fds[i].fd);
				continue;
			}
			Connection *conn = found->second;

			if (ready_fds[i].fd == conn->txn.cgi.out_fd) {
				logger.debug() << "Worker: " << "fd: " << conn->fd << " checking cgi out fd";
				if (ready_fds[i].revents & (POLLERR | POLLNVAL))
					closeConnection(*conn);
				else if (ready_fds[i].revents & (POLLIN | POLLHUP))
					onCgiReadable(*conn);
			}
			else if (ready_fds[i].fd == conn->txn.cgi.in_fd) {
				logger.debug() << "Worker: " << "fd: " << conn->fd << " checking cgi in fd";
				if (ready_fds[i].revents & (POLLERR | POLLHUP | POLLNVAL))
					closeConnection(*conn);
				else if (ready_fds[i].revents & POLLOUT)
					onCgiWritable(*conn);
			}
			else {
				if (ready_fds[i].revents & (POLLERR | POLLNVAL)) {
					closeConnection(*conn);
					continue;
				}
				if (ready_fds[i].revents & POLLIN) {
					logger.debug() << "Worker: " << "fd: " << conn->fd << " checking reading fd";
					onReadable(*conn);
				}
				else if (ready_fds[i].revents & POLLOUT) {
					logger.debug() << "Worker: " << "fd: " << conn->fd << " checking writing fd";
					onWritable(*conn);
				}

				if (ready_fds[i].revents & POLLHUP) {
					std::map<int, Connection*>::iterator current = fdToConnection.find(ready_fds[i].fd);
					if (current != fdToConnection.end())
						closeConnection(*current->second);
				}
			}
		}
	}
}

void Worker::acceptNew(int listen_fd) {
	sockaddr_in client_addr;
	socklen_t len = sizeof(client_addr);

	int client_fd = accept(listen_fd, reinterpret_cast<sockaddr*>(&client_addr), &len);
	if (client_fd < 0) {
		logger.error("accept failed");
		return ;
	}
	
	if (fcntl(client_fd, F_SETFL, O_NONBLOCK) < 0) {
		close(client_fd);
		logger.error("failed to make client socket non-blocking");
		return ;
	}
	poller.add(client_fd, POLLIN);

	Connection connection;
	connection.fd = client_fd;
	connections[client_fd] = connection;
	fdToConnection[client_fd] = &connections[client_fd];

	logger.debug() << "Worker: " << "fd: " << client_fd << " accepted from " << inet_ntoa(client_addr.sin_addr);
}

void Worker::onCgiReadable(Connection &conn) {
	if (!cgi.collect(conn.txn.cgi))
		return ;

	logger.debug() << "Worker: " << "fd: " << conn.fd << " cgi collection complete. building response...";
	closeManagedFd(conn.txn.cgi.out_fd);
	closeManagedFd(conn.txn.cgi.in_fd);

	conn.txn.response = cgi.buildResponse(conn.txn.cgi);
	conn.outbuf = http.build(conn.txn.response);
	poller.setEvents(conn.fd, POLLOUT);
}

void Worker::onCgiWritable(Connection &conn) {
	if (!cgi.sendBody(conn.txn.cgi, conn.txn.request.body))
		return ;

	logger.debug() << "Worker: " << "fd: " << conn.fd << " cgi body fully sent.";
	closeManagedFd(conn.txn.cgi.in_fd);
}

// read logic
// read into inbuffer as long as there is something to read
// loop over inbuffer and check if any full request is in there
// check if its cgi or standard request and process it
void Worker::onReadable(Connection &conn) {
	char buf[4096];
	ssize_t n = read(conn.fd, buf, sizeof(buf));

	if (n <= 0) {
		logger.debug() << "Worker: " << "fd: " << conn.fd << " closing after read result " << n;
		closeConnection(conn);
		return ;
	}
	logger.debug() << "Worker: " << "fd: " << conn.fd << " read " << n << " bytes:\n"
	        << std::string(buf, static_cast<size_t>(n));

	conn.inbuf.append(buf, n);
	ssize_t req_size = http.parse(conn.inbuf, conn.txn.request);
	while (req_size > 0) {
		logger.debug() << "Worker: " << "fd: " << conn.fd << " complete request found";
		conn.txn.route = config.route(conn.txn.request);

		if (conn.txn.route.is_cgi == true) {
			logger.debug() << "Worker: " << "fd: " << conn.fd << " this is a cgi request";
			conn.txn.cgi = cgi.start(conn.txn.request, conn.txn.route);
			if (conn.txn.cgi.failed) {
				conn.txn.response = cgi.buildResponse(conn.txn.cgi);
				conn.outbuf = http.build(conn.txn.response);
				poller.setEvents(conn.fd, POLLOUT);
				return ;
			}
			poller.add(conn.txn.cgi.in_fd, POLLOUT);
			poller.add(conn.txn.cgi.out_fd, POLLIN);
			fdToConnection[conn.txn.cgi.in_fd] = &conn;
			fdToConnection[conn.txn.cgi.out_fd] = &conn;
			return ;
		}
		else {
			logger.debug() << "Worker: " << "fd: " << conn.fd << " this is a standard request";
			// pulling requested content and storing in response data structure
			Content content = files.serve(conn.txn.route, conn.txn.request);
			conn.txn.response.status = content.status;
			conn.txn.response.body = content.body;
			conn.txn.response.headers["Content-Type"] = content.mime_type;
			conn.outbuf += http.build(conn.txn.response);
			poller.setEvents(conn.fd, POLLOUT);
		}

		conn.inbuf.erase(0, static_cast<size_t>(req_size));
		
		req_size = http.parse(conn.inbuf, conn.txn.request);
	}
}

// @todo test with response thats too big for one write cycle
// write logic
	// write() outbuff into fd
	// keep track of what has been written and what remains
void Worker::onWritable(Connection &conn) {
	ssize_t n = write(conn.fd, conn.outbuf.data() + conn.sent, conn.outbuf.size() - conn.sent);
	if (n <= 0) {
		logger.debug() << "Worker: " << "fd: " << conn.fd << " closing after write result " << n;
		closeConnection(conn);
		return ;
	}
	logger.debug() << "Worker: " << "fd: " << conn.fd << " wrote " << n << " bytes:\n"
	            << std::string(conn.outbuf.data() + conn.sent, static_cast<size_t>(n));

	conn.sent += n;
	if (conn.sent == conn.outbuf.size()) {
		conn.outbuf.clear();
		conn.sent = 0;
		conn.txn = Transaction();
		poller.setEvents(conn.fd, POLLIN);
	}
}

void Worker::closeManagedFd(int &fd) {
	if (fd < 0)
		return ;
	poller.remove(fd);
	fdToConnection.erase(fd);
	close(fd);
	fd = -1;
}

void Worker::closeConnection(Connection &conn) {
	int client_fd = conn.fd;
	closeManagedFd(conn.txn.cgi.in_fd);
	closeManagedFd(conn.txn.cgi.out_fd);
	closeManagedFd(conn.fd);
	connections.erase(client_fd);
}
