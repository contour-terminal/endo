// SPDX-License-Identifier: Apache-2.0
#include <format>

#include <agent/AgentSession.hpp>
#include <agent/tools/ExploreTool.hpp>
#include <agent/tools/GitTool.hpp>
#include <agent/tools/GlobTool.hpp>
#include <agent/tools/GrepTool.hpp>
#include <agent/tools/ListDirectoryTool.hpp>
#include <agent/tools/ReadFileTool.hpp>
#include <agent/tools/SearchTool.hpp>
#include <agent/tools/ToolRegistry.hpp>

namespace endo::agent
{

ExploreTool::ExploreTool(LlmProvider& provider, ShellExecuteCallback shellExecCb, ExploreConfig config):
    _provider(provider), _shellExecCb(std::move(shellExecCb)), _config(config)
{
}

void ExploreTool::setSystemPrompt(std::string systemPrompt)
{
    _systemPrompt = std::move(systemPrompt);
}

auto ExploreTool::name() const noexcept -> std::string_view
{
    return "explore";
}

auto ExploreTool::definition() const -> ToolDefinition
{
    return ToolDefinition {
        .name = "explore",
        .description = "Explores the codebase to answer a question using an isolated sub-agent with "
                       "read-only tools (read_file, glob, grep, git). The sub-agent's intermediate "
                       "results are discarded — only a concise summary is returned. Use this to keep "
                       "the main conversation context clean when investigating code.",
        .inputSchema =
            nlohmann::json {
                { "type", "object" },
                { "properties",
                  nlohmann::json {
                      { "question",
                        nlohmann::json {
                            { "type", "string" },
                            { "description", "The exploration question to answer about the codebase." },
                        } },
                      { "scope",
                        nlohmann::json {
                            { "type", "string" },
                            { "description",
                              "Optional context to narrow the search scope (e.g. directory, module, "
                              "topic)." },
                        } },
                  } },
                { "required", nlohmann::json::array({ "question" }) },
            },
    };
}

auto ExploreTool::execute(nlohmann::json const& arguments) -> std::expected<ToolResult, ToolError>
{
    // Extract required "question" parameter
    auto const questionIt = arguments.find("question");
    if (questionIt == arguments.end() || !questionIt->is_string() || questionIt->get<std::string>().empty())
        return std::unexpected(ToolError { .message = "Missing required parameter: question" });

    auto question = questionIt->get<std::string>();

    // Optionally prepend scope context
    if (auto const scopeIt = arguments.find("scope"); scopeIt != arguments.end() && scopeIt->is_string())
    {
        auto const& scope = scopeIt->get<std::string>();
        if (!scope.empty())
            question = std::format("Context/scope: {}\n\n{}", scope, question);
    }

    // Create a local tool registry with read-only tools only
    auto innerRegistry = ToolRegistry {};
    innerRegistry.registerTool(std::make_unique<ReadFileTool>());
    innerRegistry.registerTool(std::make_unique<GlobTool>());
    innerRegistry.registerTool(std::make_unique<GrepTool>());
    innerRegistry.registerTool(std::make_unique<SearchTool>());
    innerRegistry.registerTool(std::make_unique<ListDirectoryTool>());
    innerRegistry.registerTool(std::make_unique<GitTool>(_shellExecCb));

    // Create an isolated inner agent session
    auto innerSession = AgentSession(_provider);
    innerSession.setSystemPrompt(_systemPrompt);
    innerSession.setToolRegistry(&innerRegistry);
    innerSession.setMaxToolIterations(_config.maxTurns);

    // Run the inner agent — no streaming to the outer conversation
    auto result = innerSession.processMessage(question, nullptr);

    if (!result.has_value())
    {
        return std::unexpected(
            ToolError { .message = std::format("Explore sub-agent failed: {}", result.error().message) });
    }

    return ToolResult { .content = *result, .isError = false };
}

} // namespace endo::agent
