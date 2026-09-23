// Single entry point that reads, classifies, and links every unit into a UnitGraph.
#pragma once

#include "model/unit_graph.hpp"

#include <string>

namespace nyst {

/// Loads both managers into a finished graph. Never throws; problems end up in
/// sourceStatus (e.g. "system bus ok · user units unavailable (...)", plus a count of boot
/// jobs dropped by ordering cycles) or in the affected Unit::error.
UnitGraph loadEverything(std::string& sourceStatus);

/// "949 units · 1 failed", computed from the graph as it is now.
std::string summarizeUnits(const UnitGraph& graph);

} // namespace nyst
