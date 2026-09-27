// Polls memory, CPU, and task counts of running units, which systemd never signals.
#pragma once

#include "model/unit.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace nyst {

/// How often the poller samples.
const auto kUsagePollInterval = std::chrono::seconds(2);

/// One round of samples.
struct UsageUpdate {
    std::vector<UsageSample> samples;
    // Processes of the watched unit; processesUnitKey is empty if no unit is watched.
    std::string processesUnitKey;
    std::vector<UnitProcess> processes;
};

/// Samples every kUsagePollInterval on a background thread while there is something to
/// sample: every running unit (setPollAll) and/or one watched unit, whose processes are
/// listed too. Changing either samples right away. onUpdate runs on the background thread.
class UsagePoller {
public:
    explicit UsagePoller(std::function<void(UsageUpdate)> onUpdate);
    ~UsagePoller();

    UsagePoller(const UsagePoller&) = delete;
    UsagePoller& operator=(const UsagePoller&) = delete;

    void setPollAll(bool pollAll);
    /// Watches the unit if it is running; nullptr or a unit that is not running stops watching.
    void watchUnit(const Unit* unit);

private:
    // Keeps sdbus types out of this header, so the UI layer never sees them.
    struct Internals;
    std::unique_ptr<Internals> internals_;
};

} // namespace nyst
