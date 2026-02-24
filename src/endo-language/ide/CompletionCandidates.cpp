// SPDX-License-Identifier: Apache-2.0
#include <endo-language/builtins/BuiltinSignatures.hpp>
#include <endo-language/ide/CompletionCandidates.hpp>
#include <endo-language/ide/TypeRegistryCompletionAdapter.hpp>

#include <CoreVM/types/TypeRegistry.hpp>

#include <array>
#include <set>

namespace endo
{

namespace
{
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

    /// @brief Standard library function entry with name, signature description, and detail.
    struct StdLibEntry
    {
        std::string_view name;
        std::string_view description;
        std::string_view detail;
    };

    // clang-format off
    constexpr std::array stdLibFunctions = {
        // Type Conversion
        StdLibEntry { "string_length", "string_length s -> int",
            "**string_length** `s -> int`\n\nReturns the length of string **s** in characters." },
        StdLibEntry { "int_of_string", "int_of_string s -> int",
            "**int_of_string** `s -> int`\n\nParses string **s** as an integer." },
        StdLibEntry { "string_of_int", "string_of_int n -> string",
            "**string_of_int** `n -> string`\n\nConverts integer **n** to its string representation." },
        StdLibEntry { "not", "not b -> bool",
            "**not** `b -> bool`\n\nLogical negation of boolean **b**." },
        // String Operations
        StdLibEntry { "trim", "trim s -> string",
            "**trim** `s -> string`\n\nRemoves leading and trailing whitespace from **s**." },
        StdLibEntry { "toLower", "toLower s -> string",
            "**toLower** `s -> string`\n\nConverts all characters in **s** to lowercase." },
        StdLibEntry { "toUpper", "toUpper s -> string",
            "**toUpper** `s -> string`\n\nConverts all characters in **s** to uppercase." },
        StdLibEntry { "contains", "contains substr s -> bool",
            "**contains** `substr s -> bool`\n\nReturns true if **s** contains **substr**." },
        StdLibEntry { "startsWith", "startsWith prefix s -> bool",
            "**startsWith** `prefix s -> bool`\n\nReturns true if **s** starts with **prefix**." },
        StdLibEntry { "endsWith", "endsWith suffix s -> bool",
            "**endsWith** `suffix s -> bool`\n\nReturns true if **s** ends with **suffix**." },
        StdLibEntry { "replace", "replace old new s -> string",
            "**replace** `old new s -> string`\n\nReplaces all occurrences of **old** with **new** in **s**." },
        StdLibEntry { "split", "split delim s -> list<string>",
            "**split** `delim s -> list<string>`\n\nSplits **s** by delimiter **delim**." },
        StdLibEntry { "join", "join delim lst -> string",
            "**join** `delim lst -> string`\n\nJoins list elements with **delim** between them." },
        // List Basic
        StdLibEntry { "head", "head lst -> 'a",
            "**head** `lst -> 'a`\n\nReturns the first element of the list." },
        StdLibEntry { "tail", "tail lst -> list<'a>",
            "**tail** `lst -> list<'a>`\n\nReturns the list without its first element." },
        StdLibEntry { "length", "length lst -> int",
            "**length** `lst -> int`\n\nReturns the number of elements in the list." },
        StdLibEntry { "isEmpty", "isEmpty lst -> bool",
            "**isEmpty** `lst -> bool`\n\nReturns true if the list is empty." },
        StdLibEntry { "nth", "nth n lst -> 'a",
            "**nth** `n lst -> 'a`\n\nReturns the element at index **n** (0-based)." },
        StdLibEntry { "last", "last lst -> 'a",
            "**last** `lst -> 'a`\n\nReturns the last element of the list." },
        StdLibEntry { "replicate", "replicate n x -> list<'a>",
            "**replicate** `n x -> list<'a>`\n\nCreates a list of **n** copies of **x**." },
        // List HOFs
        StdLibEntry { "map", "map f lst -> list<'b>",
            "**map** `f lst -> list<'b>`\n\nApplies function **f** to each element of the list." },
        StdLibEntry { "filter", "filter pred lst -> list<'a>",
            "**filter** `pred lst -> list<'a>`\n\nKeeps only elements satisfying **pred**." },
        StdLibEntry { "fold", "fold f init lst -> 'b",
            "**fold** `f init lst -> 'b`\n\nReduces the list from the left with **f** and initial value **init**." },
        StdLibEntry { "reduce", "reduce f lst -> 'a",
            "**reduce** `f lst -> 'a`\n\nReduces the list from the left with **f** using the first element as initial." },
        StdLibEntry { "find", "find pred lst -> option<'a>",
            "**find** `pred lst -> option<'a>`\n\nReturns `Some x` for the first element matching **pred**, or `None`." },
        StdLibEntry { "exists", "exists pred lst -> bool",
            "**exists** `pred lst -> bool`\n\nReturns true if any element satisfies **pred**." },
        StdLibEntry { "forall", "forall pred lst -> bool",
            "**forall** `pred lst -> bool`\n\nReturns true if all elements satisfy **pred**." },
        StdLibEntry { "each", "each f lst -> unit",
            "**each** `f lst -> unit`\n\nApplies **f** to each element for side effects." },
        // List Transforms
        StdLibEntry { "sort", "sort lst -> list<'a>",
            "**sort** `lst -> list<'a>`\n\nReturns the list sorted in ascending order." },
        StdLibEntry { "reverse", "reverse lst -> list<'a>",
            "**reverse** `lst -> list<'a>`\n\nReturns the list in reverse order." },
        StdLibEntry { "distinct", "distinct lst -> list<'a>",
            "**distinct** `lst -> list<'a>`\n\nRemoves duplicate elements from the list." },
        StdLibEntry { "sortBy", "sortBy f lst -> list<'a>",
            "**sortBy** `f lst -> list<'a>`\n\nSorts the list by the key returned by **f**." },
        StdLibEntry { "groupBy", "groupBy f lst -> list<list<'a>>",
            "**groupBy** `f lst -> list<list<'a>>`\n\nGroups consecutive elements with equal keys from **f**." },
        StdLibEntry { "take", "take n lst -> list<'a>",
            "**take** `n lst -> list<'a>`\n\nReturns the first **n** elements of the list." },
        StdLibEntry { "drop", "drop n lst -> list<'a>",
            "**drop** `n lst -> list<'a>`\n\nSkips the first **n** elements and returns the rest." },
        StdLibEntry { "zip", "zip lst1 lst2 -> list<'a * 'b>",
            "**zip** `lst1 lst2 -> list<'a * 'b>`\n\nCombines two lists into a list of pairs." },
        StdLibEntry { "flatten", "flatten lst -> list<'a>",
            "**flatten** `lst -> list<'a>`\n\nFlattens a list of lists into a single list." },
        // Formatting Helpers
        StdLibEntry { "formatNumber", "formatNumber sep n -> string  |  formatNumber n -> string (locale)",
            "**formatNumber** `sep n -> string`\n\nFormats a number with thousands separator **sep**.\nAlso: `formatNumber n` uses locale default." },
        StdLibEntry { "formatDateTime", "formatDateTime epoch -> string",
            "**formatDateTime** `epoch -> string`\n\nFormats an epoch timestamp as a human-readable date/time." },
        StdLibEntry { "formatMode", "formatMode mode -> string (rwxrwxrwx)",
            "**formatMode** `mode -> string`\n\nFormats a file mode as `rwxrwxrwx` permission string." },
        StdLibEntry { "toText", "toText obj -> string",
            "**toText** `obj -> string`\n\nConverts a structured object to a text representation." },
        StdLibEntry { "string", "string x -> string",
            "**string** `x -> string`\n\nConverts any value to its string representation." },
        // Permission Tests
        StdLibEntry { "isReadable", "isReadable mode -> bool",
            "**isReadable** `mode -> bool`\n\nReturns true if the file mode indicates read permission." },
        StdLibEntry { "isWritable", "isWritable mode -> bool",
            "**isWritable** `mode -> bool`\n\nReturns true if the file mode indicates write permission." },
        StdLibEntry { "isExecutable", "isExecutable mode -> bool",
            "**isExecutable** `mode -> bool`\n\nReturns true if the file mode indicates execute permission." },
        // Environment/System
        StdLibEntry { "env", "env name -> option<string>",
            "**env** `name -> option<string>`\n\nLooks up environment variable **name**. Returns `Some value` or `None`." },
        StdLibEntry { "which", "which name -> option<string>",
            "**which** `name -> option<string>`\n\nFinds the full path of command **name** in `$PATH`." },
        StdLibEntry { "ps", "ps -> list<ProcessInfo>",
            "**ps** `-> list<ProcessInfo>`\n\nReturns a list of running processes with pid, user, cpu, mem, command fields." },
        StdLibEntry { "ls", "ls -> list<FileInfo>  |  ls path -> list<FileInfo>",
            "**ls** `-> list<FileInfo>`\n\nLists files in the current directory (or given **path**) as structured records." },
        StdLibEntry { "rand", "rand -> int  |  rand min max -> int",
            "**rand** `-> int`\n\nReturns a random integer.\nAlso: `rand min max` for a random integer in range." },
        StdLibEntry { "fetch", "fetch url -> result<string, string>",
            "**fetch** `url -> result<string, string>`\n\nFetches content from **url**. Returns `Ok body` or `Error msg`." },
        // Module function constructors (Size.*, FileMode.*, DateTime.*) are now
        // generated from the TypeRegistry via moduleFunctionStdLibCandidates().
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
    // clang-format off
    return {
        { "let", "let", "F# value/function binding",
            "**let** -- keyword\n\n```\nlet x = 42\nlet add x y = x + y\n```", CompletionKind::Keyword },
        { "rec", "rec", "Recursive function modifier",
            "**rec** -- keyword\n\n```\nlet rec fact n =\n  if n <= 1 then 1\n  else n * fact (n - 1)\n```", CompletionKind::Keyword },
        { "mut", "mut", "Mutable binding modifier",
            "**mut** -- keyword\n\n```\nlet mut x = 0\nx <- x + 1\n```", CompletionKind::Keyword },
        { "fun", "fun", "Lambda expression",
            "**fun** -- keyword\n\n```\nfun x -> x + 1\nfun x y -> x * y\n```", CompletionKind::Keyword },
        { "match", "match", "Pattern matching expression",
            "**match** -- keyword\n\n```\nmatch x with\n| Some v -> v\n| None -> 0\n```", CompletionKind::Keyword },
        { "with", "with", "Match arm separator",
            "**with** -- keyword\n\nSeparates the scrutinee from the match arms.", CompletionKind::Keyword },
        { "when", "when", "Pattern guard",
            "**when** -- keyword\n\n```\nmatch x with\n| n when n > 0 -> \"positive\"\n| _ -> \"other\"\n```", CompletionKind::Keyword },
        { "if", "if", "Conditional expression",
            "**if** -- keyword\n\n```\nif x > 0 then \"yes\" else \"no\"\n```", CompletionKind::Keyword },
        { "then", "then", "Then branch",
            "**then** -- keyword\n\nFollows the condition in an `if` expression.", CompletionKind::Keyword },
        { "else", "else", "Else branch",
            "**else** -- keyword\n\nAlternative branch in an `if` expression.", CompletionKind::Keyword },
        { "type", "type", "Type definition",
            "**type** -- keyword\n\n```\ntype Color = Red | Green | Blue\n```", CompletionKind::Keyword },
        { "of", "of", "Type constructor clause",
            "**of** -- keyword\n\nDeclares the payload type of a variant constructor.", CompletionKind::Keyword },
        { "try", "try", "Try expression",
            "**try** -- keyword\n\n```\ntry risky_op () with\n| Error e -> handle e\n```", CompletionKind::Keyword },
        { "finally", "finally", "Finally clause",
            "**finally** -- keyword\n\nCode that runs after try/with regardless of outcome.", CompletionKind::Keyword },
        { "true", "true", "Boolean literal",
            "**true** -- keyword\n\nBoolean true value.", CompletionKind::Keyword },
        { "false", "false", "Boolean literal",
            "**false** -- keyword\n\nBoolean false value.", CompletionKind::Keyword },
    };
    // clang-format on
}

std::vector<CompletionCandidate> builtinCandidates()
{
    auto const builtins = userFacingBuiltins();
    std::vector<CompletionCandidate> results;
    results.reserve(builtins.size());
    for (auto const& info: builtins)
    {
        results.push_back(CompletionCandidate {
            .text = info.name,
            .displayText = info.name,
            .description = info.description,
            .detail = info.detail,
            .kind = info.isProperty ? CompletionKind::Property : CompletionKind::Builtin,
        });
    }
    return results;
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
    static CoreVM::TypeRegistry const builtinRegistry;
    return constructorCandidatesFromRegistry(builtinRegistry);
}

std::vector<CompletionCandidate> dotAccessCandidates(
    std::string const& objectPart,
    std::string const& memberPrefix,
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> const& recordFields,
    std::unordered_map<std::string, std::string> const& variableTypes,
    std::string const& pipelineElementType,
    ModuleFunctionMap const& moduleFunctions)
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

