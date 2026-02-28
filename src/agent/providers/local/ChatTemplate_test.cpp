// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <agent/providers/local/ChatTemplate.hpp>

using namespace endo::agent;
using namespace endo::agent::local;

// =============================================================================
// chatTemplateFromString tests
// =============================================================================

TEST_CASE("agent.local.chat_template.from_string_chatml")
{
    CHECK(chatTemplateFromString("chatml") == ChatTemplateFormat::ChatML);
    CHECK(chatTemplateFromString("ChatML") == ChatTemplateFormat::ChatML);
    CHECK(chatTemplateFromString("CHATML") == ChatTemplateFormat::ChatML);
}

TEST_CASE("agent.local.chat_template.from_string_llama3")
{
    CHECK(chatTemplateFromString("llama3") == ChatTemplateFormat::Llama3);
    CHECK(chatTemplateFromString("Llama3") == ChatTemplateFormat::Llama3);
}

TEST_CASE("agent.local.chat_template.from_string_mistral")
{
    CHECK(chatTemplateFromString("mistral") == ChatTemplateFormat::Mistral);
    CHECK(chatTemplateFromString("MISTRAL") == ChatTemplateFormat::Mistral);
}

TEST_CASE("agent.local.chat_template.from_string_gemma")
{
    CHECK(chatTemplateFromString("gemma") == ChatTemplateFormat::Gemma);
}

TEST_CASE("agent.local.chat_template.from_string_phi3")
{
    CHECK(chatTemplateFromString("phi3") == ChatTemplateFormat::Phi3);
}

TEST_CASE("agent.local.chat_template.from_string_qwen2")
{
    CHECK(chatTemplateFromString("qwen2") == ChatTemplateFormat::Qwen2);
}

TEST_CASE("agent.local.chat_template.from_string_generic_fallback")
{
    CHECK(chatTemplateFromString("generic") == ChatTemplateFormat::Generic);
    CHECK(chatTemplateFromString("unknown_format") == ChatTemplateFormat::Generic);
    CHECK(chatTemplateFromString("") == ChatTemplateFormat::Generic);
}

// =============================================================================
// formatPrompt tests
// =============================================================================

TEST_CASE("agent.local.chat_template.format_prompt_chatml_single_message")
{
    auto messages = std::vector<ChatMessage> {};
    messages.push_back(ChatMessage::text(Role::User, "Hello"));

    auto const prompt = formatPrompt(messages, {}, ChatTemplateFormat::ChatML);

    CHECK(prompt.find("<|im_start|>user") != std::string::npos);
    CHECK(prompt.find("Hello") != std::string::npos);
    CHECK(prompt.find("<|im_end|>") != std::string::npos);
    CHECK(prompt.find("<|im_start|>assistant\n") != std::string::npos);
    // Should end with the assistant prompt ready for generation.
    CHECK(prompt.ends_with("<|im_start|>assistant\n"));
}

TEST_CASE("agent.local.chat_template.format_prompt_llama3_multi_turn")
{
    auto messages = std::vector<ChatMessage> {};
    messages.push_back(ChatMessage::text(Role::System, "You are a helpful assistant."));
    messages.push_back(ChatMessage::text(Role::User, "Hello"));
    messages.push_back(ChatMessage::text(Role::Assistant, "Hi there!"));
    messages.push_back(ChatMessage::text(Role::User, "How are you?"));

    auto const prompt = formatPrompt(messages, {}, ChatTemplateFormat::Llama3);

    CHECK(prompt.starts_with("<|begin_of_text|>"));
    CHECK(prompt.find("<|start_header_id|>system<|end_header_id|>") != std::string::npos);
    CHECK(prompt.find("You are a helpful assistant.") != std::string::npos);
    CHECK(prompt.find("<|start_header_id|>user<|end_header_id|>") != std::string::npos);
    CHECK(prompt.find("Hello") != std::string::npos);
    CHECK(prompt.find("Hi there!") != std::string::npos);
    CHECK(prompt.find("How are you?") != std::string::npos);
    CHECK(prompt.find("<|eot_id|>") != std::string::npos);
    CHECK(prompt.ends_with("<|start_header_id|>assistant<|end_header_id|>\n\n"));
}

