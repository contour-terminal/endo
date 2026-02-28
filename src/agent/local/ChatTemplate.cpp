// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <cctype>
#include <format>

#include <agent/local/ChatTemplate.hpp>
#include <nlohmann/json.hpp>

namespace endo::agent::local
{

namespace
{
    /// Converts a string to lowercase for case-insensitive comparison.
    [[nodiscard]] auto toLower(std::string_view input) -> std::string
    {
        auto result = std::string(input);
        std::ranges::transform(result, result.begin(), [](unsigned char ch) { return std::tolower(ch); });
        return result;
    }

    /// Builds a tool-use instruction block from the given tool definitions.
    /// @param tools The available tool definitions.
    /// @return A formatted instruction string for tool usage.
    [[nodiscard]] auto buildToolInstructions(std::span<ToolDefinition const> tools) -> std::string
    {
        auto result = std::string { "You have access to the following tools:\n" };
        for (auto const& tool: tools)
        {
            result += std::format(
                "\n{}: {}\nParameters: {}\n", tool.name, tool.description, tool.inputSchema.dump());
        }
        result += "\nTo use a tool, respond with:\n<tool_call>\n{\"name\": \"<tool_name>\", \"arguments\": "
                  "{<args>}}\n"
                  "</tool_call>\n";
        return result;
    }

    /// Returns the effective system message text, prepending tool instructions if tools are available.
    /// @param systemText The original system message text.
    /// @param tools The available tool definitions.
    /// @return The system text with tool instructions prepended when tools is non-empty.
    [[nodiscard]] auto effectiveSystemText(std::string const& systemText,
                                           std::span<ToolDefinition const> tools) -> std::string
    {
        if (tools.empty())
            return systemText;
        return buildToolInstructions(tools) + "\n" + systemText;
    }

    /// Extracts system message text from a message list.
    /// @param messages The conversation history.
    /// @return The concatenated text of all system messages, or empty string if none.
    [[nodiscard]] auto extractSystemText(std::span<ChatMessage const> messages) -> std::string
    {
        auto result = std::string {};
        for (auto const& msg: messages)
        {
            if (msg.role == Role::System)
            {
                if (!result.empty())
                    result += '\n';
                result += msg.textContent();
            }
        }
        return result;
    }

    [[nodiscard]] auto formatChatML(std::span<ChatMessage const> messages,
                                    std::span<ToolDefinition const> tools) -> std::string
    {
        auto result = std::string {};
        auto const systemText = extractSystemText(messages);
        auto const effective = effectiveSystemText(systemText, tools);

        if (!effective.empty())
            result += std::format("<|im_start|>system\n{}<|im_end|>\n", effective);

        for (auto const& msg: messages)
        {
            if (msg.role == Role::System)
                continue;
            auto const role = roleToString(msg.role);
            result += std::format("<|im_start|>{}\n{}<|im_end|>\n", role, msg.textContent());
        }

        result += "<|im_start|>assistant\n";
        return result;
    }

    [[nodiscard]] auto formatLlama3(std::span<ChatMessage const> messages,
                                    std::span<ToolDefinition const> tools) -> std::string
    {
        auto result = std::string { "<|begin_of_text|>" };
        auto const systemText = extractSystemText(messages);
        auto const effective = effectiveSystemText(systemText, tools);

        if (!effective.empty())
            result += std::format("<|start_header_id|>system<|end_header_id|>\n\n{}<|eot_id|>", effective);

        for (auto const& msg: messages)
        {
            if (msg.role == Role::System)
                continue;
            auto const role = roleToString(msg.role);
            result += std::format(
                "<|start_header_id|>{}<|end_header_id|>\n\n{}<|eot_id|>", role, msg.textContent());
        }

        result += "<|start_header_id|>assistant<|end_header_id|>\n\n";
        return result;
    }

    [[nodiscard]] auto formatMistral(std::span<ChatMessage const> messages,
                                     std::span<ToolDefinition const> tools) -> std::string
    {
        auto result = std::string {};
        auto const systemText = extractSystemText(messages);
        auto const effective = effectiveSystemText(systemText, tools);

        auto firstUser = true;
        for (auto const& msg: messages)
        {
            if (msg.role == Role::System)
                continue;

            if (msg.role == Role::User)
            {
                if (firstUser && !effective.empty())
                    result += std::format("[INST] {}\n{} [/INST]", effective, msg.textContent());
                else
                    result += std::format("[INST] {} [/INST]", msg.textContent());
                firstUser = false;
            }
            else if (msg.role == Role::Assistant)
            {
                result += std::format(" {}</s> ", msg.textContent());
            }
        }

        // If the last message was a user message, the [/INST] is already there.
        // If last was assistant followed by </s>, we need a trailing space for generation.
        return result;
    }

