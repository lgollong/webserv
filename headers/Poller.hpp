#ifndef POLLER_HPP
#define POLLER_HPP

#include <map>
#include <vector>
#include <poll.h>

class Poller {
	public:
		Poller();
		~Poller();

		int                   poll(int timeoutMs);
		const std::vector<pollfd>& events() const;
		bool                  isCurrent(const pollfd &event) const;
		void                  add(int fd, short events);
		void                  remove(int fd);
		void                  setEvents(int fd, short events);

	private:
		std::vector<pollfd> fds;
		std::map<int, short> ready;

		Poller(const Poller &other);
};

#endif
