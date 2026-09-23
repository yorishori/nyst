// Runs shell commands and turns their wait status into a plain exit code.
#include "util/command.hpp"

#include "util/debug_log.hpp"

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

} // namespace nyst
