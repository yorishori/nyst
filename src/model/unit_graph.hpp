// All units plus forward and reverse dependency edges, with simple lookups.
#pragma once

#include "model/unit.hpp"

#include <map>
#include <string>
#include <vector>

namespace nyst {

class UnitGraph {
public:
    /// Adds a unit, replacing any existing unit with the same key.
    void addUnit(const Unit& unit);

    /// Accepts a unit key or an alias key ("system:default.target"). Returns nullptr if unknown.
    const Unit* find(const std::string& key) const;

    /// Forward edges: what this unit pulls in.
    std::vector<Edge> dependenciesOf(const std::string& key) const;

    /// Reverse edges: who pulls this unit in. Edge::target is the dependent's key.
    std::vector<Edge> dependentsOf(const std::string& key) const;

    const std::map<std::string, Unit>& allUnits() const;

    /// Copies the runtime state of a freshly read unit into the existing unit with the same
    /// key. Returns false (and changes nothing) if there is no such unit.
    bool updateRuntimeState(const Unit& fresh);

    /// Records when a manager finished starting up (monotonic microseconds).
    void setBootFinishedUsec(Manager manager, std::uint64_t usec);

    /// True if the unit last started after its manager had finished booting, e.g. because
    /// it was restarted or started on demand later.
    bool startedAfterBoot(const Unit& unit) const;

    /// Creates a Missing placeholder for every edge target that has no unit.
    void addPlaceholdersForMissingTargets();

    /// Recomputes the reverse edge map from the forward edges. Call after any change.
    void rebuildReverseEdges();

    /// Recomputes Unit::wantedBy for every unit: filled when an active unit pulls the unit
    /// in but it never started. Needs the reverse edges; call after rebuildReverseEdges().
    void rebuildDiagnostics();

private:
    std::map<std::string, Unit> units_;
    std::map<std::string, std::vector<Edge>> reverseEdges_;
    std::map<std::string, std::string> keyByAliasKey_;
    std::map<Manager, std::uint64_t> bootFinishedUsec_;
};

} // namespace nyst
