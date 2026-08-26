#include "Http.hpp"
#include <sstream>
#include <cstdlib>

Http::Http() {}

Http::~Http() {}

// request-line = method SP request-target SP HTTP-version CRLF
// request-target = path [ "?" query ]
static bool isTokenChar(char value) {
	unsigned char c = static_cast<unsigned char>(value);
	if (std::isalnum(c))
		return true;
	return value == '!' || value == '#' || value == '$' || value == '%' ||
		value == '&' || value == '\'' || value == '*' || value == '+' ||
		value == '-' || value == '.' || value == '^' || value == '_' ||
		value == '`' || value == '|' || value == '~';
}

static bool isMethodToken(const std::string &method) {
	if (method.empty())
		return false;
	for (std::string::size_type i = 0; i < method.size(); ++i) {
		if (!isTokenChar(method[i]))
			return false;
	}
	return true;
}

static bool isOriginFormTarget(const std::string &target) {
	if (target.empty() || target[0] != '/')
		return false;
	for (std::string::size_type i = 0; i < target.size(); ++i) {
		unsigned char c = static_cast<unsigned char>(target[i]);
		if (c <= 0x20 || c >= 0x7f || target[i] == '#')
			return false;
	}
	return true;
}

static bool parseRequestLine(const std::string &line, Request &request) {
	std::string::size_type sp1 = line.find(' ');
	if (sp1 == std::string::npos || sp1 == 0)
		return false;
	std::string::size_type sp2 = line.find(' ', sp1 + 1);
	if (sp2 == std::string::npos || sp2 == sp1 + 1)
		return false;
	if (line.find(' ', sp2 + 1) != std::string::npos)
		return false;

	std::string method = line.substr(0, sp1);
	std::string target  = line.substr(sp1 + 1, sp2 - sp1 - 1);
	std::string version = line.substr(sp2 + 1);
	if (!isMethodToken(method) || !isOriginFormTarget(target) || version != "HTTP/1.1")
		return false;

	request.method = method;
	request.version = version;

	std::string::size_type qpos = target.find('?');
	if (qpos == std::string::npos) {
		request.path = target;
		request.query = "";
	} else {
		request.path = target.substr(0, qpos);
		request.query = target.substr(qpos + 1);
	}

	return true;
}

// header-field = field-name ":" [ field-value ] CRLF
static void parseHeaderFields(const std::string &headerBlock, std::string::size_type start,
                               std::map<std::string, std::string> &headers) {
	std::string::size_type pos = start;

	while (pos < headerBlock.size()) {
		std::string::size_type lineEnd = headerBlock.find("\r\n", pos);
		std::string line = (lineEnd == std::string::npos)
			? headerBlock.substr(pos)
			: headerBlock.substr(pos, lineEnd - pos);

		std::string::size_type colon = line.find(':');
		if (colon != std::string::npos) {
			std::string key = line.substr(0, colon);
			std::string::size_type valueStart = line.find_first_not_of(' ', colon + 1);
			std::string value = (valueStart == std::string::npos) ? "" : line.substr(valueStart);
			headers[key] = value;
		}

		if (lineEnd == std::string::npos)
			break;
		pos = lineEnd + 2;
	}
}

// > 0: bytes consumed, a complete request was parsed into `request`.
//   0: `inbuf` doesn't contain a complete request yet -- wait for more data.
//  -1: `inbuf` contains a malformed request -- a parse error, not a wait.
ssize_t Http::parse(const std::string &inbuf, Request &request) {
	std::string::size_type headerEnd = inbuf.find("\r\n\r\n");
	if (headerEnd == std::string::npos)
		return 0; // headers not fully buffered yet

	std::string headerBlock = inbuf.substr(0, headerEnd);
	std::string::size_type bodyStart = headerEnd + 4;

	std::string::size_type lineEnd = headerBlock.find("\r\n");
	std::string requestLine = (lineEnd == std::string::npos) ? headerBlock : headerBlock.substr(0, lineEnd);

	Request parsed;
	if (!parseRequestLine(requestLine, parsed))
		return -1; // malformed request-line

	std::string::size_type headersStart = (lineEnd == std::string::npos) ? headerBlock.size() : lineEnd + 2;
	parseHeaderFields(headerBlock, headersStart, parsed.headers);

	size_t contentLength = 0;
	std::map<std::string, std::string>::const_iterator it = parsed.headers.find("Content-Length");
	if (it != parsed.headers.end())
		contentLength = static_cast<size_t>(std::atoi(it->second.c_str()));

	if (inbuf.size() < bodyStart + contentLength)
		return 0; // body not fully buffered yet

	parsed.body = inbuf.substr(bodyStart, contentLength);
	request = parsed;

	return static_cast<ssize_t>(bodyStart + contentLength);
}

std::string Http::build(const Response &response) {
	int status = response.status ? response.status : 200;

	std::string reason = "OK";
	if (status == 201) reason = "Created";
	else if (status == 204) reason = "No Content";
	else if (status == 400) reason = "Bad Request";
	else if (status == 403) reason = "Forbidden";
	else if (status == 404) reason = "Not Found";
	else if (status == 405) reason = "Method Not Allowed";
	else if (status == 500) reason = "Internal Server Error";
	else if (status == 502) reason = "Bad Gateway";

	std::map<std::string, std::string> headers = response.headers;
	if (headers.find("Content-Type") == headers.end())
		headers["Content-Type"] = "text/html";

	std::ostringstream len;
	len << response.body.size();
	headers["Content-Length"] = len.str();

	std::ostringstream out;
	out << "HTTP/1.1 " << status << " " << reason << "\r\n";
	for (std::map<std::string, std::string>::const_iterator it = headers.begin(); it != headers.end(); ++it)
		out << it->first << ": " << it->second << "\r\n";
	out << "\r\n" << response.body;

	return out.str();
}
