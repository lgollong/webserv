#include "Poller.hpp"

Poller::Poller() {}

Poller::~Poller() {}

std::vector<pollfd>& Poller::poll() {
    ::poll(fds.empty() ? NULL : &fds[0], fds.size(), -1);
    return fds;
}

void Poller::add(int fd, short events) {
    pollfd pfd;
    pfd.fd = fd;
    pfd.events = events;   // events to look for
    pfd.revents = 0;       // events returned
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
