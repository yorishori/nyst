// Modal overlays: the action confirmation dialog and the key help screen.
#include "ui/dialogs.hpp"

#include "ui/button_style.hpp"

#include <ftxui/component/component.hpp>

#include <vector>

namespace nyst {

namespace {

struct KeyHelp {
    std::string keys;
    std::string action;
};

struct HelpSection {
    std::string title;
    std::vector<KeyHelp> entries;
};

std::vector<HelpSection> helpSections() {
    return {
        {"move",
         {{"↑/↓  j/k", "move cursor"},
          {"PgUp/PgDn", "move by a page"},
          {"Home/End  g/G", "first / last row"}}},
        {"tree",
         {{"←/→  h/l", "collapse / expand"},
          {"Space", "toggle expand"},
          {"Enter", "focus on unit"},
          {"Backspace", "back to previous root"},
          {"d", "flip direction"},
          {"b", "sort by startup time"}}},
        {"search and filters",
         {{"/", "search (Esc clears)"}, {"F", "filter panel"}, {"p", "problems only"}}},
        {"panes",
         {{"J", "show / hide journal"},
          {"L", "full journal (pager)"},
          {"c", "unit file + drop-ins (pager)"}}},
        {"actions (ask first)",
         {{"s / S", "start / stop"},
          {"r", "restart"},
          {"R", "reload unit"},
          {"e / E", "enable / disable"},
          {"m / M", "mask / unmask"},
          {"D", "daemon-reload"}}},
        {"other", {{"u", "reload all data"}, {"?", "this help"}, {"q", "quit"}}},
    };
}

ftxui::Element renderSection(const HelpSection& section) {
    using namespace ftxui;
    Elements lines = {text(section.title) | bold};
    for (const KeyHelp& entry : section.entries) {
        lines.push_back(hbox({
            text("  " + entry.keys) | color(Color::Cyan) | size(WIDTH, EQUAL, 17),
            text(entry.action),
        }));
    }
    lines.push_back(text(""));
    return vbox(lines);
}

} // namespace

ftxui::Component makeConfirmDialog(const std::string& question, std::function<void()> onYes,
                                   std::function<void()> onNo) {
    ftxui::Component yes = ftxui::Button("yes", onYes, bracketButtonStyle());
    ftxui::Component no = ftxui::Button("no", onNo, bracketButtonStyle());
    ftxui::Component buttons = ftxui::Container::Horizontal({yes, no});
    buttons->SetActiveChild(no);

    return ftxui::Renderer(buttons, [question, yes, no] {
        using namespace ftxui;
        return window(text(" confirm ") | bold,
                      vbox({
                          text(question),
                          text(""),
                          hbox({filler(), yes->Render(), text("  "), no->Render(), filler()}),
                          text("y / n · arrows + Enter · click") | dim | center,
                      }));
    });
}

ftxui::Element renderHelpOverlay() {
    using namespace ftxui;
    std::vector<HelpSection> sections = helpSections();
    // Two columns keep the overlay short enough for small terminals.
    Elements left;
    Elements right;
    for (std::size_t index = 0; index < sections.size(); ++index) {
        Elements& column = index < sections.size() / 2 ? left : right;
        column.push_back(renderSection(sections[index]));
    }
    return window(
        text(" keys ") | bold,
        vbox({
            hbox({vbox(left), text("   "), vbox(right)}),
            text("mouse: click rows, arrows, header labels and buttons; wheel scrolls") | dim,
            text("Esc, ? or a click closes this help") | dim,
        }));
}

} // namespace nyst
