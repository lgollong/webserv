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
		std::map<std::string, std::string> &headers, bool &isChunked, int &errorStatus) {
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
			if (lowerCase(trimOptionalWhitespace(value)) != "chunked")
				return false;
			isChunked = true;
		}

		headers[key] = trimOptionalWhitespace(value);

		if (isLastLine)
			break;
		pos = lineEnd + 2;
	}
	return true;
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

static bool parseChunkSize(const std::string &line, size_t &chunkSize) {
	std::string::size_type extension = line.find(';');
	std::string sizeText = line.substr(0, extension);
	if (sizeText.empty())
		return false;
	if (extension != std::string::npos) {
		if (extension + 1 == line.size())
			return false;
		for (std::string::size_type i = extension + 1; i < line.size(); ++i) {
			unsigned char c = static_cast<unsigned char>(line[i]);
			if (c < 0x20 || c == 0x7f)
				return false;
		}
	}

	chunkSize = 0;
	const size_t maxSize = std::numeric_limits<size_t>::max();
	for (std::string::size_type i = 0; i < sizeText.size(); ++i) {
		unsigned char c = static_cast<unsigned char>(sizeText[i]);
		size_t digit = 0;
		if (c >= '0' && c <= '9')
			digit = c - '0';
		else if (c >= 'a' && c <= 'f')
			digit = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F')
			digit = c - 'A' + 10;
		else
			return false;
		if (chunkSize > (maxSize - digit) / 16)
			return false;
		chunkSize = chunkSize * 16 + digit;
	}
	return true;
}

