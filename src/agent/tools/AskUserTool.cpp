// SPDX-License-Identifier: Apache-2.0
#include <agent/tools/AskUserTool.hpp>

namespace endo::agent
{

AskUserTool::AskUserTool(AskUserCallback askCallback): _askCallback(std::move(askCallback))
{
}

auto AskUserTool::name() const noexcept -> std::string_view
{
    return "ask_user";
}

auto AskUserTool::definition() const -> ToolDefinition
{
    return ToolDefinition {
        .name = "ask_user",
        .description = "Asks the user a clarifying question and returns their answer. "
                       "Use this when you need user input to proceed, such as choosing "
                       "between approaches or clarifying requirements.",
        .inputSchema =
            nlohmann::json {
                { "type", "object" },
                { "properties",
                  nlohmann::json {
                      { "question",
                        nlohmann::json {
                            { "type", "string" },
                            { "description", "The question to ask the user." },
                        } },
                      { "options",
                        nlohmann::json {
                            { "type", "array" },
                            { "items", nlohmann::json { { "type", "string" } } },
                            { "description", "Optional choices (2-6 items). Omit for free-text input." },
                        } },
                  } },
                { "required", nlohmann::json::array({ "question" }) },
            },
    };
}

auto AskUserTool::execute(nlohmann::json const& arguments) -> std::expected<ToolResult, ToolError>
{
    if (!_askCallback)
        return std::unexpected(ToolError { .message = "No user interaction callback configured" });

    auto const question = arguments.value("question", std::string {});
    if (question.empty())
        return std::unexpected(ToolError { .message = "Missing required parameter: question" });

    // Parse and validate options if present
    auto options = std::vector<std::string> {};
    if (arguments.contains("options") && arguments["options"].is_array())
    {
        for (auto const& opt: arguments["options"])
        {
            if (!opt.is_string() || opt.get<std::string>().empty())
                return std::unexpected(ToolError { .message = "All options must be non-empty strings" });
            options.push_back(opt.get<std::string>());
        }

        if (options.size() < 2)
            return std::unexpected(ToolError { .message = "Options must contain at least 2 choices" });

        if (options.size() > 6)
            return std::unexpected(ToolError { .message = "Options must contain at most 6 choices" });
    }

    auto const answer = _askCallback(UserQuestion { .text = question, .options = std::move(options) });

    if (answer.cancelled)
        return ToolResult { .content = "User cancelled the question.", .isError = true };

    return ToolResult { .content = answer.answer, .isError = false };
}

} // namespace endo::agent
