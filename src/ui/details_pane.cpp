// Renders every known fact about the selected unit.
#include "ui/details_pane.hpp"

#include "ui/format.hpp"

#include <string>
#include <vector>

namespace nyst {

namespace {

const int kLabelWidth = 10;
// Border (2) + label column; values wrap in what is left.
const int kValueWidth = kDetailsPaneWidth - 2 - kLabelWidth;

bool isUtf8Continuation(char character) {
    return (static_cast<unsigned char>(character) & 0xC0) == 0x80;
}

/// Wraps text at `width` bytes, preferring the last space in a line; text without spaces
/// (paths) is cut hard, but never inside a UTF-8 character.
std::vector<std::string> wrapText(const std::string& text, int width) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = std::min(text.size(), start + static_cast<std::size_t>(width));
        while (end < text.size() && end > start && isUtf8Continuation(text[end])) {
            --end;
        }
        std::size_t lastSpace = text.rfind(' ', end);
        bool breakAtSpace =
            end < text.size() && lastSpace != std::string::npos && lastSpace > start;
        if (breakAtSpace) {
            end = lastSpace + 1;
        }
        lines.push_back(text.substr(start, end - start));
        start = end;
    }
    if (lines.empty()) {
        lines.push_back("");
    }
    return lines;
}

/// A "label  value" block; the value wraps and continuation lines are indented.
ftxui::Element field(const std::string& label, const std::string& value,
                     ftxui::Decorator style = ftxui::nothing) {
    using namespace ftxui;
    Elements lines;
    std::vector<std::string> wrapped = wrapText(value.empty() ? "-" : value, kValueWidth);
    for (std::size_t index = 0; index < wrapped.size(); ++index) {
        std::string shownLabel = index == 0 ? label : "";
        lines.push_back(hbox({
            text(shownLabel) | dim | size(WIDTH, EQUAL, kLabelWidth),
            text(wrapped[index]) | style,
        }));
    }
    return vbox(lines);
}

void appendAll(ftxui::Elements& target, const ftxui::Elements& source) {
    target.insert(target.end(), source.begin(), source.end());
}

ftxui::Decorator activeStateStyle(ActiveState state) {
    switch (state) {
    case ActiveState::Active:
        return ftxui::color(ftxui::Color::Green);
    case ActiveState::Failed:
        return ftxui::color(ftxui::Color::Red) | ftxui::bold;
    case ActiveState::Activating:
    case ActiveState::Deactivating:
    case ActiveState::Reloading:
        return ftxui::color(ftxui::Color::Yellow);
    case ActiveState::Inactive:
    case ActiveState::Unknown:
        break;
    }
    return ftxui::nothing;
}

std::string originDescription(const Unit& unit) {
    switch (unit.origin) {
    case Origin::SystemdDefault:
        return "systemd default";
    case Origin::Package:
        return "package " + unit.package;
    case Origin::AdminCreated:
        return "created by the admin (/etc)";
    case Origin::UserCreated:
        return "created by the user";
    case Origin::Generated:
        return "generated at boot";
    case Origin::Transient:
        return "transient (created at runtime)";
    case Origin::Unowned:
        return "in /usr but owned by no package";
    case Origin::Missing:
        return "missing: referenced but not found";
    case Origin::Unknown:
        break;
    }
    return "unknown (no unit file)";
}

std::string joinLines(const std::vector<std::string>& values) {
    std::string joined;
    for (const std::string& value : values) {
        joined += joined.empty() ? value : ", " + value;
    }
    return joined;
}

ftxui::Elements identityFields(const Unit& unit) {
    std::string managedBy = toString(unit.manager) + " manager";
    if (!unit.runAsUser.empty()) {
        managedBy += ", runs as " + unit.runAsUser;
    }
    ftxui::Elements fields = {
        field("desc", unit.description),
        field("manager", managedBy),
    };
    if (!unit.aliases.empty()) {
        fields.push_back(field("aliases", joinLines(unit.aliases)));
    }
    return fields;
}

ftxui::Elements stateFields(const Unit& unit) {
    ftxui::Decorator loadStyle =
        unit.loadState == "loaded" ? ftxui::nothing : ftxui::color(ftxui::Color::Yellow);
    return {
        field("load", unit.loadState, loadStyle),
        field("active", toString(unit.activeState) + " (" + unit.subState + ")",
              activeStateStyle(unit.activeState)),
        field("file", unit.unitFileState),
    };
}

