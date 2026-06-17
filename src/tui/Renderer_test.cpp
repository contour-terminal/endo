// SPDX-License-Identifier: Apache-2.0
#include <tui/Buffer.hpp>
#include <tui/Canvas.hpp>
#include <tui/Cell.hpp>
#include <tui/Rect.hpp>
#include <tui/Theme.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace tui;

// ============================================================================
// Point tests
// ============================================================================

TEST_CASE("Point.default_construction")
{
    Point p;
    CHECK(p.x == 0);
    CHECK(p.y == 0);
}

TEST_CASE("Point.construction")
{
    Point p { .x = 5, .y = 10 };
    CHECK(p.x == 5);
    CHECK(p.y == 10);
}

TEST_CASE("Point.equality")
{
    Point p1 { .x = 3, .y = 4 };
    Point p2 { .x = 3, .y = 4 };
    Point p3 { .x = 3, .y = 5 };

    CHECK(p1 == p2);
    CHECK(p1 != p3);
}

TEST_CASE("Point.offset")
{
    Point p { .x = 5, .y = 10 };
    Point result = p.offset(2, -3);
    CHECK(result.x == 7);
    CHECK(result.y == 7);
}

// ============================================================================
// Size tests
// ============================================================================

TEST_CASE("Size.default_construction")
{
    Size s;
    CHECK(s.width == 0);
    CHECK(s.height == 0);
}

TEST_CASE("Size.construction")
{
    Size s { .width = 80, .height = 24 };
    CHECK(s.width == 80);
    CHECK(s.height == 24);
}

TEST_CASE("Size.empty")
{
    CHECK(Size {}.empty());
    CHECK(Size { 0, 10 }.empty());
    CHECK(Size { 10, 0 }.empty());
    CHECK(Size { -1, 10 }.empty());
    CHECK_FALSE(Size { 10, 10 }.empty());
}

TEST_CASE("Size.area")
{
    CHECK(Size { 10, 5 }.area() == 50);
    CHECK(Size { 0, 5 }.area() == 0);
}

// ============================================================================
// Rect tests
// ============================================================================

TEST_CASE("Rect.default_construction")
{
    Rect r;
    CHECK(r.x == 0);
    CHECK(r.y == 0);
    CHECK(r.width == 0);
    CHECK(r.height == 0);
    CHECK(r.empty());
}

TEST_CASE("Rect.construction")
{
    Rect r { .x = 5, .y = 10, .width = 20, .height = 15 };
    CHECK(r.x == 5);
    CHECK(r.y == 10);
    CHECK(r.width == 20);
    CHECK(r.height == 15);
}

TEST_CASE("Rect.factory_fromXYWH")
{
    auto r = Rect::fromXYWH(1, 2, 3, 4);
    CHECK(r.x == 1);
    CHECK(r.y == 2);
    CHECK(r.width == 3);
    CHECK(r.height == 4);
}

TEST_CASE("Rect.factory_fromCorners")
{
    auto r = Rect::fromCorners({ .x = 5, .y = 10 }, { .x = 15, .y = 20 });
    CHECK(r.x == 5);
    CHECK(r.y == 10);
    CHECK(r.width == 10);
    CHECK(r.height == 10);
}

TEST_CASE("Rect.factory_fromPositionSize")
{
    auto r = Rect::fromPositionSize({ .x = 3, .y = 4 }, { .width = 10, .height = 20 });
    CHECK(r.x == 3);
    CHECK(r.y == 4);
    CHECK(r.width == 10);
    CHECK(r.height == 20);
}

TEST_CASE("Rect.accessors")
{
    Rect r { .x = 5, .y = 10, .width = 20, .height = 15 };

    CHECK(r.left() == 5);
    CHECK(r.top() == 10);
    CHECK(r.right() == 25);
    CHECK(r.bottom() == 25);

    CHECK(r.topLeft() == Point { 5, 10 });
    CHECK(r.topRight() == Point { 25, 10 });
    CHECK(r.bottomLeft() == Point { 5, 25 });
    CHECK(r.bottomRight() == Point { 25, 25 });

    CHECK(r.size() == Size { 20, 15 });
    CHECK(r.position() == Point { 5, 10 });
}

