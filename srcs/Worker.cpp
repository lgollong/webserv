#include "../headers/Worker.hpp"

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

static const int kPollTimeoutMs = 1000;
static const time_t kClientIdleTimeoutSeconds = 30;
static const time_t kCgiTimeoutSeconds = 15;
static const size_t kMaxDeferredClientBytes = 16 * 1024 * 1024;
static const size_t kFileChunkBytes = 64 * 1024;

Worker::Worker(Config &config, Http &http, Cgi &cgi, StaticFile &files)
: config(config), http(http), cgi(cgi), files(files) {}

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

static std::string lowerCase(std::string value) {
	for (std::string::size_type i = 0; i < value.size(); ++i)
		value[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(value[i])));
	return value;
}

static bool requestWantsClose(const Request &request) {
	std::map<std::string, std::string>::const_iterator header = request.headers.find("connection");
	std::string option;
	if (header != request.headers.end())
		option = lowerCase(header->second);
	return option == "close" || (request.version == "HTTP/1.0" && option != "keep-alive");
}

void Worker::run() {
	const std::vector<ServerConfig> &servers = config.servers();
	for (size_t index = 0; index < servers.size(); ++index) {
		int listenFd = setupListener(servers[index]);
		poller.add(listenFd, POLLIN);
		listeners[listenFd] = index;
	}

	while (true) {
		if (poller.poll(kPollTimeoutMs) > 0) {
			std::vector<pollfd> events = poller.events();
			for (size_t i = 0; i < events.size(); ++i) {
				if (events[i].revents == 0)
					continue;
				try {
					dispatchEvent(events[i]);
				}
				catch (const std::exception &e) {
					std::cerr << "ERROR: " << e.what() << std::endl;
					std::map<int, Connection*>::iterator failed = fdToConnection.find(events[i].fd);
					if (failed != fdToConnection.end())
						closeConnection(*failed->second);
				}
			}
		}
		sweepExpiredConnections();
	}
}

void Worker::dispatchEvent(const pollfd &event) {
	std::map<int, size_t>::iterator listener = listeners.find(event.fd);
	if (listener != listeners.end()) {
		acceptNew(event.fd, listener->second);
		return ;
	}
	std::map<int, Connection*>::iterator found = fdToConnection.find(event.fd);
	if (found == fdToConnection.end())
		return ;
	Connection &conn = *found->second;

	if (event.fd == conn.txn.cgi.out_fd)
		onCgiReadable(conn);
	else if (event.fd == conn.txn.cgi.in_fd) {
		if (event.revents & POLLOUT)
			onCgiWritable(conn);
		else
			closeManagedFd(conn.txn.cgi.in_fd);
	}
	else if (event.revents & (POLLERR | POLLNVAL))
		closeConnection(conn);
	else if (event.revents & POLLIN)
		onReadable(conn);
	else if (event.revents & POLLOUT)
		onWritable(conn);
	else if (event.revents & POLLHUP)
		closeConnection(conn);
}

void Worker::acceptNew(int listen_fd, size_t serverIndex) {
	sockaddr_in client_addr;
	socklen_t len = sizeof(client_addr);
	int client_fd = accept(listen_fd, reinterpret_cast<sockaddr*>(&client_addr), &len);
	if (client_fd < 0)
		return ;
	if (fcntl(client_fd, F_SETFL, O_NONBLOCK) < 0) {
		close(client_fd);
		return ;
	}
	poller.add(client_fd, POLLIN);

	Connection &conn = connections[client_fd];
	conn.fd = client_fd;
	conn.server_index = serverIndex;
	conn.last_activity = time(NULL);
	conn.remote_addr = ipv4ToString(client_addr.sin_addr);
	fdToConnection[client_fd] = &conn;
}

