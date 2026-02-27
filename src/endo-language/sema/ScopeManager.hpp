// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <endo-language/lexer/Lexer.hpp>

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace CoreVM
{
class Value;
class AllocaInstr;
} // namespace CoreVM

namespace endo
{

/// Information about a variable binding in the scope chain.
struct BindingInfo
{
    CoreVM::Value* value;
    bool isMutable;
    bool isExported = false;
    bool isUsed = false;                                ///< Whether the binding has been referenced
    std::optional<SourceLocationRange> bindingLocation; ///< Source location of the binding definition
};

/// Entry for a disposable resource bound with `let use`.
/// Tracks the alloca and the native callback signature to call at scope exit.
struct DisposeEntry
{
    CoreVM::AllocaInstr* storage;      ///< Alloca holding the disposable resource
    std::string callbackSignature;     ///< Native callback signature (e.g., "file_close(I)V")
};

/// Manages the F# variable scope chain for name resolution during IR generation.
///
/// Provides push/pop scope management, variable binding, and function-reference tracking.
/// The scope chain is a linked list of scopes, each with its own bindings and object variable
/// tracking for ORELEASE at scope exit.
class ScopeManager
{
  public:
    ScopeManager() = default;
    ~ScopeManager();

    // Non-copyable, non-movable (owns scope chain via raw pointers)
    ScopeManager(ScopeManager const&) = delete;
    ScopeManager& operator=(ScopeManager const&) = delete;

    /// Pushes a new scope onto the scope chain.
    void pushScope();

    /// Pops the current scope and returns the list of object variable allocas
    /// that need ORELEASE instructions emitted by the caller.
    [[nodiscard]] std::vector<CoreVM::AllocaInstr*> popScope();

    /// Binds a variable in the current scope.
    void bindVariable(std::string const& name,
                      CoreVM::Value* value,
                      bool isMutable = false,
                      bool isExported = false,
                      std::optional<SourceLocationRange> location = std::nullopt);

    /// Binds an object variable in the current scope, tracking it for ORELEASE at scope exit.
    void bindObjectVariable(std::string const& name,
                            CoreVM::AllocaInstr* storage,
                            bool isMutable = false,
                            std::optional<SourceLocationRange> location = std::nullopt);

    /// Marks a variable as used by walking the scope chain.
    void markUsed(std::string const& name);

    /// Returns unused bindings in the current scope (name + location).
    /// Skips bindings named "_" and bindings without a location.
    [[nodiscard]] std::vector<std::pair<std::string, SourceLocationRange>> getUnusedBindings() const;

    /// Looks up a variable by name, walking the scope chain from innermost to outermost.
    /// @return The variable's storage value, or nullptr if not found.
    [[nodiscard]] CoreVM::Value* lookupVariable(std::string const& name) const;

    /// Looks up full binding info by name, walking the scope chain.
    /// @return The BindingInfo, or nullptr if not found.
    [[nodiscard]] BindingInfo const* lookupBinding(std::string const& name) const;

    /// Registers a mapping from a variable name to a function name (for HOF support).
    void bindFunctionRef(std::string const& varName, std::string const& funcName);

    /// Looks up a function reference mapping by variable name.
    /// @return The actual function name if found, or std::nullopt.
    [[nodiscard]] std::optional<std::string> lookupFunctionRef(std::string const& name) const;

    /// Clears the object variable tracking list for the current scope.
    /// Used by BlockExpr to prevent premature ORELEASE of let-bound objects.
    void clearObjectVariables();

    /// Registers a disposable resource for automatic cleanup at scope exit.
    /// Called when processing `let use` bindings.
    void registerDispose(CoreVM::AllocaInstr* storage, std::string callbackSignature);

    /// Returns the dispose entries for the current scope in LIFO order (reverse of registration).
    [[nodiscard]] std::vector<DisposeEntry> const& currentDisposeEntries() const;

  private:
    struct Scope
    {
        std::unordered_map<std::string, BindingInfo> bindings;
        std::vector<CoreVM::AllocaInstr*> objectVariables; ///< For ORELEASE at scope exit
        std::vector<DisposeEntry> disposeEntries;          ///< For dispose at scope exit (let use)
        std::unordered_map<std::string, std::string> functionRefs;
        Scope* parent = nullptr;
    };

    std::unique_ptr<Scope> _rootScope;
    Scope* _currentScope = nullptr;
};

} // namespace endo
