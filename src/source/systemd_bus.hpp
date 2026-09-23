// Reads units from the systemd system and user managers over D-Bus.
#pragma once

#include "model/unit.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sdbus {
class IConnection;
}

namespace nyst {

/// Everything read from one manager in one go.
struct ManagerSnapshot {
    /// Unclassified: origin is Unknown and runAsUser holds the raw User= value.
    std::vector<Unit> units;
    /// Monotonic time (microseconds) the manager finished starting up; 0 if unknown.
    std::uint64_t bootFinishedUsec = 0;
    /// Why the bus could not be reached (units is then empty), or empty.
    std::string connectionError;
};

/// Reads every loaded unit and every not-loaded unit file of one manager.
ManagerSnapshot readManager(Manager manager);

/// Opens a new connection to the bus of the manager (system bus or session bus).
/// Throws sdbus::Error; for use inside source/ only.
std::unique_ptr<sdbus::IConnection> connectToManager(Manager manager);

/// Reads one loaded unit by its D-Bus object path, like readUnitsFromManager does.
/// Throws sdbus::Error (e.g. if the unit was unloaded meanwhile); for use inside source/ only.
Unit readUnitAt(sdbus::IConnection& connection, const std::string& objectPath, Manager manager);

} // namespace nyst
