// SPDX-License-Identifier: Apache-2.0
#include "ASTPrinter.hpp"

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

void ASTPrinter::visit(BuiltinFalseStmt const&)
{
    _result += "false";
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

void ASTPrinter::visit(BuiltinTrueStmt const&)
{
    _result += "true";
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

void ASTPrinter::visit(IfStmt const& node)
{
    _result += "if ";
    _result += print(*node.condition);
    _result += "; ";
    _result += print(*node.thenBlock);
    _result += "; ";
    if (node.elseBlock)
    {
        _result += "else ";
        _result += print(*node.elseBlock);
    }
    _result += "fi";
}

void ASTPrinter::visit(WhileStmt const& node)
{
    _result += "while ";
    _result += print(*node.condition);
    _result += "; ";
    _result += print(*node.body);
    _result += "done";
}

void ASTPrinter::visit(ForListStmt const& node)
{
    _result += "for ";
    _result += node.variable;
    _result += " in ";
    for (size_t i = 0; i < node.items.size(); ++i)
    {
        if (i > 0)
            _result += " ";
        node.items[i]->accept(*this);
    }
    _result += "; do ";
    _result += print(*node.body);
    _result += "; done";
}

void ASTPrinter::visit(ForCStyleStmt const& node)
{
    _result += "for ((";
    if (node.init)
        printArithExpr(node.init.get());
    _result += "; ";
    if (node.condition)
        printArithExpr(node.condition.get());
    _result += "; ";
    if (node.step)
        printArithExpr(node.step.get());
    _result += ")); do ";
    _result += print(*node.body);
    _result += "; done";
}

void ASTPrinter::visit(CaseStmt const& node)
{
    _result += "case ";
    node.word->accept(*this);
    _result += " in ";
    for (auto const& clause: node.clauses)
    {
        for (size_t i = 0; i < clause.patterns.size(); ++i)
        {
            if (i > 0)
                _result += "|";
            _result += clause.patterns[i];
        }
        _result += ") ";
        if (clause.body)
            _result += print(*clause.body);
        _result += ";; ";
    }
    _result += "esac";
}

void ASTPrinter::visit(FunctionDefStmt const& node)
{
    _result += "function ";
    _result += node.name;
    _result += "() { ";
    if (node.body)
        _result += print(*node.body);
    _result += " }";
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

void ASTPrinter::visit(ReturnStmt const& node)
{
    _result += "return";
    if (node.value)
    {
        _result += " ";
        node.value->accept(*this);
    }
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
    _result += std::format("{}", node.value);
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

} // namespace endo::ast