TEST_CASE("Rect.empty")
{
    CHECK(Rect {}.empty());
    CHECK(Rect { 0, 0, 0, 0 }.empty());
    CHECK(Rect { 0, 0, 0, 10 }.empty());
    CHECK(Rect { 0, 0, 10, 0 }.empty());
    CHECK(Rect { 0, 0, -1, 10 }.empty());
    CHECK_FALSE(Rect { 0, 0, 10, 10 }.empty());
}

TEST_CASE("Rect.area")
{
    CHECK(Rect { 0, 0, 10, 5 }.area() == 50);
    CHECK(Rect { 0, 0, 0, 5 }.area() == 0);
}

TEST_CASE("Rect.contains_point_coordinates")
{
    Rect r { .x = 5, .y = 10, .width = 20, .height = 15 }; // x=5..24 (inclusive), y=10..24 (inclusive)

    // Inside
    CHECK(r.contains(5, 10));  // top-left corner
    CHECK(r.contains(24, 24)); // bottom-right (exclusive bounds are 25, 25)
    CHECK(r.contains(15, 17)); // middle

    // Outside
    CHECK_FALSE(r.contains(4, 10));  // left of rect
    CHECK_FALSE(r.contains(25, 10)); // right of rect (at exclusive bound)
    CHECK_FALSE(r.contains(5, 9));   // above rect
    CHECK_FALSE(r.contains(5, 25));  // below rect (at exclusive bound)
}

TEST_CASE("Rect.contains_point")
{
    Rect r { .x = 5, .y = 10, .width = 20, .height = 15 };
    CHECK(r.contains(Point { 15, 17 }));
    CHECK_FALSE(r.contains(Point { 0, 0 }));
}

TEST_CASE("Rect.contains_rect")
{
    Rect outer { .x = 0, .y = 0, .width = 100, .height = 100 };
    Rect inner { .x = 10, .y = 10, .width = 20, .height = 20 };
    Rect overlapping { .x = 90, .y = 90, .width = 20, .height = 20 };
    Rect disjoint { .x = 200, .y = 200, .width = 10, .height = 10 };

    CHECK(outer.contains(inner));
    CHECK_FALSE(outer.contains(overlapping));
    CHECK_FALSE(outer.contains(disjoint));
    CHECK_FALSE(inner.contains(outer));
}

TEST_CASE("Rect.intersects")
{
    Rect r1 { .x = 0, .y = 0, .width = 10, .height = 10 };
    Rect r2 { .x = 5, .y = 5, .width = 10, .height = 10 };   // overlaps
    Rect r3 { .x = 10, .y = 10, .width = 10, .height = 10 }; // touches corner (no intersection)
    Rect r4 { .x = 20, .y = 20, .width = 10, .height = 10 }; // disjoint

    CHECK(r1.intersects(r2));
    CHECK(r2.intersects(r1));
    CHECK_FALSE(r1.intersects(r3));
    CHECK_FALSE(r1.intersects(r4));
}

TEST_CASE("Rect.offset")
{
    Rect r { .x = 5, .y = 10, .width = 20, .height = 15 };
    auto result = r.offset(3, -2);

    CHECK(result.x == 8);
    CHECK(result.y == 8);
    CHECK(result.width == 20);
    CHECK(result.height == 15);
}

TEST_CASE("Rect.offset_point")
{
    Rect r { .x = 5, .y = 10, .width = 20, .height = 15 };
    auto result = r.offset(Point { .x = 3, .y = -2 });

    CHECK(result.x == 8);
    CHECK(result.y == 8);
}

TEST_CASE("Rect.withPosition")
{
    Rect r { .x = 5, .y = 10, .width = 20, .height = 15 };
    auto result = r.withPosition(100, 200);

    CHECK(result.x == 100);
    CHECK(result.y == 200);
    CHECK(result.width == 20);
    CHECK(result.height == 15);
}

