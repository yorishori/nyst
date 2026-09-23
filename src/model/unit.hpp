// Unit struct, its enums, and their string conversions.
#pragma once

#include <string>
#include <vector>

namespace nyst {

enum class Manager { System, User };

enum class ActiveState { Active, Inactive, Failed, Activating, Deactivating, Reloading, Unknown };

/// Where a unit's file came from; decided by source/classifier.
enum class Origin {
    SystemdDefault,
    Package,
    AdminCreated,
    UserCreated,
    Generated,
    Transient,
    Unowned,
    Missing,
    Unknown
};

enum class EdgeKind { Requires, Requisite, Wants, BindsTo, PartOf, Upholds, Triggers };

std::string toString(Manager manager);
std::string toString(ActiveState state);
std::string toString(Origin origin);
std::string toString(EdgeKind kind);

/// Parses systemd's ActiveState strings; anything unrecognised becomes Unknown.
ActiveState activeStateFromString(const std::string& text);

/// Every Origin value, in declaration order, for filters and display.
std::vector<Origin> allOrigins();

/// A dependency from one unit to another. Both ends always belong to the same manager.
struct Edge {
    std::string target; // unit key
    EdgeKind kind = EdgeKind::Wants;
};

struct Unit {
    std::string key;  // "<manager>:<name>", e.g. "system:sshd.service"; unique
    std::string name; // "sshd.service"
    std::string type; // "service", "timer", ... (suffix of name)
    Manager manager = Manager::System;
    std::vector<std::string> aliases; // other names, e.g. default.target for graphical.target
    std::string description;
    std::string loadState; // loaded, not-found, masked, error, ...
    ActiveState activeState = ActiveState::Unknown;
    std::string subState;      // running, exited, dead, ...
    std::string unitFileState; // enabled, disabled, static, ...
    std::string runAsUser;     // system services: User= or "root"; user units: current user
    Origin origin = Origin::Unknown;
    std::string package; // owning pacman package, empty if none
    std::string fragmentPath;
    std::string sourcePath; // e.g. /etc/fstab for generated mounts
    std::vector<std::string> dropInPaths;
    bool locallyModified = false;     // has a drop-in outside /usr/lib
    bool shadowsPackagedUnit = false; // file in /etc with same name as one in /usr/lib
    bool isLoaded = false;            // false = only known from ListUnitFiles
    std::vector<Edge> dependencies;   // forward edges
    std::string error;                // non-empty if reading this unit partially failed
};

/// Builds the unique graph key for a unit, e.g. "user:pipewire.service".
std::string makeUnitKey(Manager manager, const std::string& name);

/// Returns the type suffix of a unit name ("service" for "sshd.service").
std::string unitTypeFromName(const std::string& name);

/// Creates the stand-in for a unit that some edge references but systemd does not know.
Unit makeMissingPlaceholder(Manager manager, const std::string& name);

bool isMasked(const Unit& unit);

} // namespace nyst
