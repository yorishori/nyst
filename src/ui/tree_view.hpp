// Tree expansion state, flattening into visible rows, cursor, and row rendering.
#pragma once

#include "model/unit_graph.hpp"
#include "ui/filters.hpp"

#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

#include <map>
#include <set>
#include <string>
#include <vector>

namespace nyst {

enum class TreeDirection { Forward, Reverse };

std::string toString(TreeDirection direction);

/// One visible line of the tree. Rebuilt whenever the tree changes.
struct Row {
    std::string path; // "/"-joined node ids from the top level down; unique per row
    int depth = 0;
    std::string unitKey; // empty for synthetic group rows
    std::string label;   // shown instead of the unit name when non-empty (roots, groups)
    bool hasEdgeKind = false;
    EdgeKind edgeKind = EdgeKind::Wants; // relation to the parent row
    bool expandable = false;
    bool expanded = false;
    bool isCycle = false;       // unit already appears among this row's ancestors
    bool isContextOnly = false; // fails the filters; shown only because a descendant passes
    int groupSize = 0;          // members that pass the filters, for group rows
    int groupTotal = 0;         // all members, for group rows
};

/// A node before it becomes a Row: either a unit or a synthetic group.
struct TreeNode {
    std::string id;      // path segment: the unit key, or "group:<name>"
    std::string unitKey; // empty for groups
    std::string label;
    bool hasEdgeKind = false;
    EdgeKind edgeKind = EdgeKind::Wants;
};

class TreeView {
public:
    /// Points the view at a (re)loaded graph. The graph must outlive the view or the next call.
    void setGraph(const UnitGraph* graph);

    /// Applies new filters. Cheap to call every frame: does nothing if they are unchanged.
    /// A new search in the forward tree expands the way to every match and moves the
    /// cursor to the first one.
    void setFilters(const FilterState& filters);

    const std::vector<Row>& rows() const;
    const Row* selectedRow() const;
    TreeDirection direction() const;

    /// Unit key of the current focus root, or empty when showing the default roots.
    std::string focusedUnitKey() const;

    void moveCursor(int delta);
    void moveCursorToStart();
    void moveCursorToEnd();

    /// Expands the selected row, or steps into it if it is already expanded.
    void expandSelected();
    /// Collapses the selected row, or jumps to its parent if it is already collapsed.
    void collapseSelected();
    void toggleSelected();

    void toggleDirection();

    /// Switches the reverse list between "failed first, then by name" and "slowest startup
    /// first" (like systemd-analyze blame). Turning it on also switches to the reverse list.
    void toggleStartupSort();
    bool sortsByStartupTime() const;
    /// Re-roots the tree on the selected unit (Enter).
    void focusSelected();
    /// Returns to the previous root (Backspace).
    void goBack();

    ftxui::Element render() const;

    /// Wheel scrolls; a click selects a row, and a click on its arrow or on the
    /// already-selected row toggles it. Returns false if the mouse is outside the tree.
    bool handleMouse(const ftxui::Mouse& mouse);

private:
    struct FocusEntry {
        std::string unitKey;
        std::string returnPath; // cursor path to restore when leaving this focus
    };

    std::vector<TreeNode> topLevelNodes() const;
    std::vector<TreeNode> defaultForwardRoots() const;
    std::vector<TreeNode> reverseTopLevel() const;
    std::vector<TreeNode> childrenOf(const TreeNode& node) const;
    std::vector<TreeNode> unitChildren(const std::string& unitKey) const;
    bool hasChildren(const TreeNode& node) const;

    void computeGroups();
    std::map<std::string, std::string> firstPathToEachUnit() const;
    /// Breadth-first walk from the seeds (unit key, path), recording the first path found
    /// to every unit not already in pathOf.
    void walkBreadthFirst(const std::vector<std::pair<std::string, std::string>>& seeds,
                          std::map<std::string, std::string>& pathOf) const;
    void refreshSearchExpansion();
    bool isExpanded(const std::string& path) const;
    void collapsePath(const std::string& path);
    void moveCursorToFirstMatch();
    /// Re-flattens the tree. Keeps the cursor on preferredPath, or on the current row if empty.
    void rebuildRows(const std::string& preferredPath = "");
    /// Appends the node and its visible descendants. Returns false (and appends nothing)
    /// if neither the node nor anything below it passes the filters, unless alwaysShow.
    bool appendNode(const TreeNode& node, int depth, const std::string& parentPath,
                    std::set<std::string>& ancestors, bool alwaysShow);
    bool nodePassesFilters(const TreeNode& node) const;
    int countPassingMembers(const TreeNode& group) const;
    void restoreCursor(const std::string& path, const std::string& unitKey);

    std::set<std::string>& expandedPaths();
    const std::set<std::string>& expandedPaths() const;

    const UnitGraph* graph_ = nullptr;
    TreeDirection direction_ = TreeDirection::Forward;
    bool sortByStartupTime_ = false; // only meaningful in the reverse direction
    FilterState filters_ = defaultFilters();
    std::set<std::string> forwardExpanded_;
    std::set<std::string> reverseExpanded_;
    // Opened automatically to reveal search matches; kept apart from the user's own
    // expansion so clearing the search restores the tree as it was.
    std::set<std::string> searchExpanded_;
    std::vector<FocusEntry> focusStack_;
    std::vector<std::string> unreachableKeys_;
    std::vector<std::string> notLoadedKeys_;
    std::vector<Row> rows_;
    int cursor_ = 0;
    // Filled in by the renderer during layout; mutable because render() is const.
    mutable ftxui::Box frameBox_;
    mutable std::vector<ftxui::Box> rowBoxes_;
    bool hasExpandedInitialRoots_ = false;
};

} // namespace nyst
