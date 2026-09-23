// Entry point: parses arguments and runs either the --dump report or the TUI.
#include "model/unit_graph.hpp"
#include "source/loader.hpp"
#include "ui/app.hpp"

#include <iostream>
#include <string>

namespace {

std::string orDash(const std::string& text) {
    return text.empty() ? "-" : text;
}

void appendFlag(std::string& flags, const std::string& flag) {
    flags += flags.empty() ? flag : "," + flag;
}

std::string flagsOf(const nyst::Unit& unit) {
    std::string flags;
    if (unit.locallyModified) {
        appendFlag(flags, "modified");
    }
    if (unit.shadowsPackagedUnit) {
        appendFlag(flags, "shadows");
    }
    if (nyst::isMasked(unit)) {
        appendFlag(flags, "masked");
    }
    if (!unit.isLoaded) {
        appendFlag(flags, "not-loaded");
    }
    if (!unit.wantedBy.empty()) {
        appendFlag(flags, "never-started");
    }
    if (!unit.droppedByCycle.empty()) {
        appendFlag(flags, "dropped-by-cycle");
    }
    return flags;
}

void printDumpLine(const nyst::UnitGraph& graph, const nyst::Unit& unit) {
    std::cout << unit.key << '\t' << toString(unit.activeState) << '\t' << orDash(unit.subState)
              << '\t' << orDash(unit.unitFileState) << '\t' << toString(unit.origin) << '\t'
              << orDash(unit.package) << '\t' << orDash(unit.runAsUser) << '\t'
              << graph.dependenciesOf(unit.key).size() << '\t'
              << graph.dependentsOf(unit.key).size() << '\t' << orDash(flagsOf(unit)) << '\t'
              << orDash(unit.error) << '\n';
}

int runDump() {
    std::string sourceStatus;
    nyst::UnitGraph graph = nyst::loadEverything(sourceStatus);

    std::cout << "# key\tactiveState\tsubState\tunitFileState\torigin\tpackage\trunAsUser"
                 "\tdeps\tdependents\tflags\terror\n";
    for (const auto& [key, unit] : graph.allUnits()) {
        printDumpLine(graph, unit);
    }
    std::cout << "# " << nyst::summarizeUnits(graph) << " · " << sourceStatus << '\n';
    return 0;
}

void printUsage() {
    std::cout << "usage: nyst [--dump]\n"
                 "  (no arguments)  start the interactive tree\n"
                 "  --dump          print every unit as a tab-separated line and exit\n";
}

} // namespace

int main(int argc, char** argv) {
    std::string argument = argc > 1 ? argv[1] : "";
    if (argument == "--dump") {
        return runDump();
    }
    if (argument == "--help" || argument == "-h") {
        printUsage();
        return 0;
    }
    if (!argument.empty()) {
        printUsage();
        return 1;
    }
    return nyst::runTui();
}
