// SPDX-License-Identifier: Apache-2.0
#include <endo-language/ast/AST.hpp>
#include <endo-language/builtins/StubRuntime.hpp>
#include <endo-language/ide/HoverProvider.hpp>
#include <endo-language/lexer/Lexer.hpp>
#include <endo-language/parser/Parser.hpp>
#include <endo-language/types/Type.hpp>

#include <unordered_map>
#include <vector>

namespace endo
{

namespace
{

    /// Returns hover markdown for a keyword token.
    [[nodiscard]] std::optional<std::string> keywordHover(Token token)
    {
        using enum Token;
        switch (token)
        {
            case Let:
                return "`let` \u2014 Introduces an immutable binding\n\n```\nlet name = value\nlet f x y = "
                       "body\n```";
            case Mut:
                return "`mut` \u2014 Marks a binding as mutable\n\n```\nlet mut counter = 0\ncounter <- "
                       "counter + 1\n```";
            case Fun:
                return "`fun` \u2014 Lambda expression (anonymous function)\n\n```\nfun x -> x + 1\nfun x y "
                       "-> x + y\n```";
            case Match:
                return "`match` \u2014 Pattern matching expression\n\n```\nmatch value with\n| pattern1 -> "
                       "result1\n| pattern2 -> result2\n```";
            case With: return "`with` \u2014 Introduces match arms or exception handlers";
            case When:
                return "`when` \u2014 Guard clause in pattern matching\n\n```\n| x when x > 0 -> "
                       "\"positive\"\n```";
            case Type:
                return "`type` \u2014 Defines a discriminated union type\n\n```\ntype Shape =\n| Circle of "
                       "float\n| Rectangle of float * float\n```";
            case Of: return "`of` \u2014 Specifies the payload type in a union case";
            case Rec:
                return "`rec` \u2014 Marks a binding as recursive\n\n```\nlet rec factorial n =\n  if n <= 1 "
                       "then 1\n  else n * factorial (n - 1)\n```";
            case And:
                return "`and` \u2014 Defines mutually recursive functions\n\n```\nlet rec isEven n = ... and "
                       "isOdd n = ...\n```";
            case As:
                return "`as` \u2014 Pattern alias, binds the whole matched value\n\n```\n| (Some x) as opt "
                       "-> ...\n```";
            case Try:
                return "`try` \u2014 Error handling expression\n\n```\ntry expression with\n| Error e -> "
                       "handler\n```";
            case Finally:
                return "`finally` \u2014 Code that always executes after try\n\n```\ntry expression finally "
                       "cleanup\n```";
            default: return std::nullopt;
        }
    }

    /// Returns hover markdown for constructor tokens.
    [[nodiscard]] std::optional<std::string> constructorHover(Token token)
    {
        using enum Token;
        switch (token)
        {
            case OptionSome: return "`Some` : `'a -> option<'a>`\n\nWraps a value in an option type.";
            case OptionNone: return "`None` : `option<'a>`\n\nRepresents the absence of a value.";
            case ResultOk: return "`Ok` : `'a -> result<'a, 'e>`\n\nWraps a success value in a result type.";
            case ResultError:
                return "`Error` : `'e -> result<'a, 'e>`\n\nWraps an error value in a result type.";
            default: return std::nullopt;
        }
    }

    /// Returns hover markdown for operator tokens.
    [[nodiscard]] std::optional<std::string> operatorHover(Token token)
    {
        using enum Token;
        switch (token)
        {
            case ForwardPipe:
                return "`|>` \u2014 Forward pipe operator\n\nPasses the left operand as the last argument to "
                       "the right function.\n\n```\nvalue |> f |> g\n```";
            case Arrow:
                return "`->` \u2014 Arrow operator\n\nUsed in function types, lambda expressions, and match "
                       "arms.";
            case LeftArrow:
                return "`<-` \u2014 Mutation operator\n\nAssigns a new value to a mutable binding.";
            case ColonColon:
                return "`::` \u2014 List cons operator\n\nPrepends an element to a list.\n\n```\n1 :: [2; 3] "
                       " // [1; 2; 3]\n```";
            case DotDot:
                return "`..` \u2014 Range operator\n\nCreates a range of values.\n\n```\n[1..10]\n```";
            case Question:
                return "`?` \u2014 Error propagation operator\n\nUnwraps Ok/Some or returns early with "
                       "Error/None.";
            case EqualEqual: return "`==` \u2014 Equality comparison";
            case NotEqual: return "`!=` \u2014 Inequality comparison";
            case AmpAmp: return "`&&` \u2014 Logical AND";
            case PipePipe: return "`||` \u2014 Logical OR";
            case StarStar: return "`**` \u2014 Exponentiation operator";
            case Pipe: return "`|` \u2014 Process pipe (shell) or match arm separator";
            default: return std::nullopt;
        }
    }

