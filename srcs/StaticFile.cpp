#include "StaticFile.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

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
	if (hasUnsafeSegment(relative))
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

static Content readRegularFile(const std::string &path, const struct stat &info,
	const std::map<std::string, std::string> &mimeTypes) {
	int fd = open(path.c_str(), O_RDONLY);
	if (fd < 0)
		return errorContent(403);

	Content content;
	content.status = 200;
	content.mime_type = mimeType(mimeTypes, path);
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

static bool safeIndexFile(const std::string &indexFile) {
	return !indexFile.empty() && indexFile[0] != '/' && !hasUnsafeSegment(indexFile);
}

static std::string joinPath(const std::string &directory, const std::string &name) {
	if (directory[directory.size() - 1] == '/')
		return directory + name;
	return directory + "/" + name;
}

Content StaticFile::readErrorPage(const std::string &path) {
	struct stat info;
	if (path.empty() || stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode))
		return errorContent(404);
	return readRegularFile(path, info, mime_types);
}

static std::string htmlEscape(const std::string &value) {
	std::string escaped;
	for (std::string::size_type i = 0; i < value.size(); ++i) {
		switch (value[i]) {
			case '&': escaped += "&amp;"; break;
			case '<': escaped += "&lt;"; break;
			case '>': escaped += "&gt;"; break;
			case '\"': escaped += "&quot;"; break;
			default: escaped += value[i]; break;
		}
	}
	return escaped;
}

static std::string urlEncodePath(const std::string &value) {
	static const char hex[] = "0123456789ABCDEF";
	std::string encoded;
	for (std::string::size_type i = 0; i < value.size(); ++i) {
		unsigned char character = static_cast<unsigned char>(value[i]);
		if (std::isalnum(character) || character == '-' || character == '.' ||
			character == '_' || character == '~' || character == '/')
			encoded += value[i];
		else {
			encoded += '%';
			encoded += hex[character >> 4];
			encoded += hex[character & 0x0F];
		}
	}
	return encoded;
}

struct DirectoryEntry {
	std::string name;
	bool is_directory;

	DirectoryEntry(const std::string &entryName, bool directory)
	: name(entryName), is_directory(directory) {}
};

static bool directoryEntryLess(const DirectoryEntry &left, const DirectoryEntry &right) {
	return left.name < right.name;
}

static Content directoryListing(const std::string &path, const std::string &requestPath) {
	DIR *directory = opendir(path.c_str());
	if (directory == NULL)
		return errorContent(403);

	std::vector<DirectoryEntry> entries;
	struct dirent *entry = NULL;
	while ((entry = readdir(directory)) != NULL) {
		std::string name(entry->d_name);
		if (name == "." || name == "..")
			continue;
		struct stat entryInfo;
		bool isDirectory = stat(joinPath(path, name).c_str(), &entryInfo) == 0 &&
			S_ISDIR(entryInfo.st_mode);
		entries.push_back(DirectoryEntry(name, isDirectory));
	}
	closedir(directory);
	std::sort(entries.begin(), entries.end(), directoryEntryLess);

	std::string base = requestPath.empty() ? "/" : urlEncodePath(requestPath);
	if (base[base.size() - 1] != '/')
		base += '/';

	Content content;
	content.status = 200;
	content.mime_type = "text/html";
	content.body = "<!DOCTYPE html><html><head><title>Index of " + htmlEscape(requestPath) +
		"</title></head><body><h1>Index of " + htmlEscape(requestPath) + "</h1><ul>";
	for (std::vector<DirectoryEntry>::const_iterator it = entries.begin(); it != entries.end(); ++it) {
		std::string displayName = it->name;
		if (it->is_directory)
			displayName += '/';
		content.body += "<li><a href=\"" + htmlEscape(base + urlEncodePath(it->name) +
			(it->is_directory ? "/" : "")) + "\">" + htmlEscape(displayName) + "</a></li>";
	}
	content.body += "</ul></body></html>";
	return content;
}

Content StaticFile::serve(const Route &route, const Request &request) {
	std::string path;
	if (!resolvePath(route, request, path))
		return errorContent(403);

	struct stat info;
	if (stat(path.c_str(), &info) != 0)
		return errorContent(404);
	if (S_ISREG(info.st_mode))
		return readRegularFile(path, info, mime_types);
	if (!S_ISDIR(info.st_mode))
		return errorContent(403);

	if (!route.index_file.empty()) {
		if (!safeIndexFile(route.index_file))
			return errorContent(403);
		std::string indexPath = joinPath(path, route.index_file);
		struct stat indexInfo;
		if (stat(indexPath.c_str(), &indexInfo) == 0 && S_ISREG(indexInfo.st_mode))
			return readRegularFile(indexPath, indexInfo, mime_types);
	}
	if (route.autoindex)
		return directoryListing(path, request.path);
	return errorContent(403);
}

Content StaticFile::erase(const Route &route, const Request &request) {
	std::string path;
	if (!resolvePath(route, request, path))
		return errorContent(403);

	struct stat info;
	if (stat(path.c_str(), &info) != 0)
		return errorContent(404);
	if (!S_ISREG(info.st_mode))
		return errorContent(403);
	if (std::remove(path.c_str()) != 0)
		return errorContent(500);

	Content content;
	content.status = 204;
	return content;
}

static bool safeUploadName(const std::string &name) {
	if (name.empty() || name == "." || name == ".." || name.find('/') != std::string::npos)
		return false;
	for (std::string::size_type i = 0; i < name.size(); ++i) {
		unsigned char character = static_cast<unsigned char>(name[i]);
		if (!std::isalnum(character) && character != '.' && character != '_' && character != '-')
			return false;
	}
	return true;
}

Content StaticFile::upload(const Route &route, const Request &request) {
	if (route.upload_store.empty() || !matchesLocation(request.path, route.location))
		return errorContent(403);
	std::string name = request.path.substr(route.location.size());
	while (!name.empty() && name[0] == '/')
		name.erase(0, 1);
	if (!safeUploadName(name))
		return errorContent(400);
	std::string path = joinPath(route.upload_store, name);
	int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
	if (fd < 0)
		return errorContent(403);
	std::string::size_type written = 0;
	while (written < request.body.size()) {
		ssize_t count = write(fd, request.body.data() + written, request.body.size() - written);
		if (count <= 0) {
			close(fd);
			std::remove(path.c_str());
			return errorContent(500);
		}
		written += static_cast<std::string::size_type>(count);
	}
	close(fd);
	Content content;
	content.status = 201;
	return content;
}
