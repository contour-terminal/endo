// SPDX-License-Identifier: Apache-2.0
#include "PatternIRGenerator.hpp"

#include <stdexcept>
#include <variant>

namespace endo
{

PatternIRGenerator::PatternIRGenerator(CoreVM::IRBuilder& builder): _builder(builder)
{
}

CoreVM::AllocaInstr* PatternIRGenerator::createAllocaInEntryBlock(CoreVM::LiteralType type,
                                                                  std::string const& name)
{
    auto* entryBlock = _builder.handler()->getEntryBlock();
    auto allocaInstr = std::make_unique<CoreVM::AllocaInstr>(
        type, _builder.get(CoreVM::CoreNumber(1)), _builder.makeName(name));
    auto* inserted = entryBlock->insertAfterAllocas(std::move(allocaInstr));
    return static_cast<CoreVM::AllocaInstr*>(inserted);
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

        // For sub-patterns that create new basic blocks (ConstructorPattern, ConsPattern,
        // ListPattern), we need scrutinee storage so they can reload across block boundaries.
        // Simple patterns (Variable, Wildcard, Literal) don't need this.
        CoreVM::AllocaInstr* slotStorage = nullptr;
        auto const* elem = pat.elements[i].get();
        if (dynamic_cast<pattern::ConstructorPattern const*>(elem)
            || dynamic_cast<pattern::ConsPattern const*>(elem)
            || dynamic_cast<pattern::ListPattern const*>(elem)
            || dynamic_cast<pattern::AsPattern const*>(elem))
        {
            slotStorage =
                createAllocaInEntryBlock(slotValue->type(), "tuple.slot." + std::to_string(i) + ".storage");
            _builder.createStore(slotStorage, slotValue);
        }

        // Compile the sub-pattern against the extracted slot
        _scrutinee = slotValue;
        _scrutineeStorage = slotStorage;
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

void PatternIRGenerator::visit(pattern::ListPattern const& pat)
{
    if (_collectOnly)
    {
        // Collect bindings from sub-patterns
        for (auto const& elem: pat.elements)
            elem->accept(*this);
        return;
    }

    // Empty list pattern: match tag == 0 (Nil)
    if (pat.elements.empty())
    {
        auto* tag = _builder.createObjGetTag(_scrutinee, "list.pat.tag");
        auto* isNil = _builder.createNCmpEQ(tag, _builder.get(CoreVM::CoreNumber(0)), "list.pat.isNil");
        _builder.createCondBr(isNil, _successBlock, _failureBlock);
        return;
    }

    // Non-empty list pattern [a; b; c] → desugar to ConsPattern(a, ConsPattern(b, ConsPattern(c, NilCheck)))
    // Build from right to left: start with a Nil check, then wrap with ConsPatterns.
    // We implement this by iteratively checking Cons tag and extracting elements.
    auto* currentScrutinee = _scrutinee;
    auto* savedStorage = _scrutineeStorage;
    auto* finalSuccess = _successBlock;

    for (size_t i = 0; i < pat.elements.size(); ++i)
    {
        bool isLast = (i + 1 == pat.elements.size());

        // Check that current scrutinee is Cons (tag == 1)
        auto* scrutineeVal =
            (i > 0 && savedStorage) ? _builder.createLoad(savedStorage, "list.pat.reload") : currentScrutinee;

        auto* tag = _builder.createObjGetTag(scrutineeVal, "list.pat.tag." + std::to_string(i));
        auto* isCons = _builder.createNCmpEQ(tag, _builder.get(CoreVM::CoreNumber(1)), "list.pat.isCons");

        auto* consBlock = _builder.createBlock("list.pat.cons." + std::to_string(i));
        _builder.createCondBr(isCons, consBlock, _failureBlock);
        _builder.setInsertPoint(consBlock);

        // Reload scrutinee and extract head (slot 0)
        auto* reloaded = _builder.createLoad(savedStorage, "list.pat.head.reload." + std::to_string(i));
        auto* headVal = _builder.createObjGetSlot(
            reloaded, _builder.get(CoreVM::CoreNumber(0)), "list.pat.head." + std::to_string(i));

        // For the element sub-pattern: create success block that chains to next element or final check
        CoreVM::BasicBlock* elemSuccess;
        if (isLast)
        {
            // After last element, verify tail is Nil
            elemSuccess = _builder.createBlock("list.pat.tail.check");
        }
        else
        {
            elemSuccess = _builder.createBlock("list.pat.next." + std::to_string(i + 1));
        }

        // Match head element with sub-pattern
        _scrutinee = headVal;
        _scrutineeStorage = nullptr;
        _successBlock = elemSuccess;
        pat.elements[i]->accept(*this);

        _builder.setInsertPoint(elemSuccess);

        if (isLast)
        {
            // Check that tail is Nil (tag == 0)
            auto* tailReloaded =
                _builder.createLoad(savedStorage, "list.pat.tail.reload." + std::to_string(i));
            auto* tailVal = _builder.createObjGetSlot(
                tailReloaded, _builder.get(CoreVM::CoreNumber(1)), "list.pat.tail." + std::to_string(i));
            auto* tailTag = _builder.createObjGetTag(tailVal, "list.pat.tail.tag");
            auto* tailIsNil =
                _builder.createNCmpEQ(tailTag, _builder.get(CoreVM::CoreNumber(0)), "list.pat.tail.isNil");
            _builder.createCondBr(tailIsNil, finalSuccess, _failureBlock);
        }
        else
        {
            // Move scrutinee to tail (slot 1) for next iteration
            auto* tailReloaded =
                _builder.createLoad(savedStorage, "list.pat.tail.reload." + std::to_string(i));
            auto* tailVal = _builder.createObjGetSlot(
                tailReloaded, _builder.get(CoreVM::CoreNumber(1)), "list.pat.tail." + std::to_string(i));

            // Store tail as new scrutinee for the next iteration
            _builder.createStore(savedStorage, tailVal, "list.pat.next.store");
        }
    }

    // Restore state
    _scrutinee = currentScrutinee;
    _scrutineeStorage = savedStorage;
    _successBlock = finalSuccess;
}

void PatternIRGenerator::visit(pattern::ConsPattern const& pat)
{
    if (_collectOnly)
    {
        // Collect bindings from head and tail sub-patterns
        pat.head->accept(*this);
        pat.tail->accept(*this);
        return;
    }

    // Check tag == 1 (Cons)
    auto* tag = _builder.createObjGetTag(_scrutinee, "cons.pat.tag");
    auto* isCons = _builder.createNCmpEQ(tag, _builder.get(CoreVM::CoreNumber(1)), "cons.pat.isCons");

    auto* consBlock = _builder.createBlock("cons.pat.match");
    _builder.createCondBr(isCons, consBlock, _failureBlock);
    _builder.setInsertPoint(consBlock);

    // Reload scrutinee and extract head (slot 0)
    auto* scrutineeReloaded = _builder.createLoad(_scrutineeStorage, "cons.scrutinee.reload");
    auto* headVal =
        _builder.createObjGetSlot(scrutineeReloaded, _builder.get(CoreVM::CoreNumber(0)), "cons.pat.head");

    // Create block for tail matching (after head pattern succeeds)
    auto* tailBlock = _builder.createBlock("cons.pat.tail");
    auto* finalSuccess = _successBlock;

    // Match head sub-pattern
    auto* savedScrutinee = _scrutinee;
    auto* savedStorage = _scrutineeStorage;

    _scrutinee = headVal;
    _scrutineeStorage = nullptr;
    _successBlock = tailBlock;
    pat.head->accept(*this);

    // In tail block: reload scrutinee, extract tail (slot 1), match tail pattern
    _builder.setInsertPoint(tailBlock);
    auto* scrutineeReloaded2 = _builder.createLoad(savedStorage, "cons.scrutinee.reload2");
    auto* tailVal =
        _builder.createObjGetSlot(scrutineeReloaded2, _builder.get(CoreVM::CoreNumber(1)), "cons.pat.tail");

    _scrutineeStorage = savedStorage;
    _successBlock = finalSuccess;

    // Store tail in scrutinee storage for sub-pattern use (needed for nested ConsPatterns).
    // Then reload to get a fresh IR Value — the store consumes tailVal from the stack,
    // so the sub-pattern needs a separate load to avoid double-consuming the same value.
    _builder.createStore(savedStorage, tailVal, "cons.tail.store");
    _scrutinee = _builder.createLoad(savedStorage, "cons.tail.reload");
    pat.tail->accept(*this);

    // Restore state
    _scrutinee = savedScrutinee;
    _scrutineeStorage = savedStorage;
    _successBlock = finalSuccess;
}

void PatternIRGenerator::visit(pattern::RecordPattern const& pat)
{
    if (_collectOnly)
    {
        // Collect bindings from field sub-patterns
        for (auto const& field: pat.fields)
        {
            if (field.pattern)
            {
                // Explicit binding: { name = n } — collect from sub-pattern
                field.pattern->accept(*this);
            }
            else
            {
                // Punning: { name } — the field name is the binding name
                _bindings.push_back({ field.name, _scrutinee });
            }
        }
        return;
    }

    // Record pattern matching: extract each named field by its slot offset,
    // then recursively match the sub-pattern (or bind directly for punning).
    // This follows the same chain pattern as TuplePattern.
    auto* currentScrutinee = _scrutinee;
    auto* savedStorage = _scrutineeStorage;
    auto* finalSuccess = _successBlock;

    for (size_t i = 0; i < pat.fields.size(); ++i)
    {
        auto const& field = pat.fields[i];

        // Look up the slot offset for this field name
        auto it = _recordFieldOffsets.find(field.name);
        if (it == _recordFieldOffsets.end())
        {
            // Unknown field — fail match
            _builder.createBr(_failureBlock);
            return;
        }
        auto slotOffset = it->second;

        // Always reload the scrutinee from storage to avoid leaving dead temporaries on the
        // stack across block boundaries (which causes stack corruption in the TargetCodeGenerator).
        auto* recordValue =
            savedStorage ? _builder.createLoad(savedStorage, "record.reload") : currentScrutinee;

        // Extract the field value from the record object
        auto* fieldValue = _builder.createObjGetSlot(
            recordValue, _builder.get(CoreVM::CoreNumber(slotOffset)), "record.field." + field.name);

        // Create a success block for this field's sub-pattern (chains to next field or final success)
        auto* subSuccess = (i + 1 < pat.fields.size())
                               ? _builder.createBlock("record.match." + std::to_string(i + 1))
                               : finalSuccess;

        if (field.pattern)
        {
            // Explicit binding: { name = pattern } — recursively match
            _scrutinee = fieldValue;
            _scrutineeStorage = nullptr;
            _successBlock = subSuccess;
            field.pattern->accept(*this);
        }
        else
        {
            // Punning: { name } — bind the field value directly to the field name
            _bindings.push_back({ field.name, fieldValue });

            // Store in pre-allocated storage if available
            if (auto storageIt = _bindingStorage.find(field.name); storageIt != _bindingStorage.end())
                _builder.createStore(storageIt->second, fieldValue, field.name + ".pat.store");

            _builder.createBr(subSuccess);
        }

        // Set insert point for next field's check
        if (i + 1 < pat.fields.size())
            _builder.setInsertPoint(subSuccess);
    }

    // If no fields at all (empty pattern with wildcard), just match
    if (pat.fields.empty())
        _builder.createBr(finalSuccess);

    // Restore state
    _scrutinee = currentScrutinee;
    _scrutineeStorage = savedStorage;
    _successBlock = finalSuccess;
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
        // User-defined constructor — look up in constructor registry
        if (auto it = _constructorLookup.find(pat.name); it != _constructorLookup.end())
            expectedTag = it->second.tag;
        else
        {
            if (_collectOnly)
            {
                // During binding collection, still traverse payload for variable names
                // even if constructor lookup isn't populated yet
                if (pat.payload)
                    pat.payload->get()->accept(*this);
                return;
            }
            // Unknown constructor at compile time - fail match
            _builder.createBr(_failureBlock);
            return;
        }
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

    // Check if this is a user-defined constructor with multi-slot payload
    auto ctorIt = _constructorLookup.find(pat.name);
    bool const isMultiSlotCtor = ctorIt != _constructorLookup.end() && ctorIt->second.payloadSlots > 1;

    // If pattern has a payload, we need an intermediate block to extract and match it
    if (pat.payload)
    {
        auto* payloadBlock = _builder.createBlock("ctor.payload");
        _builder.createCondBr(tagMatches, payloadBlock, _failureBlock);

        _builder.setInsertPoint(payloadBlock);

        if (isMultiSlotCtor)
        {
            // Multi-slot payload: extract each slot individually into pre-allocated allocas.
            // This handles patterns like `Rectangle(w, h)` where w=slot0, h=slot1.
            auto const* tuplePat = dynamic_cast<pattern::TuplePattern const*>(pat.payload->get());
            if (tuplePat)
            {
                // Create intermediate blocks for each element check
                std::vector<CoreVM::BasicBlock*> slotBlocks;
                for (size_t slotIdx = 1; slotIdx < tuplePat->elements.size(); ++slotIdx)
                    slotBlocks.push_back(
                        _builder.createBlock("ctor.slot." + std::to_string(slotIdx) + ".check"));

                for (size_t slotIdx = 0; slotIdx < tuplePat->elements.size(); ++slotIdx)
                {
                    // Reload scrutinee for each slot (avoid multi-use createLoad in loops)
                    CoreVM::Value* reloaded = _builder.createLoad(_scrutineeStorage, "scrutinee.reload");
                    CoreVM::Value* slotVal =
                        _builder.createObjGetSlot(reloaded,
                                                  _builder.get(CoreVM::CoreNumber(slotIdx)),
                                                  "ctor.slot." + std::to_string(slotIdx));

                    // Recursively match the element pattern (handles variable bindings)
                    CoreVM::Value* savedScrutinee = _scrutinee;
                    CoreVM::BasicBlock* savedSuccess = _successBlock;
                    if (slotIdx + 1 < tuplePat->elements.size())
                        _successBlock = slotBlocks[slotIdx]; // next element's check block
                    _scrutinee = slotVal;
                    tuplePat->elements[slotIdx]->accept(*this);
                    _scrutinee = savedScrutinee;
                    _successBlock = savedSuccess;

                    if (slotIdx + 1 < tuplePat->elements.size())
                        _builder.setInsertPoint(slotBlocks[slotIdx]);
                }
            }
            else
            {
                // Single variable binding for the whole multi-slot payload — bind as the object
                CoreVM::Value* savedScrutinee = _scrutinee;
                CoreVM::Value* reloaded = _builder.createLoad(_scrutineeStorage, "scrutinee.reload");
                _scrutinee = reloaded;
                pat.payload->get()->accept(*this);
                _scrutinee = savedScrutinee;
            }
        }
        else
        {
            // Reload scrutinee from storage since we're in a new basic block.
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
    }
    else
    {
        // No payload pattern - just check the tag
        _builder.createCondBr(tagMatches, _successBlock, _failureBlock);
    }
}

} // namespace endo
