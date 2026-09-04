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

static void expectMalformed(Http &http, const std::string &raw, const std::string &name) {
	Request request;
	request.method = "unchanged";
	request.path = "/unchanged";
	request.query = "unchanged";
	request.version = "unchanged";
	request.headers["unchanged"] = "unchanged";
	request.body = "unchanged";

	ssize_t result = http.parse(raw, request);
	expect(result < 0, name + " is rejected");
	expect(request.method == "unchanged" && request.path == "/unchanged" &&
		request.query == "unchanged" && request.version == "unchanged" &&
		request.headers.size() == 1 && request.headers["unchanged"] == "unchanged" &&
		request.body == "unchanged",
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
	expect(request.headers["host"] == "example.test", "header names are canonicalized");

	std::string headers = "GET / HTTP/1.1\r\nHost: example.test\r\nX-Trace:\t request-42 \t\r\n\r\n";
	result = http.parse(headers, request);
	expect(result == static_cast<ssize_t>(headers.size()), "valid headers report consumed bytes");
	expect(request.headers["x-trace"] == "request-42", "optional header whitespace is trimmed");
	expect(request.headers.find("X-Trace") == request.headers.end(), "original header casing is not retained");

	Request incomplete;
	result = http.parse("GET / HTTP/1.1\r\nHost: example.test", incomplete);
	expect(result == 0, "fragmented request remains incomplete");

	expectMalformed(http, requestWithLine("GET  / HTTP/1.1"), "double separator");
	expectMalformed(http, requestWithLine("GET relative HTTP/1.1"), "non-origin-form target");
	expectMalformed(http, requestWithLine("GET /#fragment HTTP/1.1"), "fragment in target");
	expectMalformed(http, requestWithLine("GET / HTTP/1.0"), "unsupported version");
	expectMalformed(http, requestWithLine("GET / HTTP/1.1 trailing"), "extra request-line field");
	expectMalformed(http, requestWithLine(" / HTTP/1.1"), "empty method");

	expectMalformed(http, "GET / HTTP/1.1\r\nHost example.test\r\n\r\n", "header without colon");
	expectMalformed(http, "GET / HTTP/1.1\r\nBad Name: value\r\n\r\n", "invalid header name");
	expectMalformed(http, "GET / HTTP/1.1\r\nHost : example.test\r\n\r\n", "whitespace before colon");
	expectMalformed(http, "GET / HTTP/1.1\r\nHost: example.test\nX-Test: value\r\n\r\n", "malformed header line ending");
	expectMalformed(http, "GET / HTTP/1.1\r\nContent-Length: 0\r\ncontent-length: 0\r\n\r\n", "duplicate content length");
	expectMalformed(http, "GET / HTTP/1.1\r\nTransfer-Encoding: chunked\r\nContent-Length: 0\r\n\r\n", "conflicting body framing");
	expectMalformed(http, "GET / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n", "unsupported transfer encoding");

	std::string oversized = "GET / HTTP/1.1\r\nX-Test: " + std::string(Http::MAX_HEADER_BYTES, 'x');
	expectMalformed(http, oversized, "oversized fragmented headers");

	std::string tooManyFields = "GET / HTTP/1.1\r\n";
	for (size_t i = 0; i <= Http::MAX_HEADER_FIELDS; ++i)
		tooManyFields += "X-Test: value\r\n";
	tooManyFields += "\r\n";
	expectMalformed(http, tooManyFields, "too many headers");

	if (failures == 0)
		std::cout << "http parser tests passed" << std::endl;
	return failures == 0 ? 0 : 1;
}
