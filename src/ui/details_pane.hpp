// Renders every known fact about the selected unit.
#pragma once

#include "model/unit_graph.hpp"

#include <ftxui/dom/elements.hpp>

#include <vector>

namespace nyst {

/// Column width of the details pane, including its border.
const int kDetailsPaneWidth = 52;

/// Details for the unit, or a hint when nothing (or a group row) is selected. processes
/// lists what runs in the unit's cgroup; nullptr if not known (e.g. not fetched yet).
ftxui::Element renderDetails(const Unit* unit, const UnitGraph& graph,
                             const std::vector<UnitProcess>* processes);

} // namespace nyst