    /// Returns hover markdown for builtin function identifiers.
    [[nodiscard]] std::optional<std::string> builtinHover(std::string const& name)
    {
        static std::unordered_map<std::string, std::string> const builtins = {
            { "print", "`print` : `'a -> unit`\n\nPrints a value to stdout without a trailing newline." },
            { "println", "`println` : `'a -> unit`\n\nPrints a value to stdout followed by a newline." },
            { "string_length", "`string_length` : `string -> int`\n\nReturns the length of a string." },
            { "string_concat",
              "`string_concat` : `string -> string -> string`\n\nConcatenates two strings." },
            { "string_substring",
              "`string_substring` : `int -> int -> string -> string`\n\nExtracts a substring (start, length, "
              "string)." },
            { "int_to_string",
              "`int_to_string` : `int -> string`\n\nConverts an integer to its string representation." },
            { "string_to_int",
              "`string_to_int` : `string -> option<int>`\n\nParses a string as an integer, returning None on "
              "failure." },
            { "true", "`true` : `bool`\n\nBoolean true value." },
            { "false", "`false` : `bool`\n\nBoolean false value." },
            { "env",
              "`env` : `string -> option<string>`\n\nReturns `Some value` if the environment variable is "
              "set, `None` if not found." },
            { "shell_prompt_preset",
              "`shell_prompt_preset` : `string`\n\nPrompt theme preset (e.g. "
              "`\"endo-signature\"`, `\"minimal-arrow\"`). Read or write with `<-`." },
            { "shell_prompt_indicator",
              "`shell_prompt_indicator` : `string`\n\nPrompt indicator character(s) shown "
              "before user input. Read or write with `<-`." },
            { "shell_prompt_layout",
              "`shell_prompt_layout` : `string`\n\nPrompt layout: `\"single-line\"`, "
              "`\"two-line\"`, `\"boxed\"`, `\"powerline\"`. Read or write with `<-`." },
            { "shell_prompt_separator",
              "`shell_prompt_separator` : `string`\n\nSeparator style: `\"none\"`, `\"bar\"`, "
              "`\"rounded\"`, `\"powerline\"`, `\"boxed\"`. Read or write with `<-`." },
            { "shell_prompt_transient",
              "`shell_prompt_transient` : `string`\n\nTransient prompt mode: `\"off\"`, "
              "`\"minimal\"`, `\"arrow\"`. Read or write with `<-`." },
            { "shell_prompt_duration_threshold",
              "`shell_prompt_duration_threshold` : `int`\n\nMinimum command duration (ms) before "
              "showing elapsed time. Read or write with `<-`." },
            { "shell_prompt_spacing",
              "`shell_prompt_spacing` : `int`\n\nBlank lines above and below the "
              "prompt (0 or 1, default 1). Read or write with `<-`." },
            { "shell_ls_icons",
              "`shell_ls_icons` : `bool`\n\nShow Nerd Font icons next to filenames in `ls` output "
              "(default: `true`). Requires a Nerd Font. Read or write with `<-`." },
            { "shell_is_interactive",
              "`shell_is_interactive` : `bool`\n\nWhether the shell is running in interactive mode "
              "(true for REPL, false for scripts and `-c` commands). Read-only." },
            { "shell_exit_confirm_timeout",
              "`shell_exit_confirm_timeout` : `int`\n\nDouble Ctrl+D exit confirmation window in "
              "milliseconds (0 = immediate exit, default 1000). Read or write with `<-`." },
            // --- Agent general ---
            { "agent_provider",
              "`agent_provider` : `string`\n\nActive AI provider: `\"claude\"`, `\"openai\"`, "
              "`\"gemini\"`, `\"openai_compat\"`. Auto-detects if unset. Read or write with `<-`." },
            { "agent_prompt_indicator",
              "`agent_prompt_indicator` : `string`\n\nCharacter(s) shown at the agent mode prompt "
              "(default: `\"\u2771\"`). Read or write with `<-`." },
            { "agent_max_tool_result_size",
              "`agent_max_tool_result_size` : `int`\n\nMax bytes from a single tool call before "
              "truncation (default: 30720). Read or write with `<-`." },
            { "agent_log_tool_uses",
              "`agent_log_tool_uses` : `bool`\n\nPrint tool invocations to the terminal "
              "(default: `true`). Read or write with `<-`." },
            // --- Claude provider ---
            { "agent_claude_api_key",
              "`agent_claude_api_key` : `string`\n\nAnthropic API key. "
              "Read or write with `<-`." },
            { "agent_claude_api_key_env",
              "`agent_claude_api_key_env` : `string`\n\nEnvironment variable name for the Claude "
              "API key (default: `\"ANTHROPIC_API_KEY\"`). Read or write with `<-`." },
            { "agent_claude_model",
              "`agent_claude_model` : `string`\n\nClaude model identifier "
              "(default: `\"claude-sonnet-4-5-20250929\"`). Read or write with `<-`." },
            { "agent_claude_max_tokens",
              "`agent_claude_max_tokens` : `int`\n\nMax output tokens per Claude request "
              "(default: 8192). Read or write with `<-`." },
            { "agent_claude_thinking_mode",
              "`agent_claude_thinking_mode` : `string`\n\nThinking/reasoning mode: `\"off\"`, "
              "`\"normal\"`, `\"extended\"`. Read or write with `<-`." },
            { "agent_claude_prompt_caching",
              "`agent_claude_prompt_caching` : `bool`\n\nEnable prompt caching for Claude requests "
              "(default: `true`). Read or write with `<-`." },
            { "agent_claude_auth_type",
              "`agent_claude_auth_type` : `string`\n\nAuthentication method: `\"auto\"`, "
              "`\"oauth\"`, `\"api_key\"`. Read or write with `<-`." },
            // --- OpenAI provider ---
            { "agent_openai_api_key",
              "`agent_openai_api_key` : `string`\n\nOpenAI API key. "
              "Read or write with `<-`." },
            { "agent_openai_api_key_env",
              "`agent_openai_api_key_env` : `string`\n\nEnvironment variable name for the OpenAI "
              "API key (default: `\"OPENAI_API_KEY\"`). Read or write with `<-`." },
            { "agent_openai_model",
              "`agent_openai_model` : `string`\n\nOpenAI model identifier "
              "(default: `\"gpt-4o\"`). Read or write with `<-`." },
            { "agent_openai_base_url",
              "`agent_openai_base_url` : `string`\n\nCustom OpenAI-compatible base URL. "
              "Read or write with `<-`." },
            { "agent_openai_max_tokens",
              "`agent_openai_max_tokens` : `int`\n\nMax output tokens per OpenAI request "
              "(default: 4096). Read or write with `<-`." },
            { "agent_openai_thinking_mode",
              "`agent_openai_thinking_mode` : `string`\n\nOpenAI thinking/reasoning mode. "
              "Read or write with `<-`." },
            // --- OpenAI-compatible provider ---
            { "agent_openai_compat_api_key",
              "`agent_openai_compat_api_key` : `string`\n\nAPI key for the OpenAI-compatible "
              "endpoint. Read or write with `<-`." },
            { "agent_openai_compat_api_key_env",
              "`agent_openai_compat_api_key_env` : `string`\n\nEnvironment variable name for the "
              "OpenAI-compatible API key. Read or write with `<-`." },
            { "agent_openai_compat_model",
              "`agent_openai_compat_model` : `string`\n\nOpenAI-compatible model identifier. "
              "Read or write with `<-`." },
            { "agent_openai_compat_base_url",
              "`agent_openai_compat_base_url` : `string`\n\nOpenAI-compatible endpoint base URL "
              "(e.g. `\"http://localhost:11434/v1\"`). Read or write with `<-`." },
            { "agent_openai_compat_max_tokens",
              "`agent_openai_compat_max_tokens` : `int`\n\nMax output tokens per "
              "OpenAI-compatible request (default: 4096). Read or write with `<-`." },
            { "agent_openai_compat_thinking_mode",
              "`agent_openai_compat_thinking_mode` : `string`\n\nOpenAI-compatible "
              "thinking/reasoning mode. Read or write with `<-`." },
            // --- Gemini provider ---
            { "agent_gemini_api_key",
              "`agent_gemini_api_key` : `string`\n\nGoogle Gemini API key. "
              "Read or write with `<-`." },
            { "agent_gemini_api_key_env",
              "`agent_gemini_api_key_env` : `string`\n\nEnvironment variable name for the Gemini "
              "API key (default: `\"GEMINI_API_KEY\"`). Read or write with `<-`." },
            { "agent_gemini_model",
              "`agent_gemini_model` : `string`\n\nGemini model identifier "
              "(default: `\"gemini-2.5-flash\"`). Read or write with `<-`." },
            { "agent_gemini_max_tokens",
              "`agent_gemini_max_tokens` : `int`\n\nMax output tokens per Gemini request "
              "(default: 8192). Read or write with `<-`." },
            { "agent_gemini_thinking_mode",
              "`agent_gemini_thinking_mode` : `string`\n\nGemini thinking/reasoning mode. "
              "Read or write with `<-`." },
            // --- Plan mode ---
            { "agent_plan_mode_enabled",
              "`agent_plan_mode_enabled` : `bool`\n\nWhether `/plan` is available "
              "(default: `true`). Read or write with `<-`." },
            { "agent_plan_mode_pause_between_steps",
              "`agent_plan_mode_pause_between_steps` : `bool`\n\nPause for confirmation between "
              "plan steps (default: `false`). Read or write with `<-`." },
            { "agent_plan_mode_max_exploration_turns",
              "`agent_plan_mode_max_exploration_turns` : `int`\n\nMax exploration iterations before "
              "requiring a plan (default: 15). Read or write with `<-`." },
            // --- Explore sub-agent ---
            { "agent_explore_max_turns",
              "`agent_explore_max_turns` : `int`\n\nMaximum iterations for the explore sub-agent "
              "(default: 10). Read or write with `<-`." },
            // --- Session / lifecycle ---
            { "agent_auto_resume",
              "`agent_auto_resume` : `bool`\n\nAutomatically resume the last agent session on "
              "startup. Read or write with `<-`." },
            { "agent_session_replay",
              "`agent_session_replay` : `bool`\n\nReplay session history when resuming. "
              "Read or write with `<-`." },
            // --- Tracing ---
            { "agent_trace_enabled",
              "`agent_trace_enabled` : `bool`\n\nEnable tool I/O trace logging "
              "(default: `false`). Read or write with `<-`." },
            { "agent_trace_default_path",
              "`agent_trace_default_path` : `string`\n\nTrace file path (empty = auto-generated "
              "in `.endo/trace-logs/`). Read or write with `<-`." },
            { "agent_trace_max_files",
              "`agent_trace_max_files` : `int`\n\nMax auto-generated trace files to retain "
              "(default: 20). Read or write with `<-`." },
            // --- Permissions ---
            { "agent_permissions_policy",
              "`agent_permissions_policy` : `string`\n\nPermission policy: `\"ask\"` (default), "
              "`\"trust_session\"`, `\"trust_all\"`, `\"read_only\"`. Read or write with `<-`." },
            { "agent_trusted_tool",
              "`agent_trusted_tool` : `list<string>`\n\nTools auto-approved regardless of risk "
              "level. Read or write with `<-`." },
            { "agent_blocked_pattern",
              "`agent_blocked_pattern` : `list<string>`\n\nShell command patterns unconditionally "
              "blocked. Read or write with `<-`." },
            // --- Web search ---
            { "agent_web_search_engine",
              "`agent_web_search_engine` : `string`\n\nSearch engine: `\"duckduckgo\"` (default), "
              "`\"brave\"`, `\"google\"`. Read or write with `<-`." },
            { "agent_web_search_api_key",
              "`agent_web_search_api_key` : `string`\n\nAPI key for Brave or Google search. "
              "Read or write with `<-`." },
            { "agent_web_search_cx",
              "`agent_web_search_cx` : `string`\n\nGoogle Custom Search Engine ID. "
              "Read or write with `<-`." },
            { "agent_web_search_max_results",
              "`agent_web_search_max_results` : `int`\n\nMax results per query "
              "(default: 5, max: 20). Read or write with `<-`." },
            // --- Error recovery ---
            { "agent_error_recovery_action",
              "`agent_error_recovery_action` : `string`\n\nAction when a shell command fails: "
              "`\"ask\"` (prompt user), `\"analyze\"` (auto-analyze), `\"ignore\"` (do nothing). "
              "Read or write with `<-`." },
            { "agent_error_recovery_model",
              "`agent_error_recovery_model` : `string`\n\nModel to use for error recovery analysis. "
              "Empty string uses the active agent model. Read or write with `<-`." },
            { "Size",
              "`Size` \u2014 Record type for byte sizes with human-readable display\n\n"
              "**Fields:** `bytes`\n\n"
              "```endo\nSize.fromBytes 1024  // 1 KB\n"
              "Size.fromKB 5       // 5 KB\n"
              "1_MB                 // size literal: 1 MB\n"
              "s.bytes              // raw byte count\n```" },
            { "Size.fromBytes", "`Size.fromBytes` : `int -> Size`\n\nCreates a Size from a raw byte count." },
            { "Size.fromKB",
              "`Size.fromKB` : `int -> Size`\n\nCreates a Size from kilobytes (n \u00d7 1024)." },
            { "Size.fromMB",
              "`Size.fromMB` : `int -> Size`\n\nCreates a Size from megabytes (n \u00d7 1024\u00b2)." },
            { "Size.fromGB",
              "`Size.fromGB` : `int -> Size`\n\nCreates a Size from gigabytes (n \u00d7 1024\u00b3)." },
            { "Size.fromTB",
              "`Size.fromTB` : `int -> Size`\n\nCreates a Size from terabytes (n \u00d7 1024\u2074)." },
            { "DateTime",
              "`DateTime` \u2014 Record type for date/time values (UTC)\n\n"
              "**Fields:** `year`, `month`, `day`, `hour`, `minute`, `second`, `epoch`\n\n"
              "```endo\nDateTime.now         // current UTC time\n"
              "DateTime.fromEpoch n // DateTime from Unix epoch\n"
              "d.year               // access individual fields\n```" },
            { "DateTime.now", "`DateTime.now` : `DateTime`\n\nReturns the current UTC date and time." },
            { "DateTime.fromEpoch",
              "`DateTime.fromEpoch` : `int -> DateTime`\n\nConverts a Unix epoch timestamp to a "
              "DateTime record." },
            { "FileInfo",
              "`FileInfo` \u2014 Record type for file/directory information\n\n"
              "**Fields:** `name: str`, `size: Size`, `mode: int`, `mtime: DateTime`, `isDir: bool`\n\n"
              "Returned by `ls`. Supports dot access and pattern matching.\n\n"
              "```endo\nls |> filter (_.size.bytes > 1024) |> map _.name\n```" },
            { "ProcessInfo",
              "`ProcessInfo` \u2014 Record type for process information\n\n"
              "**Fields:** `pid: int`, `ppid: int`, `user: str`, `cpu: float`, `mem: float`, "
              "`command: str`\n\n"
              "Returned by `ps`. Supports dot access and pattern matching.\n\n"
              "```endo\nps |> filter (_.cpu > 5.0) |> sortBy _.cpu\n```" },
            { "JobInfo",
              "`JobInfo` \u2014 Record type for background job information\n\n"
              "**Fields:** `id: int`, `state: str`, `command: str`, `pid: int`\n\n"
              "Returned by `jobs`. Supports dot access and pattern matching.\n\n"
              "```endo\njobs |> filter (_.state == \"Running\")\n```" },
            { "ls",
              "`ls` : `list<FileInfo>` | `ls path` : `list<FileInfo>`\n\n"
              "Lists directory contents as structured FileInfo records.\n\n"
              "**Fields:** `name: str`, `size: Size`, `mode: int`, `mtime: DateTime`, `isDir: bool`" },
            { "ps",
              "`ps` : `list<ProcessInfo>`\n\n"
              "Lists running processes as structured ProcessInfo records.\n\n"
              "**Fields:** `pid: int`, `ppid: int`, `user: str`, `cpu: float`, `mem: float`, "
              "`command: str`" },
            { "jobs",
              "`jobs` : `list<JobInfo>`\n\n"
              "Lists background jobs as structured JobInfo records.\n\n"
              "**Fields:** `id: int`, `state: str`, `command: str`, `pid: int`" },
            { "fetch",
              "`fetch` : `str -> result<str, str>`\n\n"
              "Fetches content from a URL. Returns `Ok body` on success, `Error message` on failure." },
            { "which",
              "`which` : `str -> option<str>`\n\n"
              "Searches `$PATH` for a program. Returns `Some path` if found, `None` otherwise." },
            { "rand",
              "`rand` : `int` | `rand min max` : `int`\n\n"
              "Returns a random positive integer, or a random integer in `[min, max]`." },
            { "formatDateTime",
              "`formatDateTime` : `int -> str`\n\n"
              "Formats a Unix epoch timestamp as `YYYY-MM-DD HH:MM:SS`." },
            { "formatMode",
              "`formatMode` : `int -> str`\n\n"
              "Formats a Unix file mode as a `rwxrwxrwx` permission string." },
            { "toText",
              "`toText` : `'a -> str`\n\n"
              "Converts any value to its string representation." },
            { "isReadable",
              "`isReadable` : `int -> bool`\n\n"
              "Tests if any read permission bit is set in a Unix file mode." },
            { "isWritable",
              "`isWritable` : `int -> bool`\n\n"
              "Tests if any write permission bit is set in a Unix file mode." },
            { "isExecutable",
              "`isExecutable` : `int -> bool`\n\n"
              "Tests if any execute permission bit is set in a Unix file mode." },
            { "formatNumber",
              "`formatNumber` : `str -> int -> str` | `int -> str`\n\n"
              "Formats an integer with thousand separators. In the 1-arg form, uses the locale separator." },
            { "string",
              "`string` : `'a -> str`\n\n"
              "Universal conversion to string. Works with integers, floats, booleans, and strings." },
            { "not",
              "`not` : `bool -> bool`\n\n"
              "Boolean negation." },
        };

        if (auto const it = builtins.find(name); it != builtins.end())
            return it->second;
        return std::nullopt;
    }

