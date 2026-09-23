// Decides each unit's Origin, owning package, flags, and effective user.
#pragma once

#include "model/unit.hpp"
#include "source/package_db.hpp"

#include <string>

namespace nyst {

/// Facts about the current user that the classification rules depend on.
struct ClassifierContext {
    std::string currentUserName;
    std::string userConfigUnitDir; // e.g. /home/me/.config/systemd/user/
    std::string userDataUnitDir;   // e.g. /home/me/.local/share/systemd/user/
};

/// Sets origin, package, locallyModified, shadowsPackagedUnit, and runAsUser.
/// Expects runAsUser to hold the raw User= value as read from the bus.
void classifyUnit(Unit& unit, const PackageDb& packages, const ClassifierContext& context);

} // namespace nyst
