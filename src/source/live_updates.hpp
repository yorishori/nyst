// Watches both systemd managers over D-Bus and reports units whose state changed.
#pragma once

#include "model/unit.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace nyst {

/// One batch of changes, gathered over a short interval.
struct UnitChanges {
    std::vector<Unit> changedUnits; // fresh, unclassified copies (see readUnitAt)
    bool managerReloaded = false;   // a daemon-reload finished: everything may have changed
};

/// Subscribes to systemd's change signals on construction and stops on destruction.
/// onChanges runs on a background thread, at most every few hundred milliseconds.
/// A manager whose bus cannot be reached is skipped (and logged).
class UnitWatcher {
public:
    explicit UnitWatcher(std::function<void(UnitChanges)> onChanges);
    ~UnitWatcher();

    UnitWatcher(const UnitWatcher&) = delete;
    UnitWatcher& operator=(const UnitWatcher&) = delete;

private:
    // Keeps sdbus types out of this header, so the UI layer never sees them.
    struct Internals;
    std::unique_ptr<Internals> internals_;
};

} // namespace nyst