        // Build detail from member info
        std::string detail = "**" + memberName + "** : `" + description + "`";

        results.push_back(CompletionCandidate {
            .text = completionText,
            .displayText = completionText,
            .description = description,
            .detail = std::move(detail),
            .kind = kind,
        });
    };

    /// @brief Helper to add module function candidates for a given type name.
    auto addModuleFunctions = [&](std::string const& typeName) -> bool {
        if (auto it = moduleFunctions.find(typeName); it != moduleFunctions.end())
        {
            for (auto const& fn: it->second)
                addCandidate(typeName + "." + fn.name, fn.name, fn.signature, CompletionKind::Function);
            return true;
        }
        return false;
    };

    // Check if objectPart is a known module type (Option, DateTime, Size, FileMode, etc.)
    if (moduleFunctions.contains(objectPart))
    {
        addModuleFunctions(objectPart);
    }
    else if (objectPart == "_")
    {
        if (!pipelineElementType.empty())
        {
            // Type-aware: only show fields from the pipeline element type
            if (auto it = recordFields.find(pipelineElementType); it != recordFields.end())
            {
                for (auto const& field: it->second)
                    addCandidate(
                        "_." + field.name, field.name, "field: " + field.typeName, CompletionKind::Field);
            }
        }
        else
        {
            // Fallback: show all record fields (no pipeline context)
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
        // Generic value: offer module functions for Option (most common wrapper type) and record fields
        if (auto it = moduleFunctions.find("Option"); it != moduleFunctions.end())
        {
            for (auto const& fn: it->second)
                addCandidate(objectPart + "." + fn.name, fn.name, fn.signature, CompletionKind::Function);
        }

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
            .detail = std::string(entry.detail),
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

        // Build detail from symbol info
        std::string detail;
        if (sym.isFunction)
        {
            detail = "**" + sym.name + "** `" + description + "`\n\nUser-defined function.";
        }
        else
        {
            detail = "**" + sym.name + "** -- " + description;
        }

        results.push_back(CompletionCandidate {
            .text = sym.name,
            .displayText = sym.name,
            .description = std::move(description),
            .detail = std::move(detail),
            .kind = kind,
        });
    }

    return results;
}

std::string resolvePipelineSourceType(std::string_view fullInput,
                                      std::unordered_map<std::string, std::string> const& commandOutputTypes)
{
    // Find the first |> operator
    auto const pipePos = fullInput.find("|>");
    if (pipePos == std::string_view::npos)
        return {};

    // Extract text before the first |> and trim whitespace
    auto source = fullInput.substr(0, pipePos);
    while (!source.empty() && (source.back() == ' ' || source.back() == '\t'))
        source.remove_suffix(1);
    while (!source.empty() && (source.front() == ' ' || source.front() == '\t'))
        source.remove_prefix(1);

    if (source.empty())
        return {};

    // Split into words
    std::vector<std::string_view> words;
    size_t pos = 0;
    while (pos < source.size())
    {
        while (pos < source.size() && (source[pos] == ' ' || source[pos] == '\t'))
            ++pos;
        if (pos >= source.size())
            break;
        auto const start = pos;
        while (pos < source.size() && source[pos] != ' ' && source[pos] != '\t')
            ++pos;
        words.push_back(source.substr(start, pos - start));
    }

    if (words.empty())
        return {};

    auto const commandName = std::string(words[0]);

    // Try direct command -> type lookup (handles single-word commands)
    if (auto it = commandOutputTypes.find(commandName); it != commandOutputTypes.end())
        return it->second;

    // Try NUL-separated key for multi-word commands (e.g., "docker\0ps")
    if (words.size() > 1)
    {
        auto key = commandName;
        for (size_t i = 1; i < words.size(); ++i)
        {
            key += '\0';
            key += std::string(words[i]);
        }
        if (auto it = commandOutputTypes.find(key); it != commandOutputTypes.end())
            return it->second;
    }

    return {};
}

} // namespace endo
