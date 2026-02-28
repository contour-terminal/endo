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
    EnumValueEntry { "minimal-arrow", "Clean arrow-based prompt" },
    EnumValueEntry { "lambda-clean", "Lambda symbol prompt" },
    EnumValueEntry { "opencode-bar", "OpenCode-style bar prompt" },
    EnumValueEntry { "powerline", "Powerline-style segments" },
    EnumValueEntry { "transient", "Minimal transient prompt" },
    EnumValueEntry { "dashboard", "Dashboard-style prompt" },
    EnumValueEntry { "boxed-module", "Boxed module prompt" },
    EnumValueEntry { "gradient-glow", "Gradient glow prompt" },
    EnumValueEntry { "context-adaptive", "Context-adaptive prompt" },
    EnumValueEntry { "endo-signature", "Endo signature prompt" },
};

static constexpr std::array layoutValues = {
    EnumValueEntry { "single-line", "Single line prompt" },
    EnumValueEntry { "two-line", "Two line prompt" },
    EnumValueEntry { "boxed", "Boxed prompt layout" },
    EnumValueEntry { "powerline", "Powerline prompt layout" },
};

static constexpr std::array separatorValues = {
    EnumValueEntry { "none", "No separator" },
    EnumValueEntry { "bar", "Bar separator (|)" },
    EnumValueEntry { "powerline", "Powerline separator" },
    EnumValueEntry { "rounded", "Rounded separator" },
    EnumValueEntry { "boxed", "Boxed separator" },
};

static constexpr std::array transientValues = {
    EnumValueEntry { "off", "Disable transient prompt" },
    EnumValueEntry { "minimal", "Minimal transient prompt" },
    EnumValueEntry { "arrow", "Arrow transient prompt" },
};

static constexpr std::array providerValues = {
    EnumValueEntry { "claude", "Anthropic Claude" },
    EnumValueEntry { "openai", "OpenAI" },
    EnumValueEntry { "gemini", "Google Gemini" },
    EnumValueEntry { "openai_compat", "OpenAI-compatible endpoint" },
    EnumValueEntry { "local", "Local model (llama.cpp)" },
};

static constexpr std::array boolValues = {
    EnumValueEntry { "true", "Enable" },
    EnumValueEntry { "false", "Disable" },
};

static constexpr std::array webSearchEngineValues = {
    EnumValueEntry { "duckduckgo", "DuckDuckGo (no API key required)" },
    EnumValueEntry { "brave", "Brave Search" },
    EnumValueEntry { "google", "Google Custom Search" },
};

static constexpr std::array claudeModelValues = {
    EnumValueEntry { "claude-opus-4-6", "Claude Opus 4.6" },
    EnumValueEntry { "claude-sonnet-4-6", "Claude Sonnet 4.6" },
    EnumValueEntry { "claude-haiku-4-5-20251001", "Claude Haiku 4.5" },
    EnumValueEntry { "claude-sonnet-4-5-20250929", "Claude Sonnet 4.5" },
    EnumValueEntry { "claude-opus-4-20250514", "Claude Opus 4" },
};

static constexpr std::array openaiModelValues = {
    EnumValueEntry { "gpt-4o", "GPT-4o" },
    EnumValueEntry { "gpt-4o-mini", "GPT-4o Mini" },
    EnumValueEntry { "o3-mini", "O3 Mini" },
    EnumValueEntry { "o1", "O1" },
};

static constexpr std::array geminiModelValues = {
    EnumValueEntry { "gemini-2.5-flash", "Gemini 2.5 Flash" },
    EnumValueEntry { "gemini-2.5-pro", "Gemini 2.5 Pro" },
    EnumValueEntry { "gemini-2.0-flash", "Gemini 2.0 Flash" },
};

static constexpr std::array thinkingModeValues = {
    EnumValueEntry { "off", "No thinking (provider default)" },
    EnumValueEntry { "normal", "Moderate thinking budget" },
    EnumValueEntry { "extended", "Maximum thinking budget" },
};