void Worker::onReadable(Connection &conn) {
	char buf[4096];
	ssize_t n = read(conn.fd, buf, sizeof(buf));
	if (n <= 0) {
		closeConnection(conn);
		return ;
	}
	conn.inbuf.append(buf, n);
	conn.last_activity = time(NULL);
	if (conn.phase == RUNNING_CGI && conn.inbuf.size() > kMaxDeferredClientBytes)
		closeConnection(conn);
	else
		processBufferedRequest(conn);
}

void Worker::onWritable(Connection &conn) {
	ssize_t n = write(conn.fd, conn.outbuf.data() + conn.sent, conn.outbuf.size() - conn.sent);
	if (n <= 0) {
		closeConnection(conn);
		return ;
	}
	conn.sent += n;
	conn.last_activity = time(NULL);
	if (conn.sent < conn.outbuf.size())
		return ;

	if (conn.body_fd >= 0) {
		char chunk[kFileChunkBytes];
		size_t wanted = sizeof(chunk);
		if (conn.body_remaining < wanted)
			wanted = conn.body_remaining;
		ssize_t count = read(conn.body_fd, chunk, wanted);
		if (count <= 0) {
			closeConnection(conn);
			return ;
		}
		conn.outbuf.assign(chunk, static_cast<size_t>(count));
		conn.sent = 0;
		conn.body_remaining -= static_cast<size_t>(count);
		if (conn.body_remaining == 0) {
			close(conn.body_fd);
			conn.body_fd = -1;
		}
		return ;
	}
	finishClientResponse(conn);
}

void Worker::onCgiReadable(Connection &conn) {
	if (cgi.collect(conn.txn.cgi))
		finishCgiResponse(conn);
}

void Worker::onCgiWritable(Connection &conn) {
	if (cgi.sendBody(conn.txn.cgi, conn.txn.request.body))
		closeManagedFd(conn.txn.cgi.in_fd);
}

void Worker::processBufferedRequest(Connection &conn) {
	if (conn.phase != READING)
		return ;

	int errorStatus = 0;
	ssize_t size = http.parse(conn.inbuf, conn.txn.request, config.bodyLimit(conn.server_index), errorStatus);
	if (size == 0)
		return ;
	if (size < 0) {
		conn.inbuf.clear();
		conn.close_after_write = true;
		respond(conn, errorStatus);
		return ;
	}
	conn.inbuf.erase(0, static_cast<size_t>(size));
	conn.close_after_write = requestWantsClose(conn.txn.request);
	conn.txn.route = config.route(conn.server_index, conn.txn.request);
	handleRequest(conn);
}

void Worker::handleRequest(Connection &conn) {
	const Request &request = conn.txn.request;
	const Route &route = conn.txn.route;

	if (request.method != "GET" && request.method != "POST" && request.method != "DELETE")
		respond(conn, 501);
	else if (route.allowed_methods.count(request.method) == 0) {
		std::string allow;
		for (std::set<std::string>::const_iterator it = route.allowed_methods.begin(); it != route.allowed_methods.end(); ++it) {
			if (!allow.empty())
				allow += ", ";
			allow += *it;
		}
		conn.txn.response.headers["Allow"] = allow;
		respond(conn, 405);
	}
	else if (route.redirect_status != 0) {
		conn.txn.response.headers["Location"] = route.redirect_target;
		respond(conn, route.redirect_status);
	}
	else if (route.session_demo)
		queueSessionResponse(conn);
	else if (route.is_cgi)
		startCgi(conn);
	else if (request.method == "POST" && route.upload_enabled)
		respondWith(conn, files.upload(route, request));
	else if (request.method == "DELETE")
		respondWith(conn, files.erase(route, request));
	else if (request.method == "GET")
		respondWith(conn, files.serve(route, request));
	else
		respond(conn, 405);
}

