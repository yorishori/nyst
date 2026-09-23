// Runs systemctl verbs (start, stop, ...) on a unit in the user's terminal.
#pragma once

#include "model/unit.hpp"

#include <string>

namespace nyst {

enum class UnitAction { Start, Stop, Restart, Reload, Enable, Disable };

/// The systemctl verb, e.g. "restart".
std::string toString(UnitAction action);

/// Empty if the action can run on this unit, otherwise the reason it cannot.
/// Only loaded units can be started or stopped; enable/disable also work on unit files.
std::string whyActionUnavailable(const Unit& unit, UnitAction action);

/// Runs `systemctl [--user] <verb> <name>` attached to the terminal, so polkit can ask
/// for a password there, prints the outcome, and waits for Enter. The caller must have
/// released the terminal first. Returns the exit code (-1 if nothing could run).
int runActionInTerminal(const Unit& unit, UnitAction action);

/// Shows `systemctl [--user] cat <name>` (the unit file plus all drop-ins) in systemctl's
/// pager and blocks until it exits. The caller must have released the terminal first.
/// Returns an error message, or an empty string on success.
std::string showUnitFile(const Unit& unit);

} // namespace nyst
