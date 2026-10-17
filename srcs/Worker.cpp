#include "../headers/Worker.hpp"
#include "../headers/StaticFile.hpp"
#include "../headers/Logger.hpp"
#include "../headers/Cgi.hpp"
#include "../headers/Http.hpp"
#include "../headers/Config.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <iostream>
#include <sstream>
#include <stdexcept>

Worker::Worker(Config &config, Http &http, Cgi &cgi, StaticFile &files, Logger &logger)
: config(config), http(http), cgi(cgi), files(files), logger(logger) {}

Worker::~Worker() {}

static int setupListener(const ServerConfig &server) {
	addrinfo hints;
	std::memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE | AI_NUMERICHOST | AI_NUMERICSERV;

	std::ostringstream port;
	port << server.port;
	addrinfo *address = NULL;
	int status = getaddrinfo(server.host.c_str(), port.str().c_str(), &hints, &address);
	if (status != 0)
		throw std::runtime_error("invalid IPv4 listener address '" + server.host + "': " +
			gai_strerror(status));

	int fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
	if (fd < 0) {
		freeaddrinfo(address);
		throw std::runtime_error(std::string("socket: ") + strerror(errno));
	}

	std::string failure;
	int opt = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
		failure = std::string("setsockopt: ") + strerror(errno);
	else if (bind(fd, address->ai_addr, address->ai_addrlen) < 0)
		failure = std::string("bind: ") + strerror(errno);
	else if (listen(fd, SOMAXCONN) < 0)
		failure = std::string("listen: ") + strerror(errno);
	else if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0)
		failure = std::string("fcntl: ") + strerror(errno);
	freeaddrinfo(address);
	if (!failure.empty()) {
		close(fd);
		throw std::runtime_error(failure);
	}
	return fd;
}

static std::string ipv4ToString(const in_addr &address) {
	unsigned long value = ntohl(address.s_addr);
	std::ostringstream out;
	out << ((value >> 24) & 0xFF) << '.' << ((value >> 16) & 0xFF) << '.'
		<< ((value >> 8) & 0xFF) << '.' << (value & 0xFF);
	return out.str();
}

static void addErrorBody(Config &config, StaticFile &files, Http &http, size_t serverIndex,
	Response &response) {
	if (response.status < 400 || !response.body.empty())
		return ;

	std::string path = config.errorPage(serverIndex, response.status);
	if (!path.empty()) {
		Content content = files.readErrorPage(path);
		if (content.status == 200) {
			response.body = content.body;
			response.headers["Content-Type"] = content.mime_type;
			return ;
		}
	}
	response = http.defaultErrorResponse(response.status);
}

static bool isImplementedMethod(const std::string &method) {
	return method == "GET" || method == "POST" || method == "DELETE";
}

static bool routeAllowsMethod(const Route &route, const std::string &method) {
	return route.allowed_methods.empty() || route.allowed_methods.find(method) != route.allowed_methods.end();
}

// Whether the resolved route has a handler for an implemented method.
// Redirects and the session demo answer any method; otherwise GET and
// DELETE use the static handlers and POST needs an upload store or CGI.
static bool routeHandlesMethod(const Route &route, const std::string &method) {
	if (route.redirect_status != 0 || route.session_demo)
		return true;
	if (method == "POST")
		return route.is_cgi || (route.upload_enabled && !route.upload_store.empty());
	return method == "GET" || method == "DELETE";
}

static bool routeAcceptsMethod(const Route &route, const std::string &method) {
	return routeAllowsMethod(route, method) && routeHandlesMethod(route, method);
}

static std::string allowedMethodsValue(const Route &route) {
	static const char *const methods[] = {"DELETE", "GET", "POST"};
	std::string value;
	for (size_t i = 0; i < sizeof(methods) / sizeof(methods[0]); ++i) {
		if (!routeAcceptsMethod(route, methods[i]))
			continue;
		if (!value.empty())
			value += ", ";
		value += methods[i];
	}
	return value;
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

static bool hasConnectionOption(const Request &request, const char *option) {
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
		if (equalsIgnoreCase(value.substr(first, last - first), option))
			return true;
		if (end == value.size())
			break;
		start = end + 1;
	}
	return false;
}