    /// Renders an AST expression as a concise source string for hover preview.
    /// Handles common expression types; returns std::nullopt for complex expressions.
    /// @param expr The expression to render
    /// @return A preview string, or std::nullopt if the expression is too complex
    [[nodiscard]] std::optional<std::string> exprToString(ast::Expr const& expr)
    {
        if (auto const* e = dynamic_cast<ast::IntLiteralExpr const*>(&expr))
            return std::to_string(e->value);
        if (auto const* e = dynamic_cast<ast::FloatLiteralExpr const*>(&expr))
        {
            auto s = std::to_string(e->value);
            // Remove trailing zeros but keep at least one decimal digit
            if (s.find('.') != std::string::npos)
            {
                s.erase(s.find_last_not_of('0') + 1);
                if (s.back() == '.')
                    s += '0';
            }
            return s;
        }
        if (auto const* e = dynamic_cast<ast::BoolLiteralExpr const*>(&expr))
            return e->value ? std::string("true") : std::string("false");
        if (auto const* e = dynamic_cast<ast::LiteralExpr const*>(&expr))
            return "\"" + e->value + "\"";
        if (auto const* e = dynamic_cast<ast::IdentifierExpr const*>(&expr))
            return e->name;
        if (auto const* e = dynamic_cast<ast::ParenExpr const*>(&expr))
        {
            if (auto inner = exprToString(*e->inner))
                return "(" + *inner + ")";
        }
        if (auto const* e = dynamic_cast<ast::OptionExpr const*>(&expr))
        {
            if (!e->isSome)
                return std::string("None");
            if (e->value)
                if (auto val = exprToString(*e->value))
                    return "Some " + *val;
            return std::string("Some ...");
        }
        if (auto const* e = dynamic_cast<ast::ResultExpr const*>(&expr))
        {
            auto const prefix = e->isOk ? std::string("Ok ") : std::string("Error ");
            if (e->payload)
                if (auto val = exprToString(*e->payload))
                    return prefix + *val;
            return prefix + "...";
        }
        if (auto const* e = dynamic_cast<ast::TupleExpr const*>(&expr))
        {
            std::string result = "(";
            for (size_t i = 0; i < e->elements.size(); ++i)
            {
                if (i > 0)
                    result += ", ";
                if (auto val = exprToString(*e->elements[i]))
                    result += *val;
                else
                    result += "...";
            }
            result += ")";
            return result;
        }
        if (auto const* e = dynamic_cast<ast::UnaryExpr const*>(&expr))
        {
            if (auto operand = exprToString(*e->operand))
            {
                switch (e->op)
                {
                    case ast::UnaryOp::Neg: return "-" + *operand;
                    case ast::UnaryOp::Not: return "!" + *operand;
                }
            }
        }
        if (auto const* e = dynamic_cast<ast::BinaryExpr const*>(&expr))
        {
            auto lhs = exprToString(*e->left);
            auto rhs = exprToString(*e->right);
            if (lhs && rhs)
            {
                auto const opStr = [&]() -> std::string {
                    switch (e->op)
                    {
                        case ast::BinaryOp::Add: return " + ";
                        case ast::BinaryOp::Sub: return " - ";
                        case ast::BinaryOp::Mul: return " * ";
                        case ast::BinaryOp::Div: return " / ";
                        case ast::BinaryOp::Mod: return " % ";
                        case ast::BinaryOp::Pow: return " ** ";
                        case ast::BinaryOp::Eq: return " == ";
                        case ast::BinaryOp::Ne: return " != ";
                        case ast::BinaryOp::Lt: return " < ";
                        case ast::BinaryOp::Le: return " <= ";
                        case ast::BinaryOp::Gt: return " > ";
                        case ast::BinaryOp::Ge: return " >= ";
                        case ast::BinaryOp::And: return " && ";
                        case ast::BinaryOp::Or: return " || ";
                    }
                    return " ? ";
                }();
                return *lhs + opStr + *rhs;
            }
        }
        if (auto const* e = dynamic_cast<ast::ApplicationExpr const*>(&expr))
        {
            auto func = exprToString(*e->function);
            auto arg = exprToString(*e->argument);
            if (func && arg)
                return *func + " " + *arg;
        }
        if (auto const* e = dynamic_cast<ast::LambdaExpr const*>(&expr))
        {
            std::string result = "fun";
            for (auto const& param: e->parameters)
                result += " " + param.name;
            result += " -> ...";
            return result;
        }
        if (auto const* e = dynamic_cast<ast::PipelineExpr const*>(&expr))
        {
            auto val = exprToString(*e->value);
            auto func = exprToString(*e->function);
            if (val && func)
                return *val + " |> " + *func;
        }
        if (auto const* e = dynamic_cast<ast::RecordExpr const*>(&expr))
        {
            std::string result = "{ ";
            for (size_t i = 0; i < e->fields.size(); ++i)
            {
                if (i > 0)
                    result += "; ";
                result += e->fields[i].name + " = ";
                if (auto val = exprToString(*e->fields[i].value))
                    result += *val;
                else
                    result += "...";
            }
            result += " }";
            return result;
        }
        return std::nullopt;
    }

