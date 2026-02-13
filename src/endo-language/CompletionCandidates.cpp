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
    };
}

std::vector<CompletionCandidate> shellKeywordCandidates()
{
    return {
        { "if", "if", "Shell conditional", "", CompletionKind::Keyword },
        { "then", "then", "Shell then clause", "", CompletionKind::Keyword },
        { "else", "else", "Shell else clause", "", CompletionKind::Keyword },
        { "elif", "elif", "Shell elif clause", "", CompletionKind::Keyword },
        { "fi", "fi", "Shell end-if", "", CompletionKind::Keyword },
        { "for", "for", "Shell for loop", "", CompletionKind::Keyword },
        { "while", "while", "Shell while loop", "", CompletionKind::Keyword },
        { "do", "do", "Shell loop body", "", CompletionKind::Keyword },
        { "done", "done", "Shell end-loop", "", CompletionKind::Keyword },
        { "case", "case", "Shell case statement", "", CompletionKind::Keyword },
        { "esac", "esac", "Shell end-case", "", CompletionKind::Keyword },
        { "in", "in", "Shell in clause", "", CompletionKind::Keyword },
        { "function", "function", "Shell function definition", "", CompletionKind::Keyword },
        { "return", "return", "Shell return statement", "", CompletionKind::Keyword },
        { "break", "break", "Shell break statement", "", CompletionKind::Keyword },
        { "continue", "continue", "Shell continue statement", "", CompletionKind::Keyword },
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
