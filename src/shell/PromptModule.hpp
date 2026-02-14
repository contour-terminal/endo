// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tui/TerminalOutput.hpp>

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

namespace tui
{
struct Theme;
} // namespace tui

namespace endo
{

class FSharpPersistentState;
class OutputDefinitionRegistry;

/// @brief A single styled text segment within a prompt.
struct PromptSegment
{
    std::string text; ///< The text content.
    tui::Style style; ///< The style to render this segment with.
};

/// @brief A sequence of styled segments forming a prompt section.
using PromptSegments = std::vector<PromptSegment>;

/// @brief Context information available to prompt modules during evaluation.
struct PromptContext
{
    std::string cwd;                                      ///< Current working directory.
    std::string homePath;                                 ///< User's home directory path.
    int lastExitCode = 0;                                 ///< Exit code of the last command.
    std::chrono::milliseconds lastDuration { 0 };         ///< Duration of the last command.
    int terminalWidth = 80;                               ///< Terminal width in columns.
    bool isSSH = false;                                   ///< Whether running inside an SSH session.
    std::string hostname;                                 ///< Hostname of the machine.
    tui::Theme const* theme = nullptr;                    ///< Current TUI theme.
    FSharpPersistentState const* fsharpState = nullptr;   ///< F# persistent state (functions, bindings).
    OutputDefinitionRegistry const* outputDefs = nullptr; ///< Output definitions for structured commands.
};

/// @brief Abstract interface for a pluggable prompt module.
///
/// Each module represents a single informational section of the prompt
/// (e.g., path, git status, exit code). Modules are evaluated in order
/// and their segments are rendered by the layout engine.
class PromptModule
{
  public:
    virtual ~PromptModule() = default;

    /// @brief Returns the unique identifier for this module.
    [[nodiscard]] virtual std::string_view id() const noexcept = 0;

    /// @brief Evaluates the module and returns styled segments.
    /// @param ctx The prompt context with current shell state.
    /// @return The styled text segments to render.
    [[nodiscard]] virtual PromptSegments evaluate(PromptContext const& ctx) const = 0;

    /// @brief Returns whether this module should be displayed.
    /// @param ctx The prompt context with current shell state.
    /// @return True if the module has content to show.
    [[nodiscard]] virtual bool shouldShow(PromptContext const& ctx) const
    {
        (void) ctx;
        return true;
    }

    /// @brief Returns the desired auto-refresh interval for this module.
    /// Modules displaying time-varying data (clock, battery) should override this.
    /// The prompt re-renders at the minimum interval of all active modules.
    /// @return Refresh interval, or std::nullopt if no auto-refresh needed (default).
    [[nodiscard]] virtual std::optional<std::chrono::milliseconds> refreshInterval() const
    {
        return std::nullopt;
    }
};

} // namespace endo
