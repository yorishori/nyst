// Decides each unit's Origin, owning package, flags, and effective user.
#include "source/classifier.hpp"

#include <filesystem>

namespace nyst {

namespace {

bool startsWith(const std::string& text, const std::string& prefix) {
    return !prefix.empty() && text.compare(0, prefix.size(), prefix) == 0;
}

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

// Runtime paths exist both for the system manager (/run/systemd/...) and for
// each user manager (/run/user/<uid>/systemd/...), so match the shared part.
bool isTransientPath(const std::string& path) {
    return startsWith(path, "/run/") && contains(path, "/systemd/transient/");
}

bool isGeneratedPath(const std::string& path) {
    return startsWith(path, "/run/") && contains(path, "/systemd/generator");
}

bool isUserCreatedPath(const std::string& path, const ClassifierContext& context) {
    return startsWith(path, context.userConfigUnitDir) || startsWith(path, context.userDataUnitDir);
}

Origin originOfVendorPath(const std::string& path, const PackageDb& packages, Unit& unit) {
    std::string owner = packages.ownerOf(path);
    if (owner.empty()) {
        return Origin::Unowned;
    }
    unit.package = owner;
    return owner == "systemd" ? Origin::SystemdDefault : Origin::Package;
}

Origin decideOrigin(Unit& unit, const PackageDb& packages, const ClassifierContext& context) {
    const std::string& path = unit.fragmentPath;
    if (unit.loadState == "not-found" || unit.origin == Origin::Missing) {
        return Origin::Missing;
    }
    if (isTransientPath(path)) {
        return Origin::Transient;
    }
    if (isGeneratedPath(path)) {
        return Origin::Generated;
    }
    if (isUserCreatedPath(path, context)) {
        return Origin::UserCreated;
    }
    if (startsWith(path, "/etc/systemd/")) {
        return Origin::AdminCreated;
    }
    if (startsWith(path, "/usr/")) {
        return originOfVendorPath(path, packages, unit);
    }
    return Origin::Unknown;
}

bool hasDropInOutsideVendorDir(const Unit& unit) {
    for (const std::string& dropIn : unit.dropInPaths) {
        if (!startsWith(dropIn, "/usr/lib/")) {
            return true;
        }
    }
    return false;
}

bool shadowsVendorUnit(const Unit& unit) {
    if (unit.origin != Origin::AdminCreated) {
        return false;
    }
    std::string vendorDir =
        unit.manager == Manager::System ? "/usr/lib/systemd/system/" : "/usr/lib/systemd/user/";
    std::error_code ignored;
    return std::filesystem::exists(vendorDir + unit.name, ignored);
}

std::string effectiveUser(const Unit& unit, const ClassifierContext& context) {
    if (unit.manager == Manager::User) {
        return context.currentUserName;
    }
    if (unit.type == "service" && unit.runAsUser.empty()) {
        return "root";
    }
    return unit.runAsUser;
}

} // namespace

void classifyUnit(Unit& unit, const PackageDb& packages, const ClassifierContext& context) {
    unit.package.clear();
    unit.origin = decideOrigin(unit, packages, context);
    unit.locallyModified = hasDropInOutsideVendorDir(unit);
    unit.shadowsPackagedUnit = shadowsVendorUnit(unit);
    unit.runAsUser = effectiveUser(unit, context);
}

} // namespace nyst
