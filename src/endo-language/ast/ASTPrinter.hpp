// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <endo-language/ast/AST.hpp>
#include <endo-language/ast/Visitor.hpp>

#include <format>
#include <string>

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
    void visit(BuiltinReadStmt const& node) override;
    void visit(CompoundStmt const& node) override;
    void visit(WhileStmt const& node) override;
    void visit(ForInStmt const& node) override;
    void visit(BreakStmt const& node) override;
    void visit(ContinueStmt const& node) override;
    void visit(LogicalAndStmt const& node) override;
    void visit(LogicalOrStmt const& node) override;
    void visit(LiteralExpr const& node) override;
    void visit(StructuredPipelineSourceExpr const& node) override;
    void visit(DataSourceExpr const& node) override;
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
    void visit(IfExpr const& node) override;
    void visit(TupleExpr const& node) override;
    void visit(MutAssignStmt const& node) override;
    void visit(MutAssignExpr const& node) override;
    void visit(LetBindingStmt const& node) override;
    void visit(LetInExpr const& node) override;
    void visit(ExprStmt const& node) override;
    void visit(BinaryExpr const& node) override;
    void visit(UnaryExpr const& node) override;
    void visit(PipelineExpr const& node) override;
    void visit(ApplicationExpr const& node) override;
    void visit(IdentifierExpr const& node) override;
    void visit(IntLiteralExpr const& node) override;
    void visit(FloatLiteralExpr const& node) override;
    void visit(BoolLiteralExpr const& node) override;
    void visit(SizeLiteralExpr const& node) override;
    void visit(BreakExpr const& node) override;
    void visit(ContinueExpr const& node) override;
    void visit(ParenExpr const& node) override;
    void visit(LambdaExpr const& node) override;
    void visit(MatchExpr const& node) override;
    void visit(ListExpr const& node) override;
    void visit(ConsExpr const& node) override;
    void visit(ConcatListExpr const& node) override;
    void visit(ListRangeExpr const& node) override;
    void visit(ListComprehensionExpr const& node) override;
    void visit(ShellCommandExpr const& node) override;
    void visit(SplatExpr const& node) override;
    void visit(OptionExpr const& node) override;
    void visit(ResultExpr const& node) override;
    void visit(TryExpr const& node) override;
    void visit(OptionDefaultExpr const& node) override;
    void visit(TryWithExpr const& node) override;
    void visit(TryFinallyExpr const& node) override;
    void visit(FStringExpr const& node) override;
    void visit(UnitExpr const& node) override;
    void visit(BlockExpr const& node) override;
    void visit(RecordTypeDefStmt const& node) override;
    void visit(RecordExpr const& node) override;
    void visit(RecordUpdateExpr const& node) override;
    void visit(FieldAccessExpr const& node) override;
    void visit(OptionalChainExpr const& node) override;
    void visit(UnionTypeDefStmt const& node) override;
    void visit(UnionConstructorExpr const& node) override;
    void visit(ExecPipelineExpr const& node) override;

  private:
    void printIfExpr(IfExpr const& node, std::string_view keyword);
    void printComprehensionGenerator(ListComprehensionExpr const& node);
    void printArithExpr(ArithExpr const* expr);
};

} // namespace endo::ast
