// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/AST.h>
#include <shell/Visitor.h>

namespace crush::ast
{

class ASTPrinter: public Visitor
{
  private:
    std::string _result;

  public:
    static std::string print(Node const& node);

    void visit(FileDescriptor const&) override;
    void visit(InputRedirect const&) override;
    void visit(OutputRedirect const&) override;
    void visit(ProgramCall const&) override;
    void visit(CallPipeline const&) override;

    void visit(BuiltinChDirStmt const&) override;
    void visit(BuiltinExitStmt const&) override;
    void visit(BuiltinFalseStmt const&) override;
    void visit(BuiltinReadStmt const&) override;
    void visit(BuiltinTrueStmt const&) override;

    void visit(CompoundStmt const&) override;
    void visit(IfStmt const&) override;
    void visit(WhileStmt const&) override;

    void visit(LiteralExpr const&) override;
    void visit(SubstitutionExpr const&) override;
    void visit(CommandFileSubst const&) override;
};

} // namespace crush::ast
