// Reads units from the systemd system and user managers over D-Bus.
#pragma once

#include "model/unit.hpp"

#include <string>
#include <vector>

namespace nyst {

/// Reads every loaded unit and every not-loaded unit file of one manager.
/// Units come back unclassified: origin is Unknown and runAsUser holds the raw User= value.
/// If the bus cannot be reached, returns an empty list and describes why in connectionError.
std::vector<Unit> readUnitsFromManager(Manager manager, std::string& connectionError);

} // namespace nyst
