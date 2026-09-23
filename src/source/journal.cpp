// Reads a unit's log lines by running journalctl; the only module that knows how.
#include "source/journal.hpp"

#include "util/command.hpp"
#include "util/debug_log.hpp"

#include <cstdio>

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

std::vector<std::string> readCommandOutput(const std::string& command) {
    std::vector<std::string> lines;
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
        return {"could not run journalctl"};
    }

    std::string current;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        current += buffer;
        if (!current.empty() && current.back() == '\n') {
            lines.push_back(sanitizeLine(current));
            current.clear();
        }
    }
    if (!current.empty()) {
        lines.push_back(sanitizeLine(current));
    }

    int status = exitCodeOf(pclose(pipe));
    if (status != 0) {
        lines.push_back("(journalctl exited with status " + std::to_string(status) + ")");
    }
    return lines;
}

} // namespace

std::vector<std::string> recentLogLines(const Unit& unit, int count) {
    if (!isShellSafeUnitName(unit.name)) {
        return {"refusing to query the journal: unexpected characters in unit name"};
    }
    // stderr is merged so permission problems show up in the pane instead of vanishing.
    std::string command =
        baseCommand(unit) + " -n " + std::to_string(count) + " --no-pager -o short-iso 2>&1";
    debugLog("run: " + command);
    return readCommandOutput(command);
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
