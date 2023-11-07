// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/Visitor.h>

#include <CoreVM/Signature.h>
#include <CoreVM/ir/IRBuilder.h>
#include <CoreVM/ir/IRProgram.h>

namespace endo::ast
{
struct Expr;
struct Node;
struct Statement;
} // namespace endo::ast

namespace endo
{

class IRGenerator final: public CoreVM::IRBuilder, public ast::Visitor
{
  public:
    static CoreVM::IRProgram* generate(ast::Statement const& rootNode);

  private:
    IRGenerator();

    CoreVM::Value* codegen(ast::Node const* node);

    void visit(ast::BuiltinExitStmt const&) override;
    void visit(ast::BuiltinExportStmt const&) override;
    void visit(ast::BuiltinChDirStmt const&) override;
    void visit(ast::BuiltinFalseStmt const&) override;
    void visit(ast::BuiltinReadStmt const&) override;
    void visit(ast::BuiltinTrueStmt const&) override;
    void visit(ast::CallPipeline const&) override;
    void visit(ast::CommandFileSubst const&) override;
    void visit(ast::CompoundStmt const&) override;
    void visit(ast::FileDescriptor const&) override;
    void visit(ast::IfStmt const&) override;
    void visit(ast::InputRedirect const&) override;
    void visit(ast::LiteralExpr const&) override;
    void visit(ast::OutputRedirect const&) override;
    void visit(ast::ProgramCall const&) override;
    void visit(ast::SubstitutionExpr const&) override;
    void visit(ast::WhileStmt const&) override;

    CoreVM::Value* toBool(CoreVM::Value* value);
    std::vector<CoreVM::Constant*> createCallArgs(std::vector<std::unique_ptr<ast::Expr>> const& args);
    std::vector<CoreVM::Constant*> createCallArgs(std::string const& programName,
                                                  std::vector<std::unique_ptr<ast::Expr>> const& args);

    CoreVM::Value* _result = nullptr;
    // CoreVM::NativeCallback _processCallCallback;
    // CoreVM::IRBuiltinFunction* _processCallFunction = nullptr;
    CoreVM::Signature _processCallSignature;
};

} // namespace endo
