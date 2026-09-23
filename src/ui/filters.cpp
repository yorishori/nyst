// Filter state, unit matching, and the filter panel component.
#include "ui/filters.hpp"

#include "ui/button_style.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

#include <cctype>
#include <vector>

namespace nyst {

namespace {

const std::vector<std::string> kShownUnitTypes = {"service", "timer",     "socket", "target",
                                                  "mount",   "automount", "path",   "swap"};
const std::vector<std::string> kHiddenUnitTypes = {"device", "scope", "slice"};
const std::vector<std::string> kActiveStateGroups = {"active", "inactive", "failed",
                                                     "transitioning"};
const std::vector<std::string> kManagers = {"system", "user"};

std::vector<std::string> allUnitTypes() {
    std::vector<std::string> types = kShownUnitTypes;
    types.insert(types.end(), kHiddenUnitTypes.begin(), kHiddenUnitTypes.end());
    return types;
}

std::vector<std::string> originNames() {
    std::vector<std::string> names;
    for (Origin origin : allOrigins()) {
        names.push_back(toString(origin));
    }
    return names;
}

std::map<std::string, bool> allEnabled(const std::vector<std::string>& keys) {
    std::map<std::string, bool> values;
    for (const std::string& key : keys) {
        values[key] = true;
    }
    return values;
}

bool isEnabled(const std::map<std::string, bool>& values, const std::string& key) {
    auto it = values.find(key);
    return it == values.end() || it->second;
}

std::string activeStateGroup(ActiveState state) {
    switch (state) {
    case ActiveState::Active:
        return "active";
    case ActiveState::Failed:
        return "failed";
    case ActiveState::Activating:
    case ActiveState::Deactivating:
    case ActiveState::Reloading:
        return "transitioning";
    case ActiveState::Inactive:
    case ActiveState::Unknown:
        break;
    }
    return "inactive";
}

std::string toLower(const std::string& text) {
    std::string lower = text;
    for (char& character : lower) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return lower;
}

bool matchesSearch(const Unit& unit, const std::string& search) {
    if (search.empty()) {
        return true;
    }
    std::string needle = toLower(search);
    return toLower(unit.name).find(needle) != std::string::npos ||
           toLower(unit.description).find(needle) != std::string::npos;
}

bool isRequiredByAnotherUnit(const Unit& unit, const UnitGraph& graph) {
    for (const Edge& edge : graph.dependentsOf(unit.key)) {
        if (edge.kind == EdgeKind::Requires || edge.kind == EdgeKind::Requisite ||
            edge.kind == EdgeKind::BindsTo) {
            return true;
        }
    }
    return false;
}

bool passesCheckboxes(const Unit& unit, const FilterState& filters) {
    return isEnabled(filters.unitTypes, unit.type) &&
           isEnabled(filters.activeStates, activeStateGroup(unit.activeState)) &&
           isEnabled(filters.managers, toString(unit.manager)) &&
           isEnabled(filters.origins, toString(unit.origin));
}

int countUnchecked(const std::map<std::string, bool>& values) {
    int unchecked = 0;
    for (const auto& [key, enabled] : values) {
        if (!enabled) {
            ++unchecked;
        }
    }
    return unchecked;
}

bool allChecked(const std::map<std::string, bool>& values) {
    return countUnchecked(values) == 0;
}

ftxui::Component makeCheckboxGroup(const std::string& title, const std::vector<std::string>& keys,
                                   std::map<std::string, bool>& values) {
    ftxui::Components boxes;
    boxes.push_back(
        ftxui::Button("toggle all", [&values] { toggleAll(values); }, bracketButtonStyle()));
    for (const std::string& key : keys) {
        boxes.push_back(ftxui::Checkbox(key, &values[key]));
    }
    ftxui::Component column = ftxui::Container::Vertical(boxes);
    return ftxui::Renderer(column, [title, column] {
        return ftxui::window(ftxui::text(" " + title + " "), column->Render());
    });
}

ftxui::Component makeFlagGroup(FilterState& filters) {
    ftxui::Component column = ftxui::Container::Vertical({
        ftxui::Checkbox("only locally modified", &filters.onlyLocallyModified),
        ftxui::Checkbox("only masked", &filters.onlyMasked),
        ftxui::Checkbox("problems only (p)", &filters.problemsOnly),
    });
    return ftxui::Renderer(
        column, [column] { return ftxui::window(ftxui::text(" flags "), column->Render()); });
}

} // namespace

FilterState defaultFilters() {
    FilterState filters;
    filters.unitTypes = allEnabled(allUnitTypes());
    for (const std::string& type : kHiddenUnitTypes) {
        filters.unitTypes[type] = false;
    }
    filters.activeStates = allEnabled(kActiveStateGroups);
    filters.managers = allEnabled(kManagers);
    filters.origins = allEnabled(originNames());
    return filters;
}

bool unitPassesFilters(const Unit& unit, const FilterState& filters, const UnitGraph& graph) {
    if (!matchesSearch(unit, filters.search)) {
        return false;
    }
    // The problems view should catch a failed device too, so it ignores the checkboxes.
    if (filters.problemsOnly) {
        return isProblem(unit, graph);
    }
    if (!passesCheckboxes(unit, filters)) {
        return false;
    }
    if (filters.onlyLocallyModified && !unit.locallyModified) {
        return false;
    }
    return !filters.onlyMasked || isMasked(unit);
}

bool isProblem(const Unit& unit, const UnitGraph& graph) {
    if (unit.activeState == ActiveState::Failed) {
        return true;
    }
    if (unit.origin == Origin::Missing || unit.origin == Origin::Unowned) {
        return true;
    }
    // "error" and "bad-setting" mean systemd could not parse the unit file.
    if (unit.loadState == "not-found" || unit.loadState == "error" ||
        unit.loadState == "bad-setting") {
        return true;
    }
    if (hasWarning(unit)) {
        return true;
    }
    return isMasked(unit) && isRequiredByAnotherUnit(unit, graph);
}

int countDisabledFilters(const FilterState& filters) {
    int disabled = countUnchecked(filters.unitTypes) + countUnchecked(filters.activeStates) +
                   countUnchecked(filters.managers) + countUnchecked(filters.origins);
    if (filters.onlyLocallyModified) {
        ++disabled;
    }
    if (filters.onlyMasked) {
        ++disabled;
    }
    return disabled;
}

void toggleAll(std::map<std::string, bool>& values) {
    bool turnOn = !allChecked(values);
    for (auto& [key, enabled] : values) {
        enabled = turnOn;
    }
}

void toggleAllGroups(FilterState& filters) {
    bool turnOn = !(allChecked(filters.unitTypes) && allChecked(filters.activeStates) &&
                    allChecked(filters.managers) && allChecked(filters.origins));
    for (std::map<std::string, bool>* group :
         {&filters.unitTypes, &filters.activeStates, &filters.managers, &filters.origins}) {
        for (auto& [key, enabled] : *group) {
            enabled = turnOn;
        }
    }
}

ftxui::Component makeFilterPanel(FilterState& filters, std::function<void()> onClose) {
    ftxui::Component groups = ftxui::Container::Horizontal({
        makeCheckboxGroup("unit type", allUnitTypes(), filters.unitTypes),
        makeCheckboxGroup("state", kActiveStateGroups, filters.activeStates),
        makeCheckboxGroup("manager", kManagers, filters.managers),
        makeCheckboxGroup("origin", originNames(), filters.origins),
        makeFlagGroup(filters),
    });
    ftxui::Component toggleEverything = ftxui::Button(
        "toggle all groups", [&filters] { toggleAllGroups(filters); }, bracketButtonStyle());
    ftxui::Component close = ftxui::Button("close", onClose, bracketButtonStyle());
    ftxui::Component layout = ftxui::Container::Vertical({
        groups,
        ftxui::Container::Horizontal({toggleEverything, close}),
    });

    return ftxui::Renderer(layout, [groups, toggleEverything, close] {
        using namespace ftxui;
        Element footer = hbox({
            toggleEverything->Render(),
            text("  arrows/mouse move · Space/Enter/click toggle · Esc or F closes  ") | dim,
            filler(),
            close->Render(),
        });
        return window(text(" filters ") | bold, vbox({groups->Render(), footer}));
    });
}

} // namespace nyst
