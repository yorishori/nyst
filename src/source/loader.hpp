// Single entry point that reads, classifies, and links every unit into a UnitGraph.
#pragma once

#include "model/unit_graph.hpp"

#include <string>

namespace nyst {

/// Loads both managers into a finished graph. Never throws; problems end up in
/// statusMessage (e.g. "user units unavailable") or in the affected Unit::error.
UnitGraph loadEverything(std::string& statusMessage);

} // namespace nyst
