#include "Http.hpp"
#include <sstream>

Http::Http() {}

Http::~Http() {}

bool Http::parse(std::string &inbuf, Request &request) {
	if (inbuf.empty())
		return false; // nothing buffered yet, wait for more bytes

	// mock: no real HTTP parsing yet, just pretend whatever showed up
	// in inbuf was one complete GET request and hand back example data.
	request.method = "GET";
	request.path = "/index.html";
	request.query = "";
	request.headers["Host"] = "localhost";
	request.headers["User-Agent"] = "webserv-mock-client/1.0";
	request.body = "";

	inbuf.clear(); // pretend we consumed exactly one request's worth of bytes
	return true;
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
