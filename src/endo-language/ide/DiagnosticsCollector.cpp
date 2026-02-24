// SPDX-License-Identifier: Apache-2.0
#include <endo-language/ast/AST.hpp>
#include <endo-language/builtins/StubRuntime.hpp>
#include <endo-language/codegen/IRGenerator.hpp>
#include <endo-language/ide/DiagnosticsCollector.hpp>
#include <endo-language/lexer/Lexer.hpp>
#include <endo-language/parser/Parser.hpp>

#include <cstdlib>
#include <filesystem>
#include <format>
#include <set>
#include <string>
#include <unordered_set>

namespace endo
{

namespace
{

    /// @brief Known shell builtin command names.
    [[nodiscard]] std::set<std::string> const& builtinNames()
    {
        static auto const names = std::set<std::string> {
            "cat",
            "cd",
            "exit",
            "export",
            "set",
            "unset",
            "read",
            "sleep",
            "jobs",
            "fg",
            "bg",
            "wait",
            "bind",
            "which",
            "if",
            "then",
            "else",
            "elif",
            "for",
            "while",
            "do",
            "end",
            "in",
            "return",
            "break",
            "continue",
            "echo",
            "printf",
            "test",
            "source",
            ".",
            "exec",
            "eval",
            "shift",
            "trap",
            "type",
            "local",
            "declare",
            "typeset",
            "alias",
            "unalias",
            "command",
            "builtin",
            "hash",
            "let",
            "readonly",
            "select",
            "time",
            "until",
            "print",
            "println",
            "shell_prompt_preset",
            "shell_prompt_indicator",
            "shell_prompt_layout",
            "shell_prompt_separator",
            "shell_prompt_transient",
            "shell_prompt_duration_threshold",
            "shell_prompt_spacing",
            "shell_ls_icons",
            "shell_is_interactive",
        };
        return names;
    }

    /// @brief Checks if a command exists in PATH.
    [[nodiscard]] bool isInPath(std::string const& program)
    {
        auto const* pathEnv = std::getenv("PATH");
        if (!pathEnv)
            return false;

#if defined(_WIN32)
        constexpr char pathSeparator = ';';
#else
        constexpr char pathSeparator = ':';
#endif

        auto const pathStr = std::string_view(pathEnv);
        size_t start = 0;
        while (start < pathStr.size())
        {
            auto const end = pathStr.find(pathSeparator, start);
            auto const dir = pathStr.substr(start, end == std::string_view::npos ? end : end - start);

            if (!dir.empty())
            {
#if defined(_WIN32)
                // On Windows, check the bare name and common executable extensions.
                // Windows does not have Unix-style execute permission bits, so
                // file existence with a known extension is sufficient.
                static constexpr std::string_view extensions[] = { "", ".exe", ".cmd", ".bat" };
                for (auto const ext: extensions)
                {
                    auto const candidate = std::filesystem::path(dir) / std::string(program).append(ext);
                    std::error_code ec;
                    if (std::filesystem::exists(candidate, ec) && !ec)
                        return true;
                }
#else
                auto const candidate = std::filesystem::path(dir) / program;
                std::error_code ec;
                if (std::filesystem::exists(candidate, ec) && !ec)
                {
                    auto const status = std::filesystem::status(candidate, ec);
                    if (!ec
                        && (status.permissions() & std::filesystem::perms::owner_exec)
                               != std::filesystem::perms::none)
                    {
                        return true;
                    }
                }
#endif
            }

            if (end == std::string_view::npos)
                break;
            start = end + 1;
        }
        return false;
    }

    /// @brief Collects function names and ProgramCall nodes from the AST.
    struct CommandCollector
    {
        std::set<std::string> functionNames;
        std::vector<ast::ProgramCall const*> programCalls;

        void walkNode(ast::Node const& node)
        {
            // Statements
            if (auto const* compound = dynamic_cast<ast::CompoundStmt const*>(&node))
            {
                for (auto const& stmt: compound->statements)
                    walkNode(*stmt);
            }
            else if (auto const* pipeline = dynamic_cast<ast::CallPipeline const*>(&node))
            {
                for (auto const& call: pipeline->calls)
                    walkNode(*call);
            }
            else if (auto const* call = dynamic_cast<ast::ProgramCall const*>(&node))
            {
                programCalls.push_back(call);
            }
            else if (auto const* letStmt = dynamic_cast<ast::LetBindingStmt const*>(&node))
            {
                functionNames.insert(letStmt->name);
                for (auto const& andBinding: letStmt->andBindings)
                    functionNames.insert(andBinding.name);
                if (letStmt->value)
                    walkExpr(*letStmt->value);
            }
            else if (auto const* whileStmt = dynamic_cast<ast::WhileStmt const*>(&node))
            {
                if (whileStmt->condition)
                    walkExpr(*whileStmt->condition);
                if (whileStmt->body)
                    walkNode(*whileStmt->body);
            }
            else if (auto const* logAnd = dynamic_cast<ast::LogicalAndStmt const*>(&node))
            {
                if (logAnd->left)
                    walkNode(*logAnd->left);
                if (logAnd->right)
                    walkNode(*logAnd->right);
            }
            else if (auto const* logOr = dynamic_cast<ast::LogicalOrStmt const*>(&node))
            {
                if (logOr->left)
                    walkNode(*logOr->left);
                if (logOr->right)
                    walkNode(*logOr->right);
            }
            else if (auto const* exprStmt = dynamic_cast<ast::ExprStmt const*>(&node))
            {
                if (exprStmt->expr)
                    walkExpr(*exprStmt->expr);
            }
        }