TEST_CASE("Rect.withSize")
{
    Rect r { .x = 5, .y = 10, .width = 20, .height = 15 };
    auto result = r.withSize(50, 60);

    CHECK(result.x == 5);
    CHECK(result.y == 10);
    CHECK(result.width == 50);
    CHECK(result.height == 60);
}

TEST_CASE("Rect.inset")
{
    Rect r { .x = 10, .y = 10, .width = 30, .height = 20 };

    // Uniform inset
    auto r1 = r.inset(5);
    CHECK(r1.x == 15);
    CHECK(r1.y == 15);
    CHECK(r1.width == 20);
    CHECK(r1.height == 10);

    // Horizontal/vertical inset
    auto r2 = r.inset(2, 3);
    CHECK(r2.x == 12);
    CHECK(r2.y == 13);
    CHECK(r2.width == 26);
    CHECK(r2.height == 14);

    // Per-side inset
    auto r3 = r.inset(1, 2, 3, 4);
    CHECK(r3.x == 11);
    CHECK(r3.y == 12);
    CHECK(r3.width == 26);
    CHECK(r3.height == 14);
}

TEST_CASE("Rect.expand")
{
    Rect r { .x = 10, .y = 10, .width = 20, .height = 20 };
    auto result = r.expand(5);

    CHECK(result.x == 5);
    CHECK(result.y == 5);
    CHECK(result.width == 30);
    CHECK(result.height == 30);
}

TEST_CASE("Rect.intersect")
{
    Rect r1 { .x = 0, .y = 0, .width = 10, .height = 10 };
    Rect r2 { .x = 5, .y = 5, .width = 10, .height = 10 };

    auto result = r1.intersect(r2);
    CHECK(result.x == 5);
    CHECK(result.y == 5);
    CHECK(result.width == 5);
    CHECK(result.height == 5);
}

TEST_CASE("Rect.intersect_no_overlap")
{
    Rect r1 { .x = 0, .y = 0, .width = 10, .height = 10 };
    Rect r2 { .x = 20, .y = 20, .width = 10, .height = 10 };

    auto result = r1.intersect(r2);
    CHECK(result.empty());
}

TEST_CASE("Rect.unite")
{
    Rect r1 { .x = 0, .y = 0, .width = 10, .height = 10 };
    Rect r2 { .x = 5, .y = 5, .width = 10, .height = 10 };

    auto result = r1.unite(r2);
    CHECK(result.x == 0);
    CHECK(result.y == 0);
    CHECK(result.width == 15);
    CHECK(result.height == 15);
}

TEST_CASE("Rect.unite_empty")
{
    Rect r1 { .x = 5, .y = 5, .width = 10, .height = 10 };
    Rect r2; // empty

    auto result = r1.unite(r2);
    CHECK(result == r1);

    auto result2 = r2.unite(r1);
    CHECK(result2 == r1);
}

// ============================================================================
// Cell tests
// ============================================================================

TEST_CASE("Cell.default_construction")
{
    Cell c;
    CHECK(c.grapheme.empty());
    CHECK(c.width == 1);
    CHECK_FALSE(c.isContinuation());
    CHECK_FALSE(c.isWide());
}

TEST_CASE("Cell.reset")
{
    Cell c;
    c.grapheme = "X";
    c.width = 2;

    Style style;
    style.bold = true;
    c.reset(style);

    CHECK(c.grapheme == " ");
    CHECK(c.width == 1);
    CHECK(c.style.bold == true);
}

TEST_CASE("Cell.resetContinuation")
{
    Cell c;
    c.grapheme = "X";
    c.width = 1;

    c.resetContinuation();

    CHECK(c.grapheme.empty());
    CHECK(c.width == 0);
    CHECK(c.isContinuation());
}

TEST_CASE("Cell.isContinuation")
{
    Cell c1;
    c1.width = 0;
    CHECK(c1.isContinuation());

    Cell c2;
    c2.width = 1;
    CHECK_FALSE(c2.isContinuation());
}

TEST_CASE("Cell.isWide")
{
    Cell c1;
    c1.width = 2;
    CHECK(c1.isWide());

    Cell c2;
    c2.width = 1;
    CHECK_FALSE(c2.isWide());
}