    /// Formats hover markdown for a let binding signature (variable or function).
    /// @param name The binding name
    /// @param isExported Whether the binding is exported as an environment variable
    /// @param isMutable Whether the binding is mutable
    /// @param isRecursive Whether the binding is recursive
    /// @param parameters Function parameters (empty for simple bindings)
    /// @param returnType Optional return type annotation
    /// @param valuePreview Optional preview of the value expression source text
    /// @param detectedType Optional detected type name (e.g., record type from value)
    /// @param typeDefinition Optional type definition source text for supplementary info
    /// @return Markdown hover string
    [[nodiscard]] std::string formatLetBinding(std::string const& name,
                                               bool isExported,
                                               bool isMutable,
                                               bool isRecursive,
                                               std::vector<ast::TypedParameter> const& parameters,
                                               std::optional<TypePtr> const& returnType,
                                               std::optional<std::string> const& valuePreview = {},
                                               std::optional<std::string> const& detectedType = {},
                                               std::optional<std::string> const& typeDefinition = {})
    {
        std::string result;

        if (!parameters.empty())
        {
            result = "`" + name + "` \u2014 function\n\n```endo\nlet ";
            if (isExported)
                result += "export ";
            if (isRecursive)
                result += "rec ";
            result += name;
            for (auto const& param: parameters)
            {
                if (param.typeAnnotation)
                    result += " (" + param.name + ": " + toString(*param.typeAnnotation) + ")";
                else
                    result += " " + param.name;
            }
            if (returnType)
                result += ": " + toString(*returnType);
            result += "\n```";
        }
        else
        {
            // Determine the display type: explicit returnType takes precedence, then detectedType
            auto const displayType = returnType     ? std::optional<std::string>(toString(*returnType))
                                     : detectedType ? detectedType
                                                    : std::nullopt;

            result = "`" + name + "` \u2014 ";
            if (isExported)
                result += "exported ";
            if (isMutable)
                result += "mutable ";
            result += "binding";
            if (displayType)
                result += " : `" + *displayType + "`";
            result += "\n\n```endo\nlet ";
            if (isExported)
                result += "export ";
            if (isMutable)
                result += "mut ";
            result += name;
            if (displayType)
                result += ": " + *displayType;
            if (valuePreview && !valuePreview->empty())
                result += " = " + *valuePreview;
            result += "\n```";

            // Append type definition if available
            if (typeDefinition)
                result += "\n\n```endo\n" + *typeDefinition + "\n```";
        }

        return result;
    }

