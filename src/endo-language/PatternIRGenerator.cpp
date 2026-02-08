// SPDX-License-Identifier: Apache-2.0
#include "PatternIRGenerator.hpp"

#include <stdexcept>
#include <variant>

namespace endo
{

PatternIRGenerator::PatternIRGenerator(CoreVM::IRBuilder& builder): _builder(builder)
{
}

void PatternIRGenerator::compile(pattern::Pattern const& pattern,
                                 CoreVM::Value* scrutinee,
                                 CoreVM::AllocaInstr* scrutineeStorage,
                                 CoreVM::BasicBlock* onSuccess,
                                 CoreVM::BasicBlock* onFailure)
{
    _scrutinee = scrutinee;
    _scrutineeStorage = scrutineeStorage;
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
                return _builder.get(CoreVM::CoreNumber(arg));
            }
            else if constexpr (std::is_same_v<T, double>)
            {
                return _builder.getFloat(arg);
            }
            else if constexpr (std::is_same_v<T, bool>)
            {
                return _builder.getBoolean(arg);
            }
            else if constexpr (std::is_same_v<T, std::string>)
            {
                return _builder.get(arg);
            }
            else
            {
                return nullptr;
            }
        },
        pat.value);

    if (!literal)
    {
        _builder.createBr(_failureBlock);
        return;
    }

    // Generate appropriate comparison based on type
    CoreVM::Value* cmp = nullptr;
    if (literal->type() == CoreVM::LiteralType::String)
    {
        // String comparison
        cmp = _builder.createSCmpEQ(_scrutinee, literal, "pat.str.eq");
    }
    else if (_scrutinee->type() == CoreVM::LiteralType::Void
             || _scrutinee->type() == CoreVM::LiteralType::Object)
    {
        // Dynamic value comparison (for values from OGETSLOT with unknown compile-time type)
        cmp = _builder.createVCmpEQ(_scrutinee, literal, "pat.dyn.eq");
    }
    else if (literal->type() == CoreVM::LiteralType::Float)
    {
        // Float comparison
        auto* scrutineeFloat = _scrutinee;
        if (scrutineeFloat->type() != CoreVM::LiteralType::Float)
            scrutineeFloat = _builder.createN2F(scrutineeFloat, "pat.n2f");
        cmp = _builder.createFCmpEQ(scrutineeFloat, literal, "pat.float.eq");
    }
    else if (literal->type() == CoreVM::LiteralType::Boolean
             || _scrutinee->type() == CoreVM::LiteralType::Boolean)
    {
        // Boolean comparison — check equality via XOR+NOT: a == b iff !(a ^ b)
        auto* xorResult = _builder.createBXor(_scrutinee, literal, "pat.bool.xor");
        cmp = _builder.createBNot(xorResult, "pat.bool.eq");
    }
    else
    {
        // Numeric comparison (known types at compile time)
        cmp = _builder.createNCmpEQ(_scrutinee, literal, "pat.num.eq");
    }

    // Conditional branch: on match go to success, otherwise try next pattern
    _builder.createCondBr(cmp, _successBlock, _failureBlock);
}

void PatternIRGenerator::visit(pattern::VariablePattern const& pat)
{
    // Variable patterns always match - bind the scrutinee to the variable name
    _bindings.push_back({ pat.name, _scrutinee });

    if (_collectOnly)
        return;

    // If pre-allocated storage exists for this binding, store the value now
    // (in the same basic block where it was produced, avoiding cross-block references)
    if (auto it = _bindingStorage.find(pat.name); it != _bindingStorage.end())
    {
        _builder.createStore(it->second, _scrutinee, pat.name + ".pat.store");
    }

    // Unconditionally branch to success
    _builder.createBr(_successBlock);
}

