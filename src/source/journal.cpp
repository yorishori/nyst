// Reads a unit's log lines by running journalctl; the only module that knows how.
#include "source/journal.hpp"

#include "util/debug_log.hpp"

#include <cstdio>
#include <cstdlib>
#include <sys/wait.h>

namespace nyst {

namespace {

/// Turns a wait status from pclose/system into an exit code (-1 if killed by a signal).
int exitCodeOf(int waitStatus) {
    return WIFEXITED(waitStatus) ? WEXITSTATUS(waitStatus) : -1;
}

// Unit names are already restricted by systemd, but they reach a shell here,
// so anything outside this set is refused rather than escaped.
bool isSafeUnitName(const std::string& name) {
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
    if (!isSafeUnitName(unit.name)) {
        return {"refusing to query the journal: unexpected characters in unit name"};
    }
    // stderr is merged so permission problems show up in the pane instead of vanishing.
    std::string command =
        baseCommand(unit) + " -n " + std::to_string(count) + " --no-pager -o short-iso 2>&1";
    debugLog("run: " + command);
    return readCommandOutput(command);
}

std::string showFullJournal(const Unit& unit) {
    if (!isSafeUnitName(unit.name)) {
        return "refusing to open the journal: unexpected characters in unit name";
    }
    std::string command = baseCommand(unit) + " -e";
    debugLog("run: " + command);
    int status = exitCodeOf(std::system(command.c_str()));
    if (status != 0) {
        return "journalctl exited with status " + std::to_string(status);
    }
    return "";
}

} // namespace nyst
