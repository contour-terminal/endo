// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <agent/AgentSession.hpp>
#include <agent/tools/ToolRegistry.hpp>

using namespace endo::agent;

namespace
{

/// Mock LLM provider for testing AgentSession without network calls.
class MockProvider final: public LlmProvider
{
  public:
    std::string responseText = "Mock response";
    bool shouldFail = false;
    ProviderError failError { ProviderErrorCode::NetworkError, "connection failed", 500 };

    /// When set, the first call to generate() returns these tool calls.
    /// Subsequent calls return responseText as normal.
    std::vector<ToolCall> pendingToolCalls;
    int generateCallCount = 0;

    [[nodiscard]] auto generate(std::span<ChatMessage const> messages,
                                std::span<ToolDefinition const> /*tools*/,
                                StreamCallback streamCb)
        -> std::expected<GenerateResult, ProviderError> override
    {
        lastMessages.assign(messages.begin(), messages.end());
        ++generateCallCount;

        if (shouldFail)
            return std::unexpected(failError);

        // Stream tokens if callback provided
        if (streamCb)
            streamCb(responseText);

        auto result = GenerateResult {};

        if (!pendingToolCalls.empty())
        {
            // Return tool calls on first call, then clear them
            result.toolCalls = std::move(pendingToolCalls);
            pendingToolCalls.clear();

            // Include ToolUseBlocks in content (as real providers do)
            for (auto const& tc: result.toolCalls)
            {
                result.content.emplace_back(ToolUseBlock {
                    .id = tc.id,
                    .name = tc.name,
                    .arguments = tc.arguments,
                });
            }
        }
        else
        {
            result.content.emplace_back(TextBlock { .text = responseText });
        }

        return result;
    }

    [[nodiscard]] auto supportsToolUse() const noexcept -> bool override { return false; }

    [[nodiscard]] auto supportsImageInput() const noexcept -> bool override { return false; }

    [[nodiscard]] auto supportsImageOutput() const noexcept -> bool override { return false; }

    [[nodiscard]] auto contextSize() const noexcept -> size_t override { return 8192; }

    [[nodiscard]] auto modelInfo() const -> ModelInfo override
    {
        return ModelInfo { .providerName = "mock", .modelName = "mock-1" };
    }

    std::vector<ChatMessage> lastMessages;
};

/// Mock tool for testing the tool loop.
class MockTool final: public AgentTool
{
  public:
    std::string resultContent = "tool output";

    [[nodiscard]] auto name() const noexcept -> std::string_view override { return "mock_tool"; }

    [[nodiscard]] auto definition() const -> ToolDefinition override
    {
        return ToolDefinition {
            .name = "mock_tool",
            .description = "A mock tool",
            .inputSchema = nlohmann::json { { "type", "object" } },
        };
    }

    [[nodiscard]] auto execute(nlohmann::json const& /*arguments*/)
        -> std::expected<ToolResult, ToolError> override
    {
        return ToolResult { .content = resultContent, .isError = false };
    }
};

} // namespace

TEST_CASE("AgentSession.process_message_returns_response", "[agent]")
{
    auto provider = MockProvider {};
    provider.responseText = "Hello! How can I help?";
    auto session = AgentSession(provider);

    auto result = session.processMessage("Hi there", nullptr);

    REQUIRE(result.has_value());
    CHECK(*result == "Hello! How can I help?");
}

TEST_CASE("AgentSession.adds_messages_to_history", "[agent]")
{
    auto provider = MockProvider {};
    provider.responseText = "Response 1";
    auto session = AgentSession(provider);

    auto result = session.processMessage("Question 1", nullptr);
    REQUIRE(result.has_value());

    CHECK(session.history().size() == 2);
    CHECK(session.history().messages()[0].role == Role::User);
    CHECK(session.history().messages()[0].textContent() == "Question 1");
    CHECK(session.history().messages()[1].role == Role::Assistant);
}

TEST_CASE("AgentSession.preserves_history_across_calls", "[agent]")
{
    auto provider = MockProvider {};
    auto session = AgentSession(provider);

    provider.responseText = "First response";
    (void) session.processMessage("First question", nullptr);

    provider.responseText = "Second response";
    (void) session.processMessage("Second question", nullptr);

    CHECK(session.history().size() == 4);
    // Provider receives conversation history at time of generate() call
    // (before the second assistant response is appended)
    CHECK(provider.lastMessages.size() == 3);
}

TEST_CASE("AgentSession.streaming_callback_called", "[agent]")
{
    auto provider = MockProvider {};
    provider.responseText = "Streamed token";
    auto session = AgentSession(provider);

    auto receivedTokens = std::string {};
    auto result = session.processMessage("Hello", [&](std::string_view token) { receivedTokens += token; });

    REQUIRE(result.has_value());
    CHECK(receivedTokens == "Streamed token");
}

TEST_CASE("AgentSession.error_propagation", "[agent]")
{
    auto provider = MockProvider {};
    provider.shouldFail = true;
    auto session = AgentSession(provider);

    auto result = session.processMessage("Hello", nullptr);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == AgentErrorCode::ProviderError);
    CHECK(result.error().message.find("connection failed") != std::string::npos);
}

TEST_CASE("AgentSession.system_prompt", "[agent]")
{
    auto provider = MockProvider {};
    provider.responseText = "OK";
    auto session = AgentSession(provider);

    session.setSystemPrompt("You are helpful.");
    (void) session.processMessage("Hello", nullptr);

    // System prompt should be first in history
    CHECK(session.history().messages()[0].role == Role::System);
    CHECK(session.history().messages()[0].textContent() == "You are helpful.");
    // Provider should also receive the system prompt
    CHECK(provider.lastMessages[0].role == Role::System);
}

