#ifndef SESSION_HPP
#define SESSION_HPP

#include <map>
#include <string>

#include "types.hpp"

class SessionStore {
	public:
		SessionStore();
		~SessionStore();

		unsigned long visit(const Request &request, std::string &setCookie);

	private:
		std::map<std::string, unsigned long> visits;
		unsigned long                        created;

		SessionStore(const SessionStore &other);
		SessionStore &operator=(const SessionStore &other);
};

#endif
