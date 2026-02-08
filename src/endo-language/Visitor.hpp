// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace endo::ast
{

struct ArithExpansionExpr;
struct BreakStmt;
struct BuiltinChDirStmt;
struct BuiltinSetStmt;
struct BuiltinExportStmt;
struct BuiltinFalseStmt;
struct BuiltinExitStmt;
struct BuiltinReadStmt;
struct BuiltinTrueStmt;
struct BuiltinUnsetStmt;
struct BuiltinJobsStmt;
struct BuiltinFgStmt;
struct BuiltinBgStmt;
struct BuiltinWaitStmt;
struct BuiltinBindStmt;
struct BuiltinWhichStmt;
struct CallPipeline;
struct CaseStmt;
struct CommandFileSubst;
struct CompoundStmt;
struct ConcatExpr;
struct ContinueStmt;
struct FileDescriptor;
struct ForCStyleStmt;
struct ForListStmt;
struct FunctionDefStmt;
struct GlobExpr;
struct HereDocument;
struct HereString;
struct IfStmt;
struct InputRedirect;
struct LiteralExpr;
struct LogicalAndStmt;
struct LogicalOrStmt;
struct OutputRedirect;
struct ParamExpansionExpr;
struct ProgramCall;
struct ReturnStmt;
struct SubstitutionExpr;
struct TildeExpr;
struct VariableExpr;
struct WhileStmt;

// F# style expressions and statements
struct IfExpr;
struct TupleExpr;
struct MutAssignStmt;
struct LetBindingStmt;
struct LetInExpr;
struct ExprStmt;
struct BinaryExpr;
struct UnaryExpr;
struct PipelineExpr;
struct ApplicationExpr;
struct IdentifierExpr;
struct IntLiteralExpr;
struct FloatLiteralExpr;
struct BoolLiteralExpr;
struct ParenExpr;
struct LambdaExpr;
struct MatchExpr;
struct ListExpr;
struct ListRangeExpr;
struct ListComprehensionExpr;
struct ShellCommandExpr;
struct OptionExpr;
struct ResultExpr;
struct TryExpr;
struct TryWithExpr;

struct Visitor
{
  public:
    virtual ~Visitor() = default;

    // process calling and I/O redirects
    virtual void visit(FileDescriptor const&) = 0;
    virtual void visit(InputRedirect const&) = 0;
    virtual void visit(OutputRedirect const&) = 0;
    virtual void visit(HereDocument const&) = 0;
    virtual void visit(HereString const&) = 0;
    virtual void visit(ProgramCall const&) = 0;
    virtual void visit(CallPipeline const&) = 0;

    // flow control
    virtual void visit(BreakStmt const&) = 0;
    virtual void visit(CaseStmt const&) = 0;
    virtual void visit(CompoundStmt const&) = 0;
    virtual void visit(ContinueStmt const&) = 0;
    virtual void visit(ForCStyleStmt const&) = 0;
    virtual void visit(ForListStmt const&) = 0;
    virtual void visit(FunctionDefStmt const&) = 0;
    virtual void visit(IfStmt const&) = 0;
    virtual void visit(LogicalAndStmt const&) = 0;
    virtual void visit(LogicalOrStmt const&) = 0;
    virtual void visit(ReturnStmt const&) = 0;
    virtual void visit(WhileStmt const&) = 0;

    // builtin statements
    virtual void visit(BuiltinExitStmt const&) = 0;
    virtual void visit(BuiltinExportStmt const&) = 0;
    virtual void visit(BuiltinTrueStmt const&) = 0;
    virtual void visit(BuiltinFalseStmt const&) = 0;
    virtual void visit(BuiltinReadStmt const&) = 0;
    virtual void visit(BuiltinChDirStmt const&) = 0;
    virtual void visit(BuiltinSetStmt const&) = 0;
    virtual void visit(BuiltinUnsetStmt const&) = 0;
    virtual void visit(BuiltinJobsStmt const&) = 0;
    virtual void visit(BuiltinFgStmt const&) = 0;
    virtual void visit(BuiltinBgStmt const&) = 0;
    virtual void visit(BuiltinWaitStmt const&) = 0;
    virtual void visit(BuiltinBindStmt const&) = 0;
    virtual void visit(BuiltinWhichStmt const&) = 0;

    // expressions
    virtual void visit(ArithExpansionExpr const&) = 0;
    virtual void visit(ConcatExpr const&) = 0;
    virtual void visit(GlobExpr const&) = 0;
    virtual void visit(LiteralExpr const&) = 0;
    virtual void visit(ParamExpansionExpr const&) = 0;
    virtual void visit(SubstitutionExpr const&) = 0;
    virtual void visit(CommandFileSubst const&) = 0;
    virtual void visit(TildeExpr const&) = 0;
    virtual void visit(VariableExpr const&) = 0;

    // F# style expressions and statements
    virtual void visit(IfExpr const&) = 0;
    virtual void visit(TupleExpr const&) = 0;
    virtual void visit(MutAssignStmt const&) = 0;
    virtual void visit(LetBindingStmt const&) = 0;
    virtual void visit(LetInExpr const&) = 0;
    virtual void visit(ExprStmt const&) = 0;
    virtual void visit(BinaryExpr const&) = 0;
    virtual void visit(UnaryExpr const&) = 0;
    virtual void visit(PipelineExpr const&) = 0;
    virtual void visit(ApplicationExpr const&) = 0;
    virtual void visit(IdentifierExpr const&) = 0;
    virtual void visit(IntLiteralExpr const&) = 0;
    virtual void visit(FloatLiteralExpr const&) = 0;
    virtual void visit(BoolLiteralExpr const&) = 0;
    virtual void visit(ParenExpr const&) = 0;
    virtual void visit(LambdaExpr const&) = 0;
    virtual void visit(MatchExpr const&) = 0;
    virtual void visit(ListExpr const&) = 0;
    virtual void visit(ListRangeExpr const&) = 0;
    virtual void visit(ListComprehensionExpr const&) = 0;
    virtual void visit(ShellCommandExpr const&) = 0;
    virtual void visit(OptionExpr const&) = 0;
    virtual void visit(ResultExpr const&) = 0;
    virtual void visit(TryExpr const&) = 0;
    virtual void visit(TryWithExpr const&) = 0;
};

} // namespace endo::ast
