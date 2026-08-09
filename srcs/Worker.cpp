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

Worker::Worker(Config& config, Http& http, Cgi& cgi, StaticFile& files, Logger& logger)
: config(config), http(http), cgi(cgi), files(files), logger(logger) {}

Worker::~Worker() {}
void Worker::start() {
	// setup listener
	// loop
		// poll ready fds
		// loop ready fds
			// if ready fd = listen fd && ready fd revents = POLLIN
				// accept new connection
			// else if ready fd revents = POLLIN, POLLHUP, POLLERR
				// read logic
			// else if ready fd revents = POLLOUT
				// write logic
}