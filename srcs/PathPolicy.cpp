#include "PathPolicy.hpp"

#include <dirent.h>

// is in separate file to not violate separation of concerns, as both Cgi and StaticFile depend on this
bool pathHasSymlink(const std::string &root, const std::string &relative) {
	std::string directory = root.empty() ? "." : root;
	std::string::size_type start = 0;
	while (start <= relative.size()) {
		std::string::size_type end = relative.find('/', start);
		if (end == std::string::npos)
			end = relative.size();
		const std::string component = relative.substr(start, end - start);
		start = end + 1;
		if (component.empty())
			continue;

		DIR *listing = opendir(directory.c_str());
		if (listing == NULL)
			return false;
		bool isLink = false;
		struct dirent *entry = NULL;
		while ((entry = readdir(listing)) != NULL) {
			if (component == entry->d_name) {
				isLink = entry->d_type == DT_LNK;
				break;
			}
		}
		closedir(listing);
		if (isLink)
			return true;
		if (directory[directory.size() - 1] != '/')
			directory += '/';
		directory += component;
	}
	return false;
}
