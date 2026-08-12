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
// @todo whole keep-alive shit isnt handled
// @todo cgi logic incomplete

Worker::Worker(Config& config, Http& http, Cgi& cgi, StaticFile& files, Logger& logger)
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

void Worker::start() {
	// setup listener
	const int port = 8080;
	int listen_fd = setupListener(port);
	poller.add(listen_fd, POLLIN);
	std::cout << "-------------listening on :" << port << " (fd=" << listen_fd << ")" << std::endl;

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
		std::vector<pollfd>& ready_fds = poller.poll();

		for (size_t i = 0; i < ready_fds.size(); i++) {
			if (ready_fds[i].revents == 0)
				continue;
			if (ready_fds[i].revents & POLLIN) {
				if (ready_fds[i].fd == listen_fd) {
					std::cout << "-------------listener: incoming connection found! accepting..." << std::endl;
					acceptNew(listen_fd);
				}
				else {
					std::cout << "-------------reading..." << std::endl;
					onReadable(ready_fds[i].fd);
				}
			}
			else if (ready_fds[i].revents & POLLOUT) {
				std::cout << "-------------writing..." << std::endl;
				onWritable(ready_fds[i].fd);
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

	std::cout << "-------------accepted fd=" << client_fd
	          << " from " << inet_ntoa(client_addr.sin_addr) << std::endl;
}

//read logic
	// setup read buffer
	// read() into buffer
	// if read result = 0
		// close fd
		// remove from poller set
		// delete connections struct
	// if http.parse = true
		// reset connections[].transaction 
		// process request
		// http.build -> response
		// put response into connections[].outbuff
		// add POLLOUT event to that fd
void Worker::onReadable(int client_fd) {
	char buf[4096];
	ssize_t n = read(client_fd, buf, sizeof(buf));
	if (n < 0)
		throw std::runtime_error(std::string("read: ") + strerror(errno));

	if (n <= 0) {
		std::cout << "-------------closing fd=" << client_fd << " (read returned " << n << ")" << std::endl;
		close(client_fd);
		poller.remove(client_fd);
		connections.erase(client_fd);
		return ;
	}
	std::cout << "-------------read " << n << " bytes from fd=" << client_fd << ":\n"
	          << std::string(buf, static_cast<size_t>(n)) << std::endl;

	Connection &conn = connections[client_fd];
	conn.inbuf.append(buf, n);
	if (http.parse(conn.inbuf, conn.txn.request) == true) {
		std::cout << "-------------request complete!" << std::endl;
		conn.txn.route = config.route(conn.txn.request);
		if (conn.txn.route.is_cgi == true) {
			std::cout << "-------------executing cgi" << std::endl;
			conn.txn.cgi = cgi.start(conn.txn.request, conn.txn.route);
			poller.add(conn.txn.cgi.out_fd, POLLIN);
			return ;
		}
		
		std::cout << "-------------serving response" << std::endl;
		// pulling requested content and storing in response data structure
		Content content = files.serve(conn.txn.route, conn.txn.request);
		conn.txn.response.status = content.status;
		conn.txn.response.body = content.body;
  		conn.txn.response.headers["Content-Type"] = content.mime_type;
		conn.outbuf = http.build(conn.txn.response);
		poller.setEvents(client_fd, POLLOUT);
	}
}

// @todo test with response thats too big for one write cycle
// write logic
	// write() outbuff into fd
	// keep track of what has been written and what remains
void Worker::onWritable(int client_fd) {
	Connection &conn = connections[client_fd];
	ssize_t n = write(client_fd, conn.outbuf.data() + conn.sent, conn.outbuf.size() - conn.sent);
	if (n < 0)
		throw std::runtime_error(std::string("write: ") + strerror(errno));
	std::cout << "-------------wrote " << n << " bytes to fd=" << client_fd << ":\n"
	          << std::string(conn.outbuf.data() + conn.sent, static_cast<size_t>(n)) << std::endl;

	conn.sent += n;
	if (conn.sent == conn.outbuf.size()) {
		conn.outbuf.clear();
		conn.sent = 0;
		poller.setEvents(client_fd, POLLIN);
	}
}