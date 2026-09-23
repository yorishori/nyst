// Reads a unit's log lines by running journalctl; the only module that knows how.
#include "source/journal.hpp"

#include "util/command.hpp"

namespace nyst {

namespace {

/// "journalctl [--user] -u '<name>'". Single quotes are safe because the name was checked.
std::string baseCommand(const Unit& unit) {
    std::string command = "journalctl";
    if (unit.manager == Manager::User) {
        command += " --user";
    }
    return command + " -u '" + unit.name + "'";
}

// Log lines may carry tabs or terminal escape codes; keep them from reaching the UI.
std::string sanitizeLine(const std::string& line) {
    std::string clean;
    for (char character : line) {
        if (character == '\t') {
            clean += "    ";
        } else if (character == '\n' || character == '\r') {
            continue;
        } else if (static_cast<unsigned char>(character) < 0x20 || character == 0x7f) {
            clean += '?';
        } else {
            clean += character;
        }
    }
    return clean;
}

} // namespace

std::vector<std::string> recentLogLines(const Unit& unit, int count) {
    if (!isShellSafeUnitName(unit.name)) {
        return {"refusing to query the journal: unexpected characters in unit name"};
    }
    // stderr is merged so permission problems show up in the pane instead of vanishing.
    std::string command =
        baseCommand(unit) + " -n " + std::to_string(count) + " --no-pager -o short-iso 2>&1";
    int exitCode = 0;
    std::vector<std::string> lines;
    for (const std::string& line : readCommandLines(command, exitCode)) {
        lines.push_back(sanitizeLine(line));
    }
    if (exitCode != 0) {
        lines.push_back("(journalctl exited with status " + std::to_string(exitCode) + ")");
    }
    return lines;
}

std::string showFullJournal(const Unit& unit) {
    if (!isShellSafeUnitName(unit.name)) {
        return "refusing to open the journal: unexpected characters in unit name";
    }
    int status = runCommand(baseCommand(unit) + " -e");
    if (status != 0) {
        return "journalctl exited with status " + std::to_string(status);
    }
    return "";
}

} // namespace nyst