static constexpr std::array authTypeValues = {
    EnumValueEntry { "auto", "Auto-detect (OAuth preferred)" },
    EnumValueEntry { "oauth", "OAuth authentication" },
    EnumValueEntry { "api_key", "API key authentication" },
};

static constexpr std::array errorRecoveryActionValues = {
    EnumValueEntry { "ask", "Ask user before analyzing (default)" },
    EnumValueEntry { "analyze", "Automatically analyze failed commands" },
    EnumValueEntry { "ignore", "Do nothing on command failure" },
};

/// Combined model values for agent_error_recovery_model (all providers).
static constexpr std::array errorRecoveryModelValues = {
    // Claude models
    EnumValueEntry { "claude-opus-4-6", "Claude Opus 4.6" },
    EnumValueEntry { "claude-sonnet-4-6", "Claude Sonnet 4.6" },
    EnumValueEntry { "claude-haiku-4-5-20251001", "Claude Haiku 4.5" },
    EnumValueEntry { "claude-sonnet-4-5-20250929", "Claude Sonnet 4.5" },
    EnumValueEntry { "claude-opus-4-20250514", "Claude Opus 4" },
    // OpenAI models
    EnumValueEntry { "gpt-4o", "GPT-4o" },
    EnumValueEntry { "gpt-4o-mini", "GPT-4o Mini" },
    EnumValueEntry { "o3-mini", "O3 Mini" },
    EnumValueEntry { "o1", "O1" },
    // Gemini models
    EnumValueEntry { "gemini-2.5-flash", "Gemini 2.5 Flash" },
    EnumValueEntry { "gemini-2.5-pro", "Gemini 2.5 Pro" },
    EnumValueEntry { "gemini-2.0-flash", "Gemini 2.0 Flash" },
};

// ---------------------------------------------------------------------------
// Prompt/Shell property descriptors
// ---------------------------------------------------------------------------

static constexpr std::array promptProperties = {
    PropertyDescriptor {
        "shell_prompt_preset", CoreVM::LiteralType::String,
        "Prompt theme preset",
        "**shell_prompt_preset** -- property\n\nSets the prompt theme preset.\n\n```\nshell_prompt_preset powerline\n```",
        false, presetValues,
    },
    PropertyDescriptor {
        "shell_prompt_indicator", CoreVM::LiteralType::String,
        "Prompt indicator character(s)",
        "**shell_prompt_indicator** -- property\n\nSets the prompt indicator character(s).",
    },
    PropertyDescriptor {
        "shell_prompt_layout", CoreVM::LiteralType::String,
        "Prompt layout style",
        "**shell_prompt_layout** -- property\n\nSets the prompt layout style (single-line, two-line, boxed, powerline).",
        false, layoutValues,
    },
    PropertyDescriptor {
        "shell_prompt_separator", CoreVM::LiteralType::String,
        "Prompt separator style",
        "**shell_prompt_separator** -- property\n\nSets the separator style between prompt modules.",
        false, separatorValues,
    },
    PropertyDescriptor {
        "shell_prompt_transient", CoreVM::LiteralType::String,
        "Transient prompt mode",
        "**shell_prompt_transient** -- property\n\nControls transient prompt behavior (off, minimal, arrow).",
        false, transientValues,
    },
    PropertyDescriptor {
        "shell_prompt_duration_threshold", CoreVM::LiteralType::Number,
        "Duration display threshold (ms)",
        "**shell_prompt_duration_threshold** -- property\n\nMinimum command duration (ms) before showing elapsed time.",
    },
    PropertyDescriptor {
        "shell_prompt_spacing", CoreVM::LiteralType::Number,
        "Blank lines above/below prompt (0 or 1)",
        "**shell_prompt_spacing** -- property\n\nNumber of blank lines above/below the prompt (0 or 1).",
    },
    PropertyDescriptor {
        "shell_exit_confirm_timeout", CoreVM::LiteralType::Number,
        "Exit confirmation timeout (ms)",
        "**shell_exit_confirm_timeout** -- property\n\nTimeout (ms) for exit confirmation when background jobs exist.",
    },
    PropertyDescriptor {
        "shell_ls_icons", CoreVM::LiteralType::Boolean,
        "Show Nerd Font icons in ls output (default: true)",
        "**shell_ls_icons** -- property\n\nWhether to show Nerd Font icons in `ls` output.",
    },
    PropertyDescriptor {
        "shell_ls_directory_slash", CoreVM::LiteralType::Boolean,
        "Append trailing '/' to directory names in ls output (default: true)",
        "**shell_ls_directory_slash** -- property\n\nWhether to append trailing `/` to directory names in `ls` output.",
    },
    PropertyDescriptor {
        "shell_is_interactive", CoreVM::LiteralType::Boolean,
        "Whether running interactively (read-only)",
        "**shell_is_interactive** -- property (read-only)\n\nReturns whether the shell is running interactively.",
        true, // readOnly
    },
};

