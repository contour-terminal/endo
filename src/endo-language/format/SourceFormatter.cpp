// SPDX-License-Identifier: Apache-2.0
#include <endo-language/ast/ASTPrinter.hpp>
#include <endo-language/builtins/StubRuntime.hpp>
#include <endo-language/format/SourceFormatter.hpp>
#include <endo-language/parser/Parser.hpp>
#include <endo-language/types/Type.hpp>

#include <algorithm>
#include <format>
#include <ranges>

namespace endo::format
{

namespace
{
    /// Re-escapes special characters in a string for source output.
    /// The lexer unescapes `\n` → actual newline during parsing; this reverses that.
    /// Non-printable characters are escaped as `\xHH` hex sequences.
    std::string escapeString(std::string_view input)
    {
        std::string result;
        result.reserve(input.size());
        for (auto const ch: input)
        {
            switch (ch)
            {
                case '\n': result += "\\n"; break;
                case '\t': result += "\\t"; break;
                case '\r': result += "\\r"; break;
                case '\0': result += "\\0"; break;
                case '\a': result += "\\a"; break;
                case '\b': result += "\\b"; break;
                case '\f': result += "\\f"; break;
                case '\v': result += "\\v"; break;
                case '\\': result += "\\\\"; break;
                case '"': result += "\\\""; break;
                default:
                    if (static_cast<unsigned char>(ch) < 0x20 || static_cast<unsigned char>(ch) == 0x7F)
                        result += std::format("\\x{:02x}", static_cast<unsigned char>(ch));
                    else
                        result += ch;
                    break;
            }
        }
        return result;
    }
} // namespace

// ============================================================================
// Construction and static entry points
// ============================================================================

/// Scans source text and returns the set of 0-based line numbers that are blank
/// (empty or whitespace-only).
static std::set<int> computeBlankLines(std::string const& source)
{
    std::set<int> result;
    int line = 0;
    size_t pos = 0;
    while (pos < source.size())
    {
        auto const eol = source.find('\n', pos);
        auto const end = (eol == std::string::npos) ? source.size() : eol;
        auto const lineContent = std::string_view(source).substr(pos, end - pos);
        auto const isBlank =
            lineContent.empty() || lineContent.find_first_not_of(" \t\r") == std::string_view::npos;
        if (isBlank)
            result.insert(line);
        ++line;
        pos = (eol == std::string::npos) ? source.size() : eol + 1;
    }
    return result;
}

bool SourceFormatter::hasBlankLineBetween(int afterLine, int beforeLine) const
{
    // Check if any blank line exists in the exclusive range (afterLine, beforeLine)
    auto const it = _blankLines.lower_bound(afterLine + 1);
    return it != _blankLines.end() && *it < beforeLine;
}

SourceFormatter::SourceFormatter(FormatConfig config,
                                 std::vector<CommentTrivia> const& comments,
                                 std::set<int> blankLines):
    _config(config), _comments(comments), _blankLines(std::move(blankLines))
{
}

std::string SourceFormatter::format(std::string const& source, FormatConfig const& config)
{
    // 1. Lex to collect comments (flag must be set before construction)
    auto commentLexer = Lexer(std::make_unique<StringSource>(source), /*collectComments=*/true);
    while (commentLexer.currentToken() != Token::EndOfInput)
        commentLexer.nextToken();

    auto const& comments = commentLexer.comments();

    // 2. Parse AST (requires a stub runtime for builtin resolution)
    CoreVM::Runtime runtime;
    registerStubRuntime(runtime);
    CoreVM::diagnostics::BufferedReport report;
    auto parser = Parser(runtime, report, std::make_unique<StringSource>(source));
    auto program = parser.parse();
    if (!program || report.containsFailures())
        return source; // Parse failed — return original source unchanged

    // 3. Format
    return format(*program, comments, config, computeBlankLines(source));
}

std::string SourceFormatter::format(ast::Node const& root,
                                    std::vector<CommentTrivia> const& comments,
                                    FormatConfig const& config,
                                    std::set<int> blankLines)
{
    SourceFormatter formatter(config, comments, std::move(blankLines));
    root.accept(formatter);
    formatter.emitRemainingComments();

    if (config.trailingNewline && !formatter._result.empty() && formatter._result.back() != '\n')
        formatter._result += '\n';

    return formatter._result;
}

// ============================================================================
// Output helpers
// ============================================================================

void SourceFormatter::emit(std::string_view text)
{
    if (_atLineStart && !text.empty())
    {
        for ([[maybe_unused]] auto _: std::views::iota(0, _indentLevel))
            _result += _config.indentString();
        _atLineStart = false;
    }
    _result += text;
}

void SourceFormatter::emitNewline()
{
    _result += '\n';
    _atLineStart = true;
}

void SourceFormatter::emitIndent()
{
    if (_atLineStart)
    {
        for ([[maybe_unused]] auto _: std::views::iota(0, _indentLevel))
            _result += _config.indentString();
        _atLineStart = false;
    }
}

void SourceFormatter::emitSpace()
{
    if (!_result.empty() && _result.back() != ' ' && _result.back() != '\n')
        _result += ' ';
}

void SourceFormatter::emitBlankLine()
{
    // Emit up to the configured number of blank lines
    emitNewline();
}

void SourceFormatter::indent()
{
    ++_indentLevel;
}

void SourceFormatter::dedent()
{
    if (_indentLevel > 0)
        --_indentLevel;
}

// ============================================================================
// Comment interleaving
// ============================================================================

std::optional<int> SourceFormatter::findFirstLine(ast::Node const& node)
{
    if (node.location)
        return node.location->begin.line;

    // CompoundStmt: check first child
    if (auto const* cs = dynamic_cast<ast::CompoundStmt const*>(&node))
    {
        for (auto const& stmt: cs->statements)
            if (auto line = findFirstLine(*stmt))
                return line;
    }

    // LetBindingStmt: check value expression
    if (auto const* let = dynamic_cast<ast::LetBindingStmt const*>(&node))
    {
        if (let->value)
            if (auto line = findFirstLine(*let->value))
                return line;
    }

    // ExprStmt: check expression
    if (auto const* es = dynamic_cast<ast::ExprStmt const*>(&node))
    {
        if (es->expr)
            if (auto line = findFirstLine(*es->expr))
                return line;
    }

    // ProgramCall: check programLocation
    if (auto const* pc = dynamic_cast<ast::ProgramCall const*>(&node))
    {
        if (pc->programLocation)
            return pc->programLocation->begin.line;
    }

    // DataSourceExpr: check filePath or pipeSource
    if (auto const* ds = dynamic_cast<ast::DataSourceExpr const*>(&node))
    {
        if (ds->filePath)
            if (auto line = findFirstLine(*ds->filePath))
                return line;
        if (ds->pipeSource)
            if (auto line = findFirstLine(*ds->pipeSource))
                return line;
    }

    // CallPipeline: check first call
    if (auto const* cp = dynamic_cast<ast::CallPipeline const*>(&node))
    {
        if (!cp->calls.empty())
            if (auto line = findFirstLine(*cp->calls.front()))
                return line;
    }

    // ForInStmt: check source expression (pattern has no location)
    if (auto const* fi = dynamic_cast<ast::ForInStmt const*>(&node))
    {
        if (fi->source)
            if (auto line = findFirstLine(*fi->source))
                return line;
    }

    // WhileStmt: check condition expression
    if (auto const* ws = dynamic_cast<ast::WhileStmt const*>(&node))
    {
        if (ws->condition)
            if (auto line = findFirstLine(*ws->condition))
                return line;
    }

    // ShellCommandExpr: check wrapped command
    if (auto const* sc = dynamic_cast<ast::ShellCommandExpr const*>(&node))
    {
        if (sc->command)
            if (auto line = findFirstLine(*sc->command))
                return line;
    }

    return std::nullopt;
}

void SourceFormatter::emitLeadingComments(ast::Node const& node)
{
    auto const lineOpt = findFirstLine(node);
    if (!lineOpt)
        return;

    emitCommentsBeforeLine(*lineOpt);
}

void SourceFormatter::emitCommentsBeforeLine(int line)
{
    while (_nextCommentIndex < _comments.size())
    {
        auto const& comment = _comments[_nextCommentIndex];
        if (comment.location.begin.line >= line)
            break;
        if (comment.isTrailing)
        {
            // Trailing comments are emitted after the preceding node
            ++_nextCommentIndex;
            continue;
        }
        emit(comment.text);
        emitNewline();

        // The comment's end.line points to the line AFTER the comment text
        // (because the newline terminating the comment advances the line counter).
        auto const commentEndLine = comment.location.end.line;
        ++_nextCommentIndex;

        // Determine the line of whatever comes next (next leading comment or the code)
        auto nextContentLine = line;
        for (auto const i: std::views::iota(_nextCommentIndex, _comments.size()))
        {
            if (_comments[i].location.begin.line >= line)
                break;
            if (!_comments[i].isTrailing)
            {
                nextContentLine = _comments[i].location.begin.line;
                break;
            }
        }

        // Preserve blank lines from the original source (capped by config).
        // No extra -1 needed: commentEndLine already points past the comment's newline.
        auto const gap = nextContentLine - commentEndLine;
        auto const maxBlanks = static_cast<int>(_config.blankLinesBetweenTopLevel);
        for ([[maybe_unused]] auto _: std::views::iota(0, std::min(gap, maxBlanks)))
            emitNewline();
    }
}

std::optional<int> SourceFormatter::findLastLine(ast::Node const& node)
{
    if (node.location)
        return node.location->end.line;

    // CompoundStmt: check last child
    if (auto const* cs = dynamic_cast<ast::CompoundStmt const*>(&node))
    {
        for (const auto& statement: std::ranges::reverse_view(cs->statements))
            if (auto line = findLastLine(*statement))
                return line;
    }

    // LetBindingStmt: check value expression
    if (auto const* let = dynamic_cast<ast::LetBindingStmt const*>(&node))
    {
        if (let->value)
            if (auto line = findLastLine(*let->value))
                return line;
    }

    // ExprStmt: check expression
    if (auto const* es = dynamic_cast<ast::ExprStmt const*>(&node))
    {
        if (es->expr)
            if (auto line = findLastLine(*es->expr))
                return line;
    }

    // ProgramCall: check programLocation
    if (auto const* pc = dynamic_cast<ast::ProgramCall const*>(&node))
    {
        if (pc->programLocation)
            return pc->programLocation->end.line;
    }

    // CallPipeline: check last call
    if (auto const* cp = dynamic_cast<ast::CallPipeline const*>(&node))
    {
        for (const auto& call: std::ranges::reverse_view(cp->calls))
            if (auto line = findLastLine(*call))
                return line;
    }

    return std::nullopt;
}

void SourceFormatter::emitInlineComments(int line, int beforeColumn)
{
    while (_nextCommentIndex < _comments.size())
    {
        auto const& comment = _comments[_nextCommentIndex];
        if (comment.location.begin.line != line)
            break;
        if (comment.location.begin.column >= beforeColumn)
            break;
        // This is an inline comment on the same line, before the value expression
        emit(comment.text);
        emit(" ");
        ++_nextCommentIndex;
    }
}

void SourceFormatter::emitTrailingComment(ast::Node const& node)
{
    auto const lineOpt = findLastLine(node);
    if (!lineOpt)
        return;

    auto const nodeLine = *lineOpt;

    if (_nextCommentIndex < _comments.size())
    {
        auto const& comment = _comments[_nextCommentIndex];
        if (comment.isTrailing && comment.location.begin.line == nodeLine)
        {
            emit(" ");
            emit(comment.text);
            ++_nextCommentIndex;
        }
    }
}

void SourceFormatter::emitRemainingComments()
{
    while (_nextCommentIndex < _comments.size())
    {
        auto const& comment = _comments[_nextCommentIndex];
        if (!_atLineStart)
            emitNewline();
        emit(comment.text);
        emitNewline();
        ++_nextCommentIndex;
    }
}

void SourceFormatter::emitDanglingBodyComments(ast::Node const& blockParent)
{
    if (!blockParent.location)
        return;

    auto const endLine = blockParent.location->end.line;

    while (_nextCommentIndex < _comments.size())
    {
        auto const& comment = _comments[_nextCommentIndex];
        if (comment.location.begin.line >= endLine)
            break;
        if (comment.isTrailing)
        {
            ++_nextCommentIndex;
            continue;
        }
        emitNewline();
        emit(comment.text);
        ++_nextCommentIndex;
    }
}

// ============================================================================
// Width estimation
// ============================================================================

size_t SourceFormatter::estimateWidth(ast::Node const& node)
{
    return ast::ASTPrinter::print(node).size();
}

// ============================================================================
// Pattern printing helper
// ============================================================================

void SourceFormatter::emitPattern(pattern::Pattern const& pat)
{
    emit(pattern::toString(pat));
}

void SourceFormatter::emitParameters(std::vector<ast::TypedParameter> const& params)
{
    for (auto const& param: params)
    {
        emit(" ");
        if (param.isUnit)
            emit("()");
        else if (param.typeAnnotation)
            emit("(" + param.name + ": " + endo::toString(*param.typeAnnotation) + ")");
        else
        {
            if (param.isVariadic)
                emit("...");
            emit(param.name);
        }
    }
}

bool SourceFormatter::isCompoundExpr(ast::Expr const& expr)
{
    // Unwrap parenthesized expressions
    if (auto const* paren = dynamic_cast<ast::ParenExpr const*>(&expr))
        return paren->inner ? isCompoundExpr(*paren->inner) : false;

    return dynamic_cast<ast::IfExpr const*>(&expr) || dynamic_cast<ast::MatchExpr const*>(&expr)
           || dynamic_cast<ast::BlockExpr const*>(&expr) || dynamic_cast<ast::LambdaExpr const*>(&expr)
           || dynamic_cast<ast::LetInExpr const*>(&expr) || dynamic_cast<ast::TryWithExpr const*>(&expr)
           || dynamic_cast<ast::TryFinallyExpr const*>(&expr);
}

bool SourceFormatter::wouldFormatMultiline(ast::Expr const& expr) const
{
    if (auto const* paren = dynamic_cast<ast::ParenExpr const*>(&expr))
        return paren->inner ? wouldFormatMultiline(*paren->inner) : false;

    if (auto const* ifExpr = dynamic_cast<ast::IfExpr const*>(&expr))
    {
        if (ifExpr->elseExpr)
            return true; // if-then-else always at least 2 lines
        return isCompoundExpr(*ifExpr->thenExpr) || estimateWidth(expr) > _config.maxLineWidth;
    }

    // Match/TryWith/TryFinally/Block: always multiline
    if (dynamic_cast<ast::MatchExpr const*>(&expr))
        return true;
    if (dynamic_cast<ast::TryWithExpr const*>(&expr))
        return true;
    if (dynamic_cast<ast::TryFinallyExpr const*>(&expr))
        return true;
    if (dynamic_cast<ast::BlockExpr const*>(&expr))
        return true;

    // Lambda: multiline when body is compound
    if (auto const* lambda = dynamic_cast<ast::LambdaExpr const*>(&expr))
        return lambda->body && isCompoundExpr(*lambda->body);

    // LetIn: multiline when body is compound
    if (auto const* letIn = dynamic_cast<ast::LetInExpr const*>(&expr))
        return letIn->body && isCompoundExpr(*letIn->body);

    // Pipeline with 2+ operators always wraps
    if (auto const* pipeline = dynamic_cast<ast::PipelineExpr const*>(&expr))
    {
        auto const chain = collectPipelineChain(*pipeline);
        if (chain.size() > 2)
            return true;
        return estimateWidth(expr) > _config.maxLineWidth;
    }

    // ConcatList with 2+ operators always wraps
    if (auto const* concat = dynamic_cast<ast::ConcatListExpr const*>(&expr))
    {
        auto const chain = collectConcatChain(*concat);
        if (chain.size() > 2)
            return true;
        return estimateWidth(expr) > _config.maxLineWidth;
    }

    // List/Tuple that would exceed line width
    if (dynamic_cast<ast::ListExpr const*>(&expr) || dynamic_cast<ast::TupleExpr const*>(&expr))
        return estimateWidth(expr) > _config.maxLineWidth;

    return false;
}

// ============================================================================
// Current column tracking
// ============================================================================

size_t SourceFormatter::currentColumn() const
{
    if (_atLineStart)
        return static_cast<size_t>(_indentLevel) * _config.indentWidth;

    auto const lastNewline = _result.rfind('\n');
    if (lastNewline == std::string::npos)
        return _result.size();
    return _result.size() - lastNewline - 1;
}

// ============================================================================
// Chain-flattening helpers
// ============================================================================

std::vector<ast::Expr const*> SourceFormatter::collectPipelineChain(ast::PipelineExpr const& node)
{
    std::vector<ast::Expr const*> chain;
    auto const* current = &node;
    while (current)
    {
        chain.push_back(current->function.get());
        if (auto const* inner = dynamic_cast<ast::PipelineExpr const*>(current->value.get()))
            current = inner;
        else
        {
            chain.push_back(current->value.get());
            break;
        }
    }
    std::ranges::reverse(chain);
    return chain; // [source, step1, step2, ...]
}

std::vector<ast::Expr const*> SourceFormatter::collectConcatChain(ast::ConcatListExpr const& node)
{
    std::vector<ast::Expr const*> chain;
    chain.push_back(node.left.get());
    auto const* current = node.right.get();
    while (auto const* inner = dynamic_cast<ast::ConcatListExpr const*>(current))
    {
        chain.push_back(inner->left.get());
        current = inner->right.get();
    }
    chain.push_back(current);
    return chain; // [list1, list2, list3, ...]
}

// ============================================================================
// Adaptive wrapping helpers
// ============================================================================

bool SourceFormatter::hasComplexElement(std::vector<std::unique_ptr<ast::Expr>> const& elements) const
{
    return std::ranges::any_of(elements, [this](auto const& elem) {
        return elem && (isCompoundExpr(*elem) || wouldFormatMultiline(*elem));
    });
}

void SourceFormatter::emitWrappedElements(std::vector<std::unique_ptr<ast::Expr>> const& elements,
                                          std::string_view separator,
                                          std::string_view open,
                                          std::string_view close)
{
    auto const forceOnePerLine = hasComplexElement(elements);
    emitWrappedWith(
        elements.size(),
        separator,
        open,
        close,
        [&](size_t i) {
            if (elements[i])
                elements[i]->accept(*this);
        },
        [&](size_t i) -> size_t { return elements[i] ? estimateWidth(*elements[i]) : 0; },
        forceOnePerLine);
}

void SourceFormatter::emitWrappedWith(size_t count,
                                      std::string_view separator,
                                      std::string_view open,
                                      std::string_view close,
                                      std::function<void(size_t)> const& emitItem,
                                      std::function<size_t(size_t)> const& estimateItem,
                                      bool forceOnePerLine)
{
    if (count == 0)
    {
        emit(open);
        emit(close);
        return;
    }

    // Estimate total inline width
    auto totalInlineWidth = currentColumn() + open.size() + close.size();
    for (auto const i: std::views::iota(0uz, count))
    {
        if (i > 0)
            totalInlineWidth += separator.size();
        totalInlineWidth += estimateItem(i);
    }

    // Inline: everything fits on one line
    if (totalInlineWidth <= _config.maxLineWidth)
    {
        emit(open);
        for (auto const i: std::views::iota(0uz, count))
        {
            if (i > 0)
                emit(separator);
            emitItem(i);
        }
        emit(close);
        return;
    }

    // Wrapped mode: separator core (without trailing space) for end-of-line placement
    auto const sepCore = separator.ends_with(' ') ? separator.substr(0, separator.size() - 1) : separator;

    emit(open);
    emitNewline();
    indent();

    for (auto const i: std::views::iota(0uz, count))
    {
        if (i > 0)
        {
            emit(sepCore);
            if (forceOnePerLine)
            {
                // One element per line for complex elements
                emitNewline();
            }
            else
            {
                // Bin-pack: check if next item fits on current line
                auto const nextWidth = estimateItem(i);
                auto const widthAfterItem = currentColumn() + 1 /* space */ + nextWidth
                                            + (i + 1 < count ? separator.size() : close.size());
                if (widthAfterItem > _config.maxLineWidth)
                    emitNewline();
                else
                    emit(" ");
            }
        }
        emitItem(i);
    }
    emitNewline();
    dedent();
    emit(close);
}

// ============================================================================
// Shell construct visitors
// ============================================================================

void SourceFormatter::visit(ast::FileDescriptor const& node)
{
    emit(std::format("{}", node.value));
}

void SourceFormatter::visit(ast::InputRedirect const& node)
{
    if (node.targetFd->value != 0)
        emit(std::format(" {}<", node.targetFd->value));
    else
        emit(" <");
    node.source->accept(*this);
}

void SourceFormatter::visit(ast::OutputRedirect const& node)
{
    if (std::holds_alternative<std::unique_ptr<ast::FileDescriptor>>(node.target))
    {
        emit(std::format(" {}>&{}",
                         node.source->value,
                         std::get<std::unique_ptr<ast::FileDescriptor>>(node.target)->value));
    }
    else
    {
        if (node.source->value != 1)
            emit(std::format(" {}", node.source->value));
        else
            emit(" ");
        emit(node.append ? ">>" : ">");
        std::get<std::unique_ptr<ast::Expr>>(node.target)->accept(*this);
    }
}

void SourceFormatter::visit(ast::HereDocument const& node)
{
    if (node.targetFd->value != 0)
        emit(std::format(" {}", node.targetFd->value));
    emit(node.stripTabs ? "<<-" : "<<");
    emit(node.delimiter);
}

void SourceFormatter::visit(ast::HereString const& node)
{
    if (node.targetFd->value != 0)
        emit(std::format(" {}", node.targetFd->value));
    emit("<<<");
    node.content->accept(*this);
}

void SourceFormatter::visit(ast::ProgramCall const& node)
{
    emitLeadingComments(node);
    emit(node.program);

    for (auto const& param: node.parameters)
    {
        emit(" ");
        param->accept(*this);
    }

    for (auto const& redirect: node.inputRedirects)
        redirect->accept(*this);

    for (auto const& redirect: node.outputRedirects)
        redirect->accept(*this);

    for (auto const& heredoc: node.hereDocuments)
        heredoc->accept(*this);

    for (auto const& herestring: node.hereStrings)
        herestring->accept(*this);

    emitTrailingComment(node);
}

void SourceFormatter::visit(ast::CallPipeline const& node)
{
    emitLeadingComments(node);
    for (auto const i: std::views::iota(0uz, node.calls.size()))
    {
        if (i > 0)
            emit(" | ");
        node.calls[i]->accept(*this);
    }
    emitTrailingComment(node);
}

/// Checks whether a statement is a declaration or block construct that warrants
/// blank line separation from adjacent statements. Expression-like statements
/// (calls, shell commands, assignments) can be grouped without blank lines.
static bool isDeclarationOrBlock(ast::Node const& node)
{
    return dynamic_cast<ast::LetBindingStmt const*>(&node) != nullptr
           || dynamic_cast<ast::RecordTypeDefStmt const*>(&node) != nullptr
           || dynamic_cast<ast::UnionTypeDefStmt const*>(&node) != nullptr
           || dynamic_cast<ast::WhileStmt const*>(&node) != nullptr
           || dynamic_cast<ast::ForInStmt const*>(&node) != nullptr;
}

void SourceFormatter::visit(ast::CompoundStmt const& node)
{
    emitLeadingComments(node);
    for (auto const i: std::views::iota(0uz, node.statements.size()))
    {
        if (i > 0)
        {
            emitNewline();
            auto wantBlankLine = false;

            // (A) Existing heuristic: declarations/blocks at top level always get blank lines
            if (_indentLevel == 0
                && (isDeclarationOrBlock(*node.statements[i - 1])
                    || isDeclarationOrBlock(*node.statements[i])))
                wantBlankLine = true;

            // (B) Preserve user-authored blank lines at any indent level
            if (!wantBlankLine && !_blankLines.empty())
            {
                auto const prevStart = findFirstLine(*node.statements[i - 1]);
                auto const nextStart = findFirstLine(*node.statements[i]);
                if (prevStart && nextStart)
                    wantBlankLine = hasBlankLineBetween(*prevStart, *nextStart);
            }

            if (wantBlankLine)
                for ([[maybe_unused]] auto _:
                     std::views::iota(uint32_t { 0 }, _config.blankLinesBetweenTopLevel))
                    emitNewline();
        }
        node.statements[i]->accept(*this);
    }
    emitTrailingComment(node);
}

void SourceFormatter::visit(ast::WhileStmt const& node)
{
    emitLeadingComments(node);
    emit("while ");
    node.condition->accept(*this);
    emit(" do");
    emitNewline();
    indent();
    node.body->accept(*this);
    emitDanglingBodyComments(node);
    dedent();
    emitTrailingComment(node);
}

void SourceFormatter::visit(ast::ForInStmt const& node)
{
    emitLeadingComments(node);
    emit("for ");
    emitPattern(*node.pattern);
    emit(" in ");
    node.source->accept(*this);
    emit(" do");
    emitNewline();
    indent();
    node.body->accept(*this);
    emitDanglingBodyComments(node);
    dedent();
    emitTrailingComment(node);
}

void SourceFormatter::visit(ast::BreakStmt const& node)
{
    emitLeadingComments(node);
    emit("break");
    if (node.levels > 1)
        emit(" " + std::to_string(node.levels));
    emitTrailingComment(node);
}

void SourceFormatter::visit(ast::ContinueStmt const& node)
{
    emitLeadingComments(node);
    emit("continue");
    if (node.levels > 1)
        emit(" " + std::to_string(node.levels));
    emitTrailingComment(node);
}

void SourceFormatter::visit(ast::LogicalAndStmt const& node)
{
    emitLeadingComments(node);
    node.left->accept(*this);
    emit(" && ");
    node.right->accept(*this);
    emitTrailingComment(node);
}

void SourceFormatter::visit(ast::LogicalOrStmt const& node)
{
    emitLeadingComments(node);
    node.left->accept(*this);
    emit(" || ");
    node.right->accept(*this);
    emitTrailingComment(node);
}

void SourceFormatter::visit(ast::BuiltinExitStmt const& node)
{
    emitLeadingComments(node);
    emit("exit");
    if (node.code)
    {
        emit(" ");
        node.code->accept(*this);
    }
    emitTrailingComment(node);
}

void SourceFormatter::visit(ast::BuiltinExportStmt const& node)
{
    emitLeadingComments(node);
    emit("export ");
    emit(node.name);
    emitTrailingComment(node);
}

void SourceFormatter::visit(ast::BuiltinReadStmt const& node)
{
    emitLeadingComments(node);
    emit("read");
    for (auto const& param: node.parameters)
    {
        emit(" ");
        param->accept(*this);
    }
    emitTrailingComment(node);
}

void SourceFormatter::visit(ast::BuiltinChDirStmt const& node)
{
    emitLeadingComments(node);
    emit("cd");
    if (node.path)
    {
        emit(" ");
        node.path->accept(*this);
    }
    emitTrailingComment(node);
}

void SourceFormatter::visit(ast::BuiltinSetStmt const& node)
{
    emitLeadingComments(node);
    emit("set");
    if (node.name)
    {
        emit(" ");
        node.name->accept(*this);
    }
    if (node.value)
    {
        emit(" ");
        node.value->accept(*this);
    }
    emitTrailingComment(node);
}

void SourceFormatter::visit(ast::BuiltinUnsetStmt const& node)
{
    emitLeadingComments(node);
    emit("unset ");
    emit(node.name);
    emitTrailingComment(node);
}

void SourceFormatter::visit(ast::BuiltinJobsStmt const& node)
{
    emitLeadingComments(node);
    emit("jobs");
    emitTrailingComment(node);
}

void SourceFormatter::visit(ast::BuiltinFgStmt const& node)
{
    emitLeadingComments(node);
    emit("fg");
    if (node.jobId)
    {
        emit(" ");
        node.jobId->accept(*this);
    }
    emitTrailingComment(node);
}

void SourceFormatter::visit(ast::BuiltinBgStmt const& node)
{
    emitLeadingComments(node);
    emit("bg");
    if (node.jobId)
    {
        emit(" ");
        node.jobId->accept(*this);
    }
    emitTrailingComment(node);
}

void SourceFormatter::visit(ast::BuiltinWaitStmt const& node)
{
    emitLeadingComments(node);
    emit("wait");
    if (node.jobId)
    {
        emit(" ");
        node.jobId->accept(*this);
    }
    emitTrailingComment(node);
}

void SourceFormatter::visit(ast::BuiltinBindStmt const& node)
{
    emitLeadingComments(node);
    emit("bind");
    for (auto const& arg: node.args)
    {
        emit(" ");
        arg->accept(*this);
    }
    emitTrailingComment(node);
}

void SourceFormatter::visit(ast::BuiltinWhichStmt const& node)
{
    emitLeadingComments(node);
    emit("which");
    for (auto const& arg: node.args)
    {
        emit(" ");
        arg->accept(*this);
    }
    emitTrailingComment(node);
}

void SourceFormatter::visit(ast::LiteralExpr const& node)
{
    if (node.quoting == ast::LiteralQuoting::SingleQuoted)
        emit(std::format("'{}'", escapeString(node.value)));
    else if (node.quoting == ast::LiteralQuoting::Quoted)
        emit(std::format("\"{}\"", escapeString(node.value)));
    else
        emit(node.value);
}

void SourceFormatter::visit(ast::VariableExpr const& node)
{
    switch (node.type)
    {
        case ast::VariableType::Named:
            if (node.braced)
                emit(std::format("${{{}}}", node.name));
            else
                emit(std::format("${}", node.name));
            break;
        case ast::VariableType::ExitStatus: emit("$?"); break;
        case ast::VariableType::ProcessId: emit("$$"); break;
        case ast::VariableType::BackgroundId: emit("$!"); break;
        case ast::VariableType::Positional: emit(std::format("${}", node.name)); break;
    }
}

void SourceFormatter::visit(ast::TildeExpr const& node)
{
    emit("~");
    emit(node.user);
    emit(node.suffix);
}

void SourceFormatter::visit(ast::GlobExpr const& node)
{
    emit(node.pattern);
}

void SourceFormatter::visit(ast::ConcatExpr const& node)
{
    emit("\"");
    for (auto const& part: node.parts)
        part->accept(*this);
    emit("\"");
}

void SourceFormatter::visit(ast::ArithExpansionExpr const& node)
{
    emit("$((");
    printArithExpr(node.expression.get());
    emit("))");
}

void SourceFormatter::printArithExpr(ast::ArithExpr const* expr)
{
    if (auto const* lit = dynamic_cast<ast::ArithLiteralExpr const*>(expr))
    {
        emit(std::to_string(lit->value));
    }
    else if (auto const* var = dynamic_cast<ast::ArithVarExpr const*>(expr))
    {
        emit(var->name);
    }
    else if (auto const* binary = dynamic_cast<ast::ArithBinaryExpr const*>(expr))
    {
        emit("(");
        printArithExpr(binary->left.get());
        emit(" ");
        switch (binary->op)
        {
            case ast::ArithOp::Add: emit("+"); break;
            case ast::ArithOp::Sub: emit("-"); break;
            case ast::ArithOp::Mul: emit("*"); break;
            case ast::ArithOp::Div: emit("/"); break;
            case ast::ArithOp::Mod: emit("%"); break;
            case ast::ArithOp::Pow: emit("**"); break;
            case ast::ArithOp::Lt: emit("<"); break;
            case ast::ArithOp::Gt: emit(">"); break;
            case ast::ArithOp::Le: emit("<="); break;
            case ast::ArithOp::Ge: emit(">="); break;
            case ast::ArithOp::Eq: emit("=="); break;
            case ast::ArithOp::Ne: emit("!="); break;
            case ast::ArithOp::And: emit("&&"); break;
            case ast::ArithOp::Or: emit("||"); break;
            case ast::ArithOp::BitAnd: emit("&"); break;
            case ast::ArithOp::BitOr: emit("|"); break;
            case ast::ArithOp::BitXor: emit("^"); break;
            case ast::ArithOp::Shl: emit("<<"); break;
            case ast::ArithOp::Shr: emit(">>"); break;
            default: break;
        }
        emit(" ");
        printArithExpr(binary->right.get());
        emit(")");
    }
    else if (auto const* unary = dynamic_cast<ast::ArithUnaryExpr const*>(expr))
    {
        switch (unary->op)
        {
            case ast::ArithOp::Not: emit("!"); break;
            case ast::ArithOp::Neg: emit("-"); break;
            case ast::ArithOp::BitNot: emit("~"); break;
            default: break;
        }
        printArithExpr(unary->operand.get());
    }
}

void SourceFormatter::visit(ast::ParamExpansionExpr const& node)
{
    emit("${");
    switch (node.op)
    {
        case ast::ParamExpansionOp::Length:
            emit("#");
            emit(node.variable);
            break;
        case ast::ParamExpansionOp::DefaultValue:
            emit(node.variable);
            emit(":-");
            emit(node.operand1);
            break;
        case ast::ParamExpansionOp::AlternateValue:
            emit(node.variable);
            emit(":+");
            emit(node.operand1);
            break;
        case ast::ParamExpansionOp::AssignDefault:
            emit(node.variable);
            emit(":=");
            emit(node.operand1);
            break;
        case ast::ParamExpansionOp::ErrorIfUnset:
            emit(node.variable);
            emit(":?");
            emit(node.operand1);
            break;
        case ast::ParamExpansionOp::RemovePrefixShort:
            emit(node.variable);
            emit("#");
            emit(node.operand1);
            break;
        case ast::ParamExpansionOp::RemovePrefixLong:
            emit(node.variable);
            emit("##");
            emit(node.operand1);
            break;
        case ast::ParamExpansionOp::RemoveSuffixShort:
            emit(node.variable);
            emit("%");
            emit(node.operand1);
            break;
        case ast::ParamExpansionOp::RemoveSuffixLong:
            emit(node.variable);
            emit("%%");
            emit(node.operand1);
            break;
        case ast::ParamExpansionOp::ReplaceFirst:
            emit(node.variable);
            emit("/");
            emit(node.operand1);
            emit("/");
            emit(node.operand2);
            break;
        case ast::ParamExpansionOp::ReplaceAll:
            emit(node.variable);
            emit("//");
            emit(node.operand1);
            emit("/");
            emit(node.operand2);
            break;
    }
    emit("}");
}

void SourceFormatter::visit(ast::SubstitutionExpr const& node)
{
    emit(node.backtick ? "`" : "$(");
    if (node.pipeline)
        node.pipeline->accept(*this);
    emit(node.backtick ? "`" : ")");
}

void SourceFormatter::visit(ast::CommandFileSubst const& node)
{
    emit(node.mode == ast::ProcessSubstMode::Read ? "<(" : ">(");
    if (node.command)
        node.command->accept(*this);
    emit(")");
}

void SourceFormatter::visit(ast::StructuredPipelineSourceExpr const& node)
{
    if (node.command)
        node.command->accept(*this);
}

void SourceFormatter::visit(ast::DataSourceExpr const& node)
{
    if (node.pipeSource)
    {
        // Pipe chain: echo hello | from-json as ...
        node.pipeSource->accept(*this);
        emit(" | ");
    }

    switch (node.kind)
    {
        case ast::DataSourceExpr::Kind::OpenJson: emit("open-json"); break;
        case ast::DataSourceExpr::Kind::OpenCsv: emit("open-csv"); break;
        case ast::DataSourceExpr::Kind::FromJson: emit("from-json"); break;
        case ast::DataSourceExpr::Kind::FromCsv: emit("from-csv"); break;
    }

    if (node.filePath)
    {
        emit(" ");
        node.filePath->accept(*this);
    }

    emit(" as ");
    if (!node.typeName.empty())
    {
        emit(node.typeName);
    }
    else
    {
        emit("{ ");
        for (auto const i: std::views::iota(0uz, node.inlineFields.size()))
        {
            if (i > 0)
                emit("; ");
            emit(std::format("{}: {}", node.inlineFields[i].name, toString(node.inlineFields[i].type)));
            if (node.inlineFields[i].defaultValue)
            {
                emit(" = ");
                node.inlineFields[i].defaultValue->accept(*this);
            }
        }
        emit(" }");
    }
}

// ============================================================================
// F# expression and statement visitors
// ============================================================================

void SourceFormatter::visit(ast::CompositionExpr const& node)
{
    node.left->accept(*this);
    emit(node.op == ast::CompositionOp::Forward ? " >> " : " << ");
    node.right->accept(*this);
}

void SourceFormatter::visit(ast::PlaceholderLambdaExpr const& node)
{
    // Reconstruct the placeholder syntax by formatting body and replacing __x with _
    if (node.parenthesized)
        emit("(");

    // Format body with empty comments to avoid re-emitting file-level comments
    std::vector<CommentTrivia> const noComments;
    auto bodyStr = format(*node.body, noComments, _config);
    // Trim trailing newline from format()
    while (!bodyStr.empty() && bodyStr.back() == '\n')
        bodyStr.pop_back();

    // Replace all __x occurrences with _
    std::string const placeholder = "__x";
    std::string const replacement = "_";
    size_t pos = 0;
    while ((pos = bodyStr.find(placeholder, pos)) != std::string::npos)
    {
        // Don't replace if it's part of a longer identifier
        auto const after = pos + placeholder.size();
        if (after < bodyStr.size() && (std::isalnum(bodyStr[after]) || bodyStr[after] == '_'))
        {
            pos = after;
            continue;
        }
        bodyStr.replace(pos, placeholder.size(), replacement);
        pos += replacement.size();
    }

    emit(bodyStr);

    if (node.parenthesized)
        emit(")");
}

void SourceFormatter::visit(ast::IfExpr const& node)
{
    formatIfExpr(node, "if");
}

void SourceFormatter::formatIfExpr(ast::IfExpr const& node, std::string_view keyword)
{
    emitLeadingComments(node);

    emit(keyword);
    emit(" ");
    node.condition->accept(*this);

    auto const keywordWidth = keyword.size();

    if (!node.elseExpr)
    {
        // No else branch: `if cond then thenBody` on one line when it fits
        auto const totalWidth = estimateWidth(node);
        if (totalWidth <= _config.maxLineWidth && !isCompoundExpr(*node.thenExpr))
        {
            emit(" then ");
            node.thenExpr->accept(*this);
        }
        else
        {
            emit(" then");
            emitNewline();
            indent();
            node.thenExpr->accept(*this);
            dedent();
        }
    }
    else
    {
        // Has else branch: never put all three expressions on one line.
        auto const condWidth = estimateWidth(*node.condition);
        auto const thenWidth = estimateWidth(*node.thenExpr);
        auto const thenIsCompound = isCompoundExpr(*node.thenExpr);
        auto const elseIsCompound = node.elseExpr ? isCompoundExpr(*node.elseExpr) : false;
        auto const* nestedIf = dynamic_cast<ast::IfExpr const*>(node.elseExpr.get());
        auto const indentWidth = static_cast<size_t>(_indentLevel) * _config.indentWidth;

        // Compact two-line: `if cond then thenBody\nelse elseBody`
        auto const line1CompactWidth =
            indentWidth + keywordWidth + 1 /*" "*/ + condWidth + 6 /*" then "*/ + thenWidth;
        auto const elseBodyWidth = estimateWidth(*node.elseExpr);
        auto const line2CompactWidth = indentWidth + 5 /*"else "*/ + elseBodyWidth;
        auto const compactFeasible = !thenIsCompound && !elseIsCompound && nestedIf == nullptr
                                     && line1CompactWidth <= _config.maxLineWidth
                                     && line2CompactWidth <= _config.maxLineWidth;

        if (compactFeasible)
        {
            // Compact two-line format
            emit(" then ");
            node.thenExpr->accept(*this);
            emitNewline();
            emit("else ");
            node.elseExpr->accept(*this);
        }
        else
        {
            // Multi-line format: both branches indented
            emit(" then");
            emitNewline();
            indent();
            node.thenExpr->accept(*this);
            dedent();
            emitNewline();
            if (nestedIf != nullptr)
            {
                // Emit `elif` at the same column as `if`/`else`
                formatIfExpr(*nestedIf, "elif");
            }
            else
            {
                emit("else");
                emitNewline();
                indent();
                node.elseExpr->accept(*this);
                dedent();
            }
        }
    }

    emitTrailingComment(node);
}

void SourceFormatter::visit(ast::TupleExpr const& node)
{
    emitWrappedElements(node.elements, ", ", "(", ")");
}

void SourceFormatter::visit(ast::UnitExpr const& /*node*/)
{
    emit("()");
}

void SourceFormatter::visit(ast::BlockExpr const& node)
{
    if (node.isBraceDelimited)
    {
        emit("{ ");
        for (auto const& stmt: node.statements)
        {
            stmt->accept(*this);
            emit("; ");
        }
        if (node.result)
            node.result->accept(*this);
        emit(" }");
    }
    else
    {
        for (size_t i = 0; i < node.statements.size(); ++i)
        {
            if (i > 0)
                emitNewline();
            node.statements[i]->accept(*this);
        }
        if (node.result && !dynamic_cast<ast::UnitExpr const*>(node.result.get()))
        {
            if (!node.statements.empty())
                emitNewline();
            node.result->accept(*this);
        }
    }
}

void SourceFormatter::visit(ast::MutAssignStmt const& node)
{
    emitLeadingComments(node);
    emit(node.name);
    emit(" <- ");
    node.value->accept(*this);
    emitTrailingComment(node);
}

void SourceFormatter::visit(ast::MutAssignExpr const& node)
{
    emit(node.name);
    emit(" <- ");
    node.value->accept(*this);
}

void SourceFormatter::visit(ast::LetBindingStmt const& node)
{
    emitLeadingComments(node);
    emit("let ");
    if (node.visibility == ast::Visibility::Exported)
        emit("export ");
    if (node.visibility == ast::Visibility::Private)
        emit("private ");
    if (node.mutability == ast::Mutability::Mutable)
        emit("mut ");
    if (node.resourceMode == ast::ResourceMode::Use)
        emit("use ");
    else if (node.resourceMode == ast::ResourceMode::Manual)
        emit("manual ");
    if (node.isRecursive)
        emit("rec ");
    if (node.destructurePattern)
        emitPattern(*node.destructurePattern);
    else
        emit(node.name);
    emitParameters(node.parameters);

    if (node.returnType)
        emit(": " + endo::toString(*node.returnType));

    if (node.isProperty())
    {
        emit(" with ");
        if (node.getter)
        {
            emit("get () = ");
            if (wouldFormatMultiline(*node.getter->body))
            {
                emitNewline();
                indent();
                node.getter->body->accept(*this);
                dedent();
            }
            else
                node.getter->body->accept(*this);
        }
        if (node.getter && node.setter)
        {
            if (wouldFormatMultiline(*node.getter->body))
                emitNewline();
            else
                emit(" ");
            emit("and ");
        }
        if (node.setter)
        {
            emit("set (" + node.setter->paramName + ") = ");
            if (wouldFormatMultiline(*node.setter->body))
            {
                emitNewline();
                indent();
                node.setter->body->accept(*this);
                dedent();
            }
            else
                node.setter->body->accept(*this);
        }
    }
    else
    {
        emit(" =");
        if (node.value)
        {
            // List/Tuple expressions are self-wrapping (emitWrappedWith handles [, indent,
            // bin-pack, dedent, ]) — keep opening delimiter on the = line.
            auto const bodyWidth = estimateWidth(*node.value);
            auto const isWrappableContainer = dynamic_cast<ast::ListExpr const*>(node.value.get())
                                              || dynamic_cast<ast::TupleExpr const*>(node.value.get());
            if (!isWrappableContainer
                && (wouldFormatMultiline(*node.value)
                    || (bodyWidth > _config.maxLineWidth / 2 && !node.parameters.empty())))
            {
                emitNewline();
                indent();
                node.value->accept(*this);
                dedent();
            }
            else
            {
                emit(" ");
                // Emit inline comments between '=' and value (e.g., `let x = (* inline *) 42`)
                if (node.value->location)
                    emitInlineComments(node.value->location->begin.line, node.value->location->begin.column);
                node.value->accept(*this);
            }
        }
    }

    for (auto const& ab: node.andBindings)
    {
        emitNewline();
        emit("and ");
        emit(ab.name);
        emitParameters(ab.parameters);
        if (ab.returnType)
            emit(": " + endo::toString(*ab.returnType));
        emit(" =");
        if (ab.value)
        {
            auto const bodyWidth = estimateWidth(*ab.value);
            auto const isWrappableContainer = dynamic_cast<ast::ListExpr const*>(ab.value.get())
                                              || dynamic_cast<ast::TupleExpr const*>(ab.value.get());
            if (!isWrappableContainer
                && (wouldFormatMultiline(*ab.value)
                    || (!ab.parameters.empty() && bodyWidth > _config.maxLineWidth / 2)))
            {
                emitNewline();
                indent();
                ab.value->accept(*this);
                dedent();
            }
            else
            {
                emit(" ");
                ab.value->accept(*this);
            }
        }
    }

    emitTrailingComment(node);
}

void SourceFormatter::visit(ast::LetInExpr const& node)
{
    emit("let ");
    if (node.resourceMode == ast::ResourceMode::Use)
        emit("use ");
    else if (node.resourceMode == ast::ResourceMode::Manual)
        emit("manual ");
    if (node.isRecursive)
        emit("rec ");
    if (node.destructurePattern)
        emitPattern(*node.destructurePattern);
    else
        emit(node.name);
    emitParameters(node.parameters);
    if (node.returnType)
        emit(": " + endo::toString(*node.returnType));
    emit(" = ");
    if (node.value)
        node.value->accept(*this);

    auto const bodyIsCompound = node.body && isCompoundExpr(*node.body);

    if (bodyIsCompound)
    {
        emit(" in");
        emitNewline();
        indent();
        node.body->accept(*this);
        dedent();
    }
    else
    {
        emit(" in ");
        if (node.body)
            node.body->accept(*this);
    }
}

void SourceFormatter::visit(ast::ExprStmt const& node)
{
    emitLeadingComments(node);
    if (node.expr)
        node.expr->accept(*this);
    emitTrailingComment(node);
}

void SourceFormatter::visit(ast::BinaryExpr const& node)
{
    // No extra parentheses — ParenExpr in the AST already captures user intent.
    // Adding parens here would cause double-parens on round-trip.
    if (node.left)
        node.left->accept(*this);
    emit(" ");
    switch (node.op)
    {
        case ast::BinaryOp::Add: emit("+"); break;
        case ast::BinaryOp::Sub: emit("-"); break;
        case ast::BinaryOp::Mul: emit("*"); break;
        case ast::BinaryOp::Div: emit("/"); break;
        case ast::BinaryOp::Mod: emit("%"); break;
        case ast::BinaryOp::Pow: emit("**"); break;
        case ast::BinaryOp::Eq: emit("=="); break;
        case ast::BinaryOp::Ne: emit("!="); break;
        case ast::BinaryOp::Lt: emit("<"); break;
        case ast::BinaryOp::Le: emit("<="); break;
        case ast::BinaryOp::Gt: emit(">"); break;
        case ast::BinaryOp::Ge: emit(">="); break;
        case ast::BinaryOp::And: emit("&&"); break;
        case ast::BinaryOp::Or: emit("||"); break;
    }
    emit(" ");
    if (node.right)
        node.right->accept(*this);
}

void SourceFormatter::visit(ast::UnaryExpr const& node)
{
    switch (node.op)
    {
        case ast::UnaryOp::Neg: emit("-"); break;
        case ast::UnaryOp::Not: emit("!"); break;
    }
    if (node.operand)
        node.operand->accept(*this);
}

void SourceFormatter::visit(ast::PipelineExpr const& node)
{
    auto const chain = collectPipelineChain(node);

    // Structured pipelines (shell command source) must stay on one line
    // because shell commands terminate at newlines and multi-line breaks parsing
    auto const isShellSource = chain[0] && dynamic_cast<ast::StructuredPipelineSourceExpr const*>(chain[0]);
    if (isShellSource)
    {
        for (auto const i: std::views::iota(0uz, chain.size()))
        {
            if (i > 0)
                emit(" |> ");
            if (chain[i])
                chain[i]->accept(*this);
        }
        return;
    }

    // Single pipe that fits inline: keep on one line
    if (chain.size() <= 2)
    {
        auto const inlineWidth = currentColumn() + estimateWidth(node);
        if (inlineWidth <= _config.maxLineWidth)
        {
            if (chain[0])
                chain[0]->accept(*this);
            emit(" |> ");
            if (chain.size() > 1 && chain[1])
                chain[1]->accept(*this);
            return;
        }
    }

    // Multi-pipe or exceeds width: wrap with |> at start of continuation lines
    if (chain[0])
        chain[0]->accept(*this);

    auto const firstExprWidth = chain[0] ? estimateWidth(*chain[0]) : 0uz;
    auto const keepFirstInline = firstExprWidth <= _config.indentWidth;

    indent();
    for (auto const i: std::views::iota(1uz, chain.size()))
    {
        if (i == 1 && keepFirstInline)
            emit(" ");
        else
            emitNewline();
        emit("|> ");
        if (chain[i])
            chain[i]->accept(*this);
    }
    dedent();
}

void SourceFormatter::visit(ast::ApplicationExpr const& node)
{
    if (node.function)
        node.function->accept(*this);
    emit(" ");
    if (node.argument)
        node.argument->accept(*this);
}

void SourceFormatter::visit(ast::IdentifierExpr const& node)
{
    if (_inPlaceholderLambda && node.name == "__x")
        emit("_");
    else
        emit(node.name);
}

void SourceFormatter::visit(ast::IntLiteralExpr const& node)
{
    emit(node.originalText.empty() ? std::to_string(node.value) : node.originalText);
}

void SourceFormatter::visit(ast::FloatLiteralExpr const& node)
{
    if (!node.originalText.empty())
    {
        emit(node.originalText);
        return;
    }
    auto s = std::format("{}", node.value);
    if (s.find('.') == std::string::npos && s.find('e') == std::string::npos)
        s += ".0";
    emit(s);
}

void SourceFormatter::visit(ast::BoolLiteralExpr const& node)
{
    emit(node.value ? "true" : "false");
}

void SourceFormatter::visit(ast::SizeLiteralExpr const& node)
{
    constexpr int64_t KB = 1024;
    constexpr int64_t MB = KB * 1024;
    constexpr int64_t GB = MB * 1024;
    constexpr int64_t TB = GB * 1024;
    if (node.bytes >= TB && node.bytes % TB == 0)
        emit(std::to_string(node.bytes / TB) + "TB");
    else if (node.bytes >= GB && node.bytes % GB == 0)
        emit(std::to_string(node.bytes / GB) + "GB");
    else if (node.bytes >= MB && node.bytes % MB == 0)
        emit(std::to_string(node.bytes / MB) + "MB");
    else if (node.bytes >= KB && node.bytes % KB == 0)
        emit(std::to_string(node.bytes / KB) + "KB");
    else
        emit(std::to_string(node.bytes) + "B");
}

void SourceFormatter::visit(ast::TimeSpanLiteralExpr const& node)
{
    constexpr int64_t H = 3600000;
    constexpr int64_t MIN = 60000;
    constexpr int64_t S = 1000;
    if (node.milliseconds >= H && node.milliseconds % H == 0)
        emit(std::to_string(node.milliseconds / H) + "h");
    else if (node.milliseconds >= MIN && node.milliseconds % MIN == 0)
        emit(std::to_string(node.milliseconds / MIN) + "min");
    else if (node.milliseconds >= S && node.milliseconds % S == 0)
        emit(std::to_string(node.milliseconds / S) + "s");
    else
        emit(std::to_string(node.milliseconds) + "ms");
}

void SourceFormatter::visit(ast::BreakExpr const& /*node*/)
{
    emit("break");
}

void SourceFormatter::visit(ast::ContinueExpr const& /*node*/)
{
    emit("continue");
}

void SourceFormatter::visit(ast::ParenExpr const& node)
{
    emit("(");
    if (node.inner)
        node.inner->accept(*this);
    emit(")");
}

void SourceFormatter::visit(ast::LambdaExpr const& node)
{
    emit("fun");
    for (auto const i: std::views::iota(0uz, node.parameters.size()))
    {
        emit(" ");
        auto const& param = node.parameters[i];
        if (param.isUnit)
            emit("()");
        else if (param.typeAnnotation)
            emit("(" + param.name + ": " + endo::toString(*param.typeAnnotation) + ")");
        else
            emit(param.name);
    }
    if (node.returnType)
        emit(" : " + endo::toString(*node.returnType));
    if (node.body && wouldFormatMultiline(*node.body))
    {
        emit(" ->");
        emitNewline();
        indent();
        node.body->accept(*this);
        dedent();
    }
    else
    {
        emit(" -> ");
        if (node.body)
            node.body->accept(*this);
    }
}

void SourceFormatter::visit(ast::MatchExpr const& node)
{
    emitLeadingComments(node);
    emit("match ");
    if (node.scrutinee)
        node.scrutinee->accept(*this);
    emit(" with");
    for (auto const& arm: node.arms)
    {
        emitNewline();
        emit("| ");
        if (arm.pattern)
            emitPattern(*arm.pattern);
        if (arm.guard)
        {
            emit(" when ");
            arm.guard->accept(*this);
        }
        if (arm.body && wouldFormatMultiline(*arm.body))
        {
            emit(" ->");
            emitNewline();
            indent();
            arm.body->accept(*this);
            dedent();
        }
        else
        {
            emit(" -> ");
            arm.body->accept(*this);
        }
    }
    emitTrailingComment(node);
}

void SourceFormatter::visit(ast::ListExpr const& node)
{
    emitWrappedElements(node.elements, node.useComma ? ", " : "; ", "[", "]");
}

void SourceFormatter::visit(ast::ConsExpr const& node)
{
    if (node.head)
        node.head->accept(*this);
    emit(" :: ");
    if (node.tail)
        node.tail->accept(*this);
}

void SourceFormatter::visit(ast::ConcatListExpr const& node)
{
    auto const chain = collectConcatChain(node);

    // Single concat that fits inline: keep on one line
    if (chain.size() <= 2)
    {
        auto const inlineWidth = currentColumn() + estimateWidth(node);
        if (inlineWidth <= _config.maxLineWidth)
        {
            if (chain[0])
                chain[0]->accept(*this);
            emit(" @ ");
            if (chain.size() > 1 && chain[1])
                chain[1]->accept(*this);
            return;
        }
    }

    // Multi-concat or exceeds width: wrap with @ at start of continuation lines
    if (chain[0])
        chain[0]->accept(*this);

    auto const firstExprWidth = chain[0] ? estimateWidth(*chain[0]) : 0uz;
    auto const keepFirstInline = firstExprWidth <= _config.indentWidth;

    indent();
    for (auto const i: std::views::iota(1uz, chain.size()))
    {
        if (i == 1 && keepFirstInline)
            emit(" ");
        else
            emitNewline();
        emit("@ ");
        if (chain[i])
            chain[i]->accept(*this);
    }
    dedent();
}

void SourceFormatter::visit(ast::ListRangeExpr const& node)
{
    emit("[");
    if (node.start)
        node.start->accept(*this);
    emit("..");
    if (node.step)
    {
        node.step->accept(*this);
        emit("..");
    }
    if (node.end)
        node.end->accept(*this);
    emit("]");
}

void SourceFormatter::visit(ast::ListComprehensionExpr const& node)
{
    emit("[for ");
    printComprehensionGenerator(node);
    emit("]");
}

void SourceFormatter::printComprehensionGenerator(ast::ListComprehensionExpr const& node)
{
    emit(node.variable);
    emit(" in ");
    if (node.source)
        node.source->accept(*this);
    if (node.filter)
    {
        emit(" when ");
        node.filter->accept(*this);
    }
    emit(" -> ");
    if (auto const* inner = dynamic_cast<ast::ListComprehensionExpr const*>(node.body.get()))
    {
        emit("for ");
        printComprehensionGenerator(*inner);
    }
    else if (node.body)
    {
        node.body->accept(*this);
    }
}

void SourceFormatter::visit(ast::ShellCommandExpr const& node)
{
    emitLeadingComments(node);
    emit("& ");
    if (node.command)
        node.command->accept(*this);
}

void SourceFormatter::visit(ast::SplatExpr const& node)
{
    emit("...");
    emit(node.name);
}

void SourceFormatter::visit(ast::OptionExpr const& node)
{
    if (node.isSome)
    {
        emit("Some ");
        if (node.value)
            node.value->accept(*this);
    }
    else
    {
        emit("None");
    }
}

void SourceFormatter::visit(ast::ResultExpr const& node)
{
    if (node.isOk)
        emit("Ok ");
    else
        emit("Error ");
    if (node.payload)
        node.payload->accept(*this);
}

void SourceFormatter::visit(ast::TryExpr const& node)
{
    if (node.operand)
        node.operand->accept(*this);
    emit("?");
}

void SourceFormatter::visit(ast::OptionDefaultExpr const& node)
{
    if (node.option)
        node.option->accept(*this);
    emit(" ?| ");
    if (node.defaultValue)
        node.defaultValue->accept(*this);
}

void SourceFormatter::visit(ast::TryWithExpr const& node)
{
    emitLeadingComments(node);
    if (node.body && isCompoundExpr(*node.body))
    {
        emit("try");
        emitNewline();
        indent();
        node.body->accept(*this);
        dedent();
        emitNewline();
        emit("with");
    }
    else
    {
        emit("try ");
        if (node.body)
            node.body->accept(*this);
        emit(" with");
    }
    for (auto const& arm: node.handlers)
    {
        emitNewline();
        emit("| ");
        if (arm.pattern)
            emitPattern(*arm.pattern);
        if (arm.guard)
        {
            emit(" when ");
            arm.guard->accept(*this);
        }
        if (arm.body && wouldFormatMultiline(*arm.body))
        {
            emit(" ->");
            emitNewline();
            indent();
            arm.body->accept(*this);
            dedent();
        }
        else
        {
            emit(" -> ");
            if (arm.body)
                arm.body->accept(*this);
        }
    }
    emitTrailingComment(node);
}

void SourceFormatter::visit(ast::TryFinallyExpr const& node)
{
    emitLeadingComments(node);
    auto const bodyIsCompound = node.body && isCompoundExpr(*node.body);
    auto const finallyIsCompound = node.finallyExpr && isCompoundExpr(*node.finallyExpr);
    auto const useMultiLine = bodyIsCompound || finallyIsCompound;

    if (useMultiLine)
    {
        // Multi-line: both branches indented (symmetry)
        emit("try");
        emitNewline();
        indent();
        if (node.body)
            node.body->accept(*this);
        dedent();
        emitNewline();
        emit("finally");
        emitNewline();
        indent();
        if (node.finallyExpr)
            node.finallyExpr->accept(*this);
        dedent();
    }
    else
    {
        // Compact two-line: `try body\nfinally finallyBody`
        auto const indentWidth = static_cast<size_t>(_indentLevel) * _config.indentWidth;
        auto const bodyWidth = node.body ? estimateWidth(*node.body) : size_t { 0 };
        auto const finallyWidth = node.finallyExpr ? estimateWidth(*node.finallyExpr) : size_t { 0 };
        auto const line1Width = indentWidth + 4 /*"try "*/ + bodyWidth;
        auto const line2Width = indentWidth + 8 /*"finally "*/ + finallyWidth;

        if (line1Width <= _config.maxLineWidth && line2Width <= _config.maxLineWidth)
        {
            emit("try ");
            if (node.body)
                node.body->accept(*this);
            emitNewline();
            emit("finally ");
            if (node.finallyExpr)
                node.finallyExpr->accept(*this);
        }
        else
        {
            // Both exceed: indent both
            emit("try");
            emitNewline();
            indent();
            if (node.body)
                node.body->accept(*this);
            dedent();
            emitNewline();
            emit("finally");
            emitNewline();
            indent();
            if (node.finallyExpr)
                node.finallyExpr->accept(*this);
            dedent();
        }
    }
    emitTrailingComment(node);
}

void SourceFormatter::visit(ast::LazyExpr const& node)
{
    emitLeadingComments(node);
    emit("lazy ");
    if (node.body)
        node.body->accept(*this);
    emitTrailingComment(node);
}

void SourceFormatter::visit(ast::SeqExpr const& node)
{
    emitLeadingComments(node);
    if (node.yields.empty())
    {
        emit("seq {}");
        emitTrailingComment(node);
        return;
    }

    // Use multi-line when there are more than 2 yields
    auto const useMultiLine = node.yields.size() > 2;

    if (useMultiLine)
    {
        emit("seq {");
        ++_indentLevel;
        for (auto const& yield: node.yields)
        {
            emitNewline();
            emit(yield.isSplice ? "yield! " : "yield ");
            if (yield.value)
                yield.value->accept(*this);
        }
        --_indentLevel;
        emitNewline();
        emit("}");
    }
    else
    {
        emit("seq { ");
        for (auto i = 0u; i < node.yields.size(); ++i)
        {
            if (i > 0)
                emit("; ");
            emit(node.yields[i].isSplice ? "yield! " : "yield ");
            if (node.yields[i].value)
                node.yields[i].value->accept(*this);
        }
        emit(" }");
    }
    emitTrailingComment(node);
}

void SourceFormatter::visit(ast::FStringExpr const& node)
{
    emit("$\"");
    for (auto const& part: node.parts)
    {
        if (auto const* lit = dynamic_cast<ast::LiteralExpr const*>(part.get()))
        {
            // Escape braces in f-string literal parts: { → {{, } → }}
            auto escaped = escapeString(lit->value);
            std::string result;
            result.reserve(escaped.size());
            for (auto const ch: escaped)
            {
                if (ch == '{')
                    result += "{{";
                else if (ch == '}')
                    result += "}}";
                else
                    result += ch;
            }
            emit(result);
        }
        else
        {
            emit("{");
            part->accept(*this);
            emit("}");
        }
    }
    emit("\"");
}

void SourceFormatter::visit(ast::RecordTypeDefStmt const& node)
{
    emitLeadingComments(node);
    emit(std::format("type {}", node.name));
    if (!node.typeParams.empty())
    {
        emit("<");
        for (auto const i: std::views::iota(0uz, node.typeParams.size()))
        {
            if (i > 0)
                emit(", ");
            emit("'" + node.typeParams[i]);
        }
        emit(">");
    }
    endo::TypeVarNameMap nameMap;
    for (auto const i: std::views::iota(0uz, node.typeParamIds.size()))
        nameMap[node.typeParamIds[i]] = node.typeParams[i];
    emit(" = { ");
    for (auto const i: std::views::iota(0uz, node.fields.size()))
    {
        if (i > 0)
            emit("; ");
        emit(node.fields[i].name);
        emit(": ");
        emit(endo::toString(node.fields[i].type, nameMap));
    }
    emit(" }");
    emitTrailingComment(node);
}

void SourceFormatter::visit(ast::RecordExpr const& node)
{
    emitWrappedWith(
        node.fields.size(),
        "; ",
        "{ ",
        " }",
        [&](size_t i) {
            emit(node.fields[i].name);
            emit(" = ");
            node.fields[i].value->accept(*this);
        },
        [&](size_t i) -> size_t {
            return node.fields[i].name.size() + 3 /* " = " */ + estimateWidth(*node.fields[i].value);
        });
}

void SourceFormatter::visit(ast::RecordUpdateExpr const& node)
{
    // Estimate inline width: "{ base with field1 = v1; field2 = v2 }"
    auto const baseWidth = estimateWidth(*node.base);
    auto totalInlineWidth = currentColumn() + 2 /* "{ " */ + baseWidth + 6 /* " with " */;
    for (auto const i: std::views::iota(0uz, node.updates.size()))
    {
        if (i > 0)
            totalInlineWidth += 2; // "; "
        totalInlineWidth += node.updates[i].name.size() + 3 + estimateWidth(*node.updates[i].value);
    }
    totalInlineWidth += 2; // " }"

    if (totalInlineWidth <= _config.maxLineWidth)
    {
        // Inline
        emit("{ ");
        node.base->accept(*this);
        emit(" with ");
        for (auto const i: std::views::iota(0uz, node.updates.size()))
        {
            if (i > 0)
                emit("; ");
            emit(node.updates[i].name);
            emit(" = ");
            node.updates[i].value->accept(*this);
        }
        emit(" }");
    }
    else
    {
        // Wrapped
        emit("{ ");
        node.base->accept(*this);
        emit(" with");
        emitNewline();
        indent();
        for (auto const i: std::views::iota(0uz, node.updates.size()))
        {
            if (i > 0)
            {
                emit(";");
                emitNewline();
            }
            emit(node.updates[i].name);
            emit(" = ");
            node.updates[i].value->accept(*this);
        }
        emitNewline();
        dedent();
        emit("}");
    }
}

void SourceFormatter::visit(ast::FieldAccessExpr const& node)
{
    node.object->accept(*this);
    emit(".");
    emit(node.fieldName);
}

void SourceFormatter::visit(ast::OptionalChainExpr const& node)
{
    node.object->accept(*this);
    emit("?.");
    emit(node.fieldName);
}

void SourceFormatter::visit(ast::UnionTypeDefStmt const& node)
{
    emitLeadingComments(node);
    emit(std::format("type {}", node.name));
    if (!node.typeParams.empty())
    {
        emit("<");
        for (auto const i: std::views::iota(0uz, node.typeParams.size()))
        {
            if (i > 0)
                emit(", ");
            emit("'" + node.typeParams[i]);
        }
        emit(">");
    }
    endo::TypeVarNameMap nameMap;
    for (auto const i: std::views::iota(0uz, node.typeParamIds.size()))
        nameMap[node.typeParamIds[i]] = node.typeParams[i];
    emit(" =");
    indent();
    for (auto const& variant: node.variants)
    {
        emitNewline();
        emit("| ");
        emit(variant.name);
        if (!variant.payloadTypes.empty())
        {
            emit(" of ");
            for (auto const i: std::views::iota(0uz, variant.payloadTypes.size()))
            {
                if (i > 0)
                    emit(" * ");
                if (i < variant.fieldNames.size() && !variant.fieldNames[i].empty())
                {
                    emit(variant.fieldNames[i]);
                    emit(": ");
                }
                emit(endo::toString(variant.payloadTypes[i], nameMap));
            }
        }
    }
    dedent();
    emitTrailingComment(node);
}

void SourceFormatter::visit(ast::UnionConstructorExpr const& node)
{
    emit(node.constructorName);
    if (node.arguments.size() > 1)
    {
        emit(" (");
        for (auto const i: std::views::iota(0uz, node.arguments.size()))
        {
            if (i > 0)
                emit(", ");
            node.arguments[i]->accept(*this);
        }
        emit(")");
    }
    else
    {
        for (auto const& arg: node.arguments)
        {
            emit(" ");
            arg->accept(*this);
        }
    }
}

void SourceFormatter::visit(ast::ExecPipelineExpr const& node)
{
    for (auto const i: std::views::iota(0uz, node.commands.size()))
    {
        if (i > 0)
            emit(" | ");
        emit("exec ");
        node.commands[i].program->accept(*this);
        for (auto const& arg: node.commands[i].arguments)
        {
            emit(" ");
            arg->accept(*this);
        }
    }
}

// ============================================================================
// Pattern visitors
// ============================================================================

void SourceFormatter::visit(pattern::LiteralPattern const& pat)
{
    std::visit(
        [this](auto const& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, int64_t>)
                emit(std::to_string(v));
            else if constexpr (std::is_same_v<T, double>)
                emit(std::format("{}", v));
            else if constexpr (std::is_same_v<T, bool>)
                emit(v ? "true" : "false");
            else if constexpr (std::is_same_v<T, std::string>)
                emit("\"" + escapeString(v) + "\"");
        },
        pat.value);
}

