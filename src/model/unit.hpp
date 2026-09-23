// Unit struct, its enums, and their string conversions.
#pragma once

#include <cstdint>
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

    // Runtime details. Zero or empty when unknown or not applicable to the unit type.
    std::string result;                  // success, exit-code, timeout, oom-kill, ...
    int mainExitKind = 0;                // ExecMainCode: 1 exited, 2 killed, 3 dumped core
    int mainExitStatus = 0;              // exit code, or signal number when killed/dumped
    std::uint32_t restartCount = 0;      // automatic restarts (services)
    std::uint32_t mainPid = 0;           // running main process (services)
    std::uint64_t memoryBytes = 0;       // current memory use of the unit's cgroup
    std::uint64_t lastTriggerUsec = 0;   // timers: wall clock, microseconds since the epoch
    std::uint64_t nextElapseUsec = 0;    // timers: wall clock, microseconds since the epoch
    std::uint64_t activatingUsec = 0;    // monotonic time it last left "inactive"
    std::uint64_t activeEnterUsec = 0;   // monotonic time it last became active
    std::uint64_t inactiveEnterUsec = 0; // monotonic time it last went back to inactive
    std::uint64_t conditionUsec = 0;     // monotonic time its conditions were last checked
    bool conditionResult = true;         // false if a Condition*= check skipped the unit

    // Diagnostics, filled in by UnitGraph::rebuildDiagnostics(). Names of active units that
    // pull this one in although it never started this boot; empty if nothing is suspicious.
    std::vector<std::string> wantedBy;
    // Set by the loader from the boot journal: the ordering cycle ("a → b → a") that made
    // systemd drop this unit's start job at boot. Empty if that did not happen.
    std::string droppedByCycle;
};

/// Builds the unique graph key for a unit, e.g. "user:pipewire.service".
std::string makeUnitKey(Manager manager, const std::string& name);

/// Returns the type suffix of a unit name ("service" for "sshd.service").
std::string unitTypeFromName(const std::string& name);

/// Creates the stand-in for a unit that some edge references but systemd does not know.
Unit makeMissingPlaceholder(Manager manager, const std::string& name);

bool isMasked(const Unit& unit);

/// True if the unit is loaded but never left "inactive" this boot and no Condition*=
/// check skipped it, i.e. nothing ever tried to start it (or its start job was dropped).
bool neverStartedThisBoot(const Unit& unit);

/// True if the unit has a diagnostic worth a ⚠: never started though wanted, or its start
/// job was dropped at boot by an ordering cycle.
bool hasWarning(const Unit& unit);

/// How long the unit's last start took, like `systemd-analyze blame`: until it became active,
/// or until it gave up and went inactive again (failed units). Microseconds; 0 if unknown.
std::uint64_t startupDurationUsec(const Unit& unit);

/// Pull-in dependencies: ones that make systemd start the target (Wants=, Requires=, ...).
bool pullsIn(EdgeKind kind);

/// True if the name only uses [A-Za-z0-9:_.@\-\\], so it can be single-quoted into a
/// shell command. systemd already restricts names; this refuses anything unexpected.
bool isShellSafeUnitName(const std::string& name);

} // namespace nyst
