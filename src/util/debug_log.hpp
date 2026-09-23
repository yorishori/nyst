// Optional file logger: appends to ~/.cache/nyst/debug.log when NYST_DEBUG=1.
#pragma once

#include <string>

namespace nyst {

/// Appends a timestamped line to the debug log. Does nothing unless NYST_DEBUG=1.
void debugLog(const std::string& message);

} // namespace nyst