static bool isHttp10(const Request &request) {
	return request.version == "HTTP/1.0";
}

// HTTP/1.1 connections persist unless the client sends "close"; HTTP/1.0
// connections close unless the client asks for "keep-alive".
static bool requestWantsClose(const Request &request) {
	if (hasConnectionOption(request, "close"))
		return true;
	return isHttp10(request) && !hasConnectionOption(request, "keep-alive");
}

static void applyConnectionPolicy(const Connection &conn, Response &response) {
	for (std::map<std::string, std::string>::iterator it = response.headers.begin(); it != response.headers.end(); ) {
		std::map<std::string, std::string>::iterator current = it++;
		if (equalsIgnoreCase(current->first, "connection"))
			response.headers.erase(current);
	}
	if (conn.close_after_write)
		response.headers["Connection"] = "close";
	else if (isHttp10(conn.txn.request))
		response.headers["Connection"] = "keep-alive";
}

static const int kPollTimeoutMs = 1000;
static const time_t kClientIdleTimeoutSeconds = 30;
static const time_t kCgiTimeoutSeconds = 15;
static const time_t kCgiTerminationGraceSeconds = 2;
// A running CGI cannot parse the next request until its response is complete.
// Bound that deferred input so a client cannot grow one connection's buffer
// without limit during the CGI timeout window.
static const size_t kMaxDeferredClientBytes = 16 * 1024 * 1024;
// Static files are streamed from disk in chunks of this size, refilled each
// time the client socket has drained the previous chunk.
static const size_t kFileChunkBytes = 64 * 1024;

void Worker::run() {
	std::map<int, size_t> listeners;
	const std::vector<ServerConfig> &servers = config.servers();
	if (servers.empty())
		throw std::runtime_error("configuration has no servers");
	for (size_t index = 0; index < servers.size(); ++index) {
		int listenFd = setupListener(servers[index]);
		poller.add(listenFd, POLLIN);
		listeners[listenFd] = index;
		logger.debug() << "Worker: " << "fd: " << listenFd << " listening on :" << servers[index].port;
	}

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
		// Per-event failures are handled in dispatchReadyEvents(); this guard
		// keeps the loop alive if the snapshot or the sweep itself fails.
		try {
			std::vector<pollfd> ready_fds = poller.events();

			dispatchReadyEvents(ready_fds, listeners, servers);
			sweepExpiredConnections();
		}
		catch (const std::exception &e) {
			try {
				logger.error(std::string("event loop iteration failed: ") + e.what());
			}
			catch (...) {}
		}
	}
}

// One failing event (for example std::bad_alloc while building a response)
// closes only the connection it belongs to instead of ending the loop.
void Worker::dispatchReadyEvents(const std::vector<pollfd> &readyFds,
	std::map<int, size_t> &listeners, const std::vector<ServerConfig> &servers) {
	for (size_t i = 0; i < readyFds.size(); i++) {
		try {
			dispatchEvent(readyFds[i], listeners, servers);
		}
		catch (const std::exception &e) {
			try {
				logger.error(std::string("event handling failed: ") + e.what());
			}
			catch (...) {}
			std::map<int, Connection*>::iterator failed = fdToConnection.find(readyFds[i].fd);
			if (failed != fdToConnection.end())
				closeConnection(*failed->second);
		}
	}
}

