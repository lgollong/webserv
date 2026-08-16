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
	const int port = 8080;
	int listen_fd = setupListener(port);
	poller.add(listen_fd, POLLIN);
	logger.debug() << "fd: " << listen_fd << " listening on :" << port;

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
		std::vector<pollfd> &ready_fds = poller.poll();

		for (size_t i = 0; i < ready_fds.size(); i++) {
			if (ready_fds[i].revents == 0)
				continue;
			if (ready_fds[i].fd == listen_fd) {
				if (ready_fds[i].revents & POLLIN) {
					logger.debug() << "fd: " << listen_fd << " listener: incoming connection found! accepting...";
					acceptNew(listen_fd);
				}
				continue;
			}

			Connection *conn = fdToConnection[ready_fds[i].fd];
			if (!conn)
				throw std::runtime_error(std::string("no connection object found in map, this should never happen"));

			if (ready_fds[i].fd == conn->txn.cgi.out_fd) {
				logger.debug() << "fd: " << conn->fd << " checking cgi out fd";
				onCgiReadable(*conn);
			}
			else if (ready_fds[i].fd == conn->txn.cgi.in_fd) {
				logger.debug() << "fd: " << conn->fd << " checking cgi in fd";
				onCgiWritable(*conn);
			}
			else if (ready_fds[i].revents & POLLIN) {
				logger.debug() << "fd: " << conn->fd << " checking reading fd";
				onReadable(*conn);
			}
			else if (ready_fds[i].revents & POLLOUT) {
				logger.debug() << "fd: " << conn->fd << " checking writing fd";
				onWritable(*conn);
			}
		}
	}
}

void Worker::acceptNew(int listen_fd) {
	sockaddr_in client_addr;
	socklen_t len = sizeof(client_addr);

	int client_fd = accept(listen_fd, reinterpret_cast<sockaddr*>(&client_addr), &len);
	if (client_fd < 0)
		throw std::runtime_error(std::string("accept: ") + strerror(errno));
	
	if (fcntl(client_fd, F_SETFL, O_NONBLOCK) < 0)
		throw std::runtime_error(std::string("fcntl: ") + strerror(errno));
	poller.add(client_fd, POLLIN);

	Connection connection;
	connection.fd = client_fd;
	connections[client_fd] = connection;
	fdToConnection[client_fd] = &connections[client_fd];

	logger.debug() << "fd: " << client_fd << " accepted from " << inet_ntoa(client_addr.sin_addr);
}

void Worker::onCgiReadable(Connection &conn) {
	if (!cgi.collect(conn.txn.cgi))
		return ;

	logger.debug() << "fd: " << conn.fd << " cgi collection complete. building response...";
	poller.remove(conn.txn.cgi.out_fd);
	close (conn.txn.cgi.out_fd);
	fdToConnection.erase(conn.txn.cgi.out_fd);

	conn.txn.response = cgi.buildResponse(conn.txn.cgi);
	conn.outbuf = http.build(conn.txn.response);
	poller.setEvents(conn.fd, POLLOUT);
}

void Worker::onCgiWritable(Connection &conn) {
	if (!cgi.sendBody(conn.txn.cgi, conn.txn.request.body))
		return ;

	logger.debug() << "fd: " << conn.fd << " cgi body fully sent.";
	poller.remove(conn.txn.cgi.in_fd);
	close (conn.txn.cgi.in_fd);
	fdToConnection.erase(conn.txn.cgi.in_fd);
}

// read logic
// read into inbuffer as long as there is something to read
// loop over inbuffer and check if any full request is in there
// check if its cgi or standard request and process it
void Worker::onReadable(Connection &conn) {
	char buf[4096];
	ssize_t n = read(conn.fd, buf, sizeof(buf));
	if (n < 0)
		throw std::runtime_error(std::string("read: ") + strerror(errno));

	if (n <= 0) {
		logger.debug() << "fd: " << conn.fd << " closing (read returned " << n << ")";
		close(conn.fd);
		poller.remove(conn.fd);
		fdToConnection.erase(conn.fd);
		connections.erase(conn.fd);
		return ;
	}
	logger.debug() << "fd: " << conn.fd << " read " << n << " bytes:\n"
	        << std::string(buf, static_cast<size_t>(n));

	conn.inbuf.append(buf, n);
	ssize_t req_size = http.parse(conn.inbuf, conn.txn.request);
	while (req_size > 0) {
		logger.debug() << "fd: " << conn.fd << " complete request found";
		conn.txn.route = config.route(conn.txn.request);

		if (conn.txn.route.is_cgi == true) {
			logger.debug() << "fd: " << conn.fd << " this is a cgi request";
			conn.txn.cgi = cgi.start(conn.txn.request, conn.txn.route);
			poller.add(conn.txn.cgi.in_fd, POLLOUT);
			poller.add(conn.txn.cgi.out_fd, POLLIN);
			fdToConnection[conn.txn.cgi.in_fd] = &conn;
			fdToConnection[conn.txn.cgi.out_fd] = &conn;
			return ;
		}
		else {
			logger.debug() << "fd: " << conn.fd << " this is a standard request";
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
	if (n < 0)
		throw std::runtime_error(std::string("write: ") + strerror(errno));
	logger.debug() << "fd: " << conn.fd << " wrote " << n << " bytes:\n"
	            << std::string(conn.outbuf.data() + conn.sent, static_cast<size_t>(n));

	conn.sent += n;
	if (conn.sent == conn.outbuf.size()) {
		conn.outbuf.clear();
		conn.sent = 0;
		conn.txn = Transaction();
		poller.setEvents(conn.fd, POLLIN);
	}
}