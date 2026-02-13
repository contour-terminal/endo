// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

#include <tui/CursorShape.hpp>
#include <tui/Error.hpp>

namespace tui
{

/// @brief RGB color representation.
struct RgbColor
{
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
};

/// @brief User-defined literal for creating RgbColor from a hex value.
///
/// Usage: `0xFF6600_rgb` creates RgbColor{255, 102, 0}.
[[nodiscard]] consteval RgbColor operator""_rgb(unsigned long long hex) noexcept
{
    return RgbColor { .r = static_cast<std::uint8_t>((hex >> 16) & 0xFF),
                      .g = static_cast<std::uint8_t>((hex >> 8) & 0xFF),
                      .b = static_cast<std::uint8_t>(hex & 0xFF) };
}

/// @brief Color representation: default, 256-color index, or true color (RGB).
using Color = std::variant<std::monostate, std::uint8_t, RgbColor>;

/// @brief Underline style for terminal output (SGR 4:n).
enum class UnderlineStyle : std::uint8_t
{
    None = 0,   ///< No underline.
    Single = 1, ///< Single underline (SGR 4:1, same as SGR 4).
    Double = 2, ///< Double underline (SGR 4:2).
    Curly = 3,  ///< Curly/wavy underline (SGR 4:3).
    Dotted = 4, ///< Dotted underline (SGR 4:4).
    Dashed = 5, ///< Dashed underline (SGR 4:5).
};

/// @brief Text styling attributes for terminal output.
struct Style
{
    Color fg;                                             ///< Foreground color.
    Color bg;                                             ///< Background color.
    bool bold = false;                                    ///< Bold text.
    bool italic = false;                                  ///< Italic text.
    bool underline = false;                               ///< Underlined text (simple SGR 4).
    bool strikethrough = false;                           ///< Strikethrough text.
    bool dim = false;                                     ///< Dim/faint text.
    bool inverse = false;                                 ///< Inverse/reverse video.
    UnderlineStyle underlineStyle = UnderlineStyle::None; ///< Extended underline style (SGR 4:n).
    Color underlineColor;                                 ///< Underline color (SGR 58;2;r;g;b or 58;5;idx).
};

/// @brief RAII guard for synchronized terminal output.
///
/// Uses CSI ?2026h/l (synchronized output mode) to prevent tearing.
/// The constructor writes the begin sequence, the destructor writes the end sequence
/// and flushes.
class SyncGuard
{
  public:
    /// @brief Begins synchronized output mode.
    /// @param fd File descriptor to write to (typically STDOUT_FILENO).
    explicit SyncGuard(int fd);

    /// @brief Ends synchronized output mode and flushes.
    ~SyncGuard();

    SyncGuard(SyncGuard const&) = delete;
    auto operator=(SyncGuard const&) -> SyncGuard& = delete;
    SyncGuard(SyncGuard&&) = delete;
    auto operator=(SyncGuard&&) -> SyncGuard& = delete;

  private:
    int _fd;
};

/// @brief Handles styled terminal output, cursor control, and screen management.
///
/// Buffers output internally and flushes on demand. Supports SGR styling,
/// cursor movement, alt screen, synchronized output, double-width/height lines,
/// and sixel image output.
class TerminalOutput
{
  public:
    /// @brief Initializes the terminal output by querying terminal dimensions.
    /// @return Success or IoError.
    [[nodiscard]] auto initialize() -> VoidResult;

    /// @brief Writes styled text at the current cursor position.
    /// @param text The text to write.
    /// @param style The style to apply.
    void write(std::string_view text, Style const& style = {});

    /// @brief Writes raw text without styling.
    /// @param text The text to write directly to the buffer.
    void writeRaw(std::string_view text);

    /// @brief Moves the cursor to an absolute position.
    /// @param row Row (1-based).
    /// @param col Column (1-based).
    void moveTo(int row, int col);

    /// @brief Moves the cursor up by n rows.
    void moveUp(int n = 1);

    /// @brief Moves the cursor down by n rows.
    void moveDown(int n = 1);

    /// @brief Moves the cursor left by n columns.
    void moveLeft(int n = 1);

