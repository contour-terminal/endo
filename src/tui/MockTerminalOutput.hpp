// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tui/TerminalOutput.hpp>

namespace tui
{

/// @brief Mock terminal output for testing.
///
/// Tracks cursor position and visibility semantically instead of writing VT sequences.
/// Use this to verify cursor positioning logic in tests without a real terminal.
class MockTerminalOutput: public TerminalOutput
{
  public:
    /// @brief Constructs a mock terminal output with the given dimensions.
    /// @param cols Number of columns.
    /// @param rows Number of rows.
    explicit MockTerminalOutput(int cols = 80, int rows = 24);

    // --- Virtual overrides ---

    [[nodiscard]] auto initialize() -> VoidResult override;
    void writeText(std::string_view text, Style const& style) override;
    void writeRaw(std::string_view text) override;
    void moveTo(int row, int col) override;
    void moveUp(int n) override;
    void moveDown(int n) override;
    void moveLeft(int n) override;
    void moveRight(int n) override;
    void carriageReturn() override;
    void linefeed() override;
    void clearToEndOfDisplay() override;
    void requestCellSize() override;
    void requestCursorPosition() override;
    void requestDecMode(int mode) override;
    void clearLine() override;
    void clearToEndOfLine() override;
    void clearToStartOfLine() override;
    void clearScreen() override;
    void clearScrollback() override;
    void enterAltScreen() override;
    void leaveAltScreen() override;
    [[nodiscard]] auto syncGuard() -> SyncGuard override;
    void setDoubleWidth() override;
    void setDoubleHeightTop() override;
    void setDoubleHeightBottom() override;
    void setSingleWidth() override;
    void disableReflow() override;
    void enableReflow() override;
    void showCursor() override;
    void hideCursor() override;
    void saveCursor() override;
    void restoreCursor() override;
    void setCursorShape(CursorShape shape) override;
    void setScrollRegion(int top, int bottom) override;
    void resetScrollRegion() override;
    void writeSixel(std::string_view sixelData) override;
    void copyToClipboard(std::string_view text) override;
    void unscroll(int n) override;
    [[nodiscard]] bool supportsUnscroll() const noexcept override;
    void flush() override;
    [[nodiscard]] auto columns() const noexcept -> int override;
    [[nodiscard]] auto rows() const noexcept -> int override;
    void updateDimensions() override;

    // --- Test-specific accessors ---

    /// @brief Returns the current cursor row (0-based).
    [[nodiscard]] int cursorRow() const noexcept;

    /// @brief Returns the current cursor column (0-based).
    [[nodiscard]] int cursorCol() const noexcept;

    /// @brief Returns whether the cursor is currently visible.
    [[nodiscard]] bool cursorVisible() const noexcept;

    /// @brief Returns the total number of scroll operations since construction.
    [[nodiscard]] int scrollCount() const noexcept;

    /// @brief Returns the total number of flush() calls since construction.
    [[nodiscard]] int flushCount() const noexcept;

    /// @brief Returns the text most recently passed to copyToClipboard (empty if never called).
    [[nodiscard]] std::string const& clipboardText() const noexcept;

  private:
    int _cols;
    int _rows;
    int _cursorRow = 0;
    int _cursorCol = 0;
    int _savedCursorRow = 0;
    int _savedCursorCol = 0;
    bool _cursorVisible = true;
    int _scrollCount = 0;
    int _flushCount = 0;
    std::string _clipboard;
};

} // namespace tui
