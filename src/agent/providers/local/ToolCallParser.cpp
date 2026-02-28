// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <format>
#include <string>
#include <string_view>

#include <agent/providers/local/ToolCallParser.hpp>
#include <nlohmann/json.hpp>

namespace endo::agent::local
{

namespace
{
    /// Global counter for generating unique tool call IDs.
    auto nextCallId() -> std::string
    {
        static auto counter = 0;
        return std::format("call_{:04d}", ++counter);
    }

    /// Checks whether a tool name exists in the provided tool definitions.
    [[nodiscard]] auto isKnownTool(std::string_view name, std::span<ToolDefinition const> tools) noexcept
        -> bool
    {
        return std::ranges::any_of(tools, [name](auto const& tool) { return tool.name == name; });
    }

    /// Attempts to extract a ToolCall from a JSON object.
    ///
    /// The JSON must contain "name" (string) and "arguments" (object) fields.
    /// @param json The JSON object to inspect.
    /// @return A ToolCall if extraction succeeds, std::nullopt otherwise.
    [[nodiscard]] auto tryExtractToolCall(nlohmann::json const& json) -> std::optional<ToolCall>
    {
        if (!json.is_object())
            return std::nullopt;
        if (!json.contains("name") || !json["name"].is_string())
            return std::nullopt;
        if (!json.contains("arguments"))
            return std::nullopt;

        auto call = ToolCall {};
        call.id = nextCallId();
        call.name = json["name"].get<std::string>();
        call.arguments = json["arguments"];
        return call;
    }

    /// Extracts tool calls from `<tool_call>...</tool_call>` XML tags in the output.
    ///
    /// @param output Raw model output text.
    /// @param tools Available tool definitions for validation.
    /// @param result Output parameter for the parse result.
    /// @return true if at least one XML tag was found (regardless of parse success).
    auto tryExtractXmlToolCalls(std::string_view output,
                                std::span<ToolDefinition const> tools,
                                ToolCallParseResult& result) -> bool
    {
        constexpr auto openTag = std::string_view { "<tool_call>" };
        constexpr auto closeTag = std::string_view { "</tool_call>" };

        auto foundAnyTag = false;
        auto pos = size_t { 0 };
        auto lastEnd = size_t { 0 };
        auto const text = std::string { output };

        while (pos < text.size())
        {
            auto const tagStart = text.find(openTag, pos);
            if (tagStart == std::string::npos)
                break;

            foundAnyTag = true;

            // Collect text before this tag.
            if (tagStart > lastEnd)
            {
                auto const segment = std::string_view { text }.substr(lastEnd, tagStart - lastEnd);
                for (auto const ch: segment)
                {
                    if (ch != '\n' && ch != '\r' && ch != ' ')
                    {
                        result.textContent += segment;
                        break;
                    }
                }
            }

            auto const contentStart = tagStart + openTag.size();
            auto const tagEnd = text.find(closeTag, contentStart);
            if (tagEnd == std::string::npos)
            {
                result.hadParsingErrors = true;
                pos = contentStart;
                lastEnd = contentStart;
                continue;
            }

            auto const content = text.substr(contentStart, tagEnd - contentStart);
            lastEnd = tagEnd + closeTag.size();
            pos = lastEnd;

            try
            {
                auto const json = nlohmann::json::parse(content);
                if (auto call = tryExtractToolCall(json))
                {
                    // Validation is informational; unknown tools are still extracted.
                    [[maybe_unused]] auto const known = isKnownTool(call->name, tools);
                    result.toolCalls.push_back(std::move(*call));
                }
                else
                {
                    result.hadParsingErrors = true;
                }
            }
            catch (nlohmann::json::parse_error const&)
            {
                result.hadParsingErrors = true;
            }
        }

        // Collect any trailing text after the last tag.
        if (foundAnyTag && lastEnd < text.size())
        {
            auto const trailing = std::string_view { text }.substr(lastEnd);
            for (auto const ch: trailing)
            {
                if (ch != '\n' && ch != '\r' && ch != ' ')
                {
                    result.textContent += trailing;
                    break;
                }
            }
        }

        return foundAnyTag;
    }

