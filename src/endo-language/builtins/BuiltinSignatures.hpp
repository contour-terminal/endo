// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file BuiltinSignatures.hpp
/// @brief Single source of truth for all Endo builtin function signatures.
///
/// Registration functions grouped by category. Each function defines signatures
/// (name, parameters, return type) and binds callbacks via a CallbackResolver.
/// If the resolver returns std::nullopt, a no-op callback is bound.

#include <CoreVM/CoreVM.hpp>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace endo
{

/// Given a builtin name and parameter count, return the callback to bind.
/// Return std::nullopt to use a default no-op.
using CallbackResolver =
    std::function<std::optional<CoreVM::NativeCallback::Functor>(std::string_view name, size_t arity)>;

/// Registers F# language builtins: print, println, list_*, string_*, env.*, which_find,
/// object_to_string, list_to_string, display_result, format_*, mode_*, rand, fetch, export.
void registerFSharpBuiltins(CoreVM::Runtime& rt, CallbackResolver const& resolve);

/// Registers shell command builtins: exit, export (shell-style), set, unset, cd, read,
/// jobs, fg, bg, wait, bind, which, callproc, getvar.*, setvar.*.
void registerShellBuiltins(CoreVM::Runtime& rt, CallbackResolver const& resolve);

/// Registers internal VM builtins: internal.cmd_*, internal.redirect_*, internal.subst_*,
/// internal.procsubst_*, internal.for_*, internal.case_*, internal.function_*,
/// expand.*, internal.open_*.
void registerInternalBuiltins(CoreVM::Runtime& rt, CallbackResolver const& resolve);

/// Registers structured data builtins: structured_ps, structured_ls, structured_jobs,
/// structured_docker_*, structured_git_*, open_json, open_csv, from_json, from_csv.
void registerStructuredBuiltins(CoreVM::Runtime& rt, CallbackResolver const& resolve);

/// Registers prompt configuration properties: shell_prompt_*, shell_exit_confirm_timeout,
/// shell_ls_icons, shell_ls_directory_slash.
void registerPromptPropertyBuiltins(CoreVM::Runtime& rt, CallbackResolver const& resolve);

/// Registers agent configuration properties: agent_*, agent_claude_*, agent_openai_*,
/// agent_openai_compat_*, agent_gemini_*, agent_plan_mode_*, agent_explore_*,
/// agent_trace_*, agent_web_search_*.
void registerAgentConfigPropertyBuiltins(CoreVM::Runtime& rt, CallbackResolver const& resolve);

/// Returns the list of user-facing builtin command names for shell completion.
/// Includes shell builtins (cd, exit, ...) and control flow keywords.
std::vector<std::string> userFacingBuiltinNames();

/// Describes a user-facing builtin command for completion and diagnostics.
struct BuiltinInfo
{
    std::string name;        ///< Command name (e.g., "cd", "echo")
    std::string description; ///< Short description for completion display
    bool isProperty = false; ///< True for properties (shell_prompt_*, agent_*), false for commands
    std::string detail;      ///< Detailed documentation (markdown) for detail panel
};

/// Returns the list of user-facing builtins with descriptions.
/// Includes shell builtins (cd, exit, ...), control flow keywords, and F# output functions.
[[nodiscard]] std::vector<BuiltinInfo> userFacingBuiltins();

} // namespace endo
