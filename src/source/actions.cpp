// Runs systemctl verbs (start, stop, ...) on a unit in the user's terminal.
#include "source/actions.hpp"

#include "util/command.hpp"

#include <iostream>

namespace nyst {

namespace {

bool worksOnUnitFiles(UnitAction action) {
    return action == UnitAction::Enable || action == UnitAction::Disable ||
           action == UnitAction::Mask || action == UnitAction::Unmask;
}

bool isMaskedOnDisk(const Unit& unit) {
    return isMasked(unit) || unit.unitFileState.rfind("masked", 0) == 0;
}

void waitForEnter() {
    std::cout << "Press Enter to return to nyst..." << std::flush;
    std::string ignored;
    std::getline(std::cin, ignored);
}

/// Prints the command, runs it on the terminal, prints the outcome, and waits for Enter.
int runVisibly(const std::string& command) {
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

/// "systemctl [--user] <verb> '<name>'". Single quotes are safe: callers check the name
/// with isShellSafeUnitName first.
std::string systemctlCommand(const Unit& unit, const std::string& verb) {
    std::string command = "systemctl";
    if (unit.manager == Manager::User) {
        command += " --user";
    }
    return command + " " + verb + " '" + unit.name + "'";
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
    case UnitAction::Mask:
        return "mask";
    case UnitAction::Unmask:
        return "unmask";
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
        return unit.name + " is not loaded; only enable/disable/mask/unmask work on it";
    }
    if (action == UnitAction::Mask && isMaskedOnDisk(unit)) {
        return unit.name + " is already masked";
    }
    if (action == UnitAction::Unmask && !isMaskedOnDisk(unit)) {
        return unit.name + " is not masked";
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

    return runVisibly(systemctlCommand(unit, toString(action)));
}

int runDaemonReloadInTerminal(Manager manager) {
    std::string command =
        manager == Manager::User ? "systemctl --user daemon-reload" : "systemctl daemon-reload";
    return runVisibly(command);
}

std::string showUnitFile(const Unit& unit) {
    if (unit.origin == Origin::Missing || unit.loadState == "not-found") {
        return unit.name + " has no unit file";
    }
    if (!isShellSafeUnitName(unit.name)) {
        return unit.name + " has unexpected characters in its name";
    }
    int exitCode = runCommand(systemctlCommand(unit, "cat"));
    if (exitCode != 0) {
        return "systemctl cat exited with status " + std::to_string(exitCode);
    }
    return "";
}

} // namespace nyst
