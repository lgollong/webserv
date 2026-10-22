#include "Http.hpp"
#include <sstream>
#include <cctype>
#include <cstdlib>

Http::Http() {}

Http::~Http() {}

static std::string lowerCase(const std::string &value) {
	std::string result(value);
	for (std::string::size_type i = 0; i < result.size(); ++i)
		result[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(result[i])));
	return result;
}

static std::string trim(const std::string &value) {
	std::string::size_type first = value.find_first_not_of(" \t");
	if (first == std::string::npos)
		return "";
	return value.substr(first, value.find_last_not_of(" \t") - first + 1);
}

static int parseRequestLine(const std::string &line, Request &request) {
	std::string::size_type sp1 = line.find(' ');
	std::string::size_type sp2 = std::string::npos;
	if (sp1 != std::string::npos)
		sp2 = line.find(' ', sp1 + 1);
	if (sp2 == std::string::npos || sp1 == 0 || line.find(' ', sp2 + 1) != std::string::npos)
		return 400;

	std::string target = line.substr(sp1 + 1, sp2 - sp1 - 1);
	request.method = line.substr(0, sp1);
	request.version = line.substr(sp2 + 1);
	if (target.empty() || target[0] != '/' || request.version.compare(0, 5, "HTTP/") != 0)
		return 400;
	if (request.version != "HTTP/1.0" && request.version != "HTTP/1.1")
		return 505;

	std::string::size_type qpos = target.find('?');
	request.path = target.substr(0, qpos);
	request.query.clear();
	if (qpos != std::string::npos)
		request.query = target.substr(qpos + 1);
	return 0;
}

static bool parseHeaders(const std::string &block, Request &request) {
	std::string::size_type pos = 0;
	while (pos < block.size()) {
		std::string::size_type end = block.find("\r\n", pos);
		if (end == std::string::npos)
			end = block.size();
		std::string line = block.substr(pos, end - pos);
		std::string::size_type colon = line.find(':');
		if (colon == std::string::npos || colon == 0)
			return false;
		request.headers[lowerCase(line.substr(0, colon))] = trim(line.substr(colon + 1));
		pos = end + 2;
	}
	return true;
}

static ssize_t parseChunkedBody(const std::string &inbuf, size_t pos, size_t maxBodyBytes,
		std::string &body, int &errorStatus) {
	body.clear();
	while (true) {
		std::string::size_type lineEnd = inbuf.find("\r\n", pos);
		if (lineEnd == std::string::npos)
			return 0;
		std::string line = inbuf.substr(pos, lineEnd - pos);
		std::string sizeText = trim(line.substr(0, line.find(';')));
		char *end = NULL;
		size_t size = std::strtoul(sizeText.c_str(), &end, 16);
		if (sizeText.empty() || sizeText.size() > 8 || *end != '\0') {
			errorStatus = 400;
			return -1;
		}
		pos = lineEnd + 2;

		if (size == 0) {
			std::string::size_type bodyEnd = inbuf.find("\r\n\r\n", pos - 2);
			if (bodyEnd == std::string::npos)
				return 0;
			return static_cast<ssize_t>(bodyEnd + 4);
		}
		if (size > maxBodyBytes - body.size()) {
			errorStatus = 413;
			return -1;
		}
		if (inbuf.size() < pos + size + 2)
			return 0;
		if (inbuf.compare(pos + size, 2, "\r\n") != 0) {
			errorStatus = 400;
			return -1;
		}
		body.append(inbuf, pos, size);
		pos += size + 2;
	}
}

