// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace crush::ast
{

struct BuiltinChDirStmt;
struct BuiltinFalseStmt;
struct BuiltinExitStmt;
struct BuiltinTrueStmt;
struct CallPipeline;
struct CommandFileSubst;
struct CompoundStmt;
struct FileDescriptor;
struct IfStmt;
struct InputRedirect;
struct LiteralExpr;
struct OutputRedirect;
struct ProgramCall;
struct SubstitutionExpr;
struct WhileStmt;

struct Visitor
{
  public:
    virtual ~Visitor() = default;

    // process calling and I/O redirects
    virtual void visit(FileDescriptor const&) = 0;
    virtual void visit(InputRedirect const&) = 0;
    virtual void visit(OutputRedirect const&) = 0;
    virtual void visit(ProgramCall const&) = 0;
    virtual void visit(CallPipeline const&) = 0;

    // flow control
    virtual void visit(CompoundStmt const&) = 0;
    virtual void visit(IfStmt const&) = 0;
    virtual void visit(WhileStmt const&) = 0;

    // builtin statements
    virtual void visit(BuiltinExitStmt const&) = 0;
    virtual void visit(BuiltinTrueStmt const&) = 0;
    virtual void visit(BuiltinFalseStmt const&) = 0;
    virtual void visit(BuiltinChDirStmt const&) = 0;

    // epxressions
    virtual void visit(LiteralExpr const&) = 0;
    virtual void visit(SubstitutionExpr const&) = 0;
    virtual void visit(CommandFileSubst const&) = 0;
};

} // namespace crush::ast