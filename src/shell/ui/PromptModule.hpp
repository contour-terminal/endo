// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tui/TerminalOutput.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace tui
{
struct Theme;
} // namespace tui

namespace endo
{

struct ResolvedPromptColors;
class FSharpPersistentState;
class OutputDefinitionRegistry;

/// @brief Bit flags for events that trigger module re-evaluation.
enum class ModuleSensitivity : uint8_t
{
    None = 0,
    CwdChange = 1 << 0,   ///< Working directory changed.
    ExitCode = 1 << 1,    ///< Last command exit code changed.
    Duration = 1 << 2,    ///< Last command duration changed.
    InputChange = 1 << 3, ///< User input text changed.
    /// All shell-context changes (new prompt cycle).
    ContextChange = CwdChange | ExitCode | Duration,
};

constexpr ModuleSensitivity operator|(ModuleSensitivity a, ModuleSensitivity b) noexcept
{
    return static_cast<ModuleSensitivity>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

constexpr ModuleSensitivity operator&(ModuleSensitivity a, ModuleSensitivity b) noexcept
{
    return static_cast<ModuleSensitivity>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

constexpr ModuleSensitivity& operator|=(ModuleSensitivity& a, ModuleSensitivity b) noexcept
{
    return a = a | b;
}

constexpr bool operator!=(ModuleSensitivity a, ModuleSensitivity b) noexcept
{
    return static_cast<uint8_t>(a) != static_cast<uint8_t>(b);
}

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
    std::string cwd;                              ///< Current working directory.
    std::string homePath;                         ///< User's home directory path.
    int lastExitCode = 0;                         ///< Exit code of the last command.
    std::chrono::milliseconds lastDuration { 0 }; ///< Duration of the last command.
    int terminalWidth = 80;                       ///< Terminal width in columns.
    bool isSSH = false;                           ///< Whether running inside an SSH session.
    std::string hostname;                         ///< Hostname of the machine.
    std::string username;                         ///< Current user's login name.
    tui::Theme const* theme = nullptr;            ///< Current TUI theme.
    ResolvedPromptColors const* resolvedColors =
        nullptr; ///< Resolved prompt colors (overrides merged with theme).
    FSharpPersistentState const* fsharpState = nullptr;   ///< F# persistent state (functions, bindings).
    OutputDefinitionRegistry const* outputDefs = nullptr; ///< Output definitions for structured commands.
    int cellPixelWidth = 0;                               ///< Cell width in pixels (0 if unknown).
    int cellPixelHeight = 0;                              ///< Cell height in pixels (0 if unknown).
    int shellLevel = 0;                                   ///< Shell nesting depth (0 = outermost).
    std::string currentInput;                             ///< Current input text being edited.

    /// @brief When set, overrides the static `PromptConfig::indicator` string for this render.
    ///
    /// Populated by the render pipeline when the user has assigned a function value to
    /// `shell_prompt_indicator` (resolved against the shell's F# persistent state).
    /// IndicatorModule consumes this preferentially over its configured static string.
    std::optional<std::string> indicatorOverride;
};

/// @brief Abstract interface for a pluggable prompt module.
///
/// Each module represents a single informational section of the prompt
/// (e.g., path, git status, exit code). Modules are evaluated in order
/// and their segments are rendered by the layout engine.
///
/// Modules declare their sensitivity via sensitivity() to control when they
/// are re-evaluated. Only modules sensitive to a given event are re-evaluated
/// when that event fires, avoiding unnecessary work.
class PromptModule
{
  public:
    virtual ~PromptModule() = default;

    /// @brief Returns the unique identifier for this module.
    [[nodiscard]] virtual std::string_view id() const noexcept = 0;

    /// @brief Returns the events that should trigger re-evaluation of this module.
    /// @return Bit flags indicating which context changes affect this module.
    [[nodiscard]] virtual ModuleSensitivity sensitivity() const { return ModuleSensitivity::ContextChange; }

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

    /// @brief Invalidates any internal cache held by this module.
    ///
    /// Called after a command completes to force modules to re-query their data
    /// on the next evaluation, regardless of TTL-based caching.
    virtual void invalidateCache() {}
};

} // namespace endo
