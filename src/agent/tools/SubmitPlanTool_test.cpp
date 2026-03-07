// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <agent/tools/SubmitPlanTool.hpp>

using namespace endo::agent;

TEST_CASE("SubmitPlanTool.name_is_submit_plan", "[agent][tools]")
{
    auto const tool = SubmitPlanTool {};
    CHECK(tool.name() == "submit_plan");
}

TEST_CASE("SubmitPlanTool.definition_has_required_fields", "[agent][tools]")
{
    auto const tool = SubmitPlanTool {};
    auto const def = tool.definition();

    CHECK(def.name == "submit_plan");
    CHECK(!def.description.empty());
    CHECK(def.inputSchema.contains("properties"));
    CHECK(def.inputSchema["required"].size() == 2);
}

TEST_CASE("SubmitPlanTool.valid_plan_parsed", "[agent][tools]")
{
    auto tool = SubmitPlanTool {};

    auto const args = nlohmann::json {
        { "summary", "Add error handling to the API" },
        { "steps",
          nlohmann::json::array({
              nlohmann::json {
                  { "description", "Add try-catch to endpoint" },
                  { "files_touched", nlohmann::json::array({ "src/api.cpp" }) },
                  { "rationale", "Prevent crashes on invalid input" },
              },
              nlohmann::json {
                  { "description", "Add unit tests" },
                  { "files_touched", nlohmann::json::array({ "tests/api_test.cpp" }) },
                  { "depends_on", nlohmann::json::array({ 0 }) },
              },
          }) },
        { "risk_assessment", "Low risk change" },
        { "alternatives", nlohmann::json::array({ "Use middleware instead" }) },
    };

    auto result = tool.execute(args);
    REQUIRE(result.has_value());
    CHECK(result->content == "Plan submitted successfully.");
    CHECK_FALSE(result->isError);

    auto const& plan = tool.lastParsedPlan();
    REQUIRE(plan.has_value());
    CHECK(plan->summary == "Add error handling to the API");
    REQUIRE(plan->steps.size() == 2);
    CHECK(plan->steps[0].index == 0);
    CHECK(plan->steps[0].description == "Add try-catch to endpoint");
    CHECK(plan->steps[0].filesTouched.size() == 1);
    CHECK(plan->steps[0].filesTouched[0] == "src/api.cpp");
    CHECK(plan->steps[0].rationale == "Prevent crashes on invalid input");
    CHECK(plan->steps[1].index == 1);
    CHECK(plan->steps[1].dependsOn.size() == 1);
    CHECK(plan->steps[1].dependsOn[0] == 0);
    CHECK(plan->riskAssessment == "Low risk change");
    CHECK(plan->alternatives.size() == 1);
    CHECK(plan->alternatives[0] == "Use middleware instead");
}

TEST_CASE("SubmitPlanTool.minimal_valid_plan", "[agent][tools]")
{
    auto tool = SubmitPlanTool {};

    auto const args = nlohmann::json {
        { "summary", "Simple fix" },
        { "steps",
          nlohmann::json::array({
              nlohmann::json { { "description", "Fix the bug" } },
          }) },
    };

    auto result = tool.execute(args);
    REQUIRE(result.has_value());

    auto const& plan = tool.lastParsedPlan();
    REQUIRE(plan.has_value());
    CHECK(plan->summary == "Simple fix");
    CHECK(plan->steps.size() == 1);
    CHECK(plan->steps[0].filesTouched.empty());
    CHECK(plan->steps[0].rationale.empty());
    CHECK(plan->steps[0].dependsOn.empty());
    CHECK(plan->riskAssessment.empty());
    CHECK(plan->alternatives.empty());
}

TEST_CASE("SubmitPlanTool.missing_summary_fails", "[agent][tools]")
{
    auto tool = SubmitPlanTool {};

    auto const args = nlohmann::json {
        { "steps",
          nlohmann::json::array({
              nlohmann::json { { "description", "A step" } },
          }) },
    };

    auto result = tool.execute(args);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("summary") != std::string::npos);
}

TEST_CASE("SubmitPlanTool.missing_steps_fails", "[agent][tools]")
{
    auto tool = SubmitPlanTool {};

    auto const args = nlohmann::json {
        { "summary", "A plan" },
    };

    auto result = tool.execute(args);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("steps") != std::string::npos);
}

TEST_CASE("SubmitPlanTool.empty_steps_fails", "[agent][tools]")
{
    auto tool = SubmitPlanTool {};

    auto const args = nlohmann::json {
        { "summary", "A plan" },
        { "steps", nlohmann::json::array() },
    };

    auto result = tool.execute(args);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("at least one step") != std::string::npos);
}

TEST_CASE("SubmitPlanTool.step_missing_description_fails", "[agent][tools]")
{
    auto tool = SubmitPlanTool {};

    auto const args = nlohmann::json {
        { "summary", "A plan" },
        { "steps",
          nlohmann::json::array({
              nlohmann::json { { "files_touched", nlohmann::json::array({ "a.cpp" }) } },
          }) },
    };

    auto result = tool.execute(args);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("description") != std::string::npos);
}

TEST_CASE("SubmitPlanTool.clear_resets_plan", "[agent][tools]")
{
    auto tool = SubmitPlanTool {};

    auto const args = nlohmann::json {
        { "summary", "A plan" },
        { "steps",
          nlohmann::json::array({
              nlohmann::json { { "description", "Do something" } },
          }) },
    };

    [[maybe_unused]] auto _ = tool.execute(args);
    REQUIRE(tool.lastParsedPlan().has_value());

    tool.clearParsedPlan();
    CHECK_FALSE(tool.lastParsedPlan().has_value());
}

TEST_CASE("SubmitPlanTool.no_plan_initially", "[agent][tools]")
{
    auto const tool = SubmitPlanTool {};
    CHECK_FALSE(tool.lastParsedPlan().has_value());
}
