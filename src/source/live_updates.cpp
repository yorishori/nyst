// Watches both systemd managers over D-Bus and reports units whose state changed.
#include "source/live_updates.hpp"

#include "source/systemd_bus.hpp"
#include "util/debug_log.hpp"

#include <sdbus-c++/sdbus-c++.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <set>
#include <thread>
#include <utility>

namespace nyst {

namespace {

const auto kBatchInterval = std::chrono::milliseconds(300);

const char* const kUnitChangedMatch =
    "type='signal',sender='org.freedesktop.systemd1',"
    "interface='org.freedesktop.DBus.Properties',member='PropertiesChanged',"
    "path_namespace='/org/freedesktop/systemd1/unit'";
const char* const kReloadingMatch = "type='signal',sender='org.freedesktop.systemd1',"
                                    "interface='org.freedesktop.systemd1.Manager',"
                                    "member='Reloading'";

} // namespace

/// Per-manager bus state. Signals arrive on `signals` (with its own event loop thread);
/// changed units are re-read over `queries`, used only by the batching thread.
struct ManagerWatch {
    Manager manager = Manager::System;
    std::unique_ptr<sdbus::IConnection> signals;
    std::unique_ptr<sdbus::IConnection> queries;
    sdbus::Slot unitChangedSlot;
    sdbus::Slot reloadingSlot;
};

struct UnitWatcher::Internals {
    std::function<void(UnitChanges)> onChanges;
    std::vector<std::unique_ptr<ManagerWatch>> watches;
    std::thread batchThread;

    // Filled by the signal threads, drained by the batch thread.
    std::mutex mutex;
    std::condition_variable wake;
    bool stopping = false;
    std::set<std::pair<Manager, std::string>> changedPaths;
    bool managerReloaded = false;

    void watchManager(Manager manager);
    void markChanged(Manager manager, const std::string& objectPath);
    void markReloaded();
    void runBatches();
    void rereadUnits(UnitChanges& changes, const std::set<std::pair<Manager, std::string>>& paths);
};

void UnitWatcher::Internals::watchManager(Manager manager) {
    auto watch = std::make_unique<ManagerWatch>();
    watch->manager = manager;
    watch->signals = connectToManager(manager);
    watch->queries = connectToManager(manager);

    // systemd only emits unit signals while at least one client has subscribed.
    auto managerProxy =
        sdbus::createProxy(*watch->signals, sdbus::ServiceName{"org.freedesktop.systemd1"},
                           sdbus::ObjectPath{"/org/freedesktop/systemd1"});
    managerProxy->callMethod("Subscribe").onInterface("org.freedesktop.systemd1.Manager");

    watch->unitChangedSlot = watch->signals->addMatch(
        kUnitChangedMatch,
        [this, manager](sdbus::Message message) { markChanged(manager, message.getPath()); },
        sdbus::return_slot);
    watch->reloadingSlot = watch->signals->addMatch(
        kReloadingMatch,
        [this](sdbus::Message message) {
            bool starting = true;
            message >> starting;
            if (!starting) {
                markReloaded();
            }
        },
        sdbus::return_slot);
    watch->signals->enterEventLoopAsync();
    watches.push_back(std::move(watch));
}

void UnitWatcher::Internals::markChanged(Manager manager, const std::string& objectPath) {
    std::lock_guard<std::mutex> lock(mutex);
    changedPaths.insert({manager, objectPath});
}

void UnitWatcher::Internals::markReloaded() {
    std::lock_guard<std::mutex> lock(mutex);
    managerReloaded = true;
}

// Wakes every kBatchInterval, so a burst of signals (a unit going through several states)
// becomes one re-read and one UI update.
void UnitWatcher::Internals::runBatches() {
    while (true) {
        std::set<std::pair<Manager, std::string>> paths;
        UnitChanges changes;
        {
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait_for(lock, kBatchInterval, [this] { return stopping; });
            if (stopping) {
                return;
            }
            if (changedPaths.empty() && !managerReloaded) {
                continue;
            }
            paths.swap(changedPaths);
            changes.managerReloaded = managerReloaded;
            managerReloaded = false;
        }
        // After a daemon-reload everything gets reloaded anyway; skip the re-reads.
        if (!changes.managerReloaded) {
            rereadUnits(changes, paths);
        }
        onChanges(std::move(changes));
    }
}

void UnitWatcher::Internals::rereadUnits(UnitChanges& changes,
                                         const std::set<std::pair<Manager, std::string>>& paths) {
    for (const auto& [manager, path] : paths) {
        for (const auto& watch : watches) {
            if (watch->manager != manager) {
                continue;
            }
            try {
                changes.changedUnits.push_back(readUnitAt(*watch->queries, path, manager));
            } catch (const sdbus::Error& error) {
                // Usually the unit was garbage-collected between the signal and the read.
                debugLog("live update: reading " + path + " failed: " + error.getMessage());
            }
        }
    }
}

UnitWatcher::UnitWatcher(std::function<void(UnitChanges)> onChanges)
    : internals_(std::make_unique<Internals>()) {
    internals_->onChanges = std::move(onChanges);
    for (Manager manager : {Manager::System, Manager::User}) {
        try {
            internals_->watchManager(manager);
        } catch (const sdbus::Error& error) {
            debugLog("live updates unavailable for " + toString(manager) + ": " +
                     error.getMessage());
        }
    }
    Internals* internals = internals_.get();
    internals_->batchThread = std::thread([internals] { internals->runBatches(); });
}

// Order matters: stop the signal threads first so no callback can touch Internals, then
// the batch thread, and only then let the slots and connections go.
UnitWatcher::~UnitWatcher() {
    for (const auto& watch : internals_->watches) {
        watch->signals->leaveEventLoop();
    }
    {
        std::lock_guard<std::mutex> lock(internals_->mutex);
        internals_->stopping = true;
    }
    internals_->wake.notify_all();
    internals_->batchThread.join();
}

} // namespace nyst
