#include "../headers/Http.hpp"

#include <iostream>
#include <string>

static int failures = 0;

static void expect(bool condition, const std::string &name) {
	if (condition)
		return ;
	std::cerr << "FAIL: " << name << std::endl;
	++failures;
}

static std::string requestWithLine(const std::string &line) {
	return line + "\r\nHost: example.test\r\n\r\n";
}

static void expectMalformed(Http &http, const std::string &line, const std::string &name) {
	Request request;
	request.method = "unchanged";
	request.path = "/unchanged";
	request.query = "unchanged";
	request.version = "unchanged";

	ssize_t result = http.parse(requestWithLine(line), request);
	expect(result < 0, name + " is rejected");
	expect(request.method == "unchanged" && request.path == "/unchanged" &&
		request.query == "unchanged" && request.version == "unchanged",
		name + " does not mutate request state");
}

int main() {
	Http http;
	Request request;
	std::string valid = requestWithLine("GET /catalog?sort=asc HTTP/1.1");
	ssize_t result = http.parse(valid, request);

	expect(result == static_cast<ssize_t>(valid.size()), "valid line reports consumed bytes");
	expect(request.method == "GET", "method is parsed");
	expect(request.path == "/catalog", "path is parsed");
	expect(request.query == "sort=asc", "query is parsed");
	expect(request.version == "HTTP/1.1", "version is parsed");

	Request incomplete;
	result = http.parse("GET / HTTP/1.1\r\nHost: example.test", incomplete);
	expect(result == 0, "fragmented request remains incomplete");

	expectMalformed(http, "GET  / HTTP/1.1", "double separator");
	expectMalformed(http, "GET relative HTTP/1.1", "non-origin-form target");
	expectMalformed(http, "GET /#fragment HTTP/1.1", "fragment in target");
	expectMalformed(http, "GET / HTTP/1.0", "unsupported version");
	expectMalformed(http, "GET / HTTP/1.1 trailing", "extra request-line field");
	expectMalformed(http, " / HTTP/1.1", "empty method");

	if (failures == 0)
		std::cout << "http request-line tests passed" << std::endl;
	return failures == 0 ? 0 : 1;
}
