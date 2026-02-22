// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file PermissionManager.hpp
/// @brief Centralized tool permission gate for the agent system.

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace endo::agent
{

/// Risk classification for an agent tool invocation.
enum class ToolRisk : uint8_t
{
    ReadOnly,    ///< No side effects — auto-approved.
    Mutating,    ///< Creates or modifies files/state — prompt once per session.
    Destructive, ///< Irreversible or dangerous — always prompt.
    Blocked,     ///< Unconditionally denied (e.g. interactive commands).
};

/// Policy controlling how the permission manager handles prompts.
enum class PermissionPolicy : uint8_t
{
    Ask,          ///< Prompt for Mutating (once) and Destructive (always).
    TrustSession, ///< Auto-approve Mutating; still prompt for Destructive.
    TrustAll,     ///< Auto-approve everything (no prompts).
    ReadOnly,     ///< Deny all non-ReadOnly tools.
};

/// Result of a permission check.
enum class PermissionDecision : uint8_t
{
    Approved,  ///< Tool execution is allowed.
    Denied,    ///< User explicitly denied execution.
    Blocked,   ///< Tool is unconditionally blocked (e.g. interactive command).
    Cancelled, ///< User cancelled the prompt (Escape).
};

/// Human-readable description of a permission prompt.
struct PermissionPrompt
{
    std::string toolName;       ///< Name of the tool requesting permission.
    ToolRisk riskLevel;         ///< Classified risk level.
    std::string description;    ///< Human-readable: "Write to /foo/bar.cpp".
    std::string commandPreview; ///< For shell/git: the actual command string.
};

/// Callback invoked to prompt the user for permission.
/// @param prompt The permission prompt to display.
/// @return The user's decision.
using PermissionPromptCallback = std::function<PermissionDecision(PermissionPrompt const&)>;

/// Configuration for the permission manager.
struct PermissionConfig
{
    PermissionPolicy policy = PermissionPolicy::Ask; ///< Active permission policy.
    std::vector<std::string> trustedTools;           ///< Tools that are always auto-approved.
    std::vector<std::string> blockedPatterns;        ///< Extra shell patterns to block.
};

/// Centralized permission gate for agent tool execution.
///
/// Classifies tool risk, checks policy and session state, and delegates
/// prompting to a callback. ReadOnly tools are always auto-approved.
/// Mutating tools are prompted once per session (remembered). Destructive
/// tools always prompt. Blocked tools are always denied.
class PermissionManager
{
  public:
    /// @brief Constructs a permission manager with the given configuration.
    /// @param config The permission configuration.
    explicit PermissionManager(PermissionConfig config);

    /// @brief Sets the callback used to prompt the user for permission.
    /// @param callback The prompt callback.
    void setPromptCallback(PermissionPromptCallback callback);

    /// @brief Updates the permission configuration.
    /// @param config The new configuration.
    void setConfig(PermissionConfig config);

    /// @brief Returns the current permission configuration.
    [[nodiscard]] auto config() const noexcept -> PermissionConfig const&;

    /// @brief Checks whether a tool invocation is permitted.
    ///
    /// Uses the tool's base risk level (from classifyRisk()), the current policy,
    /// session approvals, and trusted tool overrides to decide.
    /// @param toolName The tool name.
    /// @param baseRisk The tool's classified risk level.
    /// @param arguments The tool call arguments (used for command preview).
    /// @return The permission decision.
    [[nodiscard]] auto checkPermission(std::string_view toolName,
                                       ToolRisk baseRisk,
                                       nlohmann::json const& arguments) -> PermissionDecision;

    /// @brief Clears all per-session approvals.
    void resetApprovals();

    /// @brief Returns the set of tools approved for this session.
    [[nodiscard]] auto approvedTools() const -> std::set<std::string> const&;

  private:
    /// Builds a human-readable command preview from tool arguments.
    [[nodiscard]] static auto buildCommandPreview(std::string_view toolName, nlohmann::json const& arguments)
        -> std::string;

    /// Builds a human-readable description from tool name and arguments.
    [[nodiscard]] static auto buildDescription(std::string_view toolName, nlohmann::json const& arguments)
        -> std::string;

    /// Checks if a tool is in the trusted tools list.
    [[nodiscard]] auto isTrustedTool(std::string_view toolName) const -> bool;

    PermissionConfig _config;
    PermissionPromptCallback _promptCallback;
    std::set<std::string> _approvedTools;
};

/// Converts a PermissionPolicy to its string representation.
[[nodiscard]] constexpr auto permissionPolicyToString(PermissionPolicy policy) noexcept -> std::string_view
{
    switch (policy)
    {
        case PermissionPolicy::Ask: return "ask";
        case PermissionPolicy::TrustSession: return "trust_session";
        case PermissionPolicy::TrustAll: return "trust_all";
        case PermissionPolicy::ReadOnly: return "read_only";
    }
    return "ask";
}

/// Parses a string into a PermissionPolicy enum.
/// @return The matching policy, or PermissionPolicy::Ask if unrecognized.
[[nodiscard]] constexpr auto permissionPolicyFromString(std::string_view str) noexcept -> PermissionPolicy
{
    if (str == "ask")
        return PermissionPolicy::Ask;
    if (str == "trust_session")
        return PermissionPolicy::TrustSession;
    if (str == "trust_all")
        return PermissionPolicy::TrustAll;
    if (str == "read_only")
        return PermissionPolicy::ReadOnly;
    return PermissionPolicy::Ask;
}

} // namespace endo::agent
