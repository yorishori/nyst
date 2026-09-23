// All units plus forward and reverse dependency edges, with simple lookups.
#include "model/unit_graph.hpp"

namespace nyst {

namespace {

std::string nameFromKey(const std::string& key) {
    std::size_t colon = key.find(':');
    return colon == std::string::npos ? key : key.substr(colon + 1);
}

} // namespace

void UnitGraph::addUnit(const Unit& unit) {
    units_[unit.key] = unit;
    for (const std::string& alias : unit.aliases) {
        keyByAliasKey_[makeUnitKey(unit.manager, alias)] = unit.key;
    }
}

const Unit* UnitGraph::find(const std::string& key) const {
    auto it = units_.find(key);
    if (it != units_.end()) {
        return &it->second;
    }
    auto alias = keyByAliasKey_.find(key);
    if (alias == keyByAliasKey_.end()) {
        return nullptr;
    }
    auto aliasTarget = units_.find(alias->second);
    return aliasTarget == units_.end() ? nullptr : &aliasTarget->second;
}

std::vector<Edge> UnitGraph::dependenciesOf(const std::string& key) const {
    const Unit* unit = find(key);
    return unit == nullptr ? std::vector<Edge>{} : unit->dependencies;
}

std::vector<Edge> UnitGraph::dependentsOf(const std::string& key) const {
    auto it = reverseEdges_.find(key);
    return it == reverseEdges_.end() ? std::vector<Edge>{} : it->second;
}

const std::map<std::string, Unit>& UnitGraph::allUnits() const {
    return units_;
}

bool UnitGraph::updateRuntimeState(const Unit& fresh) {
    auto it = units_.find(fresh.key);
    if (it == units_.end()) {
        return false;
    }
    copyRuntimeState(fresh, it->second);
    return true;
}

void UnitGraph::setBootFinishedUsec(Manager manager, std::uint64_t usec) {
    bootFinishedUsec_[manager] = usec;
}

bool UnitGraph::startedAfterBoot(const Unit& unit) const {
    auto it = bootFinishedUsec_.find(unit.manager);
    bool bootFinishKnown = it != bootFinishedUsec_.end() && it->second != 0;
    return bootFinishKnown && unit.activatingUsec > it->second;
}

void UnitGraph::addPlaceholdersForMissingTargets() {
    // Collect first: inserting while iterating units_ would be confusing to reason about.
    std::map<std::string, Manager> missingTargets;
    for (const auto& [key, unit] : units_) {
        for (const Edge& edge : unit.dependencies) {
            if (find(edge.target) == nullptr) {
                missingTargets[edge.target] = unit.manager;
            }
        }
    }

    for (const auto& [key, manager] : missingTargets) {
        addUnit(makeMissingPlaceholder(manager, nameFromKey(key)));
    }
}

void UnitGraph::rebuildDiagnostics() {
    for (auto& [key, unit] : units_) {
        unit.wantedBy.clear();
        if (!neverStartedThisBoot(unit)) {
            continue;
        }
        for (const Edge& dependent : dependentsOf(key)) {
            const Unit* puller = find(dependent.target);
            // Devices only pull units in when they get plugged; one that was already
            // present when its manager started (common for user managers) never does.
            if (pullsIn(dependent.kind) && puller != nullptr &&
                puller->activeState == ActiveState::Active && puller->type != "device") {
                unit.wantedBy.push_back(puller->name);
            }
        }
    }
}

void UnitGraph::rebuildReverseEdges() {
    reverseEdges_.clear();
    for (const auto& [key, unit] : units_) {
        for (const Edge& edge : unit.dependencies) {
            // Index by the canonical key so lookups via an alias and via the real name agree.
            const Unit* target = find(edge.target);
            std::string targetKey = target != nullptr ? target->key : edge.target;
            reverseEdges_[targetKey].push_back(Edge{key, edge.kind});
        }
    }
}

} // namespace nyst
