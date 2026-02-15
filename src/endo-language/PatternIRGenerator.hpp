// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/CoreVM.hpp>

#include <string>
#include <unordered_map>
#include <vector>

#include "Pattern.hpp"

namespace endo
{

/// Generates IR for pattern matching using decision trees with conditional branches.
///
/// This class visits pattern AST nodes and generates IR code that tests whether
/// a scrutinee value matches the pattern. On match, control flows to a success block;
/// on failure, control flows to a failure block.
///
/// Variable patterns collect bindings that must be installed in the F# scope before
/// evaluating the match arm body.
class PatternIRGenerator final: public pattern::PatternVisitor
{
  public:
    /// A variable binding collected during pattern compilation.
    struct Binding
    {
        std::string name;
        CoreVM::Value* value;
    };

    explicit PatternIRGenerator(CoreVM::IRBuilder& builder);

    /// Compiles a pattern to IR code.
    ///
    /// @param pattern The pattern to compile
    /// @param scrutinee The value being matched against
    /// @param scrutineeStorage The alloca where scrutinee is stored (for reloading across blocks)
    /// @param onSuccess Basic block to jump to on successful match
    /// @param onFailure Basic block to jump to on match failure
    void compile(pattern::Pattern const& pattern,
                 CoreVM::Value* scrutinee,
                 CoreVM::AllocaInstr* scrutineeStorage,
                 CoreVM::BasicBlock* onSuccess,
                 CoreVM::BasicBlock* onFailure);

    /// Collects variable bindings from a pattern without emitting any IR.
    /// This is used to pre-allocate storage for bindings before control flow branches.
    ///
    /// @param pattern The pattern to analyze
    void collectBindings(pattern::Pattern const& pattern);

    /// Returns the variable bindings collected during pattern compilation.
    [[nodiscard]] std::vector<Binding> const& bindings() const noexcept { return _bindings; }

    /// Clears collected bindings (call before compiling next arm).
    void clearBindings() { _bindings.clear(); }

    /// Sets pre-allocated allocas for storing binding values during pattern compilation.
    /// When set, the pattern compiler stores each extracted value into its corresponding
    /// alloca during IR generation (in the same basic block), avoiding cross-block references.
    void setBindingStorage(std::unordered_map<std::string, CoreVM::AllocaInstr*> storage)
    {
        _bindingStorage = std::move(storage);
    }

    /// Sets the field-name-to-slot-offset mapping for record pattern matching.
    /// Must be called before compiling a pattern that contains a RecordPattern.
    void setRecordFieldOffsets(std::unordered_map<std::string, uint8_t> offsets)
    {
        _recordFieldOffsets = std::move(offsets);
    }

    /// Metadata for a user-defined constructor variant.
    struct ConstructorMeta
    {
        uint16_t typeId;      ///< Type ID of the parent union
        int tag;              ///< Tag value for this variant
        uint8_t payloadSlots; ///< Number of payload slots
    };

    /// Sets the constructor lookup map for user-defined discriminated union patterns.
    void setConstructorLookup(std::unordered_map<std::string, ConstructorMeta> lookup)
    {
        _constructorLookup = std::move(lookup);
    }

  private:
    // Pattern visitor implementations
    void visit(pattern::LiteralPattern const& pat) override;
    void visit(pattern::VariablePattern const& pat) override;
    void visit(pattern::WildcardPattern const& pat) override;
    void visit(pattern::TuplePattern const& pat) override;
    void visit(pattern::ListPattern const& pat) override;
    void visit(pattern::ConsPattern const& pat) override;
    void visit(pattern::RecordPattern const& pat) override;
    void visit(pattern::ConstructorPattern const& pat) override;
    void visit(pattern::AsPattern const& pat) override;
    void visit(pattern::OrPattern const& pat) override;
    void visit(pattern::GuardedPattern const& pat) override;

    /// Creates an alloca in the entry block of the current handler.
    /// Required for allocas that must survive across basic blocks.
    CoreVM::AllocaInstr* createAllocaInEntryBlock(CoreVM::LiteralType type, std::string const& name);

    CoreVM::IRBuilder& _builder;
    CoreVM::Value* _scrutinee = nullptr;
    CoreVM::AllocaInstr* _scrutineeStorage = nullptr; ///< Storage for reloading scrutinee across blocks
    CoreVM::BasicBlock* _successBlock = nullptr;
    CoreVM::BasicBlock* _failureBlock = nullptr;
    std::vector<Binding> _bindings;
    std::unordered_map<std::string, CoreVM::AllocaInstr*> _bindingStorage; ///< Pre-allocated alloca storage
    bool _collectOnly = false; ///< When true, only collect bindings without emitting IR
    std::unordered_map<std::string, uint8_t> _recordFieldOffsets; ///< Field name → slot offset for records
    std::unordered_map<std::string, ConstructorMeta>
        _constructorLookup; ///< User-defined constructor metadata
};

} // namespace endo