    /// Formats hover markdown for a function parameter.
    /// @param param The parameter with optional type annotation
    /// @param functionName The enclosing function name
    /// @return Markdown hover string
    [[nodiscard]] std::string formatParameter(ast::TypedParameter const& param,
                                              std::string const& functionName)
    {
        auto result = "`" + param.name + "` \u2014 parameter of `" + functionName + "`";
        if (param.typeAnnotation)
            result += "\n\n```endo\n" + param.name + ": " + toString(*param.typeAnnotation) + "\n```";
        return result;
    }

    /// Formats a record type definition as a source string for hover preview.
    /// @param recordDef The record type definition statement
    /// @return Source text like "type Person = { name: str; age: int }"
    [[nodiscard]] std::string formatRecordTypeDef(ast::RecordTypeDefStmt const& recordDef)
    {
        std::string result = "type " + recordDef.name + " = { ";
        for (size_t i = 0; i < recordDef.fields.size(); ++i)
        {
            if (i > 0)
                result += "; ";
            result += recordDef.fields[i].name + ": " + toString(recordDef.fields[i].type);
        }
        result += " }";
        return result;
    }

    /// Returns hover markdown for a user-defined binding or function parameter.
    ///
    /// Parses the source into an AST and searches top-level `let` bindings and their
    /// parameters for a matching name. For record bindings, detects the record type name
    /// and includes the type definition.
    ///
    /// @param source The full document text
    /// @param name The identifier name to look up
    /// @return Hover markdown if a matching binding was found, otherwise std::nullopt
    [[nodiscard]] std::optional<std::string> bindingHover(std::string const& source, std::string const& name)
    {
        CoreVM::Runtime runtime;
        registerStubRuntime(runtime);

        CoreVM::diagnostics::BufferedReport report;
        Parser parser(runtime, report, std::make_unique<StringSource>(source));
        auto astRoot = parser.parse();
        if (!astRoot)
            return std::nullopt;

        // Collect top-level LetBindingStmt and RecordTypeDefStmt nodes
        std::vector<ast::LetBindingStmt const*> bindings;
        std::unordered_map<std::string, ast::RecordTypeDefStmt const*> recordTypeDefs;
        if (auto const* compound = dynamic_cast<ast::CompoundStmt const*>(astRoot.get()))
        {
            for (auto const& stmt: compound->statements)
            {
                if (auto const* letStmt = dynamic_cast<ast::LetBindingStmt const*>(stmt.get()))
                    bindings.push_back(letStmt);
                else if (auto const* recordDef = dynamic_cast<ast::RecordTypeDefStmt const*>(stmt.get()))
                    recordTypeDefs[recordDef->name] = recordDef;
            }
        }
        else if (auto const* letStmt = dynamic_cast<ast::LetBindingStmt const*>(astRoot.get()))
        {
            bindings.push_back(letStmt);
        }

        // Check binding names (including and-bindings for mutual recursion)
        for (auto const* letStmt: bindings)
        {
            if (letStmt->name == name)
            {
                auto valuePreview = std::optional<std::string> {};
                auto detectedType = std::optional<std::string> {};
                auto typeDefinition = std::optional<std::string> {};

                if (!letStmt->isFunction() && letStmt->value)
                {
                    valuePreview = exprToString(*letStmt->value);

                    // Detect record type from RecordExpr value (only if no explicit returnType)
                    if (!letStmt->returnType)
                    {
                        if (auto const* recordExpr =
                                dynamic_cast<ast::RecordExpr const*>(letStmt->value.get()))
                        {
                            if (!recordExpr->typeName.empty())
                            {
                                detectedType = recordExpr->typeName;
                                // Look up the type definition for supplementary info
                                if (auto const it = recordTypeDefs.find(recordExpr->typeName);
                                    it != recordTypeDefs.end())
                                    typeDefinition = formatRecordTypeDef(*it->second);
                            }
                        }
                    }
                    else
                    {
                        // Explicit annotation — check if it's a known record type for the definition
                        auto const typeStr = toString(*letStmt->returnType);
                        if (auto const it = recordTypeDefs.find(typeStr); it != recordTypeDefs.end())
                            typeDefinition = formatRecordTypeDef(*it->second);
                    }
                }

                return formatLetBinding(name,
                                        letStmt->isExported,
                                        letStmt->isMutable,
                                        letStmt->isRecursive,
                                        letStmt->parameters,
                                        letStmt->returnType,
                                        valuePreview,
                                        detectedType,
                                        typeDefinition);
            }

            for (auto const& andBinding: letStmt->andBindings)
            {
                if (andBinding.name == name)
                    return formatLetBinding(
                        name, false, false, true, andBinding.parameters, andBinding.returnType);
            }
        }

        // Check function parameters
        for (auto const* letStmt: bindings)
        {
            for (auto const& param: letStmt->parameters)
            {
                if (param.name == name)
                    return formatParameter(param, letStmt->name);
            }

            for (auto const& andBinding: letStmt->andBindings)
            {
                for (auto const& param: andBinding.parameters)
                {
                    if (param.name == name)
                        return formatParameter(param, andBinding.name);
                }
            }
        }

        return std::nullopt;
    }

} // namespace

std::optional<HoverInfo> computeHover(std::string const& source, SourcePosition position)
{
    // Tokenize with F# mode for proper operator recognition
    auto lexer = Lexer { std::make_unique<StringSource>(source) };
    lexer.enterFSharpExpr();

    std::vector<TokenInfo> tokens;
    while (lexer.currentToken() != Token::EndOfInput)
    {
        tokens.emplace_back(TokenInfo { lexer.currentToken(), lexer.currentLiteral(), lexer.currentRange() });
        lexer.nextToken();
    }

    for (auto const& tokenInfo: tokens)
    {
        if (tokenInfo.token == Token::EndOfInput)
            continue;

        if (!containsPosition(tokenInfo.location, position))
            continue;

        auto const range = toSourceRange(tokenInfo.location);

        // F# interpolated string hover
        if (tokenInfo.token == Token::FStringStart)
            return HoverInfo { .markdownText =
                                   "`$\"...\"`  \u2014 F#-style interpolated string. Embed expressions with "
                                   "`{expr}`.\n\n```endo\n$\"Hello, {name}!\"\n$\"Sum is {3 + 4}\"\n```",
                               .range = range };

        // Try keyword hover
        if (auto text = keywordHover(tokenInfo.token))
            return HoverInfo { .markdownText = std::move(*text), .range = range };

        // Try constructor hover
        if (auto text = constructorHover(tokenInfo.token))
            return HoverInfo { .markdownText = std::move(*text), .range = range };

        // Try operator hover
        if (auto text = operatorHover(tokenInfo.token))
            return HoverInfo { .markdownText = std::move(*text), .range = range };

        // Boolean literal hover
        if (tokenInfo.token == Token::True)
            return HoverInfo { .markdownText = "`true` : `bool`\n\nBoolean true value.", .range = range };
        if (tokenInfo.token == Token::False)
            return HoverInfo { .markdownText = "`false` : `bool`\n\nBoolean false value.", .range = range };

        // Try builtin hover for identifiers
        if (tokenInfo.token == Token::Identifier)
        {
            if (auto text = builtinHover(tokenInfo.literal))
                return HoverInfo { .markdownText = std::move(*text), .range = range };

            // Try user-defined binding hover (requires AST parsing)
            if (auto text = bindingHover(source, tokenInfo.literal))
                return HoverInfo { .markdownText = std::move(*text), .range = range };
        }

        // No hover info for this token
        return std::nullopt;
    }

    return std::nullopt;
}

} // namespace endo
