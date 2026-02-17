// SPDX-License-Identifier: Apache-2.0
#include <format>

#include <agent/tools/SubmitPlanTool.hpp>

namespace endo::agent
{

auto SubmitPlanTool::name() const noexcept -> std::string_view
{
    return "submit_plan";
}

auto SubmitPlanTool::definition() const -> ToolDefinition
{
    // clang-format off
    auto const schema = nlohmann::json {
        { "type", "object" },
        { "required", nlohmann::json::array({ "summary", "steps" }) },
        { "properties", {
            { "summary", {
                { "type", "string" },
                { "description", "High-level summary of the plan." }
            }},
            { "steps", {
                { "type", "array" },
                { "description", "Ordered list of steps to execute." },
                { "items", {
                    { "type", "object" },
                    { "required", nlohmann::json::array({ "description" }) },
                    { "properties", {
                        { "description", {
                            { "type", "string" },
                            { "description", "What this step will do." }
                        }},
                        { "files_touched", {
                            { "type", "array" },
                            { "items", { { "type", "string" } } },
                            { "description", "Files this step will read or modify." }
                        }},
                        { "rationale", {
                            { "type", "string" },
                            { "description", "Why this step is needed." }
                        }},
                        { "depends_on", {
                            { "type", "array" },
                            { "items", { { "type", "integer" } } },
                            { "description", "Indices of steps this step depends on." }
                        }}
                    }}
                }}
            }},
            { "risk_assessment", {
                { "type", "string" },
                { "description", "Assessment of risks involved." }
            }},
            { "alternatives", {
                { "type", "array" },
                { "items", { { "type", "string" } } },
                { "description", "Alternative approaches considered." }
            }}
        }}
    };
    // clang-format on

    return ToolDefinition {
        .name = "submit_plan",
        .description = "Submit a structured execution plan after exploration. "
                       "Call this once you have explored the codebase and are ready to propose a plan.",
        .inputSchema = schema,
    };
}

auto SubmitPlanTool::execute(nlohmann::json const& arguments) -> std::expected<ToolResult, ToolError>
{
    try
    {
        // Validate required fields
        if (!arguments.contains("summary") || !arguments["summary"].is_string())
            return std::unexpected(ToolError { .message = "Missing or invalid 'summary' field." });
        if (!arguments.contains("steps") || !arguments["steps"].is_array())
            return std::unexpected(ToolError { .message = "Missing or invalid 'steps' array." });
        if (arguments["steps"].empty())
            return std::unexpected(ToolError { .message = "Plan must contain at least one step." });

        auto plan = Plan {};
        plan.summary = arguments["summary"].get<std::string>();

        auto stepIndex = size_t { 0 };
        for (auto const& stepJson: arguments["steps"])
        {
            if (!stepJson.is_object() || !stepJson.contains("description"))
                return std::unexpected(
                    ToolError { .message = std::format("Step {} missing 'description'.", stepIndex) });

            auto step = PlanStep {};
            step.index = stepIndex;
            step.description = stepJson["description"].get<std::string>();

            if (stepJson.contains("files_touched") && stepJson["files_touched"].is_array())
                for (auto const& f: stepJson["files_touched"])
                    if (f.is_string())
                        step.filesTouched.push_back(f.get<std::string>());

            if (stepJson.contains("rationale") && stepJson["rationale"].is_string())
                step.rationale = stepJson["rationale"].get<std::string>();

            if (stepJson.contains("depends_on") && stepJson["depends_on"].is_array())
                for (auto const& d: stepJson["depends_on"])
                    if (d.is_number_integer())
                        step.dependsOn.push_back(d.get<size_t>());

            plan.steps.push_back(std::move(step));
            ++stepIndex;
        }

        if (arguments.contains("risk_assessment") && arguments["risk_assessment"].is_string())
            plan.riskAssessment = arguments["risk_assessment"].get<std::string>();

        if (arguments.contains("alternatives") && arguments["alternatives"].is_array())
            for (auto const& alt: arguments["alternatives"])
                if (alt.is_string())
                    plan.alternatives.push_back(alt.get<std::string>());

        _lastParsedPlan = std::move(plan);

        return ToolResult {
            .content = "Plan submitted successfully.",
            .isError = false,
        };
    }
    catch (nlohmann::json::exception const& e)
    {
        return std::unexpected(ToolError { .message = std::format("JSON parse error: {}", e.what()) });
    }
}

auto SubmitPlanTool::lastParsedPlan() const -> std::optional<Plan> const&
{
    return _lastParsedPlan;
}

void SubmitPlanTool::clearParsedPlan()
{
    _lastParsedPlan.reset();
}

} // namespace endo::agent
