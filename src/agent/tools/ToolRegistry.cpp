// SPDX-License-Identifier: Apache-2.0
#include <format>

#include <agent/tools/ToolRegistry.hpp>

namespace endo::agent
{

void ToolRegistry::registerTool(std::unique_ptr<AgentTool> tool)
{
    auto const toolName = std::string(tool->name());
    auto const index = _tools.size();
    _tools.push_back(std::move(tool));
    _nameIndex[toolName] = index;
}

auto ToolRegistry::findTool(std::string_view name) const -> AgentTool*
{
    auto const it = _nameIndex.find(std::string(name));
    if (it == _nameIndex.end())
        return nullptr;
    return _tools[it->second].get();
}

auto ToolRegistry::definitions() const -> std::vector<ToolDefinition>
{
    auto result = std::vector<ToolDefinition> {};
    result.reserve(_tools.size());
    for (auto const& tool: _tools)
        result.push_back(tool->definition());
    return result;
}

auto ToolRegistry::definitions(ToolFilter const& filter) const -> std::vector<ToolDefinition>
{
    auto result = std::vector<ToolDefinition> {};
    for (auto const& tool: _tools)
        if (filter(tool->name()))
            result.push_back(tool->definition());
    return result;
}

auto ToolRegistry::execute(ToolCall const& call) -> ToolResult
{
    auto* tool = findTool(call.name);
    if (!tool)
    {
        return ToolResult {
            .callId = call.id,
            .content = std::format("Unknown tool: {}", call.name),
            .isError = true,
        };
    }

    auto result = tool->execute(call.arguments);
    if (!result.has_value())
    {
        return ToolResult {
            .callId = call.id,
            .content = std::format("Tool error: {}", result.error().message),
            .isError = true,
        };
    }

    auto toolResult = std::move(result.value());
    toolResult.callId = call.id;
    return toolResult;
}

auto ToolRegistry::size() const noexcept -> size_t
{
    return _tools.size();
}

} // namespace endo::agent
