#include "Poller.hpp"

#include <cstddef>

Poller::Poller() {}

Poller::~Poller() {}

int Poller::poll(int timeoutMs) {
	pollfd *first = NULL;
	if (!fds.empty())
		first = &fds[0];
	ready.clear();
	int count = ::poll(first, fds.size(), timeoutMs);
	if (count > 0) {
		for (std::vector<pollfd>::const_iterator it = fds.begin(); it != fds.end(); ++it) {
			if (it->revents != 0)
				ready[it->fd] = it->revents;
		}
	}
	return count;
}

const std::vector<pollfd>& Poller::events() const {
	return fds;
}

bool Poller::isCurrent(const pollfd &event) const {
	std::map<int, short>::const_iterator found = ready.find(event.fd);
	return found != ready.end() && found->second == event.revents;
}

void Poller::add(int fd, short events) {
	ready.erase(fd);
	pollfd pfd;
	pfd.fd = fd;
	pfd.events = events;
	pfd.revents = 0;
	fds.push_back(pfd);
}

void Poller::remove(int fd) {
	ready.erase(fd);
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
