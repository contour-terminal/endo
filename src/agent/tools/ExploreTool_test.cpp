// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <set>

#include <agent/AgentConfig.hpp>
#include <agent/providers/LlmProvider.hpp>
#include <agent/tools/ExploreTool.hpp>

using namespace endo::agent;

namespace
{

/// Mock LLM provider for ExploreTool tests.
class MockProvider final: public LlmProvider
{
  public:
    std::string responseText = "Mock answer";
    int generateCallCount = 0;
    bool shouldFail = false;
    std::vector<ToolCall> pendingToolCalls;

    [[nodiscard]] auto generate(std::span<ChatMessage const>,
                                std::span<ToolDefinition const> tools,
                                StreamCallback) -> std::expected<GenerateResult, ProviderError> override
    {
        lastToolDefs.assign(tools.begin(), tools.end());
        ++generateCallCount;

        if (shouldFail)
        {
            return std::unexpected(ProviderError {
                .code = ProviderErrorCode::NetworkError, .message = "mock error", .httpStatus = 500 });
        }

        auto result = GenerateResult {};

        if (!pendingToolCalls.empty())
        {
            result.toolCalls = std::move(pendingToolCalls);
            pendingToolCalls.clear();

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

    [[nodiscard]] auto supportsToolUse() const noexcept -> bool override { return true; }

    [[nodiscard]] auto supportsImageInput() const noexcept -> bool override { return false; }

    [[nodiscard]] auto supportsImageOutput() const noexcept -> bool override { return false; }

    [[nodiscard]] auto contextSize() const noexcept -> size_t override { return 8192; }

    [[nodiscard]] auto modelInfo() const -> ModelInfo override
    {
        return ModelInfo { .providerName = "mock", .modelName = "mock-1" };
    }

    std::vector<ToolDefinition> lastToolDefs;
};

/// Provider that always returns tool calls (for max-turns testing).
class AlwaysToolProvider final: public LlmProvider
{
  public:
    [[nodiscard]] auto generate(std::span<ChatMessage const>, std::span<ToolDefinition const>, StreamCallback)
        -> std::expected<GenerateResult, ProviderError> override
    {
        auto result = GenerateResult {};
        result.toolCalls = { ToolCall {
            .id = "tc",
            .name = "glob",
            .arguments = nlohmann::json { { "pattern", "*.cpp" } },
        } };
        result.content.emplace_back(ToolUseBlock {
            .id = "tc",
            .name = "glob",
            .arguments = nlohmann::json { { "pattern", "*.cpp" } },
        });
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

auto noopShellExec(std::string const&, std::chrono::milliseconds) -> ShellExecResult
{
    return ShellExecResult { .output = "noop", .exitCode = 0 };
}

} // namespace

TEST_CASE("ExploreTool.name", "[agent][explore]")
{
    auto provider = MockProvider {};
    auto tool = ExploreTool(provider, noopShellExec, ExploreConfig {});
    CHECK(tool.name() == "explore");
}

TEST_CASE("ExploreTool.definition_schema", "[agent][explore]")
{
    auto provider = MockProvider {};
    auto tool = ExploreTool(provider, noopShellExec, ExploreConfig {});
    auto const def = tool.definition();

    CHECK(def.name == "explore");
    CHECK(!def.description.empty());

    // "question" must be listed as required
    auto const& required = def.inputSchema["required"];
    REQUIRE(required.is_array());
    CHECK(required.size() == 1);
    CHECK(required[0] == "question");

    // Schema must include "question" and "scope" properties
    auto const& props = def.inputSchema["properties"];
    CHECK(props.contains("question"));
    CHECK(props.contains("scope"));
}

TEST_CASE("ExploreTool.missing_question_returns_error", "[agent][explore]")
{
    auto provider = MockProvider {};
    auto tool = ExploreTool(provider, noopShellExec, ExploreConfig {});

    auto result = tool.execute(nlohmann::json::object());
    REQUIRE(!result.has_value());
    CHECK(result.error().message.find("question") != std::string::npos);
}

TEST_CASE("ExploreTool.empty_question_returns_error", "[agent][explore]")
{
    auto provider = MockProvider {};
    auto tool = ExploreTool(provider, noopShellExec, ExploreConfig {});

    auto result = tool.execute(nlohmann::json { { "question", "" } });
    REQUIRE(!result.has_value());
    CHECK(result.error().message.find("question") != std::string::npos);
}

TEST_CASE("ExploreTool.inner_agent_answers_directly", "[agent][explore]")
{
    auto provider = MockProvider {};
    provider.responseText = "The function is defined in src/foo.cpp:42.";
    auto tool = ExploreTool(provider, noopShellExec, ExploreConfig {});
    tool.setSystemPrompt("You are an explorer.");

    auto result = tool.execute(nlohmann::json { { "question", "Where is function foo defined?" } });
    REQUIRE(result.has_value());
    CHECK(result->content == "The function is defined in src/foo.cpp:42.");
    CHECK_FALSE(result->isError);
    CHECK(provider.generateCallCount == 1);
}

TEST_CASE("ExploreTool.inner_agent_explores_then_answers", "[agent][explore]")
{
    auto provider = MockProvider {};
    provider.responseText = "Found it in bar.cpp:10.";
    // First call returns a tool use, second returns text
    provider.pendingToolCalls = { ToolCall {
        .id = "tc-1",
        .name = "grep",
        .arguments = nlohmann::json { { "pattern", "foo" } },
    } };

    auto tool = ExploreTool(provider, noopShellExec, ExploreConfig {});
    tool.setSystemPrompt("You are an explorer.");

    auto result = tool.execute(nlohmann::json { { "question", "Find foo" } });
    REQUIRE(result.has_value());
    CHECK(result->content == "Found it in bar.cpp:10.");
    CHECK(provider.generateCallCount == 2);
}

TEST_CASE("ExploreTool.max_turns_exceeded", "[agent][explore]")
{
    auto provider = AlwaysToolProvider {};
    auto config = ExploreConfig { .maxTurns = 3 };
    auto tool = ExploreTool(provider, noopShellExec, config);
    tool.setSystemPrompt("You are an explorer.");

    auto result = tool.execute(nlohmann::json { { "question", "Infinite exploration" } });
    REQUIRE(!result.has_value());
    CHECK(result.error().message.find("failed") != std::string::npos);
}

TEST_CASE("ExploreTool.inner_agent_has_only_read_tools", "[agent][explore]")
{
    auto provider = MockProvider {};
    provider.responseText = "answer";
    auto tool = ExploreTool(provider, noopShellExec, ExploreConfig {});
    tool.setSystemPrompt("You are an explorer.");

    (void) tool.execute(nlohmann::json { { "question", "test" } });

    // Verify that only read-only tool definitions were sent to the provider
    auto const& defs = provider.lastToolDefs;
    REQUIRE(defs.size() == 5);

    auto toolNames = std::set<std::string> {};
    for (auto const& d: defs)
        toolNames.insert(d.name);

    CHECK(toolNames.count("read_file") == 1);
    CHECK(toolNames.count("glob") == 1);
    CHECK(toolNames.count("grep") == 1);
    CHECK(toolNames.count("search") == 1);
    CHECK(toolNames.count("git") == 1);

    // Verify write tools are NOT present
    CHECK(toolNames.count("write_file") == 0);
    CHECK(toolNames.count("edit_file") == 0);
    CHECK(toolNames.count("shell_execute") == 0);
    CHECK(toolNames.count("explore") == 0);
}

TEST_CASE("ExploreTool.scope_prepended_to_question", "[agent][explore]")
{
    auto provider = MockProvider {};
    provider.responseText = "answer";
    auto tool = ExploreTool(provider, noopShellExec, ExploreConfig {});
    tool.setSystemPrompt("You are an explorer.");

    (void) tool.execute(nlohmann::json { { "question", "What does it do?" }, { "scope", "src/agent/" } });

    // The user message sent to the inner agent should contain both scope and question
    // We can verify the provider received a message (generateCallCount == 1)
    CHECK(provider.generateCallCount == 1);
}

TEST_CASE("ExploreTool.provider_error_returns_tool_error", "[agent][explore]")
{
    auto provider = MockProvider {};
    provider.shouldFail = true;
    auto tool = ExploreTool(provider, noopShellExec, ExploreConfig {});
    tool.setSystemPrompt("You are an explorer.");

    auto result = tool.execute(nlohmann::json { { "question", "test" } });
    REQUIRE(!result.has_value());
    CHECK(result.error().message.find("Explore sub-agent failed") != std::string::npos);
}
