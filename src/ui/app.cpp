// Screen lifecycle, overall layout, and key handling for the TUI.
#include "ui/app.hpp"

#include "model/unit_graph.hpp"
#include "source/actions.hpp"
#include "source/journal.hpp"
#include "source/live_updates.hpp"
#include "source/loader.hpp"
#include "ui/details_pane.hpp"
#include "ui/dialogs.hpp"
#include "ui/filters.hpp"
#include "ui/journal_pane.hpp"
#include "ui/tree_view.hpp"
#include "util/debug_log.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <cctype>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace nyst {

namespace {

const int kPageSize = 20;
const int kWheelStep = 3;

/// Which part of the screen receives key presses.
enum class InputMode { Tree, Search, FilterPanel, ConfirmAction, Help };

std::string toString(InputMode mode) {
    switch (mode) {
    case InputMode::Tree:
        return "tree";
    case InputMode::Search:
        return "search";
    case InputMode::FilterPanel:
        return "filter-panel";
    case InputMode::ConfirmAction:
        return "confirm-action";
    case InputMode::Help:
        return "help";
    }
    return "unknown";
}

/// What the background loader hands back to the UI thread.
struct LoadResult {
    UnitGraph graph;
    std::string sourceStatus;
};

class Application {
public:
    Application();
    int run();

private:
    void startReload();
    void applyLoadResult(LoadResult& result);
    void startWatching();
    void applyLiveChanges(const UnitChanges& changes);
    void setMode(InputMode mode);
    bool handleEvent(const ftxui::Event& event);
    bool handleTreeModeEvent(const ftxui::Event& event);
    bool handleSearchEvent(const ftxui::Event& event);
    bool handleFilterPanelEvent(const ftxui::Event& event);
    bool handleConfirmEvent(const ftxui::Event& event);
    bool handleHelpEvent(const ftxui::Event& event);
    bool handleActionKey(const ftxui::Event& event);
    void askToRunAction(UnitAction action);
    void askToReloadDaemon();
    void askToRun(const std::string& question, const std::string& description,
                  std::function<int()> command);
    void runPendingAction();
    void setNotice(const std::string& message, bool isError);
    bool handleMovementKey(const ftxui::Event& event);
    bool handleTreeKey(const ftxui::Event& event);
    bool handlePaneKey(const ftxui::Event& event);
    void openFullJournal();
    void openUnitFile();
    const Unit* selectedUnit() const;
    bool handleMouseEvent(ftxui::Event event);
    bool handleHeaderClick(const ftxui::Mouse& mouse);

    ftxui::Element render();
    ftxui::Element renderHeader();
    ftxui::Element renderSearchBox();
    ftxui::Element renderDetailsPane();
    bool handleDetailsWheel(const ftxui::Mouse& mouse);
    ftxui::Element renderStatusBar() const;
    ftxui::Element addOverlay(ftxui::Element main);
    std::vector<std::string> keyHints() const;

    UnitGraph graph_;
    std::string sourceStatus_;
    // Loading runs on loaderThread_; these two are only touched on the UI thread.
    std::thread loaderThread_;
    bool loading_ = false;
    bool reloadQueued_ = false;
    std::unique_ptr<UnitWatcher> watcher_; // live updates; created once the screen exists
    TreeView tree_;
    JournalPane journal_;
    FilterState filters_ = defaultFilters();
    InputMode mode_ = InputMode::Tree;
    // Neither component is attached to the screen: events are routed by mode by hand.
    ftxui::Component searchInput_;
    ftxui::Component filterPanel_;
    ftxui::Component confirmDialog_; // rebuilt every time it opens
    ftxui::ScreenInteractive* screen_ = nullptr;

    // What the confirmation dialog will run on "yes", and how to describe it afterwards.
    std::function<int()> pendingCommand_;
    std::string pendingDescription_;
    // Outcome of the last action or command, shown in the status bar until replaced.
    std::string noticeMessage_;
    bool noticeIsError_ = false;

