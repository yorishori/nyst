// Runs systemctl verbs (start, stop, ...) on a unit in the user's terminal.
#include "source/actions.hpp"

#include "util/command.hpp"

#include <iostream>

namespace nyst {

namespace {

bool worksOnUnitFiles(UnitAction action) {
    return action == UnitAction::Enable || action == UnitAction::Disable;
}

std::string systemctlCommand(const Unit& unit, UnitAction action) {
    std::string command = "systemctl";
    if (unit.manager == Manager::User) {
        command += " --user";
    }
    // Single quotes are safe: the caller has checked the name with isShellSafeUnitName.
    return command + " " + toString(action) + " '" + unit.name + "'";
}

void waitForEnter() {
    std::cout << "Press Enter to return to nyst..." << std::flush;
    std::string ignored;
    std::getline(std::cin, ignored);
}

} // namespace

std::string toString(UnitAction action) {
    switch (action) {
    case UnitAction::Start:
        return "start";
    case UnitAction::Stop:
        return "stop";
    case UnitAction::Restart:
        return "restart";
    case UnitAction::Reload:
        return "reload";
    case UnitAction::Enable:
        return "enable";
    case UnitAction::Disable:
        return "disable";
    }
    return "unknown";
}

std::string whyActionUnavailable(const Unit& unit, UnitAction action) {
    if (unit.origin == Origin::Missing || unit.loadState == "not-found") {
        return unit.name + " does not exist";
    }
    if (!isShellSafeUnitName(unit.name)) {
        return unit.name + " has unexpected characters in its name";
    }
    if (!unit.isLoaded && !worksOnUnitFiles(action)) {
        return unit.name + " is not loaded; only enable/disable work on it";
    }
    return "";
}

int runActionInTerminal(const Unit& unit, UnitAction action) {
    std::string reason = whyActionUnavailable(unit, action);
    if (!reason.empty()) {
        std::cout << "\nnyst: " << reason << "\n";
        waitForEnter();
        return -1;
    }

    std::string command = systemctlCommand(unit, action);
    std::cout << "\n$ " << command << "\n" << std::flush;
    int exitCode = runCommand(command);
    if (exitCode == 0) {
        std::cout << "\nDone.\n";
    } else {
        std::cout << "\nFailed with exit status " << exitCode << ".\n";
    }
    waitForEnter();
    return exitCode;
}

} // namespace nyst
