// Screen lifecycle, overall layout, and key handling for the TUI.
#include "ui/app.hpp"

#include "model/unit_graph.hpp"
#include "source/loader.hpp"
#include "ui/filters.hpp"
#include "ui/tree_view.hpp"
#include "util/debug_log.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <iostream>
#include <string>

namespace nyst {

namespace {

const int kPageSize = 20;

/// Which part of the screen receives key presses.
enum class InputMode { Tree, Search, FilterPanel };

std::string toString(InputMode mode) {
    switch (mode) {
    case InputMode::Tree:
        return "tree";
    case InputMode::Search:
        return "search";
    case InputMode::FilterPanel:
        return "filter-panel";
    }
    return "unknown";
}

class Application {
public:
    Application();
    int run();

private:
    void reloadUnits();
    void setMode(InputMode mode);
    bool handleEvent(const ftxui::Event& event);
    bool handleTreeModeEvent(const ftxui::Event& event);
    bool handleSearchEvent(const ftxui::Event& event);
    bool handleFilterPanelEvent(const ftxui::Event& event);
    bool handleMovementKey(const ftxui::Event& event);
    bool handleTreeKey(const ftxui::Event& event);

    ftxui::Element render();
    ftxui::Element renderHeader() const;
    ftxui::Element renderSearchBox() const;
    ftxui::Element renderStatusBar() const;
    std::string keyHints() const;

