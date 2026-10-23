#include "Poller.hpp"

#include <cstddef>

Poller::Poller() {}

Poller::~Poller() {}

int Poller::poll(int timeoutMs) {
	pollfd *first = NULL;
	if (!fds.empty())
		first = &fds[0];
	return ::poll(first, fds.size(), timeoutMs);
}

const std::vector<pollfd>& Poller::events() const {
	return fds;
}

void Poller::add(int fd, short events) {
	pollfd pfd;
	pfd.fd = fd;
	pfd.events = events;
	pfd.revents = 0;
	fds.push_back(pfd);
}

void Poller::remove(int fd) {
	for (std::vector<pollfd>::iterator it = fds.begin(); it != fds.end(); ++it) {
		if (it->fd == fd) {
			fds.erase(it);
			return;
		}
	}
}

void Poller::setEvents(int fd, short events) {
	for (std::vector<pollfd>::iterator it = fds.begin(); it != fds.end(); ++it) {
		if (it->fd == fd) {
			it->events = events;
			return;
		}
	}
}
