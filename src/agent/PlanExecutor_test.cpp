// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <agent/AgentSession.hpp>
#include <agent/PlanExecutor.hpp>

using namespace endo::agent;

namespace
{

/// Mock LLM provider for PlanExecutor tests.
class MockProvider final: public LlmProvider
{
  public:
    std::string responseText = "Step executed.";
    bool shouldFail = false;
    ProviderError failError { ProviderErrorCode::NetworkError, "step failed", 500 };
    int generateCallCount = 0;

    [[nodiscard]] auto generate(std::span<ChatMessage const>,
                                std::span<ToolDefinition const>,
                                StreamCallback streamCb)
        -> std::expected<GenerateResult, ProviderError> override
    {
        ++generateCallCount;

        if (shouldFail)
            return std::unexpected(failError);

        if (streamCb)
            streamCb(responseText);

        auto result = GenerateResult {};
        result.content.emplace_back(TextBlock { .text = responseText });
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
};

auto makePlan(size_t stepCount) -> Plan
{
    auto plan = Plan {};
    plan.summary = "Test plan";
    for (auto i = size_t { 0 }; i < stepCount; ++i)
    {
        plan.steps.push_back(PlanStep {
            .index = i,
            .description = std::format("Step {}", i + 1),
        });
    }
    return plan;
}

} // namespace

TEST_CASE("PlanExecutor.single_step_execution", "[agent]")
{
    auto provider = MockProvider {};
    auto session = AgentSession(provider);
    auto executor = PlanExecutor(session, makePlan(1));

    CHECK_FALSE(executor.isComplete());

    auto result = executor.executeNextStep(nullptr);
    REQUIRE(result.has_value());
    CHECK(*result == PlanStepStatus::Completed);
    CHECK(executor.isComplete());
    CHECK(executor.plan().steps[0].status == PlanStepStatus::Completed);
}

TEST_CASE("PlanExecutor.multi_step_execution", "[agent]")
{
    auto provider = MockProvider {};
    auto session = AgentSession(provider);
    auto executor = PlanExecutor(session, makePlan(3));

    for (auto i = size_t { 0 }; i < 3; ++i)
    {
        CHECK_FALSE(executor.isComplete());
        auto result = executor.executeNextStep(nullptr);
        REQUIRE(result.has_value());
        CHECK(*result == PlanStepStatus::Completed);
        CHECK(executor.currentStepIndex() == i);
    }

    CHECK(executor.isComplete());
    CHECK(provider.generateCallCount == 3);
}

TEST_CASE("PlanExecutor.step_failure_marks_failed", "[agent]")
{
    auto provider = MockProvider {};
    provider.shouldFail = true;
    auto session = AgentSession(provider);
    auto executor = PlanExecutor(session, makePlan(2));

    auto result = executor.executeNextStep(nullptr);
    REQUIRE_FALSE(result.has_value());
    CHECK(executor.plan().steps[0].status == PlanStepStatus::Failed);
    CHECK(executor.plan().steps[1].status == PlanStepStatus::Pending);
    CHECK_FALSE(executor.isComplete());
}

TEST_CASE("PlanExecutor.skip_step", "[agent]")
{
    auto provider = MockProvider {};
    auto session = AgentSession(provider);
    auto executor = PlanExecutor(session, makePlan(3));

    // Skip the first step
    executor.skipStep(0);
    CHECK(executor.plan().steps[0].status == PlanStepStatus::Skipped);

    // Execute next (should be step 1, not 0)
    auto result = executor.executeNextStep(nullptr);
    REQUIRE(result.has_value());
    CHECK(executor.currentStepIndex() == 1);
}

TEST_CASE("PlanExecutor.no_pending_steps_returns_error", "[agent]")
{
    auto provider = MockProvider {};
    auto session = AgentSession(provider);
    auto executor = PlanExecutor(session, makePlan(1));

    // Complete the only step
    (void) executor.executeNextStep(nullptr);
    CHECK(executor.isComplete());

    // Trying to execute another step should fail
    auto result = executor.executeNextStep(nullptr);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("No pending steps") != std::string::npos);
}

TEST_CASE("PlanExecutor.plan_accessor", "[agent]")
{
    auto provider = MockProvider {};
    auto session = AgentSession(provider);
    auto plan = makePlan(2);
    plan.summary = "My test plan";
    auto executor = PlanExecutor(session, std::move(plan));

    CHECK(executor.plan().summary == "My test plan");
    CHECK(executor.plan().steps.size() == 2);
}

TEST_CASE("PlanExecutor.skip_out_of_range", "[agent]")
{
    auto provider = MockProvider {};
    auto session = AgentSession(provider);
    auto executor = PlanExecutor(session, makePlan(2));

    // Should not crash on out-of-range index
    executor.skipStep(99);
    CHECK(executor.plan().steps[0].status == PlanStepStatus::Pending);
    CHECK(executor.plan().steps[1].status == PlanStepStatus::Pending);
}
