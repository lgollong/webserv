#include "PathPolicy.hpp"

#include <dirent.h>

// True when one of relative's '/'-separated components, looked up from root
// downwards, is a symbolic link. A missing component is left to the caller's
// stat()/open(), which reports it as 404 or 403.
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
