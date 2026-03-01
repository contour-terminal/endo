// SPDX-License-Identifier: Apache-2.0
#include <endo-language/builtins/BuiltinSignatures.hpp>
#include <endo-language/builtins/PropertyDescriptors.hpp>
#include <endo-language/builtins/StdlibDescriptors.hpp>
#include <endo-language/ide/CompletionCandidates.hpp>
#include <endo-language/ide/TypeRegistryCompletionAdapter.hpp>

#include <CoreVM/types/TypeRegistry.hpp>

#include <algorithm>
#include <set>

namespace endo
{

namespace
{
    /// @brief Helper to check if a name starts with a given prefix (case-sensitive).
    [[nodiscard]] bool startsWith(std::string_view name, std::string_view prefix)
    {
        if (prefix.empty())
            return true;
        return name.starts_with(prefix);
    }

    /// @brief Checks if text is a valid numeric prefix for a compound literal.
    /// Accepts digits with optional decimal point and underscore separators.
    [[nodiscard]] bool isNumericPrefix(std::string_view text)
    {
        if (text.empty())
            return false;
        bool hasDigit = false;
        bool hasDot = false;
        for (auto const ch: text)
        {
            if (ch >= '0' && ch <= '9')
                hasDigit = true;
            else if (ch == '.' && !hasDot)
                hasDot = true;
            else if (ch == '_')
                continue;
            else
                return false;
        }
        return hasDigit;
    }

    /// @brief Resolves a compound type literal string to its type name.
    /// Returns "Size" for size literals (e.g., "15.5MB", "100KB"),
    /// "TimeSpan" for timespan literals (e.g., "500ms", "3.5s"), or empty string.
    [[nodiscard]] std::string resolveCompoundLiteralType(std::string_view text)
    {
        // Size suffixes (check longest first to avoid "B" matching "KB")
        for (auto const* const suffix: { "TB", "GB", "MB", "KB", "B" })
        {
            auto const len = std::string_view(suffix).size();
            if (text.size() > len && text.ends_with(suffix)
                && isNumericPrefix(text.substr(0, text.size() - len)))
                return "Size";
        }
        // TimeSpan suffixes (check "min" before "ms" before "s")
        for (auto const* const suffix: { "min", "ms", "h", "s" })
        {
            auto const len = std::string_view(suffix).size();
            if (text.size() > len && text.ends_with(suffix)
                && isNumericPrefix(text.substr(0, text.size() - len)))
                return "TimeSpan";
        }
        return {};
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
        { "elif", "elif", "Else-if branch",
            "**elif** -- keyword\n\nElse-if branch in an `if` expression.\n\n```\nif x > 0 then \"pos\"\nelif x == 0 then \"zero\"\nelse \"neg\"\n```", CompletionKind::Keyword },
        { "type", "type", "Type definition",
            "**type** -- keyword\n\n```\ntype Color = Red | Green | Blue\n```", CompletionKind::Keyword },
        { "of", "of", "Type constructor clause",
            "**of** -- keyword\n\nDeclares the payload type of a variant constructor.", CompletionKind::Keyword },
        { "try", "try", "Try expression",
            "**try** -- keyword\n\n```\ntry risky_op () with\n| Error e -> handle e\n```", CompletionKind::Keyword },
        { "finally", "finally", "Finally clause",
            "**finally** -- keyword\n\nCode that runs after try/with regardless of outcome.", CompletionKind::Keyword },
        { "lazy", "lazy", "Lazy evaluation wrapper",
            "**lazy** -- keyword\n\nDefers evaluation until `force` is called.\n\n```\nlet x = lazy (1 + 2)\nprintln (force x)\n```", CompletionKind::Keyword },
        { "seq", "seq", "Lazy sequence builder",
            "**seq** -- keyword\n\nBuilds a lazy sequence with `yield` and `yield!`.\n\n```\nlet s = seq { yield 1; yield 2; yield! rest }\ns |> take 5 |> toList |> each println\n```", CompletionKind::Keyword },
        { "yield", "yield", "Sequence element producer",
            "**yield** -- keyword\n\nProduces a value in a `seq` expression.\nUse `yield!` to splice another sequence.\n\n```\nseq { yield 1; yield! rest }\n```", CompletionKind::Keyword },
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
    else if (auto const compoundType = resolveCompoundLiteralType(objectPart); !compoundType.empty())
    {
        // Compound type literal (e.g., "15.5MB" → Size, "500ms" → TimeSpan)
        if (auto const fieldsIt = recordFields.find(compoundType); fieldsIt != recordFields.end())
        {
            for (auto const& field: fieldsIt->second)
                addCandidate(objectPart + "." + field.name,
                             field.name,
                             compoundType + "." + field.name + ": " + field.typeName,
                             CompletionKind::Field);
        }
    }
    else if (objectPart.find('.') != std::string::npos)
    {
        // Nested dot access: e.g., "f.mtime" → resolve f → FileInfo, mtime → DateTime
        auto const firstDot = objectPart.find('.');
        auto const firstSegment = objectPart.substr(0, firstDot);
        auto const rest = objectPart.substr(firstDot + 1);

        // Resolve the first segment via pipelineElementType or variableTypes
        std::string currentType;
        if (firstSegment == "_" && !pipelineElementType.empty())
            currentType = pipelineElementType;
        else if (auto const it = variableTypes.find(firstSegment); it != variableTypes.end())
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
        // Don't offer dot-access for stdlib function names (ps, head, trim, etc.)
        // — they are function calls, not qualifiable identifiers.
        auto const isStdlibFunction = std::ranges::any_of(stdlibDescriptors(), [&](auto const& desc) {
            return !desc.userFacingName.empty() && desc.userFacingName == objectPart;
        });

        if (!isStdlibFunction)
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
    }

    return results;
}

bool isBuiltinWithArgumentCompletion(std::string const& commandName)
{
    for (auto const& desc: allPropertyDescriptors())
        if (desc.name == commandName && !desc.enumValues.empty())
            return true;
    return false;
}

std::vector<CompletionCandidate> builtinArgumentCandidates(std::string const& commandName,
                                                           std::string const& prefix)
{
    for (auto const& desc: allPropertyDescriptors())
    {
        if (desc.name == commandName && !desc.enumValues.empty())
        {
            std::vector<CompletionCandidate> results;
            for (auto const& entry: desc.enumValues)
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
        }
    }
    return {};
}

std::vector<CompletionCandidate> standardLibraryCandidates()
{
    std::vector<CompletionCandidate> results;
    for (auto const& desc: stdlibDescriptors())
    {
        if (desc.userFacingName.empty())
            continue;
        results.push_back(CompletionCandidate {
            .text = std::string(desc.userFacingName),
            .displayText = std::string(desc.userFacingName),
            .description = std::string(desc.description),
            .detail = std::string(desc.detail),
            .kind = CompletionKind::Function,
        });
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
