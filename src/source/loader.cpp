// Single entry point that reads, classifies, and links every unit into a UnitGraph.
#include "source/loader.hpp"

#include "source/classifier.hpp"
#include "source/package_db.hpp"
#include "source/systemd_bus.hpp"
#include "util/debug_log.hpp"

#include <chrono>
#include <cstdlib>
#include <pwd.h>
#include <unistd.h>

namespace nyst {

namespace {

using Clock = std::chrono::steady_clock;

std::string millisecondsSince(Clock::time_point start) {
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start);
    return std::to_string(elapsed.count()) + " ms";
}

std::string environmentOr(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return (value != nullptr && value[0] != '\0') ? value : fallback;
}

ClassifierContext makeClassifierContext() {
    ClassifierContext context;
    const passwd* entry = getpwuid(getuid());
    std::string passwdHome = entry != nullptr ? entry->pw_dir : "";
    context.currentUserName = entry != nullptr ? entry->pw_name : std::to_string(getuid());

    std::string home = environmentOr("HOME", passwdHome);
    std::string configHome = environmentOr("XDG_CONFIG_HOME", home + "/.config");
    std::string dataHome = environmentOr("XDG_DATA_HOME", home + "/.local/share");
    context.userConfigUnitDir = configHome + "/systemd/user/";
    context.userDataUnitDir = dataHome + "/systemd/user/";
    return context;
}

/// Reads one manager into the graph and returns a short status fragment for it.
std::string loadManager(Manager manager, const PackageDb& packages,
                        const ClassifierContext& context, UnitGraph& graph) {
    auto start = Clock::now();
    std::string connectionError;
    std::vector<Unit> units = readUnitsFromManager(manager, connectionError);

    for (Unit& unit : units) {
        classifyUnit(unit, packages, context);
        graph.addUnit(unit);
    }
    debugLog("loaded " + std::to_string(units.size()) + " " + toString(manager) + " units in " +
             millisecondsSince(start));

    if (!connectionError.empty()) {
        std::string label = manager == Manager::System ? "system" : "user";
        return label + " units unavailable (" + connectionError + ")";
    }
    return toString(manager) + " bus ok";
}

std::size_t countFailedUnits(const UnitGraph& graph) {
    std::size_t failed = 0;
    for (const auto& [key, unit] : graph.allUnits()) {
        if (unit.activeState == ActiveState::Failed) {
            ++failed;
        }
    }
    return failed;
}

} // namespace

UnitGraph loadEverything(std::string& statusMessage) {
    auto start = Clock::now();
    UnitGraph graph;

    auto packageStart = Clock::now();
    PackageDb packages;
    debugLog("package db: " + std::to_string(packages.fileCount()) + " files in " +
             millisecondsSince(packageStart));

    ClassifierContext context = makeClassifierContext();
    std::string systemStatus = loadManager(Manager::System, packages, context, graph);
    std::string userStatus = loadManager(Manager::User, packages, context, graph);

    graph.addPlaceholdersForMissingTargets();
    graph.rebuildReverseEdges();
    graph.rebuildDiagnostics();

    statusMessage = std::to_string(graph.allUnits().size()) + " units · " +
                    std::to_string(countFailedUnits(graph)) + " failed · " + systemStatus + " · " +
                    userStatus;
    debugLog("loadEverything finished in " + millisecondsSince(start) + ": " + statusMessage);
    return graph;
}

} // namespace nyst