        void walkExpr(ast::Expr const& expr)
        {
            if (auto const* shellCmd = dynamic_cast<ast::ShellCommandExpr const*>(&expr))
            {
                if (shellCmd->command)
                    walkNode(*shellCmd->command);
            }
            else if (auto const* subst = dynamic_cast<ast::SubstitutionExpr const*>(&expr))
            {
                if (subst->pipeline)
                    walkNode(*subst->pipeline);
            }
        }
    };

    /// @brief Converts a CoreVM SourceLocation (1-based) to a SourceRange (0-based).
    [[nodiscard]] SourceRange coreVmToSourceRange(CoreVM::SourceLocation const& loc)
    {
        auto const startLine = loc.begin.line > 0 ? static_cast<int>(loc.begin.line) - 1 : 0;
        auto const startCol = loc.begin.column > 0 ? static_cast<int>(loc.begin.column) - 1 : 0;
        auto const endLine = loc.end.line > 0 ? static_cast<int>(loc.end.line) - 1 : startLine;
        auto const endCol = loc.end.column > 0 ? static_cast<int>(loc.end.column) - 1 : startCol + 1;

        return SourceRange {
            .start = SourcePosition { .line = startLine, .character = startCol },
            .end = SourcePosition { .line = endLine, .character = endCol },
        };
    }

    /// @brief Converts a SourceLocationRange to a SourceRange.
    /// Both use 0-based line and column indices.
    [[nodiscard]] SourceRange lexerToSourceRange(SourceLocationRange const& loc)
    {
        return SourceRange {
            .start = SourcePosition { .line = loc.begin.line, .character = loc.begin.column },
            .end = SourcePosition { .line = loc.end.line, .character = loc.end.column },
        };
    }

} // namespace

std::vector<DiagnosticMessage> collectDiagnostics(std::string const& source,
                                                  std::set<std::string> const& knownNames)
{
    CoreVM::Runtime runtime;
    registerStubRuntime(runtime);

    CoreVM::diagnostics::BufferedReport report;
    Parser parser(runtime, report, std::make_unique<StringSource>(source));

    // Inform the parser about externally known F# names so that calls to them
    // are parsed as F# applications (ExprStmt) instead of ProgramCall nodes.
    if (!knownNames.empty())
    {
        auto names = std::unordered_set<std::string>(knownNames.begin(), knownNames.end());
        parser.setKnownFSharpFunctions(std::move(names));
    }

    auto ast = parser.parse();

    std::vector<DiagnosticMessage> diagnostics;

    // Collect parse errors
    for (auto const& msg: report.messages())
    {
        auto severity = DiagnosticSeverity::Error;
        using Type = CoreVM::diagnostics::Type;
        switch (msg.type)
        {
            case Type::Warning: severity = DiagnosticSeverity::Warning; break;
            case Type::LinkError: [[fallthrough]];
            case Type::TypeError: severity = DiagnosticSeverity::Error; break;
            default: break;
        }

        diagnostics.push_back(DiagnosticMessage {
            .range = coreVmToSourceRange(msg.sourceLocation),
            .severity = severity,
            .message = msg.text,
            .suggestions = msg.suggestions,
        });
    }

    // Run IR generation to detect type errors with suggestions (e.g., unwrapped Option/Result).
    // Only propagate errors that have suggestions attached — this avoids false positives from
    // shell builtins and identifiers not registered in the stub runtime.
    if (ast && !report.containsFailures())
    {
        CoreVM::diagnostics::BufferedReport irReport;
        IRGenerator::generate(*ast, irReport, runtime, nullptr);
        for (auto const& msg: irReport.messages())
        {
            if (msg.suggestions.empty())
                continue;

            // Suppress "Undefined variable" false positives for names persisted from prior REPL prompts
            if (auto constexpr prefix = std::string_view("Undefined variable: ");
                msg.text.starts_with(prefix)
                && knownNames.contains(std::string(msg.text.substr(prefix.size()))))
                continue;

            auto severity = DiagnosticSeverity::Error;
            using Type = CoreVM::diagnostics::Type;
            switch (msg.type)
            {
                case Type::Warning: severity = DiagnosticSeverity::Warning; break;
                case Type::TypeError: [[fallthrough]];
                case Type::LinkError: severity = DiagnosticSeverity::Error; break;
                default: break;
            }

            diagnostics.push_back(DiagnosticMessage {
                .range = coreVmToSourceRange(msg.sourceLocation),
                .severity = severity,
                .message = msg.text,
                .suggestions = msg.suggestions,
            });
        }
    }

    // Walk the AST to find unknown commands (only if parse succeeded)
    if (ast)
    {
        CommandCollector collector;
        collector.walkNode(*ast);

        auto const& builtins = builtinNames();

        for (auto const* call: collector.programCalls)
        {
            auto const& program = call->program;

            // Skip commands with explicit paths (e.g., ./script, /usr/bin/ls)
            if (program.find('/') != std::string::npos)
                continue;

            // Skip if it's a known builtin
            if (builtins.contains(program))
                continue;

            // Skip if it's a defined function (shell or F#)
            if (collector.functionNames.contains(program))
                continue;

            // Skip if it's a known external name (persisted F# function/binding from prior prompts)
            if (knownNames.contains(program))
                continue;

            // Skip if found in PATH
            if (isInPath(program))
                continue;

            // Unknown command — emit diagnostic
            if (call->programLocation.has_value())
            {
                diagnostics.push_back(DiagnosticMessage {
                    .range = lexerToSourceRange(*call->programLocation),
                    .severity = DiagnosticSeverity::Error,
                    .message = std::format("command not found: {}", program),
                });
            }
        }
    }

    return diagnostics;
}

} // namespace endo
