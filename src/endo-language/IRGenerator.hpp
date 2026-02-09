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

/// Describes the return type category of a function for auto-wrapping support.
enum class ReturnKind
{
    Plain,  ///< Function returns a plain value (no wrapping needed)
    Result, ///< Function returns a Result type (Ok/Error)
    Option, ///< Function returns an Option type (Some/None)
};

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
        std::vector<std::string> parameters;                ///< Parameter names in order
        std::vector<std::optional<TypePtr>> parameterTypes; ///< Type annotations (parallel to parameters)
        std::optional<TypePtr> returnType;                  ///< Optional return type annotation
        ast::Expr const* body;                              ///< Function body expression (for inlining)
        ReturnKind returnKind = ReturnKind::Plain;          ///< Whether function returns Result/Option type
        bool isRecursive = false;                           ///< Whether function is declared with `let rec`
    };

    /// Function table persisted across REPL prompts (name -> function metadata).
    std::unordered_map<std::string, PersistedFunction> functions;

    /// A persisted value binding (re-evaluated at each prompt).
    struct PersistedValueBinding
    {
        std::string name;
        ast::Expr const* value; ///< Expression AST to re-evaluate each prompt
        bool isMutable;
        bool isObjectExpr; ///< Whether value is Option/Result/Tuple (for ORELEASE)
        CoreVM::LiteralType storageType = CoreVM::LiteralType::Void; ///< IR type of the stored value
    };

    /// Value bindings persisted across REPL prompts, in definition order.
    std::vector<PersistedValueBinding> valueBindings;

    /// Saved runtime values for mutable bindings after each prompt execution.
    /// Maps binding name -> raw VM uint64_t value.
    std::unordered_map<std::string, uint64_t> mutableSnapshots;

    /// AST nodes retained to keep PersistedFunction::body pointers valid.
    std::vector<std::unique_ptr<ast::Statement>> retainedASTs;
};

/// Generates IR code from an AST.
class IRGenerator final: public ast::Visitor
{
  public:
    /// Returns the underlying IR builder.
    [[nodiscard]] CoreVM::IRBuilder& builder() noexcept { return _builder; }

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
    void visit(ast::IfExpr const& node) override;
    void visit(ast::TupleExpr const& node) override;
    void visit(ast::MutAssignStmt const& node) override;
    void visit(ast::LetBindingStmt const& node) override;
    void visit(ast::LetInExpr const& node) override;
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
    void visit(ast::TryFinallyExpr const& node) override;
    void visit(ast::FStringExpr const& node) override;

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

    /// Converts a value to string type for printing.
    /// Handles Number, Float, Boolean, Void/Object, and String types.
    /// @return String-typed value, or nullptr on unsupported types.
    CoreVM::Value* convertToString(CoreVM::Value* value, std::string_view label);

    /// Tries to generate IR for a builtin function call (string_length, etc.).
    /// @return true if the name matched a builtin and code was generated
    bool tryGenerateBuiltinCall(std::string const& name, std::vector<ast::Expr const*> const& argExprs);

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
    struct BindingInfo
    {
        CoreVM::Value* value;
        bool isMutable;
    };

    struct FSharpScope
    {
        std::unordered_map<std::string, BindingInfo> bindings;
        std::vector<CoreVM::AllocaInstr*>
            objectVariables; ///< Variables holding objects (for ORELEASE at scope exit)
        FSharpScope* parent = nullptr;
    };

    void pushFSharpScope();
    void popFSharpScope();
    void bindFSharpVariable(std::string const& name, CoreVM::Value* value, bool isMutable = false);
    void bindFSharpObjectVariable(std::string const& name,
                                  CoreVM::AllocaInstr* storage,
                                  bool isMutable = false);
    [[nodiscard]] CoreVM::Value* lookupFSharpVariable(std::string const& name) const;
    [[nodiscard]] BindingInfo const* lookupFSharpBinding(std::string const& name) const;

    // F# function management
    struct FSharpFunction
    {
        std::vector<std::string> parameters;                ///< Parameter names in order
        std::vector<std::optional<TypePtr>> parameterTypes; ///< Type annotations (parallel to parameters)
        std::optional<TypePtr> returnType;                  ///< Optional return type annotation
        ast::Expr const* body;                              ///< Function body expression (for inlining)
        ReturnKind returnKind = ReturnKind::Plain;          ///< Whether function returns Result/Option type
        bool isRecursive = false;                           ///< Whether function is declared with `let rec`
        /// Names of all functions in the mutual recursion group (empty for non-mutual).
        std::vector<std::string> mutualGroup;
        /// Captured variable bindings from the enclosing scope at function creation time.
        /// Maps variable names to their storage (entry-block allocas).
        std::unordered_map<std::string, CoreVM::Value*> capturedBindings;