    // Screen areas from the last frame, for mouse hit-testing.
    ftxui::Box searchBoxArea_;
    ftxui::Box problemsLabelArea_;
    ftxui::Box directionLabelArea_;
    ftxui::Box filtersLabelArea_;
    ftxui::Box sortLabelArea_;
    ftxui::Box filterPanelArea_;
    ftxui::Box confirmDialogArea_;
    ftxui::Box detailsArea_;

    // Details pane scroll position in lines; reset whenever the selection changes.
    int detailsScroll_ = 0;
    int detailsContentHeight_ = 0;
    std::string detailsUnitKey_;
};

bool isLeftClick(const ftxui::Mouse& mouse) {
    return mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed;
}

bool isCharacter(const ftxui::Event& event, char character) {
    return event == ftxui::Event::Character(character);
}

/// Maps s/S/r/R/e/E/m/M to their action. Returns false for any other event.
bool actionForKey(const ftxui::Event& event, UnitAction& action) {
    if (isCharacter(event, 's')) {
        action = UnitAction::Start;
    } else if (isCharacter(event, 'S')) {
        action = UnitAction::Stop;
    } else if (isCharacter(event, 'r')) {
        action = UnitAction::Restart;
    } else if (isCharacter(event, 'R')) {
        action = UnitAction::Reload;
    } else if (isCharacter(event, 'e')) {
        action = UnitAction::Enable;
    } else if (isCharacter(event, 'E')) {
        action = UnitAction::Disable;
    } else if (isCharacter(event, 'm')) {
        action = UnitAction::Mask;
    } else if (isCharacter(event, 'M')) {
        action = UnitAction::Unmask;
    } else {
        return false;
    }
    return true;
}

std::string capitalized(std::string text) {
    if (!text.empty()) {
        text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
    }
    return text;
}

Application::Application() {
    ftxui::InputOption searchOptions;
    searchOptions.content = &filters_.search;
    searchOptions.placeholder = "name or description";
    searchOptions.multiline = false;
    // ftxui inverts a focused input, which turns the tinted box into a solid block;
    // the coloured border already shows that the search box is active.
    searchOptions.transform = [](ftxui::InputState state) {
        return state.is_placeholder ? state.element | ftxui::dim : state.element;
    };
    searchInput_ = ftxui::Input(searchOptions);
    filterPanel_ = makeFilterPanel(filters_, [this] { setMode(InputMode::Tree); });
}

int Application::run() {
    auto screen = ftxui::ScreenInteractive::Fullscreen();
    screen_ = &screen;
    startReload();
    startWatching();

    auto renderer = ftxui::Renderer([this] { return render(); });
    auto component = ftxui::CatchEvent(
        renderer, [this](const ftxui::Event& event) { return handleEvent(event); });
    screen.Loop(component);

    watcher_.reset();
    // A load may still be running and about to Post to the screen; let it finish first.
    if (loaderThread_.joinable()) {
        loaderThread_.join();
    }
    screen_ = nullptr;
    return 0;
}

// Loads on a background thread so the UI stays responsive; the result is handed back
// through screen_->Post, so graph_ is only ever touched on the UI thread.
void Application::startReload() {
    if (loading_) {
        reloadQueued_ = true;
        return;
    }
    loading_ = true;
    if (loaderThread_.joinable()) {
        loaderThread_.join();
    }
    ftxui::ScreenInteractive* screen = screen_;
    loaderThread_ = std::thread([this, screen] {
        auto result = std::make_shared<LoadResult>();
        result->graph = loadEverything(result->sourceStatus);
        screen->Post([this, result] { applyLoadResult(*result); });
        screen->PostEvent(ftxui::Event::Custom);
    });
}

// The tree keeps a pointer into graph_, so it is re-pointed after every load.
void Application::applyLoadResult(LoadResult& result) {
    graph_ = std::move(result.graph);
    sourceStatus_ = result.sourceStatus;
    tree_.setGraph(&graph_);
    journal_.invalidate();
    loading_ = false;
    if (reloadQueued_) {
        reloadQueued_ = false;
        startReload();
    }
}

// Like loading, changes arrive on a background thread and are applied on the UI thread.
void Application::startWatching() {
    ftxui::ScreenInteractive* screen = screen_;
    watcher_ = std::make_unique<UnitWatcher>([this, screen](UnitChanges changes) {
        auto shared = std::make_shared<UnitChanges>(std::move(changes));
        screen->Post([this, shared] { applyLiveChanges(*shared); });
        screen->PostEvent(ftxui::Event::Custom);
    });
}

// Units nyst already knows get their runtime state refreshed in place. Anything else
// (a newly loaded unit, a finished daemon-reload) needs the full picture: edges, origin,
// and groups may all have changed, so it triggers a background reload instead.
void Application::applyLiveChanges(const UnitChanges& changes) {
    if (changes.managerReloaded) {
        startReload();
        return;
    }
    const Unit* selected = selectedUnit();
    std::string selectedKey = selected == nullptr ? "" : selected->key;
    bool needsReload = false;
    for (const Unit& fresh : changes.changedUnits) {
        const Unit* known = graph_.find(fresh.key);
        if (known == nullptr || !known->isLoaded) {
            // `systemctl status` on an unloaded unit loads it briefly without starting it;
            // only a unit that actually runs is worth a full reload.
            needsReload = needsReload || fresh.activeState != ActiveState::Inactive;
            continue;
        }
        graph_.updateRuntimeState(fresh);
        if (fresh.key == selectedKey) {
            journal_.invalidate();
        }
    }
    graph_.rebuildDiagnostics();
    tree_.setGraph(&graph_);
    if (needsReload) {
        startReload();
    }
}

const Unit* Application::selectedUnit() const {
    const Row* row = tree_.selectedRow();
    return row == nullptr ? nullptr : graph_.find(row->unitKey);
}

// Leaves the fullscreen UI so journalctl's pager owns the terminal until it exits.
void Application::openFullJournal() {
    const Unit* unit = selectedUnit();
    if (unit == nullptr) {
        return;
    }
    std::string error;
    screen_->WithRestoredIO([&error, unit] { error = showFullJournal(*unit); })();
    if (!error.empty()) {
        setNotice(error, true);
    }
}

// Leaves the fullscreen UI so systemctl's pager owns the terminal until it exits.
void Application::openUnitFile() {
    const Unit* unit = selectedUnit();
    if (unit == nullptr) {
        return;
    }
    std::string error;
    screen_->WithRestoredIO([&error, unit] { error = showUnitFile(*unit); })();
    if (!error.empty()) {
        setNotice(error, true);
    }
}

void Application::setNotice(const std::string& message, bool isError) {
    noticeMessage_ = message;
    noticeIsError_ = isError;
}

void Application::askToRunAction(UnitAction action) {
    const Unit* unit = selectedUnit();
    if (unit == nullptr) {
        setNotice("select a unit first", true);
        return;
    }
    std::string reason = whyActionUnavailable(*unit, action);
    if (!reason.empty()) {
        setNotice(reason, true);
        return;
    }
    // A copy, so the command stays valid even if the graph is reloaded meanwhile.
    Unit target = *unit;
    askToRun(capitalized(toString(action)) + " " + unit->name + " (" + toString(unit->manager) +
                 ")?",
             toString(action) + " " + unit->name,
             [target, action] { return runActionInTerminal(target, action); });
}

void Application::askToReloadDaemon() {
    const Unit* unit = selectedUnit();
    Manager manager = unit == nullptr ? Manager::System : unit->manager;
    askToRun("Reload the " + toString(manager) + " manager configuration (daemon-reload)?",
             toString(manager) + " daemon-reload",
             [manager] { return runDaemonReloadInTerminal(manager); });
}

void Application::askToRun(const std::string& question, const std::string& description,
                           std::function<int()> command) {
    pendingCommand_ = command;
    pendingDescription_ = description;
    confirmDialog_ = makeConfirmDialog(
        question, [this] { runPendingAction(); }, [this] { setMode(InputMode::Tree); });
    setMode(InputMode::ConfirmAction);
}

// Leaves the fullscreen UI so systemctl (and polkit's password prompt) own the terminal.
// Everything is reloaded afterwards: simpler than refreshing one unit, and an action can
// change the state of the units around it too.
void Application::runPendingAction() {
    setMode(InputMode::Tree);
    if (!pendingCommand_) {
        return;
    }
    int exitCode = -1;
    std::function<int()> command = pendingCommand_;
    screen_->WithRestoredIO([&exitCode, command] { exitCode = command(); })();
    pendingCommand_ = nullptr;

    startReload();
    if (exitCode == 0) {
        setNotice(pendingDescription_ + ": done", false);
    } else {
        setNotice(pendingDescription_ + ": failed (exit " + std::to_string(exitCode) + ")", true);
    }
}

void Application::setMode(InputMode mode) {
    debugLog("input mode: " + toString(mode_) + " -> " + toString(mode));
    mode_ = mode;
}

bool Application::handleEvent(const ftxui::Event& event) {
    if (event.is_mouse()) {
        return handleMouseEvent(event);
    }
    switch (mode_) {
    case InputMode::Search:
        return handleSearchEvent(event);
    case InputMode::FilterPanel:
        return handleFilterPanelEvent(event);
    case InputMode::ConfirmAction:
        return handleConfirmEvent(event);
    case InputMode::Help:
        return handleHelpEvent(event);
    case InputMode::Tree:
        break;
    }
    return handleTreeModeEvent(event);
}

bool Application::handleConfirmEvent(const ftxui::Event& event) {
    if (isCharacter(event, 'y') || isCharacter(event, 'Y')) {
        runPendingAction();
        return true;
    }
    if (isCharacter(event, 'n') || isCharacter(event, 'N') || event == ftxui::Event::Escape) {
        setMode(InputMode::Tree);
        return true;
    }
    return confirmDialog_->OnEvent(event);
}

bool Application::handleHelpEvent(const ftxui::Event& event) {
    if (event == ftxui::Event::Escape || isCharacter(event, '?') || isCharacter(event, 'q')) {
        setMode(InputMode::Tree);
    }
    // Swallow everything else so keys don't act on the tree hidden behind the help.
    return true;
}

bool Application::handleActionKey(const ftxui::Event& event) {
    if (isCharacter(event, 'D')) {
        askToReloadDaemon();
        return true;
    }
    UnitAction action = UnitAction::Start;
    if (!actionForKey(event, action)) {
        return false;
    }
    askToRunAction(action);
    return true;
}

// Takes the event by value: ftxui only exposes the mouse data through a non-const accessor.
bool Application::handleMouseEvent(ftxui::Event event) {
    const ftxui::Mouse& mouse = event.mouse();
    if (mode_ == InputMode::Help) {
        if (isLeftClick(mouse)) {
            setMode(InputMode::Tree);
        }
        return true;
    }
    if (mode_ == InputMode::ConfirmAction) {
        if (isLeftClick(mouse) && !confirmDialogArea_.Contain(mouse.x, mouse.y)) {
            setMode(InputMode::Tree);
            return true;
        }
        return confirmDialog_->OnEvent(event);
    }
    if (mode_ == InputMode::FilterPanel) {
        if (isLeftClick(mouse) && !filterPanelArea_.Contain(mouse.x, mouse.y)) {
            setMode(InputMode::Tree);
            return true;
        }
        return filterPanel_->OnEvent(event);
    }
    if (mode_ == InputMode::Search && searchBoxArea_.Contain(mouse.x, mouse.y)) {
        return searchInput_->OnEvent(event);
    }
    if (isLeftClick(mouse) && handleHeaderClick(mouse)) {
        return true;
    }
    if (mode_ == InputMode::Search && isLeftClick(mouse)) {
        setMode(InputMode::Tree);
    }
    if (handleDetailsWheel(mouse)) {
        return true;
    }
    if (journal_.isVisible() && journal_.handleMouse(mouse)) {
        return true;
    }
    return tree_.handleMouse(mouse);
}

bool Application::handleHeaderClick(const ftxui::Mouse& mouse) {
    if (searchBoxArea_.Contain(mouse.x, mouse.y)) {
        setMode(InputMode::Search);
    } else if (filtersLabelArea_.Contain(mouse.x, mouse.y)) {
        setMode(InputMode::FilterPanel);
    } else if (directionLabelArea_.Contain(mouse.x, mouse.y)) {
        tree_.toggleDirection();
    } else if (sortLabelArea_.Contain(mouse.x, mouse.y)) {
        tree_.cycleListOrder();
    } else if (problemsLabelArea_.Contain(mouse.x, mouse.y)) {
        filters_.problemsOnly = !filters_.problemsOnly;
    } else {
        return false;
    }
    return true;
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
    if (isCharacter(event, '?')) {
        setMode(InputMode::Help);
        return true;
    }
    if (isCharacter(event, 'q')) {
        screen_->Exit();
        return true;
    }
    if (isCharacter(event, 'u')) {
        startReload();
        return true;
    }
    return handleMovementKey(event) || handleTreeKey(event) || handlePaneKey(event) ||
           handleActionKey(event);
}

bool Application::handlePaneKey(const ftxui::Event& event) {
    if (isCharacter(event, 'J')) {
        journal_.toggleVisible();
    } else if (isCharacter(event, 'L')) {
        openFullJournal();
    } else if (isCharacter(event, 'c')) {
        openUnitFile();
    } else {
        return false;
    }
    return true;
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
    } else if (isCharacter(event, 'b')) {
        tree_.cycleListOrder();
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
    journal_.showUnit(selectedUnit());

    Elements sections = {
        renderHeader(),
        hbox({
            window(text(" tree "), tree_.render()) | flex,
            renderDetailsPane(),
        }) | flex,
    };
    if (journal_.isVisible()) {
        sections.push_back(journal_.render());
    }
    sections.push_back(renderStatusBar());
    return addOverlay(vbox(sections));
}

ftxui::Element Application::addOverlay(ftxui::Element main) {
    using namespace ftxui;
    Element overlay;
    switch (mode_) {
    case InputMode::FilterPanel:
        overlay = filterPanel_->Render() | reflect(filterPanelArea_);
        break;
    case InputMode::ConfirmAction:
        overlay = confirmDialog_->Render() | reflect(confirmDialogArea_);
        break;
    case InputMode::Help:
        overlay = renderHelpOverlay();
        break;
    case InputMode::Tree:
    case InputMode::Search:
        return main;
    }
    return dbox({main, overlay | clear_under | center});
}

// yframe centres whatever is focused, so focusing the line half a pane below the
// desired top edge scrolls the content by exactly detailsScroll_ lines.
ftxui::Element Application::renderDetailsPane() {
    using namespace ftxui;
    const Unit* unit = selectedUnit();
    std::string key = unit == nullptr ? "" : unit->key;
    if (key != detailsUnitKey_) {
        detailsUnitKey_ = key;
        detailsScroll_ = 0;
    }

    Element content = renderDetails(unit, graph_);
    content->ComputeRequirement();
    detailsContentHeight_ = content->requirement().min_y;

    // ftxui measures the frame as y_max - y_min (one less than its rows); the window's
    // border takes another two.
    int frameSpan = std::max(0, detailsArea_.y_max - detailsArea_.y_min - 2);
    Element scrolled = content | focusPosition(0, detailsScroll_ + frameSpan / 2) |
                       vscroll_indicator | yframe | flex;
    return window(text(" details "), scrolled) | size(WIDTH, EQUAL, kDetailsPaneWidth) |
           reflect(detailsArea_);
}

bool Application::handleDetailsWheel(const ftxui::Mouse& mouse) {
    using ftxui::Mouse;
    bool isWheel = mouse.button == Mouse::WheelUp || mouse.button == Mouse::WheelDown;
    if (!isWheel || !detailsArea_.Contain(mouse.x, mouse.y)) {
        return false;
    }
    int visibleRows = detailsArea_.y_max - detailsArea_.y_min - 1;
    int maxScroll = std::max(0, detailsContentHeight_ - visibleRows);
    int step = mouse.button == Mouse::WheelUp ? -kWheelStep : kWheelStep;
    detailsScroll_ = std::clamp(detailsScroll_ + step, 0, maxScroll);
    return true;
}

ftxui::Element Application::renderSearchBox() {
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
    return box | size(WIDTH, GREATER_THAN, 40) | reflect(searchBoxArea_);
}

// The labels are a right-aligned flexbox beside the search box, so on a narrow terminal
// they wrap onto extra lines in that column instead of being cut off or pushed under it.
ftxui::Element Application::renderHeader() {
    using namespace ftxui;
    Elements labels;
    std::string focused = tree_.focusedUnitKey();
    if (!focused.empty()) {
        labels.push_back(text("focus: " + focused) | color(Color::Cyan));
        labels.push_back(text("(Backspace to go back)") | dim);
    }
    // Every label is always present so it can be clicked to toggle.
    Element problems = filters_.problemsOnly ? text("[problems only]") | color(Color::Red) | bold
                                             : text("[problems: off]") | dim;
    labels.push_back(problems | reflect(problemsLabelArea_));
    labels.push_back(text("[dir: " + toString(tree_.direction()) + "]") |
                     reflect(directionLabelArea_));
    labels.push_back(text("[sort: " + toString(tree_.listOrder()) + "]") | reflect(sortLabelArea_));
    labels.push_back(text("[filters: " + std::to_string(countDisabledFilters(filters_)) + " off]") |
                     reflect(filtersLabelArea_));

    FlexboxConfig layout;
    layout.Set(FlexboxConfig::JustifyContent::FlexEnd);
    layout.SetGap(1, 0);
    Element labelArea = flexbox(labels, layout) | vcenter | flex;
    return hbox({renderSearchBox(), text(" "), labelArea, text(" ")});
}

// A flexbox so that, on a narrow terminal, whole hints wrap onto extra lines
// instead of the end of the bar being cut off.
ftxui::Element Application::renderStatusBar() const {
    using namespace ftxui;
    Elements items;
    if (loading_) {
        items.push_back(text("loading units...") | color(Color::Cyan) | bold);
    }
    if (!sourceStatus_.empty()) {
        items.push_back(text(summarizeUnits(graph_) + " · " + sourceStatus_));
    }
    if (!noticeMessage_.empty()) {
        Color noticeColor = noticeIsError_ ? Color::Red : Color::Green;
        items.push_back(text(noticeMessage_) | color(noticeColor) | bold);
    }
    for (const std::string& hint : keyHints()) {
        items.push_back(text(hint) | dim);
    }
    FlexboxConfig layout;
    layout.SetGap(3, 0);
    return flexbox(items, layout) | xflex;
}

std::vector<std::string> Application::keyHints() const {
    switch (mode_) {
    case InputMode::Search:
        return {"Enter keep", "Esc clear", "↑↓ move"};
    case InputMode::FilterPanel:
        return {"Space/click toggle", "p problems", "Esc/click outside close"};
    case InputMode::ConfirmAction:
        return {"y yes", "n/Esc no"};
    case InputMode::Help:
        return {"Esc/?/click close"};
    case InputMode::Tree:
        break;
    }
    return {"/ search",        "F filters",       "p problems",         "d direction",
            "b sort order",    "Enter focus",     "J journal",          "L full log",
            "c unit file",     "s/S start/stop",  "r/R restart/reload", "e/E enable/disable",
            "m/M mask/unmask", "D daemon-reload", "u reload",           "? help",
            "q quit"};
}

} // namespace

int runTui() {
    Application application;
    return application.run();
}

} // namespace nyst