ftxui::Elements originFields(const Unit& unit, const UnitGraph& graph) {
    ftxui::Elements fields = {
        field("origin", originDescription(unit)),
        field("fragment", unit.fragmentPath),
    };
    if (!unit.sourcePath.empty()) {
        fields.push_back(field("source", unit.sourcePath));
    }
    for (const std::string& dropIn : unit.dropInPaths) {
        fields.push_back(field("drop-in", dropIn));
    }
    ftxui::Decorator warning = ftxui::color(ftxui::Color::Yellow);
    if (unit.locallyModified) {
        fields.push_back(field("", "⚙ has a drop-in outside /usr/lib", warning));
    }
    if (unit.shadowsPackagedUnit) {
        fields.push_back(field("", "⇪ overrides a unit shipped in /usr/lib", warning));
    }
    fields.push_back(
        field("edges", std::to_string(graph.dependenciesOf(unit.key).size()) + " dependencies, " +
                           std::to_string(graph.dependentsOf(unit.key).size()) + " dependents"));
    return fields;
}

/// "exited with status 1", "killed by signal 9", or empty when the process never ran.
std::string mainProcessOutcome(const Unit& unit) {
    switch (unit.mainExitKind) {
    case 1:
        return "exited with status " + std::to_string(unit.mainExitStatus);
    case 2:
        return "killed by signal " + std::to_string(unit.mainExitStatus);
    case 3:
        return "dumped core (signal " + std::to_string(unit.mainExitStatus) + ")";
    default:
        return "";
    }
}

/// Only the facts that apply to this unit, so an idle target shows an empty section.
ftxui::Elements runtimeFields(const Unit& unit) {
    ftxui::Elements fields;
    if (!unit.result.empty()) {
        ftxui::Decorator style =
            unit.result == "success" ? ftxui::nothing : ftxui::color(ftxui::Color::Red);
        fields.push_back(field("result", unit.result, style));
    }
    std::string outcome = mainProcessOutcome(unit);
    if (!outcome.empty()) {
        fields.push_back(field("main exit", outcome));
    }
    if (unit.mainPid != 0) {
        fields.push_back(field("main pid", std::to_string(unit.mainPid)));
    }
    if (unit.memoryBytes != 0) {
        fields.push_back(field("memory", formatBytes(unit.memoryBytes)));
    }
    if (unit.restartCount != 0) {
        fields.push_back(field("restarts", std::to_string(unit.restartCount),
                               ftxui::color(ftxui::Color::Yellow)));
    }
    if (unit.lastTriggerUsec != 0) {
        fields.push_back(field("last run", formatWallClockWithDistance(unit.lastTriggerUsec)));
    }
    if (unit.nextElapseUsec != 0) {
        fields.push_back(field("next run", formatWallClockWithDistance(unit.nextElapseUsec)));
    }
    return fields;
}

bool isTriggeringCondition(const std::string& condition) {
    return condition.find("=|") != std::string::npos;
}

// Plain conditions must all hold; "|" (triggering) ones need just one to hold. So when
// every listed condition is triggering, a skip means none of them held.
std::string skipReasonIntro(const std::vector<std::string>& conditions, bool knowsWhich) {
    if (knowsWhich) {
        return "because this did not hold:";
    }
    for (const std::string& condition : conditions) {
        if (!isTriggeringCondition(condition)) {
            return "because one of these did not hold:";
        }
    }
    return "because none of these held:";
}

/// Why a Condition*= check skipped the unit: the failed conditions if systemd kept them,
/// otherwise all of them, since at least one did not hold.
ftxui::Elements skippedFields(const Unit& unit) {
    ftxui::Elements fields = {field("skipped", "at " + formatSinceBoot(unit.conditionUsec))};
    bool knowsWhich = !unit.failedConditions.empty();
    const std::vector<std::string>& shown = knowsWhich ? unit.failedConditions : unit.conditions;
    if (shown.empty()) {
        return fields;
    }
    fields.push_back(field("", skipReasonIntro(shown, knowsWhich), ftxui::dim));
    for (const std::string& condition : shown) {
        fields.push_back(field("", condition, ftxui::color(ftxui::Color::Yellow)));
    }
    return fields;
}

std::vector<std::string> triggerNames(const UnitGraph& graph, const std::vector<Edge>& edges) {
    std::vector<std::string> names;
    for (const Edge& edge : edges) {
        const Unit* other = graph.find(edge.target);
        if (edge.kind == EdgeKind::Triggers && other != nullptr) {
            names.push_back(other->name);
        }
    }
    return names;
}

void appendList(ftxui::Elements& fields, const std::string& label,
                const std::vector<std::string>& values) {
    for (std::size_t index = 0; index < values.size(); ++index) {
        fields.push_back(field(index == 0 ? label : "", values[index]));
    }
}

