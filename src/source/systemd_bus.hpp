// Reads units from the systemd system and user managers over D-Bus.
#pragma once

#include "model/unit.hpp"

#include <memory>
#include <string>
#include <vector>

namespace sdbus {
class IConnection;
}

namespace nyst {

/// Reads every loaded unit and every not-loaded unit file of one manager.
/// Units come back unclassified: origin is Unknown and runAsUser holds the raw User= value.
/// If the bus cannot be reached, returns an empty list and describes why in connectionError.
std::vector<Unit> readUnitsFromManager(Manager manager, std::string& connectionError);

/// Opens a new connection to the bus of the manager (system bus or session bus).
/// Throws sdbus::Error; for use inside source/ only.
std::unique_ptr<sdbus::IConnection> connectToManager(Manager manager);

/// Reads one loaded unit by its D-Bus object path, like readUnitsFromManager does.
/// Throws sdbus::Error (e.g. if the unit was unloaded meanwhile); for use inside source/ only.
Unit readUnitAt(sdbus::IConnection& connection, const std::string& objectPath, Manager manager);

} // namespace nyst
