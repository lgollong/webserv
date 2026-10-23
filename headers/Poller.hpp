#ifndef POLLER_HPP
#define POLLER_HPP

#include <vector>
#include <poll.h>

class Poller {
	public:
		Poller();
		~Poller();

		int                   poll(int timeoutMs);
		const std::vector<pollfd>& events() const;
		void                  add(int fd, short events);
		void                  remove(int fd);
		void                  setEvents(int fd, short events);

	private:
		std::vector<pollfd> fds;

		Poller(const Poller &other);
};

#endif
