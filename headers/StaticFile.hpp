#ifndef STATICFILE_HPP
#define STATICFILE_HPP

#include <map>
#include <string>
#include "types.hpp"

// Reads a regular file beneath a resolved route root and resolves its MIME type.
class StaticFile {
	public:
		StaticFile();
		~StaticFile();

		Content serve(const Route &route, const Request &request);

	private:
		std::map<std::string, std::string> mime_types;
};

#endif
