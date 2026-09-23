// Recent journal lines for the selected unit, reloaded whenever the selection changes.
// Loading is synchronous; if moving the cursor feels slow, switch to a debounced background load.
#include "ui/journal_pane.hpp"

#include "source/journal.hpp"

#include <algorithm>

namespace nyst {

namespace {

const int kPaneHeight = 12;
const int kWheelStep = 3;

} // namespace

bool JournalPane::isVisible() const {
    return visible_;
}

void JournalPane::toggleVisible() {
    visible_ = !visible_;
}

void JournalPane::showUnit(const Unit* unit) {
    // While hidden, keep nothing, so showing the pane again triggers a fresh load.
    if (unit == nullptr || !visible_) {
        invalidate();
        return;
    }
    if (unit->key == unitKey_) {
        return;
    }
    unitKey_ = unit->key;
    scrollFromBottom_ = 0;
    lines_ = recentLogLines(*unit, kJournalLineCount);
}

void JournalPane::invalidate() {
    unitKey_.clear();
    lines_.clear();
}

ftxui::Element JournalPane::render() const {
    using namespace ftxui;
    std::string title = " journal (last " + std::to_string(kJournalLineCount) + " lines) ";

    Elements rows;
    for (const std::string& line : lines_) {
        rows.push_back(text(line));
    }
    if (rows.empty()) {
        rows.push_back(text(unitKey_.empty() ? "no unit selected" : "no log lines") | dim);
    }

    // Focusing a line keeps yframe scrolled to it; normally that is the newest line.
    int focusedIndex = std::max(0, static_cast<int>(rows.size()) - 1 - scrollFromBottom_);
    rows[focusedIndex] = rows[focusedIndex] | focus;

    return window(text(title), vbox(rows) | vscroll_indicator | yframe | flex) |
           size(HEIGHT, EQUAL, kPaneHeight) | reflect(paneBox_);
}

bool JournalPane::handleMouse(const ftxui::Mouse& mouse) {
    using ftxui::Mouse;
    if (!paneBox_.Contain(mouse.x, mouse.y)) {
        return false;
    }
    int maxScroll = std::max(0, static_cast<int>(lines_.size()) - 1);
    if (mouse.button == Mouse::WheelUp) {
        scrollFromBottom_ = std::min(maxScroll, scrollFromBottom_ + kWheelStep);
    } else if (mouse.button == Mouse::WheelDown) {
        scrollFromBottom_ = std::max(0, scrollFromBottom_ - kWheelStep);
    }
    return true;
}

} // namespace nyst
