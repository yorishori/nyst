// Recent journal lines for the selected unit, reloaded whenever the selection changes.
#pragma once

#include "model/unit.hpp"

#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

#include <string>
#include <vector>

namespace nyst {

/// Number of journal lines fetched per unit.
const int kJournalLineCount = 30;

class JournalPane {
public:
    bool isVisible() const;
    void toggleVisible();

    /// Loads the unit's lines if it differs from the one already shown. nullptr clears the pane.
    void showUnit(const Unit* unit);

    /// Forgets the loaded lines so the next showUnit() fetches them again.
    void invalidate();

    ftxui::Element render() const;

    /// Wheel scrolls through the lines. Returns false if the mouse is outside the pane.
    bool handleMouse(const ftxui::Mouse& mouse);

private:
    bool visible_ = true;
    std::string unitKey_;
    std::vector<std::string> lines_;
    int scrollFromBottom_ = 0;
    // Filled in by the renderer during layout; mutable because render() is const.
    mutable ftxui::Box paneBox_;
};

} // namespace nyst
