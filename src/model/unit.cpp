// Unit struct, its enums, and their string conversions.
#include "model/unit.hpp"

namespace nyst {

std::string toString(Manager manager) {
    switch (manager) {
    case Manager::System:
        return "system";
    case Manager::User:
        return "user";
    }
    return "unknown";
}

std::string toString(ActiveState state) {
    switch (state) {
    case ActiveState::Active:
        return "active";
    case ActiveState::Inactive:
        return "inactive";
    case ActiveState::Failed:
        return "failed";
    case ActiveState::Activating:
        return "activating";
    case ActiveState::Deactivating:
        return "deactivating";
    case ActiveState::Reloading:
        return "reloading";
    case ActiveState::Unknown:
        return "unknown";
    }
    return "unknown";
}

std::string toString(Origin origin) {
    switch (origin) {
    case Origin::SystemdDefault:
        return "systemd";
    case Origin::Package:
        return "package";
    case Origin::AdminCreated:
        return "admin";
    case Origin::UserCreated:
        return "user";
    case Origin::Generated:
        return "generated";
    case Origin::Transient:
        return "transient";
    case Origin::Unowned:
        return "unowned";
    case Origin::Missing:
        return "missing";
    case Origin::Unknown:
        return "unknown";
    }
    return "unknown";
}

std::string toString(EdgeKind kind) {
    switch (kind) {
    case EdgeKind::Requires:
        return "requires";
    case EdgeKind::Requisite:
        return "requisite";
    case EdgeKind::Wants:
        return "wants";
    case EdgeKind::BindsTo:
        return "binds-to";
    case EdgeKind::PartOf:
        return "part-of";
    case EdgeKind::Upholds:
        return "upholds";
    case EdgeKind::Triggers:
        return "triggers";
    }
    return "unknown";
}

ActiveState activeStateFromString(const std::string& text) {
    if (text == "active") {
        return ActiveState::Active;
    }
    if (text == "inactive") {
        return ActiveState::Inactive;
    }
    if (text == "failed") {
        return ActiveState::Failed;
    }
    if (text == "activating") {
        return ActiveState::Activating;
    }
    if (text == "deactivating") {
        return ActiveState::Deactivating;
    }
    // "refreshing" is systemd's reload variant for mount/extension images.
    if (text == "reloading" || text == "refreshing") {
        return ActiveState::Reloading;
    }
    return ActiveState::Unknown;
}

std::vector<Origin> allOrigins() {
    return {Origin::SystemdDefault, Origin::Package,   Origin::AdminCreated,
            Origin::UserCreated,    Origin::Generated, Origin::Transient,
            Origin::Unowned,        Origin::Missing,   Origin::Unknown};
}

std::string makeUnitKey(Manager manager, const std::string& name) {
    return toString(manager) + ":" + name;
}

std::string unitTypeFromName(const std::string& name) {
    std::size_t dot = name.rfind('.');
    if (dot == std::string::npos) {
        return "";
    }
    return name.substr(dot + 1);
}

Unit makeMissingPlaceholder(Manager manager, const std::string& name) {
    Unit unit;
    unit.key = makeUnitKey(manager, name);
    unit.name = name;
    unit.type = unitTypeFromName(name);
    unit.manager = manager;
    unit.description = "(referenced but not found)";
    unit.loadState = "not-found";
    unit.activeState = ActiveState::Inactive;
    unit.subState = "dead";
    unit.origin = Origin::Missing;
    unit.isLoaded = false;
    return unit;
}

bool isMasked(const Unit& unit) {
    return unit.loadState == "masked";
}

bool neverStartedThisBoot(const Unit& unit) {
    bool skippedByCondition = unit.conditionUsec != 0 && !unit.conditionResult;
    return unit.isLoaded && unit.loadState == "loaded" &&
           unit.activeState == ActiveState::Inactive && unit.activatingUsec == 0 &&
           !skippedByCondition;
}

bool hasWarning(const Unit& unit) {
    return !unit.wantedBy.empty() || !unit.droppedByCycle.empty();
}

std::uint64_t startupDurationUsec(const Unit& unit) {
    if (unit.activatingUsec == 0) {
        return 0;
    }
    // Same rule as systemd-analyze blame: fall back to "went inactive again" only for
    // units that never became active at all.
    if (unit.activeEnterUsec != 0) {
        return unit.activeEnterUsec > unit.activatingUsec
                   ? unit.activeEnterUsec - unit.activatingUsec
                   : 0;
    }
    if (unit.inactiveEnterUsec > unit.activatingUsec) {
        return unit.inactiveEnterUsec - unit.activatingUsec;
    }
    return 0;
}

bool isRunning(const Unit& unit) {
    if (unit.type == "scope") {
        return unit.subState == "running" || unit.subState == "abandoned";
    }
    // reload, reload-signal, reload-notify: still running, just reloading its config.
    return unit.type == "service" &&
           (unit.subState == "running" || unit.subState.rfind("reload", 0) == 0);
}

void applyUsageSample(const UsageSample& sample, std::uint64_t maxGapUsec, Unit& unit) {
    unit.mainPid = sample.mainPid;
    unit.memoryBytes = sample.memoryBytes;
    unit.tasksCurrent = sample.tasksCurrent;
    if (sample.sampledUsec == 0) {
        unit.cpuPercent = -1;
        return;
    }
    bool hasPrevious = unit.cpuSampleUsec != 0 && sample.sampledUsec > unit.cpuSampleUsec &&
                       sample.cpuUsageNsec >= unit.cpuUsageNsec;
    std::uint64_t gapUsec = sample.sampledUsec - unit.cpuSampleUsec;
    if (hasPrevious && gapUsec <= maxGapUsec) {
        double usedNsec = static_cast<double>(sample.cpuUsageNsec - unit.cpuUsageNsec);
        unit.cpuPercent = usedNsec / (static_cast<double>(gapUsec) * 1000.0) * 100.0;
    } else {
        unit.cpuPercent = -1;
    }
    unit.cpuUsageNsec = sample.cpuUsageNsec;
    unit.cpuSampleUsec = sample.sampledUsec;
}

void copyRuntimeState(const Unit& from, Unit& to) {
    to.loadState = from.loadState;
    to.activeState = from.activeState;
    to.subState = from.subState;
    to.unitFileState = from.unitFileState;
    to.result = from.result;
    to.mainExitKind = from.mainExitKind;
    to.mainExitStatus = from.mainExitStatus;
    to.restartCount = from.restartCount;
    to.mainPid = from.mainPid;
    to.memoryBytes = from.memoryBytes;
    to.tasksCurrent = from.tasksCurrent;
    to.cpuUsageNsec = from.cpuUsageNsec;
    to.cpuSampleUsec = from.cpuSampleUsec;
    to.lastTriggerUsec = from.lastTriggerUsec;
    to.nextElapseUsec = from.nextElapseUsec;
    to.activatingUsec = from.activatingUsec;
    to.activeEnterUsec = from.activeEnterUsec;
    to.inactiveEnterUsec = from.inactiveEnterUsec;
    to.conditionUsec = from.conditionUsec;
    to.conditionResult = from.conditionResult;
    to.conditions = from.conditions;
    to.failedConditions = from.failedConditions;
    to.startTimeoutUsec = from.startTimeoutUsec;
}

// Requisite= only checks that the target is already running, and PartOf=/Triggers=
// never start anything, so they don't count.
bool pullsIn(EdgeKind kind) {
    return kind == EdgeKind::Requires || kind == EdgeKind::Wants || kind == EdgeKind::BindsTo ||
           kind == EdgeKind::Upholds;
}

bool isShellSafeUnitName(const std::string& name) {
    if (name.empty()) {
        return false;
    }
    for (char character : name) {
        bool allowed =
            (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == ':' || character == '_' ||
            character == '.' || character == '@' || character == '-' || character == '\\';
        if (!allowed) {
            return false;
        }
    }
    return true;
}

} // namespace nyst
