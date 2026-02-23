// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <endo-language/ast/AST.hpp>
#include <endo-language/ast/Visitor.hpp>
#include <endo-language/ide/CompletionItem.hpp>
#include <endo-language/types/TypeInferencer.hpp>

#include <CoreVM/CoreVM.hpp>

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>

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
        bool hasVariadicParam = false;                      ///< True if last param is variadic (...args)
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
        bool isExported = false; ///< Whether binding is exported as environment variable
    };

    /// Value bindings persisted across REPL prompts, in definition order.
    std::vector<PersistedValueBinding> valueBindings;

    /// Saved runtime values for mutable bindings after each prompt execution.
    /// Maps binding name -> raw VM uint64_t value.
    std::unordered_map<std::string, uint64_t> mutableSnapshots;

    /// AST nodes retained to keep PersistedFunction::body pointers valid.
    std::vector<std::unique_ptr<ast::Statement>> retainedASTs;

    /// Record type field info for completion (type name -> field info with types).
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> recordTypeFields;

    /// Output definition record type metadata (type name -> fields + ID).
    struct OutputDefRecordType
    {
        uint16_t typeId;
        std::vector<CoreVM::FieldInfo> fields;
    };

    std::unordered_map<std::string, OutputDefRecordType> outputDefinitionTypes;

    /// Structured command name -> metadata (for IRGenerator lookup).
    struct StructuredCommandInfo
    {
        std::string builtinCallbackName; ///< e.g., "structured_docker_ps"
        uint16_t recordTypeId;           ///< List element type ID
        std::string recordTypeName;      ///< For _recordTypes lookup
    };

    /// Key format: "docker\0ps" (command + NUL + args joined by NUL)
    std::unordered_map<std::string, StructuredCommandInfo> structuredCommands;

    /// A persisted property definition with get/set accessor AST pointers.
    struct PersistedProperty
    {
        ast::PropertyAccessor const* getter = nullptr; ///< Getter body (for read access)
        ast::PropertyAccessor const* setter = nullptr; ///< Setter body (for write access)
    };

    /// Properties persisted across REPL prompts (name -> accessor metadata).
    std::unordered_map<std::string, PersistedProperty> properties;
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

    /// Reports a type error with suggestions for fixing at the current location.
    template <typename... Args>
    void reportTypeErrorWithSuggestions(std::vector<std::string> suggestions,
                                        std::format_string<Args...> f,
                                        Args&&... args);

    void visit(ast::BuiltinExitStmt const& node) override;
    void visit(ast::BuiltinExportStmt const& node) override;
    void visit(ast::BuiltinChDirStmt const& node) override;
    void visit(ast::BuiltinSetStmt const& node) override;
    void visit(ast::BuiltinReadStmt const& node) override;
    void visit(ast::CallPipeline const& node) override;
    void visit(ast::CommandFileSubst const& node) override;
    void visit(ast::CompoundStmt const& node) override;
    void visit(ast::FileDescriptor const& node) override;
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
    void visit(ast::StructuredPipelineSourceExpr const& node) override;
    void visit(ast::DataSourceExpr const& node) override;
    void visit(ast::SubstitutionExpr const& node) override;
    void visit(ast::WhileStmt const& node) override;
    void visit(ast::ForInStmt const& node) override;
    void visit(ast::BreakStmt const& node) override;
    void visit(ast::ContinueStmt const& node) override;

    // F# style expressions and statements
    void visit(ast::IfExpr const& node) override;
    void visit(ast::TupleExpr const& node) override;
    void visit(ast::MutAssignStmt const& node) override;
    void visit(ast::MutAssignExpr const& node) override;
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
    void visit(ast::BreakExpr const& node) override;
    void visit(ast::ContinueExpr const& node) override;
    void visit(ast::ParenExpr const& node) override;
    void visit(ast::LambdaExpr const& node) override;
    void visit(ast::MatchExpr const& node) override;
    void visit(ast::ListExpr const& node) override;
    void visit(ast::ConsExpr const& node) override;
    void visit(ast::ConcatListExpr const& node) override;
    void visit(ast::ListRangeExpr const& node) override;
    void visit(ast::ListComprehensionExpr const& node) override;
    void visit(ast::ShellCommandExpr const& node) override;
    void visit(ast::SplatExpr const& node) override;
    void visit(ast::OptionExpr const& node) override;
    void visit(ast::ResultExpr const& node) override;
    void visit(ast::TryExpr const& node) override;
    void visit(ast::OptionDefaultExpr const& node) override;
    void visit(ast::TryWithExpr const& node) override;
    void visit(ast::TryFinallyExpr const& node) override;
    void visit(ast::FStringExpr const& node) override;
    void visit(ast::UnitExpr const& node) override;
    void visit(ast::BlockExpr const& node) override;
    void visit(ast::RecordTypeDefStmt const& node) override;
    void visit(ast::RecordExpr const& node) override;
    void visit(ast::RecordUpdateExpr const& node) override;
    void visit(ast::FieldAccessExpr const& node) override;
    void visit(ast::OptionalChainExpr const& node) override;
    void visit(ast::UnionTypeDefStmt const& node) override;
    void visit(ast::UnionConstructorExpr const& node) override;
    void visit(ast::ExecPipelineExpr const& node) override;

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

    /// Builds command arguments using a runtime-evaluated program name expression.
    void buildCommandArgs(CoreVM::Value* programNameValue,
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

    /// Ensures a value is String-typed for use in exec commands.
    ///
    /// For values that are already String-typed, returns them directly.
    /// For values with non-String IR types (Object/Void from pattern matching)
    /// that are strings at runtime, reinterprets via typed alloca store/load.
    /// This avoids convertToString's N2S fallback which corrupts string pointers.
    CoreVM::Value* ensureString(CoreVM::Value* value, std::string_view label);

    /// Emits IR to export a variable's current value as a shell environment variable.
    /// Loads the value from @p storage, converts to string, and calls the export callback.
    void emitExportVariable(CoreVM::Value* storage, std::string const& name);

    /// Tries to generate IR for a builtin function call (string_length, etc.).
    /// @return true if the name matched a builtin and code was generated
    bool tryGenerateBuiltinCall(std::string const& name, std::vector<ast::Expr const*> const& argExprs);

    /// Tries to generate IR for a built-in property access on collection types.
    /// Dispatches on the object's type ID and field name to emit the appropriate IR.
    /// @param obj The object value being accessed
    /// @param fieldName The property name (e.g., "length", "isSome")
    /// @return true if the property was handled, false if not a known built-in property
    bool tryGenerateBuiltinPropertyAccess(CoreVM::Value* obj, std::string const& fieldName);

    /// @brief Tries to generate a call to a native runtime function by name.
    /// @param name The function name to look up in the runtime.
    /// @param args Already-codegen'd argument values.
    /// @return true if a matching native function was found and the call was generated.
    bool tryGenerateNativeCall(std::string const& name, std::vector<CoreVM::Value*> const& args);

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
        bool isExported = false;
    };

    struct FSharpScope
    {
        std::unordered_map<std::string, BindingInfo> bindings;
        std::vector<CoreVM::AllocaInstr*>
            objectVariables; ///< Variables holding objects (for ORELEASE at scope exit)
        /// Maps variable names to function names they reference (HOF support).
        std::unordered_map<std::string, std::string> functionRefs;
        FSharpScope* parent = nullptr;
    };

    void pushFSharpScope();
    void popFSharpScope();
    void bindFSharpVariable(std::string const& name,
                            CoreVM::Value* value,
                            bool isMutable = false,
                            bool isExported = false);
    void bindFSharpObjectVariable(std::string const& name,
                                  CoreVM::AllocaInstr* storage,
                                  bool isMutable = false);
    [[nodiscard]] CoreVM::Value* lookupFSharpVariable(std::string const& name) const;
    [[nodiscard]] BindingInfo const* lookupFSharpBinding(std::string const& name) const;

    /// Walks the scope chain looking for a function reference mapping for @p name.
    /// Returns the actual function name if found, or std::nullopt.
    [[nodiscard]] std::optional<std::string> lookupFSharpFunctionRef(std::string const& name) const;

    /// Describes whether a function produces a displayable result or unit (side-effect only).
    enum class ResultKind : uint8_t
    {
        Value, ///< Function produces a displayable result.
        Unit,  ///< Function produces unit (side-effect only, no auto-display).
    };

    // F# function management
    struct FSharpFunction
    {
        std::vector<std::string> parameters;                ///< Parameter names in order
        std::vector<std::optional<TypePtr>> parameterTypes; ///< Type annotations (parallel to parameters)
        std::optional<TypePtr> returnType;                  ///< Optional return type annotation
        ast::Expr const* body;                              ///< Function body expression (for inlining)
        ReturnKind returnKind = ReturnKind::Plain;          ///< Whether function returns Result/Option type
        bool isRecursive = false;                           ///< Whether function is declared with `let rec`
        bool hasVariadicParam = false;                      ///< True if last param is variadic (...args)
        /// Names of all functions in the mutual recursion group (empty for non-mutual).
        std::vector<std::string> mutualGroup;
        /// Captured variable bindings from the enclosing scope at function creation time.
        /// Maps variable names to their storage (entry-block allocas).
        std::unordered_map<std::string, CoreVM::Value*> capturedBindings;
        /// Deterministic ordering of captured variable names for function compilation.
        /// Populated by compileFunctionBody; used at both definition and call sites.
        std::vector<std::string> captureOrder;
        /// Maps captured variable names to source function names (HOF support).
        /// Preserves function reference info through closures and partial application.
        std::unordered_map<std::string, std::string> capturedFunctionRefs;
        /// Pre-compiled IR function (set when closure-based compilation is active).
        /// When non-null, calls emit FunctionCallInstr instead of AST inlining.
        CoreVM::IRFunction* compiledFunction = nullptr;
        /// IR return type of the compiled function body (valid when compiledFunction != nullptr).
        CoreVM::LiteralType compiledReturnType = CoreVM::LiteralType::Void;
        /// Builtin higher-order function marker. Empty = normal function,
        /// otherwise "map"/"filter"/"fold"/"reduce"/"reverse".
        std::string builtinHOF;
        /// Whether this function produces a displayable result or unit (side-effect only).
        ResultKind resultKind = ResultKind::Value;

        size_t arity() const { return parameters.size(); }
    };

    void registerFSharpFunction(std::string const& name, FSharpFunction func);
    [[nodiscard]] FSharpFunction const* lookupFSharpFunction(std::string const& name) const;

    // F# function call dispatch helpers (extracted from visit(ApplicationExpr))

    /// Generates IR for a partial application (under-saturated call).
    /// Creates a new lambda capturing the supplied arguments.
    void generatePartialApplication(FSharpFunction const* func,
                                    std::string const& funcName,
                                    std::vector<CoreVM::Value*> const& args);

    /// Generates IR for a mutual-recursive function call using dispatch-loop optimization.
    void generateMutualRecursiveCall(FSharpFunction const* func,
                                     std::string const& funcName,
                                     std::vector<CoreVM::Value*> const& args);

    /// Generates IR for a self-recursive function call using loop-based tail-call optimization.
    void generateRecursiveCall(FSharpFunction const* func,
                               std::string const& funcName,
                               std::vector<CoreVM::Value*> const& args);

    /// Generates IR for a non-recursive function call by inlining the function body.
    void generateFSharpCall(FSharpFunction const* func,
                            std::string const& funcName,
                            std::vector<CoreVM::Value*> const& args);

    /// Dispatches a builtin higher-order function call to the appropriate IR generator.
    void generateBuiltinHOFCall(FSharpFunction const* func,
                                std::string const& funcName,
                                std::vector<CoreVM::Value*> const& args);

    /// Generates IR for `map f xs` — applies f to each element, returns new list.
    void generateMapIR(std::string const& funcName, CoreVM::Value* listValue);

    /// Generates IR for `filter pred xs` — keeps elements where pred returns true.
    void generateFilterIR(std::string const& predName, CoreVM::Value* listValue);

    /// Generates IR for `fold init f xs` — left fold over list with initial accumulator.
    void generateFoldIR(CoreVM::Value* initValue, std::string const& funcName, CoreVM::Value* listValue);

    /// Generates IR for `reduce f xs` — fold without initial value, returns Option.
    void generateReduceIR(std::string const& funcName, CoreVM::Value* listValue);

    /// Generates IR for `reverse xs` — reverses a list.
    void generateReverseIR(CoreVM::Value* listValue);

    /// Generates IR for `find pred xs` — returns first element matching predicate as Option.
    void generateFindIR(std::string const& predName, CoreVM::Value* listValue);

    /// Generates IR for `exists pred xs` — returns true if any element matches predicate.
    void generateExistsIR(std::string const& predName, CoreVM::Value* listValue);

    /// Generates IR for `forall pred xs` — returns true if all elements match predicate.
    void generateForallIR(std::string const& predName, CoreVM::Value* listValue);

    /// Generates IR for `each f xs` — applies function to each element for side effects, returns unit.
    void generateEachIR(std::string const& funcParamName, CoreVM::Value* listValue);

    /// Generates IR for `take n xs` — returns first n elements of list.
    void generateTakeIR(CoreVM::Value* countValue, CoreVM::Value* listValue);

    /// Generates IR for `drop n xs` — skips first n elements, returns rest.
    void generateDropIR(CoreVM::Value* countValue, CoreVM::Value* listValue);

    /// Generates IR for `zip xs ys` — pairs elements from two lists into tuple list.
    void generateZipIR(CoreVM::Value* listA, CoreVM::Value* listB);

    /// Generates IR for `flatten xss` — concatenates a list of lists into a single list.
    void generateFlattenIR(CoreVM::Value* listOfLists);

    /// Generates IR for `sortBy f xs` — sorts list by key extracted via function.
    void generateSortByIR(std::string const& funcParamName, CoreVM::Value* listValue);

    /// Generates IR for `groupBy f xs` — groups list elements by key extracted via function.
    void generateGroupByIR(std::string const& funcParamName, CoreVM::Value* listValue);

    /// Generates IR for `sort xs` — sorts a list of comparable elements numerically.
    void generateSortIR(CoreVM::Value* listValue);

    /// Generates IR for `distinct xs` — removes duplicate elements from list.
    void generateDistinctIR(CoreVM::Value* listValue);

    // Option combinators: Option.map, Option.bind, Option.defaultValue

    /// Resolves a function argument expression (identifier or lambda) to a function reference.
    struct ResolvedFunction
    {
        FSharpFunction const* func;
        std::string name;
    };

    [[nodiscard]] std::optional<ResolvedFunction> resolveFunctionArgument(ast::Expr const* expr);

    /// Dispatches module-qualified Option.{map,bind,defaultValue} calls.
    /// @return true if the method was recognized and code generated.
    bool tryGenerateOptionCall(std::string const& methodName, std::vector<ast::Expr const*> const& argExprs);

    /// Dispatches method-style opt.{map,bind,defaultValue} calls.
    /// @return true if the method was recognized and code generated.
    bool tryGenerateOptionMethodCall(std::string const& methodName,
                                     ast::Expr const* objectExpr,
                                     std::vector<ast::Expr const*> const& argExprs);

    /// Generates IR for `Option.map f opt` from argument expressions.
    void generateOptionMap(std::vector<ast::Expr const*> const& argExprs);

    /// Generates IR for `Option.map` with a pre-evaluated option value.
    void generateOptionMapWithValue(ast::Expr const* funcExpr, CoreVM::Value* optionValue);

    /// Generates IR for `Option.bind f opt` from argument expressions.
    void generateOptionBind(std::vector<ast::Expr const*> const& argExprs);

    /// Generates IR for `Option.bind` with a pre-evaluated option value.
    void generateOptionBindWithValue(ast::Expr const* funcExpr, CoreVM::Value* optionValue);

    /// Generates IR for `Option.defaultValue def opt` from argument expressions.
    void generateOptionDefaultValue(std::vector<ast::Expr const*> const& argExprs);

    /// Generates IR for `Option.defaultValue` with a pre-evaluated option value.
    void generateOptionDefaultValueWithValue(ast::Expr const* defaultExpr, CoreVM::Value* optionValue);

    /// Compiles a function definition as a separate IRFunction for closure-based calls.
    /// Sets func->compiledFunction to the new function on success.
    void compileFunctionBody(std::string const& name, FSharpFunction& func);

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

    /// Returns true if the expression produces a unit (void) result that should not be auto-displayed.
    [[nodiscard]] bool isUnitProducingExpr(ast::Expr const* expr) const;

    /// Implementation helper for isUnitProducingExpr with cycle detection for recursive functions.
    [[nodiscard]] bool isUnitProducingExprImpl(ast::Expr const* expr,
                                               std::unordered_set<std::string>& visited) const;

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

    // IR emit helpers for container types with type tag slots.
    // Each emits ObjAlloc → ObjSetTag → ObjSetSlot(payload) → ObjSetSlot(type tag).

    /// Emits one nesting level of a list comprehension (forward phase).
    /// All levels share the same accumulator for flat output.
    /// Creates blocks inline after source codegen to preserve execution order.
    void emitComprehensionLevel(ast::ListComprehensionExpr const& node,
                                CoreVM::AllocaInstr* accStorage,
                                CoreVM::BasicBlock* doneBlock,
                                int level = 0);

    /// Emits IR for an empty (Nil) list with the given element type tag.
    CoreVM::Value* emitNilList(CoreVM::LiteralType elemType, std::string_view label);

    /// Emits IR for a Cons cell (head :: tail) with the given element type tag.
    CoreVM::Value* emitListCons(CoreVM::Value* head,
                                CoreVM::Value* tail,
                                CoreVM::LiteralType elemType,
                                std::string_view label);

    /// Emits IR for a Some(value) option with the given inner type tag.
    CoreVM::Value* emitSomeOption(CoreVM::Value* value,
                                  CoreVM::LiteralType innerType,
                                  std::string_view label);

    /// Emits IR for a None option.
    CoreVM::Value* emitNoneOption(std::string_view label);

    /// Emits IR for an Ok(value) result with the given inner type tag.
    CoreVM::Value* emitOkResult(CoreVM::Value* value, CoreVM::LiteralType innerType, std::string_view label);

    /// Emits IR for an Error(value) result with the given inner type tag.
    CoreVM::Value* emitErrorResult(CoreVM::Value* value,
                                   CoreVM::LiteralType innerType,
                                   std::string_view label);

    /// Emits IR for a Tuple2 with packed type tags in slot 2.
    CoreVM::Value* emitTuple2(CoreVM::Value* fst,
                              CoreVM::Value* snd,
                              CoreVM::LiteralType fstType,
                              CoreVM::LiteralType sndType,
                              std::string_view label);

    /// Emits IR for a Tuple3 with packed type tags in slot 3.
    CoreVM::Value* emitTuple3(CoreVM::Value* e0,
                              CoreVM::Value* e1,
                              CoreVM::Value* e2,
                              CoreVM::LiteralType t0,
                              CoreVM::LiteralType t1,
                              CoreVM::LiteralType t2,
                              std::string_view label);

    /// Creates an alloca in the entry block of the current function.
    /// This ensures allocas are always at the beginning, which is required
    /// for proper stack tracking in the TargetCodeGenerator.
    CoreVM::AllocaInstr* createAllocaInEntryBlock(CoreVM::LiteralType type, std::string const& name);

    CoreVM::IRBuilder _builder;
    CoreVM::diagnostics::Report& _report;
    CoreVM::Runtime& _runtime;
    bool _hasErrors = false;
    CoreVM::Value* _result = nullptr;
    CoreVM::Signature _processCallSignature;

    /// When false, ShellCommandExpr runs with normal I/O (statement-level).
    /// When true (default), ShellCommandExpr captures stdout as a string.
    bool _shellCommandCaptureMode = true;

    std::vector<LoopContext> _loopStack;
    int _functionDepth = 0;

    // F# scope chain (owned via raw pointer chain, root scope is unique_ptr)
    std::unique_ptr<FSharpScope> _rootFSharpScope;
    FSharpScope* _currentFSharpScope = nullptr;

    // F# function table (name -> function metadata)
    std::unordered_map<std::string, FSharpFunction> _fsharpFunctions;

    /// Property definition with get/set accessor AST pointers for inline codegen.
    struct FSharpProperty
    {
        ast::PropertyAccessor const* getter = nullptr; ///< Getter body (for read access)
        ast::PropertyAccessor const* setter = nullptr; ///< Setter body (for write access)
    };

    /// Properties defined via `let Name with get/set ...` syntax.
    std::unordered_map<std::string, FSharpProperty> _fsharpProperties;

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

    /// True when the current expression being codegen'd is in tail position
    /// (i.e., its value is the function's return value). Used to decide UCALL vs UTCALL.
    bool _inTailPosition = false;

    /// The function currently being compiled by compileFunctionBody, or nullptr.
    /// Used to detect that we're inside a function body compilation (not the main function).
    CoreVM::IRFunction* _compilingFunction = nullptr;

    /// Value bindings created during this codegen pass, to be persisted back.
    std::vector<FSharpPersistentState::PersistedValueBinding> _newValueBindings;

    /// Optional persistent state pointer for REPL sessions (not owned).
    FSharpPersistentState* _persistentState = nullptr;

    /// Type inference results from the pre-pass TypeInferencer.
    /// Maps function names to their inferred parameter and return types.
    InferenceResult _inferenceResult;

    /// Applies inferred types from the TypeInferencer to a function's missing annotations.
    void applyInferredTypes(std::string const& name, FSharpFunction& func);

    /// Tracks the "inner type" of Option/Result values for type propagation.
    /// When an expression produces an Option<T> or Result<T,E>, the inner type T
    /// is recorded here so that the ? operator can produce correctly-typed extractions.
    std::unordered_map<CoreVM::Value*, CoreVM::LiteralType> _innerTypeAnnotations;

    /// Annotates a value with its known inner type (e.g., the T in Option<T>).
    void annotateInnerType(CoreVM::Value* val, CoreVM::LiteralType type);

    /// Retrieves the inner type annotation for a value, if any.
    [[nodiscard]] std::optional<CoreVM::LiteralType> getInnerType(CoreVM::Value* val) const;

    /// Tracks the builtin object type ID for values known to be typed objects.
    /// Used for runtime dispatch (e.g., list printing via object_to_string).
    std::unordered_map<CoreVM::Value*, uint16_t> _objectTypeIdAnnotations;

    /// Annotates a value with its known builtin object type ID.
    void annotateObjectTypeId(CoreVM::Value* val, uint16_t typeId);

    /// Retrieves the object type ID annotation for a value, if any.
    [[nodiscard]] std::optional<uint16_t> getObjectTypeId(CoreVM::Value* val) const;

    /// Tracks the inner object type ID for Option/Result wrappers.
    /// When a value like `Some [1;2;3]` wraps a typed object, this records
    /// the inner object's type ID (e.g., List) so pattern extraction can recover it.
    std::unordered_map<CoreVM::Value*, uint16_t> _innerObjectTypeIdAnnotations;

    /// Annotates a value with the object type ID of its inner/wrapped value.
    void annotateInnerObjectTypeId(CoreVM::Value* val, uint16_t typeId);

    /// Retrieves the inner object type ID annotation for a value, if any.
    [[nodiscard]] std::optional<uint16_t> getInnerObjectTypeId(CoreVM::Value* val) const;

    /// Tracks the element type ID for list values.
    /// When a list is known to contain elements of a specific record type (e.g., ProcessInfo),
    /// this annotation propagates through let bindings and pipelines so that field access
    /// on extracted elements resolves correctly.
    std::unordered_map<CoreVM::Value*, uint16_t> _listElementTypeAnnotations;

    /// Annotates a list value with the type ID of its elements.
    void annotateListElementTypeId(CoreVM::Value* val, uint16_t typeId);

    /// Retrieves the list element type ID annotation for a value, if any.
    [[nodiscard]] std::optional<uint16_t> getListElementTypeId(CoreVM::Value* val) const;

    /// Tracks the element literal type for list values.
    /// When a list is known to contain elements of a specific primitive type (String, Number, Float),
    /// this annotation propagates through let bindings and pipelines so that HOF element allocas
    /// use the correct type instead of hardcoded Number.
    std::unordered_map<CoreVM::Value*, CoreVM::LiteralType> _listElementLiteralTypes;

    /// Annotates a list value with the literal type of its elements.
    void annotateListElementLiteralType(CoreVM::Value* val, CoreVM::LiteralType type);

    /// Retrieves the list element literal type annotation for a value, if any.
    [[nodiscard]] std::optional<CoreVM::LiteralType> getListElementLiteralType(CoreVM::Value* val) const;

    /// Determines the common literal type of a collection of values.
    /// Returns the shared type if all values have the same non-Void type, std::nullopt otherwise.
    [[nodiscard]] static std::optional<CoreVM::LiteralType> determineCommonLiteralType(
        std::span<CoreVM::Value* const> values);

    /// Tracks registered record type definitions.
    struct RecordTypeInfo
    {
        uint16_t typeId;                       ///< Assigned type ID
        std::string name;                      ///< Type name
        std::vector<CoreVM::FieldInfo> fields; ///< Field definitions (name + offset)
        std::unordered_map<std::string, CoreVM::LiteralType> fieldTypes; ///< Field name → VM literal type
        std::unordered_map<std::string, uint16_t>
            fieldObjectTypeIds; ///< Object-typed field → nested record type ID
    };

    /// Maps record type names to their metadata.
    std::unordered_map<std::string, RecordTypeInfo> _recordTypes;

    /// Looks up a record type by name, returning nullptr if not found.
    [[nodiscard]] RecordTypeInfo const* lookupRecordType(std::string const& name) const;

    /// Resolves a record type from a set of field names (for anonymous record literals).
    [[nodiscard]] RecordTypeInfo const* resolveRecordTypeByFields(
        std::vector<std::string> const& fieldNames) const;

    /// Tracks registered discriminated union type definitions.
    struct UnionTypeInfo
    {
        uint16_t typeId;                           ///< Assigned type ID
        std::string name;                          ///< Type name (e.g., "Shape")
        std::vector<CoreVM::VariantInfo> variants; ///< Variant definitions

        /// Maps field name to (variant_tag, slot_offset) for field access on union values.
        std::unordered_map<std::string, std::pair<int, uint8_t>> fieldLookup;
    };

    /// Information about a single constructor of a discriminated union.
    struct ConstructorInfo
    {
        std::string typeName;                ///< Parent union type name
        uint16_t typeId;                     ///< Assigned type ID of the parent union
        int tag;                             ///< Tag value for this constructor variant
        uint8_t payloadSlots;                ///< Number of payload slots (0 for unit constructors)
        std::vector<std::string> fieldNames; ///< Named fields (parallel to payload slots, empty if unnamed)
    };

    /// Maps union type names to their metadata.
    std::unordered_map<std::string, UnionTypeInfo> _unionTypes;

    /// Maps constructor names to their metadata.
    std::unordered_map<std::string, ConstructorInfo> _constructorRegistry;

    /// Looks up a constructor by name, returning nullptr if not found.
    [[nodiscard]] ConstructorInfo const* lookupConstructor(std::string const& name) const;
};

} // namespace endo
