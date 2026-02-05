// SPDX-License-Identifier: Apache-2.0
module;
#include <shell/AST.hpp>
#include <shell/Visitor.hpp>

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

    void visit(ForListStmt const& node) override
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

    void visit(ForCStyleStmt const& node) override
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

    void visit(CaseStmt const& node) override
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

    void visit(FunctionDefStmt const& node) override
    {
        _result += "function ";
        _result += node.name;
        _result += "() { ";
        if (node.body)
            _result += print(*node.body);
        _result += " }";
    }

    void visit(BreakStmt const& node) override
    {
        _result += "break";
        if (node.levels > 1)
            _result += " " + std::to_string(node.levels);
    }

    void visit(ContinueStmt const& node) override
    {
        _result += "continue";
        if (node.levels > 1)
            _result += " " + std::to_string(node.levels);
    }

    void visit(ReturnStmt const& node) override
    {
        _result += "return";
        if (node.value)
        {
            _result += " ";
            node.value->accept(*this);
        }
    }

    void visit(LogicalAndStmt const& node) override
    {
        node.left->accept(*this);
        _result += " && ";
        node.right->accept(*this);
    }

    void visit(LogicalOrStmt const& node) override
    {
        node.left->accept(*this);
        _result += " || ";
        node.right->accept(*this);
    }

    void visit(LiteralExpr const& node) override { _result += std::format("{}", node.value); }

    void visit(SubstitutionExpr const& node) override
    {
        _result += node.backtick ? "`" : "$(";
        if (node.pipeline)
            node.pipeline->accept(*this);
        _result += node.backtick ? "`" : ")";
    }

    void visit(CommandFileSubst const& node) override
    {
        _result += (node.mode == ProcessSubstMode::Read) ? "<(" : ">(";
        if (node.command)
            node.command->accept(*this);
        _result += ')';
    }

    void visit(TildeExpr const& node) override
    {
        _result += '~';
        _result += node.user;
        _result += node.suffix;
    }

    void visit(GlobExpr const& node) override { _result += node.pattern; }

    void visit(ArithExpansionExpr const& node) override
    {
        _result += "$((";
        printArithExpr(node.expression.get());
        _result += "))";
    }

    void printArithExpr(ArithExpr const* expr)
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

    void visit(ParamExpansionExpr const& node) override
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
