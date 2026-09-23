// Filter state, unit matching, and the filter panel component.
#pragma once

#include "model/unit_graph.hpp"

#include <ftxui/component/component_base.hpp>

#include <functional>
#include <map>
#include <string>

namespace nyst {

/// Everything that decides which units are shown. Each map entry is one checkbox;
/// a key that is missing from a map counts as enabled.
struct FilterState {
    std::map<std::string, bool> unitTypes;    // "service", "timer", ...
    std::map<std::string, bool> activeStates; // active, inactive, failed, transitioning
    std::map<std::string, bool> managers;     // system, user
    std::map<std::string, bool> origins;      // keyed by toString(Origin)
    bool onlyLocallyModified = false;
    bool onlyMasked = false;
    /// Shows only units that look broken; replaces the checkbox groups while on.
    bool problemsOnly = false;
    std::string search; // case-insensitive substring of name or description

    bool operator==(const FilterState& other) const = default;
};

/// Everything on except the noisy device, scope, and slice types.
FilterState defaultFilters();

bool unitPassesFilters(const Unit& unit, const FilterState& filters, const UnitGraph& graph);

/// Failed, missing, unloadable, unowned, never started although an active unit wants it,
/// or masked while another unit requires it.
bool isProblem(const Unit& unit, const UnitGraph& graph);

/// Number of unchecked boxes plus active "only" flags, for the header.
int countDisabledFilters(const FilterState& filters);

/// Turns every value in the group on, or all off if they are all on already.
void toggleAll(std::map<std::string, bool>& values);

/// toggleAll across every checkbox group at once. The "only" flags are left alone.
void toggleAllGroups(FilterState& filters);

/// Checkbox panel that edits filters in place. filters must outlive the component.
/// onClose runs when the panel's close button is clicked.
ftxui::Component makeFilterPanel(FilterState& filters, std::function<void()> onClose);

} // namespace nyst