TEST_CASE("AgentSession.reset_clears_history", "[agent]")
{
    auto provider = MockProvider {};
    provider.responseText = "Response";
    auto session = AgentSession(provider);

    (void) session.processMessage("Hello", nullptr);
    CHECK_FALSE(session.history().empty());

    session.reset();
    CHECK(session.history().empty());
}

TEST_CASE("AgentSession.tool_loop_single_round_trip", "[agent]")
{
    auto provider = MockProvider {};
    provider.responseText = "Done using tool";
    auto session = AgentSession(provider);

    // Set up tool registry
    auto registry = ToolRegistry {};
    auto mockTool = std::make_unique<MockTool>();
    mockTool->resultContent = "file contents here";
    registry.registerTool(std::move(mockTool));
    session.setToolRegistry(&registry);

    // First generate returns tool call, second returns text
    provider.pendingToolCalls = { ToolCall {
        .id = "call-1",
        .name = "mock_tool",
        .arguments = nlohmann::json { { "path", "/test.txt" } },
    } };

    auto result = session.processMessage("Read file", nullptr);

    REQUIRE(result.has_value());
    CHECK(*result == "Done using tool");
    CHECK(provider.generateCallCount == 2); // First call (tool), second call (text)

    // History should contain: user, assistant (tool use), user (tool result), assistant (text)
    CHECK(session.history().size() == 4);

    // Verify the tool result message is in history
    auto const& toolResultMsg = session.history().messages()[2];
    CHECK(toolResultMsg.role == Role::User);
    auto const* toolResult = std::get_if<ToolResultBlock>(&toolResultMsg.content[0]);
    REQUIRE(toolResult != nullptr);
    CHECK(toolResult->toolUseId == "call-1");
    CHECK(toolResult->content == "file contents here");
}

TEST_CASE("AgentSession.tool_loop_max_iterations", "[agent]")
{
    auto provider = MockProvider {};
    auto session = AgentSession(provider);

    auto registry = ToolRegistry {};
    registry.registerTool(std::make_unique<MockTool>());
    session.setToolRegistry(&registry);
    session.setMaxToolIterations(3);

    // Provider always returns tool calls (never a text response)
    // We need to replenish pendingToolCalls after each generate call
    auto callCount = 0;
    // Override: mock always returns tool calls
    provider.pendingToolCalls = { ToolCall { .id = "c1", .name = "mock_tool", .arguments = {} } };

    // To make this work, we need the provider to always return tool calls.
    // The mock clears pendingToolCalls after first use, so we need a different approach.
    // Instead, let's use a custom provider that always returns tool calls.
    struct AlwaysToolProvider final: public LlmProvider
    {
        [[nodiscard]] auto generate(std::span<ChatMessage const>,
                                    std::span<ToolDefinition const>,
                                    StreamCallback) -> std::expected<GenerateResult, ProviderError> override
        {
            auto result = GenerateResult {};
            result.toolCalls = { ToolCall { .id = "tc", .name = "mock_tool", .arguments = {} } };
            result.content.emplace_back(ToolUseBlock { .id = "tc", .name = "mock_tool", .arguments = {} });
            return result;
        }

        [[nodiscard]] auto supportsToolUse() const noexcept -> bool override { return true; }

        [[nodiscard]] auto supportsImageInput() const noexcept -> bool override { return false; }

        [[nodiscard]] auto supportsImageOutput() const noexcept -> bool override { return false; }

        [[nodiscard]] auto contextSize() const noexcept -> size_t override { return 8192; }

        [[nodiscard]] auto modelInfo() const -> ModelInfo override
        {
            return ModelInfo { .providerName = "mock", .modelName = "mock-1" };
        }
    };

    auto alwaysToolProvider = AlwaysToolProvider {};
    auto limitSession = AgentSession(alwaysToolProvider);
    limitSession.setToolRegistry(&registry);
    limitSession.setMaxToolIterations(3);

    auto result = limitSession.processMessage("Hello", nullptr);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == AgentErrorCode::ToolLoopExceeded);
    CHECK(result.error().message.find("3") != std::string::npos);
}

TEST_CASE("AgentSession.no_registry_no_tools", "[agent]")
{
    auto provider = MockProvider {};
    provider.responseText = "Plain response";
    auto session = AgentSession(provider);

    // No tool registry set — should work like before
    auto result = session.processMessage("Hello", nullptr);

    REQUIRE(result.has_value());
    CHECK(*result == "Plain response");
    CHECK(provider.generateCallCount == 1);
}

TEST_CASE("AgentSession.tool_status_callback_fires", "[agent]")
{
    auto provider = MockProvider {};
    provider.responseText = "Done";
    auto session = AgentSession(provider);

    auto registry = ToolRegistry {};
    registry.registerTool(std::make_unique<MockTool>());
    session.setToolRegistry(&registry);

    auto statusToolNames = std::vector<std::string> {};
    session.setToolStatusCallback([&](std::string_view toolName) { statusToolNames.emplace_back(toolName); });

    provider.pendingToolCalls = { ToolCall { .id = "c1", .name = "mock_tool", .arguments = {} } };

    (void) session.processMessage("Test", nullptr);

    REQUIRE(statusToolNames.size() == 1);
    CHECK(statusToolNames[0] == "mock_tool");
}
