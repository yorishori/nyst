// Tree expansion state, flattening into visible rows, cursor, and row rendering.
#include "ui/tree_view.hpp"

#include <algorithm>

namespace nyst {

namespace {

const char* const kUnreachableGroupId = "group:unreachable";
const char* const kNotLoadedGroupId = "group:not-loaded";
const char* const kSystemRootKey = "system:default.target";
const char* const kUserRootKey = "user:default.target";

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

std::string parentPathOf(const std::string& path) {
    std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? "" : path.substr(0, slash);
}

} // namespace

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
    return direction_;
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
        expandedPaths().erase(row->path);
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
        expandedPaths().erase(row->path);
    } else {
        expandedPaths().insert(row->path);
    }
    rebuildRows();
}

void TreeView::toggleDirection() {
    direction_ =
        direction_ == TreeDirection::Forward ? TreeDirection::Reverse : TreeDirection::Forward;
    std::string focused = focusedUnitKey();
    if (!focused.empty()) {
        expandedPaths().insert(focused);
    }
    rebuildRows();
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
    rebuildRows(row->unitKey);
}

void TreeView::goBack() {
    if (focusStack_.empty()) {
        return;
    }
    std::string returnPath = focusStack_.back().returnPath;
    focusStack_.pop_back();
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
    if (direction_ == TreeDirection::Forward) {
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
    for (const auto& [key, unit] : graph_->allUnits()) {
        units.push_back(&unit);
    }
    std::sort(units.begin(), units.end(), failedFirstThenByName);

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
    std::vector<Edge> edges = direction_ == TreeDirection::Forward ? graph_->dependenciesOf(unitKey)
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
    if (direction_ == TreeDirection::Forward) {
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
    for (const TreeNode& node : topLevelNodes()) {
        appendNode(node, 0, "", ancestors);
    }
    restoreCursor(previousPath, previousUnitKey);
}

void TreeView::appendNode(const TreeNode& node, int depth, const std::string& parentPath,
                          std::set<std::string>& ancestors) {
    Row row;
    row.path = parentPath.empty() ? node.id : parentPath + "/" + node.id;
    row.depth = depth;
    row.unitKey = node.unitKey;
    row.label = node.label;
    row.hasEdgeKind = node.hasEdgeKind;
    row.edgeKind = node.edgeKind;
    row.isCycle = !isGroup(node) && ancestors.count(node.unitKey) > 0;
    row.expandable = !row.isCycle && hasChildren(node);
    row.expanded = row.expandable && expandedPaths().count(row.path) > 0;
    if (isGroup(node)) {
        row.groupSize = static_cast<int>(childrenOf(node).size());
    }
    rows_.push_back(row);

    if (!row.expanded) {
        return;
    }
    ancestors.insert(node.unitKey);
    for (const TreeNode& child : childrenOf(node)) {
        appendNode(child, depth + 1, row.path, ancestors);
    }
    ancestors.erase(node.unitKey);
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

std::set<std::string>& TreeView::expandedPaths() {
    return direction_ == TreeDirection::Forward ? forwardExpanded_ : reverseExpanded_;
}

const std::set<std::string>& TreeView::expandedPaths() const {
    return direction_ == TreeDirection::Forward ? forwardExpanded_ : reverseExpanded_;
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
    left.push_back(text(row.label) | bold);
    left.push_back(text(" (" + std::to_string(row.groupSize) + ")") | dim);
    return hbox(left);
}

ftxui::Element renderUnitRow(const Row& row, const Unit& unit, TreeDirection direction,
                             ftxui::Elements left) {
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

    Elements right = {originTag(unit)};
    if (unit.locallyModified) {
        right.push_back(text(" ⚙") | color(Color::Yellow));
    }
    if (unit.shadowsPackagedUnit) {
        right.push_back(text(" ⇪") | color(Color::Yellow));
    }
    right.push_back(text(" "));
    return hbox({hbox(left), filler(), hbox(right)});
}

ftxui::Element renderRow(const Row& row, const UnitGraph& graph, TreeDirection direction) {
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
    return renderUnitRow(row, *unit, direction, left);
}

} // namespace

ftxui::Element TreeView::render() const {
    using namespace ftxui;
    if (graph_ == nullptr || rows_.empty()) {
        return text("no units") | dim | center | flex;
    }

    Elements lines;
    for (int index = 0; index < static_cast<int>(rows_.size()); ++index) {
        Element line = renderRow(rows_[index], *graph_, direction_);
        if (index == cursor_) {
            line = line | inverted | focus;
        }
        lines.push_back(line);
    }
    return vbox(lines) | vscroll_indicator | yframe | flex;
}

} // namespace nyst
