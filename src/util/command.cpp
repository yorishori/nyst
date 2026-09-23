// Runs shell commands and turns their wait status into a plain exit code.
#include "util/command.hpp"

#include "util/debug_log.hpp"

#include <cstdio>
#include <cstdlib>
#include <sys/wait.h>

namespace nyst {

int exitCodeOf(int waitStatus) {
    return WIFEXITED(waitStatus) ? WEXITSTATUS(waitStatus) : -1;
}

int runCommand(const std::string& command) {
    debugLog("run: " + command);
    int exitCode = exitCodeOf(std::system(command.c_str()));
    debugLog("exit code " + std::to_string(exitCode) + ": " + command);
    return exitCode;
}

std::vector<std::string> readCommandLines(const std::string& command, int& exitCode) {
    debugLog("run: " + command);
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
        exitCode = -1;
        return {};
    }

    std::vector<std::string> lines;
    std::string current;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        current += buffer;
        if (current.back() == '\n') {
            current.pop_back();
            lines.push_back(current);
            current.clear();
        }
    }
    if (!current.empty()) {
        lines.push_back(current);
    }
    exitCode = exitCodeOf(pclose(pipe));
    return lines;
}

} // namespace nyst