// ---------------------------------------------------------------------------
// Agent configuration property descriptors
// ---------------------------------------------------------------------------

static constexpr std::array agentProperties = {
    // --- Top-level agent settings ---
    PropertyDescriptor {
        "agent_provider", CoreVM::LiteralType::String,
        "Active AI provider",
        "**agent_provider** -- property\n\nSets the active AI provider (claude, openai, gemini, openai_compat).",
        false, providerValues,
    },
    PropertyDescriptor {
        "agent_prompt_indicator", CoreVM::LiteralType::String,
        "Agent prompt indicator character(s)",
        "**agent_prompt_indicator** -- property\n\nSets the agent prompt indicator character(s).",
    },
    PropertyDescriptor {
        "agent_max_tool_result_size", CoreVM::LiteralType::Number,
        "Max bytes for tool result truncation",
        "**agent_max_tool_result_size** -- property\n\nMaximum bytes for tool result truncation.",
    },
    PropertyDescriptor {
        "agent_log_tool_uses", CoreVM::LiteralType::Boolean,
        "Enable/disable tool invocation logging",
        "**agent_log_tool_uses** -- property\n\nEnables or disables logging of tool invocations.",
        false, boolValues,
    },
    // --- Claude provider ---
    PropertyDescriptor {
        "agent_claude_api_key", CoreVM::LiteralType::String,
        "Claude API key",
        "**agent_claude_api_key** -- property\n\nSets the Claude API key.",
    },
    PropertyDescriptor {
        "agent_claude_api_key_env", CoreVM::LiteralType::String,
        "Claude API key environment variable",
        "**agent_claude_api_key_env** -- property\n\nEnvironment variable name containing the Claude API key.",
    },
    PropertyDescriptor {
        "agent_claude_model", CoreVM::LiteralType::String,
        "Claude model identifier",
        "**agent_claude_model** -- property\n\nSets the Claude model identifier.",
        false, claudeModelValues,
    },
    PropertyDescriptor {
        "agent_claude_max_tokens", CoreVM::LiteralType::Number,
        "Claude max output tokens",
        "**agent_claude_max_tokens** -- property\n\nMaximum output tokens for Claude responses.",
    },
    PropertyDescriptor {
        "agent_claude_thinking_mode", CoreVM::LiteralType::String,
        "Claude thinking/reasoning mode (off/normal/extended)",
        "**agent_claude_thinking_mode** -- property\n\nSets Claude thinking/reasoning mode (off, normal, extended).",
        false, thinkingModeValues,
    },
    PropertyDescriptor {
        "agent_claude_prompt_caching", CoreVM::LiteralType::Boolean,
        "Enable Claude prompt caching (true/false)",
        "**agent_claude_prompt_caching** -- property\n\nEnables or disables Claude prompt caching.",
    },
    PropertyDescriptor {
        "agent_claude_auth_type", CoreVM::LiteralType::String,
        "Claude auth method (auto/oauth/api_key)",
        "**agent_claude_auth_type** -- property\n\nSets the Claude authentication method (auto, oauth, api_key).",
        false, authTypeValues,
    },
    // --- OpenAI provider ---
    PropertyDescriptor {
        "agent_openai_api_key", CoreVM::LiteralType::String,
        "OpenAI API key",
        "**agent_openai_api_key** -- property\n\nSets the OpenAI API key.",
    },
    PropertyDescriptor {
        "agent_openai_api_key_env", CoreVM::LiteralType::String,
        "OpenAI API key environment variable",
        "**agent_openai_api_key_env** -- property\n\nEnvironment variable name containing the OpenAI API key.",
    },
    PropertyDescriptor {
        "agent_openai_model", CoreVM::LiteralType::String,
        "OpenAI model identifier",
        "**agent_openai_model** -- property\n\nSets the OpenAI model identifier.",
        false, openaiModelValues,
    },
    PropertyDescriptor {
        "agent_openai_base_url", CoreVM::LiteralType::String,
        "OpenAI base URL",
        "**agent_openai_base_url** -- property\n\nSets the OpenAI API base URL.",
    },
    PropertyDescriptor {
        "agent_openai_max_tokens", CoreVM::LiteralType::Number,
        "OpenAI max output tokens",
        "**agent_openai_max_tokens** -- property\n\nMaximum output tokens for OpenAI responses.",
    },
    PropertyDescriptor {
        "agent_openai_thinking_mode", CoreVM::LiteralType::String,
        "OpenAI thinking/reasoning mode",
        "**agent_openai_thinking_mode** -- property\n\nSets OpenAI thinking/reasoning mode.",
        false, thinkingModeValues,
    },
    // --- OpenAI-compatible provider ---
    PropertyDescriptor {
        "agent_openai_compat_api_key", CoreVM::LiteralType::String,
        "OpenAI-compatible API key",
        "**agent_openai_compat_api_key** -- property\n\nSets the OpenAI-compatible API key.",
    },
    PropertyDescriptor {
        "agent_openai_compat_api_key_env", CoreVM::LiteralType::String,
        "OpenAI-compatible API key environment variable",
        "**agent_openai_compat_api_key_env** -- property\n\nEnvironment variable name containing the API key.",
    },
    PropertyDescriptor {
        "agent_openai_compat_model", CoreVM::LiteralType::String,
        "OpenAI-compatible model identifier",
        "**agent_openai_compat_model** -- property\n\nSets the OpenAI-compatible model identifier.",
        false, openaiModelValues,
    },
    PropertyDescriptor {
        "agent_openai_compat_base_url", CoreVM::LiteralType::String,
        "OpenAI-compatible base URL",
        "**agent_openai_compat_base_url** -- property\n\nSets the OpenAI-compatible API base URL.",
    },
    PropertyDescriptor {
        "agent_openai_compat_max_tokens", CoreVM::LiteralType::Number,
        "OpenAI-compatible max output tokens",
        "**agent_openai_compat_max_tokens** -- property\n\nMaximum output tokens.",
    },
    PropertyDescriptor {
        "agent_openai_compat_thinking_mode", CoreVM::LiteralType::String,
        "OpenAI-compatible thinking mode",
        "**agent_openai_compat_thinking_mode** -- property\n\nSets the thinking mode.",
        false, thinkingModeValues,
    },
    // --- Gemini provider ---
    PropertyDescriptor {
        "agent_gemini_api_key", CoreVM::LiteralType::String,
        "Gemini API key",
        "**agent_gemini_api_key** -- property\n\nSets the Gemini API key.",
    },
    PropertyDescriptor {
        "agent_gemini_api_key_env", CoreVM::LiteralType::String,
        "Gemini API key environment variable",
        "**agent_gemini_api_key_env** -- property\n\nEnvironment variable name containing the Gemini API key.",
    },
    PropertyDescriptor {
        "agent_gemini_model", CoreVM::LiteralType::String,
        "Gemini model identifier",
        "**agent_gemini_model** -- property\n\nSets the Gemini model identifier.",
        false, geminiModelValues,
    },
    PropertyDescriptor {
        "agent_gemini_max_tokens", CoreVM::LiteralType::Number,
        "Gemini max output tokens",
        "**agent_gemini_max_tokens** -- property\n\nMaximum output tokens for Gemini responses.",
    },
    PropertyDescriptor {
        "agent_gemini_thinking_mode", CoreVM::LiteralType::String,
        "Gemini thinking/reasoning mode",
        "**agent_gemini_thinking_mode** -- property\n\nSets Gemini thinking/reasoning mode.",
        false, thinkingModeValues,
    },
    // --- Plan mode ---
    PropertyDescriptor {
        "agent_plan_mode_enabled", CoreVM::LiteralType::Boolean,
        "Enable/disable plan mode",
        "**agent_plan_mode_enabled** -- property\n\nEnables or disables plan mode.",
        false, boolValues,
    },
    PropertyDescriptor {
        "agent_plan_mode_pause_between_steps", CoreVM::LiteralType::Boolean,
        "Pause for confirmation between plan steps",
        "**agent_plan_mode_pause_between_steps** -- property\n\nPause for confirmation between plan steps.",
        false, boolValues,
    },
    PropertyDescriptor {
        "agent_plan_mode_max_exploration_turns", CoreVM::LiteralType::Number,
        "Max exploration iterations",
        "**agent_plan_mode_max_exploration_turns** -- property\n\nMaximum number of exploration iterations.",
    },
    // --- Explore sub-agent ---
    PropertyDescriptor {
        "agent_explore_max_turns", CoreVM::LiteralType::Number,
        "Max explore sub-agent iterations",
        "**agent_explore_max_turns** -- property\n\nMaximum explore sub-agent iterations.",
    },
    // --- Session ---
    PropertyDescriptor {
        "agent_auto_resume", CoreVM::LiteralType::Boolean,
        "Auto-resume last agent session",
        "**agent_auto_resume** -- property\n\nAuto-resume the last agent session on startup.",
    },
    PropertyDescriptor {
        "agent_session_replay", CoreVM::LiteralType::Boolean,
        "Replay history when resuming",
        "**agent_session_replay** -- property\n\nReplay conversation history when resuming a session.",
    },
    // --- Trace ---
    PropertyDescriptor {
        "agent_trace_enabled", CoreVM::LiteralType::Boolean,
        "Enable/disable trace logging",
        "**agent_trace_enabled** -- property\n\nEnables or disables trace logging.",
        false, boolValues,
    },
    PropertyDescriptor {
        "agent_trace_default_path", CoreVM::LiteralType::String,
        "Trace file path",
        "**agent_trace_default_path** -- property\n\nDefault path for trace log files.",
    },
    PropertyDescriptor {
        "agent_trace_max_files", CoreVM::LiteralType::Number,
        "Max trace files to retain",
        "**agent_trace_max_files** -- property\n\nMaximum number of trace files to retain.",
    },
    // --- Permissions ---
    PropertyDescriptor {
        "agent_permissions_policy", CoreVM::LiteralType::String,
        "Agent permission policy",
        "**agent_permissions_policy** -- property\n\nSets the agent permission policy.",
    },
    PropertyDescriptor {
        "agent_trusted_tool", CoreVM::LiteralType::Object,
        "Auto-approved tools",
        "**agent_trusted_tool** -- property\n\nTools that are auto-approved without user confirmation.",
    },
    PropertyDescriptor {
        "agent_blocked_pattern", CoreVM::LiteralType::Object,
        "Blocked shell command patterns",
        "**agent_blocked_pattern** -- property\n\nShell command patterns blocked from agent execution.",
    },
    // --- Web search ---
    PropertyDescriptor {
        "agent_web_search_engine", CoreVM::LiteralType::String,
        "Web search engine",
        "**agent_web_search_engine** -- property\n\nSets the web search engine (duckduckgo, brave, google).",
        false, webSearchEngineValues,
    },
    PropertyDescriptor {
        "agent_web_search_api_key", CoreVM::LiteralType::String,
        "Web search API key",
        "**agent_web_search_api_key** -- property\n\nAPI key for the selected web search engine.",
    },
    PropertyDescriptor {
        "agent_web_search_cx", CoreVM::LiteralType::String,
        "Google Custom Search Engine ID",
        "**agent_web_search_cx** -- property\n\nGoogle Custom Search Engine ID.",
    },
    PropertyDescriptor {
        "agent_web_search_max_results", CoreVM::LiteralType::Number,
        "Max web search results per query",
        "**agent_web_search_max_results** -- property\n\nMaximum number of web search results per query.",
    },
    // --- Local inference ---
    PropertyDescriptor {
        "agent_local_model_path", CoreVM::LiteralType::String,
        "Path to local GGUF model file",
        "**agent_local_model_path** -- property\n\nSets the path to a local GGUF model file for offline inference.",
    },
    PropertyDescriptor {
        "agent_local_model_dir", CoreVM::LiteralType::String,
        "Directory for downloaded models",
        "**agent_local_model_dir** -- property\n\nSets the directory where downloaded models are stored.",
    },
    PropertyDescriptor {
        "agent_local_gpu_layers", CoreVM::LiteralType::Number,
        "GPU layers to offload (-1 = all, 0 = CPU only)",
        "**agent_local_gpu_layers** -- property\n\nNumber of model layers to offload to GPU (-1 = all, 0 = CPU only).",
    },
    PropertyDescriptor {
        "agent_local_context_size", CoreVM::LiteralType::Number,
        "Context window size in tokens",
        "**agent_local_context_size** -- property\n\nSets the context window size in tokens for local inference.",
    },
    PropertyDescriptor {
        "agent_local_threads", CoreVM::LiteralType::Number,
        "CPU threads for inference (0 = auto)",
        "**agent_local_threads** -- property\n\nNumber of CPU threads for local inference (0 = auto-detect).",
    },
    PropertyDescriptor {
        "agent_local_batch_size", CoreVM::LiteralType::Number,
        "Batch size for prompt evaluation",
        "**agent_local_batch_size** -- property\n\nBatch size for prompt evaluation in local inference.",
    },
    PropertyDescriptor {
        "agent_local_temperature", CoreVM::LiteralType::Number,
        "Sampling temperature (value * 100, e.g. 70 = 0.7)",
        "**agent_local_temperature** -- property\n\nSampling temperature for local inference (integer, value * 100).",
    },
    PropertyDescriptor {
        "agent_local_flash_attention", CoreVM::LiteralType::Boolean,
        "Enable Flash Attention",
        "**agent_local_flash_attention** -- property\n\nEnables or disables Flash Attention for local inference.",
        false, boolValues,
    },
    PropertyDescriptor {
        "agent_local_max_tokens", CoreVM::LiteralType::Number,
        "Maximum tokens to generate",
        "**agent_local_max_tokens** -- property\n\nMaximum number of tokens to generate per response.",
    },
    PropertyDescriptor {
        "agent_local_chat_template", CoreVM::LiteralType::String,
        "Chat template override (empty = auto-detect)",
        "**agent_local_chat_template** -- property\n\nOverrides the chat template format (chatml, llama3, mistral, gemma, phi3, qwen2). Empty uses auto-detection from GGUF metadata.",
    },
    // --- Error recovery ---
    PropertyDescriptor {
        "agent_error_recovery_action", CoreVM::LiteralType::String,
        "Action on command failure (ask/analyze/ignore)",
        "**agent_error_recovery_action** -- property\n\nAction on command failure (ask, analyze, ignore).",
        false, errorRecoveryActionValues,
    },
    PropertyDescriptor {
        "agent_error_recovery_model", CoreVM::LiteralType::String,
        "Model for error analysis (empty = active model)",
        "**agent_error_recovery_model** -- property\n\nModel to use for error analysis. Empty uses the active model.",
        false, errorRecoveryModelValues,
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
