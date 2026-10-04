#include "../headers/Session.hpp"

#include <iostream>

static int failures = 0;

static void expect(bool condition, const char *message) {
	if (condition)
		return;
	std::cerr << "failure: " << message << std::endl;
	++failures;
}

static Request requestWithCookie(const std::string &cookie) {
	Request request;
	request.headers["cookie"] = cookie;
	return request;
}

int main() {
	std::string value;
	expect(SessionStore::parseCookie("theme=dark; webserv_session=token; mode=full",
		SessionStore::cookieName(), value) && value == "token",
		"parses the named cookie among other cookies");
	expect(SessionStore::parseCookie(" webserv_session = token ", SessionStore::cookieName(), value) &&
		value == "token", "trims optional cookie whitespace");
	expect(!SessionStore::parseCookie("theme=dark", SessionStore::cookieName(), value),
		"rejects a missing session cookie");
	expect(!SessionStore::parseCookie("webserv_session=one; webserv_session=two",
		SessionStore::cookieName(), value), "rejects duplicate session cookies");
	expect(!SessionStore::parseCookie("webserv_session=", SessionStore::cookieName(), value),
		"rejects an empty session cookie");

	SessionStore store;
	time_t now = 1000;
	Session *created = store.create(now);
	expect(created != NULL, "creates a session from operating-system entropy");
	if (created != NULL) {
		std::string id = created->id;
		std::string cookie = SessionStore::setCookie(*created);
		expect(id.size() == 64, "creates a 256-bit hexadecimal session identifier");
		expect(created->expires_at == now + SessionStore::LIFETIME_SECONDS,
			"assigns the bounded session lifetime");
		expect(cookie.find(std::string(SessionStore::cookieName()) + "=" + id) == 0 &&
			cookie.find("Max-Age=1800") != std::string::npos && cookie.find("Path=/") != std::string::npos &&
			cookie.find("HttpOnly") != std::string::npos, "builds one scoped HttpOnly Set-Cookie value");
		expect(store.fromRequest(requestWithCookie("other=value; webserv_session=" + id), now) != NULL,
			"looks up a valid request cookie");
		expect(store.fromRequest(requestWithCookie("webserv_session=not-a-token"), now) == NULL,
			"rejects malformed session identifiers");
		expect(store.fromRequest(requestWithCookie("webserv_session=" + id + "; webserv_session=" + id), now) == NULL,
			"rejects ambiguous request cookies");
		expect(store.find(id, now + SessionStore::LIFETIME_SECONDS) == NULL && store.size() == 0,
			"expires and removes sessions at their deadline");
	}

	Session *expired = store.create(now);
	expect(expired != NULL, "creates a session for periodic cleanup");
	store.sweep(now + SessionStore::LIFETIME_SECONDS);
	expect(store.size() == 0, "periodic cleanup removes expired sessions");

	if (failures == 0)
		std::cout << "session store tests passed" << std::endl;
	return failures == 0 ? 0 : 1;
}
