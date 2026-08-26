#include "../headers/Http.hpp"

#include <iostream>
#include <sstream>
#include <string>

static int failures = 0;

static void expect(bool condition, const std::string &name) {
	if (condition)
		return ;
	std::cerr << "FAIL: " << name << std::endl;
	++failures;
}

static int countHeader(const std::string &raw, const std::string &name) {
	int count = 0;
	std::string needle = "\r\n" + name + ":";
	std::string::size_type pos = 0;
	while ((pos = raw.find(needle, pos)) != std::string::npos) {
		++count;
		pos += needle.size();
	}
	return count;
}

static std::string headerValue(const std::string &raw, const std::string &name) {
	std::string needle = "\r\n" + name + ": ";
	std::string::size_type start = raw.find(needle);
	if (start == std::string::npos)
		return "";
	start += needle.size();
	std::string::size_type end = raw.find("\r\n", start);
	return end == std::string::npos ? "" : raw.substr(start, end - start);
}

static std::string statusPrefix(int status) {
	std::ostringstream out;
	out << "HTTP/1.1 " << status << " ";
	return out.str();
}

int main() {
	Http http;
	Response response;
	response.body = "hello";
	std::string raw = http.build(response);
	expect(raw.find("HTTP/1.1 200 OK\r\n") == 0, "default status is 200 OK");
	expect(headerValue(raw, "Content-Type") == "text/html", "default content type is emitted");
	expect(headerValue(raw, "Content-Length") == "5", "content length matches body");
	expect(raw.substr(raw.size() - response.body.size()) == response.body, "body follows header separator");

	response.body.clear();
	raw = http.build(response);
	expect(headerValue(raw, "Content-Length") == "0", "empty body has zero content length");

	response.status = 201;
	response.body = "{}";
	response.headers["content-type"] = "application/json";
	response.headers["Content-Length"] = "999";
	response.headers["content-length"] = "2";
	response.headers["Location"] = "/items/1";
	response.headers["X-Trace"] = "request-42";
	raw = http.build(response);
	expect(raw.find("HTTP/1.1 201 Created\r\n") == 0, "created reason phrase is emitted");
	expect(headerValue(raw, "Content-Type") == "application/json", "explicit content type is preserved case-insensitively");
	expect(headerValue(raw, "Content-Length") == "2", "caller content length is replaced");
	expect(countHeader(raw, "Content-Length") == 1, "only one content length is emitted");
	expect(countHeader(raw, "Content-Type") == 1, "only one content type is emitted");
	expect(headerValue(raw, "Location") == "/items/1", "location header is preserved");
	expect(headerValue(raw, "X-Trace") == "request-42", "extension header is preserved");

	response.status = 204;
	response.body = "must not be sent";
	response.headers.clear();
	raw = http.build(response);
	expect(raw.find("HTTP/1.1 204 No Content\r\n") == 0, "no-content reason phrase is emitted");
	expect(headerValue(raw, "Content-Length") == "0", "no-content response has empty serialized body");
	expect(raw.substr(raw.find("\r\n\r\n") + 4).empty(), "no-content response omits body bytes");

	response.status = 418;
	response.body = "teapot";
	raw = http.build(response);
	expect(raw.find("HTTP/1.1 500 Internal Server Error\r\n") == 0, "unsupported status falls back to 500");

	response.status = 302;
	response.body = "redirect";
	response.headers["Bad Header"] = "ignored";
	response.headers["X-Injected"] = "bad\r\nInjected: value";
	response.headers["X-Safe"] = "kept";
	raw = http.build(response);
	expect(raw.find("HTTP/1.1 302 Found\r\n") == 0, "redirect reason phrase is emitted");
	expect(raw.find("Bad Header:") == std::string::npos, "invalid header name is suppressed");
	expect(raw.find("Injected: value") == std::string::npos, "unsafe header value is suppressed");
	expect(headerValue(raw, "X-Safe") == "kept", "safe header survives validation");

	const int statuses[] = {301, 303, 304, 307, 308, 400, 401, 403, 404, 405, 409,
		411, 413, 415, 431, 500, 501, 502, 503};
	for (size_t i = 0; i < sizeof(statuses) / sizeof(statuses[0]); ++i) {
		response.status = statuses[i];
		response.body = "x";
		response.headers.clear();
		raw = http.build(response);
		expect(raw.find(statusPrefix(statuses[i])) == 0,
			"supported status is serialized");
	}

	const int defaultErrorStatuses[] = {400, 403, 404, 405, 413, 431, 500, 502};
	for (size_t i = 0; i < sizeof(defaultErrorStatuses) / sizeof(defaultErrorStatuses[0]); ++i) {
		Response error = http.defaultErrorResponse(defaultErrorStatuses[i]);
		raw = http.build(error);
		expect(error.status == defaultErrorStatuses[i], "default error preserves its status");
		expect(!error.body.empty(), "default error has a body");
		expect(error.headers["Content-Type"] == "text/html", "default error declares HTML content");
		expect(raw.find(statusPrefix(defaultErrorStatuses[i])) == 0,
			"default error status is serialized");
		expect(headerValue(raw, "Content-Type") == "text/html", "default error serializes HTML content type");
		std::ostringstream length;
		length << error.body.size();
		expect(headerValue(raw, "Content-Length") == length.str(),
			"default error content length matches body");
		expect(raw.substr(raw.find("\r\n\r\n") + 4) == error.body,
			"default error serializes its complete body");
	}

	Response invalidDefault = http.defaultErrorResponse(200);
	expect(invalidDefault.status == 500, "non-error default falls back to 500");
	expect(!invalidDefault.body.empty(), "500 fallback default has a body");

	if (failures == 0)
		std::cout << "http response tests passed" << std::endl;
	return failures == 0 ? 0 : 1;
}