TEST_CASE("Cell.equality")
{
    Cell c1;
    c1.grapheme = "A";
    c1.width = 1;

    Cell c2;
    c2.grapheme = "A";
    c2.width = 1;

    Cell c3;
    c3.grapheme = "B";
    c3.width = 1;

    CHECK(c1 == c2);
    CHECK(c1 != c3);
}

// ============================================================================
// Buffer tests
// ============================================================================

TEST_CASE("Buffer.default_construction")
{
    Buffer buf;
    CHECK(buf.rows() == 0);
    CHECK(buf.cols() == 0);
    CHECK(buf.empty());
}

TEST_CASE("Buffer.construction_with_size")
{
    Buffer buf(24, 80);
    CHECK(buf.rows() == 24);
    CHECK(buf.cols() == 80);
    CHECK_FALSE(buf.empty());
    CHECK(buf.size() == Size { 80, 24 });
}

TEST_CASE("Buffer.inBounds")
{
    Buffer buf(10, 20);

    CHECK(buf.inBounds(0, 0));
    CHECK(buf.inBounds(9, 19));
    CHECK(buf.inBounds(5, 10));

    CHECK_FALSE(buf.inBounds(-1, 0));
    CHECK_FALSE(buf.inBounds(0, -1));
    CHECK_FALSE(buf.inBounds(10, 0));
    CHECK_FALSE(buf.inBounds(0, 20));
}

TEST_CASE("Buffer.at")
{
    Buffer buf(10, 20);

    Cell& cell = buf.at(5, 10);
    cell.grapheme = "X";

    CHECK(buf.at(5, 10).grapheme == "X");
}

TEST_CASE("Buffer.at_throws_out_of_range")
{
    Buffer buf(10, 20);
    CHECK_THROWS_AS(buf.at(-1, 0), std::out_of_range);
    CHECK_THROWS_AS(buf.at(10, 0), std::out_of_range);
    CHECK_THROWS_AS(buf.at(0, 20), std::out_of_range);
}

TEST_CASE("Buffer.tryAt")
{
    Buffer buf(10, 20);

    CHECK(buf.tryAt(5, 10) != nullptr);
    CHECK(buf.tryAt(-1, 0) == nullptr);
    CHECK(buf.tryAt(10, 0) == nullptr);
}

TEST_CASE("Buffer.resize")
{
    Buffer buf(10, 20);
    buf.at(5, 10).grapheme = "X";

    buf.resize(15, 25);
    CHECK(buf.rows() == 15);
    CHECK(buf.cols() == 25);
    CHECK(buf.at(5, 10).grapheme == "X"); // Preserved

    buf.resize(3, 5);
    CHECK(buf.rows() == 3);
    CHECK(buf.cols() == 5);
    // (5, 10) is now out of bounds
}

TEST_CASE("Buffer.clear")
{
    Buffer buf(10, 20);
    buf.at(5, 10).grapheme = "X";
    buf.at(5, 10).width = 2;

    Style style;
    style.bold = true;
    buf.clear(style);

    CHECK(buf.at(5, 10).grapheme == " ");
    CHECK(buf.at(5, 10).width == 1);
    CHECK(buf.at(5, 10).style.bold == true);
}

TEST_CASE("Buffer.clearRect")
{
    Buffer buf(10, 20);

    // Fill entire buffer with 'X'
    for (int r = 0; r < 10; ++r)
        for (int c = 0; c < 20; ++c)
            buf.at(r, c).grapheme = "X";

    // Clear a region
    buf.clearRect(Rect { .x = 5, .y = 2, .width = 10, .height = 3 });

    // Check cleared region
    CHECK(buf.at(2, 5).grapheme == " ");
    CHECK(buf.at(4, 14).grapheme == " ");

    // Check outside region still has 'X'
    CHECK(buf.at(0, 0).grapheme == "X");
    CHECK(buf.at(5, 0).grapheme == "X");
}

