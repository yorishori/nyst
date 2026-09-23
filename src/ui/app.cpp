// Screen lifecycle, overall layout, and key handling for the TUI.
#include "ui/app.hpp"

#include "model/unit_graph.hpp"
#include "source/loader.hpp"
#include "ui/tree_view.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <iostream>
#include <string>

namespace nyst {

namespace {

const int kPageSize = 20;

class Application {
public:
    int run();

private:
    void reloadUnits();
    bool handleEvent(const ftxui::Event& event);
    bool handleMovementKey(const ftxui::Event& event);
    bool handleTreeKey(const ftxui::Event& event);

    ftxui::Element render() const;
    ftxui::Element renderHeader() const;
    ftxui::Element renderStatusBar() const;

    UnitGraph graph_;
    std::string statusMessage_;
    TreeView tree_;
    ftxui::ScreenInteractive* screen_ = nullptr;
};

bool isCharacter(const ftxui::Event& event, char character) {
    return event == ftxui::Event::Character(character);
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

bool Application::handleEvent(const ftxui::Event& event) {
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

ftxui::Element Application::render() const {
    using namespace ftxui;
    return vbox({
        renderHeader(),
        window(text(" tree "), tree_.render()) | flex,
        renderStatusBar(),
    });
}

ftxui::Element Application::renderHeader() const {
    using namespace ftxui;
    Elements parts = {text(" nyst ") | bold | inverted, text(" ")};
    std::string focused = tree_.focusedUnitKey();
    if (!focused.empty()) {
        parts.push_back(text("focus: " + focused) | color(Color::Cyan));
        parts.push_back(text("  (Backspace to go back)") | dim);
    }
    parts.push_back(filler());
    parts.push_back(text("[dir: " + toString(tree_.direction()) + "] "));
    return hbox(parts);
}

ftxui::Element Application::renderStatusBar() const {
    using namespace ftxui;
    return hbox({
        text(" " + statusMessage_),
        filler(),
        text("d direction · Enter focus · u reload · q quit ") | dim,
    });
}

} // namespace

int runTui() {
    Application application;
    return application.run();
}

} // namespace nyst
