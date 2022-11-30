#pragma once

namespace contrair::ast
{

struct FileDescriptor;
struct InputRedirect;
struct OutputRedirect;
struct ProgramCall;
struct CallPipeline;
struct CommandFileSubst;
struct CompoundStmt;
struct IfStmt;

struct Visitor
{
  public:
    virtual ~Visitor() = default;

    virtual void visit(FileDescriptor const&) = 0;
    virtual void visit(InputRedirect const&) = 0;
    virtual void visit(OutputRedirect const&) = 0;
    virtual void visit(ProgramCall const&) = 0;
    virtual void visit(CallPipeline const&) = 0;
    virtual void visit(CommandFileSubst const&) = 0;
    virtual void visit(CompoundStmt const&) = 0;
    virtual void visit(IfStmt const&) = 0;
};

}
