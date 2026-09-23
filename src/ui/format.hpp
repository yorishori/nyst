// Human-readable sizes, timestamps, and durations for display.
#pragma once

#include <cstdint>
#include <string>

namespace nyst {

/// "512 B", "3.4 MiB", "1.2 GiB".
std::string formatBytes(std::uint64_t bytes);

/// "1.234s", "2min 5s", "1h 3min". Input in microseconds.
std::string formatDuration(std::uint64_t usec);

/// A rough span like "45s", "3min", "2h", "4d": the single largest unit. Input in microseconds.
std::string formatRoughSpan(std::uint64_t usec);

/// A monotonic timestamp (microseconds since kernel start) as "+2min 3s (19:47:24)":
/// the offset from boot plus the local wall-clock time, with the date if it is not recent.
std::string formatSinceBoot(std::uint64_t monotonicUsec);

/// Microseconds between a monotonic timestamp and now; 0 if it lies in the future.
std::uint64_t monotonicAgeUsec(std::uint64_t monotonicUsec);

/// Local wall-clock time plus distance from now: "2026-09-23 19:20 (3h ago)" or "(in 2d)".
/// Input in microseconds since the epoch.
std::string formatWallClockWithDistance(std::uint64_t usecSinceEpoch);

} // namespace nyst
