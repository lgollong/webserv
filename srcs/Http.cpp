#include "Http.hpp"
#include <sstream>
#include <cctype>
#include <limits>

Http::Http() {}

Http::~Http() {}

static const int kBadRequest = 400;
static const int kPayloadTooLarge = 413;
static const int kRequestHeaderFieldsTooLarge = 431;

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

static std::string lowerCase(const std::string &value) {
	std::string result;
	result.reserve(value.size());
	for (std::string::size_type i = 0; i < value.size(); ++i)
		result += static_cast<char>(std::tolower(static_cast<unsigned char>(value[i])));
	return result;
}

static bool isHeaderValue(const std::string &value) {
	for (std::string::size_type i = 0; i < value.size(); ++i) {
		unsigned char c = static_cast<unsigned char>(value[i]);
		if (c == '\t' || (c >= 0x20 && c != 0x7f))
			continue;
		return false;
	}
	return true;
}

static std::string trimOptionalWhitespace(const std::string &value) {
	std::string::size_type first = 0;
	while (first < value.size() && (value[first] == ' ' || value[first] == '\t'))
		++first;

	std::string::size_type last = value.size();
	while (last > first && (value[last - 1] == ' ' || value[last - 1] == '\t'))
		--last;
	return value.substr(first, last - first);
}

// header-field = field-name ":" OWS field-value OWS CRLF
static bool parseHeaderFields(const std::string &headerBlock, std::string::size_type start,
		std::map<std::string, std::string> &headers, int &errorStatus) {
	std::string::size_type pos = start;
	size_t fieldCount = 0;
	bool hasContentLength = false;
	bool hasTransferEncoding = false;

	while (pos < headerBlock.size()) {
		std::string::size_type lineEnd = headerBlock.find("\r\n", pos);
		bool isLastLine = lineEnd == std::string::npos;
		std::string line = isLastLine
			? headerBlock.substr(pos)
			: headerBlock.substr(pos, lineEnd - pos);

		std::string::size_type colon = line.find(':');
		if (colon == std::string::npos || colon == 0)
			return false;

		std::string key = line.substr(0, colon);
		for (std::string::size_type i = 0; i < key.size(); ++i) {
			if (!isTokenChar(key[i]))
				return false;
		}

		std::string value = line.substr(colon + 1);
		if (!isHeaderValue(value))
			return false;

		key = lowerCase(key);
		if (++fieldCount > Http::MAX_HEADER_FIELDS) {
			errorStatus = kRequestHeaderFieldsTooLarge;
			return false;
		}
		if (key == "content-length") {
			if (hasContentLength || hasTransferEncoding)
				return false;
			hasContentLength = true;
		}
		if (key == "transfer-encoding") {
			if (hasTransferEncoding || hasContentLength)
				return false;
			hasTransferEncoding = true;
		}

		headers[key] = trimOptionalWhitespace(value);

		if (isLastLine)
			break;
		pos = lineEnd + 2;
	}
	return !hasTransferEncoding;
}

static bool parseContentLength(const std::string &value, size_t &contentLength) {
	if (value.empty())
		return false;

	contentLength = 0;
	const size_t maxSize = std::numeric_limits<size_t>::max();
	for (std::string::size_type i = 0; i < value.size(); ++i) {
		if (value[i] < '0' || value[i] > '9')
			return false;
		size_t digit = static_cast<size_t>(value[i] - '0');
		if (contentLength > (maxSize - digit) / 10)
			return false;
		contentLength = contentLength * 10 + digit;
	}
	return true;
}

// > 0: bytes consumed, a complete request was parsed into `request`.
//   0: `inbuf` doesn't contain a complete request yet -- wait for more data.
//  -1: `inbuf` contains a malformed request -- a parse error, not a wait.
ssize_t Http::parse(const std::string &inbuf, Request &request) {
	int ignoredStatus = 0;
	return parse(inbuf, request, DEFAULT_MAX_BODY_BYTES, ignoredStatus);
}

ssize_t Http::parse(const std::string &inbuf, Request &request, size_t maxBodyBytes) {
	int ignoredStatus = 0;
	return parse(inbuf, request, maxBodyBytes, ignoredStatus);
}

ssize_t Http::parse(const std::string &inbuf, Request &request, int &errorStatus) {
	return parse(inbuf, request, DEFAULT_MAX_BODY_BYTES, errorStatus);
}

ssize_t Http::parse(const std::string &inbuf, Request &request, size_t maxBodyBytes, int &errorStatus) {
	errorStatus = 0;
	std::string::size_type headerEnd = inbuf.find("\r\n\r\n");
	if (headerEnd == std::string::npos) {
		if (inbuf.size() > MAX_HEADER_BYTES) {
			errorStatus = kRequestHeaderFieldsTooLarge;
			return -1;
		}
		return 0; // headers not fully buffered yet
	}
	if (headerEnd + 4 > MAX_HEADER_BYTES) {
		errorStatus = kRequestHeaderFieldsTooLarge;
		return -1;
	}

	std::string headerBlock = inbuf.substr(0, headerEnd);
	std::string::size_type bodyStart = headerEnd + 4;

	std::string::size_type lineEnd = headerBlock.find("\r\n");
	std::string requestLine = (lineEnd == std::string::npos) ? headerBlock : headerBlock.substr(0, lineEnd);

	Request parsed;
	if (!parseRequestLine(requestLine, parsed)) {
		errorStatus = kBadRequest;
		return -1; // malformed request-line
	}

	std::string::size_type headersStart = (lineEnd == std::string::npos) ? headerBlock.size() : lineEnd + 2;
	if (!parseHeaderFields(headerBlock, headersStart, parsed.headers, errorStatus)) {
		if (errorStatus == 0)
			errorStatus = kBadRequest;
		return -1; // malformed or unsupported headers
	}

	size_t contentLength = 0;
	std::map<std::string, std::string>::const_iterator it = parsed.headers.find("content-length");
	if (it != parsed.headers.end() && !parseContentLength(it->second, contentLength)) {
		errorStatus = kBadRequest;
		return -1; // invalid Content-Length
	}
	if (contentLength > maxBodyBytes) {
		errorStatus = kPayloadTooLarge;
		return -1; // request body is larger than the configured limit
	}

	const size_t maxSize = std::numeric_limits<size_t>::max();
	if (contentLength > maxSize - bodyStart) {
		errorStatus = kBadRequest;
		return -1; // full request length cannot be represented
	}
	const size_t requestSize = bodyStart + contentLength;
	if (requestSize > static_cast<size_t>(std::numeric_limits<ssize_t>::max())) {
		errorStatus = kBadRequest;
		return -1; // parser contract cannot return this consumed count
	}

	if (inbuf.size() < requestSize)
		return 0; // body not fully buffered yet

	parsed.body = inbuf.substr(bodyStart, contentLength);
	request = parsed;

	return static_cast<ssize_t>(requestSize);
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
	else if (status == 413) reason = "Payload Too Large";
	else if (status == 431) reason = "Request Header Fields Too Large";
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
