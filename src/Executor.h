#pragma once

#include "AST.h"
#include "Visitor.h"

namespace contrair
{

class Environment
{
  public:
    virtual ~Environment() = default;

    virtual void set(std::string_view name, std::string_view value) = 0;
    [[nodiscard]] virtual std::string_view get(std::string_view name) const = 0;
};

class Executor: public ast::Visitor
{
  public:
    explicit Executor(Environment& env);

    void visit(ast::FileDescriptor const&) override;
    void visit(ast::InputRedirect const&) override;
    void visit(ast::OutputRedirect const&) override;
    void visit(ast::ProgramCall const&) override;
    void visit(ast::CallPipeline const&) override;
    void visit(ast::CommandFileSubst const&) override;
    void visit(ast::CompoundStmt const&) override;
    void visit(ast::IfStmt const&) override;
};

} // namespace contrair
