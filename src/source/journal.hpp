// Reads a unit's log lines by running journalctl; the only module that knows how.
#pragma once

#include "model/unit.hpp"

#include <string>
#include <vector>

namespace nyst {

/// Last `count` journal lines for the unit, oldest first. Errors (bad name, journalctl
/// failing, no permission) come back as readable lines instead of an exception.
std::vector<std::string> recentLogLines(const Unit& unit, int count);

/// Opens the unit's full journal in journalctl's pager, jumped to the end, and blocks
/// until the pager exits. The caller must have released the terminal first.
/// Returns an error message, or an empty string on success.
std::string showFullJournal(const Unit& unit);

} // namespace nyst
