// Maps absolute file paths to the pacman package that owns them (via libalpm).
#pragma once

#include <map>
#include <string>

namespace nyst {

class PackageDb {
public:
    /// Reads the local pacman database once. On failure, logs it and owns nothing.
    PackageDb();

    /// Returns the owning package name, or an empty string if the path is unowned.
    std::string ownerOf(const std::string& path) const;

    std::size_t fileCount() const;

private:
    std::map<std::string, std::string> ownerByPath_;
};

} // namespace nyst
