// SPDX-License-Identifier: Apache-2.0
#include "Completer.hpp"

#include <endo-language/AST.hpp>
#include <endo-language/CompletionCandidates.hpp>
#include <endo-language/CompletionContext.hpp>
#include <endo-language/Lexer.hpp>
#include <endo-language/Parser.hpp>
#include <endo-language/StubRuntime.hpp>
#include <endo-language/Type.hpp>

namespace endo
{

namespace
{
    /// @brief Checks if candidateText starts with prefix (case-sensitive).
    [[nodiscard]] bool matchesPrefix(std::string_view candidateText, std::string_view prefix)
    {
        if (prefix.empty())
            return true;
        return candidateText.size() >= prefix.size() && candidateText.substr(0, prefix.size()) == prefix;
    }

    /// @brief Filters candidates by prefix match.
    [[nodiscard]] std::vector<CompletionCandidate> filterByPrefix(std::vector<CompletionCandidate> candidates,
                                                                  std::string_view prefix)
    {
        if (prefix.empty())
            return candidates;

        std::vector<CompletionCandidate> filtered;
        for (auto& c: candidates)
        {
            if (matchesPrefix(c.text, prefix))
                filtered.push_back(std::move(c));
        }
        return filtered;
    }

    /// @brief Deduplicates candidates by text (keeps first occurrence).
    void deduplicateInto(std::vector<CompletionCandidate>& target, std::vector<CompletionCandidate> source)
    {
        for (auto& c: source)
        {
            bool isDuplicate = false;
            for (auto const& existing: target)
            {
                if (existing.text == c.text)
                {
                    isDuplicate = true;
                    break;
                }
            }
            if (!isDuplicate)
                target.push_back(std::move(c));
        }
    }
} // namespace

DocumentRecordInfo collectRecordInfo(std::string const& source)
{
    DocumentRecordInfo result;

    CoreVM::Runtime runtime;
    registerStubRuntime(runtime);

    CoreVM::diagnostics::BufferedReport report;
    Parser parser(runtime, report, std::make_unique<StringSource>(source));
    auto astRoot = parser.parse();
    if (!astRoot)
        return result;

    // Walk top-level statements
    auto const processStmt = [&](ast::Node const* stmt) {
        if (auto const* recordDef = dynamic_cast<ast::RecordTypeDefStmt const*>(stmt))
        {
            std::vector<RecordFieldInfo> fields;
            for (auto const& field: recordDef->fields)
                fields.push_back(RecordFieldInfo { .name = field.name, .typeName = toString(field.type) });
            result.recordFields[recordDef->name] = std::move(fields);
        }
        else if (auto const* letStmt = dynamic_cast<ast::LetBindingStmt const*>(stmt))
        {
            if (!letStmt->isFunction() && letStmt->value)
            {
                if (auto const* recordExpr = dynamic_cast<ast::RecordExpr const*>(letStmt->value.get()))
                {
                    if (!recordExpr->typeName.empty())
                        result.variableTypes[letStmt->name] = recordExpr->typeName;
                }
            }
        }
    };

    if (auto const* compound = dynamic_cast<ast::CompoundStmt const*>(astRoot.get()))
    {
        for (auto const& stmt: compound->statements)
            processStmt(stmt.get());
    }
    else
    {
        processStmt(astRoot.get());
    }

    return result;
}

std::vector<CompletionCandidate> computeCompletions(std::string_view source,
                                                    size_t cursorByteOffset,
                                                    CompletionDataSource const& dataSource)
{
    auto const ctx = CompletionContextAnalyzer::analyze(source, cursorByteOffset);
    auto const& prefix = ctx.prefix;

    std::vector<CompletionCandidate> results;

    // Check for dot-access pattern in prefix (highest priority)
    auto const dotPos = prefix.rfind('.');
    if (dotPos != std::string::npos && dotPos > 0
        && (ctx.type == CompletionContextType::Command || ctx.type == CompletionContextType::Argument))
    {
        auto const objectPart = prefix.substr(0, dotPos);
        auto const memberPrefix = prefix.substr(dotPos + 1);
        return dotAccessCandidates(
            objectPart, memberPrefix, dataSource.recordFields, dataSource.variableTypes);
    }

    switch (ctx.type)
    {
        case CompletionContextType::Command: {
            deduplicateInto(results, filterByPrefix(keywordCandidates(), prefix));
            deduplicateInto(results, filterByPrefix(builtinCandidates(), prefix));
            deduplicateInto(results, filterByPrefix(shellKeywordCandidates(), prefix));
            deduplicateInto(results, filterByPrefix(constructorCandidates(), prefix));
            deduplicateInto(results, filterByPrefix(symbolCandidates(dataSource.symbols), prefix));
            deduplicateInto(results, filterByPrefix(standardLibraryCandidates(), prefix));
            deduplicateInto(results, filterByPrefix(dataSource.additionalCandidates, prefix));
            break;
        }
        case CompletionContextType::Argument: {
            if (ctx.command.has_value() && isBuiltinWithArgumentCompletion(*ctx.command))
            {
                deduplicateInto(results, builtinArgumentCandidates(*ctx.command, prefix));
            }
            else
            {
                deduplicateInto(results, filterByPrefix(constructorCandidates(), prefix));
                deduplicateInto(results, filterByPrefix(symbolCandidates(dataSource.symbols), prefix));
                deduplicateInto(results, filterByPrefix(standardLibraryCandidates(), prefix));
            }
            deduplicateInto(results, filterByPrefix(dataSource.additionalCandidates, prefix));
            break;
        }
        case CompletionContextType::Variable:
        case CompletionContextType::VariableBrace:
        case CompletionContextType::FilePath:
        case CompletionContextType::Redirect:
        case CompletionContextType::Option: {
            deduplicateInto(results, filterByPrefix(dataSource.additionalCandidates, prefix));
            break;
        }
        case CompletionContextType::Unknown: break;
    }

    return results;
}

} // namespace endo
