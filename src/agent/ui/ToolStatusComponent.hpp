// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tui/Component.hpp>
#include <tui/Spinner.hpp>

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <agent/Types.hpp>
#include <agent/session/AgentMessages.hpp>

namespace endo::agent
{

/// Completion information for a finished tool execution.
struct ToolCompletionInfo
{
    bool isError = false;                     ///< Whether the tool returned an error.
    size_t outputSize = 0;                    ///< Size of the tool output in bytes.
    std::chrono::milliseconds duration { 0 }; ///< Wall-clock execution duration.
};

/// A single tool execution entry tracked by the component.
struct ToolEntry
{
    std::string name;                                ///< Tool name.
    std::string argsSummary;                         ///< Compact summary of tool arguments.
    std::chrono::steady_clock::time_point startTime; ///< When the tool started executing.
    std::optional<ToolCompletionInfo> completion;    ///< Set when the tool finishes.
};

/// Renders tool execution status as a TUI component.
///
/// Shows in-progress and completed tool calls with spinners, status icons,
/// tool names, argument summaries, durations, and output sizes.
///
/// Visual layout per row:
/// @code
/// │ ⠙ shell_execute $ cmake --build --preset clang-debug              3.2s
/// │ ✓ read_file src/main.cpp                                    1.2s  4.8 KB
/// │ ✗ edit_file src/foo.cpp                                     0.8s  error
/// @endcode
class ToolStatusComponent: public tui::Component
{
  public:
    ToolStatusComponent() = default;
    ~ToolStatusComponent() override = default;

    /// @brief Renders the tool status entries to the given canvas.
    void render(tui::Canvas& canvas) override;

    /// @brief Returns the preferred size (height = number of visible entries).
    [[nodiscard]] tui::Size preferredSize() const override;

    /// @brief Records a tool starting execution.
    /// @param call The tool call that started.
    void toolStarted(ToolCall const& call);

    /// @brief Records a tool finishing execution.
    /// @param result The tool result message.
    void toolCompleted(ToolResultMessage const& result);

    /// @brief Removes all entries.
    void clear();

    /// @brief Returns true if there are any entries to display.
    [[nodiscard]] bool hasEntries() const noexcept;

    /// @brief Advances the spinner animation.
    /// @return true if the frame changed (needs re-render).
    [[nodiscard]] bool tickSpinner();

    /// @brief Returns the remaining time in ms until the next spinner frame, or -1 if no spinner active.
    [[nodiscard]] int spinnerTimeoutMs() const;

    /// @brief Formats a duration for display.
    /// @param ms The duration in milliseconds.
    /// @return Formatted string (e.g., "0.3s", "3.2s", "1m 12s").
    [[nodiscard]] static auto formatElapsed(std::chrono::milliseconds ms) -> std::string;

    /// @brief Formats a byte size for display.
    /// @param bytes The size in bytes.
    /// @return Formatted string (e.g., "42 B", "4.8 KB", "2.1 MB").
    [[nodiscard]] static auto formatSize(size_t bytes) -> std::string;

    /// @brief Formats tool call arguments as a compact summary string.
    /// @param name The tool name.
    /// @param arguments The tool call arguments.
    /// @return A compact summary string for display.
    [[nodiscard]] static auto formatArgsSummary(std::string const& name, nlohmann::json const& arguments)
        -> std::string;

  private:
    std::vector<ToolEntry> _entries;
    tui::Spinner _spinner { tui::SpinnerType::Dots };

    /// Maximum number of completed entries to show.
    static constexpr auto MaxVisibleCompleted = size_t { 5 };

    /// Returns whether there is an active (in-progress) entry.
    [[nodiscard]] bool hasActiveEntry() const noexcept;
};

} // namespace endo::agent
