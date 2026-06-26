// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tui/CursorShape.hpp>
#include <tui/Error.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

namespace tui
{

/// @brief RGB color representation.
struct RgbColor
{
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    /// Componentwise equality.
    [[nodiscard]] constexpr bool operator==(RgbColor const&) const noexcept = default;
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

/// @brief Linearly interpolates between two RGB colors.
///
/// @param a The start color.
/// @param b The end color.
/// @param t Interpolation parameter in [0, 1].
/// @return The interpolated color.
[[nodiscard]] constexpr RgbColor lerpColor(RgbColor a, RgbColor b, float t) noexcept
{
    auto const lerp = [](std::uint8_t x, std::uint8_t y, float s) noexcept -> std::uint8_t {
        auto const val = static_cast<float>(x) + ((static_cast<float>(y) - static_cast<float>(x)) * s);
        if (val < 0.0f)
            return 0;
        if (val > 255.0f)
            return 255;
        return static_cast<std::uint8_t>(val);
    };
    return { .r = lerp(a.r, b.r, t), .g = lerp(a.g, b.g, t), .b = lerp(a.b, b.b, t) };
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
#if defined(_WIN32)
    using NativeHandle = void*; ///< Windows HANDLE, avoids #include <windows.h>.
#else
    using NativeHandle = int; ///< POSIX file descriptor.
#endif

    /// @brief Constructs a no-op guard (no synchronized output).
    SyncGuard();

    /// @brief Begins synchronized output mode.
    /// @param handle Native file handle to write to.
    explicit SyncGuard(NativeHandle handle);

    /// @brief Ends synchronized output mode and flushes.
    ~SyncGuard();

    SyncGuard(SyncGuard const&) = delete;
    auto operator=(SyncGuard const&) -> SyncGuard& = delete;
    SyncGuard(SyncGuard&&) noexcept;
    auto operator=(SyncGuard&&) noexcept -> SyncGuard&;

  private:
    NativeHandle _handle;
};

/// @brief Handles styled terminal output, cursor control, and screen management.
///
/// Buffers output internally and flushes on demand. Supports SGR styling,
/// cursor movement, alt screen, synchronized output, double-width/height lines,
/// and sixel image output.
///
/// All public methods are virtual to allow subclassing for testing (e.g., MockTerminalOutput).
class TerminalOutput
{
  public:
    TerminalOutput() = default;
    virtual ~TerminalOutput() = default;

    TerminalOutput(TerminalOutput const&) = delete;
    auto operator=(TerminalOutput const&) -> TerminalOutput& = delete;
    TerminalOutput(TerminalOutput&&) = delete;
    auto operator=(TerminalOutput&&) -> TerminalOutput& = delete;

    /// @brief Initializes the terminal output by querying terminal dimensions.
    /// @return Success or IoError.
    [[nodiscard]] virtual auto initialize() -> VoidResult;

    /// @brief Writes styled text at the current cursor position.
    /// @param text The plain text to write (no VT sequences).
    /// @param style The style to apply.
    virtual void writeText(std::string_view text, Style const& style = {});

    /// @brief Writes raw text without styling.
    /// @param text The text to write directly to the buffer.
    virtual void writeRaw(std::string_view text);

    /// @brief Moves the cursor to an absolute position.
    /// @param row Row (1-based).
    /// @param col Column (1-based).
    virtual void moveTo(int row, int col);

    /// @brief Moves the cursor up by n rows.
    virtual void moveUp(int n = 1);

    /// @brief Moves the cursor down by n rows.
    virtual void moveDown(int n = 1);

    /// @brief Moves the cursor left by n columns.
    virtual void moveLeft(int n = 1);

    /// @brief Moves the cursor right by n columns.
    virtual void moveRight(int n = 1);

    /// @brief Moves the cursor to column 0 (carriage return).
    virtual void carriageReturn();

    /// @brief Moves the cursor down one row, scrolling if at the bottom.
    virtual void linefeed();

    /// @brief Clears from the cursor to the end of the display (CSI J).
    virtual void clearToEndOfDisplay();

    /// @brief Sends a request for cell pixel dimensions (CSI 16 t).
    virtual void requestCellSize();

    /// @brief Sends a request for cursor position (CSI 6 n).
    virtual void requestCursorPosition();

    /// @brief Sends a DEC Private Mode request (DECRQM): CSI ? mode $ p.
    ///
    /// The terminal responds with CSI ? mode ; status $ y (DecModeReport).
    /// @param mode The DEC private mode number to query.
    virtual void requestDecMode(int mode);

    /// @brief Clears the entire current line.
    virtual void clearLine();

    /// @brief Clears from cursor to end of line.
    virtual void clearToEndOfLine();

    /// @brief Clears from cursor to start of line.
    virtual void clearToStartOfLine();

    /// @brief Clears the entire screen.
    virtual void clearScreen();

    /// @brief Clears the scrollback buffer.
    virtual void clearScrollback();

    /// @brief Enters the alternate screen buffer.
    virtual void enterAltScreen();

    /// @brief Leaves the alternate screen buffer.
    virtual void leaveAltScreen();

