// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/CoreVM.hpp>

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include "AST.hpp"
#include "Visitor.hpp"

namespace endo
{

class PatternIRGenerator; // Forward declaration

/// Persistent state for F# definitions that survives across REPL prompts.
///
/// When running interactively, `let` function definitions (including `let rec`)
/// are stored here so that subsequent prompts can call previously defined functions.
///
/// Owned by the Shell; passed to IRGenerator::generate() for each execution.
struct FSharpPersistentState
{
    /// A persisted function definition (captures are not preserved across prompts).
    struct PersistedFunction
    {
        std::vector<std::string> parameters; ///< Parameter names in order
        ast::Expr const* body;               ///< Function body expression (for inlining)
        bool returnsResultOrOption = false;  ///< Whether function returns Result/Option type
        bool isRecursive = false;            ///< Whether function is declared with `let rec`
    };

    /// Function table persisted across REPL prompts (name -> function metadata).
    std::unordered_map<std::string, PersistedFunction> functions;

    /// AST nodes retained to keep PersistedFunction::body pointers valid.
    std::vector<std::unique_ptr<ast::Statement>> retainedASTs;
};

/// Generates IR code from an AST.
class IRGenerator final: public CoreVM::IRBuilder, public ast::Visitor
{
    friend class PatternIRGenerator; // Allow PatternIRGenerator to access protected IRBuilder methods

  public:
    /// Generates IR code from an AST.
    ///
    /// @param rootNode The root statement of the AST
    /// @param report Diagnostics report for error messages
    /// @param runtime Runtime instance for accessing builtins
    /// @param persistentState Optional persistent state for REPL sessions.
    ///        When non-null, previously defined functions are pre-loaded and
    ///        newly defined functions are stored back for future prompts.
    /// @return The generated IR program, or nullptr if errors occurred
    static std::unique_ptr<CoreVM::IRProgram> generate(ast::Statement const& rootNode,
                                                       CoreVM::diagnostics::Report& report,
                                                       CoreVM::Runtime& runtime,
                                                       FSharpPersistentState* persistentState = nullptr);

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

    // F# style expressions and statements
    void visit(ast::LetBindingStmt const& node) override;
    void visit(ast::ExprStmt const& node) override;
    void visit(ast::BinaryExpr const& node) override;
    void visit(ast::UnaryExpr const& node) override;
    void visit(ast::PipelineExpr const& node) override;
    void visit(ast::ApplicationExpr const& node) override;
    void visit(ast::IdentifierExpr const& node) override;
    void visit(ast::IntLiteralExpr const& node) override;
    void visit(ast::FloatLiteralExpr const& node) override;
    void visit(ast::BoolLiteralExpr const& node) override;
    void visit(ast::ParenExpr const& node) override;
    void visit(ast::LambdaExpr const& node) override;
    void visit(ast::MatchExpr const& node) override;
    void visit(ast::ListExpr const& node) override;
    void visit(ast::ListRangeExpr const& node) override;
    void visit(ast::ListComprehensionExpr const& node) override;
    void visit(ast::ShellCommandExpr const& node) override;
    void visit(ast::OptionExpr const& node) override;
    void visit(ast::ResultExpr const& node) override;
    void visit(ast::TryExpr const& node) override;
    void visit(ast::TryWithExpr const& node) override;

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

    /// Generates IR for builtin print/println call.
    /// @param argument The string expression to print
    /// @param appendNewline If true, appends newline after printing (println)
    void generatePrintCall(ast::Expr const* argument, bool appendNewline);

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

    // Helper for dynamic type comparison
    [[nodiscard]] bool needsDynamicCompare(CoreVM::Value* lhs, CoreVM::Value* rhs) const;

    // F# variable scope management
    struct FSharpScope
    {
        std::unordered_map<std::string, CoreVM::Value*> bindings;
        std::vector<CoreVM::AllocaInstr*>
            objectVariables; ///< Variables holding objects (for ORELEASE at scope exit)
        FSharpScope* parent = nullptr;
    };

