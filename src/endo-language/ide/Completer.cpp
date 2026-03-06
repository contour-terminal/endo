// SPDX-License-Identifier: Apache-2.0
#include <endo-language/ast/AST.hpp>
#include <endo-language/builtins/StubRuntime.hpp>
#include <endo-language/ide/Completer.hpp>
#include <endo-language/ide/CompletionCandidates.hpp>
#include <endo-language/ide/CompletionContext.hpp>
#include <endo-language/ide/TypeRegistryCompletionAdapter.hpp>
#include <endo-language/lexer/Lexer.hpp>
#include <endo-language/parser/Parser.hpp>
#include <endo-language/types/Type.hpp>

namespace endo
{

namespace
{
    /// @brief Checks if candidateText starts with prefix (case-sensitive).
    [[nodiscard]] bool matchesPrefix(std::string_view candidateText, std::string_view prefix)
    {
        if (prefix.empty())
            return true;
        return candidateText.starts_with(prefix);
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
            fields.reserve(recordDef->fields.size());
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
                else if (dynamic_cast<ast::SizeLiteralExpr const*>(letStmt->value.get()))
                {
                    result.variableTypes[letStmt->name] = "Size";
                }
                else if (dynamic_cast<ast::TimeSpanLiteralExpr const*>(letStmt->value.get()))
                {
                    result.variableTypes[letStmt->name] = "TimeSpan";
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

        // Resolve pipeline type for underscore patterns
        std::string pipelineType;
        if (objectPart == "_" || objectPart.starts_with("_."))
        {
            auto lineStart = source.rfind('\n', cursorByteOffset > 0 ? cursorByteOffset - 1 : 0);
            lineStart = (lineStart == std::string_view::npos) ? 0 : lineStart + 1;
            auto lineEnd = source.find('\n', cursorByteOffset);
            if (lineEnd == std::string_view::npos)
                lineEnd = source.size();
            pipelineType = resolvePipelineSourceType(source.substr(lineStart, lineEnd - lineStart),
                                                     dataSource.commandOutputTypes);
        }

        return dotAccessCandidates(objectPart,
                                   memberPrefix,
                                   dataSource.recordFields,
                                   dataSource.variableTypes,
                                   pipelineType,
                                   dataSource.moduleFunctions);
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
            {
                // Module function stdlib candidates (DateTime.now, Size.fromBytes, etc.)
                static CoreVM::TypeRegistry const builtinRegistry;
                deduplicateInto(results,
                                filterByPrefix(moduleFunctionStdLibCandidates(builtinRegistry), prefix));
            }
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
