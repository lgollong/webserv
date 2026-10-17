#include "StaticFile.hpp"

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

// Upload routes keep their files in upload_store, so GET, DELETE, and
// autoindex on the same URL resolve there instead of under the route root.
static const std::string &filesystemBase(const Route &route) {
	if (route.upload_enabled && !route.upload_store.empty())
		return route.upload_store;
	return route.root;
}

static bool resolvePath(const Route &route, const Request &request, std::string &path) {
	const std::string &base = filesystemBase(route);
	if (base.empty() || !matchesLocation(request.path, route.location))
		return false;

	std::string relative = request.path.substr(route.location.size());
	while (!relative.empty() && relative[0] == '/')
		relative.erase(0, 1);
	if (hasUnsafeSegment(relative))
		return false;

	path = base;
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

// Maps the errno left by a failed stat/open/opendir/remove on the local
// filesystem to an HTTP status. The subject forbids inspecting errno only
// after socket/pipe read/write, so storeFile() still maps a failed write()
// to 500 without looking at it.
static int statusForFilesystemError(int error) {
	if (error == ENOENT || error == ENOTDIR || error == ENAMETOOLONG)
		return 404;
	if (error == EACCES || error == EPERM)
		return 403;
	return 500;
}

static Content readRegularFile(const std::string &path, const struct stat &info,
	const std::map<std::string, std::string> &mimeTypes) {
	int fd = open(path.c_str(), O_RDONLY);
	if (fd < 0)
		return errorContent(statusForFilesystemError(errno));

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
		return errorContent(statusForFilesystemError(errno));

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
		return errorContent(statusForFilesystemError(errno));
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
		return errorContent(statusForFilesystemError(errno));
	if (!S_ISREG(info.st_mode))
		return errorContent(403);
	if (std::remove(path.c_str()) != 0)
		return errorContent(statusForFilesystemError(errno));

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

// Writes data to a new file; never replaces an existing one. An existing
// target is a 409 conflict; a missing upload_store is a server
// configuration problem (500), not a missing client resource.
static int storeFile(const std::string &path, const std::string &data) {
	int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
	if (fd < 0) {
		if (errno == EEXIST)
			return 409;
		if (errno == EACCES || errno == EPERM)
			return 403;
		return 500;
	}
	std::string::size_type written = 0;
	while (written < data.size()) {
		ssize_t count = write(fd, data.data() + written, data.size() - written);
		if (count <= 0) {
			close(fd);
			std::remove(path.c_str());
			return 500;
		}
		written += static_cast<std::string::size_type>(count);
	}
	close(fd);
	return 201;
}

static std::string lowerCopy(const std::string &value) {
	std::string lowered(value);
	for (std::string::size_type i = 0; i < lowered.size(); ++i)
		lowered[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(lowered[i])));
	return lowered;
}

static std::string trimSpaces(const std::string &value) {
	std::string::size_type first = value.find_first_not_of(" \t");
	if (first == std::string::npos)
		return "";
	std::string::size_type last = value.find_last_not_of(" \t");
	return value.substr(first, last - first + 1);
}

static bool startsAt(const std::string &value, std::string::size_type pos, const std::string &prefix) {
	return pos <= value.size() && value.compare(pos, prefix.size(), prefix) == 0;
}

// Reads one parameter of a header such as
// `form-data; name="file"; filename="a.txt"`. Quoted values may contain ';'.
static bool headerParameter(const std::string &header, const std::string &name, std::string &value) {
	std::string::size_type pos = header.find(';');
	while (pos != std::string::npos) {
		++pos;
		std::string::size_type equal = header.find('=', pos);
		if (equal == std::string::npos)
			return false;
		std::string key = lowerCopy(trimSpaces(header.substr(pos, equal - pos)));
		std::string parsed;
		std::string::size_type i = equal + 1;
		while (i < header.size() && (header[i] == ' ' || header[i] == '\t'))
			++i;
		if (i < header.size() && header[i] == '"') {
			// Browsers percent-encode '"' in multipart filenames and send
			// backslashes literally (HTML form submission), so nothing is unescaped.
			for (++i; i < header.size() && header[i] != '"'; ++i)
				parsed += header[i];
			if (i >= header.size())
				return false;
			++i;
		} else {
			while (i < header.size() && header[i] != ';')
				parsed += header[i++];
			parsed = trimSpaces(parsed);
		}
		if (key == name) {
			value = parsed;
			return true;
		}
		pos = header.find(';', i);
	}
	return false;
}

static bool isMultipartFormData(const std::string &contentType) {
	std::string type = lowerCopy(trimSpaces(contentType.substr(0, contentType.find(';'))));
	return type == "multipart/form-data";
}

struct UploadPart {
	std::string filename;
	std::string data;
};

// Returns the part's Content-Disposition filename, or false for parts that
// are not files (plain form fields or file inputs left empty).
static bool partFilename(const std::string &headers, std::string &filename) {
	std::string::size_type pos = 0;
	while (pos <= headers.size()) {
		std::string::size_type end = headers.find("\r\n", pos);
		if (end == std::string::npos)
			end = headers.size();
		std::string line = headers.substr(pos, end - pos);
		std::string::size_type colon = line.find(':');
		if (colon != std::string::npos &&
			lowerCopy(trimSpaces(line.substr(0, colon))) == "content-disposition") {
			std::string value = line.substr(colon + 1);
			return lowerCopy(trimSpaces(value.substr(0, value.find(';')))) == "form-data" &&
				headerParameter(value, "filename", filename) && !filename.empty();
		}
		pos = end + 2;
	}
	return false;
}

// multipart/form-data framing (RFC 7578, RFC 2046 section 5.1.1).
static bool parseMultipart(const std::string &body, const std::string &boundary,
	std::vector<UploadPart> &files) {
	const std::string delimiter = "--" + boundary;
	std::string::size_type pos = body.find(delimiter);
	if (pos == std::string::npos || (pos != 0 && (pos < 2 || !startsAt(body, pos - 2, "\r\n"))))
		return false;
	pos += delimiter.size();
	while (true) {
		if (startsAt(body, pos, "--"))
			return true;
		if (!startsAt(body, pos, "\r\n"))
			return false;
		pos += 2;

		std::string headers;
		std::string::size_type dataStart = pos + 2;
		if (!startsAt(body, pos, "\r\n")) {
			std::string::size_type headerEnd = body.find("\r\n\r\n", pos);
			if (headerEnd == std::string::npos)
				return false;
			headers = body.substr(pos, headerEnd - pos);
			dataStart = headerEnd + 4;
		}
		std::string::size_type next = body.find("\r\n" + delimiter, dataStart);
		if (next == std::string::npos)
			return false;

		UploadPart part;
		if (partFilename(headers, part.filename)) {
			part.data = body.substr(dataStart, next - dataStart);
			files.push_back(part);
		}
		pos = next + 2 + delimiter.size();
	}
}

// Browsers send the client's file name; keep its base name and replace
// characters outside the safe upload set instead of rejecting the file.
static std::string sanitizeUploadName(const std::string &filename) {
	std::string::size_type slash = filename.find_last_of("/\\");
	std::string name = (slash == std::string::npos) ? filename : filename.substr(slash + 1);
	for (std::string::size_type i = 0; i < name.size(); ++i) {
		unsigned char character = static_cast<unsigned char>(name[i]);
		if (!std::isalnum(character) && character != '.' && character != '_' && character != '-')
			name[i] = '_';
	}
	return name;
}

static Content uploadMultipart(const Route &route, const Request &request,
	const std::string &contentType) {
	std::string boundary;
	std::vector<UploadPart> parts;
	if (!headerParameter(contentType, "boundary", boundary) || boundary.empty() ||
		boundary.size() > 70 || !parseMultipart(request.body, boundary, parts) || parts.empty())
		return errorContent(400);

	std::vector<std::string> names;
	for (std::vector<UploadPart>::const_iterator it = parts.begin(); it != parts.end(); ++it) {
		std::string name = sanitizeUploadName(it->filename);
		if (!safeUploadName(name) || std::find(names.begin(), names.end(), name) != names.end())
			return errorContent(400);
		names.push_back(name);
	}

	for (size_t i = 0; i < parts.size(); ++i) {
		int status = storeFile(joinPath(route.upload_store, names[i]), parts[i].data);
		if (status != 201) {
			for (size_t stored = 0; stored < i; ++stored)
				std::remove(joinPath(route.upload_store, names[stored]).c_str());
			return errorContent(status);
		}
	}

	std::string base = urlEncodePath(route.location);
	if (base.empty() || base[base.size() - 1] != '/')
		base += '/';
	Content content;
	content.status = 201;
	content.mime_type = "text/html";
	content.body = "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>Upload complete</title>"
		"</head><body><h1>Upload complete</h1><ul>";
	for (std::vector<std::string>::const_iterator it = names.begin(); it != names.end(); ++it)
		content.body += "<li><a href=\"" + htmlEscape(base + urlEncodePath(*it)) + "\">" +
			htmlEscape(*it) + "</a></li>";
	content.body += "</ul></body></html>";
	if (names.size() == 1)
		content.location = base + urlEncodePath(names[0]);
	return content;
}

Content StaticFile::upload(const Route &route, const Request &request) {
	if (route.upload_store.empty() || !matchesLocation(request.path, route.location))
		return errorContent(403);
	std::string name = request.path.substr(route.location.size());
	while (!name.empty() && name[0] == '/')
		name.erase(0, 1);

	// Form uploads post to the location itself and name files in the body.
	std::map<std::string, std::string>::const_iterator contentType = request.headers.find("content-type");
	if (contentType != request.headers.end() && isMultipartFormData(contentType->second)) {
		if (!name.empty())
			return errorContent(400);
		return uploadMultipart(route, request, contentType->second);
	}

	if (!safeUploadName(name))
		return errorContent(400);
	int status = storeFile(joinPath(route.upload_store, name), request.body);
	if (status != 201)
		return errorContent(status);
	Content content;
	content.status = 201;
	content.location = request.path;
	return content;
}
