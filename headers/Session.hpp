#ifndef SESSION_HPP
#define SESSION_HPP

#include <cstddef>
#include <ctime>
#include <map>
#include <string>

#include "types.hpp"

struct Session {
	std::string id;
	time_t      expires_at;
	unsigned long visits;

	Session() : expires_at(0), visits(0) {}
};

// Process-local session state for the optional cookie/session demonstration.
class SessionStore {
	public:
		static const time_t LIFETIME_SECONDS = 1800;

		SessionStore();
		~SessionStore();

		Session *create(time_t now);
		Session *find(const std::string &id, time_t now);
		Session *fromRequest(const Request &request, time_t now);
		void     sweep(time_t now);
		size_t   size() const;

		static const char *cookieName();
		static bool        parseCookie(const std::string &header, const std::string &name,
			std::string &value);
		static std::string setCookie(const Session &session);

	private:
		std::map<std::string, Session> sessions;

		SessionStore(const SessionStore &other);
		SessionStore &operator=(const SessionStore &other);
};

#endif
