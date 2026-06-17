// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tui/Cell.hpp>
#include <tui/Rect.hpp>

#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tui
{

class TerminalOutput;

/// @brief An image region stored in the buffer.
///
/// Represents a pre-encoded sixel image covering a rectangular region of terminal cells.
struct ImageRegion
{
    Rect cellArea;               ///< Which cells this image covers (buffer coordinates).
    std::string encodedSixel;    ///< Pre-encoded sixel data (without DCS framing).
    std::size_t contentHash = 0; ///< Hash of encodedSixel for fast diff comparison.
};

/// A 2D grid of terminal cells representing a virtual screen buffer.
///
/// The buffer stores cells in row-major order and provides operations
/// for reading, writing, and clearing cells. Coordinates are 0-based.
///
/// The buffer also tracks cursor position and visibility state for
/// final cursor placement after rendering.
class Buffer
{
  public:
    /// Constructs an empty buffer (0x0).
    Buffer() = default;

    /// Constructs a buffer with the given dimensions.
    /// @param rows Number of rows.
    /// @param cols Number of columns.
    Buffer(int rows, int cols);

    // --- Dimensions ---

    /// Returns the number of rows.
    [[nodiscard]] int rows() const noexcept { return _rows; }

    /// Returns the number of columns.
    [[nodiscard]] int cols() const noexcept { return _cols; }

    /// Returns the size as a Size struct.
    [[nodiscard]] Size size() const noexcept { return { .width = _cols, .height = _rows }; }

    /// Returns true if the buffer has no cells.
    [[nodiscard]] bool empty() const noexcept { return _cells.empty(); }

    /// Resizes the buffer to new dimensions.
    /// Existing content is preserved where possible; new cells are cleared.
    /// @param rows New number of rows.
    /// @param cols New number of columns.
    void resize(int rows, int cols);

    // --- Cell Access ---

    /// Returns true if the given coordinates are within bounds.
    [[nodiscard]] bool inBounds(int row, int col) const noexcept
    {
        return row >= 0 && row < _rows && col >= 0 && col < _cols;
    }

    /// Returns a reference to the cell at (row, col).
    /// @throws std::out_of_range if coordinates are out of bounds.
    [[nodiscard]] Cell& at(int row, int col);

    /// Returns a const reference to the cell at (row, col).
    /// @throws std::out_of_range if coordinates are out of bounds.
    [[nodiscard]] Cell const& at(int row, int col) const;

    /// Returns a pointer to the cell at (row, col), or nullptr if out of bounds.
    [[nodiscard]] Cell* tryAt(int row, int col) noexcept;

    /// Returns a const pointer to the cell at (row, col), or nullptr if out of bounds.
    [[nodiscard]] Cell const* tryAt(int row, int col) const noexcept;

    // --- Drawing Operations ---

    /// Clears all cells to spaces with the given style.
    void clear(Style const& style = {});

    /// Clears a rectangular region to spaces with the given style.
    void clearRect(Rect area, Style const& style = {});

    /// Writes a string starting at (row, col).
    /// Handles wide characters by marking continuation cells.
    /// @param row Starting row (0-based).
    /// @param col Starting column (0-based).
    /// @param text UTF-8 text to write.
    /// @param style Style to apply.
    /// @param maxWidth Maximum number of display columns the caller is permitted to write,
    ///                 measured from @p col. A grapheme cluster is skipped entirely if it (or
    ///                 its continuation cells) would exceed this budget — this prevents a wide
    ///                 cluster at the edge of a sub-region from spilling into adjacent cells.
    ///                 Defaults to the full buffer width (no extra limit).
    /// @return Number of columns consumed.
    int putString(int row,
                  int col,
                  std::string_view text,
                  Style const& style,
                  int maxWidth = std::numeric_limits<int>::max());

    /// Fills a rectangular region with a character.
    /// @param area The region to fill.
    /// @param ch Character to fill with (ASCII).
    /// @param style Style to apply.
    void fill(Rect area, char ch, Style const& style);

    // --- Cursor State ---

    /// Sets the cursor position.
    void setCursor(int row, int col) noexcept;

    /// Returns the cursor position.
    [[nodiscard]] Point cursor() const noexcept { return _cursor; }

    /// Sets cursor visibility.
    void setCursorVisible(bool visible) noexcept { _cursorVisible = visible; }

    /// Returns true if the cursor should be visible.
    [[nodiscard]] bool cursorVisible() const noexcept { return _cursorVisible; }

    // --- Iteration ---

    /// Raw access to cell data (for diffing).
    [[nodiscard]] std::vector<Cell> const& cells() const noexcept { return _cells; }

    // --- Image Regions ---

    /// @brief Adds an image region to the buffer.
    /// @param cellArea The cell area the image covers (in buffer coordinates).
    /// @param encodedSixel Pre-encoded sixel data (without DCS framing).
    void addImage(Rect cellArea, std::string encodedSixel);

    /// @brief Returns all image regions.
    [[nodiscard]] std::span<ImageRegion const> images() const noexcept;

    /// @brief Clears all image regions.
    void clearImages() noexcept;

    /// Writes buffer content to a terminal output stream.
    ///
    /// Iterates rows/cells and emits text with styling, handling wide characters
    /// and continuation cells. Uses carriageReturn + clearToEndOfLine per row.
    /// @param out The terminal output to write to.
    void writeTo(TerminalOutput& out) const;

  private:
    std::vector<Cell> _cells;
    int _rows = 0;
    int _cols = 0;
    Point _cursor { .x = 0, .y = 0 };
    bool _cursorVisible = true;
    std::vector<ImageRegion> _images;

    /// Converts (row, col) to a linear index.
    [[nodiscard]] std::size_t index(int row, int col) const noexcept
    {
        return (static_cast<std::size_t>(row) * static_cast<std::size_t>(_cols))
               + static_cast<std::size_t>(col);
    }
};

} // namespace tui
