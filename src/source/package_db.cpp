// Maps absolute file paths to the pacman package that owns them (via libalpm).
#include "source/package_db.hpp"

#include "util/debug_log.hpp"

#include <alpm.h>
#include <alpm_list.h>

namespace nyst {

namespace {

void addPackageFiles(alpm_pkg_t* package, std::map<std::string, std::string>& ownerByPath) {
    std::string packageName = alpm_pkg_get_name(package);
    alpm_filelist_t* files = alpm_pkg_get_files(package);
    if (files == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < files->count; ++i) {
        std::string relativePath = files->files[i].name;
        // Directories end in '/' and are shared by many packages, so they say nothing.
        if (relativePath.empty() || relativePath.back() == '/') {
            continue;
        }
        ownerByPath["/" + relativePath] = packageName;
    }
}

} // namespace

PackageDb::PackageDb() {
    alpm_errno_t initError = ALPM_ERR_OK;
    alpm_handle_t* handle = alpm_initialize("/", "/var/lib/pacman", &initError);
    if (handle == nullptr) {
        debugLog(std::string("alpm_initialize failed: ") + alpm_strerror(initError));
        return;
    }

    alpm_db_t* localDb = alpm_get_localdb(handle);
    for (alpm_list_t* item = alpm_db_get_pkgcache(localDb); item != nullptr;
         item = alpm_list_next(item)) {
        addPackageFiles(static_cast<alpm_pkg_t*>(item->data), ownerByPath_);
    }

    alpm_release(handle);
}

std::string PackageDb::ownerOf(const std::string& path) const {
    auto it = ownerByPath_.find(path);
    return it == ownerByPath_.end() ? "" : it->second;
}

std::size_t PackageDb::fileCount() const {
    return ownerByPath_.size();
}

} // namespace nyst