    void pushFSharpScope();
    void popFSharpScope();
    void bindFSharpVariable(std::string const& name, CoreVM::Value* value);
    void bindFSharpObjectVariable(std::string const& name, CoreVM::AllocaInstr* storage);
    [[nodiscard]] CoreVM::Value* lookupFSharpVariable(std::string const& name) const;

    // F# function management
    struct FSharpFunction
    {
        std::vector<std::string> parameters; ///< Parameter names in order
        ast::Expr const* body;               ///< Function body expression (for inlining)
        bool returnsResultOrOption = false;  ///< Whether function returns Result/Option type
        bool isRecursive = false;            ///< Whether function is declared with `let rec`
        /// Captured variable bindings from the enclosing scope at function creation time.
        /// Maps variable names to their storage (entry-block allocas).
        std::unordered_map<std::string, CoreVM::Value*> capturedBindings;

        size_t arity() const { return parameters.size(); }
    };

    void registerFSharpFunction(std::string const& name, FSharpFunction func);
    [[nodiscard]] FSharpFunction const* lookupFSharpFunction(std::string const& name) const;

    /// Analyzes a function body to determine if it returns Result or Option type
    [[nodiscard]] bool isBodyResultOrOption(ast::Expr const* body) const;

    /// Collects free variables referenced in @p body that are not in @p boundNames
    /// and are currently accessible in the F# variable scope chain.
    [[nodiscard]] std::unordered_map<std::string, CoreVM::Value*> collectFreeVariables(
        ast::Expr const* body, std::vector<std::string> const& boundNames) const;

    // F# function context for error propagation (? operator)
    // Tracks return block and storage for early returns from try expressions
    struct FSharpFunctionContext
    {
        CoreVM::BasicBlock* returnBlock;    ///< Block to jump to on error propagation
        CoreVM::AllocaInstr* returnStorage; ///< Storage for the return value
        bool returnsResultOrOption;         ///< Whether function returns Result/Option
    };

    void pushFSharpFunctionContext(CoreVM::BasicBlock* returnBlock,
                                   CoreVM::AllocaInstr* returnStorage,
                                   bool returnsResultOrOption);
    void popFSharpFunctionContext();
    [[nodiscard]] FSharpFunctionContext* currentFSharpFunctionContext();

    /// Creates an alloca in the entry block of the current handler.
    /// This ensures allocas are always at the beginning, which is required
    /// for proper stack tracking in the TargetCodeGenerator.
    CoreVM::AllocaInstr* createAllocaInEntryBlock(CoreVM::LiteralType type, std::string const& name);

    CoreVM::diagnostics::Report& _report;
    CoreVM::Runtime& _runtime;
    CoreVM::SourceLocation _currentLocation;
    bool _hasErrors = false;
    CoreVM::Value* _result = nullptr;
    CoreVM::Signature _processCallSignature;

    std::vector<LoopContext> _loopStack;
    int _functionDepth = 0;

    // F# scope chain (owned via raw pointer chain, root scope is unique_ptr)
    std::unique_ptr<FSharpScope> _rootFSharpScope;
    FSharpScope* _currentFSharpScope = nullptr;

    // F# function table (name -> function metadata)
    std::unordered_map<std::string, FSharpFunction> _fsharpFunctions;

    // Lambda counter for generating unique anonymous function names
    size_t _lambdaCounter = 0;
    [[nodiscard]] std::string generateLambdaName();

    // F# function context stack for error propagation
    std::vector<FSharpFunctionContext> _fsharpFunctionContextStack;

    /// Tracks the active recursive function compilation for loop-based tail-call optimization.
    /// When set, recursive calls within the body become jumps back to the entry block.
    struct RecursiveCallContext
    {
        std::string functionName;                       ///< Name of the recursive function
        CoreVM::BasicBlock* entryBlock;                 ///< Loop entry block to jump back to
        std::vector<CoreVM::AllocaInstr*> paramAllocas; ///< Parameter storage for updating on recursion
        CoreVM::AllocaInstr* resultStorage;             ///< Storage for the final result
        CoreVM::BasicBlock* exitBlock;                  ///< Block to continue after recursion completes
    };

    std::optional<RecursiveCallContext> _activeRecursion;
};

} // namespace endo
