// SPDX-License-Identifier: Apache-2.0
#include <shell/ASTPrinter.h>

#include <crispy/assert.h>

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
    _result += fmt::format("{}", node.value);
}

void ASTPrinter::visit(InputRedirect const& node)
{
    crispy::ignore_unused(node);
}

void ASTPrinter::visit(OutputRedirect const& node)
{
    if (std::holds_alternative<std::unique_ptr<LiteralExpr>>(node.target))
    {
        _result += fmt::format(
            " {}>{}", node.source->value, std::get<std::unique_ptr<LiteralExpr>>(node.target)->value);
    }
    else
        _result += fmt::format(
            " {}>&{}", node.source->value, std::get<std::unique_ptr<FileDescriptor>>(node.target)->value);
}

void ASTPrinter::visit(ProgramCall const& node)
{
    _result += fmt::format("{}", node.program);

    for (auto const& param: node.parameters)
    {
        _result += ' ';
        param->accept(*this);
    }

    for (auto const& redirect: node.outputRedirects)
    {
        redirect->accept(*this);
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

void ASTPrinter::visit(BuiltinTrueStmt const&)
{
    _result += "true";
}

void ASTPrinter::visit(BuiltinFalseStmt const&)
{
    _result += "false";
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

void ASTPrinter::visit(CallPipeline const& node)
{
    for (size_t i = 0; i < node.calls.size(); ++i)
    {
        if (i > 0)
            _result += " | ";
        node.calls[i]->accept(*this);
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

void ASTPrinter::visit(LiteralExpr const& node)
{
    _result += fmt::format("{}", node.value);
}

void ASTPrinter::visit(SubstitutionExpr const& node)
{
    crispy::ignore_unused(node);
}

void ASTPrinter::visit(CommandFileSubst const& node)
{
    crispy::ignore_unused(node);
}

} // namespace endo::ast
