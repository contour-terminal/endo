// SPDX-License-Identifier: Apache-2.0
#include "CompletionCandidates.hpp"

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
        // Environment/System
        StdLibEntry { "env", "env name -> string" },
        StdLibEntry { "rand", "rand min max -> int" },
        StdLibEntry { "fetch", "fetch url -> string" },
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
        { "set_prompt_preset", "set_prompt_preset", "Set prompt theme preset", "", CompletionKind::Builtin },
        { "set_prompt_indicator",
          "set_prompt_indicator",
          "Set prompt indicator character(s)",
          "",
          CompletionKind::Builtin },
        { "set_prompt_layout", "set_prompt_layout", "Set prompt layout style", "", CompletionKind::Builtin },
        { "set_prompt_separator",
          "set_prompt_separator",
          "Set prompt separator style",
          "",
          CompletionKind::Builtin },
        { "set_prompt_transient",
          "set_prompt_transient",
          "Set transient prompt mode",
          "",
          CompletionKind::Builtin },
        { "set_prompt_duration_threshold",
          "set_prompt_duration_threshold",
          "Set duration display threshold (ms)",
          "",
          CompletionKind::Builtin },
        { "set_prompt_spacing",
          "set_prompt_spacing",
          "Set blank lines above/below prompt (0 or 1)",
          "",
          CompletionKind::Builtin },
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

    if (objectPart == "Option")
    {
        // Static Option module methods
        for (auto const& method: optionMethods)
            addCandidate("Option." + std::string(method.name),
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
        "set_prompt_preset",    "set_prompt_indicator", "set_prompt_layout",
        "set_prompt_separator", "set_prompt_transient", "set_prompt_duration_threshold",
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

    if (commandName == "set_prompt_preset")
        return collectValues(presetValues);
    if (commandName == "set_prompt_layout")
        return collectValues(layoutValues);
    if (commandName == "set_prompt_separator")
        return collectValues(separatorValues);
    if (commandName == "set_prompt_transient")
        return collectValues(transientValues);

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
