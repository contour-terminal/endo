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
struct CallPipeline;
struct CaseStmt;
struct CommandFileSubst;
struct CompoundStmt;
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

    // expressions
    virtual void visit(ArithExpansionExpr const&) = 0;
    virtual void visit(GlobExpr const&) = 0;
    virtual void visit(LiteralExpr const&) = 0;
    virtual void visit(ParamExpansionExpr const&) = 0;
    virtual void visit(SubstitutionExpr const&) = 0;
    virtual void visit(CommandFileSubst const&) = 0;
    virtual void visit(TildeExpr const&) = 0;
    virtual void visit(VariableExpr const&) = 0;
};

} // namespace endo::ast
