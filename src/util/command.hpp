// Runs shell commands and turns their wait status into a plain exit code.
#pragma once

#include <string>
#include <vector>

namespace nyst {

/// Turns a wait status from pclose/system into an exit code (-1 if killed by a signal).
int exitCodeOf(int waitStatus);

/// Runs the command through the shell, attached to the current terminal, and logs it.
/// Returns its exit code.
int runCommand(const std::string& command);

/// Runs the command through the shell and returns its standard output split into lines
/// (without the newlines). exitCode gets its exit code, or -1 if it could not start.
std::vector<std::string> readCommandLines(const std::string& command, int& exitCode);

} // namespace nyst
