#ifndef PATHPOLICY_HPP
#define PATHPOLICY_HPP

#include <string>

// Symlinks below a configured root are never followed. The subject allows
// neither realpath() nor lstat()/readlink(), so links are detected through
// readdir()'s d_type. A file system that reports DT_UNKNOWN cannot be checked;
// APFS, ext4, xfs, btrfs, tmpfs, and overlayfs report types.
bool pathHasSymlink(const std::string &root, const std::string &relative);

#endif