void Worker::dispatchEvent(const pollfd &event, std::map<int, size_t> &listeners,
	const std::vector<ServerConfig> &servers) {
	if (event.revents == 0)
		return ;
	std::map<int, size_t>::iterator listener = listeners.find(event.fd);
	if (listener != listeners.end()) {
		if (event.revents & (POLLERR | POLLHUP | POLLNVAL)) {
			logger.error("listener received a poll error event");
			int failedFd = listener->first;
			size_t serverIndex = listener->second;
			poller.remove(failedFd);
			close(failedFd);
			listeners.erase(listener);
			try {
				int replacementFd = setupListener(servers[serverIndex]);
				poller.add(replacementFd, POLLIN);
				listeners[replacementFd] = serverIndex;
			}
			catch (const std::exception &e) {
				logger.error(e.what());
				return ;
			}
			return ;
		}
		if (event.revents & POLLIN)
			acceptNew(event.fd, listener->second);
		return ;
	}

	std::map<int, Connection*>::iterator found = fdToConnection.find(event.fd);
	if (found == fdToConnection.end()) {
		poller.remove(event.fd);
		return ;
	}
	Connection *conn = found->second;

	if (event.fd == conn->txn.cgi.out_fd) {
		logger.debug() << "Worker: " << "fd: " << conn->fd << " checking cgi out fd";
		if (event.revents & (POLLERR | POLLNVAL))
			failCgiJob(*conn);
		else if (event.revents & (POLLIN | POLLHUP))
			onCgiReadable(*conn);
	}
	else if (event.fd == conn->txn.cgi.in_fd) {
		logger.debug() << "Worker: " << "fd: " << conn->fd << " checking cgi in fd";
		if (event.revents & (POLLERR | POLLHUP | POLLNVAL))
			failCgiJob(*conn);
		else if (event.revents & POLLOUT)
			onCgiWritable(*conn);
	}
	else {
		if (event.revents & (POLLERR | POLLNVAL)) {
			closeConnection(*conn);
			return ;
		}
		if (event.revents & POLLIN) {
			logger.debug() << "Worker: " << "fd: " << conn->fd << " checking reading fd";
			onReadable(*conn);
		}
		else if (event.revents & POLLOUT) {
			logger.debug() << "Worker: " << "fd: " << conn->fd << " checking writing fd";
			onWritable(*conn);
		}

		// macOS reports a client's half-close (shutdown(SHUT_WR)) as
		// POLLIN|POLLHUP while the socket stays writable. Keep reading
		// until EOF instead of closing on POLLHUP, and let a queued
		// response flush; the write result decides if the peer is gone.
		if ((event.revents & POLLHUP) && !(event.revents & POLLIN)) {
			std::map<int, Connection*>::iterator current = fdToConnection.find(event.fd);
			if (current != fdToConnection.end() && current->second->phase != WRITING)
				closeConnection(*current->second);
		}
	}
}

void Worker::acceptNew(int listen_fd, size_t serverIndex) {
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
	connection.server_index = serverIndex;
	connection.phase = READING;
	connection.last_activity = time(NULL);
	connection.remote_addr = ipv4ToString(client_addr.sin_addr);
	connections[client_fd] = connection;
	fdToConnection[client_fd] = &connections[client_fd];

	logger.debug() << "Worker: " << "fd: " << client_fd << " accepted from " << connection.remote_addr;
}

void Worker::onCgiReadable(Connection &conn) {
	if (!cgi.collect(conn.txn.cgi))
		return ;
	if (conn.txn.cgi.failed) {
		// Read error or output above Cgi::MAX_OUTPUT_BYTES: stop the child.
		failCgiJob(conn);
		return ;
	}

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
	conn.txn.response.status = status;
	addErrorBody(config, files, http, conn.server_index, conn.txn.response);
	conn.keep_alive = false;
	conn.close_after_write = true;
	applyConnectionPolicy(conn, conn.txn.response);
	conn.outbuf = http.build(conn.txn.response);
	conn.sent = 0;
	conn.phase = WRITING;
	poller.setEvents(conn.fd, POLLOUT);
}