/// The command a service runs, what a socket listens on, and what triggers what.
ftxui::Elements whatItRunsFields(const Unit& unit, const UnitGraph& graph) {
    ftxui::Elements fields;
    appendList(fields, "command", unit.commands);
    if (!unit.workingDirectory.empty()) {
        fields.push_back(field("workdir", unit.workingDirectory));
    }
    appendList(fields, "listens", unit.listenAddresses);
    // A unit's Triggers= edges point at what it activates (timer -> service); the reverse
    // edges name whoever activates it.
    appendList(fields, "triggers", triggerNames(graph, graph.dependenciesOf(unit.key)));
    appendList(fields, "trig. by", triggerNames(graph, graph.dependentsOf(unit.key)));
    return fields;
}

/// When the unit last started, how long that took, and how long it has been in its state.
ftxui::Elements timingFields(const Unit& unit, const UnitGraph& graph) {
    ftxui::Elements fields;
    if (unit.activatingUsec != 0) {
        std::string started = formatSinceBoot(unit.activatingUsec);
        if (graph.startedAfterBoot(unit)) {
            started += " · after boot";
        }
        fields.push_back(field("started", started));
    }
    std::uint64_t startup = startupDurationUsec(unit);
    if (startup != 0) {
        fields.push_back(field("took", formatDuration(startup)));
    }
    // Next to "took", this tells whether systemd cut a slow start short or the program
    // gave up by itself.
    if (unit.startTimeoutUsec == kNoTimeout) {
        fields.push_back(field("timeout", "none (waits forever)"));
    } else if (unit.startTimeoutUsec != 0) {
        fields.push_back(field("timeout", formatDuration(unit.startTimeoutUsec)));
    }
    bool isActive =
        unit.activeState == ActiveState::Active || unit.activeState == ActiveState::Reloading;
    if (isActive && unit.activeEnterUsec != 0) {
        fields.push_back(
            field("active", "for " + formatRoughSpan(monotonicAgeUsec(unit.activeEnterUsec)) +
                                ", since " + formatSinceBoot(unit.activeEnterUsec)));
    }
    bool wentInactive =
        unit.activeState == ActiveState::Inactive || unit.activeState == ActiveState::Failed;
    if (wentInactive && unit.inactiveEnterUsec != 0 &&
        unit.inactiveEnterUsec > unit.activatingUsec) {
        fields.push_back(field("stopped", formatSinceBoot(unit.inactiveEnterUsec)));
    }
    if (unit.conditionUsec != 0 && !unit.conditionResult) {
        appendAll(fields, skippedFields(unit));
    }
    return fields;
}

ftxui::Elements warningFields(const Unit& unit) {
    ftxui::Elements fields;
    if (!unit.wantedBy.empty()) {
        fields.push_back(field("⚠ warning",
                               "never started this boot, but these active units want it: " +
                                   joinLines(unit.wantedBy),
                               ftxui::color(ftxui::Color::Yellow)));
    }
    if (!unit.droppedByCycle.empty()) {
        fields.push_back(field("⚠ warning",
                               "start job dropped at boot to break an ordering cycle "
                               "(each unit waits for the next): " +
                                   unit.droppedByCycle,
                               ftxui::color(ftxui::Color::Yellow)));
    }
    return fields;
}

} // namespace

ftxui::Element renderDetails(const Unit* unit, const UnitGraph& graph) {
    using namespace ftxui;
    if (unit == nullptr) {
        return text("select a unit to see its details") | dim;
    }

    Elements lines = {text(unit->name) | bold, separatorEmpty()};
    Elements warnings = warningFields(*unit);
    if (!warnings.empty()) {
        appendAll(lines, warnings);
        lines.push_back(separatorEmpty());
    }
    appendAll(lines, identityFields(*unit));
    lines.push_back(separatorEmpty());
    appendAll(lines, stateFields(*unit));
    appendAll(lines, runtimeFields(*unit));
    Elements whatItRuns = whatItRunsFields(*unit, graph);
    if (!whatItRuns.empty()) {
        lines.push_back(separatorEmpty());
        appendAll(lines, whatItRuns);
    }
    Elements timing = timingFields(*unit, graph);
    if (!timing.empty()) {
        lines.push_back(separatorEmpty());
        appendAll(lines, timing);
    }
    lines.push_back(separatorEmpty());
    appendAll(lines, originFields(*unit, graph));
    if (!unit->error.empty()) {
        lines.push_back(separatorEmpty());
        lines.push_back(field("error", unit->error, color(Color::Red)));
    }
    return vbox(lines);
}

} // namespace nyst
