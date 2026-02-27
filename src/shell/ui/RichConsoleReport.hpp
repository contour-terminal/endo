// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/CoreVM.hpp>

#include <string>
#include <string_view>

namespace endo
{

/// Controls whether ANSI color output is used.
enum class ColorMode
{
    Auto,     ///< Detect via isatty() and NO_COLOR environment variable.
    Enabled,  ///< Always use color.
    Disabled, ///< Never use color.
};

/// Formats a diagnostic message in Rust-style output with optional ANSI color.
///
/// @param message The diagnostic message to format.
/// @param useColor Whether to include ANSI escape sequences for colored output.
/// @return The formatted diagnostic string (without trailing newline).
[[nodiscard]] std::string formatDiagnostic(CoreVM::diagnostics::Message const& message, bool useColor);

/// A rich diagnostic report that outputs Rust-compiler-style colored error messages to stderr.
///
/// Extends the base Report class to provide:
/// - Colored error/warning labels
/// - Syntax-highlighted source context
/// - Curly underline carets at the error location
/// - Hint lines with suggestions
///
/// Color output is automatically detected via isatty(STDERR_FILENO) and the NO_COLOR env var.
class RichConsoleReport: public CoreVM::diagnostics::Report
{
  public:
    RichConsoleReport();

    /// Sets the source text for context snippet extraction.
    ///
    /// When a diagnostic message lacks a contextSnippet but has a valid source location,
    /// the report will extract the relevant source line from this text.
    /// @param source The full source text being compiled.
    void setSourceText(std::string_view source);

    void push_back(CoreVM::diagnostics::Message message) override;
    [[nodiscard]] bool containsFailures() const noexcept override;

  private:
    size_t _errorCount = 0;
    bool _useColor = false;
    std::string_view _sourceText;
};

/// A diagnostic report that buffers formatted messages in memory instead of writing to stderr.
///
/// Used when compilation errors must be captured and displayed later (e.g., during
/// tab-completion callbacks where direct stderr output would corrupt the TUI prompt).
class BufferingConsoleReport: public CoreVM::diagnostics::Report
{
  public:
    BufferingConsoleReport();
    explicit BufferingConsoleReport(ColorMode colorMode);

    /// Sets the source text for context snippet extraction.
    /// @param source The full source text being compiled.
    void setSourceText(std::string_view source);

    void push_back(CoreVM::diagnostics::Message message) override;
    [[nodiscard]] bool containsFailures() const noexcept override;

    /// Returns the buffered formatted diagnostic messages.
    [[nodiscard]] std::vector<std::string> const& formattedMessages() const noexcept;

    /// Returns whether any messages have been buffered.
    [[nodiscard]] bool hasMessages() const noexcept;

  private:
    size_t _errorCount = 0;
    bool _useColor = false;
    std::string_view _sourceText;
    std::vector<std::string> _formattedMessages;
};

} // namespace endo