static ssize_t parseChunkedBody(const std::string &inbuf, size_t bodyStart, size_t maxBodyBytes,
		std::string &body, int &errorStatus) {
	size_t pos = bodyStart;
	std::string decoded;
	const size_t maxSize = std::numeric_limits<size_t>::max();

	while (true) {
		std::string::size_type lineEnd = inbuf.find("\r\n", pos);
		if (lineEnd == std::string::npos) {
			if (inbuf.size() - pos > Http::MAX_CHUNK_LINE_BYTES) {
				errorStatus = kRequestHeaderFieldsTooLarge;
				return -1;
			}
			return 0;
		}
		if (lineEnd - pos > Http::MAX_CHUNK_LINE_BYTES) {
			errorStatus = kRequestHeaderFieldsTooLarge;
			return -1;
		}

		size_t chunkSize = 0;
		if (!parseChunkSize(inbuf.substr(pos, lineEnd - pos), chunkSize)) {
			errorStatus = kBadRequest;
			return -1;
		}
		pos = lineEnd + 2;

		if (chunkSize == 0) {
			if (pos > maxSize - 2) {
				errorStatus = kBadRequest;
				return -1;
			}
			if (inbuf.size() >= pos + 2 && inbuf.compare(pos, 2, "\r\n") == 0) {
				pos += 2;
				if (pos > static_cast<size_t>(std::numeric_limits<ssize_t>::max())) {
					errorStatus = kBadRequest;
					return -1;
				}
				body = decoded;
				return static_cast<ssize_t>(pos);
			}

			std::string::size_type trailerEnd = inbuf.find("\r\n\r\n", pos);
			if (trailerEnd == std::string::npos) {
				if (inbuf.size() - pos > Http::MAX_HEADER_BYTES) {
					errorStatus = kRequestHeaderFieldsTooLarge;
					return -1;
				}
				return 0;
			}
			if (trailerEnd > maxSize - 4) {
				errorStatus = kBadRequest;
				return -1;
			}
			if (trailerEnd + 4 - pos > Http::MAX_HEADER_BYTES) {
				errorStatus = kRequestHeaderFieldsTooLarge;
				return -1;
			}

			std::map<std::string, std::string> trailers;
			bool trailerChunked = false;
			if (!parseHeaderFields(inbuf.substr(pos, trailerEnd - pos), 0, trailers, trailerChunked, errorStatus) ||
				trailers.find("content-length") != trailers.end() || trailers.find("transfer-encoding") != trailers.end()) {
				if (errorStatus == 0)
					errorStatus = kBadRequest;
				return -1;
			}
			pos = trailerEnd + 4;
			if (pos > static_cast<size_t>(std::numeric_limits<ssize_t>::max())) {
				errorStatus = kBadRequest;
				return -1;
			}
			body = decoded;
			return static_cast<ssize_t>(pos);
		}

		if (chunkSize > maxBodyBytes || decoded.size() > maxBodyBytes - chunkSize) {
			errorStatus = kPayloadTooLarge;
			return -1;
		}
		if (pos > maxSize - chunkSize || maxSize - pos - chunkSize < 2) {
			errorStatus = kBadRequest;
			return -1;
		}
		size_t chunkEnd = pos + chunkSize;
		if (inbuf.size() < chunkEnd + 2)
			return 0;
		if (inbuf.compare(chunkEnd, 2, "\r\n") != 0) {
			errorStatus = kBadRequest;
			return -1;
		}

		decoded.append(inbuf, pos, chunkSize);
		pos = chunkEnd + 2;
	}
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
	bool isChunked = false;
	if (!parseHeaderFields(headerBlock, headersStart, parsed.headers, isChunked, errorStatus)) {
		if (errorStatus == 0)
			errorStatus = kBadRequest;
		return -1; // malformed or unsupported headers
	}
	if (isChunked) {
		std::string decodedBody;
		ssize_t requestSize = parseChunkedBody(inbuf, bodyStart, maxBodyBytes, decodedBody, errorStatus);
		if (requestSize <= 0)
			return requestSize;
		parsed.body = decodedBody;
		request = parsed;
		return requestSize;
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

static const char *reasonPhrase(int status) {
	switch (status) {
		case 200: return "OK";
		case 201: return "Created";
		case 202: return "Accepted";
		case 204: return "No Content";
		case 301: return "Moved Permanently";
		case 302: return "Found";
		case 303: return "See Other";
		case 304: return "Not Modified";
		case 307: return "Temporary Redirect";
		case 308: return "Permanent Redirect";
		case 400: return "Bad Request";
		case 401: return "Unauthorized";
		case 403: return "Forbidden";
		case 404: return "Not Found";
		case 405: return "Method Not Allowed";
		case 409: return "Conflict";
		case 411: return "Length Required";
		case 413: return "Payload Too Large";
		case 415: return "Unsupported Media Type";
		case 431: return "Request Header Fields Too Large";
		case 500: return "Internal Server Error";
		case 501: return "Not Implemented";
		case 502: return "Bad Gateway";
		case 503: return "Service Unavailable";
	}
	return NULL;
}

static bool hasResponseHeader(const std::map<std::string, std::string> &headers,
		const std::string &wanted, std::string &value) {
	for (std::map<std::string, std::string>::const_iterator it = headers.begin(); it != headers.end(); ++it) {
		if (lowerCase(it->first) != wanted || !isHeaderValue(it->second))
			continue;
		value = it->second;
		return true;
	}
	return false;
}

static bool isResponseHeader(const std::string &name, const std::string &value) {
	if (name.empty() || !isHeaderValue(value))
		return false;
	for (std::string::size_type i = 0; i < name.size(); ++i) {
		if (!isTokenChar(name[i]))
			return false;
	}
	return true;
}

std::string Http::build(const Response &response) {
	int status = response.status ? response.status : 200;
	const char *reason = reasonPhrase(status);
	if (reason == NULL) {
		status = 500;
		reason = reasonPhrase(status);
	}

	std::string body = response.body;
	if (status == 204 || status == 304)
		body.clear();

	std::string contentType;
	if (!hasResponseHeader(response.headers, "content-type", contentType))
		contentType = "text/html";

	std::ostringstream len;
	len << body.size();

	std::ostringstream out;
	out << "HTTP/1.1 " << status << " " << reason << "\r\n";
	out << "Content-Type: " << contentType << "\r\n";
	out << "Content-Length: " << len.str() << "\r\n";
	for (std::map<std::string, std::string>::const_iterator it = response.headers.begin(); it != response.headers.end(); ++it) {
		std::string name = lowerCase(it->first);
		if (name == "content-type" || name == "content-length" || !isResponseHeader(it->first, it->second))
			continue;
		out << it->first << ": " << it->second << "\r\n";
	}
	out << "\r\n" << body;

	return out.str();
}
