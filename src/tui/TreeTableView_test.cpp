// SPDX-License-Identifier: Apache-2.0
#include <tui/TreeTableView.hpp>

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <vector>

using namespace tui;

namespace
{
/// A tiny in-memory tree model for exercising the generic view without any domain code.
///
/// Nodes are ints; children are stored in a map. The "current node" plus a parent stack
/// model navigation. Sorting just reverses the child order so re-sorts are observable.
class FakeModel: public TreeTableModel
{
  public:
    FakeModel()
    {
        _children[0] = { 1, 2, 3 }; // root's children
        _children[2] = { 4, 5 };    // node 2 is a directory
        _names = { { 0, "root" }, { 1, "alpha" }, { 2, "dir" }, { 3, "beta" }, { 4, "x" }, { 5, "y" } };
    }

    [[nodiscard]] std::vector<TableColumn> columns() const override
    {
        return { TableColumn {
                     .header = "Name", .width = ColumnWidth::Flex, .size = 4, .align = ColumnAlign::Left },
                 TableColumn {
                     .header = "Id", .width = ColumnWidth::Fixed, .size = 3, .align = ColumnAlign::Right } };
    }

    [[nodiscard]] std::vector<RowId> rows() const override
    {
        std::vector<RowId> out;
        for (auto const id: _children.at(_current))
            out.push_back(static_cast<RowId>(id));
        return out;
    }

    [[nodiscard]] std::string cellText(RowId row, std::size_t column) const override
    {
        if (column == 0)
            return _names.at(static_cast<int>(row));
        return std::to_string(row);
    }

    [[nodiscard]] RowStyle rowStyle(RowId, bool selected) const override
    {
        return RowStyle { .style = {}, .selected = selected };
    }

    [[nodiscard]] bool canDescend(RowId row) const override
    {
        return _children.contains(static_cast<int>(row));
    }

    bool descend(RowId row) override
    {
        if (!canDescend(row))
            return false;
        _stack.push_back(_current);
        _current = static_cast<int>(row);
        return true;
    }

    bool ascend() override
    {
        if (_stack.empty())
            return false;
        _current = _stack.back();
        _stack.pop_back();
        return true;
    }

    [[nodiscard]] std::string currentTitle() const override { return _names.at(_current); }

    void sortBy(int) override
    {
        auto& kids = _children[_current];
        std::ranges::reverse(kids);
    }

    [[nodiscard]] int current() const { return _current; }

  private:
    std::map<int, std::vector<int>> _children;
    std::map<int, std::string> _names;
    std::vector<int> _stack;
    int _current = 0;
};
} // namespace

TEST_CASE("TreeTableView: cursor moves within row bounds", "[treetable]")
{
    FakeModel model;
    TreeTableView view(model);
    view.setArea(Rect { .x = 0, .y = 0, .width = 20, .height = 10 });

    CHECK(view.cursorIndex() == 0);
    view.moveCursor(1);
    CHECK(view.cursorIndex() == 1);
    view.moveCursor(100); // clamps to last (3 rows -> index 2)
    CHECK(view.cursorIndex() == 2);
    view.moveCursor(-100); // clamps to first
    CHECK(view.cursorIndex() == 0);
}

TEST_CASE("TreeTableView: moveToTop/Bottom", "[treetable]")
{
    FakeModel model;
    TreeTableView view(model);
    view.setArea(Rect { .x = 0, .y = 0, .width = 20, .height = 10 });

    view.moveToBottom();
    CHECK(view.cursorIndex() == 2);
    view.moveToTop();
    CHECK(view.cursorIndex() == 0);
}

TEST_CASE("TreeTableView: descend into a directory row resets cursor", "[treetable]")
{
    FakeModel model;
    TreeTableView view(model);
    view.setArea(Rect { .x = 0, .y = 0, .width = 20, .height = 10 });

    // Move to node 2 ("dir", index 1) and descend.
    view.moveCursor(1);
    bool valid = false;
    CHECK(view.cursorRow(valid) == 2);
    view.descendCursor();
    CHECK(model.current() == 2);
    CHECK(view.cursorIndex() == 0); // reset on descend
}

TEST_CASE("TreeTableView: descend is a no-op on a leaf row", "[treetable]")
{
    FakeModel model;
    TreeTableView view(model);
    view.setArea(Rect { .x = 0, .y = 0, .width = 20, .height = 10 });

    // Row 0 is node 1 ("alpha"), a leaf.
    view.descendCursor();
    CHECK(model.current() == 0); // unchanged
}

TEST_CASE("TreeTableView: ascend restores the remembered cursor", "[treetable]")
{
    FakeModel model;
    TreeTableView view(model);
    view.setArea(Rect { .x = 0, .y = 0, .width = 20, .height = 10 });

    view.moveCursor(1); // cursor on "dir" (index 1)
    view.descendCursor();
    REQUIRE(model.current() == 2);
    view.ascendCursor();
    CHECK(model.current() == 0);
    CHECK(view.cursorIndex() == 1); // restored to where we descended from
}

TEST_CASE("TreeTableView: sortBy keeps the cursor on the same row", "[treetable]")
{
    FakeModel model;
    TreeTableView view(model);
    view.setArea(Rect { .x = 0, .y = 0, .width = 20, .height = 10 });

    // Cursor on row index 0 = node 1. After reverse-sort, node 1 is last.
    bool valid = false;
    REQUIRE(view.cursorRow(valid) == 1);
    view.sortBy(0);
    CHECK(view.cursorRow(valid) == 1); // same row id
    CHECK(view.cursorIndex() == 2);    // now at the end
}

TEST_CASE("TreeTableView: selection-changed callback fires on navigation", "[treetable]")
{
    FakeModel model;
    TreeTableView view(model);
    view.setArea(Rect { .x = 0, .y = 0, .width = 20, .height = 10 });

    int changes = 0;
    view.onSelectionChanged([&] { ++changes; });
    view.moveCursor(1);
    view.descendCursor();
    CHECK(changes >= 2);
}

TEST_CASE("TreeTableView: handles j/k/l/h key events", "[treetable]")
{
    FakeModel model;
    TreeTableView view(model);
    view.setArea(Rect { .x = 0, .y = 0, .width = 20, .height = 10 });

    auto keyChar = [](char32_t c) {
        return InputEvent { KeyEvent { .key = {}, .modifiers = Modifier::None, .codepoint = c } };
    };

    CHECK(view.onEvent(keyChar(U'j')) == EventResult::Handled);
    CHECK(view.cursorIndex() == 1);
    CHECK(view.onEvent(keyChar(U'k')) == EventResult::Handled);
    CHECK(view.cursorIndex() == 0);
    CHECK(view.onEvent(keyChar(U'z')) == EventResult::Ignored);
}
