// Tree expansion state, flattening into visible rows, cursor, and row rendering.
#include "ui/tree_view.hpp"

#include "ui/format.hpp"

#include <algorithm>
#include <deque>

namespace nyst {

namespace {

const char* const kUnreachableGroupId = "group:unreachable";
const char* const kNotLoadedGroupId = "group:not-loaded";
const char* const kSystemRootKey = "system:default.target";
const char* const kUserRootKey = "user:default.target";
const int kWheelStep = 3;

TreeNode makeUnitNode(const std::string& unitKey, const std::string& label) {
    TreeNode node;
    node.id = unitKey;
    node.unitKey = unitKey;
    node.label = label;
    return node;
}

TreeNode makeGroupNode(const std::string& id, const std::string& label) {
    TreeNode node;
    node.id = id;
    node.label = label;
    return node;
}

/// Edge targets may name an alias (default.target); the tree always uses the real key.
std::string canonicalKey(const UnitGraph& graph, const std::string& key) {
    const Unit* unit = graph.find(key);
    return unit == nullptr ? key : unit->key;
}

bool isGroup(const TreeNode& node) {
    return node.unitKey.empty();
}

// Strongest relation first, then by name, so duplicates keep the most meaningful kind.
bool childComesBefore(const TreeNode& left, const TreeNode& right) {
    if (left.edgeKind != right.edgeKind) {
        return left.edgeKind < right.edgeKind;
    }
    return left.id < right.id;
}

bool failedFirstThenByName(const Unit* left, const Unit* right) {
    bool leftFailed = left->activeState == ActiveState::Failed;
    bool rightFailed = right->activeState == ActiveState::Failed;
    if (leftFailed != rightFailed) {
        return leftFailed;
    }
    if (left->name != right->name) {
        return left->name < right->name;
    }
    return left->key < right->key;
}

bool slowestStartupFirst(const Unit* left, const Unit* right) {
    std::uint64_t leftDuration = startupDurationUsec(*left);
    std::uint64_t rightDuration = startupDurationUsec(*right);
    if (leftDuration != rightDuration) {
        return leftDuration > rightDuration;
    }
    return left->key < right->key;
}

/// Boot order key: first everything started during boot, then everything started later,
/// then everything that never started; by start time within each part.
struct BootOrderEntry {
    int part = 0;
    std::uint64_t startedUsec = 0;
    const Unit* unit = nullptr;
};

bool bootOrderEntryBefore(const BootOrderEntry& left, const BootOrderEntry& right) {
    if (left.part != right.part) {
        return left.part < right.part;
    }
    if (left.startedUsec != right.startedUsec) {
        return left.startedUsec < right.startedUsec;
    }
    return left.unit->key < right.unit->key;
}

std::vector<const Unit*> unitsInBootOrder(const UnitGraph& graph) {
    std::vector<BootOrderEntry> entries;
    for (const auto& [key, unit] : graph.allUnits()) {
        BootOrderEntry entry;
        entry.unit = &unit;
        entry.startedUsec = unit.activatingUsec;
        if (unit.activatingUsec == 0) {
            entry.part = 2;
        } else if (graph.startedAfterBoot(unit)) {
            entry.part = 1;
        }
        entries.push_back(entry);
    }
    std::sort(entries.begin(), entries.end(), bootOrderEntryBefore);

    std::vector<const Unit*> units;
    for (const BootOrderEntry& entry : entries) {
        units.push_back(entry.unit);
    }
    return units;
}

std::string parentPathOf(const std::string& path) {
    std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? "" : path.substr(0, slash);
}

} // namespace

std::string toString(View view) {
    switch (view) {
    case View::Tree:
        return "Tree";
    case View::Dependents:
        return "Dependents";
    case View::Boot:
        return "Boot";
    case View::Slowest:
        return "Slowest";
    case View::Problems:
        return "Problems";
    }
    return "Unknown";
}

std::vector<View> allViews() {
    return {View::Tree, View::Dependents, View::Boot, View::Slowest, View::Problems};
}

std::string toString(ListOrder order) {
    switch (order) {
    case ListOrder::FailedFirst:
        return "name";
    case ListOrder::SlowestStartup:
        return "slowest";
    case ListOrder::BootOrder:
        return "boot order";
    }
    return "unknown";
}

std::string toString(TreeDirection direction) {
    switch (direction) {
    case TreeDirection::Forward:
        return "forward";
    case TreeDirection::Reverse:
        return "reverse";
    }
    return "unknown";
}

void TreeView::setGraph(const UnitGraph* graph) {
    graph_ = graph;
    computeGroups();
    refreshSearchExpansion();

    // Open the two manager roots on first load so the tree is useful right away.
    if (!hasExpandedInitialRoots_ && graph_ != nullptr) {
        for (const TreeNode& root : defaultForwardRoots()) {
            if (!isGroup(root)) {
                forwardExpanded_.insert(root.id);
            }
        }
        hasExpandedInitialRoots_ = true;
    }
    rebuildRows();
}

void TreeView::setFilters(const FilterState& filters) {
    FilterState effective = filters;
    effective.problemsOnly = view_ == View::Problems;
    if (effective == filters_) {
        return;
    }
    bool searchChanged = effective.search != filters_.search;
    filters_ = effective;
    refreshSearchExpansion();
    rebuildRows();
    if (searchChanged && !filters_.search.empty()) {
        moveCursorToFirstMatch();
    }
}

const std::vector<Row>& TreeView::rows() const {
    return rows_;
}

const Row* TreeView::selectedRow() const {
    if (cursor_ < 0 || cursor_ >= static_cast<int>(rows_.size())) {
        return nullptr;
    }
    return &rows_[cursor_];
}

TreeDirection TreeView::direction() const {
    return view_ == View::Tree ? TreeDirection::Forward : TreeDirection::Reverse;
}

std::string TreeView::focusedUnitKey() const {
    return focusStack_.empty() ? "" : focusStack_.back().unitKey;
}

void TreeView::moveCursor(int delta) {
    if (rows_.empty()) {
        return;
    }
    cursor_ = std::clamp(cursor_ + delta, 0, static_cast<int>(rows_.size()) - 1);
}

void TreeView::moveCursorToStart() {
    cursor_ = 0;
}

void TreeView::moveCursorToEnd() {
    cursor_ = rows_.empty() ? 0 : static_cast<int>(rows_.size()) - 1;
}

void TreeView::expandSelected() {
    const Row* row = selectedRow();
    if (row == nullptr || !row->expandable) {
        return;
    }
    if (row->expanded) {
        moveCursor(1);
        return;
    }
    expandedPaths().insert(row->path);
    rebuildRows();
}

void TreeView::collapseSelected() {
    const Row* row = selectedRow();
    if (row == nullptr) {
        return;
    }
    if (row->expanded) {
        collapsePath(row->path);
        rebuildRows();
        return;
    }

    std::string parentPath = parentPathOf(row->path);
    for (int index = cursor_ - 1; index >= 0; --index) {
        if (rows_[index].path == parentPath) {
            cursor_ = index;
            return;
        }
    }
}

void TreeView::toggleSelected() {
    const Row* row = selectedRow();
    if (row == nullptr || !row->expandable) {
        return;
    }
    if (row->expanded) {
        collapsePath(row->path);
    } else {
        expandedPaths().insert(row->path);
    }
    rebuildRows();
}

void TreeView::setView(View view) {
    const Row* before = selectedRow();
    std::string unitBefore = before == nullptr ? "" : before->unitKey;
    view_ = view;
    filters_.problemsOnly = view_ == View::Problems;
    std::string focused = focusedUnitKey();
    if (!focused.empty()) {
        expandedPaths().insert(focused);
    }
    refreshSearchExpansion();
    rebuildRows();
    // The unit may not be in this tab's list; then start at the top instead of wherever
    // the old row index happens to land.
    const Row* after = selectedRow();
    if (after == nullptr || after->unitKey != unitBefore) {
        moveCursorToStart();
    }
}

View TreeView::view() const {
    return view_;
}

ListOrder TreeView::listOrder() const {
    switch (view_) {
    case View::Boot:
        return ListOrder::BootOrder;
    case View::Slowest:
        return ListOrder::SlowestStartup;
    case View::Tree:
    case View::Dependents:
    case View::Problems:
        break;
    }
    return ListOrder::FailedFirst;
}

void TreeView::focusSelected() {
    const Row* row = selectedRow();
    if (row == nullptr || row->unitKey.empty()) {
        return;
    }
    focusStack_.push_back(FocusEntry{row->unitKey, row->path});
    // The focus root's path is just its key, in either direction.
    forwardExpanded_.insert(row->unitKey);
    reverseExpanded_.insert(row->unitKey);
    std::string newRootPath = row->unitKey;
    refreshSearchExpansion();
    rebuildRows(newRootPath);
}

void TreeView::goBack() {
    if (focusStack_.empty()) {
        return;
    }
    std::string returnPath = focusStack_.back().returnPath;
    focusStack_.pop_back();
    refreshSearchExpansion();
    rebuildRows(returnPath);
}

std::vector<TreeNode> TreeView::topLevelNodes() const {
    if (graph_ == nullptr) {
        return {};
    }
    std::string focused = focusedUnitKey();
    if (!focused.empty()) {
        return {makeUnitNode(focused, "")};
    }
    if (direction() == TreeDirection::Forward) {
        return defaultForwardRoots();
    }
    return reverseTopLevel();
}

std::vector<TreeNode> TreeView::defaultForwardRoots() const {
    std::vector<TreeNode> roots;
    const Unit* systemRoot = graph_->find(kSystemRootKey);
    if (systemRoot != nullptr) {
        roots.push_back(makeUnitNode(systemRoot->key, "System"));
    }
    const Unit* userRoot = graph_->find(kUserRootKey);
    if (userRoot != nullptr) {
        roots.push_back(makeUnitNode(userRoot->key, "User (" + userRoot->runAsUser + ")"));
    }
    if (!unreachableKeys_.empty()) {
        roots.push_back(makeGroupNode(kUnreachableGroupId, "Not reachable from default.target"));
    }
    if (!notLoadedKeys_.empty()) {
        roots.push_back(makeGroupNode(kNotLoadedGroupId, "Not loaded"));
    }
    return roots;
}

std::vector<TreeNode> TreeView::reverseTopLevel() const {
    std::vector<const Unit*> units;
    if (listOrder() == ListOrder::BootOrder) {
        units = unitsInBootOrder(*graph_);
    } else {
        for (const auto& [key, unit] : graph_->allUnits()) {
            units.push_back(&unit);
        }
        std::sort(units.begin(), units.end(),
                  listOrder() == ListOrder::SlowestStartup ? slowestStartupFirst
                                                           : failedFirstThenByName);
    }

    std::vector<TreeNode> nodes;
    for (const Unit* unit : units) {
        nodes.push_back(makeUnitNode(unit->key, ""));
    }
    return nodes;
}

std::vector<TreeNode> TreeView::childrenOf(const TreeNode& node) const {
    if (!isGroup(node)) {
        return unitChildren(node.unitKey);
    }
    const std::vector<std::string>& members =
        node.id == kUnreachableGroupId ? unreachableKeys_ : notLoadedKeys_;
    std::vector<TreeNode> children;
    for (const std::string& key : members) {
        children.push_back(makeUnitNode(key, ""));
    }
    return children;
}

std::vector<TreeNode> TreeView::unitChildren(const std::string& unitKey) const {
    std::vector<Edge> edges = direction() == TreeDirection::Forward
                                  ? graph_->dependenciesOf(unitKey)
                                  : graph_->dependentsOf(unitKey);
    std::vector<TreeNode> candidates;
    for (const Edge& edge : edges) {
        TreeNode child = makeUnitNode(canonicalKey(*graph_, edge.target), "");
        child.hasEdgeKind = true;
        child.edgeKind = edge.kind;
        candidates.push_back(child);
    }
    std::sort(candidates.begin(), candidates.end(), childComesBefore);

    // A unit can list the same target under several properties; show it once.
    std::vector<TreeNode> children;
    std::set<std::string> seen;
    for (const TreeNode& child : candidates) {
        if (seen.insert(child.id).second) {
            children.push_back(child);
        }
    }
    return children;
}

bool TreeView::hasChildren(const TreeNode& node) const {
    if (isGroup(node)) {
        return node.id == kUnreachableGroupId ? !unreachableKeys_.empty() : !notLoadedKeys_.empty();
    }
    if (direction() == TreeDirection::Forward) {
        return !graph_->dependenciesOf(node.unitKey).empty();
    }
    return !graph_->dependentsOf(node.unitKey).empty();
}

void TreeView::computeGroups() {
    unreachableKeys_.clear();
    notLoadedKeys_.clear();
    if (graph_ == nullptr) {
        return;
    }

    std::set<std::string> reached;
    std::vector<std::string> pending;
    for (const char* rootKey : {kSystemRootKey, kUserRootKey}) {
        if (graph_->find(rootKey) != nullptr) {
            pending.push_back(canonicalKey(*graph_, rootKey));
        }
    }
    while (!pending.empty()) {
        std::string key = pending.back();
        pending.pop_back();
        if (!reached.insert(key).second) {
            continue;
        }
        for (const Edge& edge : graph_->dependenciesOf(key)) {
            pending.push_back(canonicalKey(*graph_, edge.target));
        }
    }

    for (const auto& [key, unit] : graph_->allUnits()) {
        if (unit.isLoaded && reached.count(key) == 0) {
            unreachableKeys_.push_back(key);
        }
        // Missing placeholders already show up under whoever references them.
        if (!unit.isLoaded && unit.origin != Origin::Missing) {
            notLoadedKeys_.push_back(key);
        }
    }
}

void TreeView::rebuildRows(const std::string& preferredPath) {
    std::string previousPath = preferredPath;
    std::string previousUnitKey;
    const Row* previous = selectedRow();
    if (previous != nullptr) {
        previousUnitKey = previous->unitKey;
        if (previousPath.empty()) {
            previousPath = previous->path;
        }
    }

    rows_.clear();
    std::set<std::string> ancestors;
    // Roots, groups, and the focus root stay visible as landmarks; the reverse
    // top level is a plain list of units, so it is filtered like everything else.
    bool alwaysShowTopLevel = direction() == TreeDirection::Forward || !focusedUnitKey().empty();
    for (const TreeNode& node : topLevelNodes()) {
        appendNode(node, 0, "", ancestors, alwaysShowTopLevel);
    }
    restoreCursor(previousPath, previousUnitKey);
}

bool TreeView::appendNode(const TreeNode& node, int depth, const std::string& parentPath,
                          std::set<std::string>& ancestors, bool alwaysShow) {
    Row row;
    row.path = parentPath.empty() ? node.id : parentPath + "/" + node.id;
    row.depth = depth;
    row.unitKey = node.unitKey;
    row.label = node.label;
    row.hasEdgeKind = node.hasEdgeKind;
    row.edgeKind = node.edgeKind;
    row.isCycle = !isGroup(node) && ancestors.count(node.unitKey) > 0;
    row.expandable = !row.isCycle && hasChildren(node);
    row.expanded = row.expandable && isExpanded(row.path);
    if (isGroup(node)) {
        row.groupTotal = static_cast<int>(childrenOf(node).size());
        row.groupSize = countPassingMembers(node);
    }
    std::size_t rowIndex = rows_.size();
    rows_.push_back(row);

    // Children that don't pass remove themselves, so anything left behind was kept.
    if (row.expanded) {
        ancestors.insert(node.unitKey);
        for (const TreeNode& child : childrenOf(node)) {
            appendNode(child, depth + 1, row.path, ancestors, false);
        }
        ancestors.erase(node.unitKey);
    }
    bool hasVisibleDescendant = rows_.size() > rowIndex + 1;

    bool passes = nodePassesFilters(node);
    if (!passes && !hasVisibleDescendant && !alwaysShow) {
        rows_.resize(rowIndex);
        return false;
    }
    rows_[rowIndex].isContextOnly = !passes && !isGroup(node);
    return true;
}

bool TreeView::nodePassesFilters(const TreeNode& node) const {
    if (isGroup(node)) {
        return false;
    }
    const Unit* unit = graph_->find(node.unitKey);
    return unit != nullptr && unitPassesFilters(*unit, filters_, *graph_);
}

int TreeView::countPassingMembers(const TreeNode& group) const {
    int passing = 0;
    for (const TreeNode& member : childrenOf(group)) {
        if (nodePassesFilters(member)) {
            ++passing;
        }
    }
    return passing;
}

// Prefer the exact same row, then the same unit anywhere (e.g. after a direction
// flip), then stay at roughly the same height.
void TreeView::restoreCursor(const std::string& path, const std::string& unitKey) {
    for (int index = 0; index < static_cast<int>(rows_.size()); ++index) {
        if (rows_[index].path == path) {
            cursor_ = index;
            return;
        }
    }
    for (int index = 0; index < static_cast<int>(rows_.size()); ++index) {
        if (!unitKey.empty() && rows_[index].unitKey == unitKey) {
            cursor_ = index;
            return;
        }
    }
    moveCursor(0);
}

// Roots are walked before the groups, so a unit that a default.target reaches is always
// revealed there rather than through some unreachable unit that happens to be closer.
std::map<std::string, std::string> TreeView::firstPathToEachUnit() const {
    std::vector<std::pair<std::string, std::string>> rootSeeds;
    std::vector<std::pair<std::string, std::string>> groupSeeds;
    for (const TreeNode& top : topLevelNodes()) {
        if (!isGroup(top)) {
            rootSeeds.push_back({top.unitKey, top.id});
            continue;
        }
        for (const TreeNode& member : childrenOf(top)) {
            groupSeeds.push_back({member.unitKey, top.id + "/" + member.id});
        }
    }

    std::map<std::string, std::string> pathOf;
    walkBreadthFirst(rootSeeds, pathOf);
    walkBreadthFirst(groupSeeds, pathOf);
    return pathOf;
}

void TreeView::walkBreadthFirst(const std::vector<std::pair<std::string, std::string>>& seeds,
                                std::map<std::string, std::string>& pathOf) const {
    std::deque<std::string> pending;
    for (const auto& [key, path] : seeds) {
        if (pathOf.count(key) == 0) {
            pathOf[key] = path;
            pending.push_back(key);
        }
    }
    while (!pending.empty()) {
        std::string key = pending.front();
        pending.pop_front();
        for (const TreeNode& child : unitChildren(key)) {
            if (pathOf.count(child.unitKey) == 0) {
                pathOf[child.unitKey] = pathOf[key] + "/" + child.id;
                pending.push_back(child.unitKey);
            }
        }
    }
}

// Only the forward tree needs this: the reverse top level already lists every match.
void TreeView::refreshSearchExpansion() {
    searchExpanded_.clear();
    if (graph_ == nullptr || filters_.search.empty() || direction() != TreeDirection::Forward) {
        return;
    }
    for (const auto& [key, path] : firstPathToEachUnit()) {
        const Unit* unit = graph_->find(key);
        if (unit == nullptr || !unitPassesFilters(*unit, filters_, *graph_)) {
            continue;
        }
        // Open every ancestor of the match, not the match itself.
        for (std::size_t slash = path.find('/'); slash != std::string::npos;
             slash = path.find('/', slash + 1)) {
            searchExpanded_.insert(path.substr(0, slash));
        }
    }
}

bool TreeView::isExpanded(const std::string& path) const {
    bool openedBySearch = direction() == TreeDirection::Forward && searchExpanded_.count(path) > 0;
    return openedBySearch || expandedPaths().count(path) > 0;
}

void TreeView::collapsePath(const std::string& path) {
    expandedPaths().erase(path);
    searchExpanded_.erase(path);
}

void TreeView::moveCursorToFirstMatch() {
    for (int index = 0; index < static_cast<int>(rows_.size()); ++index) {
        const Row& row = rows_[index];
        if (!row.unitKey.empty() && !row.isContextOnly) {
            cursor_ = index;
            return;
        }
    }
}

std::set<std::string>& TreeView::expandedPaths() {
    return direction() == TreeDirection::Forward ? forwardExpanded_ : reverseExpanded_;
}

const std::set<std::string>& TreeView::expandedPaths() const {
    return direction() == TreeDirection::Forward ? forwardExpanded_ : reverseExpanded_;
}

namespace {

std::string reverseEdgeLabel(EdgeKind kind) {
    switch (kind) {
    case EdgeKind::Requires:
        return "required-by";
    case EdgeKind::Requisite:
        return "requisite-of";
    case EdgeKind::Wants:
        return "wanted-by";
    case EdgeKind::BindsTo:
        return "bound-by";
    case EdgeKind::PartOf:
        return "has-part";
    case EdgeKind::Upholds:
        return "upheld-by";
    case EdgeKind::Triggers:
        return "triggered-by";
    }
    return "";
}

ftxui::Element stateIcon(const Unit& unit) {
    using namespace ftxui;
    if (unit.origin == Origin::Missing || unit.loadState == "not-found") {
        return text("?") | color(Color::Magenta);
    }
    if (isMasked(unit)) {
        return text("⊘") | color(Color::GrayLight);
    }
    switch (unit.activeState) {
    case ActiveState::Active:
        return text("●") | color(Color::Green);
    case ActiveState::Failed:
        return text("✗") | color(Color::Red) | bold;
    case ActiveState::Activating:
    case ActiveState::Deactivating:
    case ActiveState::Reloading:
        return text("◐") | color(Color::Yellow);
    case ActiveState::Inactive:
    case ActiveState::Unknown:
        break;
    }
    return text("○") | dim;
}

ftxui::Element originTag(const Unit& unit) {
    using namespace ftxui;
    switch (unit.origin) {
    case Origin::SystemdDefault:
        return text("[sys]") | color(Color::Blue);
    case Origin::Package:
        return text("[pkg:" + unit.package + "]") | color(Color::Cyan);
    case Origin::AdminCreated:
        return text("[admin]") | color(Color::Yellow);
    case Origin::UserCreated:
        return text("[user]") | color(Color::Green);
    case Origin::Generated:
        return text("[gen]") | dim;
    case Origin::Transient:
        return text("[transient]") | dim;
    case Origin::Unowned:
        return text("[unowned]") | color(Color::Red);
    case Origin::Missing:
        return text("[missing]") | color(Color::Magenta);
    case Origin::Unknown:
        break;
    }
    return text("");
}

std::string expandArrow(const Row& row) {
    if (row.isCycle) {
        return "↻";
    }
    if (!row.expandable) {
        return " ";
    }
    return row.expanded ? "▾" : "▸";
}

std::string runsAsSuffix(const Unit& unit) {
    bool nonRootService = unit.manager == Manager::System && unit.type == "service" &&
                          !unit.runAsUser.empty() && unit.runAsUser != "root";
    return nonRootService ? " (as " + unit.runAsUser + ")" : "";
}

ftxui::Element renderGroupRow(const Row& row, ftxui::Elements left) {
    using namespace ftxui;
    std::string count = std::to_string(row.groupSize);
    if (row.groupSize != row.groupTotal) {
        count += " of " + std::to_string(row.groupTotal);
    }
    left.push_back(text(row.label) | bold);
    left.push_back(text(" (" + count + ")") | dim);
    return hbox(left);
}

/// Timing shown on the right of a row, depending on the list order.
ftxui::Elements timingColumn(const Unit& unit, const UnitGraph& graph, ListOrder order) {
    using namespace ftxui;
    std::uint64_t startup = startupDurationUsec(unit);
    if (order == ListOrder::SlowestStartup && startup > 0) {
        return {text(formatDuration(startup) + "  ") | color(Color::Yellow)};
    }
    if (order != ListOrder::BootOrder || unit.activatingUsec == 0) {
        return {};
    }
    Elements timing = {text("+" + formatDuration(unit.activatingUsec)) | color(Color::Cyan)};
    // Sub-millisecond starts (sockets, targets) would only show as "(0.000s)".
    if (startup >= 1000) {
        timing.push_back(text(" (" + formatDuration(startup) + ")") | dim);
    }
    if (graph.startedAfterBoot(unit)) {
        timing.push_back(text(" after boot") | color(Color::Yellow));
    }
    timing.push_back(text("  "));
    return timing;
}

ftxui::Element renderUnitRow(const Row& row, const Unit& unit, const UnitGraph& graph,
                             TreeDirection direction, ListOrder order, ftxui::Elements left) {
    using namespace ftxui;
    left.push_back(stateIcon(unit));
    left.push_back(text(" "));
    if (row.label.empty()) {
        left.push_back(text(unit.name + runsAsSuffix(unit)));
    } else {
        left.push_back(text(row.label) | bold);
        left.push_back(text("  " + unit.name) | dim);
    }
    if (row.hasEdgeKind) {
        std::string edge = direction == TreeDirection::Forward ? toString(row.edgeKind)
                                                               : reverseEdgeLabel(row.edgeKind);
        left.push_back(text("   " + edge) | dim);
    }

    Elements right = timingColumn(unit, graph, order);
    right.push_back(originTag(unit));
    if (hasWarning(unit)) {
        right.push_back(text(" ⚠") | color(Color::Yellow) | bold);
    }
    if (unit.locallyModified) {
        right.push_back(text(" ⚙") | color(Color::Yellow));
    }
    if (unit.shadowsPackagedUnit) {
        right.push_back(text(" ⇪") | color(Color::Yellow));
    }
    right.push_back(text(" "));
    return hbox({hbox(left), filler(), hbox(right)});
}

ftxui::Element renderRow(const Row& row, const UnitGraph& graph, TreeDirection direction,
                         ListOrder order) {
    using namespace ftxui;
    Elements left = {text(std::string(static_cast<std::size_t>(row.depth) * 2, ' ')),
                     text(expandArrow(row) + " ")};
    if (row.unitKey.empty()) {
        return renderGroupRow(row, left);
    }
    const Unit* unit = graph.find(row.unitKey);
    if (unit == nullptr) {
        left.push_back(text(row.unitKey) | color(Color::Red));
        return hbox(left);
    }
    return renderUnitRow(row, *unit, graph, direction, order, left);
}

} // namespace

ftxui::Element TreeView::render() const {
    using namespace ftxui;
    if (graph_ == nullptr) {
        return text("loading units...") | dim | center | flex;
    }
    if (rows_.empty()) {
        return text("no units match the filters") | dim | center | flex;
    }

    rowBoxes_.assign(rows_.size(), Box{});
    Elements lines;
    for (int index = 0; index < static_cast<int>(rows_.size()); ++index) {
        Element line =
            renderRow(rows_[index], *graph_, direction(), listOrder()) | reflect(rowBoxes_[index]);
        if (rows_[index].isContextOnly) {
            line = line | dim;
        }
        if (index == cursor_) {
            line = line | inverted | focus;
        }
        lines.push_back(line);
    }
    return vbox(lines) | vscroll_indicator | yframe | reflect(frameBox_) | flex;
}

bool TreeView::handleMouse(const ftxui::Mouse& mouse) {
    using ftxui::Mouse;
    if (!frameBox_.Contain(mouse.x, mouse.y)) {
        return false;
    }
    if (mouse.button == Mouse::WheelUp || mouse.button == Mouse::WheelDown) {
        moveCursor(mouse.button == Mouse::WheelUp ? -kWheelStep : kWheelStep);
        return true;
    }
    if (mouse.button != Mouse::Left || mouse.motion != Mouse::Pressed) {
        return false;
    }

    // Rows scrolled out of view still have boxes, but outside frameBox_, so the
    // Contain check above keeps clicks from reaching them.
    int rowCount = static_cast<int>(std::min(rows_.size(), rowBoxes_.size()));
    for (int index = 0; index < rowCount; ++index) {
        const ftxui::Box& box = rowBoxes_[index];
        if (!box.Contain(mouse.x, mouse.y)) {
            continue;
        }
        int arrowColumn = box.x_min + rows_[index].depth * 2;
        bool clickedArrow = mouse.x >= arrowColumn && mouse.x <= arrowColumn + 1;
        bool wasSelected = index == cursor_;
        cursor_ = index;
        if (clickedArrow || wasSelected) {
            toggleSelected();
        }
        return true;
    }
    return false;
}

} // namespace nyst
