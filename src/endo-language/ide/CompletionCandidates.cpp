// SPDX-License-Identifier: Apache-2.0
#include <endo-language/ide/CompletionCandidates.hpp>

#include <array>
#include <set>

namespace endo
{

namespace
{
    /// @brief Static table of Option module methods with descriptions.
    struct OptionMethod
    {
        std::string_view name;
        std::string_view description;
    };

    constexpr std::array optionMethods = {
        OptionMethod { "map", "Option.map f opt -> option" },
        OptionMethod { "bind", "Option.bind f opt -> option" },
        OptionMethod { "defaultValue", "Option.defaultValue d opt -> value" },
    };

    /// @brief Enumerated argument value with description.
    struct EnumValueEntry
    {
        std::string_view value;
        std::string_view description;
    };

    constexpr std::array presetValues = {
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

    constexpr std::array layoutValues = {
        EnumValueEntry { "single-line", "Single line prompt" },
        EnumValueEntry { "two-line", "Two line prompt" },
        EnumValueEntry { "boxed", "Boxed prompt layout" },
        EnumValueEntry { "powerline", "Powerline prompt layout" },
    };

    constexpr std::array separatorValues = {
        EnumValueEntry { "none", "No separator" },
        EnumValueEntry { "bar", "Bar separator (|)" },
        EnumValueEntry { "powerline", "Powerline separator" },
        EnumValueEntry { "rounded", "Rounded separator" },
        EnumValueEntry { "boxed", "Boxed separator" },
    };

    constexpr std::array transientValues = {
        EnumValueEntry { "off", "Disable transient prompt" },
        EnumValueEntry { "minimal", "Minimal transient prompt" },
        EnumValueEntry { "arrow", "Arrow transient prompt" },
    };

    constexpr std::array providerValues = {
        EnumValueEntry { "claude", "Anthropic Claude" },
        EnumValueEntry { "openai", "OpenAI" },
        EnumValueEntry { "gemini", "Google Gemini" },
        EnumValueEntry { "openai_compat", "OpenAI-compatible endpoint" },
    };

    constexpr std::array boolValues = {
        EnumValueEntry { "true", "Enable" },
        EnumValueEntry { "false", "Disable" },
    };

    constexpr std::array webSearchEngineValues = {
        EnumValueEntry { "duckduckgo", "DuckDuckGo (no API key required)" },
        EnumValueEntry { "brave", "Brave Search" },
        EnumValueEntry { "google", "Google Custom Search" },
    };

    constexpr std::array claudeModelValues = {
        EnumValueEntry { "claude-opus-4-6", "Claude Opus 4.6" },
        EnumValueEntry { "claude-sonnet-4-6", "Claude Sonnet 4.6" },
        EnumValueEntry { "claude-haiku-4-5-20251001", "Claude Haiku 4.5" },
        EnumValueEntry { "claude-sonnet-4-5-20250929", "Claude Sonnet 4.5" },
        EnumValueEntry { "claude-opus-4-20250514", "Claude Opus 4" },
    };

    constexpr std::array openaiModelValues = {
        EnumValueEntry { "gpt-4o", "GPT-4o" },
        EnumValueEntry { "gpt-4o-mini", "GPT-4o Mini" },
        EnumValueEntry { "o3-mini", "O3 Mini" },
        EnumValueEntry { "o1", "O1" },
    };

    constexpr std::array geminiModelValues = {
        EnumValueEntry { "gemini-2.5-flash", "Gemini 2.5 Flash" },
        EnumValueEntry { "gemini-2.5-pro", "Gemini 2.5 Pro" },
        EnumValueEntry { "gemini-2.0-flash", "Gemini 2.0 Flash" },
    };

    constexpr std::array thinkingModeValues = {
        EnumValueEntry { "off", "No thinking (provider default)" },
        EnumValueEntry { "normal", "Moderate thinking budget" },
        EnumValueEntry { "extended", "Maximum thinking budget" },
    };

    constexpr std::array authTypeValues = {
        EnumValueEntry { "auto", "Auto-detect (OAuth preferred)" },
        EnumValueEntry { "oauth", "OAuth authentication" },
        EnumValueEntry { "api_key", "API key authentication" },
    };

    constexpr std::array errorRecoveryActionValues = {
        EnumValueEntry { "ask", "Ask user before analyzing (default)" },
        EnumValueEntry { "analyze", "Automatically analyze failed commands" },
        EnumValueEntry { "ignore", "Do nothing on command failure" },
    };

    /// @brief Standard library function entry with name and signature description.
    struct StdLibEntry
    {
        std::string_view name;
        std::string_view description;
    };

    // clang-format off
    constexpr std::array stdLibFunctions = {
        // Type Conversion
        StdLibEntry { "string_length", "string_length s -> int" },
        StdLibEntry { "int_of_string", "int_of_string s -> int" },
        StdLibEntry { "string_of_int", "string_of_int n -> string" },
        StdLibEntry { "not", "not b -> bool" },
        // String Operations
        StdLibEntry { "trim", "trim s -> string" },
        StdLibEntry { "toLower", "toLower s -> string" },
        StdLibEntry { "toUpper", "toUpper s -> string" },
        StdLibEntry { "contains", "contains substr s -> bool" },
        StdLibEntry { "startsWith", "startsWith prefix s -> bool" },
        StdLibEntry { "endsWith", "endsWith suffix s -> bool" },
        StdLibEntry { "replace", "replace old new s -> string" },
        StdLibEntry { "split", "split delim s -> list<string>" },
        StdLibEntry { "join", "join delim lst -> string" },
        // List Basic
        StdLibEntry { "head", "head lst -> 'a" },
        StdLibEntry { "tail", "tail lst -> list<'a>" },
        StdLibEntry { "length", "length lst -> int" },
        StdLibEntry { "isEmpty", "isEmpty lst -> bool" },
        StdLibEntry { "nth", "nth n lst -> 'a" },
        StdLibEntry { "last", "last lst -> 'a" },
        StdLibEntry { "replicate", "replicate n x -> list<'a>" },
        // List HOFs
        StdLibEntry { "map", "map f lst -> list<'b>" },
        StdLibEntry { "filter", "filter pred lst -> list<'a>" },
        StdLibEntry { "fold", "fold f init lst -> 'b" },
        StdLibEntry { "reduce", "reduce f lst -> 'a" },
        StdLibEntry { "find", "find pred lst -> option<'a>" },
        StdLibEntry { "exists", "exists pred lst -> bool" },
        StdLibEntry { "forall", "forall pred lst -> bool" },
        StdLibEntry { "each", "each f lst -> unit" },
        // List Transforms
        StdLibEntry { "sort", "sort lst -> list<'a>" },
        StdLibEntry { "reverse", "reverse lst -> list<'a>" },
        StdLibEntry { "distinct", "distinct lst -> list<'a>" },
        StdLibEntry { "sortBy", "sortBy f lst -> list<'a>" },
        StdLibEntry { "groupBy", "groupBy f lst -> list<list<'a>>" },
        StdLibEntry { "take", "take n lst -> list<'a>" },
        StdLibEntry { "drop", "drop n lst -> list<'a>" },
        StdLibEntry { "zip", "zip lst1 lst2 -> list<'a * 'b>" },
        StdLibEntry { "flatten", "flatten lst -> list<'a>" },
        // Formatting Helpers
        StdLibEntry { "formatNumber", "formatNumber sep n -> string  |  formatNumber n -> string (locale)" },
        StdLibEntry { "formatDateTime", "formatDateTime epoch -> string" },
        StdLibEntry { "formatMode", "formatMode mode -> string (rwxrwxrwx)" },
        StdLibEntry { "toText", "toText obj -> string" },
        StdLibEntry { "string", "string x -> string" },
        // Permission Tests
        StdLibEntry { "isReadable", "isReadable mode -> bool" },
        StdLibEntry { "isWritable", "isWritable mode -> bool" },
        StdLibEntry { "isExecutable", "isExecutable mode -> bool" },
        // Environment/System
        StdLibEntry { "env", "env name -> option<string>" },
        StdLibEntry { "which", "which name -> option<string>" },
        StdLibEntry { "ps", "ps -> list<ProcessInfo>" },
        StdLibEntry { "ls", "ls -> list<FileInfo>  |  ls path -> list<FileInfo>" },
        StdLibEntry { "rand", "rand -> int  |  rand min max -> int" },
        StdLibEntry { "fetch", "fetch url -> result<string, string>" },
        // DateTime constructors
        StdLibEntry { "DateTime.now", "DateTime.now -> DateTime (current UTC time)" },
        StdLibEntry { "DateTime.fromEpoch", "DateTime.fromEpoch epoch -> DateTime" },
    };
    // clang-format on

    /// @brief Helper to check if a name starts with a given prefix (case-sensitive).
    [[nodiscard]] bool startsWith(std::string_view name, std::string_view prefix)
    {
        if (prefix.empty())
            return true;
        return name.size() >= prefix.size() && name.substr(0, prefix.size()) == prefix;
    }

    /// @brief Formats a function signature description from symbol info.
    [[nodiscard]] std::string formatFunctionDescription(SymbolDefinitionInfo const& sym)
    {
        std::string result;
        if (sym.isRecursive)
            result += "rec ";
        result += sym.name;
        result += '(';
        for (size_t i = 0; i < sym.parameterNames.size(); ++i)
        {
            if (i > 0)
                result += ", ";
            result += sym.parameterNames[i];
            if (i < sym.parameterTypes.size() && sym.parameterTypes[i].has_value())
            {
                result += ": ";
                result += *sym.parameterTypes[i];
            }
        }
        result += ')';
        if (sym.returnType.has_value())
        {
            result += " -> ";
            result += *sym.returnType;
        }
        return result;
    }
} // namespace

std::vector<CompletionCandidate> keywordCandidates()
{
    return {
        { "let", "let", "F# value/function binding", "", CompletionKind::Keyword },
        { "rec", "rec", "Recursive function modifier", "", CompletionKind::Keyword },
        { "mut", "mut", "Mutable binding modifier", "", CompletionKind::Keyword },
        { "fun", "fun", "Lambda expression", "", CompletionKind::Keyword },
        { "match", "match", "Pattern matching expression", "", CompletionKind::Keyword },
        { "with", "with", "Match arm separator", "", CompletionKind::Keyword },
        { "when", "when", "Pattern guard", "", CompletionKind::Keyword },
        { "if", "if", "Conditional expression", "", CompletionKind::Keyword },
        { "then", "then", "Then branch", "", CompletionKind::Keyword },
        { "else", "else", "Else branch", "", CompletionKind::Keyword },
        { "type", "type", "Type definition", "", CompletionKind::Keyword },
        { "of", "of", "Type constructor clause", "", CompletionKind::Keyword },
        { "try", "try", "Try expression", "", CompletionKind::Keyword },
        { "finally", "finally", "Finally clause", "", CompletionKind::Keyword },
        { "true", "true", "Boolean literal", "", CompletionKind::Keyword },
        { "false", "false", "Boolean literal", "", CompletionKind::Keyword },
    };
}

std::vector<CompletionCandidate> builtinCandidates()
{
    return {
        { "cat", "cat", "builtin", "", CompletionKind::Builtin },
        { "cd", "cd", "builtin", "", CompletionKind::Builtin },
        { "exit", "exit", "builtin", "", CompletionKind::Builtin },
        { "export", "export", "builtin", "", CompletionKind::Builtin },
        { "set", "set", "builtin", "", CompletionKind::Builtin },
        { "unset", "unset", "builtin", "", CompletionKind::Builtin },
        { "read", "read", "builtin", "", CompletionKind::Builtin },
        { "sleep", "sleep", "builtin", "", CompletionKind::Builtin },
        { "jobs", "jobs", "builtin", "", CompletionKind::Builtin },
        { "fg", "fg", "builtin", "", CompletionKind::Builtin },
        { "bg", "bg", "builtin", "", CompletionKind::Builtin },
        { "wait", "wait", "builtin", "", CompletionKind::Builtin },
        { "bind", "bind", "builtin", "", CompletionKind::Builtin },
        { "which", "which", "builtin", "", CompletionKind::Builtin },
        { "print", "print", "F# print function", "", CompletionKind::Builtin },
        { "println", "println", "F# print with newline", "", CompletionKind::Builtin },
        { "echo", "echo", "builtin", "", CompletionKind::Builtin },
        { "mv", "mv", "builtin", "", CompletionKind::Builtin },
        { "rm", "rm", "builtin", "", CompletionKind::Builtin },
        // Shell/Prompt properties
        { "shell_prompt_preset", "shell_prompt_preset", "Prompt theme preset", "", CompletionKind::Property },
        { "shell_prompt_indicator",
          "shell_prompt_indicator",
          "Prompt indicator character(s)",
          "",
          CompletionKind::Property },
        { "shell_prompt_layout", "shell_prompt_layout", "Prompt layout style", "", CompletionKind::Property },
        { "shell_prompt_separator",
          "shell_prompt_separator",
          "Prompt separator style",
          "",
          CompletionKind::Property },
        { "shell_prompt_transient",
          "shell_prompt_transient",
          "Transient prompt mode",
          "",
          CompletionKind::Property },
        { "shell_prompt_duration_threshold",
          "shell_prompt_duration_threshold",
          "Duration display threshold (ms)",
          "",
          CompletionKind::Property },
        { "shell_prompt_spacing",
          "shell_prompt_spacing",
          "Blank lines above/below prompt (0 or 1)",
          "",
          CompletionKind::Property },
        { "shell_exit_confirm_timeout",
          "shell_exit_confirm_timeout",
          "Exit confirmation timeout (ms)",
          "",
          CompletionKind::Property },
        { "shell_is_interactive",
          "shell_is_interactive",
          "Whether running interactively (read-only)",
          "",
          CompletionKind::Property },
        // Agent general properties
        { "agent_provider", "agent_provider", "Active AI provider", "", CompletionKind::Property },
        { "agent_prompt_indicator",
          "agent_prompt_indicator",
          "Agent prompt indicator character(s)",
          "",
          CompletionKind::Property },
        { "agent_max_tool_result_size",
          "agent_max_tool_result_size",
          "Max bytes for tool result truncation",
          "",
          CompletionKind::Property },
        { "agent_log_tool_uses",
          "agent_log_tool_uses",
          "Enable/disable tool invocation logging",
          "",
          CompletionKind::Property },
        // Claude provider properties
        { "agent_claude_api_key", "agent_claude_api_key", "Claude API key", "", CompletionKind::Property },
        { "agent_claude_api_key_env",
          "agent_claude_api_key_env",
          "Claude API key environment variable",
          "",
          CompletionKind::Property },
        { "agent_claude_model",
          "agent_claude_model",
          "Claude model identifier",
          "",
          CompletionKind::Property },
        { "agent_claude_max_tokens",
          "agent_claude_max_tokens",
          "Claude max output tokens",
          "",
          CompletionKind::Property },
        { "agent_claude_thinking_mode",
          "agent_claude_thinking_mode",
          "Claude thinking/reasoning mode (off/normal/extended)",
          "",
          CompletionKind::Property },
        { "agent_claude_prompt_caching",
          "agent_claude_prompt_caching",
          "Enable Claude prompt caching (true/false)",
          "",
          CompletionKind::Property },
        { "agent_claude_auth_type",
          "agent_claude_auth_type",
          "Claude auth method (auto/oauth/api_key)",
          "",
          CompletionKind::Property },
        // OpenAI provider properties
        { "agent_openai_api_key", "agent_openai_api_key", "OpenAI API key", "", CompletionKind::Property },
        { "agent_openai_api_key_env",
          "agent_openai_api_key_env",
          "OpenAI API key environment variable",
          "",
          CompletionKind::Property },
        { "agent_openai_model",
          "agent_openai_model",
          "OpenAI model identifier",
          "",
          CompletionKind::Property },
        { "agent_openai_base_url", "agent_openai_base_url", "OpenAI base URL", "", CompletionKind::Property },
        { "agent_openai_max_tokens",
          "agent_openai_max_tokens",
          "OpenAI max output tokens",
          "",
          CompletionKind::Property },
        { "agent_openai_thinking_mode",
          "agent_openai_thinking_mode",
          "OpenAI thinking/reasoning mode",
          "",
          CompletionKind::Property },
        // OpenAI-compatible provider properties
        { "agent_openai_compat_api_key",
          "agent_openai_compat_api_key",
          "OpenAI-compatible API key",
          "",
          CompletionKind::Property },
        { "agent_openai_compat_api_key_env",
          "agent_openai_compat_api_key_env",
          "OpenAI-compatible API key environment variable",
          "",
          CompletionKind::Property },
        { "agent_openai_compat_model",
          "agent_openai_compat_model",
          "OpenAI-compatible model identifier",
          "",
          CompletionKind::Property },
        { "agent_openai_compat_base_url",
          "agent_openai_compat_base_url",
          "OpenAI-compatible base URL",
          "",
          CompletionKind::Property },
        { "agent_openai_compat_max_tokens",
          "agent_openai_compat_max_tokens",
          "OpenAI-compatible max output tokens",
          "",
          CompletionKind::Property },
        { "agent_openai_compat_thinking_mode",
          "agent_openai_compat_thinking_mode",
          "OpenAI-compatible thinking mode",
          "",
          CompletionKind::Property },
        // Gemini provider properties
        { "agent_gemini_api_key", "agent_gemini_api_key", "Gemini API key", "", CompletionKind::Property },
        { "agent_gemini_api_key_env",
          "agent_gemini_api_key_env",
          "Gemini API key environment variable",
          "",
          CompletionKind::Property },
        { "agent_gemini_model",
          "agent_gemini_model",
          "Gemini model identifier",
          "",
          CompletionKind::Property },
        { "agent_gemini_max_tokens",
          "agent_gemini_max_tokens",
          "Gemini max output tokens",
          "",
          CompletionKind::Property },
        { "agent_gemini_thinking_mode",
          "agent_gemini_thinking_mode",
          "Gemini thinking/reasoning mode",
          "",
          CompletionKind::Property },
        // Plan mode properties
        { "agent_plan_mode_enabled",
          "agent_plan_mode_enabled",
          "Enable/disable plan mode",
          "",
          CompletionKind::Property },
        { "agent_plan_mode_pause_between_steps",
          "agent_plan_mode_pause_between_steps",
          "Pause for confirmation between plan steps",
          "",
          CompletionKind::Property },
        { "agent_plan_mode_max_exploration_turns",
          "agent_plan_mode_max_exploration_turns",
          "Max exploration iterations",
          "",
          CompletionKind::Property },
        // Explore sub-agent properties
        { "agent_explore_max_turns",
          "agent_explore_max_turns",
          "Max explore sub-agent iterations",
          "",
          CompletionKind::Property },
        // Trace properties
        { "agent_trace_enabled",
          "agent_trace_enabled",
          "Enable/disable trace logging",
          "",
          CompletionKind::Property },
        { "agent_trace_default_path",
          "agent_trace_default_path",
          "Trace file path",
          "",
          CompletionKind::Property },
        { "agent_trace_max_files",
          "agent_trace_max_files",
          "Max trace files to retain",
          "",
          CompletionKind::Property },
        // MCP server management (multi-arg functions, not properties)
        { "add_mcp_server", "add_mcp_server", "Register an MCP server", "", CompletionKind::Builtin },
        { "set_mcp_env",
          "set_mcp_env",
          "Set environment variable for an MCP server",
          "",
          CompletionKind::Builtin },
        { "remove_mcp_server", "remove_mcp_server", "Remove an MCP server", "", CompletionKind::Builtin },
        // Web search properties
        { "agent_web_search_engine",
          "agent_web_search_engine",
          "Web search engine",
          "",
          CompletionKind::Property },
        { "agent_web_search_api_key",
          "agent_web_search_api_key",
          "Web search API key",
          "",
          CompletionKind::Property },
        { "agent_web_search_max_results",
          "agent_web_search_max_results",
          "Max web search results per query",
          "",
          CompletionKind::Property },
        { "agent_web_search_cx",
          "agent_web_search_cx",
          "Google Custom Search Engine ID",
          "",
          CompletionKind::Property },
        // Error recovery properties
        { "agent_error_recovery_action",
          "agent_error_recovery_action",
          "Action on command failure (ask/analyze/ignore)",
          "",
          CompletionKind::Property },
        { "agent_error_recovery_model",
          "agent_error_recovery_model",
          "Model for error analysis (empty = active model)",
          "",
          CompletionKind::Property },
    };
}

std::vector<CompletionCandidate> shellKeywordCandidates()
{
    return {
        { "if", "if", "Conditional expression", "", CompletionKind::Keyword },
        { "then", "then", "Then branch", "", CompletionKind::Keyword },
        { "else", "else", "Else branch", "", CompletionKind::Keyword },
        { "elif", "elif", "Elif branch", "", CompletionKind::Keyword },
        { "for", "for", "For-in loop", "", CompletionKind::Keyword },
        { "while", "while", "While loop", "", CompletionKind::Keyword },
        { "do", "do", "Loop body", "", CompletionKind::Keyword },
        { "end", "end", "End loop", "", CompletionKind::Keyword },
        { "in", "in", "In clause", "", CompletionKind::Keyword },
        { "return", "return", "Return statement", "", CompletionKind::Keyword },
        { "break", "break", "Break statement", "", CompletionKind::Keyword },
        { "continue", "continue", "Continue statement", "", CompletionKind::Keyword },
    };
}

std::vector<CompletionCandidate> constructorCandidates()
{
    return {
        { "Some", "Some", "Option constructor (value present)", "", CompletionKind::Constructor },
        { "None", "None", "Option constructor (no value)", "", CompletionKind::Constructor },
        { "Ok", "Ok", "Result constructor (success)", "", CompletionKind::Constructor },
        { "Error", "Error", "Result constructor (failure)", "", CompletionKind::Constructor },
    };
}

std::vector<CompletionCandidate> dotAccessCandidates(
    std::string const& objectPart,
    std::string const& memberPrefix,
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> const& recordFields,
    std::unordered_map<std::string, std::string> const& variableTypes)
{
    std::vector<CompletionCandidate> results;

    auto addCandidate = [&](std::string const& completionText,
                            std::string const& memberName,
                            std::string const& description,
                            CompletionKind kind) {
        if (!startsWith(memberName, memberPrefix))
            return;
        // Avoid duplicates
        for (auto const& existing: results)
            if (existing.text == completionText)
                return;
        results.push_back(CompletionCandidate {
            .text = completionText,
            .displayText = completionText,
            .description = description,
            .detail = {},
            .kind = kind,
        });
    };

    // DateTime module methods
    constexpr std::array dateTimeMethods = {
        OptionMethod { "now", "DateTime.now -> DateTime (current UTC time)" },
        OptionMethod { "fromEpoch", "DateTime.fromEpoch epoch -> DateTime" },
    };

    if (objectPart == "Option")
    {
        // Static Option module methods
        for (auto const& method: optionMethods)
            addCandidate("Option." + std::string(method.name),
                         std::string(method.name),
                         std::string(method.description),
                         CompletionKind::Function);
    }
    else if (objectPart == "DateTime")
    {
        // Static DateTime module methods
        for (auto const& method: dateTimeMethods)
            addCandidate("DateTime." + std::string(method.name),
                         std::string(method.name),
                         std::string(method.description),
                         CompletionKind::Function);
    }
    else if (objectPart == "_")
    {
        // Underscore field access: offer all record fields with deduplication
        std::set<std::string> seen;
        for (auto const& [typeName, fields]: recordFields)
        {
            for (auto const& field: fields)
            {
                if (seen.insert(field.name).second)
                    addCandidate(
                        "_." + field.name, field.name, "field: " + field.typeName, CompletionKind::Field);
            }
        }
    }
    else if (objectPart.find('.') != std::string::npos)
    {
        // Nested dot access: e.g., "f.mtime" → resolve f → FileInfo, mtime → DateTime
        auto const firstDot = objectPart.find('.');
        auto const firstSegment = objectPart.substr(0, firstDot);
        auto const rest = objectPart.substr(firstDot + 1);

        // Resolve the first segment via variableTypes
        std::string currentType;
        if (auto const it = variableTypes.find(firstSegment); it != variableTypes.end())
            currentType = it->second;

        bool resolved = false;
        // Walk remaining segments through recordFields
        if (!currentType.empty())
        {
            auto remaining = rest;
            while (!remaining.empty() && !currentType.empty())
            {
                auto const dot = remaining.find('.');
                auto const segment = remaining.substr(0, dot);

                // Look up this segment's type in the current record type's fields
                std::string nextType;
                if (auto const fieldsIt = recordFields.find(currentType); fieldsIt != recordFields.end())
                {
                    for (auto const& field: fieldsIt->second)
                    {
                        if (field.name == segment)
                        {
                            nextType = field.typeName;
                            break;
                        }
                    }
                }
                currentType = nextType;

                if (dot == std::string::npos)
                    break;
                remaining = remaining.substr(dot + 1);
            }

            // If we resolved to a record type, offer its fields
            if (!currentType.empty())
            {
                if (auto const fieldsIt = recordFields.find(currentType); fieldsIt != recordFields.end())
                {
                    for (auto const& field: fieldsIt->second)
                        addCandidate(objectPart + "." + field.name,
                                     field.name,
                                     currentType + "." + field.name + ": " + field.typeName,
                                     CompletionKind::Field);
                    resolved = true;
                }
            }
        }

        // Module function pattern: if firstSegment is a record type name (e.g., "DateTime"),
        // module functions return that type (e.g., DateTime.now -> DateTime)
        if (!resolved && recordFields.contains(firstSegment))
        {
            if (auto const fieldsIt = recordFields.find(firstSegment); fieldsIt != recordFields.end())
            {
                for (auto const& field: fieldsIt->second)
                    addCandidate(objectPart + "." + field.name,
                                 field.name,
                                 firstSegment + "." + field.name + ": " + field.typeName,
                                 CompletionKind::Field);
                resolved = true;
            }
        }

        // Fall through to generic behavior if we couldn't resolve the type chain
        if (!resolved)
        {
            std::set<std::string> seen;
            for (auto const& [typeName, typeFields]: recordFields)
            {
                for (auto const& field: typeFields)
                {
                    if (seen.insert(field.name).second)
                        addCandidate(objectPart + "." + field.name,
                                     field.name,
                                     "field: " + field.typeName,
                                     CompletionKind::Field);
                }
            }
        }
    }
    else if (auto const it = variableTypes.find(objectPart); it != variableTypes.end())
    {
        // Known variable type: offer only fields of the specific record type
        auto const& recordTypeName = it->second;
        if (auto const fieldsIt = recordFields.find(recordTypeName); fieldsIt != recordFields.end())
        {
            for (auto const& field: fieldsIt->second)
                addCandidate(objectPart + "." + field.name,
                             field.name,
                             recordTypeName + "." + field.name + ": " + field.typeName,
                             CompletionKind::Field);
        }
    }
    else
    {
        // Generic value: offer both Option methods and record fields
        for (auto const& method: optionMethods)
            addCandidate(objectPart + "." + std::string(method.name),
                         std::string(method.name),
                         std::string(method.description),
                         CompletionKind::Function);

        std::set<std::string> seen;
        for (auto const& [typeName, fields]: recordFields)
        {
            for (auto const& field: fields)
            {
                if (seen.insert(field.name).second)
                    addCandidate(objectPart + "." + field.name,
                                 field.name,
                                 "field: " + field.typeName,
                                 CompletionKind::Field);
            }
        }
    }

    return results;
}

bool isBuiltinWithArgumentCompletion(std::string const& commandName)
{
    static auto const names = std::set<std::string> {
        "shell_prompt_preset",
        "shell_prompt_indicator",
        "shell_prompt_layout",
        "shell_prompt_separator",
        "shell_prompt_transient",
        "shell_prompt_duration_threshold",
        "agent_provider",
        "agent_log_tool_uses",
        "agent_plan_mode_enabled",
        "agent_plan_mode_pause_between_steps",
        "agent_trace_enabled",
        "agent_web_search_engine",
        "agent_claude_model",
        "agent_openai_model",
        "agent_openai_compat_model",
        "agent_gemini_model",
        "agent_claude_thinking_mode",
        "agent_openai_thinking_mode",
        "agent_openai_compat_thinking_mode",
        "agent_gemini_thinking_mode",
        "agent_claude_auth_type",
        "agent_error_recovery_action",
        "agent_error_recovery_model",
    };
    return names.contains(commandName);
}

std::vector<CompletionCandidate> builtinArgumentCandidates(std::string const& commandName,
                                                           std::string const& prefix)
{
    auto collectValues = [&](auto const& entries) {
        std::vector<CompletionCandidate> results;
        for (auto const& entry: entries)
        {
            if (startsWith(entry.value, prefix))
                results.push_back(CompletionCandidate {
                    .text = std::string(entry.value),
                    .displayText = std::string(entry.value),
                    .description = std::string(entry.description),
                    .detail = {},
                    .kind = CompletionKind::EnumValue,
                });
        }
        return results;
    };

    if (commandName == "shell_prompt_preset")
        return collectValues(presetValues);
    if (commandName == "shell_prompt_layout")
        return collectValues(layoutValues);
    if (commandName == "shell_prompt_separator")
        return collectValues(separatorValues);
    if (commandName == "shell_prompt_transient")
        return collectValues(transientValues);
    if (commandName == "agent_provider")
        return collectValues(providerValues);
    if (commandName == "agent_web_search_engine")
        return collectValues(webSearchEngineValues);
    if (commandName == "agent_log_tool_uses" || commandName == "agent_plan_mode_enabled"
        || commandName == "agent_plan_mode_pause_between_steps" || commandName == "agent_trace_enabled")
        return collectValues(boolValues);
    if (commandName == "agent_claude_model")
        return collectValues(claudeModelValues);
    if (commandName == "agent_openai_model" || commandName == "agent_openai_compat_model")
        return collectValues(openaiModelValues);
    if (commandName == "agent_gemini_model")
        return collectValues(geminiModelValues);
    if (commandName == "agent_claude_thinking_mode" || commandName == "agent_openai_thinking_mode"
        || commandName == "agent_openai_compat_thinking_mode" || commandName == "agent_gemini_thinking_mode")
        return collectValues(thinkingModeValues);
    if (commandName == "agent_claude_auth_type")
        return collectValues(authTypeValues);
    if (commandName == "agent_error_recovery_action")
        return collectValues(errorRecoveryActionValues);
    if (commandName == "agent_error_recovery_model")
    {
        // Offer all known models across all providers.
        auto results = collectValues(claudeModelValues);
        auto openai = collectValues(openaiModelValues);
        auto gemini = collectValues(geminiModelValues);
        results.insert(results.end(), openai.begin(), openai.end());
        results.insert(results.end(), gemini.begin(), gemini.end());
        return results;
    }

    return {};
}

std::vector<CompletionCandidate> standardLibraryCandidates()
{
    std::vector<CompletionCandidate> results;
    results.reserve(stdLibFunctions.size());
    for (auto const& entry: stdLibFunctions)
        results.push_back(CompletionCandidate {
            .text = std::string(entry.name),
            .displayText = std::string(entry.name),
            .description = std::string(entry.description),
            .detail = {},
            .kind = CompletionKind::Function,
        });
    return results;
}

std::vector<CompletionCandidate> symbolCandidates(std::vector<SymbolDefinitionInfo> const& symbols)
{
    std::vector<CompletionCandidate> results;
    results.reserve(symbols.size());

    for (auto const& sym: symbols)
    {
        auto kind = sym.isFunction ? CompletionKind::Function : CompletionKind::Variable;
        auto description = sym.isFunction  ? formatFunctionDescription(sym)
                           : sym.isMutable ? std::string("mutable value")
                                           : std::string("value");

        results.push_back(CompletionCandidate {
            .text = sym.name,
            .displayText = sym.name,
            .description = std::move(description),
            .detail = {},
            .kind = kind,
        });
    }

    return results;
}

} // namespace endo
