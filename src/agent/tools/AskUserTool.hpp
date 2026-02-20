// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <functional>
#include <string>
#include <vector>

#include <agent/tools/AgentTool.hpp>

namespace endo::agent
{

/// A question to present to the user during agent execution.
struct UserQuestion
{
    std::string text;                 ///< The question text.
    std::vector<std::string> options; ///< Optional choices (empty for free-text input).
    bool multiSelect = false;         ///< Allow selecting multiple options.
    bool allowOther = true;           ///< Show "Other..." for free-text fallback.
};

/// The user's answer to a question.
struct UserAnswer
{
    std::string answer;     ///< The answer text.
    bool cancelled = false; ///< Whether the user cancelled instead of answering.
};

/// Callback type for asking the user a question.
using AskUserCallback = std::function<UserAnswer(UserQuestion const&)>;

/// Tool for asking the user clarifying questions during agent execution.
///
/// Input: { question: string (required), options?: string[] }
/// - question: The question to ask the user.
/// - options: Optional choices (2–6 items). Omit for free-text input.
///
/// Returns the user's answer as text, or an error if cancelled.
class AskUserTool final: public AgentTool
{
  public:
    /// @brief Constructs an ask-user tool with the given callback.
    /// @param askCallback Callback that presents the question to the user and returns their answer.
    explicit AskUserTool(AskUserCallback askCallback);

    [[nodiscard]] auto name() const noexcept -> std::string_view override;
    [[nodiscard]] auto definition() const -> ToolDefinition override;
    [[nodiscard]] auto execute(nlohmann::json const& arguments)
        -> std::expected<ToolResult, ToolError> override;

  private:
    AskUserCallback _askCallback;
};

} // namespace endo::agent
