// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <agent/PermissionManager.hpp>

using namespace endo::agent;

TEST_CASE("PermissionManager.readonly_auto_approved", "[agent][permissions]")
{
    auto pm = PermissionManager(PermissionConfig {});
    auto callbackInvoked = false;
    pm.setPromptCallback([&](PermissionPrompt const&) {
        callbackInvoked = true;
        return PermissionDecision::Denied;
    });

    auto const decision = pm.checkPermission("read_file", ToolRisk::ReadOnly, {});
    CHECK(decision == PermissionDecision::Approved);
    CHECK_FALSE(callbackInvoked);
}

TEST_CASE("PermissionManager.blocked_always_denied", "[agent][permissions]")
{
    auto pm = PermissionManager(PermissionConfig {});
    auto callbackInvoked = false;
    pm.setPromptCallback([&](PermissionPrompt const&) {
        callbackInvoked = true;
        return PermissionDecision::Approved;
    });

    auto const decision = pm.checkPermission("shell_execute", ToolRisk::Blocked, {});
    CHECK(decision == PermissionDecision::Blocked);
    CHECK_FALSE(callbackInvoked);
}

TEST_CASE("PermissionManager.mutating_prompts_once_then_remembered", "[agent][permissions]")
{
    auto pm = PermissionManager(PermissionConfig {});
    auto promptCount = 0;
    pm.setPromptCallback([&](PermissionPrompt const&) {
        ++promptCount;
        return PermissionDecision::Approved;
    });

    // First call: prompts
    auto d1 = pm.checkPermission("write_file", ToolRisk::Mutating, {});
    CHECK(d1 == PermissionDecision::Approved);
    CHECK(promptCount == 1);

    // Second call: auto-approved (remembered)
    auto d2 = pm.checkPermission("write_file", ToolRisk::Mutating, {});
    CHECK(d2 == PermissionDecision::Approved);
    CHECK(promptCount == 1);
}

TEST_CASE("PermissionManager.destructive_always_prompts", "[agent][permissions]")
{
    auto pm = PermissionManager(PermissionConfig {});
    auto promptCount = 0;
    pm.setPromptCallback([&](PermissionPrompt const&) {
        ++promptCount;
        return PermissionDecision::Approved;
    });

    (void) pm.checkPermission("git", ToolRisk::Destructive, {});
    (void) pm.checkPermission("git", ToolRisk::Destructive, {});
    (void) pm.checkPermission("git", ToolRisk::Destructive, {});
    CHECK(promptCount == 3);
}

TEST_CASE("PermissionManager.trust_all_policy", "[agent][permissions]")
{
    auto pm = PermissionManager(PermissionConfig { .policy = PermissionPolicy::TrustAll });
    auto callbackInvoked = false;
    pm.setPromptCallback([&](PermissionPrompt const&) {
        callbackInvoked = true;
        return PermissionDecision::Denied;
    });

    CHECK(pm.checkPermission("write_file", ToolRisk::Mutating, {}) == PermissionDecision::Approved);
    CHECK(pm.checkPermission("git", ToolRisk::Destructive, {}) == PermissionDecision::Approved);
    CHECK_FALSE(callbackInvoked);
}

TEST_CASE("PermissionManager.read_only_policy", "[agent][permissions]")
{
    auto pm = PermissionManager(PermissionConfig { .policy = PermissionPolicy::ReadOnly });
    pm.setPromptCallback([](PermissionPrompt const&) { return PermissionDecision::Approved; });

    CHECK(pm.checkPermission("read_file", ToolRisk::ReadOnly, {}) == PermissionDecision::Approved);
    CHECK(pm.checkPermission("write_file", ToolRisk::Mutating, {}) == PermissionDecision::Denied);
    CHECK(pm.checkPermission("git", ToolRisk::Destructive, {}) == PermissionDecision::Denied);
}

TEST_CASE("PermissionManager.trust_session_policy", "[agent][permissions]")
{
    auto pm = PermissionManager(PermissionConfig { .policy = PermissionPolicy::TrustSession });
    auto promptCount = 0;
    pm.setPromptCallback([&](PermissionPrompt const&) {
        ++promptCount;
        return PermissionDecision::Approved;
    });

    // Mutating: auto-approved with TrustSession
    CHECK(pm.checkPermission("write_file", ToolRisk::Mutating, {}) == PermissionDecision::Approved);
    CHECK(promptCount == 0);

    // Destructive: still prompts
    CHECK(pm.checkPermission("git", ToolRisk::Destructive, {}) == PermissionDecision::Approved);
    CHECK(promptCount == 1);
}

TEST_CASE("PermissionManager.trusted_tool_override", "[agent][permissions]")
{
    auto pm = PermissionManager(PermissionConfig { .trustedTools = { "write_file", "edit_file" } });
    auto callbackInvoked = false;
    pm.setPromptCallback([&](PermissionPrompt const&) {
        callbackInvoked = true;
        return PermissionDecision::Denied;
    });

    CHECK(pm.checkPermission("write_file", ToolRisk::Mutating, {}) == PermissionDecision::Approved);
    CHECK(pm.checkPermission("edit_file", ToolRisk::Mutating, {}) == PermissionDecision::Approved);
    CHECK_FALSE(callbackInvoked);

    // Non-trusted tool still prompts
    CHECK(pm.checkPermission("shell_execute", ToolRisk::Mutating, {}) == PermissionDecision::Denied);
    CHECK(callbackInvoked);
}

