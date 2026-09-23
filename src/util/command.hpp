// Runs shell commands and turns their wait status into a plain exit code.
#pragma once

#include <string>

namespace nyst {

/// Turns a wait status from pclose/system into an exit code (-1 if killed by a signal).
int exitCodeOf(int waitStatus);

/// Runs the command through the shell, attached to the current terminal, and logs it.
/// Returns its exit code.
int runCommand(const std::string& command);

} // namespace nyst