void Worker::queueSessionResponse(Connection &conn) {
	Session *session = sessions.fromRequest(conn.txn.request, time(NULL));
	bool created = false;
	if (session == NULL) {
		session = sessions.create(time(NULL));
		created = true;
	}

	if (session == NULL) {
		conn.txn.response.status = 500;
		addErrorBody(config, files, http, conn.server_index, conn.txn.response);
	}
	else {
		++session->visits;
		conn.txn.response.status = 200;
		conn.txn.response.headers["Content-Type"] = "text/plain";
		std::ostringstream visits;
		visits << session->visits;
		conn.txn.response.body = "Session visits: " + visits.str() + "\n";
		if (created)
			conn.txn.response.headers["Set-Cookie"] = SessionStore::setCookie(*session);
	}
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
	ssize_t req_size = http.parse(conn.inbuf, conn.txn.request, config.bodyLimit(conn.server_index), parseStatus);
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
	conn.txn.route = config.route(conn.server_index, conn.txn.request);

	if (!isImplementedMethod(conn.txn.request.method)) {
		logger.debug() << "Worker: " << "fd: " << conn.fd << " method not implemented "
			<< conn.txn.request.method;
		conn.txn.response.status = 501;
		addErrorBody(config, files, http, conn.server_index, conn.txn.response);
		applyConnectionPolicy(conn, conn.txn.response);
		conn.outbuf = http.build(conn.txn.response);
		conn.sent = 0;
		conn.phase = WRITING;
		poller.setEvents(conn.fd, POLLOUT);
		return ;
	}

	if (!routeAcceptsMethod(conn.txn.route, conn.txn.request.method)) {
		logger.debug() << "Worker: " << "fd: " << conn.fd << " rejected method "
			<< conn.txn.request.method;
		conn.txn.response.status = 405;
		addErrorBody(config, files, http, conn.server_index, conn.txn.response);
		conn.txn.response.headers["Allow"] = allowedMethodsValue(conn.txn.route);
		applyConnectionPolicy(conn, conn.txn.response);
		conn.outbuf = http.build(conn.txn.response);
		conn.sent = 0;
		conn.phase = WRITING;
		poller.setEvents(conn.fd, POLLOUT);
		return ;
	}

	if (conn.txn.request.method == "DELETE") {
		logger.debug() << "Worker: " << "fd: " << conn.fd << " deleting a static file";
		Content content = files.erase(conn.txn.route, conn.txn.request);
		conn.txn.response.status = content.status;
		conn.txn.response.body = content.body;
		conn.txn.response.headers["Content-Type"] = content.mime_type;
		addErrorBody(config, files, http, conn.server_index, conn.txn.response);
		applyConnectionPolicy(conn, conn.txn.response);
		conn.outbuf = http.build(conn.txn.response);
		conn.sent = 0;
		conn.phase = WRITING;
		poller.setEvents(conn.fd, POLLOUT);
		return ;
	}
	if (conn.txn.request.method == "POST" && conn.txn.route.upload_enabled &&
		!conn.txn.route.upload_store.empty()) {
		Content content = files.upload(conn.txn.route, conn.txn.request);
		conn.txn.response.status = content.status;
		conn.txn.response.body = content.body;
		conn.txn.response.headers["Content-Type"] = content.mime_type;
		if (!content.location.empty())
			conn.txn.response.headers["Location"] = content.location;
		addErrorBody(config, files, http, conn.server_index, conn.txn.response);
		applyConnectionPolicy(conn, conn.txn.response);
		conn.outbuf = http.build(conn.txn.response);
		conn.sent = 0;
		conn.phase = WRITING;
		poller.setEvents(conn.fd, POLLOUT);
		return ;
	}
	if (conn.txn.route.session_demo) {
		logger.debug() << "Worker: " << "fd: " << conn.fd << " handling session demonstration";
		queueSessionResponse(conn);
		return ;
	}

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
		std::vector<int> serverFds;
		const std::vector<pollfd> &managed = poller.events();
		for (std::vector<pollfd>::const_iterator it = managed.begin(); it != managed.end(); ++it)
			serverFds.push_back(it->fd);
		conn.txn.cgi = cgi.start(conn.txn.request, conn.txn.route,
			config.servers()[conn.server_index], conn.remote_addr, serverFds);
		if (conn.txn.cgi.failed) {
			failCgiJob(conn);
			return ;
		}

		conn.phase = RUNNING_CGI;
		// Keep watching the client while CGI runs so disconnects are detected and
		// additional request bytes can be buffered without starting a new
		// transaction before the current CGI response is complete.
		poller.setEvents(conn.fd, POLLIN);
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
	addErrorBody(config, files, http, conn.server_index, conn.txn.response);
	applyConnectionPolicy(conn, conn.txn.response);
	if (content.file_fd >= 0) {
		// Send the head now; onWritable() streams the file body after it.
		conn.body_fd = content.file_fd;
		conn.body_remaining = content.file_size;
		conn.outbuf = http.buildHead(conn.txn.response, content.file_size);
	}
	else
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
	if (logger.enabled(LOG_DEBUG))
		logger.debug() << "Worker: " << "fd: " << conn.fd << " read " << n << " bytes:\n"
			<< std::string(buf, static_cast<size_t>(n));

	conn.inbuf.append(buf, n);
	conn.last_activity = time(NULL);
	if (conn.phase == RUNNING_CGI && conn.inbuf.size() > kMaxDeferredClientBytes) {
		logger.debug() << "Worker: " << "fd: " << conn.fd
			<< " closing after deferred CGI input exceeded its limit";
		closeConnection(conn);
		return ;
	}
	if (conn.phase == READING) {
		processBufferedRequest(conn);
		return ;
	}
	// A client may send a pipelined or fragmented next request while CGI is
	// running. Keep those bytes in the connection buffer, but never replace the
	// active transaction. The buffered request is processed after the current
	// response has been fully written.
}

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
	if (logger.enabled(LOG_DEBUG))
		logger.debug() << "Worker: " << "fd: " << conn.fd << " wrote " << n << " bytes:\n"
			<< std::string(conn.outbuf.data() + conn.sent, static_cast<size_t>(n));

	conn.sent += n;
	conn.last_activity = time(NULL);
	if (conn.sent < conn.outbuf.size())
		return ;
	if (conn.body_fd >= 0) {
		if (!refillFromBodyFile(conn)) {
			logger.debug() << "Worker: " << "fd: " << conn.fd << " streamed file ended early";
			closeConnection(conn);
			return ;
		}
		if (!conn.outbuf.empty())
			return ;
	}
	finishClientResponse(conn);
}

