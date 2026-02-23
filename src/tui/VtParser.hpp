// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tui/InputEvent.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace tui
{

/// @brief Feed-based incremental state machine for parsing VT input sequences.
///
/// Accepts raw bytes from stdin via feed() and produces InputEvent objects.
/// Handles CSI sequences, CSIu (Kitty keyboard protocol), SS3 sequences,
/// SGR mouse reporting, bracketed paste, UTF-8 multi-byte sequences,
/// and bare ESC disambiguation via timeout().
class VtParser
{
  public:
    /// @brief Feeds raw bytes and produces zero or more parsed events.
    /// @param data Raw bytes from stdin.
    /// @return Vector of parsed input events.
    [[nodiscard]] auto feed(std::string_view data) -> std::vector<InputEvent>;

    /// @brief Call after a timeout (e.g. 50ms) with no new data.
    ///
    /// Resolves ambiguous sequences like a bare ESC that could be the start
    /// of a longer escape sequence.
    /// @return Vector of resolved events (typically 0 or 1).
    [[nodiscard]] auto timeout() -> std::vector<InputEvent>;

  private:
    /// @brief Parser state machine states.
    enum class State : std::uint8_t
    {
        Ground,       ///< Default state, expecting new input.
        Escape,       ///< Received ESC, waiting for sequence type.
        CsiEntry,     ///< Received ESC[, waiting for params or final byte.
        CsiParam,     ///< Parsing CSI parameter digits and semicolons.
        Ss3,          ///< Received ESC O, waiting for final byte.
        PasteBody,    ///< Inside bracketed paste (ESC[200~), collecting text.
        Utf8Sequence, ///< Collecting UTF-8 continuation bytes.
        DcsEntry,     ///< Received ESC P, collecting parameter/intermediate bytes.
        DcsBody,      ///< Inside DCS body, collecting data until ST (ESC \).
    };

    State _state = State::Ground;
    std::string _paramBuf;  ///< Buffer for CSI parameter bytes.
    std::string _utf8Buf;   ///< Buffer for UTF-8 multi-byte sequence.
    std::string _pasteBuf;  ///< Buffer for bracketed paste content.
    std::string _dcsBuf;    ///< Buffer for DCS (Device Control String) payload.
    int _utf8Remaining = 0; ///< Expected remaining UTF-8 continuation bytes.

    /// @brief Processes a single byte in the Ground state.
    void processGround(std::uint8_t byte, std::vector<InputEvent>& events);

    /// @brief Processes a single byte in the Escape state.
    void processEscape(std::uint8_t byte, std::vector<InputEvent>& events);

    /// @brief Processes a single byte in the CsiEntry/CsiParam state.
    void processCsi(std::uint8_t byte, std::vector<InputEvent>& events);

    /// @brief Processes a single byte in the Ss3 state.
    void processSs3(std::uint8_t byte, std::vector<InputEvent>& events);

    /// @brief Processes a single byte in the PasteBody state.
    void processPaste(std::uint8_t byte, std::vector<InputEvent>& events);

    /// @brief Processes a single byte in the Utf8Sequence state.
    void processUtf8(std::uint8_t byte, std::vector<InputEvent>& events);

    /// @brief Processes a single byte in the DcsEntry state.
    void processDcsEntry(std::uint8_t byte, std::vector<InputEvent>& events);

    /// @brief Processes a single byte in the DcsBody state.
    void processDcsBody(std::uint8_t byte, std::vector<InputEvent>& events);

    /// @brief Parses a complete CSI sequence from _paramBuf and the final byte.
    void dispatchCsi(char finalByte, std::vector<InputEvent>& events);

    /// @brief Emits a KeyEvent for a single Unicode codepoint.
    static void emitCodepoint(char32_t cp, std::vector<InputEvent>& events);

    /// @brief Decodes a complete UTF-8 sequence to a codepoint and emits a KeyEvent.
    void emitUtf8(std::vector<InputEvent>& events);
};

} // namespace tui
