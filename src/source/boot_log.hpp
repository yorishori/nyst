// Finds start jobs that systemd dropped at boot to break ordering cycles (from the journal).
#pragma once

#include "model/unit.hpp"

#include <map>
#include <string>

namespace nyst {

/// Unit name -> the cycle its start job was dropped from, written as
/// "a → b → c → a" where each unit is ordered after the next one.
/// Empty if nothing was dropped or the journal cannot be read.
std::map<std::string, std::string> readJobsDroppedByOrderingCycles(Manager manager);

} // namespace nyst