        size_t arity() const { return parameters.size(); }
    };

    void registerFSharpFunction(std::string const& name, FSharpFunction func);
    [[nodiscard]] FSharpFunction const* lookupFSharpFunction(std::string const& name) const;

    /// Maps a high-level TypePtr to the corresponding CoreVM::LiteralType for validation.
    [[nodiscard]] static std::optional<CoreVM::LiteralType> mapTypeToLiteralType(TypePtr const& type);

    /// Validates that a value's IR type matches the given type annotation.
    /// Reports a type error on mismatch.
    /// @return true if types match or no annotation given, false on mismatch.
    bool validateTypeAnnotation(TypePtr const& annotated, CoreVM::Value* value, std::string_view context);

    /// Extracts parameter names and type annotations from TypedParameter vectors
    /// into the FSharpFunction fields.
    static void extractTypedParameters(std::vector<ast::TypedParameter> const& typedParams,
                                       FSharpFunction& func);

    /// Analyzes a function body to determine if it returns Result, Option, or plain type.
    [[nodiscard]] ReturnKind determineReturnKind(ast::Expr const* body) const;

    /// Checks if any sub-expression within the given expression is a TryExpr.
    /// Does NOT recurse into lambda bodies (separate functions).
    [[nodiscard]] bool containsTryExpr(ast::Expr const* body) const;

    /// Checks if the body's final expression needs auto-wrapping in Result/Option.
    [[nodiscard]] bool needsAutoWrap(ast::Expr const* body) const;

    /// Emits IR to wrap a raw value in a Result (Ok) or Option (Some) object.
    CoreVM::Value* wrapInResultOrOption(CoreVM::Value* value, ReturnKind kind);

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
        ReturnKind returnKind;              ///< Whether function returns Result/Option
    };

    void pushFSharpFunctionContext(CoreVM::BasicBlock* returnBlock,
                                   CoreVM::AllocaInstr* returnStorage,
                                   ReturnKind returnKind);
    void popFSharpFunctionContext();
    [[nodiscard]] FSharpFunctionContext* currentFSharpFunctionContext();

    /// Creates an alloca in the entry block of the current handler.
    /// This ensures allocas are always at the beginning, which is required
    /// for proper stack tracking in the TargetCodeGenerator.
    CoreVM::AllocaInstr* createAllocaInEntryBlock(CoreVM::LiteralType type, std::string const& name);

    CoreVM::IRBuilder _builder;
    CoreVM::diagnostics::Report& _report;
    CoreVM::Runtime& _runtime;
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

    /// Tracks active mutual recursion compilation with dispatch-loop optimization.
    /// Each function in the group has its own parameter allocas; a dispatch tag
    /// selects which body to execute on each iteration.
    struct MutualRecursionContext
    {
        /// One slot per function in the mutual recursion group.
        struct FunctionSlot
        {
            std::string name;                               ///< Function name
            int dispatchIndex;                              ///< Dispatch tag value for this function
            std::vector<CoreVM::AllocaInstr*> paramAllocas; ///< Parameter storage
        };

        std::vector<FunctionSlot> functions; ///< All functions in the mutual group
        CoreVM::AllocaInstr* dispatchTag;    ///< Dispatch tag storage (selects which body to run)
        CoreVM::BasicBlock* dispatchBlock;   ///< Dispatch loop entry block
        CoreVM::AllocaInstr* resultStorage;  ///< Shared result storage
        CoreVM::BasicBlock* exitBlock;       ///< Exit block after recursion completes

        /// Finds a function slot by name, or nullptr if not found.
        [[nodiscard]] FunctionSlot const* findFunction(std::string const& name) const
        {
            for (auto const& f: functions)
                if (f.name == name)
                    return &f;
            return nullptr;
        }
    };

    std::optional<RecursiveCallContext> _activeRecursion;
    std::optional<MutualRecursionContext> _activeMutualRecursion;

    /// Value bindings created during this codegen pass, to be persisted back.
    std::vector<FSharpPersistentState::PersistedValueBinding> _newValueBindings;
};

} // namespace endo
