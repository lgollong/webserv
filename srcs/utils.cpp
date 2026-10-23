#include "utils.hpp"
#include <cctype>

std::string lowerCase(const std::string &value) {
	std::string result(value);
	for (std::string::size_type i = 0; i < result.size(); ++i)
		result[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(result[i])));
	return result;
}

std::string trim(const std::string &value) {
	std::string::size_type first = value.find_first_not_of(" \t");
	if (first == std::string::npos)
		return "";
	return value.substr(first, value.find_last_not_of(" \t") - first + 1);
}

bool matchesLocation(const std::string &path, const std::string &location) {
	if (path.compare(0, location.size(), location) != 0)
		return false;
	return path.size() == location.size() || location[location.size() - 1] == '/' || path[location.size()] == '/';
}

bool hasDotSegment(const std::string &path) {
	const std::string wrapped = "/" + path + "/";
	return wrapped.find("/./") != std::string::npos || wrapped.find("/../") != std::string::npos;
}