void Worker::startCgi(Connection &conn) {
	std::vector<int> serverFds;
	const std::vector<pollfd> &managed = poller.events();
	for (std::vector<pollfd>::const_iterator it = managed.begin(); it != managed.end(); ++it)
		serverFds.push_back(it->fd);
	conn.txn.cgi = cgi.start(conn.txn.request, conn.txn.route,
		config.servers()[conn.server_index], conn.remote_addr, serverFds);
	if (conn.txn.cgi.failed) {
		finishCgiResponse(conn);
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
}

void Worker::respond(Connection &conn, int status) {
	conn.txn.response.status = status;
	queueResponse(conn);
}

void Worker::respondWith(Connection &conn, const Content &content) {
	conn.txn.response.status = content.status;
	conn.txn.response.body = content.body;
	conn.txn.response.headers["Content-Type"] = content.mime_type;
	if (!content.location.empty())
		conn.txn.response.headers["Location"] = content.location;
	conn.body_fd = content.file_fd;
	conn.body_remaining = content.file_size;
	queueResponse(conn);
}

void Worker::queueResponse(Connection &conn) {
	Response &response = conn.txn.response;
	if (response.status >= 400 && response.body.empty()) {
		std::string page = config.errorPage(conn.server_index, response.status);
		Content content;
		if (!page.empty())
			content = files.readErrorPage(page);
		if (content.status == 200) {
			response.body = content.body;
			response.headers["Content-Type"] = content.mime_type;
		}
		else
			response.body = http.defaultErrorResponse(response.status).body;
	}
	if (conn.close_after_write)
		response.headers["Connection"] = "close";
	else if (conn.txn.request.version == "HTTP/1.0")
		response.headers["Connection"] = "keep-alive";

	if (conn.body_fd >= 0)
		conn.outbuf = http.buildHead(response, conn.body_remaining);
	else
		conn.outbuf = http.build(response);
	conn.sent = 0;
	conn.phase = WRITING;
	poller.setEvents(conn.fd, POLLOUT);
}

void Worker::queueSessionResponse(Connection &conn) {
	std::string cookie;
	std::ostringstream body;
	body << "Session visits: " << sessions.visit(conn.txn.request, cookie) << "\n";
	conn.txn.response.headers["Content-Type"] = "text/plain";
	conn.txn.response.body = body.str();
	if (!cookie.empty())
		conn.txn.response.headers["Set-Cookie"] = cookie;
	respond(conn, 200);
}

void Worker::finishCgiResponse(Connection &conn) {
	closeManagedFd(conn.txn.cgi.out_fd);
	closeManagedFd(conn.txn.cgi.in_fd);
	cgi.stop(conn.txn.cgi);
	conn.txn.response = cgi.buildResponse(conn.txn.cgi);
	if (!http.supportsStatus(conn.txn.response.status))
		conn.txn.response = Response();
	if (conn.txn.response.status == 0)
		conn.txn.response.status = 502;
	queueResponse(conn);
}

void Worker::finishClientResponse(Connection &conn) {
	if (conn.close_after_write) {
		closeConnection(conn);
		return ;
	}
	conn.outbuf.clear();
	conn.sent = 0;
	conn.txn = Transaction();
	conn.phase = READING;
	poller.setEvents(conn.fd, POLLIN);
	processBufferedRequest(conn);
}

void Worker::sweepExpiredConnections() {
	const time_t now = time(NULL);
	for (std::map<int, Connection>::iterator it = connections.begin(); it != connections.end(); ) {
		Connection &conn = (it++)->second;
		if (conn.phase == RUNNING_CGI) {
			if (now - conn.txn.cgi.started_at >= kCgiTimeoutSeconds) {
				conn.txn.cgi.failed = true;
				finishCgiResponse(conn);
			}
		}
		else if (now - conn.last_activity >= kClientIdleTimeoutSeconds)
			closeConnection(conn);
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
	if (conn.body_fd >= 0)
		close(conn.body_fd);
	closeManagedFd(conn.txn.cgi.in_fd);
	closeManagedFd(conn.txn.cgi.out_fd);
	cgi.stop(conn.txn.cgi);
	closeManagedFd(conn.fd);
	connections.erase(client_fd);
}
