#include "StaticFile.hpp"

#include <cctype>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

StaticFile::StaticFile() {
	mime_types[".html"] = "text/html";
	mime_types[".htm"]  = "text/html";
	mime_types[".css"]  = "text/css";
	mime_types[".js"]   = "application/javascript";
	mime_types[".json"] = "application/json";
	mime_types[".png"]  = "image/png";
	mime_types[".jpg"]  = "image/jpeg";
	mime_types[".jpeg"] = "image/jpeg";
	mime_types[".gif"]  = "image/gif";
	mime_types[".ico"]  = "image/x-icon";
	mime_types[".txt"]  = "text/plain";
}

StaticFile::~StaticFile() {}

static bool matchesLocation(const std::string &path, const std::string &location) {
	if (location.empty() || path.compare(0, location.size(), location) != 0)
		return false;
	if (location == "/" || path.size() == location.size())
		return true;
	return location[location.size() - 1] == '/' || path[location.size()] == '/';
}

static bool hasUnsafeSegment(const std::string &path) {
	std::string::size_type start = 0;
	while (start < path.size()) {
		std::string::size_type end = path.find('/', start);
		if (end == std::string::npos)
			end = path.size();
		std::string segment = path.substr(start, end - start);
		if (segment == "." || segment == "..")
			return true;
		if (end == path.size())
			break;
		start = end + 1;
	}
	return false;
}

static bool resolvePath(const Route &route, const Request &request, std::string &path) {
	if (route.root.empty() || !matchesLocation(request.path, route.location))
		return false;

	std::string relative = request.path.substr(route.location.size());
	while (!relative.empty() && relative[0] == '/')
		relative.erase(0, 1);
	if (relative.empty() || hasUnsafeSegment(relative))
		return false;

	path = route.root;
	if (path[path.size() - 1] != '/')
		path += '/';
	path += relative;
	return true;
}

static std::string mimeType(const std::map<std::string, std::string> &mimeTypes, const std::string &path) {
	std::string::size_type dot = path.find_last_of('.');
	std::string::size_type slash = path.find_last_of('/');
	if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
		return "application/octet-stream";

	std::string extension = path.substr(dot);
	for (std::string::size_type i = 0; i < extension.size(); ++i)
		extension[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(extension[i])));
	std::map<std::string, std::string>::const_iterator it = mimeTypes.find(extension);
	return it == mimeTypes.end() ? "application/octet-stream" : it->second;
}

static Content errorContent(int status) {
	Content content;
	content.status = status;
	return content;
}

Content StaticFile::serve(const Route &route, const Request &request) {
	std::string path;
	if (!resolvePath(route, request, path))
		return errorContent(403);

	struct stat info;
	if (stat(path.c_str(), &info) != 0)
		return errorContent(404);
	if (!S_ISREG(info.st_mode))
		return errorContent(403);

	int fd = open(path.c_str(), O_RDONLY);
	if (fd < 0)
		return errorContent(403);

	Content content;
	content.status = 200;
	content.mime_type = mimeType(mime_types, path);
	if (info.st_size > 0)
		content.body.reserve(static_cast<size_t>(info.st_size));

	char buffer[4096];
	while (true) {
		ssize_t readCount = read(fd, buffer, sizeof(buffer));
		if (readCount < 0) {
			close(fd);
			return errorContent(500);
		}
		if (readCount == 0)
			break;
		content.body.append(buffer, static_cast<size_t>(readCount));
	}
	close(fd);
	return content;
}
