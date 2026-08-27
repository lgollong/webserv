#ifndef STATICFILE_HPP
#define STATICFILE_HPP

#include <map>
#include <string>
#include "types.hpp"

// Serves or removes a regular file beneath a resolved route root.
class StaticFile {
	public:
		StaticFile();
		~StaticFile();

		Content serve(const Route &route, const Request &request);
		Content erase(const Route &route, const Request &request);
		Content upload(const Route &route, const Request &request);
		Content readErrorPage(const std::string &path);

	private:
		std::map<std::string, std::string> mime_types;
};

#endif
