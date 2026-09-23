// Shared look for clickable buttons in panels and dialogs.
#pragma once

#include <ftxui/component/component_options.hpp>

namespace nyst {

/// "[label]", cyan normally and inverted when focused. ftxui's Ascii style only shows the
/// brackets while focused; always showing them makes buttons recognisable as clickable.
ftxui::ButtonOption bracketButtonStyle();

} // namespace nyst