    /// Extracts tool calls from ` ```json ... ``` ` fenced code blocks.
    ///
    /// @param output Raw model output text.
    /// @param tools Available tool definitions for validation.
    /// @param result Output parameter for the parse result.
    /// @return true if at least one code block was found.
    auto tryExtractCodeBlockToolCalls(std::string_view output,
                                      std::span<ToolDefinition const> tools,
                                      ToolCallParseResult& result) -> bool
    {
        constexpr auto openFence = std::string_view { "```json" };
        constexpr auto closeFence = std::string_view { "```" };

        auto foundAnyBlock = false;
        auto pos = size_t { 0 };
        auto lastEnd = size_t { 0 };
        auto const text = std::string { output };

        while (pos < text.size())
        {
            auto const blockStart = text.find(openFence, pos);
            if (blockStart == std::string::npos)
                break;

            foundAnyBlock = true;

            // Collect text before this block.
            if (blockStart > lastEnd)
            {
                auto const segment = std::string_view { text }.substr(lastEnd, blockStart - lastEnd);
                for (auto const ch: segment)
                {
                    if (ch != '\n' && ch != '\r' && ch != ' ')
                    {
                        result.textContent += segment;
                        break;
                    }
                }
            }

            auto const contentStart = blockStart + openFence.size();
            auto const blockEnd = text.find(closeFence, contentStart);
            if (blockEnd == std::string::npos)
            {
                result.hadParsingErrors = true;
                pos = contentStart;
                lastEnd = contentStart;
                continue;
            }

            auto const content = text.substr(contentStart, blockEnd - contentStart);
            lastEnd = blockEnd + closeFence.size();
            pos = lastEnd;

            try
            {
                auto const json = nlohmann::json::parse(content);

                if (json.is_array())
                {
                    for (auto const& element: json)
                    {
                        if (auto call = tryExtractToolCall(element))
                        {
                            [[maybe_unused]] auto const known = isKnownTool(call->name, tools);
                            result.toolCalls.push_back(std::move(*call));
                        }
                    }
                }
                else if (auto call = tryExtractToolCall(json))
                {
                    [[maybe_unused]] auto const known = isKnownTool(call->name, tools);
                    result.toolCalls.push_back(std::move(*call));
                }
            }
            catch (nlohmann::json::parse_error const&)
            {
                result.hadParsingErrors = true;
            }
        }

        // Collect trailing text.
        if (foundAnyBlock && lastEnd < text.size())
        {
            auto const trailing = std::string_view { text }.substr(lastEnd);
            for (auto const ch: trailing)
            {
                if (ch != '\n' && ch != '\r' && ch != ' ')
                {
                    result.textContent += trailing;
                    break;
                }
            }
        }

        return foundAnyBlock;
    }

    /// Extracts tool calls from inline JSON objects in the output.
    ///
    /// Scans for top-level `{...}` brace-balanced objects that contain
    /// "name" and "arguments" fields.
    ///
    /// @param output Raw model output text.
    /// @param tools Available tool definitions for validation.
    /// @param result Output parameter for the parse result.
    /// @return true if at least one inline JSON tool call was found.
    auto tryExtractInlineJsonToolCalls(std::string_view output,
                                       std::span<ToolDefinition const> tools,
                                       ToolCallParseResult& result) -> bool
    {
        auto foundAny = false;
        auto pos = size_t { 0 };
        auto lastEnd = size_t { 0 };
        auto const text = std::string { output };

        while (pos < text.size())
        {
            auto const braceStart = text.find('{', pos);
            if (braceStart == std::string::npos)
                break;

            // Find the matching closing brace (brace-balanced).
            auto depth = 0;
            auto braceEnd = braceStart;
            auto inString = false;
            auto escaped = false;

            for (auto i = braceStart; i < text.size(); ++i)
            {
                auto const ch = text[i];
                if (escaped)
                {
                    escaped = false;
                    continue;
                }
                if (ch == '\\' && inString)
                {
                    escaped = true;
                    continue;
                }
                if (ch == '"')
                {
                    inString = !inString;
                    continue;
                }
                if (inString)
                    continue;

                if (ch == '{')
                    ++depth;
                else if (ch == '}')
                {
                    --depth;
                    if (depth == 0)
                    {
                        braceEnd = i;
                        break;
                    }
                }
            }

            if (depth != 0)
            {
                // Unbalanced braces; skip past this opening brace.
                pos = braceStart + 1;
                continue;
            }

            auto const candidate = text.substr(braceStart, braceEnd - braceStart + 1);

            try
            {
                auto const json = nlohmann::json::parse(candidate);
                if (auto call = tryExtractToolCall(json))
                {
                    // Collect text before this JSON.
                    if (braceStart > lastEnd)
                    {
                        auto const segment = std::string_view { text }.substr(lastEnd, braceStart - lastEnd);
                        for (auto const ch: segment)
                        {
                            if (ch != '\n' && ch != '\r' && ch != ' ')
                            {
                                result.textContent += segment;
                                break;
                            }
                        }
                    }

                    [[maybe_unused]] auto const known = isKnownTool(call->name, tools);
                    result.toolCalls.push_back(std::move(*call));
                    foundAny = true;
                    lastEnd = braceEnd + 1;
                }
            }
            catch (nlohmann::json::parse_error const&)
            {
                // Not valid JSON; skip past this brace.
            }

            pos = braceEnd + 1;
        }

        // Collect trailing text.
        if (foundAny && lastEnd < text.size())
        {
            auto const trailing = std::string_view { text }.substr(lastEnd);
            for (auto const ch: trailing)
            {
                if (ch != '\n' && ch != '\r' && ch != ' ')
                {
                    result.textContent += trailing;
                    break;
                }
            }
        }

        return foundAny;
    }

} // namespace

auto parseToolCalls(std::string_view output, std::span<ToolDefinition const> tools) -> ToolCallParseResult
{
    auto result = ToolCallParseResult {};

    // Priority 1: XML tags.
    if (tryExtractXmlToolCalls(output, tools, result))
        return result;

    // Priority 2: JSON code blocks.
    if (tryExtractCodeBlockToolCalls(output, tools, result))
        return result;

    // Priority 3: Inline JSON objects.
    if (tryExtractInlineJsonToolCalls(output, tools, result))
        return result;

    // Priority 4: Plain text fallback.
    result.textContent = std::string { output };
    return result;
}

} // namespace endo::agent::local
