#ifndef UTILS_HPP
#define UTILS_HPP

#include <string>

std::string lowerCase(const std::string &value);
std::string trim(const std::string &value);
bool        matchesLocation(const std::string &path, const std::string &location);
bool        hasDotSegment(const std::string &path);

#endif
