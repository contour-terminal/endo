// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <format>
#include <string>

#include "AST.hpp"
#include "Visitor.hpp"

namespace endo::ast
{

class ASTPrinter: public Visitor
{
  private:
    std::string _result;

  public:
    static std::string print(Node const& node);

    void visit(FileDescriptor const& node) override;
    void visit(InputRedirect const& node) override;
    void visit(OutputRedirect const& node) override;
    void visit(HereDocument const& node) override;
    void visit(HereString const& node) override;
    void visit(ProgramCall const& node) override;
    void visit(CallPipeline const& node) override;
    void visit(BuiltinChDirStmt const& node) override;
    void visit(BuiltinSetStmt const& node) override;
    void visit(BuiltinExitStmt const& node) override;
    void visit(BuiltinExportStmt const& node) override;
    void visit(BuiltinFalseStmt const& node) override;
    void visit(BuiltinReadStmt const& node) override;
    void visit(BuiltinTrueStmt const& node) override;
    void visit(CompoundStmt const& node) override;
    void visit(IfStmt const& node) override;
    void visit(WhileStmt const& node) override;
    void visit(ForListStmt const& node) override;
    void visit(ForCStyleStmt const& node) override;
    void visit(CaseStmt const& node) override;
    void visit(FunctionDefStmt const& node) override;
    void visit(BreakStmt const& node) override;
    void visit(ContinueStmt const& node) override;
    void visit(ReturnStmt const& node) override;
    void visit(LogicalAndStmt const& node) override;
    void visit(LogicalOrStmt const& node) override;
    void visit(LiteralExpr const& node) override;
    void visit(SubstitutionExpr const& node) override;
    void visit(CommandFileSubst const& node) override;
    void visit(TildeExpr const& node) override;
    void visit(GlobExpr const& node) override;
    void visit(ConcatExpr const& node) override;
    void visit(ArithExpansionExpr const& node) override;
    void visit(ParamExpansionExpr const& node) override;
    void visit(VariableExpr const& node) override;
    void visit(BuiltinUnsetStmt const& node) override;
    void visit(BuiltinJobsStmt const& node) override;
    void visit(BuiltinFgStmt const& node) override;
    void visit(BuiltinBgStmt const& node) override;
    void visit(BuiltinWaitStmt const& node) override;
    void visit(BuiltinBindStmt const& node) override;
    void visit(BuiltinWhichStmt const& node) override;

    // F# style expressions and statements
    void visit(LetBindingStmt const& node) override;
    void visit(BinaryExpr const& node) override;
    void visit(UnaryExpr const& node) override;
    void visit(PipelineExpr const& node) override;
    void visit(ApplicationExpr const& node) override;
    void visit(IdentifierExpr const& node) override;
    void visit(IntLiteralExpr const& node) override;
    void visit(FloatLiteralExpr const& node) override;
    void visit(BoolLiteralExpr const& node) override;
    void visit(ParenExpr const& node) override;
    void visit(LambdaExpr const& node) override;
    void visit(MatchExpr const& node) override;
    void visit(ListExpr const& node) override;
    void visit(ListRangeExpr const& node) override;
    void visit(ListComprehensionExpr const& node) override;

  private:
    void printArithExpr(ArithExpr const* expr);
};

} // namespace endo::ast