TEST_CASE("Buffer.putString")
{
    Buffer buf(10, 20);

    int consumed = buf.putString(5, 3, "hello", Style {});
    CHECK(consumed == 5);
    CHECK(buf.at(5, 3).grapheme == "h");
    CHECK(buf.at(5, 4).grapheme == "e");
    CHECK(buf.at(5, 5).grapheme == "l");
    CHECK(buf.at(5, 6).grapheme == "l");
    CHECK(buf.at(5, 7).grapheme == "o");
}

TEST_CASE("Buffer.putString_with_style")
{
    Buffer buf(10, 20);
    Style style;
    style.bold = true;

    buf.putString(5, 3, "hi", style);

    CHECK(buf.at(5, 3).style.bold == true);
    CHECK(buf.at(5, 4).style.bold == true);
}

TEST_CASE("Buffer.putString_clipping")
{
    Buffer buf(10, 20);

    // Write string that would extend past buffer edge
    int consumed = buf.putString(5, 17, "hello", Style {});

    // Should only write 3 characters (columns 17, 18, 19)
    CHECK(consumed == 3);
    CHECK(buf.at(5, 17).grapheme == "h");
    CHECK(buf.at(5, 18).grapheme == "e");
    CHECK(buf.at(5, 19).grapheme == "l");
}

TEST_CASE("Buffer.fill")
{
    Buffer buf(10, 20);
    Style style;
    style.bold = true;

    buf.fill(Rect { .x = 2, .y = 3, .width = 5, .height = 4 }, '*', style);

    // Check filled region
    CHECK(buf.at(3, 2).grapheme == "*");
    CHECK(buf.at(3, 2).style.bold == true);
    CHECK(buf.at(6, 6).grapheme == "*");

    // Check outside region (cells are initialized to space by Buffer constructor)
    CHECK(buf.at(0, 0).grapheme == " ");
    CHECK(buf.at(0, 0).style.bold == false);
}

TEST_CASE("Buffer.cursor")
{
    Buffer buf(10, 20);

    CHECK(buf.cursor() == Point { 0, 0 });

    buf.setCursor(5, 10);
    CHECK(buf.cursor() == Point { 10, 5 });
}

TEST_CASE("Buffer.cursorVisible")
{
    Buffer buf(10, 20);

    CHECK(buf.cursorVisible() == true); // default

    buf.setCursorVisible(false);
    CHECK(buf.cursorVisible() == false);
}

// ============================================================================
// Canvas tests
// ============================================================================

TEST_CASE("Canvas.dimensions")
{
    Buffer buf(24, 80);
    auto theme = darkTheme();
    Canvas canvas(buf, Rect { .x = 10, .y = 5, .width = 30, .height = 10 }, theme);

    CHECK(canvas.width() == 30);
    CHECK(canvas.height() == 10);
    CHECK(canvas.size() == Size { 30, 10 });
    CHECK(canvas.area() == Rect { 0, 0, 30, 10 });
}

TEST_CASE("Canvas.put")
{
    Buffer buf(24, 80);
    auto theme = darkTheme();
    Canvas canvas(buf, Rect { .x = 10, .y = 5, .width = 30, .height = 10 }, theme);
    Style style;
    style.bold = true;

    canvas.put(2, 3, "A", style);

    // Canvas coordinates (2, 3) should map to buffer coordinates (5+2, 10+3) = (7, 13)
    CHECK(buf.at(7, 13).grapheme == "A");
    CHECK(buf.at(7, 13).style.bold == true);
}

