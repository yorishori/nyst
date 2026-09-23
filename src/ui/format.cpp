// Human-readable sizes, timestamps, and durations for display.
#include "ui/format.hpp"

#include <cstdio>
#include <ctime>

namespace nyst {

namespace {

const std::uint64_t kUsecPerSecond = 1000000;

std::string printfToString(const char* format, double value, const char* unit) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), format, value, unit);
    return buffer;
}

std::uint64_t clockUsec(clockid_t clock) {
    timespec now{};
    clock_gettime(clock, &now);
    return static_cast<std::uint64_t>(now.tv_sec) * kUsecPerSecond +
           static_cast<std::uint64_t>(now.tv_nsec) / 1000;
}

std::string localTime(std::uint64_t usecSinceEpoch, const char* format) {
    std::time_t seconds = static_cast<std::time_t>(usecSinceEpoch / kUsecPerSecond);
    std::tm local{};
    localtime_r(&seconds, &local);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), format, &local);
    return buffer;
}

/// "3h", "2d", "45s": the single largest unit, which is all a glance needs.
std::string roughSpan(std::uint64_t seconds) {
    if (seconds < 60) {
        return std::to_string(seconds) + "s";
    }
    if (seconds < 3600) {
        return std::to_string(seconds / 60) + "min";
    }
    if (seconds < 86400) {
        return std::to_string(seconds / 3600) + "h";
    }
    return std::to_string(seconds / 86400) + "d";
}

} // namespace

std::string formatBytes(std::uint64_t bytes) {
    const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    if (unit == 0) {
        return std::to_string(bytes) + " B";
    }
    return printfToString("%.1f %s", value, units[unit]);
}

std::string formatDuration(std::uint64_t usec) {
    std::uint64_t seconds = usec / kUsecPerSecond;
    if (seconds < 60) {
        return printfToString("%.3f%s", static_cast<double>(usec) / kUsecPerSecond, "s");
    }
    if (seconds < 3600) {
        return std::to_string(seconds / 60) + "min " + std::to_string(seconds % 60) + "s";
    }
    return std::to_string(seconds / 3600) + "h " + std::to_string(seconds % 3600 / 60) + "min";
}

std::string formatRoughSpan(std::uint64_t usec) {
    return roughSpan(usec / kUsecPerSecond);
}

std::uint64_t monotonicAgeUsec(std::uint64_t monotonicUsec) {
    std::uint64_t now = clockUsec(CLOCK_MONOTONIC);
    return now > monotonicUsec ? now - monotonicUsec : 0;
}

// Wall-clock time = now - (monotonic now - monotonic then). Good enough for display;
// it drifts only if the clock was changed since.
std::string formatSinceBoot(std::uint64_t monotonicUsec) {
    std::uint64_t age = monotonicAgeUsec(monotonicUsec);
    std::uint64_t wallClock = clockUsec(CLOCK_REALTIME) - age;
    const std::uint64_t day = 24ULL * 3600 * kUsecPerSecond;
    const char* format = age < day ? "%H:%M:%S" : "%Y-%m-%d %H:%M";
    return "+" + formatDuration(monotonicUsec) + "  (" + localTime(wallClock, format) + ")";
}

std::string formatWallClockWithDistance(std::uint64_t usecSinceEpoch) {
    std::string buffer = localTime(usecSinceEpoch, "%Y-%m-%d %H:%M");

    std::uint64_t now = clockUsec(CLOCK_REALTIME);
    std::string distance = usecSinceEpoch <= now
                               ? roughSpan((now - usecSinceEpoch) / kUsecPerSecond) + " ago"
                               : "in " + roughSpan((usecSinceEpoch - now) / kUsecPerSecond);
    return buffer + " (" + distance + ")";
}

} // namespace nyst