    UnitGraph graph_;
    std::string statusMessage_;
    TreeView tree_;
    FilterState filters_ = defaultFilters();
    InputMode mode_ = InputMode::Tree;
    // Neither component is attached to the screen: events are routed by mode by hand.
    ftxui::Component searchInput_;
    ftxui::Component filterPanel_;
    ftxui::ScreenInteractive* screen_ = nullptr;
};

bool isCharacter(const ftxui::Event& event, char character) {
    return event == ftxui::Event::Character(character);
}

Application::Application() {
    ftxui::InputOption searchOptions;
    searchOptions.content = &filters_.search;
    searchOptions.placeholder = "name or description";
    searchOptions.multiline = false;
    searchInput_ = ftxui::Input(searchOptions);
    filterPanel_ = makeFilterPanel(filters_);
}

int Application::run() {
    std::cerr << "nyst: loading units..." << std::endl;
    reloadUnits();

    auto screen = ftxui::ScreenInteractive::Fullscreen();
    screen_ = &screen;

    auto renderer = ftxui::Renderer([this] { return render(); });
    auto component = ftxui::CatchEvent(
        renderer, [this](const ftxui::Event& event) { return handleEvent(event); });
    screen.Loop(component);

    screen_ = nullptr;
    return 0;
}

// The tree keeps a pointer into graph_, so refresh it after every load.
void Application::reloadUnits() {
    graph_ = loadEverything(statusMessage_);
    tree_.setGraph(&graph_);
}

void Application::setMode(InputMode mode) {
    debugLog("input mode: " + toString(mode_) + " -> " + toString(mode));
    mode_ = mode;
}

bool Application::handleEvent(const ftxui::Event& event) {
    switch (mode_) {
    case InputMode::Search:
        return handleSearchEvent(event);
    case InputMode::FilterPanel:
        return handleFilterPanelEvent(event);
    case InputMode::Tree:
        break;
    }
    return handleTreeModeEvent(event);
}

bool Application::handleSearchEvent(const ftxui::Event& event) {
    using ftxui::Event;
    if (event == Event::Escape) {
        filters_.search.clear();
        setMode(InputMode::Tree);
        return true;
    }
    if (event == Event::Return) {
        setMode(InputMode::Tree);
        return true;
    }
    // Let the cursor move through results without leaving the search box.
    if (event == Event::ArrowUp || event == Event::ArrowDown || event == Event::PageUp ||
        event == Event::PageDown) {
        return handleMovementKey(event);
    }
    return searchInput_->OnEvent(event);
}

bool Application::handleFilterPanelEvent(const ftxui::Event& event) {
    if (event == ftxui::Event::Escape || isCharacter(event, 'F')) {
        setMode(InputMode::Tree);
        return true;
    }
    if (isCharacter(event, 'p')) {
        filters_.problemsOnly = !filters_.problemsOnly;
        return true;
    }
    return filterPanel_->OnEvent(event);
}

bool Application::handleTreeModeEvent(const ftxui::Event& event) {
    if (isCharacter(event, '/')) {
        setMode(InputMode::Search);
        return true;
    }
    if (isCharacter(event, 'F')) {
        setMode(InputMode::FilterPanel);
        return true;
    }
    if (isCharacter(event, 'p')) {
        filters_.problemsOnly = !filters_.problemsOnly;
        return true;
    }
    if (isCharacter(event, 'q')) {
        screen_->Exit();
        return true;
    }
    if (isCharacter(event, 'u')) {
        reloadUnits();
        return true;
    }
    return handleMovementKey(event) || handleTreeKey(event);
}

bool Application::handleMovementKey(const ftxui::Event& event) {
    using ftxui::Event;
    if (event == Event::ArrowUp || isCharacter(event, 'k')) {
        tree_.moveCursor(-1);
    } else if (event == Event::ArrowDown || isCharacter(event, 'j')) {
        tree_.moveCursor(1);
    } else if (event == Event::PageUp) {
        tree_.moveCursor(-kPageSize);
    } else if (event == Event::PageDown) {
        tree_.moveCursor(kPageSize);
    } else if (event == Event::Home || isCharacter(event, 'g')) {
        tree_.moveCursorToStart();
    } else if (event == Event::End || isCharacter(event, 'G')) {
        tree_.moveCursorToEnd();
    } else {
        return false;
    }
    return true;
}

bool Application::handleTreeKey(const ftxui::Event& event) {
    using ftxui::Event;
    if (event == Event::ArrowRight || isCharacter(event, 'l')) {
        tree_.expandSelected();
    } else if (event == Event::ArrowLeft || isCharacter(event, 'h')) {
        tree_.collapseSelected();
    } else if (isCharacter(event, ' ')) {
        tree_.toggleSelected();
    } else if (event == Event::Return) {
        tree_.focusSelected();
    } else if (event == Event::Backspace) {
        tree_.goBack();
    } else if (isCharacter(event, 'd')) {
        tree_.toggleDirection();
    } else {
        return false;
    }
    return true;
}

// Filters are applied here, once per frame, so every edit (typing, checkboxes, p)
// shows up without each handler having to remember to refresh the tree.
ftxui::Element Application::render() {
    using namespace ftxui;
    tree_.setFilters(filters_);
    Element main = vbox({
        renderHeader(),
        window(text(" tree "), tree_.render()) | flex,
        renderStatusBar(),
    });
    if (mode_ != InputMode::FilterPanel) {
        return main;
    }
    return dbox({main, filterPanel_->Render() | clear_under | center});
}

ftxui::Element Application::renderSearchBox() const {
    using namespace ftxui;
    Element content;
    if (mode_ == InputMode::Search) {
        content = searchInput_->Render();
    } else if (filters_.search.empty()) {
        content = text("press / to search") | dim;
    } else {
        content = text(filters_.search);
    }
    Element box = window(text(" / search "), content);
    if (mode_ == InputMode::Search) {
        box = box | color(ftxui::Color::Cyan);
    }
    return box | size(WIDTH, GREATER_THAN, 40);
}

ftxui::Element Application::renderHeader() const {
    using namespace ftxui;
    Elements labels;
    std::string focused = tree_.focusedUnitKey();
    if (!focused.empty()) {
        labels.push_back(text(" focus: " + focused) | color(Color::Cyan));
        labels.push_back(text("  (Backspace to go back)") | dim);
    }
    labels.push_back(filler());
    if (filters_.problemsOnly) {
        labels.push_back(text("[problems only] ") | color(Color::Red) | bold);
    }
    labels.push_back(text("[dir: " + toString(tree_.direction()) + "] "));
    labels.push_back(
        text("[filters: " + std::to_string(countDisabledFilters(filters_)) + " off] "));
    return hbox({renderSearchBox(), hbox(labels) | vcenter | flex});
}

ftxui::Element Application::renderStatusBar() const {
    using namespace ftxui;
    return hbox({
        text(" " + statusMessage_),
        filler(),
        text(keyHints() + " ") | dim,
    });
}

std::string Application::keyHints() const {
    switch (mode_) {
    case InputMode::Search:
        return "Enter keep · Esc clear · ↑↓ move";
    case InputMode::FilterPanel:
        return "Space toggle · p problems · Esc close";
    case InputMode::Tree:
        break;
    }
    return "/ search · F filters · p problems · d direction · Enter focus · u reload · q quit";
}

} // namespace

int runTui() {
    Application application;
    return application.run();
}

} // namespace nyst
