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

void appendAll(ftxui::Elements& target, const ftxui::Elements& source) {
    target.insert(target.end(), source.begin(), source.end());
}

} // namespace

ftxui::Element renderDetails(const Unit* unit, const UnitGraph& graph) {
    using namespace ftxui;
    if (unit == nullptr) {
        return text("select a unit to see its details") | dim;
    }

    Elements lines = {text(unit->name) | bold, separatorEmpty()};
    appendAll(lines, identityFields(*unit));
    lines.push_back(separatorEmpty());
    appendAll(lines, stateFields(*unit));
    appendAll(lines, runtimeFields(*unit));
    lines.push_back(separatorEmpty());
    appendAll(lines, originFields(*unit, graph));
    if (!unit->error.empty()) {
        lines.push_back(separatorEmpty());
        lines.push_back(field("error", unit->error, color(Color::Red)));
    }
    return vbox(lines);
}

} // namespace nyst
