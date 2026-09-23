// Finds start jobs that systemd dropped at boot to break ordering cycles (from the journal).
#include "source/boot_log.hpp"

#include "util/command.hpp"
#include "util/debug_log.hpp"

#include <vector>

namespace nyst {

namespace {

const std::string kFoundCycle = "Found ordering cycle: ";
const std::string kJobPrefix = "Job ";
const std::string kDeletedSuffix = " deleted to break ordering cycle";
const std::string kAfter = " after ";

/// "sshd.service/start" -> "sshd.service".
std::string withoutJobType(const std::string& job) {
    return job.substr(0, job.find('/'));
}

/// Turns "a/start after b/start after c/start - after a" into "a → b → c → a".
std::string describeCycle(const std::string& chain) {
    std::string description;
    std::string firstUnit;
    std::size_t start = 0;
    while (start < chain.size()) {
        std::size_t next = chain.find(kAfter, start);
        std::string job = chain.substr(start, next == std::string::npos ? next : next - start);
        std::string unit = withoutJobType(job.substr(0, job.find(" -")));
        if (firstUnit.empty()) {
            firstUnit = unit;
        }
        description += description.empty() ? unit : " → " + unit;
        // systemd ends the chain with " - after <first unit>" to show where it closes.
        if (job.find(" -") != std::string::npos || next == std::string::npos) {
            break;
        }
        start = next + kAfter.size();
    }
    return description + " → " + firstUnit;
}

/// "sockets.target: Job foo.socket/start deleted to break ..." -> "foo.socket".
std::string droppedUnitName(const std::string& line) {
    std::size_t deleted = line.find(kDeletedSuffix);
    std::size_t job = line.rfind(kJobPrefix, deleted);
    if (job == std::string::npos) {
        return "";
    }
    std::size_t nameStart = job + kJobPrefix.size();
    return withoutJobType(line.substr(nameStart, deleted - nameStart));
}

std::vector<std::string> readCycleMessages(Manager manager) {
    std::string command = "journalctl";
    if (manager == Manager::User) {
        command += " --user";
    }
    command += " -b -o cat --no-pager SYSLOG_IDENTIFIER=systemd --grep 'ordering cycle' "
               "2>/dev/null";
    int exitCode = 0;
    return readCommandLines(command, exitCode);
}

} // namespace

// systemd logs each "Found ordering cycle" line right before the matching
// "Job ... deleted" line, so each dropped job takes the most recent cycle.
std::map<std::string, std::string> readJobsDroppedByOrderingCycles(Manager manager) {
    std::map<std::string, std::string> cycleByUnit;
    std::string lastCycle;
    for (const std::string& line : readCycleMessages(manager)) {
        std::size_t found = line.find(kFoundCycle);
        if (found != std::string::npos) {
            lastCycle = describeCycle(line.substr(found + kFoundCycle.size()));
            continue;
        }
        if (line.find(kDeletedSuffix) == std::string::npos) {
            continue;
        }
        std::string unit = droppedUnitName(line);
        if (!unit.empty() && cycleByUnit.count(unit) == 0) {
            cycleByUnit[unit] = lastCycle;
        }
    }
    debugLog(toString(manager) + ": " + std::to_string(cycleByUnit.size()) +
             " start jobs dropped by ordering cycles this boot");
    return cycleByUnit;
}

} // namespace nyst