    /// @brief Creates a synchronized output guard.
    ///
    /// While the guard is alive, output is batched to prevent tearing.
    /// @return An RAII SyncGuard object.
    [[nodiscard]] virtual auto syncGuard() -> SyncGuard;

    /// @brief Sets the current line to double-width (ESC #6).
    virtual void setDoubleWidth();

    /// @brief Sets the current line to double-height top half (ESC #3).
    virtual void setDoubleHeightTop();

    /// @brief Sets the current line to double-height bottom half (ESC #4).
    virtual void setDoubleHeightBottom();

    /// @brief Sets the current line to single-width (ESC #5).
    virtual void setSingleWidth();

    /// @brief Disables text reflow mode (DEC mode 2028l — Contour extension).
    virtual void disableReflow();

    /// @brief Enables text reflow mode (DEC mode 2028h — Contour extension).
    virtual void enableReflow();

    /// @brief Shows the cursor (CSI ?25h).
    virtual void showCursor();

    /// @brief Hides the cursor (CSI ?25l).
    virtual void hideCursor();

    /// @brief Saves the cursor position (ESC 7).
    virtual void saveCursor();

    /// @brief Restores the cursor position (ESC 8).
    virtual void restoreCursor();

    /// @brief Sets the cursor shape using DECSCUSR (CSI Ps SP q).
    /// @param shape The desired cursor shape.
    virtual void setCursorShape(CursorShape shape);

    /// @brief Sets the scroll region to the given rows (1-based, inclusive).
    /// @param top First row of the scroll region.
    /// @param bottom Last row of the scroll region.
    virtual void setScrollRegion(int top, int bottom);

    /// @brief Resets the scroll region to the full screen (CSI r).
    virtual void resetScrollRegion();

    /// @brief Writes raw sixel image data to the terminal.
    /// @param sixelData The sixel-encoded image data (without DCS/ST framing).
    virtual void writeSixel(std::string_view sixelData);

    /// @brief Copies text to the system clipboard using OSC 52.
    ///
    /// This works in terminals that support OSC 52 (most modern terminals).
    /// The text is base64-encoded and sent via the OSC 52 escape sequence.
    /// @param text The text to copy to the clipboard.
    virtual void copyToClipboard(std::string_view text);

    /// @brief Unscrolls (restores) lines from the scrollback buffer.
    ///
    /// Uses the Kitty unscroll extension (CSI Ps + T) to restore content
    /// that was previously scrolled into the scrollback buffer.
    /// Supported by: Kitty, Contour, mintty.
    ///
    /// @param n Number of lines to restore from scrollback.
    /// @note On unsupported terminals, this sequence is silently ignored.
    virtual void unscroll(int n);

    /// @brief Detects terminal capabilities that require query/response I/O.
    ///
    /// Must be called after raw mode is enabled (ECHO off) to prevent
    /// response bytes from being echoed to the screen.
    /// Currently detects unscroll support via XTVERSION query.
    virtual void detectCapabilities();

    /// @brief Checks if the terminal supports the unscroll extension.
    ///
    /// Detects support by checking for known terminals:
    /// - Kitty: KITTY_WINDOW_ID environment variable
    /// - Contour: TERMINAL_NAME=contour environment variable
    /// - mintty: TERM_PROGRAM=mintty environment variable
    ///
    /// @return true if unscroll is likely supported.
    [[nodiscard]] virtual bool supportsUnscroll() const noexcept;

    /// @brief Flushes the internal buffer to stdout.
    virtual void flush();

    /// @brief Returns the terminal width in columns.
    [[nodiscard]] virtual auto columns() const noexcept -> int;

    /// @brief Returns the terminal height in rows.
    [[nodiscard]] virtual auto rows() const noexcept -> int;

    /// @brief Updates the cached terminal dimensions.
    virtual void updateDimensions();

    /// @brief Records that inline content room has been reserved on the terminal.
    ///
    /// Called by Screen::clearAndRelease() to communicate to the next Screen's
    /// first flushInline() render that room already exists on screen and LF
    /// reservation can be skipped.
    ///
    /// @param rows Number of rows of reserved room.
    void setInlineRoomReserved(int rows) noexcept { _inlineRoomReserved = rows; }

    /// @brief Consumes and returns any previously reserved inline room.
    ///
    /// Returns the number of rows of room previously reserved via
    /// setInlineRoomReserved(), then resets the value to 0 (one-shot).
    ///
    /// @return Number of reserved rows, or 0 if none.
    [[nodiscard]] int consumeInlineRoom() noexcept
    {
        auto const room = _inlineRoomReserved;
        _inlineRoomReserved = 0;
        return room;
    }

  private:
    std::string _buffer; ///< Output buffer for batching writes.
    int _cols = 80;
    int _rows = 24;
    bool _unscrollSupported = false; ///< Cached unscroll support detection.
    int _inlineRoomReserved = 0;     ///< Rows of inline room reserved by previous Screen.

    /// @brief Appends SGR (Select Graphic Rendition) sequences for the given style.
    void appendSgr(Style const& style);

    /// @brief Appends the SGR reset sequence.
    void appendSgrReset();
};

} // namespace tui