void SourceFormatter::visit(pattern::VariablePattern const& pat)
{
    if (pat.isMutable)
        emit("mut ");
    emit(pat.name);
}

void SourceFormatter::visit(pattern::WildcardPattern const& /*pat*/)
{
    emit("_");
}

void SourceFormatter::visit(pattern::TuplePattern const& pat)
{
    emit("(");
    for (auto const i: std::views::iota(0uz, pat.elements.size()))
    {
        if (i > 0)
            emit(", ");
        pat.elements[i]->accept(*this);
    }
    emit(")");
}

void SourceFormatter::visit(pattern::ListPattern const& pat)
{
    emit("[");
    for (auto const i: std::views::iota(0uz, pat.elements.size()))
    {
        if (i > 0)
            emit("; ");
        pat.elements[i]->accept(*this);
    }
    if (pat.restBinding)
    {
        if (!pat.elements.empty())
            emit("; ");
        emit(*pat.restBinding);
        emit("...");
    }
    emit("]");
}

void SourceFormatter::visit(pattern::ConsPattern const& pat)
{
    pat.head->accept(*this);
    emit(" :: ");
    pat.tail->accept(*this);
}

void SourceFormatter::visit(pattern::RecordPattern const& pat)
{
    emit("{ ");
    for (auto const i: std::views::iota(0uz, pat.fields.size()))
    {
        if (i > 0)
            emit("; ");
        emit(pat.fields[i].name);
        if (pat.fields[i].pattern)
        {
            emit(" = ");
            pat.fields[i].pattern->accept(*this);
        }
    }
    if (pat.hasWildcard)
    {
        if (!pat.fields.empty())
            emit("; ");
        emit("_");
    }
    emit(" }");
}