    /// @brief Moves the cursor right by n columns.
    void moveRight(int n = 1);

    /// @brief Clears the entire current line.
    void clearLine();

    /// @brief Clears from cursor to end of line.
    void clearToEndOfLine();

    /// @brief Clears from cursor to start of line.
    void clearToStartOfLine();

    /// @brief Clears the entire screen.
    void clearScreen();

    /// @brief Clears the scrollback buffer.
    void clearScrollback();

    /// @brief Enters the alternate screen buffer.
    void enterAltScreen();

    /// @brief Leaves the alternate screen buffer.
    void leaveAltScreen();

    /// @brief Creates a synchronized output guard.
    ///
    /// While the guard is alive, output is batched to prevent tearing.
    /// @return An RAII SyncGuard object.
    [[nodiscard]] auto syncGuard() -> SyncGuard;

    /// @brief Sets the current line to double-width (ESC #6).
    void setDoubleWidth();

    /// @brief Sets the current line to double-height top half (ESC #3).
    void setDoubleHeightTop();

    /// @brief Sets the current line to double-height bottom half (ESC #4).
    void setDoubleHeightBottom();

    /// @brief Sets the current line to single-width (ESC #5).
    void setSingleWidth();

    /// @brief Shows the cursor (CSI ?25h).
    void showCursor();

    /// @brief Hides the cursor (CSI ?25l).
    void hideCursor();

    /// @brief Saves the cursor position (ESC 7).
    void saveCursor();

    /// @brief Restores the cursor position (ESC 8).
    void restoreCursor();

    /// @brief Sets the cursor shape using DECSCUSR (CSI Ps SP q).
    /// @param shape The desired cursor shape.
    void setCursorShape(CursorShape shape);

    /// @brief Sets the scroll region to the given rows (1-based, inclusive).
    /// @param top First row of the scroll region.
    /// @param bottom Last row of the scroll region.
    void setScrollRegion(int top, int bottom);

    /// @brief Resets the scroll region to the full screen (CSI r).
    void resetScrollRegion();

    /// @brief Writes raw sixel image data to the terminal.
    /// @param sixelData The sixel-encoded image data (without DCS/ST framing).
    void writeSixel(std::string_view sixelData);

    /// @brief Copies text to the system clipboard using OSC 52.
    ///
    /// This works in terminals that support OSC 52 (most modern terminals).
    /// The text is base64-encoded and sent via the OSC 52 escape sequence.
    /// @param text The text to copy to the clipboard.
    void copyToClipboard(std::string_view text);

    /// @brief Unscrolls (restores) lines from the scrollback buffer.
    ///
    /// Uses the Kitty unscroll extension (CSI Ps + T) to restore content
    /// that was previously scrolled into the scrollback buffer.
    /// Supported by: Kitty, Contour, mintty.
    ///
    /// @param n Number of lines to restore from scrollback.
    /// @note On unsupported terminals, this sequence is silently ignored.
    void unscroll(int n);

    /// @brief Checks if the terminal supports the unscroll extension.
    ///
    /// Detects support by checking for known terminals:
    /// - Kitty: KITTY_WINDOW_ID environment variable
    /// - Contour: TERMINAL_NAME=contour environment variable
    /// - mintty: TERM_PROGRAM=mintty environment variable
    ///
    /// @return true if unscroll is likely supported.
    [[nodiscard]] bool supportsUnscroll() const noexcept;

    /// @brief Flushes the internal buffer to stdout.
    void flush();

    /// @brief Returns the terminal width in columns.
    [[nodiscard]] auto columns() const noexcept -> int;

    /// @brief Returns the terminal height in rows.
    [[nodiscard]] auto rows() const noexcept -> int;

    /// @brief Updates the cached terminal dimensions.
    void updateDimensions();

  private:
    std::string _buffer; ///< Output buffer for batching writes.
    int _cols = 80;
    int _rows = 24;
    bool _unscrollSupported = false; ///< Cached unscroll support detection.

    /// @brief Appends SGR (Select Graphic Rendition) sequences for the given style.
    void appendSgr(Style const& style);

    /// @brief Appends the SGR reset sequence.
    void appendSgrReset();
};

} // namespace tui