TEST_CASE("PermissionManager.reset_approvals", "[agent][permissions]")
{
    auto pm = PermissionManager(PermissionConfig {});
    auto promptCount = 0;
    pm.setPromptCallback([&](PermissionPrompt const&) {
        ++promptCount;
        return PermissionDecision::Approved;
    });

    (void) pm.checkPermission("write_file", ToolRisk::Mutating, {});
    CHECK(promptCount == 1);

    (void) pm.checkPermission("write_file", ToolRisk::Mutating, {});
    CHECK(promptCount == 1); // remembered

    pm.resetApprovals();
    CHECK(pm.approvedTools().empty());

    (void) pm.checkPermission("write_file", ToolRisk::Mutating, {});
    CHECK(promptCount == 2); // prompts again
}

TEST_CASE("PermissionManager.user_denial_propagation", "[agent][permissions]")
{
    auto pm = PermissionManager(PermissionConfig {});
    pm.setPromptCallback([](PermissionPrompt const&) { return PermissionDecision::Denied; });

    auto const decision = pm.checkPermission("write_file", ToolRisk::Mutating, {});
    CHECK(decision == PermissionDecision::Denied);
    // Denied tool is NOT remembered as approved.
    CHECK(pm.approvedTools().empty());
}

TEST_CASE("PermissionManager.user_cancellation_propagation", "[agent][permissions]")
{
    auto pm = PermissionManager(PermissionConfig {});
    pm.setPromptCallback([](PermissionPrompt const&) { return PermissionDecision::Cancelled; });

    auto const decision = pm.checkPermission("write_file", ToolRisk::Mutating, {});
    CHECK(decision == PermissionDecision::Cancelled);
    CHECK(pm.approvedTools().empty());
}

TEST_CASE("PermissionManager.no_callback_failsafe_denial", "[agent][permissions]")
{
    auto pm = PermissionManager(PermissionConfig {});
    // No callback set — should fail safe to Denied.
    auto const decision = pm.checkPermission("write_file", ToolRisk::Mutating, {});
    CHECK(decision == PermissionDecision::Denied);
}

TEST_CASE("PermissionManager.description_for_known_tools", "[agent][permissions]")
{
    auto pm = PermissionManager(PermissionConfig {});
    PermissionPrompt capturedPrompt;
    pm.setPromptCallback([&](PermissionPrompt const& p) {
        capturedPrompt = p;
        return PermissionDecision::Approved;
    });

    auto const args = nlohmann::json { { "path", "/foo/bar.cpp" } };
    (void) pm.checkPermission("write_file", ToolRisk::Mutating, args);
    CHECK(capturedPrompt.description.find("/foo/bar.cpp") != std::string::npos);
}

TEST_CASE("PermissionManager.command_preview_for_shell", "[agent][permissions]")
{
    auto pm = PermissionManager(PermissionConfig {});
    PermissionPrompt capturedPrompt;
    pm.setPromptCallback([&](PermissionPrompt const& p) {
        capturedPrompt = p;
        return PermissionDecision::Approved;
    });

    auto const args = nlohmann::json { { "command", "make -j8" } };
    (void) pm.checkPermission("shell_execute", ToolRisk::Mutating, args);
    CHECK(capturedPrompt.commandPreview == "make -j8");
}

TEST_CASE("PermissionManager.command_preview_for_git", "[agent][permissions]")
{
    auto pm = PermissionManager(PermissionConfig {});
    PermissionPrompt capturedPrompt;
    pm.setPromptCallback([&](PermissionPrompt const& p) {
        capturedPrompt = p;
        return PermissionDecision::Approved;
    });

    auto const args = nlohmann::json { { "subcommand", "commit" }, { "args", { "-m", "test" } } };
    (void) pm.checkPermission("git", ToolRisk::Mutating, args);
    CHECK(capturedPrompt.commandPreview == "git commit -m test");
}

TEST_CASE("PermissionManager.policy_string_conversion", "[agent][permissions]")
{
    CHECK(permissionPolicyToString(PermissionPolicy::Ask) == "ask");
    CHECK(permissionPolicyToString(PermissionPolicy::TrustSession) == "trust_session");
    CHECK(permissionPolicyToString(PermissionPolicy::TrustAll) == "trust_all");
    CHECK(permissionPolicyToString(PermissionPolicy::ReadOnly) == "read_only");

    CHECK(permissionPolicyFromString("ask") == PermissionPolicy::Ask);
    CHECK(permissionPolicyFromString("trust_session") == PermissionPolicy::TrustSession);
    CHECK(permissionPolicyFromString("trust_all") == PermissionPolicy::TrustAll);
    CHECK(permissionPolicyFromString("read_only") == PermissionPolicy::ReadOnly);
    CHECK(permissionPolicyFromString("invalid") == PermissionPolicy::Ask);
}