// Loads the next chunk of a streamed file into outbuf. Regular disk files are
// exempt from poll() readiness. Returns false if the file ended before the
// announced Content-Length, which leaves closing as the only valid framing.
bool Worker::refillFromBodyFile(Connection &conn) {
	conn.outbuf.clear();
	conn.sent = 0;
	if (conn.body_remaining > 0) {
		char chunk[kFileChunkBytes];
		size_t wanted = conn.body_remaining < sizeof(chunk) ? conn.body_remaining : sizeof(chunk);
		ssize_t count = read(conn.body_fd, chunk, wanted);
		if (count <= 0)
			return false;
		conn.outbuf.assign(chunk, static_cast<size_t>(count));
		conn.body_remaining -= static_cast<size_t>(count);
	}
	if (conn.body_remaining == 0) {
		close(conn.body_fd);
		conn.body_fd = -1;
	}
	return true;
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
	sessions.sweep(now);
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
	// A syntactically valid Status the server cannot serialize is still a bad
	// upstream response.
	if (!http.supportsStatus(conn.txn.response.status)) {
		conn.txn.response = Response();
		conn.txn.response.status = 502;
	}
	addErrorBody(config, files, http, conn.server_index, conn.txn.response);
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
	if (conn.body_fd >= 0) {
		close(conn.body_fd);
		conn.body_fd = -1;
	}
	releaseCgiJob(conn);
	closeManagedFd(conn.fd);
	connections.erase(client_fd);
}
