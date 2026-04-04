// SPDX-License-Identifier: Apache-2.0
#include <endo-language/builtins/PropertyDescriptors.hpp>

#include <algorithm>
#include <array>
#include <vector>

namespace endo
{

// ---------------------------------------------------------------------------
// Enum value arrays (moved from CompletionCandidates.cpp)
// ---------------------------------------------------------------------------

// clang-format off
static constexpr std::array presetValues = {
    EnumValueEntry { .value="minimal-arrow", .description="Clean arrow-based prompt" },
    EnumValueEntry { .value="lambda-clean", .description="Lambda symbol prompt" },
    EnumValueEntry { .value="opencode-bar", .description="OpenCode-style bar prompt" },
    EnumValueEntry { .value="powerline", .description="Powerline-style segments" },
    EnumValueEntry { .value="transient", .description="Minimal transient prompt" },
    EnumValueEntry { .value="dashboard", .description="Dashboard-style prompt" },
    EnumValueEntry { .value="boxed-module", .description="Boxed module prompt" },
    EnumValueEntry { .value="gradient-glow", .description="Gradient glow prompt" },
    EnumValueEntry { .value="context-adaptive", .description="Context-adaptive prompt" },
    EnumValueEntry { .value="endo-signature", .description="Endo signature prompt" },
};

static constexpr std::array layoutValues = {
    EnumValueEntry { .value="single-line", .description="Single line prompt" },
    EnumValueEntry { .value="two-line", .description="Two line prompt" },
    EnumValueEntry { .value="boxed", .description="Boxed prompt layout" },
    EnumValueEntry { .value="powerline", .description="Powerline prompt layout" },
};

static constexpr std::array separatorValues = {
    EnumValueEntry { .value="none", .description="No separator" },
    EnumValueEntry { .value="bar", .description="Bar separator (|)" },
    EnumValueEntry { .value="powerline", .description="Powerline separator" },
    EnumValueEntry { .value="rounded", .description="Rounded separator" },
    EnumValueEntry { .value="boxed", .description="Boxed separator" },
};

static constexpr std::array transientValues = {
    EnumValueEntry { .value="off", .description="Disable transient prompt" },
    EnumValueEntry { .value="minimal", .description="Minimal transient prompt" },
    EnumValueEntry { .value="arrow", .description="Arrow transient prompt" },
};

static constexpr std::array providerValues = {
    EnumValueEntry { .value="claude", .description="Anthropic Claude" },
    EnumValueEntry { .value="openai", .description="OpenAI" },
    EnumValueEntry { .value="gemini", .description="Google Gemini" },
    EnumValueEntry { .value="openai_compat", .description="OpenAI-compatible endpoint" },
    EnumValueEntry { .value="local", .description="Local model (llama.cpp)" },
};

static constexpr std::array boolValues = {
    EnumValueEntry { .value="true", .description="Enable" },
    EnumValueEntry { .value="false", .description="Disable" },
};

static constexpr std::array colorValues = {
    EnumValueEntry { .value="transparent", .description="Use terminal default background" },
    EnumValueEntry { .value="theme", .description="Use theme default color" },
};
// clang-format on

static constexpr std::array webSearchEngineValues = {
    EnumValueEntry { .value = "duckduckgo", .description = "DuckDuckGo (no API key required)" },
    EnumValueEntry { .value = "brave", .description = "Brave Search" },
    EnumValueEntry { .value = "google", .description = "Google Custom Search" },
};

static constexpr std::array claudeModelValues = {
    EnumValueEntry { .value = "claude-opus-4-6", .description = "Claude Opus 4.6" },
    EnumValueEntry { .value = "claude-sonnet-4-6", .description = "Claude Sonnet 4.6" },
    EnumValueEntry { .value = "claude-haiku-4-5-20251001", .description = "Claude Haiku 4.5" },
    EnumValueEntry { .value = "claude-sonnet-4-5-20250929", .description = "Claude Sonnet 4.5" },
    EnumValueEntry { .value = "claude-opus-4-20250514", .description = "Claude Opus 4" },
};

static constexpr std::array openaiModelValues = {
    EnumValueEntry { .value = "gpt-4o", .description = "GPT-4o" },
    EnumValueEntry { .value = "gpt-4o-mini", .description = "GPT-4o Mini" },
    EnumValueEntry { .value = "o3-mini", .description = "O3 Mini" },
    EnumValueEntry { .value = "o1", .description = "O1" },
};

static constexpr std::array geminiModelValues = {
    EnumValueEntry { .value = "gemini-2.5-flash", .description = "Gemini 2.5 Flash" },
    EnumValueEntry { .value = "gemini-2.5-pro", .description = "Gemini 2.5 Pro" },
    EnumValueEntry { .value = "gemini-2.0-flash", .description = "Gemini 2.0 Flash" },
};

static constexpr std::array thinkingModeValues = {
    EnumValueEntry { .value = "off", .description = "No thinking (provider default)" },
    EnumValueEntry { .value = "normal", .description = "Moderate thinking budget" },
    EnumValueEntry { .value = "extended", .description = "Maximum thinking budget" },
};

static constexpr std::array authTypeValues = {
    EnumValueEntry { .value = "auto", .description = "Auto-detect (OAuth preferred)" },
    EnumValueEntry { .value = "oauth", .description = "OAuth authentication" },
    EnumValueEntry { .value = "api_key", .description = "API key authentication" },
};

static constexpr std::array errorRecoveryActionValues = {
    EnumValueEntry { .value = "ask", .description = "Ask user before analyzing (default)" },
    EnumValueEntry { .value = "analyze", .description = "Automatically analyze failed commands" },
    EnumValueEntry { .value = "ignore", .description = "Do nothing on command failure" },
};

/// Combined model values for agent_error_recovery_model (all providers).
static constexpr std::array errorRecoveryModelValues = {
    // Claude models
    EnumValueEntry { .value = "claude-opus-4-6", .description = "Claude Opus 4.6" },
    EnumValueEntry { .value = "claude-sonnet-4-6", .description = "Claude Sonnet 4.6" },
    EnumValueEntry { .value = "claude-haiku-4-5-20251001", .description = "Claude Haiku 4.5" },
    EnumValueEntry { .value = "claude-sonnet-4-5-20250929", .description = "Claude Sonnet 4.5" },
    EnumValueEntry { .value = "claude-opus-4-20250514", .description = "Claude Opus 4" },
    // OpenAI models
    EnumValueEntry { .value = "gpt-4o", .description = "GPT-4o" },
    EnumValueEntry { .value = "gpt-4o-mini", .description = "GPT-4o Mini" },
    EnumValueEntry { .value = "o3-mini", .description = "O3 Mini" },
    EnumValueEntry { .value = "o1", .description = "O1" },
    // Gemini models
    EnumValueEntry { .value = "gemini-2.5-flash", .description = "Gemini 2.5 Flash" },
    EnumValueEntry { .value = "gemini-2.5-pro", .description = "Gemini 2.5 Pro" },
    EnumValueEntry { .value = "gemini-2.0-flash", .description = "Gemini 2.0 Flash" },
};

// ---------------------------------------------------------------------------
// Prompt/Shell property descriptors
// ---------------------------------------------------------------------------

static constexpr std::array promptProperties = {
    PropertyDescriptor {
        .name = "shell_prompt_preset",
        .type = CoreVM::LiteralType::String,
        .description = "Prompt theme preset",
        .detail = "**shell_prompt_preset** -- property\n\nSets the prompt theme "
                  "preset.\n\n```\nshell_prompt_preset powerline\n```",
        .readOnly = false,
        .enumValues = presetValues,
    },
    PropertyDescriptor {
        .name = "shell_prompt_indicator",
        .type = CoreVM::LiteralType::String,
        .description = "Prompt indicator character(s)",
        .detail = "**shell_prompt_indicator** -- property\n\nSets the prompt indicator character(s).",
    },
    PropertyDescriptor {
        .name = "shell_prompt_layout",
        .type = CoreVM::LiteralType::String,
        .description = "Prompt layout style",
        .detail = "**shell_prompt_layout** -- property\n\nSets the prompt layout style (single-line, "
                  "two-line, boxed, powerline).",
        .readOnly = false,
        .enumValues = layoutValues,
    },
    PropertyDescriptor {
        .name = "shell_prompt_separator",
        .type = CoreVM::LiteralType::String,
        .description = "Prompt separator style",
        .detail =
            "**shell_prompt_separator** -- property\n\nSets the separator style between prompt modules.",
        .readOnly = false,
        .enumValues = separatorValues,
    },
    PropertyDescriptor {
        .name = "shell_prompt_transient",
        .type = CoreVM::LiteralType::String,
        .description = "Transient prompt mode",
        .detail = "**shell_prompt_transient** -- property\n\nControls transient prompt behavior (off, "
                  "minimal, arrow).",
        .readOnly = false,
        .enumValues = transientValues,
    },
    PropertyDescriptor {
        .name = "shell_prompt_duration_threshold",
        .type = CoreVM::LiteralType::Number,
        .description = "Duration display threshold (ms)",
        .detail = "**shell_prompt_duration_threshold** -- property\n\nMinimum command duration (ms) before "
                  "showing elapsed time.",
    },
    PropertyDescriptor {
        .name = "shell_prompt_spacing",
        .type = CoreVM::LiteralType::Number,
        .description = "Blank lines above/below prompt (0 or 1)",
        .detail =
            "**shell_prompt_spacing** -- property\n\nNumber of blank lines above/below the prompt (0 or 1).",
    },
    PropertyDescriptor {
        .name = "shell_exit_confirm_timeout",
        .type = CoreVM::LiteralType::Number,
        .description = "Exit confirmation timeout (ms)",
        .detail = "**shell_exit_confirm_timeout** -- property\n\nTimeout (ms) for exit confirmation when "
                  "background jobs exist.",
    },
    PropertyDescriptor {
        .name = "shell_ls_icons",
        .type = CoreVM::LiteralType::Boolean,
        .description = "Show Nerd Font icons in ls output (default: true)",
        .detail = "**shell_ls_icons** -- property\n\nWhether to show Nerd Font icons in `ls` output.",
    },
    PropertyDescriptor {
        .name = "shell_ls_directory_slash",
        .type = CoreVM::LiteralType::Boolean,
        .description = "Append trailing '/' to directory names in ls output (default: true)",
        .detail = "**shell_ls_directory_slash** -- property\n\nWhether to append trailing `/` to directory "
                  "names in `ls` output.",
    },
    PropertyDescriptor {
        .name = "shell_is_interactive",
        .type = CoreVM::LiteralType::Boolean,
        .description = "Whether running interactively (read-only)",
        .detail = "**shell_is_interactive** -- property (read-only)\n\nReturns whether the shell is running "
                  "interactively.",
        .readOnly = true, // readOnly
    },
    // --- Prompt color overrides ---
    PropertyDescriptor {
        .name = "shell_prompt_color_background",
        .type = CoreVM::LiteralType::String,
        .description = "Prompt background color (#RRGGBB, transparent, or theme)",
        .detail = "**shell_prompt_color_background** -- property\n\nSets the prompt background color.\n\n- "
                  "`transparent` — use terminal default background\n- `#RRGGBB` — custom solid color\n- "
                  "`theme` — reset to theme default\n\n```\nshell_prompt_color_background "
                  "transparent\nshell_prompt_color_background \"#2D3237\"\nshell_prompt_color_background "
                  "theme\n```",
        .readOnly = false,
        .enumValues = colorValues,
    },
    PropertyDescriptor {
        .name = "shell_prompt_color_path",
        .type = CoreVM::LiteralType::String,
        .description = "Path module text color (#RRGGBB, gradient, or theme)",
        .detail =
            "**shell_prompt_color_path** -- property\n\nSets the path module text color. Supports solid "
            "colors and gradients.\n\n```\nshell_prompt_color_path \"#FF6600\"\nshell_prompt_color_path "
            "\"#5078FF:#00DCC8\"\nshell_prompt_color_path theme\n```",
        .readOnly = false,
        .enumValues = colorValues,
    },
    PropertyDescriptor {
        .name = "shell_prompt_color_git_clean",
        .type = CoreVM::LiteralType::String,
        .description = "Git branch color when clean (#RRGGBB, gradient, or theme)",
        .detail = "**shell_prompt_color_git_clean** -- property\n\nSets the git branch text color when the "
                  "repository is clean.",
        .readOnly = false,
        .enumValues = colorValues,
    },
    PropertyDescriptor {
        .name = "shell_prompt_color_git_dirty",
        .type = CoreVM::LiteralType::String,
        .description = "Git branch color when dirty (#RRGGBB, gradient, or theme)",
        .detail = "**shell_prompt_color_git_dirty** -- property\n\nSets the git branch text color when there "
                  "are unstaged changes.",
        .readOnly = false,
        .enumValues = colorValues,
    },
    PropertyDescriptor {
        .name = "shell_prompt_color_git_staged",
        .type = CoreVM::LiteralType::String,
        .description = "Git indicator color when staged (#RRGGBB, gradient, or theme)",
        .detail = "**shell_prompt_color_git_staged** -- property\n\nSets the git indicator color when staged "
                  "changes exist.",
        .readOnly = false,
        .enumValues = colorValues,
    },
    PropertyDescriptor {
        .name = "shell_prompt_color_indicator",
        .type = CoreVM::LiteralType::String,
        .description = "Input indicator color (#RRGGBB, gradient, or theme)",
        .detail = "**shell_prompt_color_indicator** -- property\n\nSets the input line indicator color "
                  "(e.g., `|> `).",
        .readOnly = false,
        .enumValues = colorValues,
    },
    PropertyDescriptor {
        .name = "shell_prompt_color_indicator_error",
        .type = CoreVM::LiteralType::String,
        .description = "Indicator color on error (#RRGGBB, gradient, or theme)",
        .detail = "**shell_prompt_color_indicator_error** -- property\n\nSets the indicator color when the "
                  "last command failed (non-zero exit).",
        .readOnly = false,
        .enumValues = colorValues,
    },
    PropertyDescriptor {
        .name = "shell_prompt_color_exit_code",
        .type = CoreVM::LiteralType::String,
        .description = "Exit code badge color (#RRGGBB, gradient, or theme)",
        .detail = "**shell_prompt_color_exit_code** -- property\n\nSets the exit code badge color.",
        .readOnly = false,
        .enumValues = colorValues,
    },
    PropertyDescriptor {
        .name = "shell_prompt_color_duration",
        .type = CoreVM::LiteralType::String,
        .description = "Duration badge color (#RRGGBB, gradient, or theme)",
        .detail = "**shell_prompt_color_duration** -- property\n\nSets the command duration badge color.",
        .readOnly = false,
        .enumValues = colorValues,
    },
    PropertyDescriptor {
        .name = "shell_prompt_color_hostname",
        .type = CoreVM::LiteralType::String,
        .description = "Hostname text color (#RRGGBB, gradient, or theme)",
        .detail = "**shell_prompt_color_hostname** -- property\n\nSets the hostname module text color (shown "
                  "in SSH sessions).",
        .readOnly = false,
        .enumValues = colorValues,
    },
    PropertyDescriptor {
        .name = "shell_prompt_color_separator",
        .type = CoreVM::LiteralType::String,
        .description = "Separator/bar color (#RRGGBB, gradient, or theme)",
        .detail = "**shell_prompt_color_separator** -- property\n\nSets the left bar / separator color.",
        .readOnly = false,
        .enumValues = colorValues,
    },
    PropertyDescriptor {
        .name = "shell_prompt_color_badge",
        .type = CoreVM::LiteralType::String,
        .description = "Badge background color (#RRGGBB or theme)",
        .detail = "**shell_prompt_color_badge** -- property\n\nSets the badge background color (used by F# "
                  "mode, structured output).",
        .readOnly = false,
        .enumValues = colorValues,
    },
    PropertyDescriptor {
        .name = "shell_prompt_color_badge_text",
        .type = CoreVM::LiteralType::String,
        .description = "Badge text color (#RRGGBB or theme)",
        .detail = "**shell_prompt_color_badge_text** -- property\n\nSets the badge text color.",
        .readOnly = false,
        .enumValues = colorValues,
    },
    PropertyDescriptor {
        .name = "shell_prompt_color_clock",
        .type = CoreVM::LiteralType::String,
        .description = "Clock text color (#RRGGBB or theme)",
        .detail = "**shell_prompt_color_clock** -- property\n\nSets the clock module text color.",
        .readOnly = false,
        .enumValues = colorValues,
    },
};

// ---------------------------------------------------------------------------
// Agent configuration property descriptors
// ---------------------------------------------------------------------------

static constexpr std::array agentProperties = {
    // --- Top-level agent settings ---
    PropertyDescriptor {
        .name = "agent_provider",
        .type = CoreVM::LiteralType::String,
        .description = "Active AI provider",
        .detail = "**agent_provider** -- property\n\nSets the active AI provider (claude, openai, gemini, "
                  "openai_compat).",
        .readOnly = false,
        .enumValues = providerValues,
    },
    PropertyDescriptor {
        .name = "agent_prompt_indicator",
        .type = CoreVM::LiteralType::String,
        .description = "Agent prompt indicator character(s)",
        .detail = "**agent_prompt_indicator** -- property\n\nSets the agent prompt indicator character(s).",
    },
    PropertyDescriptor {
        .name = "agent_max_tool_result_size",
        .type = CoreVM::LiteralType::Number,
        .description = "Max bytes for tool result truncation",
        .detail = "**agent_max_tool_result_size** -- property\n\nMaximum bytes for tool result truncation.",
    },
    PropertyDescriptor {
        .name = "agent_log_tool_uses",
        .type = CoreVM::LiteralType::Boolean,
        .description = "Enable/disable tool invocation logging",
        .detail = "**agent_log_tool_uses** -- property\n\nEnables or disables logging of tool invocations.",
        .readOnly = false,
        .enumValues = boolValues,
    },
    // --- Claude provider ---
    PropertyDescriptor {
        .name = "agent_claude_api_key",
        .type = CoreVM::LiteralType::String,
        .description = "Claude API key",
        .detail = "**agent_claude_api_key** -- property\n\nSets the Claude API key.",
    },
    PropertyDescriptor {
        .name = "agent_claude_api_key_env",
        .type = CoreVM::LiteralType::String,
        .description = "Claude API key environment variable",
        .detail = "**agent_claude_api_key_env** -- property\n\nEnvironment variable name containing the "
                  "Claude API key.",
    },
    PropertyDescriptor {
        .name = "agent_claude_model",
        .type = CoreVM::LiteralType::String,
        .description = "Claude model identifier",
        .detail = "**agent_claude_model** -- property\n\nSets the Claude model identifier.",
        .readOnly = false,
        .enumValues = claudeModelValues,
    },
    PropertyDescriptor {
        .name = "agent_claude_max_tokens",
        .type = CoreVM::LiteralType::Number,
        .description = "Claude max output tokens",
        .detail = "**agent_claude_max_tokens** -- property\n\nMaximum output tokens for Claude responses.",
    },
    PropertyDescriptor {
        .name = "agent_claude_thinking_mode",
        .type = CoreVM::LiteralType::String,
        .description = "Claude thinking/reasoning mode (off/normal/extended)",
        .detail = "**agent_claude_thinking_mode** -- property\n\nSets Claude thinking/reasoning mode (off, "
                  "normal, extended).",
        .readOnly = false,
        .enumValues = thinkingModeValues,
    },
    PropertyDescriptor {
        .name = "agent_claude_prompt_caching",
        .type = CoreVM::LiteralType::Boolean,
        .description = "Enable Claude prompt caching (true/false)",
        .detail = "**agent_claude_prompt_caching** -- property\n\nEnables or disables Claude prompt caching.",
    },
    PropertyDescriptor {
        .name = "agent_claude_auth_type",
        .type = CoreVM::LiteralType::String,
        .description = "Claude auth method (auto/oauth/api_key)",
        .detail = "**agent_claude_auth_type** -- property\n\nSets the Claude authentication method (auto, "
                  "oauth, api_key).",
        .readOnly = false,
        .enumValues = authTypeValues,
    },
    // --- OpenAI provider ---
    PropertyDescriptor {
        .name = "agent_openai_api_key",
        .type = CoreVM::LiteralType::String,
        .description = "OpenAI API key",
        .detail = "**agent_openai_api_key** -- property\n\nSets the OpenAI API key.",
    },
    PropertyDescriptor {
        .name = "agent_openai_api_key_env",
        .type = CoreVM::LiteralType::String,
        .description = "OpenAI API key environment variable",
        .detail = "**agent_openai_api_key_env** -- property\n\nEnvironment variable name containing the "
                  "OpenAI API key.",
    },
    PropertyDescriptor {
        .name = "agent_openai_model",
        .type = CoreVM::LiteralType::String,
        .description = "OpenAI model identifier",
        .detail = "**agent_openai_model** -- property\n\nSets the OpenAI model identifier.",
        .readOnly = false,
        .enumValues = openaiModelValues,
    },
    PropertyDescriptor {
        .name = "agent_openai_base_url",
        .type = CoreVM::LiteralType::String,
        .description = "OpenAI base URL",
        .detail = "**agent_openai_base_url** -- property\n\nSets the OpenAI API base URL.",
    },
    PropertyDescriptor {
        .name = "agent_openai_max_tokens",
        .type = CoreVM::LiteralType::Number,
        .description = "OpenAI max output tokens",
        .detail = "**agent_openai_max_tokens** -- property\n\nMaximum output tokens for OpenAI responses.",
    },
    PropertyDescriptor {
        .name = "agent_openai_thinking_mode",
        .type = CoreVM::LiteralType::String,
        .description = "OpenAI thinking/reasoning mode",
        .detail = "**agent_openai_thinking_mode** -- property\n\nSets OpenAI thinking/reasoning mode.",
        .readOnly = false,
        .enumValues = thinkingModeValues,
    },
    // --- OpenAI-compatible provider ---
    PropertyDescriptor {
        .name = "agent_openai_compat_api_key",
        .type = CoreVM::LiteralType::String,
        .description = "OpenAI-compatible API key",
        .detail = "**agent_openai_compat_api_key** -- property\n\nSets the OpenAI-compatible API key.",
    },
    PropertyDescriptor {
        .name = "agent_openai_compat_api_key_env",
        .type = CoreVM::LiteralType::String,
        .description = "OpenAI-compatible API key environment variable",
        .detail = "**agent_openai_compat_api_key_env** -- property\n\nEnvironment variable name containing "
                  "the API key.",
    },
    PropertyDescriptor {
        .name = "agent_openai_compat_model",
        .type = CoreVM::LiteralType::String,
        .description = "OpenAI-compatible model identifier",
        .detail = "**agent_openai_compat_model** -- property\n\nSets the OpenAI-compatible model identifier.",
        .readOnly = false,
        .enumValues = openaiModelValues,
    },
    PropertyDescriptor {
        .name = "agent_openai_compat_base_url",
        .type = CoreVM::LiteralType::String,
        .description = "OpenAI-compatible base URL",
        .detail = "**agent_openai_compat_base_url** -- property\n\nSets the OpenAI-compatible API base URL.",
    },
    PropertyDescriptor {
        .name = "agent_openai_compat_max_tokens",
        .type = CoreVM::LiteralType::Number,
        .description = "OpenAI-compatible max output tokens",
        .detail = "**agent_openai_compat_max_tokens** -- property\n\nMaximum output tokens.",
    },
    PropertyDescriptor {
        .name = "agent_openai_compat_thinking_mode",
        .type = CoreVM::LiteralType::String,
        .description = "OpenAI-compatible thinking mode",
        .detail = "**agent_openai_compat_thinking_mode** -- property\n\nSets the thinking mode.",
        .readOnly = false,
        .enumValues = thinkingModeValues,
    },
    // --- Gemini provider ---
    PropertyDescriptor {
        .name = "agent_gemini_api_key",
        .type = CoreVM::LiteralType::String,
        .description = "Gemini API key",
        .detail = "**agent_gemini_api_key** -- property\n\nSets the Gemini API key.",
    },
    PropertyDescriptor {
        .name = "agent_gemini_api_key_env",
        .type = CoreVM::LiteralType::String,
        .description = "Gemini API key environment variable",
        .detail = "**agent_gemini_api_key_env** -- property\n\nEnvironment variable name containing the "
                  "Gemini API key.",
    },
    PropertyDescriptor {
        .name = "agent_gemini_model",
        .type = CoreVM::LiteralType::String,
        .description = "Gemini model identifier",
        .detail = "**agent_gemini_model** -- property\n\nSets the Gemini model identifier.",
        .readOnly = false,
        .enumValues = geminiModelValues,
    },
    PropertyDescriptor {
        .name = "agent_gemini_max_tokens",
        .type = CoreVM::LiteralType::Number,
        .description = "Gemini max output tokens",
        .detail = "**agent_gemini_max_tokens** -- property\n\nMaximum output tokens for Gemini responses.",
    },
    PropertyDescriptor {
        .name = "agent_gemini_thinking_mode",
        .type = CoreVM::LiteralType::String,
        .description = "Gemini thinking/reasoning mode",
        .detail = "**agent_gemini_thinking_mode** -- property\n\nSets Gemini thinking/reasoning mode.",
        .readOnly = false,
        .enumValues = thinkingModeValues,
    },
    // --- Plan mode ---
    PropertyDescriptor {
        .name = "agent_plan_mode_enabled",
        .type = CoreVM::LiteralType::Boolean,
        .description = "Enable/disable plan mode",
        .detail = "**agent_plan_mode_enabled** -- property\n\nEnables or disables plan mode.",
        .readOnly = false,
        .enumValues = boolValues,
    },
    PropertyDescriptor {
        .name = "agent_plan_mode_pause_between_steps",
        .type = CoreVM::LiteralType::Boolean,
        .description = "Pause for confirmation between plan steps",
        .detail = "**agent_plan_mode_pause_between_steps** -- property\n\nPause for confirmation between "
                  "plan steps.",
        .readOnly = false,
        .enumValues = boolValues,
    },
    PropertyDescriptor {
        .name = "agent_plan_mode_max_exploration_turns",
        .type = CoreVM::LiteralType::Number,
        .description = "Max exploration iterations",
        .detail = "**agent_plan_mode_max_exploration_turns** -- property\n\nMaximum number of exploration "
                  "iterations.",
    },
    // --- Explore sub-agent ---
    PropertyDescriptor {
        .name = "agent_explore_max_turns",
        .type = CoreVM::LiteralType::Number,
        .description = "Max explore sub-agent iterations",
        .detail = "**agent_explore_max_turns** -- property\n\nMaximum explore sub-agent iterations.",
    },
    // --- Session ---
    PropertyDescriptor {
        .name = "agent_auto_resume",
        .type = CoreVM::LiteralType::Boolean,
        .description = "Auto-resume last agent session",
        .detail = "**agent_auto_resume** -- property\n\nAuto-resume the last agent session on startup.",
    },
    PropertyDescriptor {
        .name = "agent_session_replay",
        .type = CoreVM::LiteralType::Boolean,
        .description = "Replay history when resuming",
        .detail =
            "**agent_session_replay** -- property\n\nReplay conversation history when resuming a session.",
    },
    // --- Trace ---
    PropertyDescriptor {
        .name = "agent_trace_enabled",
        .type = CoreVM::LiteralType::Boolean,
        .description = "Enable/disable trace logging",
        .detail = "**agent_trace_enabled** -- property\n\nEnables or disables trace logging.",
        .readOnly = false,
        .enumValues = boolValues,
    },
    PropertyDescriptor {
        .name = "agent_trace_default_path",
        .type = CoreVM::LiteralType::String,
        .description = "Trace file path",
        .detail = "**agent_trace_default_path** -- property\n\nDefault path for trace log files.",
    },
    PropertyDescriptor {
        .name = "agent_trace_max_files",
        .type = CoreVM::LiteralType::Number,
        .description = "Max trace files to retain",
        .detail = "**agent_trace_max_files** -- property\n\nMaximum number of trace files to retain.",
    },
    PropertyDescriptor {
        .name = "agent_trace_terminal",
        .type = CoreVM::LiteralType::Boolean,
        .description = "Display trace events on terminal",
        .detail =
            "**agent_trace_terminal** -- property\n\nEnables real-time display of agent trace events on the "
            "terminal.\nShows LLM requests/responses, tool calls, compaction events, and errors.",
        .readOnly = false,
        .enumValues = boolValues,
    },
    // --- Permissions ---
    PropertyDescriptor {
        .name = "agent_permissions_policy",
        .type = CoreVM::LiteralType::String,
        .description = "Agent permission policy",
        .detail = "**agent_permissions_policy** -- property\n\nSets the agent permission policy.",
    },
    PropertyDescriptor {
        .name = "agent_trusted_tool",
        .type = CoreVM::LiteralType::Object,
        .description = "Auto-approved tools",
        .detail =
            "**agent_trusted_tool** -- property\n\nTools that are auto-approved without user confirmation.",
    },
    PropertyDescriptor {
        .name = "agent_blocked_pattern",
        .type = CoreVM::LiteralType::Object,
        .description = "Blocked shell command patterns",
        .detail =
            "**agent_blocked_pattern** -- property\n\nShell command patterns blocked from agent execution.",
    },
    // --- Web search ---
    PropertyDescriptor {
        .name = "agent_web_search_engine",
        .type = CoreVM::LiteralType::String,
        .description = "Web search engine",
        .detail = "**agent_web_search_engine** -- property\n\nSets the web search engine (duckduckgo, brave, "
                  "google).",
        .readOnly = false,
        .enumValues = webSearchEngineValues,
    },
    PropertyDescriptor {
        .name = "agent_web_search_api_key",
        .type = CoreVM::LiteralType::String,
        .description = "Web search API key",
        .detail = "**agent_web_search_api_key** -- property\n\nAPI key for the selected web search engine.",
    },
    PropertyDescriptor {
        .name = "agent_web_search_cx",
        .type = CoreVM::LiteralType::String,
        .description = "Google Custom Search Engine ID",
        .detail = "**agent_web_search_cx** -- property\n\nGoogle Custom Search Engine ID.",
    },
    PropertyDescriptor {
        .name = "agent_web_search_max_results",
        .type = CoreVM::LiteralType::Number,
        .description = "Max web search results per query",
        .detail =
            "**agent_web_search_max_results** -- property\n\nMaximum number of web search results per query.",
    },
    // --- Local inference ---
    PropertyDescriptor {
        .name = "agent_local_model_path",
        .type = CoreVM::LiteralType::String,
        .description = "Path to local GGUF model file",
        .detail = "**agent_local_model_path** -- property\n\nSets the path to a local GGUF model file for "
                  "offline inference.",
    },
    PropertyDescriptor {
        .name = "agent_local_model_dir",
        .type = CoreVM::LiteralType::String,
        .description = "Directory for downloaded models",
        .detail =
            "**agent_local_model_dir** -- property\n\nSets the directory where downloaded models are stored.",
    },
    PropertyDescriptor {
        .name = "agent_local_gpu_layers",
        .type = CoreVM::LiteralType::Number,
        .description = "GPU layers to offload (-1 = all, 0 = CPU only)",
        .detail = "**agent_local_gpu_layers** -- property\n\nNumber of model layers to offload to GPU (-1 = "
                  "all, 0 = CPU only).",
    },
    PropertyDescriptor {
        .name = "agent_local_context_size",
        .type = CoreVM::LiteralType::Number,
        .description = "Context window size in tokens",
        .detail = "**agent_local_context_size** -- property\n\nSets the context window size in tokens for "
                  "local inference.",
    },
    PropertyDescriptor {
        .name = "agent_local_threads",
        .type = CoreVM::LiteralType::Number,
        .description = "CPU threads for inference (0 = auto)",
        .detail = "**agent_local_threads** -- property\n\nNumber of CPU threads for local inference (0 = "
                  "auto-detect).",
    },
    PropertyDescriptor {
        .name = "agent_local_batch_size",
        .type = CoreVM::LiteralType::Number,
        .description = "Batch size for prompt evaluation",
        .detail =
            "**agent_local_batch_size** -- property\n\nBatch size for prompt evaluation in local inference.",
    },
    PropertyDescriptor {
        .name = "agent_local_temperature",
        .type = CoreVM::LiteralType::Number,
        .description = "Sampling temperature (value * 100, e.g. 70 = 0.7)",
        .detail = "**agent_local_temperature** -- property\n\nSampling temperature for local inference "
                  "(integer, value * 100).",
    },
    PropertyDescriptor {
        .name = "agent_local_flash_attention",
        .type = CoreVM::LiteralType::Boolean,
        .description = "Enable Flash Attention",
        .detail = "**agent_local_flash_attention** -- property\n\nEnables or disables Flash Attention for "
                  "local inference.",
        .readOnly = false,
        .enumValues = boolValues,
    },
    PropertyDescriptor {
        .name = "agent_local_max_tokens",
        .type = CoreVM::LiteralType::Number,
        .description = "Maximum tokens to generate",
        .detail =
            "**agent_local_max_tokens** -- property\n\nMaximum number of tokens to generate per response.",
    },
    PropertyDescriptor {
        .name = "agent_local_chat_template",
        .type = CoreVM::LiteralType::String,
        .description = "Chat template override (empty = auto-detect)",
        .detail = "**agent_local_chat_template** -- property\n\nOverrides the chat template format (chatml, "
                  "llama3, mistral, gemma, phi3, qwen2). Empty uses auto-detection from GGUF metadata.",
    },
    // --- Error recovery ---
    PropertyDescriptor {
        .name = "agent_error_recovery_action",
        .type = CoreVM::LiteralType::String,
        .description = "Action on command failure (ask/analyze/ignore)",
        .detail = "**agent_error_recovery_action** -- property\n\nAction on command failure (ask, analyze, "
                  "ignore).",
        .readOnly = false,
        .enumValues = errorRecoveryActionValues,
    },
    PropertyDescriptor {
        .name = "agent_error_recovery_model",
        .type = CoreVM::LiteralType::String,
        .description = "Model for error analysis (empty = active model)",
        .detail = "**agent_error_recovery_model** -- property\n\nModel to use for error analysis. Empty uses "
                  "the active model.",
        .readOnly = false,
        .enumValues = errorRecoveryModelValues,
    },
};
// clang-format on

// ---------------------------------------------------------------------------
// Combined property table (prompt + agent)
// ---------------------------------------------------------------------------

/// @brief Lazily-constructed combined view of all property descriptors.
/// Needed because std::span cannot span two separate arrays.
static std::vector<PropertyDescriptor> const& allProperties()
{
    static auto const combined = [] {
        std::vector<PropertyDescriptor> result;
        result.reserve(promptProperties.size() + agentProperties.size());
        result.insert(result.end(), promptProperties.begin(), promptProperties.end());
        result.insert(result.end(), agentProperties.begin(), agentProperties.end());
        return result;
    }();
    return combined;
}

// ---------------------------------------------------------------------------
// Public accessors
// ---------------------------------------------------------------------------

std::span<PropertyDescriptor const> promptPropertyDescriptors()
{
    return promptProperties;
}

std::span<PropertyDescriptor const> agentPropertyDescriptors()
{
    return agentProperties;
}

std::span<PropertyDescriptor const> allPropertyDescriptors()
{
    auto const& all = allProperties();
    return { all.data(), all.size() };
}

} // namespace endo
