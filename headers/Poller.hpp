#ifndef POLLER_HPP
#define POLLER_HPP

#include <vector>
#include <poll.h>

// Thin poll() wrapper; maintains the fd set and per-fd interest.
class Poller {
	public:
		Poller();
		~Poller();

		std::vector<pollfd>&  poll();
		void                  add(int fd, short events);
		void                  remove(int fd);
		void                  setEvents(int fd, short events);

	private:
		std::vector<pollfd> fds;

		Poller(const Poller& other);
};

#endif
