// Human-readable sizes, timestamps, and durations for display.
#include "ui/format.hpp"

#include <chrono>
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

std::uint64_t nowUsecSinceEpoch() {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(now).count());
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

std::string formatWallClockWithDistance(std::uint64_t usecSinceEpoch) {
    std::time_t seconds = static_cast<std::time_t>(usecSinceEpoch / kUsecPerSecond);
    std::tm localTime{};
    localtime_r(&seconds, &localTime);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &localTime);

    std::uint64_t now = nowUsecSinceEpoch();
    std::string distance = usecSinceEpoch <= now
                               ? roughSpan((now - usecSinceEpoch) / kUsecPerSecond) + " ago"
                               : "in " + roughSpan((usecSinceEpoch - now) / kUsecPerSecond);
    return std::string(buffer) + " (" + distance + ")";
}

} // namespace nyst
