#include "StaticFile.hpp"
#include "utils.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
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

static std::string joinPath(const std::string &directory, const std::string &name) {
	if (directory[directory.size() - 1] == '/')
		return directory + name;
	return directory + "/" + name;
}

static bool resolvePath(const Route &route, const Request &request, std::string &path) {
	std::string base = route.root;
	if (route.upload_enabled)
		base = route.upload_store;
	if (!matchesLocation(request.path, route.location))
		return false;
	std::string relative = request.path.substr(route.location.size());
	while (!relative.empty() && relative[0] == '/')
		relative.erase(0, 1);
	if (hasDotSegment(relative))
		return false;
	path = joinPath(base, relative);
	return true;
}

static std::string mimeType(const std::map<std::string, std::string> &mimeTypes, const std::string &path) {
	std::string::size_type dot = path.find_last_of('.');
	if (dot == std::string::npos || path.find('/', dot) != std::string::npos)
		return "application/octet-stream";
	std::map<std::string, std::string>::const_iterator it = mimeTypes.find(path.substr(dot));
	if (it == mimeTypes.end())
		return "application/octet-stream";
	return it->second;
}

static Content errorContent(int status) {
	Content content;
	content.status = status;
	return content;
}

static int statusForFilesystemError(int error) {
	if (error == ENOENT || error == ENOTDIR || error == ENAMETOOLONG)
		return 404;
	if (error == EACCES || error == EPERM)
		return 403;
	return 500;
}

static Content openRegularFile(const std::string &path, const struct stat &info,
	const std::map<std::string, std::string> &mimeTypes) {
	int fd = open(path.c_str(), O_RDONLY);
	if (fd < 0)
		return errorContent(statusForFilesystemError(errno));
	Content content;
	content.status = 200;
	content.mime_type = mimeType(mimeTypes, path);
	content.file_fd = fd;
	content.file_size = static_cast<size_t>(info.st_size);
	return content;
}

