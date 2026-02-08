// SPDX-License-Identifier: Apache-2.0
#include "PatternIRGenerator.hpp"

#include <stdexcept>
#include <variant>

#include "IRGenerator.hpp"

namespace endo
{

PatternIRGenerator::PatternIRGenerator(IRGenerator& gen): _gen(gen)
{
}

void PatternIRGenerator::compile(pattern::Pattern const& pattern,
                                 CoreVM::Value* scrutinee,
                                 CoreVM::BasicBlock* onSuccess,
                                 CoreVM::BasicBlock* onFailure)
{
    _scrutinee = scrutinee;
    _successBlock = onSuccess;
    _failureBlock = onFailure;
    _collectOnly = false;

    pattern.accept(*this);
}

void PatternIRGenerator::collectBindings(pattern::Pattern const& pattern)
{
    _collectOnly = true;
    _scrutinee = nullptr;
    _successBlock = nullptr;
    _failureBlock = nullptr;

    pattern.accept(*this);

    _collectOnly = false;
}

void PatternIRGenerator::visit(pattern::LiteralPattern const& pat)
{
    // Literal patterns don't introduce bindings
    if (_collectOnly)
        return;

    // Compare scrutinee with literal value
    CoreVM::Value* literal = std::visit(
        [this](auto&& arg) -> CoreVM::Value* {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, int64_t>)
            {
                return _gen.get(CoreVM::CoreNumber(arg));
            }
            else if constexpr (std::is_same_v<T, double>)
            {
                // CoreVM uses integers; truncate for now
                return _gen.get(CoreVM::CoreNumber(static_cast<int64_t>(arg)));
            }
            else if constexpr (std::is_same_v<T, bool>)
            {
                return _gen.get(arg);
            }
            else if constexpr (std::is_same_v<T, std::string>)
            {
                return _gen.get(arg);
            }
            else
            {
                return nullptr;
            }
        },
        pat.value);

    if (!literal)
    {
        _gen.createBr(_failureBlock);
        return;
    }

    // Generate appropriate comparison based on type
    CoreVM::Value* cmp = nullptr;
    if (literal->type() == CoreVM::LiteralType::String)
    {
        // String comparison
        cmp = _gen.createSCmpEQ(_scrutinee, literal, "pat.str.eq");
    }
    else
    {
        // Numeric/boolean comparison
        cmp = _gen.createNCmpEQ(_scrutinee, literal, "pat.num.eq");
    }

    // Conditional branch: on match go to success, otherwise try next pattern
    _gen.createCondBr(cmp, _successBlock, _failureBlock);
}

void PatternIRGenerator::visit(pattern::VariablePattern const& pat)
{
    // Variable patterns always match - bind the scrutinee to the variable name
    _bindings.push_back({ pat.name, _scrutinee });

    if (_collectOnly)
        return;

    // Unconditionally branch to success
    _gen.createBr(_successBlock);
}

void PatternIRGenerator::visit(pattern::WildcardPattern const&)
{
    // Wildcard always matches, no binding needed
    if (_collectOnly)
        return;

    _gen.createBr(_successBlock);
}

void PatternIRGenerator::visit(pattern::AsPattern const& pat)
{
    // Bind the entire value to the name
    _bindings.push_back({ pat.name, _scrutinee });

    // Also check the inner pattern (for more bindings)
    pat.inner->accept(*this);
}

void PatternIRGenerator::visit(pattern::OrPattern const& pat)
{
    if (pat.alternatives.empty())
    {
        if (!_collectOnly)
            _gen.createBr(_failureBlock);
        return;
    }

    if (_collectOnly)
    {
        // In collect-only mode, just visit the first alternative for bindings
        // (all alternatives should have the same bindings)
        if (!pat.alternatives.empty())
            pat.alternatives[0]->accept(*this);
        return;
    }

    // For or-patterns, try each alternative in sequence
    // On match of any alternative, go to success
    // Only if all fail, go to failure

    // Create blocks for each alternative check (except the last)
    std::vector<CoreVM::BasicBlock*> altBlocks;
    for (size_t i = 1; i < pat.alternatives.size(); ++i)
    {
        altBlocks.push_back(_gen.createBlock("pat.or.alt." + std::to_string(i)));
    }

    // Try each alternative
    for (size_t i = 0; i < pat.alternatives.size(); ++i)
    {
        CoreVM::BasicBlock* nextTry = (i + 1 < pat.alternatives.size()) ? altBlocks[i] : _failureBlock;

        // Compile this alternative with success going to original success,
        // and failure going to next alternative (or final failure)
        PatternIRGenerator altCompiler(_gen);
        altCompiler.compile(*pat.alternatives[i], _scrutinee, _successBlock, nextTry);

        // Collect any bindings from this alternative
        // Note: All alternatives in an or-pattern should bind the same variables
        // For now, we only keep bindings from the first alternative
        if (i == 0)
        {
            for (auto const& binding: altCompiler.bindings())
            {
                _bindings.push_back(binding);
            }
        }

        // Move to next alternative's check block
        if (i + 1 < pat.alternatives.size())
        {
            _gen.setInsertPoint(altBlocks[i]);
        }
    }
}