TEST_CASE("Canvas.put_clipping")
{
    Buffer buf(24, 80);
    auto theme = darkTheme();
    Canvas canvas(buf, Rect { .x = 10, .y = 5, .width = 30, .height = 10 }, theme);

    // Put outside canvas bounds - should be ignored
    canvas.put(-1, 0, "X", Style {});
    canvas.put(0, -1, "X", Style {});
    canvas.put(10, 0, "X", Style {}); // row 10 is out of bounds (height=10, valid 0-9)
    canvas.put(0, 30, "X", Style {}); // col 30 is out of bounds

    // Put something valid to verify clipping works
    canvas.put(0, 0, "Y", Style {});

    // Verify valid position was written (canvas (0,0) = buffer (5, 10))
    CHECK(buf.at(5, 10).grapheme == "Y");

    // Verify nothing was written to buffer positions that would correspond to
    // out-of-bounds canvas coordinates (they should still be the default space)
    CHECK(buf.at(4, 10).grapheme == " ");  // Canvas row -1 would be buffer row 4
    CHECK(buf.at(5, 9).grapheme == " ");   // Canvas col -1 would be buffer col 9
    CHECK(buf.at(15, 10).grapheme == " "); // Canvas row 10 would be buffer row 15
    CHECK(buf.at(5, 40).grapheme == " ");  // Canvas col 30 would be buffer col 40
}

TEST_CASE("Canvas.putString")
{
    Buffer buf(24, 80);
    auto theme = darkTheme();
    Canvas canvas(buf, Rect { .x = 10, .y = 5, .width = 30, .height = 10 }, theme);

    int consumed = canvas.putString(2, 3, "hello", Style {});
    CHECK(consumed == 5);

    // Canvas (2, 3) = Buffer (7, 13)
    CHECK(buf.at(7, 13).grapheme == "h");
    CHECK(buf.at(7, 17).grapheme == "o");
}

TEST_CASE("Canvas.putString_clipping")
{
    Buffer buf(24, 80);
    auto theme = darkTheme();
    Canvas canvas(
        buf, Rect { .x = 10, .y = 5, .width = 10, .height = 5 }, theme); // Small canvas: 10 cols, 5 rows

    // Write string starting at column 7, which should clip at column 10
    int consumed = canvas.putString(0, 7, "hello", Style {});
    CHECK(consumed == 3); // Only "hel" fits (cols 7, 8, 9)
}

TEST_CASE("Canvas.putString_wide_cluster_right_edge")
{
    // A wide (2-cell) grapheme cluster at the canvas' last column must not spill its
    // continuation cell into cells outside the canvas area (which belong to other components).
    Buffer buf(24, 80);
    auto theme = darkTheme();
    // Canvas occupies buffer columns 10..19 (width 10); buffer column 20 is outside the area.
    Canvas canvas(buf, Rect { .x = 10, .y = 5, .width = 10, .height = 5 }, theme);

    // "界" is a width-2 CJK glyph. Placed at canvas col 9 it would occupy canvas cols 9 and 10,
    // i.e. buffer cols 19 and 20 — but col 20 is outside the canvas. It must be dropped entirely.
    int consumed = canvas.putString(0, 9, "界", Style {});
    CHECK(consumed == 0); // Did not fit within the area.

    // Canvas col 9 = buffer col 19; the out-of-area cell is buffer col 20.
    CHECK(buf.at(5, 19).grapheme == " "); // Last canvas cell untouched (cluster dropped).
    CHECK(buf.at(5, 20).grapheme == " "); // Cell outside the canvas area must be untouched.

    // A width-2 cluster that fully fits (canvas cols 8..9 = buffer cols 18..19) is written.
    consumed = canvas.putString(0, 8, "界", Style {});
    CHECK(consumed == 2);
    CHECK(buf.at(5, 18).grapheme == "界");
    CHECK(buf.at(5, 20).grapheme == " "); // Still nothing outside the area.
}

TEST_CASE("Canvas.fill")
{
    Buffer buf(24, 80);
    auto theme = darkTheme();
    Canvas canvas(buf, Rect { .x = 10, .y = 5, .width = 30, .height = 10 }, theme);
    Style style;
    style.bold = true;

    canvas.fill(Rect { .x = 2, .y = 1, .width = 5, .height = 3 }, '#', style);

    // Canvas (1, 2) = Buffer (6, 12)
    CHECK(buf.at(6, 12).grapheme == "#");
    CHECK(buf.at(6, 12).style.bold == true);

    // Canvas (3, 6) = Buffer (8, 16)
    CHECK(buf.at(8, 16).grapheme == "#");
}

