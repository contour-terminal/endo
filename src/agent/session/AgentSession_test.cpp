// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include <agent/conversation/ConversationCompactor.hpp>
#include <agent/session/AgentSession.hpp>
#include <agent/tools/SubmitPlanTool.hpp>
#include <agent/tools/ToolRegistry.hpp>
#include <agent/tracing/AgentTracer.hpp>

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

    /// Optional token usage to return from generate().
    std::optional<TokenUsage> mockUsage;

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
            (void) streamCb(responseText);

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

        result.usage = mockUsage;

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

/// Mock tool that returns a large result for truncation testing.
class LargeResultTool final: public AgentTool
{
  public:
    size_t resultSize = 50000;

    [[nodiscard]] auto name() const noexcept -> std::string_view override { return "large_tool"; }

    [[nodiscard]] auto definition() const -> ToolDefinition override
    {
        return ToolDefinition {
            .name = "large_tool",
            .description = "A tool that returns large results",
            .inputSchema = nlohmann::json { { "type", "object" } },
        };
    }

    [[nodiscard]] auto execute(nlohmann::json const& /*arguments*/)
        -> std::expected<ToolResult, ToolError> override
    {
        return ToolResult { .content = std::string(resultSize, 'x'), .isError = false };
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
    auto result = session.processMessage("Hello", [&](std::string_view token) -> bool {
        receivedTokens += token;
        return true;
    });

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
    session.setToolStatusCallback([&](ToolCall const& call) { statusToolNames.emplace_back(call.name); });

    provider.pendingToolCalls = { ToolCall { .id = "c1", .name = "mock_tool", .arguments = {} } };

    (void) session.processMessage("Test", nullptr);

    REQUIRE(statusToolNames.size() == 1);
    CHECK(statusToolNames[0] == "mock_tool");
}

// ============================================================================
// Tool result truncation tests
// ============================================================================

TEST_CASE("AgentSession.tool_result_under_limit_unchanged", "[agent]")
{
    auto provider = MockProvider {};
    provider.responseText = "Done";
    auto session = AgentSession(provider);

    auto registry = ToolRegistry {};
    auto tool = std::make_unique<MockTool>();
    tool->resultContent = "short result";
    registry.registerTool(std::move(tool));
    session.setToolRegistry(&registry);
    session.setMaxToolResultSize(1000);

    provider.pendingToolCalls = { ToolCall { .id = "c1", .name = "mock_tool", .arguments = {} } };
    (void) session.processMessage("Test", nullptr);

    // The tool result in history should be unchanged
    auto const& toolResultMsg = session.history().messages()[2];
    auto const* result = std::get_if<ToolResultBlock>(&toolResultMsg.content[0]);
    REQUIRE(result != nullptr);
    CHECK(result->content == "short result");
}

TEST_CASE("AgentSession.tool_result_over_limit_truncated", "[agent]")
{
    auto provider = MockProvider {};
    provider.responseText = "Done";
    auto session = AgentSession(provider);

    auto registry = ToolRegistry {};
    auto tool = std::make_unique<LargeResultTool>();
    tool->resultSize = 50000;
    registry.registerTool(std::move(tool));
    session.setToolRegistry(&registry);
    session.setMaxToolResultSize(1000);

    provider.pendingToolCalls = { ToolCall { .id = "c1", .name = "large_tool", .arguments = {} } };
    (void) session.processMessage("Test", nullptr);

    // The tool result in history should be truncated
    auto const& toolResultMsg = session.history().messages()[2];
    auto const* result = std::get_if<ToolResultBlock>(&toolResultMsg.content[0]);
    REQUIRE(result != nullptr);
    CHECK(result->content.size() < 50000);
    CHECK(result->content.find("[truncated") != std::string::npos);
    CHECK(result->content.find("49000 bytes omitted") != std::string::npos);
}

TEST_CASE("AgentSession.tool_result_at_limit_unchanged", "[agent]")
{
    auto provider = MockProvider {};
    provider.responseText = "Done";
    auto session = AgentSession(provider);

    auto registry = ToolRegistry {};
    auto tool = std::make_unique<LargeResultTool>();
    tool->resultSize = 1000;
    registry.registerTool(std::move(tool));
    session.setToolRegistry(&registry);
    session.setMaxToolResultSize(1000);

    provider.pendingToolCalls = { ToolCall { .id = "c1", .name = "large_tool", .arguments = {} } };
    (void) session.processMessage("Test", nullptr);

    auto const& toolResultMsg = session.history().messages()[2];
    auto const* result = std::get_if<ToolResultBlock>(&toolResultMsg.content[0]);
    REQUIRE(result != nullptr);
    CHECK(result->content.size() == 1000);
    CHECK(result->content.find("[truncated") == std::string::npos);
}

// ============================================================================
// Agent tracer tests
// ============================================================================

TEST_CASE("AgentSession.tracer_records_tool_calls", "[agent]")
{
    auto provider = MockProvider {};
    provider.responseText = "Done";
    auto session = AgentSession(provider);

    auto registry = ToolRegistry {};
    auto mockTool = std::make_unique<MockTool>();
    mockTool->resultContent = "tool output for trace";
    registry.registerTool(std::move(mockTool));
    session.setToolRegistry(&registry);

    // Create tracer to temp file
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-session-tracer";
    std::filesystem::remove_all(tmpDir);
    auto const tracePath = tmpDir / "trace.jsonl";
    auto tracer = AgentTracer::create(tracePath);
    REQUIRE(tracer.has_value());
    session.setTracer(&*tracer);

    provider.pendingToolCalls = { ToolCall {
        .id = "trace-call-1",
        .name = "mock_tool",
        .arguments = nlohmann::json { { "key", "value" } },
    } };

    (void) session.processMessage("Test trace", nullptr);

    // Read back trace file and find tool_call entries
    auto ifs = std::ifstream(tracePath);
    auto line = std::string {};
    auto toolCallFound = false;
    while (std::getline(ifs, line))
    {
        auto const doc = nlohmann::json::parse(line);
        if (doc.at("type") == "tool_call")
        {
            CHECK(doc.at("call_id") == "trace-call-1");
            CHECK(doc.at("tool_name") == "mock_tool");
            CHECK(doc.at("arguments").at("key") == "value");
            CHECK(doc.at("result").at("content") == "tool output for trace");
            CHECK(doc.at("result").at("is_error") == false);
            toolCallFound = true;
        }
    }
    CHECK(toolCallFound);

    ifs.close();
    tracer->close();
    std::filesystem::remove_all(tmpDir);
}

TEST_CASE("AgentSession.tracer_records_user_message", "[agent]")
{
    auto provider = MockProvider {};
    provider.responseText = "Done";
    auto session = AgentSession(provider);

    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-session-tracer-msg";
    std::filesystem::remove_all(tmpDir);
    auto const tracePath = tmpDir / "trace.jsonl";
    auto tracer = AgentTracer::create(tracePath);
    REQUIRE(tracer.has_value());
    session.setTracer(&*tracer);

    (void) session.processMessage("Hello agent", nullptr);

    auto ifs = std::ifstream(tracePath);
    auto line = std::string {};
    auto userMsgFound = false;
    while (std::getline(ifs, line))
    {
        auto const doc = nlohmann::json::parse(line);
        if (doc.at("type") == "user_message")
        {
            CHECK(doc.at("mode") == "chat");
            CHECK(doc.at("content") == "Hello agent");
            userMsgFound = true;
        }
    }
    CHECK(userMsgFound);

    ifs.close();
    tracer->close();
    std::filesystem::remove_all(tmpDir);
}

TEST_CASE("AgentSession.tracer_null_by_default", "[agent]")
{
    auto provider = MockProvider {};
    provider.responseText = "Done";
    auto session = AgentSession(provider);

    auto registry = ToolRegistry {};
    registry.registerTool(std::make_unique<MockTool>());
    session.setToolRegistry(&registry);

    // No tracer set — should not crash
    provider.pendingToolCalls = { ToolCall { .id = "tc", .name = "mock_tool", .arguments = {} } };
    auto result = session.processMessage("Test no trace", nullptr);
    REQUIRE(result.has_value());
}

// ============================================================================
// Plan mode tests
// ============================================================================

namespace
{
/// Provider that returns a submit_plan tool call on the first generate().
class PlanSubmitProvider final: public LlmProvider
{
  public:
    nlohmann::json planArguments;
    std::string explorationText;
    int generateCallCount = 0;
    std::vector<ToolDefinition> lastToolDefs;