TEST_CASE("agent.local.chat_template.format_prompt_chatml_with_tools")
{
    auto messages = std::vector<ChatMessage> {};
    messages.push_back(ChatMessage::text(Role::System, "You are an assistant."));
    messages.push_back(ChatMessage::text(Role::User, "Search for test"));

    auto tools = std::vector<ToolDefinition> {};
    tools.push_back(ToolDefinition {
        .name = "search",
        .description = "Search for files",
        .inputSchema = nlohmann::json { { "type", "object" },
                                        { "properties", { { "query", { { "type", "string" } } } } } },
    });

    auto const prompt = formatPrompt(messages, tools, ChatTemplateFormat::ChatML);

    // Tool instructions should be present in the system section.
    CHECK(prompt.find("You have access to the following tools:") != std::string::npos);
    CHECK(prompt.find("search: Search for files") != std::string::npos);
    CHECK(prompt.find("<tool_call>") != std::string::npos);
    // Original system message should also be present.
    CHECK(prompt.find("You are an assistant.") != std::string::npos);
}

TEST_CASE("agent.local.chat_template.format_prompt_phi3_with_system")
{
    auto messages = std::vector<ChatMessage> {};
    messages.push_back(ChatMessage::text(Role::System, "Be concise."));
    messages.push_back(ChatMessage::text(Role::User, "What is 2+2?"));

    auto const prompt = formatPrompt(messages, {}, ChatTemplateFormat::Phi3);

    CHECK(prompt.find("<|system|>") != std::string::npos);
    CHECK(prompt.find("Be concise.") != std::string::npos);
    CHECK(prompt.find("<|end|>") != std::string::npos);
    CHECK(prompt.find("<|user|>") != std::string::npos);
    CHECK(prompt.find("What is 2+2?") != std::string::npos);
    CHECK(prompt.ends_with("<|assistant|>\n"));
}

// =============================================================================
// stopTokens tests
// =============================================================================

TEST_CASE("agent.local.chat_template.stop_tokens_chatml")
{
    auto const tokens = stopTokens(ChatTemplateFormat::ChatML);
    REQUIRE_FALSE(tokens.empty());
    CHECK(tokens[0] == "<|im_end|>");
}

TEST_CASE("agent.local.chat_template.stop_tokens_llama3")
{
    auto const tokens = stopTokens(ChatTemplateFormat::Llama3);
    REQUIRE_FALSE(tokens.empty());
    CHECK(tokens[0] == "<|eot_id|>");
}

TEST_CASE("agent.local.chat_template.stop_tokens_mistral")
{
    auto const tokens = stopTokens(ChatTemplateFormat::Mistral);
    REQUIRE_FALSE(tokens.empty());
    CHECK(tokens[0] == "</s>");
}

TEST_CASE("agent.local.chat_template.stop_tokens_gemma")
{
    auto const tokens = stopTokens(ChatTemplateFormat::Gemma);
    REQUIRE_FALSE(tokens.empty());
    CHECK(tokens[0] == "<end_of_turn>");
}

TEST_CASE("agent.local.chat_template.stop_tokens_phi3")
{
    auto const tokens = stopTokens(ChatTemplateFormat::Phi3);
    REQUIRE_FALSE(tokens.empty());
    CHECK(tokens[0] == "<|end|>");
}

TEST_CASE("agent.local.chat_template.stop_tokens_qwen2")
{
    auto const tokens = stopTokens(ChatTemplateFormat::Qwen2);
    REQUIRE_FALSE(tokens.empty());
    CHECK(tokens[0] == "<|im_end|>");
}

TEST_CASE("agent.local.chat_template.stop_tokens_generic")
{
    auto const tokens = stopTokens(ChatTemplateFormat::Generic);
    REQUIRE(tokens.size() == 2);
    CHECK(tokens[0] == "### User:");
    CHECK(tokens[1] == "### System:");
}
