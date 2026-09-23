// Shared look for clickable buttons in panels and dialogs.
#include "ui/button_style.hpp"

#include <ftxui/dom/elements.hpp>

namespace nyst {

ftxui::ButtonOption bracketButtonStyle() {
    ftxui::ButtonOption option;
    option.transform = [](const ftxui::EntryState& state) {
        ftxui::Element label = ftxui::text("[" + state.label + "]");
        return state.focused ? label | ftxui::inverted : label | ftxui::color(ftxui::Color::Cyan);
    };
    return option;
}

} // namespace nyst
