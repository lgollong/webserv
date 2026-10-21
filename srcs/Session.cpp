#include "Session.hpp"

#include <ctime>
#include <sstream>

static const char kCookieName[] = "webserv_session";

SessionStore::SessionStore() : created(0) {}

SessionStore::~SessionStore() {}

static std::string cookieValue(const Request &request, const std::string &name) {
	std::map<std::string, std::string>::const_iterator header = request.headers.find("cookie");
	if (header == request.headers.end())
		return "";
	const std::string &cookies = header->second;
	const std::string prefix = name + "=";
	std::string::size_type pos = 0;
	while ((pos = cookies.find(prefix, pos)) != std::string::npos) {
		if (pos == 0 || cookies[pos - 1] == ' ' || cookies[pos - 1] == ';') {
			pos += prefix.size();
			return cookies.substr(pos, cookies.find(';', pos) - pos);
		}
		pos += prefix.size();
	}
	return "";
}

unsigned long SessionStore::visit(const Request &request, std::string &setCookie) {
	std::map<std::string, unsigned long>::iterator found = visits.find(cookieValue(request, kCookieName));
	if (found != visits.end())
		return ++found->second;

	std::ostringstream id;
	id << std::hex << time(NULL) << '-' << ++created;
	visits[id.str()] = 1;
	setCookie = std::string(kCookieName) + "=" + id.str() + "; Path=/; HttpOnly";
	return 1;
}