ssize_t Http::parse(const std::string &inbuf, Request &request, size_t maxBodyBytes, int &errorStatus) {
	errorStatus = 0;
	std::string::size_type headerEnd = inbuf.find("\r\n\r\n");
	if (headerEnd == std::string::npos || headerEnd + 4 > MAX_HEADER_BYTES) {
		if (headerEnd == std::string::npos && inbuf.size() <= MAX_HEADER_BYTES)
			return 0;
		errorStatus = 431;
		return -1;
	}

	Request parsed;
	std::string::size_type lineEnd = inbuf.find("\r\n");
	errorStatus = parseRequestLine(inbuf.substr(0, lineEnd), parsed);
	if (errorStatus == 0 && lineEnd < headerEnd &&
		!parseHeaders(inbuf.substr(lineEnd + 2, headerEnd - lineEnd - 2), parsed))
		errorStatus = 400;
	if (errorStatus != 0)
		return -1;

	size_t bodyStart = headerEnd + 4;
	std::map<std::string, std::string>::const_iterator it = parsed.headers.find("transfer-encoding");
	if (it != parsed.headers.end() && lowerCase(it->second) == "chunked") {
		ssize_t requestSize = parseChunkedBody(inbuf, bodyStart, maxBodyBytes, parsed.body, errorStatus);
		if (requestSize > 0)
			request = parsed;
		return requestSize;
	}

	size_t contentLength = 0;
	it = parsed.headers.find("content-length");
	if (it != parsed.headers.end()) {
		if (it->second.empty() || it->second.size() > 18 ||
			it->second.find_first_not_of("0123456789") != std::string::npos) {
			errorStatus = 400;
			return -1;
		}
		contentLength = std::strtoul(it->second.c_str(), NULL, 10);
	}
	if (contentLength > maxBodyBytes) {
		errorStatus = 413;
		return -1;
	}
	if (inbuf.size() < bodyStart + contentLength)
		return 0;

	parsed.body = inbuf.substr(bodyStart, contentLength);
	request = parsed;
	return static_cast<ssize_t>(bodyStart + contentLength);
}

static const char *reasonPhrase(int status) {
	switch (status) {
		case 200: return "OK";
		case 201: return "Created";
		case 204: return "No Content";
		case 301: return "Moved Permanently";
		case 302: return "Found";
		case 303: return "See Other";
		case 307: return "Temporary Redirect";
		case 308: return "Permanent Redirect";
		case 400: return "Bad Request";
		case 401: return "Unauthorized";
		case 403: return "Forbidden";
		case 404: return "Not Found";
		case 405: return "Method Not Allowed";
		case 409: return "Conflict";
		case 413: return "Payload Too Large";
		case 431: return "Request Header Fields Too Large";
		case 500: return "Internal Server Error";
		case 501: return "Not Implemented";
		case 502: return "Bad Gateway";
		case 503: return "Service Unavailable";
		case 505: return "HTTP Version Not Supported";
	}
	return NULL;
}

bool Http::supportsStatus(int status) const {
	return reasonPhrase(status) != NULL;
}

Response Http::defaultErrorResponse(int status) const {
	if (status < 400 || !supportsStatus(status))
		status = 500;

	std::ostringstream out;
	out << "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>"
		<< status << " " << reasonPhrase(status) << "</title></head><body><h1>"
		<< status << " " << reasonPhrase(status) << "</h1></body></html>";

	Response response;
	response.status = status;
	response.headers["Content-Type"] = "text/html";
	response.body = out.str();
	return response;
}

std::string Http::build(const Response &response) {
	std::string head = buildHead(response, response.body.size());
	if (response.status == 204)
		return head;
	return head + response.body;
}

std::string Http::buildHead(const Response &response, size_t bodyLength) {
	int status = response.status;
	if (!supportsStatus(status))
		status = 500;
	if (status == 204)
		bodyLength = 0;

	std::ostringstream out;
	out << "HTTP/1.1 " << status << " " << reasonPhrase(status) << "\r\n";
	out << "Content-Length: " << bodyLength << "\r\n";
	std::map<std::string, std::string>::const_iterator type = response.headers.find("Content-Type");
	if (type == response.headers.end() || type->second.empty())
		out << "Content-Type: text/html\r\n";
	for (std::map<std::string, std::string>::const_iterator it = response.headers.begin(); it != response.headers.end(); ++it) {
		if (!it->second.empty())
			out << it->first << ": " << it->second << "\r\n";
	}
	out << "\r\n";
	return out.str();
}
