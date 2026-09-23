// Reads units from the systemd system and user managers over D-Bus.
#include "source/systemd_bus.hpp"

#include "util/debug_log.hpp"

#include <sdbus-c++/sdbus-c++.h>

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <map>
#include <memory>
#include <set>

namespace nyst {

namespace {

const char* const kSystemdService = "org.freedesktop.systemd1";
const char* const kManagerPath = "/org/freedesktop/systemd1";
const char* const kManagerInterface = "org.freedesktop.systemd1.Manager";
// An empty interface name makes GetAll return the properties of every interface at once:
// the common Unit ones plus the type-specific ones (Service, Timer, ...).
const char* const kAllInterfaces = "";

using PropertyMap = std::map<sdbus::PropertyName, sdbus::Variant>;

/// One row of Manager.ListUnits, signature (ssssssouso).
using ListUnitsRow =
    sdbus::Struct<std::string, std::string, std::string, std::string, std::string, std::string,
                  sdbus::ObjectPath, uint32_t, std::string, sdbus::ObjectPath>;

/// One row of Manager.ListUnitFiles, signature (ss): path and state.
using ListUnitFilesRow = sdbus::Struct<std::string, std::string>;

std::unique_ptr<sdbus::IProxy> makeProxy(sdbus::IConnection& connection, const std::string& path) {
    return sdbus::createProxy(connection, sdbus::ServiceName{kSystemdService},
                              sdbus::ObjectPath{path});
}

/// Reads a property of D-Bus type T, or returns fallback if it is missing or has another type.
template <typename T> T typedProperty(const PropertyMap& properties, const char* name, T fallback) {
    auto it = properties.find(sdbus::PropertyName{name});
    if (it == properties.end() || !it->second.containsValueOfType<T>()) {
        return fallback;
    }
    return it->second.get<T>();
}

std::string stringProperty(const PropertyMap& properties, const char* name) {
    return typedProperty<std::string>(properties, name, "");
}

std::vector<std::string> stringListProperty(const PropertyMap& properties, const char* name) {
    return typedProperty<std::vector<std::string>>(properties, name, {});
}

void appendEdges(const PropertyMap& properties, const char* propertyName, EdgeKind kind,
                 Unit& unit) {
    for (const std::string& target : stringListProperty(properties, propertyName)) {
        unit.dependencies.push_back(Edge{makeUnitKey(unit.manager, target), kind});
    }
}

void applyUnitProperties(const PropertyMap& properties, Unit& unit) {
    std::string description = stringProperty(properties, "Description");
    if (!description.empty()) {
        unit.description = description;
    }
    unit.unitFileState = stringProperty(properties, "UnitFileState");
    unit.fragmentPath = stringProperty(properties, "FragmentPath");
    unit.sourcePath = stringProperty(properties, "SourcePath");
    unit.dropInPaths = stringListProperty(properties, "DropInPaths");

    appendEdges(properties, "Requires", EdgeKind::Requires, unit);
    appendEdges(properties, "Requisite", EdgeKind::Requisite, unit);
    appendEdges(properties, "Wants", EdgeKind::Wants, unit);
    appendEdges(properties, "BindsTo", EdgeKind::BindsTo, unit);
    appendEdges(properties, "PartOf", EdgeKind::PartOf, unit);
    appendEdges(properties, "Upholds", EdgeKind::Upholds, unit);
    appendEdges(properties, "Triggers", EdgeKind::Triggers, unit);
}

std::uint64_t monotonicNowUsec() {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<std::uint64_t>(now.tv_sec) * 1000000 +
           static_cast<std::uint64_t>(now.tv_nsec) / 1000;
}

std::uint64_t realtimeNowUsec() {
    timespec now{};
    clock_gettime(CLOCK_REALTIME, &now);
    return static_cast<std::uint64_t>(now.tv_sec) * 1000000 +
           static_cast<std::uint64_t>(now.tv_nsec) / 1000;
}

// Timers based on OnBootSec= and friends only report a monotonic next elapse;
// convert it to wall-clock time so the UI has one kind of timestamp to show.
std::uint64_t nextElapseAsRealtime(const PropertyMap& properties) {
    std::uint64_t realtime = typedProperty<std::uint64_t>(properties, "NextElapseUSecRealtime", 0);
    if (realtime != 0) {
        return realtime;
    }
    std::uint64_t monotonic =
        typedProperty<std::uint64_t>(properties, "NextElapseUSecMonotonic", 0);
    if (monotonic == 0) {
        return 0;
    }
    return realtimeNowUsec() + monotonic - monotonicNowUsec();
}

void applyRuntimeProperties(const PropertyMap& properties, Unit& unit) {
    unit.result = stringProperty(properties, "Result");
    unit.mainExitKind = typedProperty<std::int32_t>(properties, "ExecMainCode", 0);
    unit.mainExitStatus = typedProperty<std::int32_t>(properties, "ExecMainStatus", 0);
    unit.restartCount = typedProperty<std::uint32_t>(properties, "NRestarts", 0);
    unit.mainPid = typedProperty<std::uint32_t>(properties, "MainPID", 0);
    // systemd reports "unknown" as UINT64_MAX.
    std::uint64_t memory = typedProperty<std::uint64_t>(properties, "MemoryCurrent", 0);
    unit.memoryBytes = memory == UINT64_MAX ? 0 : memory;
    unit.lastTriggerUsec = typedProperty<std::uint64_t>(properties, "LastTriggerUSec", 0);
    unit.nextElapseUsec = nextElapseAsRealtime(properties);
    unit.activatingUsec =
        typedProperty<std::uint64_t>(properties, "InactiveExitTimestampMonotonic", 0);
    unit.activeEnterUsec =
        typedProperty<std::uint64_t>(properties, "ActiveEnterTimestampMonotonic", 0);
    unit.inactiveEnterUsec =
        typedProperty<std::uint64_t>(properties, "InactiveEnterTimestampMonotonic", 0);
    unit.conditionUsec = typedProperty<std::uint64_t>(properties, "ConditionTimestampMonotonic", 0);
    unit.conditionResult = typedProperty<bool>(properties, "ConditionResult", true);
}

void appendError(Unit& unit, const std::string& message) {
    if (!unit.error.empty()) {
        unit.error += "; ";
    }
    unit.error += message;
}

void applyAliases(const PropertyMap& properties, Unit& unit) {
    for (const std::string& name : stringListProperty(properties, "Names")) {
        if (name != unit.name) {
            unit.aliases.push_back(name);
        }
    }
}

PropertyMap readAllProperties(sdbus::IConnection& connection, const std::string& objectPath) {
    auto proxy = makeProxy(connection, objectPath);
    return proxy->getAllProperties().onInterface(kAllInterfaces);
}

void applyAllProperties(const PropertyMap& properties, Unit& unit) {
    applyUnitProperties(properties, unit);
    applyAliases(properties, unit);
    applyRuntimeProperties(properties, unit);
    if (unit.manager == Manager::System && unit.type == "service") {
        unit.runAsUser = stringProperty(properties, "User");
    }
}

void readUnitDetails(sdbus::IConnection& connection, const std::string& objectPath, Unit& unit) {
    try {
        applyAllProperties(readAllProperties(connection, objectPath), unit);
    } catch (const sdbus::Error& error) {
        appendError(unit, "reading properties failed: " + error.getMessage());
        debugLog("properties of " + unit.key + " failed: " + error.getMessage());
    }
}

Unit unitFromListing(const ListUnitsRow& row, Manager manager) {
    Unit unit;
    unit.name = std::get<0>(row);
    unit.key = makeUnitKey(manager, unit.name);
    unit.type = unitTypeFromName(unit.name);
    unit.manager = manager;
    unit.description = std::get<1>(row);
    unit.loadState = std::get<2>(row);
    unit.activeState = activeStateFromString(std::get<3>(row));
    unit.subState = std::get<4>(row);
    unit.isLoaded = true;
    return unit;
}

void readLoadedUnits(sdbus::IConnection& connection, sdbus::IProxy& managerProxy, Manager manager,
                     std::vector<Unit>& units, std::set<std::string>& knownNames) {
    std::vector<ListUnitsRow> rows;
    managerProxy.callMethod("ListUnits").onInterface(kManagerInterface).storeResultsTo(rows);

    for (const ListUnitsRow& row : rows) {
        Unit unit = unitFromListing(row, manager);
        std::string objectPath = std::get<6>(row);
        readUnitDetails(connection, objectPath, unit);
        knownNames.insert(unit.name);
        // Alias unit files (dbus.service -> dbus-broker.service) must not show up as not-loaded.
        knownNames.insert(unit.aliases.begin(), unit.aliases.end());
        units.push_back(unit);
    }
}

bool isTemplateFile(const std::string& name) {
    return name.find("@.") != std::string::npos;
}

Unit unitFromUnitFile(const std::string& path, const std::string& state, Manager manager) {
    Unit unit;
    unit.name = std::filesystem::path(path).filename().string();
    unit.key = makeUnitKey(manager, unit.name);
    unit.type = unitTypeFromName(unit.name);
    unit.manager = manager;
    unit.loadState = "not-loaded";
    unit.activeState = ActiveState::Inactive;
    unit.subState = "dead";
    unit.unitFileState = state;
    unit.fragmentPath = path;
    unit.isLoaded = false;
    return unit;
}

/// Adds unit files systemd has not loaded. Deliberately avoids LoadUnit, which changes state.
void readNotLoadedUnitFiles(sdbus::IProxy& managerProxy, Manager manager, std::vector<Unit>& units,
                            std::set<std::string>& knownNames) {
    std::vector<ListUnitFilesRow> rows;
    managerProxy.callMethod("ListUnitFiles").onInterface(kManagerInterface).storeResultsTo(rows);

    for (const ListUnitFilesRow& row : rows) {
        const std::string& path = std::get<0>(row);
        std::string name = std::filesystem::path(path).filename().string();
        if (isTemplateFile(name) || knownNames.count(name) > 0) {
            continue;
        }
        knownNames.insert(name);
        units.push_back(unitFromUnitFile(path, std::get<1>(row), manager));
    }
}

} // namespace

std::unique_ptr<sdbus::IConnection> connectToManager(Manager manager) {
    if (manager == Manager::System) {
        return sdbus::createSystemBusConnection();
    }
    return sdbus::createSessionBusConnection();
}

Unit readUnitAt(sdbus::IConnection& connection, const std::string& objectPath, Manager manager) {
    PropertyMap properties = readAllProperties(connection, objectPath);
    Unit unit;
    unit.name = stringProperty(properties, "Id");
    unit.key = makeUnitKey(manager, unit.name);
    unit.type = unitTypeFromName(unit.name);
    unit.manager = manager;
    unit.loadState = stringProperty(properties, "LoadState");
    unit.activeState = activeStateFromString(stringProperty(properties, "ActiveState"));
    unit.subState = stringProperty(properties, "SubState");
    unit.isLoaded = true;
    applyAllProperties(properties, unit);
    return unit;
}

std::vector<Unit> readUnitsFromManager(Manager manager, std::string& connectionError) {
    std::vector<Unit> units;
    std::set<std::string> knownNames;
    try {
        auto connection = connectToManager(manager);
        auto managerProxy = makeProxy(*connection, kManagerPath);
        readLoadedUnits(*connection, *managerProxy, manager, units, knownNames);
        readNotLoadedUnitFiles(*managerProxy, manager, units, knownNames);
    } catch (const sdbus::Error& error) {
        connectionError = error.getMessage();
        debugLog(toString(manager) + " bus failed: " + error.getName() + ": " + error.getMessage());
    }
    return units;
}

} // namespace nyst
