// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/CoreVM.hpp>

#include <memory>

#include "AST.hpp"
#include "Visitor.hpp"

namespace endo
{

/// Generates IR code from an AST.
class IRGenerator final: public CoreVM::IRBuilder, public ast::Visitor
{
  public:
    /// Generates IR code from an AST.
    ///
    /// @param rootNode The root statement of the AST
    /// @param report Diagnostics report for error messages
    /// @param runtime Runtime instance for accessing builtins
    /// @return The generated IR program, or nullptr if errors occurred
    static std::unique_ptr<CoreVM::IRProgram> generate(ast::Statement const& rootNode,
                                                       CoreVM::diagnostics::Report& report,
                                                       CoreVM::Runtime& runtime);

  private:
    explicit IRGenerator(CoreVM::diagnostics::Report& report, CoreVM::Runtime& runtime);

    /// Finds a builtin function by its signature string.
    [[nodiscard]] CoreVM::NativeCallback* findCallback(std::string const& signature) const;

    CoreVM::Value* codegen(ast::Node const* node);

    /// Reports a type error at the current location.
    template <typename... Args>
    void reportTypeError(std::format_string<Args...> f, Args&&... args);

    void visit(ast::BuiltinExitStmt const& node) override;
    void visit(ast::BuiltinExportStmt const& node) override;
    void visit(ast::BuiltinChDirStmt const& node) override;
    void visit(ast::BuiltinSetStmt const& node) override;
    void visit(ast::BuiltinFalseStmt const& node) override;
    void visit(ast::BuiltinReadStmt const& node) override;
    void visit(ast::BuiltinTrueStmt const& node) override;
    void visit(ast::CallPipeline const& node) override;
    void visit(ast::CommandFileSubst const& node) override;
    void visit(ast::CompoundStmt const& node) override;
    void visit(ast::FileDescriptor const& node) override;
    void visit(ast::IfStmt const& node) override;
    void visit(ast::LogicalAndStmt const& node) override;
    void visit(ast::LogicalOrStmt const& node) override;
    void visit(ast::InputRedirect const& node) override;
    void visit(ast::HereDocument const& node) override;
    void visit(ast::HereString const& node) override;
    void visit(ast::LiteralExpr const& node) override;
    void visit(ast::TildeExpr const& node) override;
    void visit(ast::GlobExpr const& node) override;
    void visit(ast::ConcatExpr const& node) override;
    void visit(ast::ArithExpansionExpr const& node) override;
    void visit(ast::ParamExpansionExpr const& node) override;
    void visit(ast::VariableExpr const& node) override;
    void visit(ast::BuiltinUnsetStmt const& node) override;
    void visit(ast::BuiltinJobsStmt const& node) override;
    void visit(ast::BuiltinFgStmt const& node) override;
    void visit(ast::BuiltinBgStmt const& node) override;
    void visit(ast::BuiltinWaitStmt const& node) override;
    void visit(ast::BuiltinBindStmt const& node) override;
    void visit(ast::BuiltinWhichStmt const& node) override;
    void visit(ast::OutputRedirect const& node) override;
    void visit(ast::ProgramCall const& node) override;
    void visit(ast::SubstitutionExpr const& node) override;
    void visit(ast::WhileStmt const& node) override;
    void visit(ast::ForListStmt const& node) override;
    void visit(ast::ForCStyleStmt const& node) override;
    void visit(ast::CaseStmt const& node) override;
    void visit(ast::FunctionDefStmt const& node) override;
    void visit(ast::BreakStmt const& node) override;
    void visit(ast::ContinueStmt const& node) override;
    void visit(ast::ReturnStmt const& node) override;

    /// Generates code for an arithmetic expression, returning an integer value.
    CoreVM::Value* codegenArith(ast::ArithExpr const* expr);

    /// Converts a value to a boolean for conditional branching.
    CoreVM::Value* toBool(CoreVM::Value* value);

    /// Checks if any expression in the list contains a runtime-evaluated expression.
    [[nodiscard]] bool containsRuntimeExpr(std::vector<std::unique_ptr<ast::Expr>> const& expressions) const;

    std::vector<CoreVM::Constant*> createConstantArray(
        std::vector<std::unique_ptr<ast::Expr>> const& expressions);

    /// Builds command arguments using the command builder builtins.
    void buildCommandArgs(std::string const& programName,
                          std::vector<std::unique_ptr<ast::Expr>> const& args);

    /// Executes the built command (non-piped version).
    CoreVM::Value* execBuiltCommand();

    /// Executes the built command (piped version).
    CoreVM::Value* execBuiltCommandPiped(bool lastInChain);

    /// Executes the built command in the background (job control).
    CoreVM::Value* execBuiltCommandPipedBackground(std::string const& programName,
                                                   std::vector<std::unique_ptr<ast::Expr>> const& args);

    std::vector<CoreVM::Constant*> createCallArgs(std::vector<std::unique_ptr<ast::Expr>> const& args);

    std::vector<CoreVM::Constant*> createCallArgs(std::string const& programName,
                                                  std::vector<std::unique_ptr<ast::Expr>> const& args);

    // Loop context management for break/continue
    struct LoopContext
    {
        CoreVM::BasicBlock* continueTarget;
        CoreVM::BasicBlock* breakTarget;
    };

    void pushLoopContext(CoreVM::BasicBlock* continueTarget, CoreVM::BasicBlock* breakTarget);
    void popLoopContext();
    [[nodiscard]] LoopContext* getLoopContext(int levels = 1);

    // Function context management for return
    void pushFunctionContext();
    void popFunctionContext();
    [[nodiscard]] bool inFunction() const;

    CoreVM::diagnostics::Report& _report;
    CoreVM::Runtime& _runtime;
    CoreVM::SourceLocation _currentLocation;
    bool _hasErrors = false;
    CoreVM::Value* _result = nullptr;
    CoreVM::Signature _processCallSignature;

    std::vector<LoopContext> _loopStack;
    int _functionDepth = 0;
};

} // namespace endo