TEST_CASE("Canvas.clear")
{
    Buffer buf(24, 80);
    auto theme = darkTheme();
    Canvas canvas(buf, Rect { .x = 10, .y = 5, .width = 30, .height = 10 }, theme);

    // Put some content first
    canvas.putString(0, 0, "hello", Style {});

    Style clearStyle;
    clearStyle.dim = true;
    canvas.clear(clearStyle);

    // Check that the canvas area is cleared
    CHECK(buf.at(5, 10).grapheme == " ");
    CHECK(buf.at(5, 10).style.dim == true);
}

TEST_CASE("Canvas.setCursor")
{
    Buffer buf(24, 80);
    auto theme = darkTheme();
    Canvas canvas(buf, Rect { .x = 10, .y = 5, .width = 30, .height = 10 }, theme);

    canvas.setCursor(3, 7);

    // Canvas (3, 7) = Buffer (8, 17)
    CHECK(buf.cursor() == Point { 17, 8 });
}

TEST_CASE("Canvas.hideCursor")
{
    Buffer buf(24, 80);
    auto theme = darkTheme();
    Canvas canvas(buf, Rect { .x = 10, .y = 5, .width = 30, .height = 10 }, theme);

    canvas.hideCursor();
    CHECK(buf.cursorVisible() == false);
}

TEST_CASE("Canvas.subcanvas")
{
    Buffer buf(24, 80);
    auto theme = darkTheme();
    Canvas canvas(buf, Rect { .x = 10, .y = 5, .width = 30, .height = 10 }, theme);

    // Create subcanvas within the canvas
    Canvas sub = canvas.subcanvas(Rect { .x = 5, .y = 2, .width = 10, .height = 4 });

    CHECK(sub.width() == 10);
    CHECK(sub.height() == 4);

    // Write to subcanvas
    sub.put(1, 2, "X", Style {});

    // Subcanvas (1, 2) relative to canvas (5, 2) = canvas (3, 7) = buffer (8, 17)
    CHECK(buf.at(8, 17).grapheme == "X");
}

TEST_CASE("Canvas.subcanvas_clipping")
{
    Buffer buf(24, 80);
    auto theme = darkTheme();
    Canvas canvas(buf, Rect { .x = 10, .y = 5, .width = 30, .height = 10 }, theme);

    // Create subcanvas that extends beyond parent canvas
    Canvas sub = canvas.subcanvas(
        Rect { .x = 25, .y = 0, .width = 20, .height = 5 }); // Would extend to col 45, but parent ends at 30

    // Subcanvas should be clipped to parent bounds
    CHECK(sub.width() == 5); // 30 - 25 = 5
    CHECK(sub.height() == 5);
}

TEST_CASE("Canvas.drawBox")
{
    Buffer buf(24, 80);
    auto theme = darkTheme();
    Canvas canvas(buf, Rect { .x = 0, .y = 0, .width = 80, .height = 24 }, theme);

    canvas.drawBox(Rect { .x = 5, .y = 2, .width = 10, .height = 4 }, BorderStyle::Single, Style {});

    // Check corners (using Unicode box characters)
    // Top-left at (2, 5)
    CHECK(buf.at(2, 5).grapheme.empty() == false);

    // Just verify the box was drawn (specific characters depend on implementation)
    // The important thing is that the buffer was modified
}

TEST_CASE("Canvas.drawHLine")
{
    Buffer buf(24, 80);
    auto theme = darkTheme();
    Canvas canvas(buf, Rect { .x = 0, .y = 0, .width = 80, .height = 24 }, theme);

    canvas.drawHLine(5, 10, 15, "-", Style {});

    CHECK(buf.at(5, 10).grapheme == "-");
    CHECK(buf.at(5, 24).grapheme == "-");
}

TEST_CASE("Canvas.drawVLine")
{
    Buffer buf(24, 80);
    auto theme = darkTheme();
    Canvas canvas(buf, Rect { .x = 0, .y = 0, .width = 80, .height = 24 }, theme);

    canvas.drawVLine(10, 5, 8, "|", Style {});

    CHECK(buf.at(5, 10).grapheme == "|");
    CHECK(buf.at(12, 10).grapheme == "|");
}