void PatternIRGenerator::visit(pattern::WildcardPattern const&)
{
    // Wildcard always matches, no binding needed
    if (_collectOnly)
        return;

    _builder.createBr(_successBlock);
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
            _builder.createBr(_failureBlock);
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
        altBlocks.push_back(_builder.createBlock("pat.or.alt." + std::to_string(i)));
    }

    // Try each alternative
    for (size_t i = 0; i < pat.alternatives.size(); ++i)
    {
        CoreVM::BasicBlock* nextTry = (i + 1 < pat.alternatives.size()) ? altBlocks[i] : _failureBlock;

        // For alternatives after the first, we're in a new basic block where
        // the scrutinee isn't on the stack. Reload it from storage.
        auto* scrutineeForAlt = _scrutinee;
        if (i > 0 && _scrutineeStorage)
            scrutineeForAlt = _builder.createLoad(_scrutineeStorage, "scrutinee.or.reload");

        // Compile this alternative with success going to original success,
        // and failure going to next alternative (or final failure)
        PatternIRGenerator altCompiler(_builder);
        altCompiler.compile(*pat.alternatives[i], scrutineeForAlt, _scrutineeStorage, _successBlock, nextTry);

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
            _builder.setInsertPoint(altBlocks[i]);
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

void PatternIRGenerator::visit(pattern::TuplePattern const& pat)
{
    if (_collectOnly)
    {
        // Collect bindings from sub-patterns
        for (auto const& elem: pat.elements)
            elem->accept(*this);
        return;
    }

    // Match tuple by extracting each slot and recursively matching sub-patterns.
    // Chain: slot[0] match → slot[1] match → ... → success
    // Any sub-pattern failure jumps to the overall failure block.
    auto* currentScrutinee = _scrutinee;
    auto* savedStorage = _scrutineeStorage;
    auto* finalSuccess = _successBlock;

    for (size_t i = 0; i < pat.elements.size(); ++i)
    {
        // For subsequent elements, reload the scrutinee from storage since we're in a new block
        auto* tupleValue =
            (i > 0 && savedStorage) ? _builder.createLoad(savedStorage, "tuple.reload") : currentScrutinee;

        // Extract slot[i] from the tuple object
        auto* slotValue = _builder.createObjGetSlot(
            tupleValue, _builder.get(CoreVM::CoreNumber(i)), "tuple.slot." + std::to_string(i));

        // Create a success block for this sub-pattern (chains to next sub-pattern or final success)
        auto* subSuccess = (i + 1 < pat.elements.size())
                               ? _builder.createBlock("tuple.match." + std::to_string(i + 1))
                               : finalSuccess;

        // Compile the sub-pattern against the extracted slot
        _scrutinee = slotValue;
        _scrutineeStorage = nullptr; // Sub-patterns don't need to reload from storage
        _successBlock = subSuccess;
        // _failureBlock stays the same — any failure goes to the overall failure

        pat.elements[i]->accept(*this);

        // If there's a next sub-pattern, set insert point to the sub-success block
        if (i + 1 < pat.elements.size())
            _builder.setInsertPoint(subSuccess);
    }

    // Restore original state
    _scrutinee = currentScrutinee;
    _scrutineeStorage = savedStorage;
    _successBlock = finalSuccess;
}

void PatternIRGenerator::visit(pattern::ListPattern const&)
{
    // Lists require runtime representation
    if (_collectOnly)
        return;
    _builder.createBr(_failureBlock);
}

void PatternIRGenerator::visit(pattern::ConsPattern const&)
{
    // Cons patterns require runtime list representation
    if (_collectOnly)
        return;
    _builder.createBr(_failureBlock);
}

void PatternIRGenerator::visit(pattern::RecordPattern const&)
{
    // Records require runtime representation
    if (_collectOnly)
        return;
    _builder.createBr(_failureBlock);
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
        _builder.createBr(_failureBlock);
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
    CoreVM::Value* tag = _builder.createObjGetTag(_scrutinee, "ctor.tag");

    // Check if tag matches expected
    CoreVM::Value* tagMatches =
        _builder.createNCmpEQ(tag, _builder.get(CoreVM::CoreNumber(expectedTag)), "ctor.tag.eq");

    // If pattern has a payload, we need an intermediate block to extract and match it
    if (pat.payload)
    {
        auto* payloadBlock = _builder.createBlock("ctor.payload");
        _builder.createCondBr(tagMatches, payloadBlock, _failureBlock);

        _builder.setInsertPoint(payloadBlock);

        // Reload scrutinee from storage since we're in a new basic block.
        // The stack tracking resets at block boundaries, so we must reload
        // to ensure the value is available for use.
        CoreVM::Value* scrutineeReloaded = _builder.createLoad(_scrutineeStorage, "scrutinee.reload");

        // Extract payload from slot 0 using OGETSLOT
        CoreVM::Value* payloadValue = _builder.createObjGetSlot(
            scrutineeReloaded, _builder.get(CoreVM::CoreNumber(0)), "ctor.payload.value");

        // Recursively match the payload pattern
        CoreVM::Value* savedScrutinee = _scrutinee;
        _scrutinee = payloadValue;
        pat.payload->get()->accept(*this);
        _scrutinee = savedScrutinee;
    }
    else
    {
        // No payload pattern - just check the tag
        _builder.createCondBr(tagMatches, _successBlock, _failureBlock);
    }
}

} // namespace endo
