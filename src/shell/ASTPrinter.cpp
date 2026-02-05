// SPDX-License-Identifier: Apache-2.0
module;
#include <shell/AST.h>
#include <shell/Visitor.h>

#include <crispy/assert.h>

#include <format>

import Lexer;

export module ASTPrinter;

namespace endo::ast
{

export class ASTPrinter: public Visitor
{
  private:
    std::string _result;

  public:
    static std::string print(Node const& node)
    {
        ASTPrinter printer;
        node.accept(printer);
        return printer._result;
    }

    void visit(FileDescriptor const& node) override { _result += std::format("{}", node.value); }

    void visit(InputRedirect const& node) override
    {
        if (node.targetFd->value != 0)
            _result += std::format(" {}<", node.targetFd->value);
        else
            _result += " <";
        node.source->accept(*this);
    }

    void visit(OutputRedirect const& node) override
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

    void visit(HereDocument const& node) override
    {
        if (node.targetFd->value != 0)
            _result += std::format(" {}", node.targetFd->value);
        _result += node.stripTabs ? "<<-" : "<<";
        _result += node.delimiter;
    }

    void visit(HereString const& node) override
    {
        if (node.targetFd->value != 0)
            _result += std::format(" {}", node.targetFd->value);
        _result += "<<<";
        node.content->accept(*this);
    }

    void visit(ProgramCall const& node) override
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

    void visit(CallPipeline const& node) override
    {
        for (size_t i = 0; i < node.calls.size(); ++i)
        {
            if (i > 0)
                _result += " | ";
            node.calls[i]->accept(*this);
        }
    }

    void visit(BuiltinChDirStmt const& node) override
    {
        _result += "cd";

        if (node.path)
        {
            _result += ' ';
            node.path->accept(*this);
        }
    }

    void visit(BuiltinSetStmt const& node) override
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

    void visit(BuiltinExitStmt const& node) override
    {
        _result += "exit";
        if (node.code)
        {
            _result += ' ';
            node.code->accept(*this);
        }
    }

    void visit(BuiltinExportStmt const& node) override { _result += "export " + node.name; }

    void visit(BuiltinFalseStmt const&) override { _result += "false"; }

    void visit(BuiltinReadStmt const& node) override
    {
        _result += "read";
        for (auto const& param: node.parameters)
        {
            _result += ' ';
            param->accept(*this);
        }
    }

    void visit(BuiltinTrueStmt const&) override { _result += "true"; }

    void visit(CompoundStmt const& node) override
    {
        for (size_t i = 0; i < node.statements.size(); ++i)
        {
            if (i > 0)
                _result += "; ";
            node.statements[i]->accept(*this);
        }
    }

    void visit(IfStmt const& node) override
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

    void visit(WhileStmt const& node) override
    {
        _result += "while ";
        _result += print(*node.condition);
        _result += "; ";
        _result += print(*node.body);
        _result += "done";
    }

    void visit(LiteralExpr const& node) override { _result += std::format("{}", node.value); }

    void visit(SubstitutionExpr const& node) override { (void) node; }

    void visit(CommandFileSubst const& node) override { (void) node; }

    void visit(VariableExpr const& node) override
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

    void visit(BuiltinUnsetStmt const& node) override { _result += "unset " + node.name; }
};

} // namespace endo::ast