    [[nodiscard]] auto generate(std::span<ChatMessage const>,
                                std::span<ToolDefinition const> tools,
                                StreamCallback) -> std::expected<GenerateResult, ProviderError> override
    {
        ++generateCallCount;
        lastToolDefs.assign(tools.begin(), tools.end());

        auto result = GenerateResult {};
        if (generateCallCount == 1 && !explorationText.empty())
        {
            // First call: return a read_file tool call to simulate exploration
            result.toolCalls = { ToolCall { .id = "tc-explore", .name = "read_file", .arguments = {} } };
            result.content.emplace_back(
                ToolUseBlock { .id = "tc-explore", .name = "read_file", .arguments = {} });
        }
        else
        {
            // Submit the plan
            result.toolCalls = { ToolCall {
                .id = "tc-plan", .name = "submit_plan", .arguments = planArguments } };
            result.content.emplace_back(
                ToolUseBlock { .id = "tc-plan", .name = "submit_plan", .arguments = planArguments });
        }
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

/// A simple mock read_file tool for plan mode tests.
class MockReadFileTool final: public AgentTool
{
  public:
    [[nodiscard]] auto name() const noexcept -> std::string_view override { return "read_file"; }

    [[nodiscard]] auto definition() const -> ToolDefinition override
    {
        return ToolDefinition {
            .name = "read_file",
            .description = "Read a file",
            .inputSchema = nlohmann::json { { "type", "object" } },
        };
    }

    [[nodiscard]] auto execute(nlohmann::json const&) -> std::expected<ToolResult, ToolError> override
    {
        return ToolResult { .content = "file contents", .isError = false };
    }
};
} // namespace

TEST_CASE("AgentSession.plan_mode_returns_plan", "[agent]")
{
    auto provider = PlanSubmitProvider {};
    provider.planArguments = nlohmann::json {
        { "summary", "Test plan" },
        { "steps",
          nlohmann::json::array({
              nlohmann::json { { "description", "Do the thing" } },
          }) },
    };

    auto session = AgentSession(provider);
    auto registry = ToolRegistry {};
    registry.registerTool(std::make_unique<SubmitPlanTool>());
    registry.registerTool(std::make_unique<MockReadFileTool>());
    session.setToolRegistry(&registry);

    auto result = session.processMessageForPlan("Add a feature", nullptr);

    REQUIRE(result.has_value());
    CHECK(result->summary == "Test plan");
    CHECK(result->steps.size() == 1);
    CHECK(result->steps[0].description == "Do the thing");
}

TEST_CASE("AgentSession.plan_mode_only_read_tools_sent", "[agent]")
{
    auto provider = PlanSubmitProvider {};
    provider.planArguments = nlohmann::json {
        { "summary", "Plan" },
        { "steps",
          nlohmann::json::array({
              nlohmann::json { { "description", "Step" } },
          }) },
    };

    auto session = AgentSession(provider);
    auto registry = ToolRegistry {};
    registry.registerTool(std::make_unique<SubmitPlanTool>());
    registry.registerTool(std::make_unique<MockReadFileTool>());

    // Register a write tool that should NOT be sent to the provider
    class MockWriteTool final: public AgentTool
    {
      public:
        [[nodiscard]] auto name() const noexcept -> std::string_view override { return "write_file"; }

        [[nodiscard]] auto definition() const -> ToolDefinition override
        {
            return ToolDefinition { .name = "write_file", .description = "Write", .inputSchema = {} };
        }

        [[nodiscard]] auto execute(nlohmann::json const&) -> std::expected<ToolResult, ToolError> override
        {
            return ToolResult { .content = "written", .isError = false };
        }
    };

    registry.registerTool(std::make_unique<MockWriteTool>());
    session.setToolRegistry(&registry);

    (void) session.processMessageForPlan("Plan something", nullptr);

    // Check that write_file was NOT in the tool definitions sent to the provider
    auto hasWriteFile = false;
    for (auto const& def: provider.lastToolDefs)
    {
        if (def.name == "write_file")
            hasWriteFile = true;
    }
    CHECK_FALSE(hasWriteFile);

    // Check that submit_plan and read_file WERE sent
    auto hasSubmitPlan = false;
    auto hasReadFile = false;
    for (auto const& def: provider.lastToolDefs)
    {
        if (def.name == "submit_plan")
            hasSubmitPlan = true;
        if (def.name == "read_file")
            hasReadFile = true;
    }
    CHECK(hasSubmitPlan);
    CHECK(hasReadFile);
}

TEST_CASE("AgentSession.plan_mode_exceeded_iterations", "[agent]")
{
    /// Provider that never submits a plan — always calls read_file.
    struct NeverPlanProvider final: public LlmProvider
    {
        [[nodiscard]] auto generate(std::span<ChatMessage const>,
                                    std::span<ToolDefinition const>,
                                    StreamCallback) -> std::expected<GenerateResult, ProviderError> override
        {
            auto result = GenerateResult {};
            result.toolCalls = { ToolCall { .id = "tc", .name = "read_file", .arguments = {} } };
            result.content.emplace_back(ToolUseBlock { .id = "tc", .name = "read_file", .arguments = {} });
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

    auto provider = NeverPlanProvider {};
    auto session = AgentSession(provider);
    session.setMaxExplorationIterations(3);

    auto registry = ToolRegistry {};
    registry.registerTool(std::make_unique<SubmitPlanTool>());
    registry.registerTool(std::make_unique<MockReadFileTool>());
    session.setToolRegistry(&registry);

    auto result = session.processMessageForPlan("Plan", nullptr);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == AgentErrorCode::ToolLoopExceeded);
    CHECK(result.error().message.find("3") != std::string::npos);
}

// =============================================================================
// Token usage accumulation tests
// =============================================================================

TEST_CASE("AgentSession.session_usage_accumulates_across_turns", "[agent]")
{
    auto provider = MockProvider {};
    provider.responseText = "Response";
    provider.mockUsage = TokenUsage { .inputTokens = 100, .outputTokens = 50 };

    auto session = AgentSession(provider);
    CHECK(session.sessionUsage().inputTokens == 0);
    CHECK(session.turnCount() == 0);

    auto r1 = session.processMessage("First", nullptr);
    REQUIRE(r1.has_value());
    CHECK(session.sessionUsage().inputTokens == 100);
    CHECK(session.sessionUsage().outputTokens == 50);
    CHECK(session.turnCount() == 1);

    auto r2 = session.processMessage("Second", nullptr);
    REQUIRE(r2.has_value());
    CHECK(session.sessionUsage().inputTokens == 200);
    CHECK(session.sessionUsage().outputTokens == 100);
    CHECK(session.turnCount() == 2);
}

TEST_CASE("AgentSession.session_usage_no_usage_from_provider", "[agent]")
{
    auto provider = MockProvider {};
    provider.responseText = "Response";
    // mockUsage is nullopt by default.

    auto session = AgentSession(provider);
    auto result = session.processMessage("Hello", nullptr);
    REQUIRE(result.has_value());
    CHECK(session.sessionUsage().inputTokens == 0);
    CHECK(session.sessionUsage().outputTokens == 0);
    CHECK(session.turnCount() == 1);
}

TEST_CASE("AgentSession.reset_clears_usage", "[agent]")
{
    auto provider = MockProvider {};
    provider.responseText = "Response";
    provider.mockUsage = TokenUsage { .inputTokens = 500, .outputTokens = 200 };

    auto session = AgentSession(provider);
    (void) session.processMessage("First", nullptr);
    CHECK(session.turnCount() == 1);
    CHECK(session.sessionUsage().inputTokens == 500);

    session.reset();
    CHECK(session.turnCount() == 0);
    CHECK(session.sessionUsage().inputTokens == 0);
    CHECK(session.sessionUsage().outputTokens == 0);
}

// =============================================================================
// Per-turn usage (lastTurnUsage) tests
// =============================================================================

TEST_CASE("AgentSession.last_turn_usage_single_call", "[agent]")
{
    auto provider = MockProvider {};
    provider.responseText = "Response";
    provider.mockUsage = TokenUsage { .inputTokens = 200, .outputTokens = 80 };

    auto session = AgentSession(provider);
    auto result = session.processMessage("Hello", nullptr);
    REQUIRE(result.has_value());

    // Single generate() call: lastTurnUsage should equal the usage from that call.
    CHECK(session.lastTurnUsage().inputTokens == 200);
    CHECK(session.lastTurnUsage().outputTokens == 80);
}

TEST_CASE("AgentSession.last_turn_usage_multi_call_tool_loop", "[agent]")
{
    // Provider that returns a tool call on the first generate(), then text on the second.
    // Each call reports different usage to verify input=last, output=sum semantics.
    struct MultiCallProvider final: public LlmProvider
    {
        int callCount = 0;

        [[nodiscard]] auto generate(std::span<ChatMessage const>,
                                    std::span<ToolDefinition const>,
                                    StreamCallback) -> std::expected<GenerateResult, ProviderError> override
        {
            ++callCount;
            auto result = GenerateResult {};
            if (callCount == 1)
            {
                // First call: tool use, input=1000, output=50
                result.toolCalls = { ToolCall { .id = "tc1", .name = "mock_tool", .arguments = {} } };
                result.content.emplace_back(
                    ToolUseBlock { .id = "tc1", .name = "mock_tool", .arguments = {} });
                result.usage = TokenUsage { .inputTokens = 1000, .outputTokens = 50 };
            }
            else
            {
                // Second call: text response, input=1200 (context grew), output=100
                result.content.emplace_back(TextBlock { .text = "Done" });
                result.usage = TokenUsage { .inputTokens = 1200, .outputTokens = 100 };
            }
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

    auto provider = MultiCallProvider {};
    auto session = AgentSession(provider);

    auto registry = ToolRegistry {};
    registry.registerTool(std::make_unique<MockTool>());
    session.setToolRegistry(&registry);

    auto result = session.processMessage("Test", nullptr);
    REQUIRE(result.has_value());

    // inputTokens should be from the LAST call (1200), not the sum (2200).
    CHECK(session.lastTurnUsage().inputTokens == 1200);
    // outputTokens should be the SUM across both calls (50 + 100 = 150).
    CHECK(session.lastTurnUsage().outputTokens == 150);

    // sessionUsage should be the full sum of everything.
    CHECK(session.sessionUsage().inputTokens == 2200);
    CHECK(session.sessionUsage().outputTokens == 150);
}

TEST_CASE("AgentSession.last_turn_usage_resets_per_turn", "[agent]")
{
    auto provider = MockProvider {};
    provider.responseText = "Response";
    provider.mockUsage = TokenUsage { .inputTokens = 300, .outputTokens = 60 };

    auto session = AgentSession(provider);

    // First turn
    (void) session.processMessage("First", nullptr);
    CHECK(session.lastTurnUsage().inputTokens == 300);
    CHECK(session.lastTurnUsage().outputTokens == 60);

    // Second turn with different usage
    provider.mockUsage = TokenUsage { .inputTokens = 500, .outputTokens = 40 };
    (void) session.processMessage("Second", nullptr);

    // lastTurnUsage should reflect only the second turn's values.
    CHECK(session.lastTurnUsage().inputTokens == 500);
    CHECK(session.lastTurnUsage().outputTokens == 40);
}

TEST_CASE("AgentSession.last_turn_usage_no_usage_from_provider", "[agent]")
{
    auto provider = MockProvider {};
    provider.responseText = "Response";
    // mockUsage is nullopt by default.

    auto session = AgentSession(provider);
    auto result = session.processMessage("Hello", nullptr);
    REQUIRE(result.has_value());

    CHECK(session.lastTurnUsage().inputTokens == 0);
    CHECK(session.lastTurnUsage().outputTokens == 0);
}

TEST_CASE("AgentSession.reset_clears_last_turn_usage", "[agent]")
{
    auto provider = MockProvider {};
    provider.responseText = "Response";
    provider.mockUsage = TokenUsage { .inputTokens = 500, .outputTokens = 200 };

    auto session = AgentSession(provider);
    (void) session.processMessage("Hello", nullptr);
    CHECK(session.lastTurnUsage().inputTokens == 500);

    session.reset();
    CHECK(session.lastTurnUsage().inputTokens == 0);
    CHECK(session.lastTurnUsage().outputTokens == 0);
}

// ============================================================================
// Multimodal (image) message tests
// ============================================================================

TEST_CASE("AgentSession.process_message_with_images", "[agent]")
{
    auto provider = MockProvider {};
    provider.responseText = "I see an image.";
    auto session = AgentSession(provider);

    auto const imageData = std::vector<uint8_t> { 0x89, 0x50, 0x4E, 0x47 }; // PNG header bytes
    auto images = std::vector<ImageBlock> {};
    images.push_back(ImageBlock { .data = imageData, .mediaType = "image/png" });

    auto result = session.processMessage("What is in this image?", std::span(images), nullptr);

    REQUIRE(result.has_value());
    CHECK(*result == "I see an image.");

    // Verify the user message in history has both TextBlock and ImageBlock.
    REQUIRE(session.history().size() == 2);
    auto const& userMsg = session.history().messages()[0];
    REQUIRE(userMsg.content.size() == 2);
    CHECK(std::holds_alternative<TextBlock>(userMsg.content[0]));
    CHECK(std::holds_alternative<ImageBlock>(userMsg.content[1]));

    auto const& imgBlock = std::get<ImageBlock>(userMsg.content[1]);
    CHECK(imgBlock.mediaType == "image/png");
    CHECK(imgBlock.data == imageData);
}

TEST_CASE("AgentSession.process_message_without_images_is_text_only", "[agent]")
{
    auto provider = MockProvider {};
    provider.responseText = "Response";
    auto session = AgentSession(provider);

    auto result = session.processMessage("Hello", std::span<ImageBlock const> {}, nullptr);

    REQUIRE(result.has_value());
    REQUIRE(session.history().size() == 2);
    auto const& userMsg = session.history().messages()[0];
    REQUIRE(userMsg.content.size() == 1);
    CHECK(std::holds_alternative<TextBlock>(userMsg.content[0]));
}

TEST_CASE("AgentSession.process_message_with_multiple_images", "[agent]")
{
    auto provider = MockProvider {};
    provider.responseText = "I see two images.";
    auto session = AgentSession(provider);

    auto images = std::vector<ImageBlock> {};
    images.push_back(ImageBlock { .data = { 0xFF, 0xD8, 0xFF }, .mediaType = "image/jpeg" });
    images.push_back(ImageBlock { .data = { 0x89, 0x50, 0x4E, 0x47 }, .mediaType = "image/png" });

    auto result = session.processMessage("Describe these", std::span(images), nullptr);

    REQUIRE(result.has_value());
    REQUIRE(session.history().size() == 2);
    auto const& userMsg = session.history().messages()[0];
    REQUIRE(userMsg.content.size() == 3); // 1 TextBlock + 2 ImageBlocks
    CHECK(std::holds_alternative<TextBlock>(userMsg.content[0]));
    CHECK(std::holds_alternative<ImageBlock>(userMsg.content[1]));
    CHECK(std::holds_alternative<ImageBlock>(userMsg.content[2]));
}
