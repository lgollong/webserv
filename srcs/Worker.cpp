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
#include <cctype>
#include <cstring>
#include <ctime>
#include <iostream>

// @note is error handling rigorous enough? memset etc. not handled?
// @todo handle malformed requests
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

static void addDefaultErrorBody(Http &http, Response &response) {
	if (response.status >= 400 && response.body.empty())
		response = http.defaultErrorResponse(response.status);
}

static bool equalsIgnoreCase(const std::string &value, const char *expected) {
	if (value.size() != std::strlen(expected))
		return false;
	for (std::string::size_type i = 0; i < value.size(); ++i) {
		if (std::tolower(static_cast<unsigned char>(value[i])) !=
			std::tolower(static_cast<unsigned char>(expected[i])))
			return false;
	}
	return true;
}

static bool requestWantsClose(const Request &request) {
	std::map<std::string, std::string>::const_iterator header = request.headers.find("connection");
	if (header == request.headers.end())
		return false;

	const std::string &value = header->second;
	std::string::size_type start = 0;
	while (start < value.size()) {
		std::string::size_type end = value.find(',', start);
		if (end == std::string::npos)
			end = value.size();
		std::string::size_type first = start;
		while (first < end && (value[first] == ' ' || value[first] == '\t'))
			++first;
		std::string::size_type last = end;
		while (last > first && (value[last - 1] == ' ' || value[last - 1] == '\t'))
			--last;
		if (equalsIgnoreCase(value.substr(first, last - first), "close"))
			return true;
		if (end == value.size())
			break;
		start = end + 1;
	}
	return false;
}

static void applyConnectionPolicy(const Connection &conn, Response &response) {
	for (std::map<std::string, std::string>::iterator it = response.headers.begin(); it != response.headers.end(); ) {
		std::map<std::string, std::string>::iterator current = it++;
		if (equalsIgnoreCase(current->first, "connection"))
			response.headers.erase(current);
	}
	if (conn.close_after_write)
		response.headers["Connection"] = "close";
}