Content StaticFile::readErrorPage(const std::string &path) {
	int fd = open(path.c_str(), O_RDONLY);
	if (fd < 0)
		return errorContent(404);
	Content content;
	content.status = 200;
	content.mime_type = mimeType(mime_types, path);
	char buffer[4096];
	ssize_t count;
	while ((count = read(fd, buffer, sizeof(buffer))) > 0)
		content.body.append(buffer, static_cast<size_t>(count));
	close(fd);
	if (count < 0)
		return errorContent(500);
	return content;
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

static Content directoryListing(const std::string &path, const std::string &requestPath) {
	DIR *directory = opendir(path.c_str());
	if (directory == NULL)
		return errorContent(statusForFilesystemError(errno));
	std::vector<std::string> names;
	struct dirent *entry;
	while ((entry = readdir(directory)) != NULL) {
		std::string name(entry->d_name);
		if (name != "." && name != "..") {
			if (entry->d_type == DT_DIR)
				name += "/";
			names.push_back(name);
		}
	}
	closedir(directory);
	std::sort(names.begin(), names.end());

	std::string base = requestPath;
	if (base[base.size() - 1] != '/')
		base += '/';
	Content content;
	content.status = 200;
	content.mime_type = "text/html";
	content.body = "<!DOCTYPE html><html><head><title>Index of " + htmlEscape(requestPath) +
		"</title></head><body><h1>Index of " + htmlEscape(requestPath) + "</h1><ul>";
	for (std::vector<std::string>::const_iterator it = names.begin(); it != names.end(); ++it)
		content.body += "<li><a href=\"" + htmlEscape(base + *it) + "\">" + htmlEscape(*it) + "</a></li>";
	content.body += "</ul></body></html>";
	return content;
}

Content StaticFile::serve(const Route &route, const Request &request) {
	std::string path;
	if (!resolvePath(route, request, path))
		return errorContent(403);

	struct stat info;
	if (stat(path.c_str(), &info) != 0)
		return errorContent(statusForFilesystemError(errno));
	if (S_ISREG(info.st_mode))
		return openRegularFile(path, info, mime_types);
	if (!S_ISDIR(info.st_mode))
		return errorContent(403);

	if (!route.index_file.empty()) {
		std::string indexPath = joinPath(path, route.index_file);
		if (stat(indexPath.c_str(), &info) == 0 && S_ISREG(info.st_mode))
			return openRegularFile(indexPath, info, mime_types);
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
		return errorContent(statusForFilesystemError(errno));
	if (!S_ISREG(info.st_mode))
		return errorContent(403);
	if (std::remove(path.c_str()) != 0)
		return errorContent(statusForFilesystemError(errno));
	return errorContent(204);
}

static std::string sanitizeUploadName(const std::string &filename) {
	std::string name = filename.substr(filename.find_last_of("/\\") + 1);
	for (std::string::size_type i = 0; i < name.size(); ++i) {
		if (!std::isalnum(static_cast<unsigned char>(name[i])) && name[i] != '.' && name[i] != '-')
			name[i] = '_';
	}
	if (name.empty() || name == "." || name == "..")
		return "";
	return name;
}

static int storeFile(const std::string &path, const std::string &data) {
	int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
	if (fd < 0) {
		if (errno == EEXIST)
			return 409;
		return statusForFilesystemError(errno);
	}
	ssize_t written = write(fd, data.data(), data.size());
	close(fd);
	if (written != static_cast<ssize_t>(data.size())) {
		std::remove(path.c_str());
		return 500;
	}
	return 201;
}

static Content uploadMultipart(const Route &route, const Request &request, const std::string &contentType) {
	std::string::size_type boundaryPos = contentType.find("boundary=");
	if (boundaryPos == std::string::npos)
		return errorContent(400);
	std::string boundary = contentType.substr(boundaryPos + 9);
	boundary = boundary.substr(0, boundary.find(';'));
	if (boundary.size() >= 2 && boundary[0] == '"')
		boundary = boundary.substr(1, boundary.size() - 2);
	const std::string delimiter = "\r\n--" + boundary;

	const std::string body = "\r\n" + request.body;
	std::string::size_type pos = body.find(delimiter);
	int stored = 0;
	while (pos != std::string::npos && body.compare(pos + delimiter.size(), 2, "--") != 0) {
		std::string::size_type headersStart = pos + delimiter.size() + 2;
		std::string::size_type dataStart = body.find("\r\n\r\n", headersStart);
		std::string::size_type next = body.find(delimiter, headersStart);
		if (dataStart == std::string::npos || next == std::string::npos || dataStart > next)
			return errorContent(400);
		std::string headers = body.substr(headersStart, dataStart - headersStart);
		std::string::size_type filename = headers.find("filename=\"");
		if (filename != std::string::npos) {
			filename += 10;
			std::string name = sanitizeUploadName(headers.substr(filename, headers.find('"', filename) - filename));
			if (!name.empty()) {
				int status = storeFile(joinPath(route.upload_store, name), body.substr(dataStart + 4, next - dataStart - 4));
				if (status != 201)
					return errorContent(status);
				++stored;
			}
		}
		pos = next;
	}
	if (stored == 0)
		return errorContent(400);
	Content content = errorContent(201);
	content.mime_type = "text/html";
	content.body = "<!DOCTYPE html><html><body><p>Upload complete. <a href=\"" + htmlEscape(route.location) +
		"/\">Stored files</a></p></body></html>";
	return content;
}

Content StaticFile::upload(const Route &route, const Request &request) {
	std::map<std::string, std::string>::const_iterator contentType = request.headers.find("content-type");
	if (contentType != request.headers.end() && contentType->second.compare(0, 19, "multipart/form-data") == 0)
		return uploadMultipart(route, request, contentType->second);

	std::string name = request.path.substr(route.location.size());
	while (!name.empty() && name[0] == '/')
		name.erase(0, 1);
	if (name.empty() || name != sanitizeUploadName(name))
		return errorContent(400);
	Content content = errorContent(storeFile(joinPath(route.upload_store, name), request.body));
	if (content.status == 201)
		content.location = request.path;
	return content;
}
