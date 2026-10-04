#include "Session.hpp"

#include <fcntl.h>
#include <sstream>
#include <unistd.h>

static const char kSessionCookieName[] = "webserv_session";
static const size_t kSessionIdBytes = 32;

SessionStore::SessionStore() {}

SessionStore::~SessionStore() {}

static std::string trimCookiePart(const std::string &value) {
	std::string::size_type first = 0;
	while (first < value.size() && (value[first] == ' ' || value[first] == '\t'))
		++first;
	std::string::size_type last = value.size();
	while (last > first && (value[last - 1] == ' ' || value[last - 1] == '\t'))
		--last;
	return value.substr(first, last - first);
}

static bool validSessionId(const std::string &id) {
	if (id.size() != kSessionIdBytes * 2)
		return false;
	for (std::string::size_type i = 0; i < id.size(); ++i) {
		if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f')))
			return false;
	}
	return true;
}

const char *SessionStore::cookieName() {
	return kSessionCookieName;
}

bool SessionStore::parseCookie(const std::string &header, const std::string &name,
	std::string &value) {
	bool found = false;
	value.clear();
	std::string::size_type start = 0;
	while (start < header.size()) {
		std::string::size_type end = header.find(';', start);
		if (end == std::string::npos)
			end = header.size();
		std::string part = trimCookiePart(header.substr(start, end - start));
		std::string::size_type equal = part.find('=');
		if (equal != std::string::npos && trimCookiePart(part.substr(0, equal)) == name) {
			if (found)
				return false;
			value = trimCookiePart(part.substr(equal + 1));
			if (value.empty())
				return false;
			found = true;
		}
		if (end == header.size())
			break;
		start = end + 1;
	}
	return found;
}

static bool randomBytes(unsigned char *bytes, size_t count) {
	int fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0)
		return false;
	size_t received = 0;
	while (received < count) {
		ssize_t result = read(fd, bytes + received, count - received);
		if (result <= 0) {
			close(fd);
			return false;
		}
		received += static_cast<size_t>(result);
	}
	close(fd);
	return true;
}

static std::string hexEncode(const unsigned char *bytes, size_t count) {
	static const char hex[] = "0123456789abcdef";
	std::string value;
	value.reserve(count * 2);
	for (size_t i = 0; i < count; ++i) {
		value += hex[bytes[i] >> 4];
		value += hex[bytes[i] & 0x0F];
	}
	return value;
}

Session *SessionStore::create(time_t now) {
	for (int attempt = 0; attempt < 8; ++attempt) {
		unsigned char bytes[kSessionIdBytes];
		if (!randomBytes(bytes, sizeof(bytes)))
			return NULL;
		Session session;
		session.id = hexEncode(bytes, sizeof(bytes));
		session.expires_at = now + LIFETIME_SECONDS;
		std::pair<std::map<std::string, Session>::iterator, bool> inserted =
			sessions.insert(std::make_pair(session.id, session));
		if (inserted.second)
			return &inserted.first->second;
	}
	return NULL;
}

Session *SessionStore::find(const std::string &id, time_t now) {
	if (!validSessionId(id))
		return NULL;
	std::map<std::string, Session>::iterator found = sessions.find(id);
	if (found == sessions.end())
		return NULL;
	if (found->second.expires_at <= now) {
		sessions.erase(found);
		return NULL;
	}
	return &found->second;
}

Session *SessionStore::fromRequest(const Request &request, time_t now) {
	std::map<std::string, std::string>::const_iterator header = request.headers.find("cookie");
	if (header == request.headers.end())
		return NULL;
	std::string id;
	if (!parseCookie(header->second, cookieName(), id))
		return NULL;
	return find(id, now);
}

void SessionStore::sweep(time_t now) {
	for (std::map<std::string, Session>::iterator it = sessions.begin(); it != sessions.end(); ) {
		std::map<std::string, Session>::iterator current = it++;
		if (current->second.expires_at <= now)
			sessions.erase(current);
	}
}

size_t SessionStore::size() const {
	return sessions.size();
}

std::string SessionStore::setCookie(const Session &session) {
	if (!validSessionId(session.id))
		return "";
	std::ostringstream lifetime;
	lifetime << LIFETIME_SECONDS;
	return std::string(cookieName()) + "=" + session.id + "; Max-Age=" + lifetime.str() +
		"; Path=/; HttpOnly";
}
