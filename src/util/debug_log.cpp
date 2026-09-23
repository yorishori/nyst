// Optional file logger: appends to ~/.cache/nyst/debug.log when NYST_DEBUG=1.
#include "util/debug_log.hpp"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace nyst {

namespace {

bool isLoggingEnabled() {
    const char* value = std::getenv("NYST_DEBUG");
    return value != nullptr && std::string(value) == "1";
}

std::filesystem::path logFilePath() {
    const char* home = std::getenv("HOME");
    std::filesystem::path base = home != nullptr ? home : "/tmp";
    return base / ".cache" / "nyst" / "debug.log";
}

std::string currentTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    auto millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() %
        1000;

    std::tm localTime{};
    localtime_r(&seconds, &localTime);

    std::ostringstream out;
    out << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S") << '.' << std::setw(3)
        << std::setfill('0') << millis;
    return out.str();
}

} // namespace

void debugLog(const std::string& message) {
    // Read once: the environment does not change while we run.
    static const bool enabled = isLoggingEnabled();
    if (!enabled) {
        return;
    }

    std::filesystem::path path = logFilePath();
    std::error_code ignored;
    std::filesystem::create_directories(path.parent_path(), ignored);

    std::ofstream file(path, std::ios::app);
    if (file) {
        file << currentTimestamp() << "  " << message << '\n';
    }
}

} // namespace nyst
