// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/CoreVM.hpp>

#include <string>
#include <utility>
#include <vector>

#include "Pattern.hpp"

namespace endo
{

class IRGenerator;

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

    explicit PatternIRGenerator(IRGenerator& gen);

    /// Compiles a pattern to IR code.
    ///
    /// @param pattern The pattern to compile
    /// @param scrutinee The value being matched against
    /// @param onSuccess Basic block to jump to on successful match
    /// @param onFailure Basic block to jump to on match failure
    void compile(pattern::Pattern const& pattern,
                 CoreVM::Value* scrutinee,
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

    IRGenerator& _gen;
    CoreVM::Value* _scrutinee = nullptr;
    CoreVM::BasicBlock* _successBlock = nullptr;
    CoreVM::BasicBlock* _failureBlock = nullptr;
    std::vector<Binding> _bindings;
    bool _collectOnly = false; ///< When true, only collect bindings without emitting IR
};

} // namespace endo
