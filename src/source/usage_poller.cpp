// Polls memory, CPU, and task counts of running units, which systemd never signals.
#include "source/usage_poller.hpp"

#include "source/systemd_bus.hpp"
#include "util/debug_log.hpp"

#include <sdbus-c++/sdbus-c++.h>

#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>
#include <utility>

namespace nyst {

namespace {

/// The unit whose processes are listed, usually the selected one.
struct WatchedUnit {
    std::string key; // empty = none
    std::string name;
    Manager manager = Manager::System;

    bool operator==(const WatchedUnit& other) const = default;
};

} // namespace

struct UsagePoller::Internals {
    std::function<void(UsageUpdate)> onUpdate;
    std::thread thread;
    // Opened on first use and dropped after an error, so a bus that comes back is picked up.
    // Only the polling thread touches them.
    std::map<Manager, std::unique_ptr<sdbus::IConnection>> connections;

    // Shared with the UI thread.
    std::mutex mutex;
    std::condition_variable wake;
    bool stopping = false;
    bool pollAllNow = false;
    bool pollWatchedNow = false;
    bool pollAll = false;
    WatchedUnit watched;

    void run();
    void pollOnce(bool all, const WatchedUnit& unit);
    sdbus::IConnection& connection(Manager manager);
};

// Full rounds keep a fixed schedule; a newly watched unit is sampled on its own right away,
// so moving the cursor quickly never triggers a burst of full rounds.
void UsagePoller::Internals::run() {
    auto nextRound = std::chrono::steady_clock::now();
    while (true) {
        bool all = false;
        WatchedUnit unit;
        {
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait_until(lock, nextRound,
                            [this] { return stopping || pollAllNow || pollWatchedNow; });
            if (stopping) {
                return;
            }
            auto now = std::chrono::steady_clock::now();
            bool roundDue = pollAllNow || now >= nextRound;
            if (roundDue) {
                nextRound = now + kUsagePollInterval;
            }
            all = pollAll && roundDue;
            pollAllNow = false;
            pollWatchedNow = false;
            unit = watched;
        }
        if (all || !unit.key.empty()) {
            pollOnce(all, unit);
        }
    }
}

sdbus::IConnection& UsagePoller::Internals::connection(Manager manager) {
    std::unique_ptr<sdbus::IConnection>& slot = connections[manager];
    if (!slot) {
        slot = connectToManager(manager);
    }
    return *slot;
}

// A manager that fails is skipped this round; the other one still gets sampled.
void UsagePoller::Internals::pollOnce(bool all, const WatchedUnit& unit) {
    UsageUpdate update;
    for (Manager manager : {Manager::System, Manager::User}) {
        bool watchedHere = !unit.key.empty() && unit.manager == manager;
        if (!all && !watchedHere) {
            continue;
        }
        try {
            std::vector<UsageSample> samples =
                readRunningUsage(connection(manager), manager, all ? "" : unit.name);
            update.samples.insert(update.samples.end(), samples.begin(), samples.end());
            if (watchedHere) {
                update.processes = readUnitProcesses(connection(manager), unit.name);
                update.processesUnitKey = unit.key;
            }
        } catch (const sdbus::Error& error) {
            debugLog("usage poll of " + toString(manager) + " failed: " + error.getMessage());
            connections.erase(manager);
        }
    }
    onUpdate(std::move(update));
}

UsagePoller::UsagePoller(std::function<void(UsageUpdate)> onUpdate)
    : internals_(std::make_unique<Internals>()) {
    internals_->onUpdate = std::move(onUpdate);
    Internals* internals = internals_.get();
    internals_->thread = std::thread([internals] { internals->run(); });
}

UsagePoller::~UsagePoller() {
    {
        std::lock_guard<std::mutex> lock(internals_->mutex);
        internals_->stopping = true;
    }
    internals_->wake.notify_all();
    internals_->thread.join();
}

void UsagePoller::setPollAll(bool pollAll) {
    std::lock_guard<std::mutex> lock(internals_->mutex);
    if (internals_->pollAll == pollAll) {
        return;
    }
    internals_->pollAll = pollAll;
    if (pollAll) {
        internals_->pollAllNow = true;
        internals_->wake.notify_all();
    }
}

void UsagePoller::watchUnit(const Unit* unit) {
    WatchedUnit watched;
    if (unit != nullptr && isRunning(*unit)) {
        watched = WatchedUnit{unit->key, unit->name, unit->manager};
    }
    std::lock_guard<std::mutex> lock(internals_->mutex);
    if (internals_->watched == watched) {
        return;
    }
    internals_->watched = watched;
    if (!watched.key.empty()) {
        internals_->pollWatchedNow = true;
        internals_->wake.notify_all();
    }
}

} // namespace nyst
