#include "../headers/Http.hpp"

#include <iostream>
#include <limits>
#include <sstream>
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

static Request unchangedRequest() {
	Request request;
	request.method = "unchanged";
	request.path = "/unchanged";
	request.query = "unchanged";
	request.version = "unchanged";
	request.headers["unchanged"] = "unchanged";
	request.body = "unchanged";
	return request;
}

static bool isUnchanged(const Request &request) {
	return request.method == "unchanged" && request.path == "/unchanged" &&
		request.query == "unchanged" && request.version == "unchanged" &&
		request.headers.size() == 1 && request.headers.find("unchanged") != request.headers.end() &&
		request.headers.find("unchanged")->second == "unchanged" && request.body == "unchanged";
}

static void expectMalformed(Http &http, const std::string &raw, const std::string &name) {
	Request request = unchangedRequest();

	ssize_t result = http.parse(raw, request);
	expect(result < 0, name + " is rejected");
	expect(isUnchanged(request), name + " does not mutate request state");
}

static void expectIncomplete(Http &http, const std::string &raw, const std::string &name) {
	Request request = unchangedRequest();
	ssize_t result = http.parse(raw, request);
	expect(result == 0, name + " remains incomplete");
	expect(isUnchanged(request), name + " does not mutate request state");
}

static void expectParseStatus(Http &http, const std::string &raw, int expectedStatus,
		const std::string &name) {
	Request request = unchangedRequest();
	int status = 0;
	ssize_t result = http.parse(raw, request, status);
	expect(result < 0, name + " is rejected");
	expect(status == expectedStatus, name + " reports the expected status");
	expect(isUnchanged(request), name + " does not mutate request state");
}

static void expectParseStatusWithLimit(Http &http, const std::string &raw, size_t maxBodyBytes,
		int expectedStatus, const std::string &name) {
	Request request = unchangedRequest();
	int status = 0;
	ssize_t result = http.parse(raw, request, maxBodyBytes, status);
	expect(result < 0, name + " is rejected");
	expect(status == expectedStatus, name + " reports the expected status");
	expect(isUnchanged(request), name + " does not mutate request state");
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

	std::string zeroLength = "POST /upload HTTP/1.1\r\nHost: example.test\r\nContent-Length: 0\r\n\r\n";
	result = http.parse(zeroLength, request);
	expect(result == static_cast<ssize_t>(zeroLength.size()), "zero-length body reports consumed bytes");
	expect(request.body.empty(), "zero-length body is empty");

	std::string fragmentedBody = "POST /upload HTTP/1.1\r\nHost: example.test\r\nContent-Length: 4\r\n\r\nda";
	expectIncomplete(http, fragmentedBody, "fragmented body");
	fragmentedBody += "ta";
	result = http.parse(fragmentedBody, request);
	expect(result == static_cast<ssize_t>(fragmentedBody.size()), "complete fragmented body reports consumed bytes");
	expect(request.body == "data", "complete fragmented body is parsed");

	std::string leadingZeroes = "POST /upload HTTP/1.1\r\nContent-Length: 004\r\n\r\ndata";
	result = http.parse(leadingZeroes, request);
	expect(result == static_cast<ssize_t>(leadingZeroes.size()), "leading-zero content length reports consumed bytes");
	expect(request.body == "data", "leading-zero content length is parsed");

	std::string nextRequest = requestWithLine("GET /next HTTP/1.1");
	std::string pipelined = "POST /upload HTTP/1.1\r\nHost: example.test\r\nContent-Length: 4\r\n\r\ndata" + nextRequest;
	result = http.parse(pipelined, request);
	expect(result == static_cast<ssize_t>(pipelined.size() - nextRequest.size()), "body consumed count ends at next request");
	expect(request.body == "data", "pipelined request body is parsed");
	expect(pipelined.substr(static_cast<size_t>(result)) == nextRequest, "pipelined bytes remain untouched");

	expectMalformed(http, "POST / HTTP/1.1\r\nContent-Length:\r\n\r\n", "empty content length");
	expectMalformed(http, "POST / HTTP/1.1\r\nContent-Length: -1\r\n\r\n", "negative content length");
	expectMalformed(http, "POST / HTTP/1.1\r\nContent-Length: +1\r\n\r\n", "signed content length");
	expectMalformed(http, "POST / HTTP/1.1\r\nContent-Length: 12bytes\r\n\r\n", "non-decimal content length");
	expectMalformed(http, "POST / HTTP/1.1\r\nContent-Length: 1 2\r\n\r\n", "content length with internal whitespace");

	std::ostringstream overflowLength;
	overflowLength << std::numeric_limits<size_t>::max() << "0";
	expectMalformed(http, "POST / HTTP/1.1\r\nContent-Length: " + overflowLength.str() + "\r\n\r\n", "overflowing content length");

	Request limited = unchangedRequest();
	result = http.parse("POST / HTTP/1.1\r\nContent-Length: 4\r\n\r\n", limited, 3);
	expect(result < 0, "body over explicit limit is rejected");
	expect(isUnchanged(limited), "body over explicit limit does not mutate request state");

	std::ostringstream defaultLimit;
	defaultLimit << static_cast<size_t>(Http::DEFAULT_MAX_BODY_BYTES) + 1;
	expectMalformed(http, "POST / HTTP/1.1\r\nContent-Length: " + defaultLimit.str() + "\r\n\r\n", "body over default limit");

	expectParseStatus(http, requestWithLine("GET  / HTTP/1.1"), 400, "malformed request line");
	expectParseStatus(http, "GET / HTTP/1.1\r\nBad Header\r\n\r\n", 400, "malformed header");
	expectParseStatusWithLimit(http, "GET / HTTP/1.1\r\nContent-Length: 4\r\n\r\n", 3, 413,
		"body over explicit limit status");

	std::string oversizedHeader = "GET / HTTP/1.1\r\nX-Test: " + std::string(Http::MAX_HEADER_BYTES, 'x');
	expectParseStatus(http, oversizedHeader, 431, "oversized header");

	Response errorResponse;
	errorResponse.status = 413;
	expect(http.build(errorResponse).find("HTTP/1.1 413 Payload Too Large\r\n") == 0,
		"payload-too-large response has a reason phrase");
	errorResponse.status = 431;
	expect(http.build(errorResponse).find("HTTP/1.1 431 Request Header Fields Too Large\r\n") == 0,
		"header-too-large response has a reason phrase");

	if (failures == 0)
		std::cout << "http parser tests passed" << std::endl;
	return failures == 0 ? 0 : 1;
}
