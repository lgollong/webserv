#ifndef STATICFILE_HPP
#define STATICFILE_HPP

#include <map>
#include <string>
#include "types.hpp"

// Reads a file from disk, resolves its MIME type, autoindex/upload.
class StaticFile {
	public:
		StaticFile();
		~StaticFile();

		Content serve(const Route& route, const Request& request);

	private:
		std::map<std::string, std::string> mime_types;
};

#endif