static const int kPollTimeoutMs = 1000;
static const time_t kClientIdleTimeoutSeconds = 30;
static const time_t kCgiTimeoutSeconds = 15;
static const time_t kCgiTerminationGraceSeconds = 2;

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
		int ready_count = poller.poll(kPollTimeoutMs);
		if (ready_count < 0) {
			logger.error("poll failed");
			continue;
		}
		if (ready_count == 0) {
			sweepExpiredConnections();
			continue;
		}

		// Callbacks can remove fds, so iterate a stable snapshot of poll results.
		std::vector<pollfd> ready_fds = poller.events();

		for (size_t i = 0; i < ready_fds.size(); i++) {
			if (ready_fds[i].revents == 0)
				continue;
			if (ready_fds[i].fd == listen_fd) {
				if (ready_fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
					logger.error("listener received a poll error event");
					poller.remove(listen_fd);
					close(listen_fd);
					try {
						listen_fd = setupListener(port);
						poller.add(listen_fd, POLLIN);
					}
					catch (const std::exception &e) {
						logger.error(e.what());
						return ;
					}
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
					failCgiJob(*conn);
				else if (ready_fds[i].revents & (POLLIN | POLLHUP))
					onCgiReadable(*conn);
			}
			else if (ready_fds[i].fd == conn->txn.cgi.in_fd) {
				logger.debug() << "Worker: " << "fd: " << conn->fd << " checking cgi in fd";
				if (ready_fds[i].revents & (POLLERR | POLLHUP | POLLNVAL))
					failCgiJob(*conn);
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
		sweepExpiredConnections();
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
	connection.phase = READING;
	connection.last_activity = time(NULL);
	connections[client_fd] = connection;
	fdToConnection[client_fd] = &connections[client_fd];

	logger.debug() << "Worker: " << "fd: " << client_fd << " accepted from " << inet_ntoa(client_addr.sin_addr);
}

void Worker::onCgiReadable(Connection &conn) {
	if (!cgi.collect(conn.txn.cgi))
		return ;

	if (conn.txn.cgi.sent < conn.txn.request.body.size())
		conn.txn.cgi.failed = true;
	closeManagedFd(conn.txn.cgi.out_fd);
	closeManagedFd(conn.txn.cgi.in_fd);
	if (cgi.reap(conn.txn.cgi))
		finishCgiResponse(conn);
}

void Worker::onCgiWritable(Connection &conn) {
	if (!cgi.sendBody(conn.txn.cgi, conn.txn.request.body))
		return ;

	logger.debug() << "Worker: " << "fd: " << conn.fd << " cgi body fully sent.";
	closeManagedFd(conn.txn.cgi.in_fd);
	if (conn.txn.cgi.failed)
		failCgiJob(conn);
}

void Worker::queueParserError(Connection &conn, int status) {
	conn.inbuf.clear();
	conn.txn = Transaction();
	conn.txn.response = http.defaultErrorResponse(status);
	conn.keep_alive = false;
	conn.close_after_write = true;
	applyConnectionPolicy(conn, conn.txn.response);
	conn.outbuf = http.build(conn.txn.response);
	conn.sent = 0;
	conn.phase = WRITING;
	poller.setEvents(conn.fd, POLLOUT);
}

void Worker::processBufferedRequest(Connection &conn) {
	if (conn.phase != READING)
		return ;

	int parseStatus = 0;
	ssize_t req_size = http.parse(conn.inbuf, conn.txn.request, parseStatus);
	if (req_size < 0) {
		logger.debug() << "Worker: " << "fd: " << conn.fd << " parser rejected request with status " << parseStatus;
		queueParserError(conn, parseStatus);
		return ;
	}
	if (req_size == 0)
		return ;

	logger.debug() << "Worker: " << "fd: " << conn.fd << " complete request found";
	conn.inbuf.erase(0, static_cast<size_t>(req_size));
	conn.close_after_write = requestWantsClose(conn.txn.request);
	conn.keep_alive = !conn.close_after_write;
	conn.txn.route = config.route(conn.txn.request);

	if (conn.txn.route.redirect_status >= 300 && conn.txn.route.redirect_status < 400 &&
		!conn.txn.route.redirect_target.empty()) {
		logger.debug() << "Worker: " << "fd: " << conn.fd << " redirecting to "
			<< conn.txn.route.redirect_target;
		conn.txn.response.status = conn.txn.route.redirect_status;
		conn.txn.response.headers["Location"] = conn.txn.route.redirect_target;
		applyConnectionPolicy(conn, conn.txn.response);
		conn.outbuf = http.build(conn.txn.response);
		conn.sent = 0;
		conn.phase = WRITING;
		poller.setEvents(conn.fd, POLLOUT);
		return ;
	}

	if (conn.txn.route.is_cgi == true) {
		logger.debug() << "Worker: " << "fd: " << conn.fd << " this is a cgi request";
		conn.txn.cgi = cgi.start(conn.txn.request, conn.txn.route);
		if (conn.txn.cgi.failed) {
			failCgiJob(conn);
			return ;
		}

		conn.phase = RUNNING_CGI;
		if (conn.txn.request.body.empty())
			closeManagedFd(conn.txn.cgi.in_fd);
		else {
			poller.add(conn.txn.cgi.in_fd, POLLOUT);
			fdToConnection[conn.txn.cgi.in_fd] = &conn;
		}
		poller.add(conn.txn.cgi.out_fd, POLLIN);
		fdToConnection[conn.txn.cgi.out_fd] = &conn;
		return ;
	}

	logger.debug() << "Worker: " << "fd: " << conn.fd << " this is a standard request";
	Content content = files.serve(conn.txn.route, conn.txn.request);
	conn.txn.response.status = content.status;
	conn.txn.response.body = content.body;
	conn.txn.response.headers["Content-Type"] = content.mime_type;
	addDefaultErrorBody(http, conn.txn.response);
	applyConnectionPolicy(conn, conn.txn.response);
	conn.outbuf = http.build(conn.txn.response);
	conn.sent = 0;
	conn.phase = WRITING;
	poller.setEvents(conn.fd, POLLOUT);
}

// A queued response owns the current transaction until its last byte is written.
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
	conn.last_activity = time(NULL);
	conn.phase = READING;
	processBufferedRequest(conn);
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
	conn.last_activity = time(NULL);
	if (conn.sent == conn.outbuf.size())
		finishClientResponse(conn);
}

void Worker::finishClientResponse(Connection &conn) {
	conn.outbuf.clear();
	conn.sent = 0;
	if (conn.close_after_write) {
		closeConnection(conn);
		return ;
	}

	conn.txn = Transaction();
	conn.phase = READING;
	poller.setEvents(conn.fd, POLLIN);
	processBufferedRequest(conn);
}

void Worker::sweepExpiredConnections() {
	const time_t now = time(NULL);
	std::vector<int> expired;
	for (std::map<int, Connection>::const_iterator it = connections.begin(); it != connections.end(); ++it) {
		if (it->second.hasClientTimedOut(now, kClientIdleTimeoutSeconds))
			expired.push_back(it->first);
	}
	for (std::vector<int>::const_iterator it = expired.begin(); it != expired.end(); ++it) {
		std::map<int, Connection>::iterator found = connections.find(*it);
		if (found == connections.end())
			continue;
		logger.debug() << "Worker: " << "fd: " << found->second.fd << " client inactivity timeout";
		closeConnection(found->second);
	}
	sweepCgiJobs(now);
	reapDetachedCgiJobs(now);
}

void Worker::sweepCgiJobs(time_t now) {
	for (std::map<int, Connection>::iterator it = connections.begin(); it != connections.end(); ++it) {
		Connection &conn = it->second;
		if (conn.phase != RUNNING_CGI)
			continue;

		CgiJob &job = conn.txn.cgi;
		if (job.hasTimedOut(now, kCgiTimeoutSeconds) && !job.termination_requested) {
			logger.debug() << "Worker: " << "fd: " << conn.fd << " cgi timeout";
			failCgiJob(conn);
			continue;
		}
		if (job.hasTerminationGraceExpired(now, kCgiTerminationGraceSeconds))
			cgi.forceTerminate(job);
		if (job.pid > 0 && cgi.reap(job) && (job.done || job.failed))
			finishCgiResponse(conn);
		else if (job.pid <= 0 && (job.done || job.failed))
			finishCgiResponse(conn);
	}
}

void Worker::reapDetachedCgiJobs(time_t now) {
	for (std::map<pid_t, time_t>::iterator it = pendingCgiReaps.begin(); it != pendingCgiReaps.end(); ) {
		std::map<pid_t, time_t>::iterator current = it++;
		pid_t pid = current->first;
		if (now >= current->second && now - current->second >= kCgiTerminationGraceSeconds)
			cgi.forceTerminate(pid);
		if (cgi.reap(pid))
			pendingCgiReaps.erase(current);
	}
}

void Worker::finishCgiResponse(Connection &conn) {
	closeManagedFd(conn.txn.cgi.out_fd);
	closeManagedFd(conn.txn.cgi.in_fd);
	conn.txn.response = cgi.buildResponse(conn.txn.cgi);
	addDefaultErrorBody(http, conn.txn.response);
	applyConnectionPolicy(conn, conn.txn.response);
	conn.outbuf = http.build(conn.txn.response);
	conn.phase = WRITING;
	poller.setEvents(conn.fd, POLLOUT);
}

void Worker::failCgiJob(Connection &conn) {
	CgiJob &job = conn.txn.cgi;
	job.failed = true;
	closeManagedFd(job.in_fd);
	closeManagedFd(job.out_fd);
	if (job.pid > 0) {
		cgi.terminate(job);
		if (!cgi.reap(job)) {
			conn.phase = RUNNING_CGI;
			return;
		}
	}
	finishCgiResponse(conn);
}

void Worker::releaseCgiJob(Connection &conn) {
	CgiJob &job = conn.txn.cgi;
	closeManagedFd(job.in_fd);
	closeManagedFd(job.out_fd);
	if (job.pid <= 0)
		return;

	cgi.terminate(job);
	pid_t pid = job.pid;
	if (!cgi.reap(job))
		pendingCgiReaps[pid] = job.termination_requested_at;
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
	releaseCgiJob(conn);
	closeManagedFd(conn.fd);
	connections.erase(client_fd);
}
