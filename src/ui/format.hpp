// Human-readable sizes, timestamps, and durations for display.
#pragma once

#include <cstdint>
#include <string>

namespace nyst {

/// "512 B", "3.4 MiB", "1.2 GiB".
std::string formatBytes(std::uint64_t bytes);

/// "1.234s", "2min 5s", "1h 3min". Input in microseconds.
std::string formatDuration(std::uint64_t usec);

/// Local wall-clock time plus distance from now: "2026-09-23 19:20 (3h ago)" or "(in 2d)".
/// Input in microseconds since the epoch.
std::string formatWallClockWithDistance(std::uint64_t usecSinceEpoch);

} // namespace nyst
