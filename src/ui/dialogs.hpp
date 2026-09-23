// Modal overlays: the action confirmation dialog and the key help screen.
#pragma once

#include <ftxui/component/component_base.hpp>
#include <ftxui/dom/elements.hpp>

#include <functional>
#include <string>

namespace nyst {

/// "<question> [yes] [no]" with "no" focused. Build a fresh one each time the dialog opens,
/// so the focus always starts on "no".
ftxui::Component makeConfirmDialog(const std::string& question, std::function<void()> onYes,
                                   std::function<void()> onNo);

/// Every key binding, grouped, for the `?` overlay.
ftxui::Element renderHelpOverlay();

} // namespace nyst