void SourceFormatter::visit(pattern::ConstructorPattern const& pat)
{
    emit(pat.name);
    if (pat.payload)
    {
        emit(" ");
        (*pat.payload)->accept(*this);
    }
}

void SourceFormatter::visit(pattern::AsPattern const& pat)
{
    pat.inner->accept(*this);
    emit(" as ");
    emit(pat.name);
}

void SourceFormatter::visit(pattern::OrPattern const& pat)
{
    for (auto const i: std::views::iota(0uz, pat.alternatives.size()))
    {
        if (i > 0)
            emit(" | ");
        pat.alternatives[i]->accept(*this);
    }
}

void SourceFormatter::visit(pattern::GuardedPattern const& pat)
{
    pat.pattern->accept(*this);
    if (pat.guard)
    {
        emit(" when ");
        pat.guard->accept(*this);
    }
}

// ============================================================================
// Module System
// ============================================================================

void SourceFormatter::visit(ast::ImportStmt const& node)
{
    emitLeadingComments(node);
    emitIndent();
    emit("import ");
    emit(node.modulePath);
    emitTrailingComment(node);
}

void SourceFormatter::visit(ast::OpenStmt const& node)
{
    emitLeadingComments(node);
    emitIndent();
    emit("open ");
    emit(node.modulePath);
    if (!node.selectiveNames.empty())
    {
        emit(" with (");
        for (size_t i = 0; i < node.selectiveNames.size(); ++i)
        {
            if (i > 0)
                emit(", ");
            emit(node.selectiveNames[i]);
        }
        emit(")");
    }
    emitTrailingComment(node);
}

void SourceFormatter::visit(ast::ModuleDeclStmt const& node)
{
    emitLeadingComments(node);
    emitIndent();
    emit("module ");
    emit(node.name);
    emit(" =");
    emitNewline();
    indent();
    for (auto const& stmt: node.body)
    {
        stmt->accept(*this);
        emitNewline();
    }
    dedent();
    emitIndent();
    emit("end");
    emitTrailingComment(node);
}

} // namespace endo::format
