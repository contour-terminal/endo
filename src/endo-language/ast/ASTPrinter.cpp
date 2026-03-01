// SPDX-License-Identifier: Apache-2.0
#include <endo-language/ast/ASTPrinter.hpp>
#include <endo-language/types/Type.hpp>

#include <crispy/assert.h>

#include <format>

namespace endo::ast
{

std::string ASTPrinter::print(Node const& node)
{
    ASTPrinter printer;
    node.accept(printer);
    return printer._result;
}

void ASTPrinter::visit(FileDescriptor const& node)
{
    _result += std::format("{}", node.value);
}

void ASTPrinter::visit(InputRedirect const& node)
{
    if (node.targetFd->value != 0)
        _result += std::format(" {}<", node.targetFd->value);
    else
        _result += " <";
    node.source->accept(*this);
}

void ASTPrinter::visit(OutputRedirect const& node)
{
    if (std::holds_alternative<std::unique_ptr<FileDescriptor>>(node.target))
    {
        // fd duplication: 2>&1
        _result += std::format(
            " {}>&{}", node.source->value, std::get<std::unique_ptr<FileDescriptor>>(node.target)->value);
    }
    else
    {
        // file redirect: > file or >> file
        if (node.source->value != 1)
            _result += std::format(" {}", node.source->value);
        else
            _result += " ";
        _result += node.append ? ">>" : ">";
        std::get<std::unique_ptr<Expr>>(node.target)->accept(*this);
    }
}

void ASTPrinter::visit(HereDocument const& node)
{
    if (node.targetFd->value != 0)
        _result += std::format(" {}", node.targetFd->value);
    _result += node.stripTabs ? "<<-" : "<<";
    _result += node.delimiter;
}

void ASTPrinter::visit(HereString const& node)
{
    if (node.targetFd->value != 0)
        _result += std::format(" {}", node.targetFd->value);
    _result += "<<<";
    node.content->accept(*this);
}

void ASTPrinter::visit(ProgramCall const& node)
{
    _result += std::format("{}", node.program);

    for (auto const& param: node.parameters)
    {
        _result += ' ';
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
}

void ASTPrinter::visit(CallPipeline const& node)
{
    for (size_t i = 0; i < node.calls.size(); ++i)
    {
        if (i > 0)
            _result += " | ";
        node.calls[i]->accept(*this);
    }
}

void ASTPrinter::visit(BuiltinChDirStmt const& node)
{
    _result += "cd";

    if (node.path)
    {
        _result += ' ';
        node.path->accept(*this);
    }
}

void ASTPrinter::visit(BuiltinSetStmt const& node)
{
    _result += "set";

    if (node.name)
    {
        _result += ' ';
        node.name->accept(*this);
    }
    if (node.value)
    {
        _result += ' ';
        node.value->accept(*this);
    }
}

void ASTPrinter::visit(BuiltinExitStmt const& node)
{
    _result += "exit";
    if (node.code)
    {
        _result += ' ';
        node.code->accept(*this);
    }
}

void ASTPrinter::visit(BuiltinExportStmt const& node)
{
    _result += "export " + node.name;
}

void ASTPrinter::visit(BuiltinReadStmt const& node)
{
    _result += "read";
    for (auto const& param: node.parameters)
    {
        _result += ' ';
        param->accept(*this);
    }
}

void ASTPrinter::visit(CompoundStmt const& node)
{
    for (size_t i = 0; i < node.statements.size(); ++i)
    {
        if (i > 0)
            _result += "; ";
        node.statements[i]->accept(*this);
    }
}

void ASTPrinter::visit(WhileStmt const& node)
{
    _result += "while ";
    _result += print(*node.condition);
    _result += " do ";
    _result += print(*node.body);
}

void ASTPrinter::visit(ForInStmt const& node)
{
    _result += "for ";
    _result += pattern::toString(*node.pattern);
    _result += " in ";
    node.source->accept(*this);
    _result += " do ";
    _result += print(*node.body);
}

void ASTPrinter::visit(BreakStmt const& node)
{
    _result += "break";
    if (node.levels > 1)
        _result += " " + std::to_string(node.levels);
}

void ASTPrinter::visit(ContinueStmt const& node)
{
    _result += "continue";
    if (node.levels > 1)
        _result += " " + std::to_string(node.levels);
}

void ASTPrinter::visit(LogicalAndStmt const& node)
{
    node.left->accept(*this);
    _result += " && ";
    node.right->accept(*this);
}

void ASTPrinter::visit(LogicalOrStmt const& node)
{
    node.left->accept(*this);
    _result += " || ";
    node.right->accept(*this);
}

void ASTPrinter::visit(LiteralExpr const& node)
{
    if (node.quoting == LiteralQuoting::SingleQuoted)
        _result += std::format("'{}'", node.value);
    else if (node.quoting == LiteralQuoting::Quoted)
        _result += std::format("\"{}\"", node.value);
    else
        _result += std::format("{}", node.value);
}

void ASTPrinter::visit(StructuredPipelineSourceExpr const& node)
{
    _result += "$(";
    if (node.command)
        node.command->accept(*this);
    _result += " |>)";
}

void ASTPrinter::visit(DataSourceExpr const& node)
{
    switch (node.kind)
    {
        case DataSourceExpr::Kind::OpenJson: _result += "open-json"; break;
        case DataSourceExpr::Kind::OpenCsv: _result += "open-csv"; break;
        case DataSourceExpr::Kind::FromJson: _result += "from-json"; break;
        case DataSourceExpr::Kind::FromCsv: _result += "from-csv"; break;
    }

    if (node.filePath)
    {
        _result += " ";
        node.filePath->accept(*this);
    }
    if (node.pipeSource)
    {
        node.pipeSource->accept(*this);
        _result += " | ";
        switch (node.kind)
        {
            case DataSourceExpr::Kind::FromJson: _result += "from-json"; break;
            case DataSourceExpr::Kind::FromCsv: _result += "from-csv"; break;
            default: break;
        }
    }

    _result += " as ";
    if (!node.typeName.empty())
    {
        _result += node.typeName;
    }
    else
    {
        _result += "{ ";
        for (size_t i = 0; i < node.inlineFields.size(); ++i)
        {
            if (i > 0)
                _result += "; ";
            _result += std::format("{}: {}", node.inlineFields[i].name, toString(node.inlineFields[i].type));
            if (node.inlineFields[i].defaultValue)
            {
                _result += " = ";
                node.inlineFields[i].defaultValue->accept(*this);
            }
        }
        _result += " }";
    }
}

void ASTPrinter::visit(SubstitutionExpr const& node)
{
    _result += node.backtick ? "`" : "$(";
    if (node.pipeline)
        node.pipeline->accept(*this);
    _result += node.backtick ? "`" : ")";
}

void ASTPrinter::visit(CommandFileSubst const& node)
{
    _result += (node.mode == ProcessSubstMode::Read) ? "<(" : ">(";
    if (node.command)
        node.command->accept(*this);
    _result += ')';
}

void ASTPrinter::visit(TildeExpr const& node)
{
    _result += '~';
    _result += node.user;
    _result += node.suffix;
}

void ASTPrinter::visit(GlobExpr const& node)
{
    _result += node.pattern;
}

void ASTPrinter::visit(ConcatExpr const& node)
{
    _result += '"';
    for (auto const& part: node.parts)
        part->accept(*this);
    _result += '"';
}

void ASTPrinter::visit(FStringExpr const& node)
{
    _result += "$\"";
    for (auto const& part: node.parts)
    {
        if (auto const* lit = dynamic_cast<LiteralExpr const*>(part.get()))
        {
            _result += lit->value;
        }
        else
        {
            _result += '{';
            part->accept(*this);
            _result += '}';
        }
    }
    _result += '"';
}

void ASTPrinter::visit(ArithExpansionExpr const& node)
{
    _result += "$((";
    printArithExpr(node.expression.get());
    _result += "))";
}

void ASTPrinter::printArithExpr(ArithExpr const* expr)
{
    if (auto const* lit = dynamic_cast<ArithLiteralExpr const*>(expr))
    {
        _result += std::to_string(lit->value);
    }
    else if (auto const* var = dynamic_cast<ArithVarExpr const*>(expr))
    {
        _result += var->name;
    }
    else if (auto const* binary = dynamic_cast<ArithBinaryExpr const*>(expr))
    {
        _result += '(';
        printArithExpr(binary->left.get());
        _result += ' ';
        switch (binary->op)
        {
            case ArithOp::Add: _result += '+'; break;
            case ArithOp::Sub: _result += '-'; break;
            case ArithOp::Mul: _result += '*'; break;
            case ArithOp::Div: _result += '/'; break;
            case ArithOp::Mod: _result += '%'; break;
            case ArithOp::Pow: _result += "**"; break;
            case ArithOp::Lt: _result += '<'; break;
            case ArithOp::Gt: _result += '>'; break;
            case ArithOp::Le: _result += "<="; break;
            case ArithOp::Ge: _result += ">="; break;
            case ArithOp::Eq: _result += "=="; break;
            case ArithOp::Ne: _result += "!="; break;
            case ArithOp::And: _result += "&&"; break;
            case ArithOp::Or: _result += "||"; break;
            case ArithOp::BitAnd: _result += '&'; break;
            case ArithOp::BitOr: _result += '|'; break;
            case ArithOp::BitXor: _result += '^'; break;
            case ArithOp::Shl: _result += "<<"; break;
            case ArithOp::Shr: _result += ">>"; break;
            default: break;
        }
        _result += ' ';
        printArithExpr(binary->right.get());
        _result += ')';
    }
    else if (auto const* unary = dynamic_cast<ArithUnaryExpr const*>(expr))
    {
        switch (unary->op)
        {
            case ArithOp::Not: _result += '!'; break;
            case ArithOp::Neg: _result += '-'; break;
            case ArithOp::BitNot: _result += '~'; break;
            default: break;
        }
        printArithExpr(unary->operand.get());
    }
}

void ASTPrinter::visit(ParamExpansionExpr const& node)
{
    _result += "${";
    switch (node.op)
    {
        case ParamExpansionOp::Length:
            _result += '#';
            _result += node.variable;
            break;
        case ParamExpansionOp::DefaultValue:
            _result += node.variable;
            _result += ":-";
            _result += node.operand1;
            break;
        case ParamExpansionOp::AlternateValue:
            _result += node.variable;
            _result += ":+";
            _result += node.operand1;
            break;
        case ParamExpansionOp::AssignDefault:
            _result += node.variable;
            _result += ":=";
            _result += node.operand1;
            break;
        case ParamExpansionOp::ErrorIfUnset:
            _result += node.variable;
            _result += ":?";
            _result += node.operand1;
            break;
        case ParamExpansionOp::RemovePrefixShort:
            _result += node.variable;
            _result += '#';
            _result += node.operand1;
            break;
        case ParamExpansionOp::RemovePrefixLong:
            _result += node.variable;
            _result += "##";
            _result += node.operand1;
            break;
        case ParamExpansionOp::RemoveSuffixShort:
            _result += node.variable;
            _result += '%';
            _result += node.operand1;
            break;
        case ParamExpansionOp::RemoveSuffixLong:
            _result += node.variable;
            _result += "%%";
            _result += node.operand1;
            break;
        case ParamExpansionOp::ReplaceFirst:
            _result += node.variable;
            _result += '/';
            _result += node.operand1;
            _result += '/';
            _result += node.operand2;
            break;
        case ParamExpansionOp::ReplaceAll:
            _result += node.variable;
            _result += "//";
            _result += node.operand1;
            _result += '/';
            _result += node.operand2;
            break;
    }
    _result += '}';
}

void ASTPrinter::visit(VariableExpr const& node)
{
    switch (node.type)
    {
        case VariableType::Named:
            if (node.braced)
                _result += std::format("${{{}}}", node.name);
            else
                _result += std::format("${}", node.name);
            break;
        case VariableType::ExitStatus: _result += "$?"; break;
        case VariableType::ProcessId: _result += "$$"; break;
        case VariableType::BackgroundId: _result += "$!"; break;
        case VariableType::Positional: _result += std::format("${}", node.name); break;
    }
}

void ASTPrinter::visit(BuiltinUnsetStmt const& node)
{
    _result += "unset " + node.name;
}

void ASTPrinter::visit(BuiltinJobsStmt const&)
{
    _result += "jobs";
}

void ASTPrinter::visit(BuiltinFgStmt const& node)
{
    _result += "fg";
    if (node.jobId)
    {
        _result += " ";
        node.jobId->accept(*this);
    }
}

void ASTPrinter::visit(BuiltinBgStmt const& node)
{
    _result += "bg";
    if (node.jobId)
    {
        _result += " ";
        node.jobId->accept(*this);
    }
}

void ASTPrinter::visit(BuiltinWaitStmt const& node)
{
    _result += "wait";
    if (node.jobId)
    {
        _result += " ";
        node.jobId->accept(*this);
    }
}

void ASTPrinter::visit(BuiltinBindStmt const& node)
{
    _result += "bind";
    for (auto const& arg: node.args)
    {
        _result += " ";
        arg->accept(*this);
    }
}

void ASTPrinter::visit(BuiltinWhichStmt const& node)
{
    _result += "which";
    for (auto const& arg: node.args)
    {
        _result += " ";
        arg->accept(*this);
    }
}

// ============================================================================
// F# Style Expressions and Statements
// ============================================================================

void ASTPrinter::visit(IfExpr const& node)
{
    printIfExpr(node, "if");
}

void ASTPrinter::printIfExpr(IfExpr const& node, std::string_view keyword)
{
    _result += keyword;
    _result += " ";
    node.condition->accept(*this);
    _result += " then ";
    node.thenExpr->accept(*this);
    if (node.elseExpr)
    {
        if (auto const* nestedIf = dynamic_cast<IfExpr const*>(node.elseExpr.get()))
        {
            _result += " ";
            printIfExpr(*nestedIf, "elif");
        }
        else
        {
            _result += " else ";
            node.elseExpr->accept(*this);
        }
    }
}

void ASTPrinter::visit(TupleExpr const& node)
{
    _result += '(';
    for (size_t i = 0; i < node.elements.size(); ++i)
    {
        if (i > 0)
            _result += ", ";
        node.elements[i]->accept(*this);
    }
    _result += ')';
}

void ASTPrinter::visit(MutAssignStmt const& node)
{
    _result += node.name;
    _result += " <- ";
    node.value->accept(*this);
}

void ASTPrinter::visit(MutAssignExpr const& node)
{
    _result += node.name;
    _result += " <- ";
    node.value->accept(*this);
}

void ASTPrinter::visit(LetBindingStmt const& node)
{
    _result += "let ";
    if (node.isExported)
        _result += "export ";
    if (node.isMutable)
        _result += "mut ";
    if (node.resourceMode == ResourceMode::Use)
        _result += "use ";
    else if (node.resourceMode == ResourceMode::Manual)
        _result += "manual ";
    if (node.isRecursive)
        _result += "rec ";
    if (node.destructurePattern)
        _result += pattern::toString(*node.destructurePattern);
    else
        _result += node.name;
    for (auto const& param: node.parameters)
    {
        _result += ' ';
        if (param.typeAnnotation)
            _result += "(" + param.name + ": " + endo::toString(*param.typeAnnotation) + ")";
        else
            _result += param.name;
    }
    if (node.returnType)
        _result += ": " + endo::toString(*node.returnType);
    if (node.isProperty())
    {
        _result += " with ";
        if (node.getter)
        {
            _result += "get () = ";
            node.getter->body->accept(*this);
        }
        if (node.getter && node.setter)
            _result += " and ";
        if (node.setter)
        {
            _result += "set (" + node.setter->paramName + ") = ";
            node.setter->body->accept(*this);
        }
    }
    else
    {
        _result += " = ";
        if (node.value)
            node.value->accept(*this);
    }

    for (auto const& ab: node.andBindings)
    {
        _result += " and ";
        _result += ab.name;
        for (auto const& param: ab.parameters)
        {
            _result += ' ';
            if (param.typeAnnotation)
                _result += "(" + param.name + ": " + endo::toString(*param.typeAnnotation) + ")";
            else
                _result += param.name;
        }
        if (ab.returnType)
            _result += ": " + endo::toString(*ab.returnType);
        _result += " = ";
        if (ab.value)
            ab.value->accept(*this);
    }
}

void ASTPrinter::visit(LetInExpr const& node)
{
    _result += "let ";
    if (node.resourceMode == ResourceMode::Use)
        _result += "use ";
    else if (node.resourceMode == ResourceMode::Manual)
        _result += "manual ";
    if (node.isRecursive)
        _result += "rec ";
    if (node.destructurePattern)
        _result += pattern::toString(*node.destructurePattern);
    else
        _result += node.name;
    for (auto const& param: node.parameters)
    {
        _result += ' ';
        if (param.typeAnnotation)
            _result += "(" + param.name + ": " + endo::toString(*param.typeAnnotation) + ")";
        else
            _result += param.name;
    }
    if (node.returnType)
        _result += ": " + endo::toString(*node.returnType);
    _result += " = ";
    if (node.value)
        node.value->accept(*this);
    _result += " in ";
    if (node.body)
        node.body->accept(*this);
}

void ASTPrinter::visit(ExprStmt const& node)
{
    if (node.expr)
        node.expr->accept(*this);
}

void ASTPrinter::visit(BinaryExpr const& node)
{
    _result += '(';
    if (node.left)
        node.left->accept(*this);
    _result += ' ';
    switch (node.op)
    {
        case BinaryOp::Add: _result += '+'; break;
        case BinaryOp::Sub: _result += '-'; break;
        case BinaryOp::Mul: _result += '*'; break;
        case BinaryOp::Div: _result += '/'; break;
        case BinaryOp::Mod: _result += '%'; break;
        case BinaryOp::Pow: _result += "**"; break;
        case BinaryOp::Eq: _result += "=="; break;
        case BinaryOp::Ne: _result += "!="; break;
        case BinaryOp::Lt: _result += '<'; break;
        case BinaryOp::Le: _result += "<="; break;
        case BinaryOp::Gt: _result += '>'; break;
        case BinaryOp::Ge: _result += ">="; break;
        case BinaryOp::And: _result += "&&"; break;
        case BinaryOp::Or: _result += "||"; break;
    }
    _result += ' ';
    if (node.right)
        node.right->accept(*this);
    _result += ')';
}

void ASTPrinter::visit(CompositionExpr const& node)
{
    _result += '(';
    if (node.left)
        node.left->accept(*this);
    _result += (node.op == CompositionOp::Forward) ? " >> " : " << ";
    if (node.right)
        node.right->accept(*this);
    _result += ')';
}

void ASTPrinter::visit(PlaceholderLambdaExpr const& node)
{
    // Print as the equivalent desugared lambda for debug output
    _result += "fun __x -> ";
    if (node.body)
        node.body->accept(*this);
}

void ASTPrinter::visit(UnaryExpr const& node)
{
    switch (node.op)
    {
        case UnaryOp::Neg: _result += '-'; break;
        case UnaryOp::Not: _result += '!'; break;
    }
    if (node.operand)
        node.operand->accept(*this);
}

void ASTPrinter::visit(PipelineExpr const& node)
{
    _result += '(';
    if (node.value)
        node.value->accept(*this);
    _result += " |> ";
    if (node.function)
        node.function->accept(*this);
    _result += ')';
}

void ASTPrinter::visit(ApplicationExpr const& node)
{
    _result += '(';
    if (node.function)
        node.function->accept(*this);
    _result += ' ';
    if (node.argument)
        node.argument->accept(*this);
    _result += ')';
}

void ASTPrinter::visit(IdentifierExpr const& node)
{
    _result += node.name;
}

void ASTPrinter::visit(IntLiteralExpr const& node)
{
    _result += std::to_string(node.value);
}

void ASTPrinter::visit(FloatLiteralExpr const& node)
{
    _result += std::format("{}", node.value);
}

void ASTPrinter::visit(BoolLiteralExpr const& node)
{
    _result += node.value ? "true" : "false";
}

void ASTPrinter::visit(SizeLiteralExpr const& node)
{
    constexpr int64_t KB = 1024;
    constexpr int64_t MB = KB * 1024;
    constexpr int64_t GB = MB * 1024;
    constexpr int64_t TB = GB * 1024;
    if (node.bytes >= TB && node.bytes % TB == 0)
        _result += std::to_string(node.bytes / TB) + "TB";
    else if (node.bytes >= GB && node.bytes % GB == 0)
        _result += std::to_string(node.bytes / GB) + "GB";
    else if (node.bytes >= MB && node.bytes % MB == 0)
        _result += std::to_string(node.bytes / MB) + "MB";
    else if (node.bytes >= KB && node.bytes % KB == 0)
        _result += std::to_string(node.bytes / KB) + "KB";
    else
        _result += std::to_string(node.bytes) + "B";
}

void ASTPrinter::visit(TimeSpanLiteralExpr const& node)
{
    constexpr int64_t H = 3600000;
    constexpr int64_t MIN = 60000;
    constexpr int64_t S = 1000;
    if (node.milliseconds >= H && node.milliseconds % H == 0)
        _result += std::to_string(node.milliseconds / H) + "h";
    else if (node.milliseconds >= MIN && node.milliseconds % MIN == 0)
        _result += std::to_string(node.milliseconds / MIN) + "min";
    else if (node.milliseconds >= S && node.milliseconds % S == 0)
        _result += std::to_string(node.milliseconds / S) + "s";
    else
        _result += std::to_string(node.milliseconds) + "ms";
}

void ASTPrinter::visit(BreakExpr const& /*node*/)
{
    _result += "break";
}

void ASTPrinter::visit(ContinueExpr const& /*node*/)
{
    _result += "continue";
}

void ASTPrinter::visit(ParenExpr const& node)
{
    _result += '(';
    if (node.inner)
        node.inner->accept(*this);
    _result += ')';
}

void ASTPrinter::visit(LambdaExpr const& node)
{
    _result += "fun ";
    for (size_t i = 0; i < node.parameters.size(); ++i)
    {
        if (i > 0)
            _result += ' ';
        auto const& param = node.parameters[i];
        if (param.typeAnnotation)
            _result += "(" + param.name + ": " + endo::toString(*param.typeAnnotation) + ")";
        else
            _result += param.name;
    }
    if (node.returnType)
        _result += " : " + endo::toString(*node.returnType);
    _result += " -> ";
    if (node.body)
        node.body->accept(*this);
}

void ASTPrinter::visit(MatchExpr const& node)
{
    _result += "match ";
    if (node.scrutinee)
        node.scrutinee->accept(*this);
    _result += " with";
    for (auto const& arm: node.arms)
    {
        _result += " | ";
        if (arm.pattern)
            _result += pattern::toString(*arm.pattern);
        if (arm.guard)
        {
            _result += " when ";
            arm.guard->accept(*this);
        }
        _result += " -> ";
        if (arm.body)
            arm.body->accept(*this);
    }
}

void ASTPrinter::visit(ListExpr const& node)
{
    _result += '[';
    for (size_t i = 0; i < node.elements.size(); ++i)
    {
        if (i > 0)
            _result += "; ";
        if (node.elements[i])
            node.elements[i]->accept(*this);
    }
    _result += ']';
}

void ASTPrinter::visit(ConsExpr const& node)
{
    _result += '(';
    if (node.head)
        node.head->accept(*this);
    _result += " :: ";
    if (node.tail)
        node.tail->accept(*this);
    _result += ')';
}

void ASTPrinter::visit(ConcatListExpr const& node)
{
    _result += '(';
    if (node.left)
        node.left->accept(*this);
    _result += " @ ";
    if (node.right)
        node.right->accept(*this);
    _result += ')';
}

void ASTPrinter::visit(ListRangeExpr const& node)
{
    _result += '[';
    if (node.start)
        node.start->accept(*this);
    _result += "..";
    if (node.step)
    {
        node.step->accept(*this);
        _result += "..";
    }
    if (node.end)
        node.end->accept(*this);
    _result += ']';
}

void ASTPrinter::visit(ListComprehensionExpr const& node)
{
    _result += "[for ";
    printComprehensionGenerator(node);
    _result += ']';
}

void ASTPrinter::printComprehensionGenerator(ListComprehensionExpr const& node)
{
    _result += node.variable;
    _result += " in ";
    if (node.source)
        node.source->accept(*this);
    if (node.filter)
    {
        _result += " when ";
        node.filter->accept(*this);
    }
    _result += " -> ";
    if (auto const* inner = dynamic_cast<ListComprehensionExpr const*>(node.body.get()))
    {
        _result += "for ";
        printComprehensionGenerator(*inner);
    }
    else if (node.body)
    {
        node.body->accept(*this);
    }
}

void ASTPrinter::visit(ShellCommandExpr const& node)
{
    _result += "& ";
    if (node.command)
        node.command->accept(*this);
}

void ASTPrinter::visit(SplatExpr const& node)
{
    _result += "...";
    _result += node.name;
}

void ASTPrinter::visit(OptionExpr const& node)
{
    if (node.isSome)
    {
        _result += "Some ";
        if (node.value)
            node.value->accept(*this);
    }
    else
    {
        _result += "None";
    }
}

void ASTPrinter::visit(ResultExpr const& node)
{
    if (node.isOk)
    {
        _result += "Ok ";
    }
    else
    {
        _result += "Error ";
    }
    if (node.payload)
        node.payload->accept(*this);
}

void ASTPrinter::visit(TryExpr const& node)
{
    if (node.operand)
        node.operand->accept(*this);
    _result += '?';
}

void ASTPrinter::visit(OptionDefaultExpr const& node)
{
    if (node.option)
        node.option->accept(*this);
    _result += " ?| ";
    if (node.defaultValue)
        node.defaultValue->accept(*this);
}

void ASTPrinter::visit(TryWithExpr const& node)
{
    _result += "try ";
    if (node.body)
        node.body->accept(*this);
    _result += " with";
    for (auto const& arm: node.handlers)
    {
        _result += " | ";
        if (arm.pattern)
            _result += pattern::toString(*arm.pattern);
        if (arm.guard)
        {
            _result += " when ";
            arm.guard->accept(*this);
        }
        _result += " -> ";
        if (arm.body)
            arm.body->accept(*this);
    }
}

void ASTPrinter::visit(TryFinallyExpr const& node)
{
    _result += "try ";
    if (node.body)
        node.body->accept(*this);
    _result += " finally ";
    if (node.finallyExpr)
        node.finallyExpr->accept(*this);
}

void ASTPrinter::visit(LazyExpr const& node)
{
    _result += "lazy ";
    if (node.body)
        node.body->accept(*this);
}

void ASTPrinter::visit(SeqExpr const& node)
{
    _result += "seq { ";
    for (auto i = 0u; i < node.yields.size(); ++i)
    {
        if (i > 0)
            _result += "; ";
        _result += node.yields[i].isSplice ? "yield! " : "yield ";
        if (node.yields[i].value)
            node.yields[i].value->accept(*this);
    }
    _result += " }";
}

void ASTPrinter::visit(UnitExpr const& /*node*/)
{
    _result += "()";
}

void ASTPrinter::visit(BlockExpr const& node)
{
    _result += "{ ";
    for (auto const& stmt: node.statements)
    {
        stmt->accept(*this);
        _result += "; ";
    }
    if (node.result)
        node.result->accept(*this);
    _result += " }";
}

void ASTPrinter::visit(RecordTypeDefStmt const& node)
{
    _result += std::format("type {} = {{ ", node.name);
    for (size_t i = 0; i < node.fields.size(); ++i)
    {
        if (i > 0)
            _result += "; ";
        _result += node.fields[i].name;
        _result += ": ";
        _result += endo::toString(node.fields[i].type);
    }
    _result += " }";
}

void ASTPrinter::visit(RecordExpr const& node)
{
    _result += "{ ";
    for (size_t i = 0; i < node.fields.size(); ++i)
    {
        if (i > 0)
            _result += "; ";
        _result += node.fields[i].name;
        _result += " = ";
        node.fields[i].value->accept(*this);
    }
    _result += " }";
}

void ASTPrinter::visit(RecordUpdateExpr const& node)
{
    _result += "{ ";
    node.base->accept(*this);
    _result += " with ";
    for (size_t i = 0; i < node.updates.size(); ++i)
    {
        if (i > 0)
            _result += "; ";
        _result += node.updates[i].name;
        _result += " = ";
        node.updates[i].value->accept(*this);
    }
    _result += " }";
}

void ASTPrinter::visit(FieldAccessExpr const& node)
{
    node.object->accept(*this);
    _result += ".";
    _result += node.fieldName;
}

void ASTPrinter::visit(OptionalChainExpr const& node)
{
    node.object->accept(*this);
    _result += "?.";
    _result += node.fieldName;
}

void ASTPrinter::visit(UnionTypeDefStmt const& node)
{
    _result += std::format("type {} =", node.name);
    for (auto const& variant: node.variants)
    {
        _result += " | ";
        _result += variant.name;
        if (!variant.payloadTypes.empty())
        {
            _result += " of ";
            for (size_t i = 0; i < variant.payloadTypes.size(); ++i)
            {
                if (i > 0)
                    _result += " * ";
                _result += endo::toString(variant.payloadTypes[i]);
            }
        }
    }
}

void ASTPrinter::visit(UnionConstructorExpr const& node)
{
    _result += node.constructorName;
    for (auto const& arg: node.arguments)
    {
        _result += " ";
        arg->accept(*this);
    }
}

void ASTPrinter::visit(ExecPipelineExpr const& node)
{
    for (size_t i = 0; i < node.commands.size(); ++i)
    {
        if (i > 0)
            _result += " | ";
        _result += "exec ";
        node.commands[i].program->accept(*this);
        for (auto const& arg: node.commands[i].arguments)
        {
            _result += ' ';
            arg->accept(*this);
        }
    }
}

} // namespace endo::ast