    [[nodiscard]] auto formatGemma(std::span<ChatMessage const> messages,
                                   std::span<ToolDefinition const> tools) -> std::string
    {
        auto result = std::string {};
        auto const systemText = extractSystemText(messages);
        auto const effective = effectiveSystemText(systemText, tools);

        auto systemMerged = false;
        for (auto const& msg: messages)
        {
            if (msg.role == Role::System)
                continue;

            if (msg.role == Role::User)
            {
                auto text = msg.textContent();
                if (!systemMerged && !effective.empty())
                {
                    text = effective + "\n" + text;
                    systemMerged = true;
                }
                result += std::format("<start_of_turn>user\n{}<end_of_turn>\n", text);
            }
            else if (msg.role == Role::Assistant)
            {
                result += std::format("<start_of_turn>model\n{}<end_of_turn>\n", msg.textContent());
            }
        }

        result += "<start_of_turn>model\n";
        return result;
    }

    [[nodiscard]] auto formatPhi3(std::span<ChatMessage const> messages,
                                  std::span<ToolDefinition const> tools) -> std::string
    {
        auto result = std::string {};
        auto const systemText = extractSystemText(messages);
        auto const effective = effectiveSystemText(systemText, tools);

        if (!effective.empty())
            result += std::format("<|system|>\n{}<|end|>\n", effective);

        for (auto const& msg: messages)
        {
            if (msg.role == Role::System)
                continue;

            if (msg.role == Role::User)
                result += std::format("<|user|>\n{}<|end|>\n", msg.textContent());
            else if (msg.role == Role::Assistant)
                result += std::format("<|assistant|>\n{}<|end|>\n", msg.textContent());
        }

        result += "<|assistant|>\n";
        return result;
    }

    [[nodiscard]] auto formatGeneric(std::span<ChatMessage const> messages,
                                     std::span<ToolDefinition const> tools) -> std::string
    {
        auto result = std::string {};
        auto const systemText = extractSystemText(messages);
        auto const effective = effectiveSystemText(systemText, tools);

        if (!effective.empty())
            result += std::format("### System:\n{}\n\n", effective);

        for (auto const& msg: messages)
        {
            if (msg.role == Role::System)
                continue;

            if (msg.role == Role::User)
                result += std::format("### User:\n{}\n\n", msg.textContent());
            else if (msg.role == Role::Assistant)
                result += std::format("### Assistant:\n{}\n\n", msg.textContent());
        }

        result += "### Assistant:\n";
        return result;
    }
} // namespace

auto chatTemplateFromString(std::string_view name) -> ChatTemplateFormat
{
    auto const lower = toLower(name);

    if (lower == "chatml")
        return ChatTemplateFormat::ChatML;
    if (lower == "llama3")
        return ChatTemplateFormat::Llama3;
    if (lower == "mistral")
        return ChatTemplateFormat::Mistral;
    if (lower == "gemma")
        return ChatTemplateFormat::Gemma;
    if (lower == "phi3")
        return ChatTemplateFormat::Phi3;
    if (lower == "qwen2")
        return ChatTemplateFormat::Qwen2;

    return ChatTemplateFormat::Generic;
}

auto formatPrompt(std::span<ChatMessage const> messages,
                  std::span<ToolDefinition const> tools,
                  ChatTemplateFormat format) -> std::string
{
    switch (format)
    {
        case ChatTemplateFormat::ChatML: return formatChatML(messages, tools);
        case ChatTemplateFormat::Llama3: return formatLlama3(messages, tools);
        case ChatTemplateFormat::Mistral: return formatMistral(messages, tools);
        case ChatTemplateFormat::Gemma: return formatGemma(messages, tools);
        case ChatTemplateFormat::Phi3: return formatPhi3(messages, tools);
        case ChatTemplateFormat::Qwen2: return formatChatML(messages, tools); // Qwen2 uses ChatML format.
        case ChatTemplateFormat::Generic: return formatGeneric(messages, tools);
    }
    return formatGeneric(messages, tools);
}

auto stopTokens(ChatTemplateFormat format) -> std::vector<std::string>
{
    switch (format)
    {
        case ChatTemplateFormat::ChatML:
        case ChatTemplateFormat::Qwen2: return { "<|im_end|>" };
        case ChatTemplateFormat::Llama3: return { "<|eot_id|>" };
        case ChatTemplateFormat::Mistral: return { "</s>" };
        case ChatTemplateFormat::Gemma: return { "<end_of_turn>" };
        case ChatTemplateFormat::Phi3: return { "<|end|>" };
        case ChatTemplateFormat::Generic: return { "### User:", "### System:" };
    }
    return { "### User:", "### System:" };
}

} // namespace endo::agent::local