void PatternIRGenerator::visit(pattern::GuardedPattern const&)
{
    // Guards are handled at the match arm level in IRGenerator,
    // not here in the pattern compiler
    if (_collectOnly)
        return;
    throw std::logic_error("GuardedPattern should not be directly compiled; guards are handled in MatchExpr");
}

// Patterns that require runtime type support - emit errors for now

void PatternIRGenerator::visit(pattern::TuplePattern const&)
{
    // Tuples require runtime representation
    if (_collectOnly)
        return;
    _gen.createBr(_failureBlock);
}

void PatternIRGenerator::visit(pattern::ListPattern const&)
{
    // Lists require runtime representation
    if (_collectOnly)
        return;
    _gen.createBr(_failureBlock);
}

void PatternIRGenerator::visit(pattern::ConsPattern const&)
{
    // Cons patterns require runtime list representation
    if (_collectOnly)
        return;
    _gen.createBr(_failureBlock);
}

void PatternIRGenerator::visit(pattern::RecordPattern const&)
{
    // Records require runtime representation
    if (_collectOnly)
        return;
    _gen.createBr(_failureBlock);
}

void PatternIRGenerator::visit(pattern::ConstructorPattern const& pat)
{
    // Discriminated unions are represented as TypedObjects with tags
    // The pattern matches if the object's tag matches the constructor

    // Determine expected tag based on constructor name
    // Option: None=0, Some=1
    // Result: Error=0, Ok=1
    int expectedTag = -1;
    if (pat.name == "None")
        expectedTag = 0;
    else if (pat.name == "Some")
        expectedTag = 1;
    else if (pat.name == "Error")
        expectedTag = 0;
    else if (pat.name == "Ok")
        expectedTag = 1;
    else
    {
        // Unknown constructor - fail match
        // TODO: Support user-defined ADTs
        if (_collectOnly)
            return;
        _gen.createBr(_failureBlock);
        return;
    }

    if (_collectOnly)
    {
        // Collect bindings from payload pattern if present
        if (pat.payload)
        {
            // For collect mode, we just need to visit the payload
            // The scrutinee will be set properly during actual compilation
            pat.payload->get()->accept(*this);
        }
        return;
    }

    // Get the tag from the object using OGETTAG
    CoreVM::Value* tag = _gen.createObjGetTag(_scrutinee, "ctor.tag");

    // Check if tag matches expected
    CoreVM::Value* tagMatches =
        _gen.createNCmpEQ(tag, _gen.get(CoreVM::CoreNumber(expectedTag)), "ctor.tag.eq");

    // If pattern has a payload, we need an intermediate block to extract and match it
    if (pat.payload)
    {
        auto* payloadBlock = _gen.createBlock("ctor.payload");
        _gen.createCondBr(tagMatches, payloadBlock, _failureBlock);

        _gen.setInsertPoint(payloadBlock);

        // Extract payload from slot 0 using OGETSLOT
        CoreVM::Value* payloadValue =
            _gen.createObjGetSlot(_scrutinee, _gen.get(CoreVM::CoreNumber(0)), "ctor.payload.value");

        // Recursively match the payload pattern
        CoreVM::Value* savedScrutinee = _scrutinee;
        _scrutinee = payloadValue;
        pat.payload->get()->accept(*this);
        _scrutinee = savedScrutinee;
    }
    else
    {
        // No payload pattern - just check the tag
        _gen.createCondBr(tagMatches, _successBlock, _failureBlock);
    }
}

} // namespace endo
