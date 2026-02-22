// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <format>

#include <agent/PermissionManager.hpp>

namespace endo::agent
{

PermissionManager::PermissionManager(PermissionConfig config): _config(std::move(config))
{
}

void PermissionManager::setPromptCallback(PermissionPromptCallback callback)
{
    _promptCallback = std::move(callback);
}

void PermissionManager::setConfig(PermissionConfig config)
{
    _config = std::move(config);
}

auto PermissionManager::config() const noexcept -> PermissionConfig const&
{
    return _config;
}

auto PermissionManager::checkPermission(std::string_view toolName,
                                        ToolRisk baseRisk,
                                        nlohmann::json const& arguments) -> PermissionDecision
{
    // Blocked tools are always denied regardless of policy.
    if (baseRisk == ToolRisk::Blocked)
        return PermissionDecision::Blocked;

    // ReadOnly tools are always approved.
    if (baseRisk == ToolRisk::ReadOnly)
        return PermissionDecision::Approved;

    // TrustAll policy: approve everything.
    if (_config.policy == PermissionPolicy::TrustAll)
        return PermissionDecision::Approved;

    // ReadOnly policy: deny all non-ReadOnly tools.
    if (_config.policy == PermissionPolicy::ReadOnly)
        return PermissionDecision::Denied;

    // Check trusted tools list.
    if (isTrustedTool(toolName))
        return PermissionDecision::Approved;

    // TrustSession policy: auto-approve Mutating, still prompt Destructive.
    if (_config.policy == PermissionPolicy::TrustSession && baseRisk == ToolRisk::Mutating)
        return PermissionDecision::Approved;

    // Check session-level approvals (Mutating tools remembered after first prompt).
    auto const toolNameStr = std::string(toolName);
    if (baseRisk == ToolRisk::Mutating && _approvedTools.contains(toolNameStr))
        return PermissionDecision::Approved;

    // Need to prompt. If no callback, fail safe to denied.
    if (!_promptCallback)
        return PermissionDecision::Denied;

    auto const prompt = PermissionPrompt {
        .toolName = toolNameStr,
        .riskLevel = baseRisk,
        .description = buildDescription(toolName, arguments),
        .commandPreview = buildCommandPreview(toolName, arguments),
    };

    auto const decision = _promptCallback(prompt);

    // "Yes, always for this tool" is signaled by returning Approved
    // when the tool is Mutating — the caller (AgentWorker) handles
    // distinguishing "Yes" vs "Yes, always" via the prompt UI and
    // calls approvedTools or records the approval.
    // Here we just record mutating approvals on success.
    if (decision == PermissionDecision::Approved && baseRisk == ToolRisk::Mutating)
        _approvedTools.insert(toolNameStr);

    return decision;
}

void PermissionManager::resetApprovals()
{
    _approvedTools.clear();
}

auto PermissionManager::approvedTools() const -> std::set<std::string> const&
{
    return _approvedTools;
}

auto PermissionManager::buildCommandPreview(std::string_view toolName, nlohmann::json const& arguments)
    -> std::string
{
    if (!arguments.is_object())
        return {};

    if (toolName == "shell_execute")
        return arguments.value("command", std::string {});

    if (toolName == "git")
    {
        auto preview = std::string { "git " };
        preview += arguments.value("subcommand", std::string {});
        if (arguments.contains("args") && arguments["args"].is_array())
        {
            for (auto const& arg: arguments["args"])
            {
                if (arg.is_string())
                {
                    preview += ' ';
                    preview += arg.get<std::string>();
                }
            }
        }
        return preview;
    }

    return {};
}

auto PermissionManager::buildDescription(std::string_view toolName, nlohmann::json const& arguments)
    -> std::string
{
    if (!arguments.is_object())
        return std::format("Execute tool: {}", toolName);

    if (toolName == "write_file")
    {
        auto const path = arguments.value("path", std::string {});
        return std::format("Write to {}", path.empty() ? "(unknown file)" : path);
    }

    if (toolName == "edit_file")
    {
        auto const path = arguments.value("path", std::string {});
        return std::format("Edit {}", path.empty() ? "(unknown file)" : path);
    }

    if (toolName == "shell_execute")
    {
        auto const command = arguments.value("command", std::string {});
        if (command.size() > 80)
            return std::format("Execute: {}...", command.substr(0, 77));
        return std::format("Execute: {}", command);
    }

    if (toolName == "git")
    {
        auto const subcommand = arguments.value("subcommand", std::string {});
        return std::format("Run git {}", subcommand);
    }

    if (toolName == "save_memory")
        return "Save to agent memory";

    if (toolName == "endo_execute")
        return "Execute endo source code";

    return std::format("Execute tool: {}", toolName);
}

auto PermissionManager::isTrustedTool(std::string_view toolName) const -> bool
{
    return std::ranges::any_of(_config.trustedTools,
                               [toolName](auto const& trusted) { return trusted == toolName; });
}

} // namespace endo::agent
