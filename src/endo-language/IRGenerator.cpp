// SPDX-License-Identifier: Apache-2.0
#include "IRGenerator.hpp"

#include <CoreVM/CoreVM.hpp>

#include <bit>
#include <functional>
#include <typeinfo>

#include "AST.hpp"
#include "ASTPrinter.hpp"
#include "DiagnosticsAdapter.hpp"
#include "Pattern.hpp"
#include "PatternIRGenerator.hpp"
#include "ScopedLogger.hpp"
#include "TypeInferencer.hpp"

// {{{ trace macros
// clang-format off
#if 0 // defined(TRACE_PARSER)
    #define TRACE_SCOPE(message) ScopedLogger _logger { message }
    #define TRACE(message, ...) do { ScopedLogger::write(::std::format(message, __VA_ARGS__)); } while (0)
#else
    #define TRACE_SCOPE(message) do {} while (0)
    #define TRACE(message, ...) do {} while (0)
#endif
// clang-format on
// }}}

#define GLOBAL_SCOPE_INIT_NAME "@main"

namespace endo
{

namespace
{

    /// Collected type information from walking an object's IR instruction chain.
    struct ObjectTypeInfo
    {
        int64_t typeId = -1;                     ///< Builtin type ID from ObjAllocInstr
        std::optional<int64_t> tag;              ///< Variant tag (for sum types like Option/Result)
        std::map<int64_t, CoreVM::Value*> slots; ///< Slot index → value stored in that slot
    };

    /// Walks the IR instruction chain (ObjSetSlot → ObjSetTag → ObjAlloc) to collect object type info.
    /// Returns std::nullopt if the value doesn't trace back to an ObjAllocInstr.
    [[nodiscard]] std::optional<ObjectTypeInfo> tryGetObjectInfo(CoreVM::Value* value)
    {
        ObjectTypeInfo info;
        auto* current = value;
        while (current)
        {
            if (auto* alloc = dynamic_cast<CoreVM::ObjAllocInstr*>(current))
            {
                info.typeId = alloc->typeId()->get();
                return info;
            }
            if (auto* setSlot = dynamic_cast<CoreVM::ObjSetSlotInstr*>(current))
            {
                info.slots[setSlot->slotIndex()->get()] = setSlot->value();
                current = setSlot->object();
            }
            else if (auto* setTag = dynamic_cast<CoreVM::ObjSetTagInstr*>(current))
            {
                if (auto* tagConst = dynamic_cast<CoreVM::ConstantInt*>(setTag->tag()))
                    info.tag = tagConst->get();
                current = setTag->object();
            }
            else
                break;
        }
        return std::nullopt;
    }

    // Forward declaration for mutual recursion with typesCompatible.
    [[nodiscard]] std::string typeName(CoreVM::Value* value);

    /// Returns a human-readable type name for an IR value.
    /// For Object types, traces back through the IR chain to produce parameterized names
    /// like "option<int>", "result<string>", "int * string", etc.
    [[nodiscard]] std::string typeName(CoreVM::Value* value)
    {
        switch (value->type())
        {
            case CoreVM::LiteralType::Number: return "int";
            case CoreVM::LiteralType::Float: return "float";
            case CoreVM::LiteralType::String: return "string";
            case CoreVM::LiteralType::Boolean: return "bool";
            case CoreVM::LiteralType::Void: return "unit";
            case CoreVM::LiteralType::Object:
                if (auto info = tryGetObjectInfo(value))
                {
                    if (info->typeId == CoreVM::BuiltinTypeId::Option)
                    {
                        if (auto it = info->slots.find(0); it != info->slots.end())
                            return std::format("option<{}>", typeName(it->second));
                        return "option"; // None — no inner type known
                    }
                    if (info->typeId == CoreVM::BuiltinTypeId::Result)
                    {
                        if (auto it = info->slots.find(0); it != info->slots.end())
                            return std::format("result<{}>", typeName(it->second));
                        return "result";
                    }
                    if (info->typeId == CoreVM::BuiltinTypeId::Tuple2)
                    {
                        auto it0 = info->slots.find(0);
                        auto it1 = info->slots.find(1);
                        if (it0 != info->slots.end() && it1 != info->slots.end())
                            return std::format("{} * {}", typeName(it0->second), typeName(it1->second));
                        return "tuple";
                    }
                    if (info->typeId == CoreVM::BuiltinTypeId::Tuple3)
                    {
                        auto it0 = info->slots.find(0);
                        auto it1 = info->slots.find(1);
                        auto it2 = info->slots.find(2);
                        if (it0 != info->slots.end() && it1 != info->slots.end() && it2 != info->slots.end())
                            return std::format("{} * {} * {}",
                                               typeName(it0->second),
                                               typeName(it1->second),
                                               typeName(it2->second));
                        return "tuple";
                    }
                    if (info->typeId == CoreVM::BuiltinTypeId::List)
                    {
                        // Cons: slot[0] = head element
                        if (auto it = info->slots.find(0); it != info->slots.end())
                            return std::format("list<{}>", typeName(it->second));
                        return "list"; // Nil — no element type known
                    }
                }
                return "object";
            default: return "unknown";
        }
    }

    /// Checks if two IR values have compatible types.
    /// For object types, compares base type IDs and slot types where both are known.
    /// Sum types (Option/Result) with different tags are compatible (e.g., Some(42) vs None).
    [[nodiscard]] bool typesCompatible(CoreVM::Value* a, CoreVM::Value* b)
    {
        // Void acts as a wildcard (unknown type) — compatible with anything.
        // This occurs for function handler parameters whose concrete types aren't
        // known until call time.
        if (a->type() == CoreVM::LiteralType::Void || b->type() == CoreVM::LiteralType::Void)
            return true;

        // Different base IR types → incompatible
        if (a->type() != b->type())
            return false;

        // For non-object types, same LiteralType means compatible
        if (a->type() != CoreVM::LiteralType::Object)
            return true;

        // Both are Object — compare detailed object type info
        auto const infoA = tryGetObjectInfo(a);
        auto const infoB = tryGetObjectInfo(b);

        // If we can't trace either back to ObjAlloc, assume compatible (can't prove mismatch)
        if (!infoA || !infoB)
            return true;

        // Different base type IDs → incompatible (e.g., option vs result)
        if (infoA->typeId != infoB->typeId)
            return false;

        // For sum types (Option/Result/List): different variant tags → compatible
        // (e.g., Some(42) vs None, Ok(42) vs Error("msg"), Cons vs Nil)
        auto const isSumType = infoA->typeId == CoreVM::BuiltinTypeId::Option
                               || infoA->typeId == CoreVM::BuiltinTypeId::Result
                               || infoA->typeId == CoreVM::BuiltinTypeId::List;
        if (isSumType && infoA->tag.has_value() && infoB->tag.has_value() && *infoA->tag != *infoB->tag)
            return true;

        // Compare slot types where both have values
        for (auto const& [slot, valA]: infoA->slots)
        {
            if (auto it = infoB->slots.find(slot); it != infoB->slots.end())
            {
                if (!typesCompatible(valA, it->second))
                    return false;
            }
        }
        return true;
    }

} // namespace

std::unique_ptr<CoreVM::IRProgram> IRGenerator::generate(ast::Statement const& rootNode,
                                                         CoreVM::diagnostics::Report& report,
                                                         CoreVM::Runtime& runtime,
                                                         FSharpPersistentState* persistentState)
{
    IRGenerator generator(report, runtime);

    generator._builder.setProgram(std::make_unique<CoreVM::IRProgram>());
    generator._builder.setHandler(generator._builder.getHandler(GLOBAL_SCOPE_INIT_NAME));
    generator._builder.setInsertPoint(generator._builder.createBlock("EntryPoint"));

    // Initialize F# root scope
    generator.pushFSharpScope();

    // Pre-populate function table from persistent state (REPL session continuity)
    if (persistentState)
    {
        for (auto const& [name, persisted]: persistentState->functions)
        {
            FSharpFunction func;
            func.parameters = persisted.parameters;
            func.parameterTypes = persisted.parameterTypes;
            func.returnType = persisted.returnType;
            func.body = persisted.body;
            func.returnKind = persisted.returnKind;
            func.isRecursive = persisted.isRecursive;
            // capturedBindings intentionally left empty — captures from previous
            // IR programs are no longer valid; only pure functions persist correctly.
            generator.registerFSharpFunction(name, std::move(func));
        }

        // Pre-load persisted value bindings (in order, so dependencies resolve correctly)
        for (auto const& binding: persistentState->valueBindings)
        {
            CoreVM::Value* val = nullptr;

            // For mutable bindings with a saved runtime snapshot, use the snapshot value
            // instead of re-evaluating the original AST expression (which would discard mutations).
            if (binding.isMutable)
            {
                if (auto it = persistentState->mutableSnapshots.find(binding.name);
                    it != persistentState->mutableSnapshots.end())
                {
                    auto const rawValue = it->second;
                    switch (binding.storageType)
                    {
                        case CoreVM::LiteralType::Number:
                            val = generator._builder.get(CoreVM::CoreNumber(static_cast<int64_t>(rawValue)));
                            break;
                        case CoreVM::LiteralType::Boolean:
                            val = generator._builder.getBoolean(rawValue != 0);
                            break;
                        case CoreVM::LiteralType::Float:
                            val = generator._builder.getFloat(std::bit_cast<double>(rawValue));
                            break;
                        default:
                            // String/Object types: fall back to re-evaluating the AST
                            val = generator.codegen(binding.value);
                            break;
                    }
                }
            }

            // Non-mutable bindings or no snapshot available: evaluate the AST expression
            if (!val)
                val = generator.codegen(binding.value);

            if (!val)
                continue;
            auto* storage = generator.createAllocaInEntryBlock(val->type(), binding.name);
            generator._builder.createStore(storage, val, binding.name);
            if (binding.isObjectExpr)
                generator.bindFSharpObjectVariable(binding.name, storage, binding.isMutable);
            else
                generator.bindFSharpVariable(binding.name, storage, binding.isMutable);
        }

        // Re-compute captured bindings and compile persisted functions as handlers.
        // Value bindings are now in scope, so closures can resolve their captures.
        for (auto& [name, func]: generator._fsharpFunctions)
        {
            if (func.body)
            {
                auto boundNames = func.parameters;
                func.capturedBindings = generator.collectFreeVariables(func.body, boundNames);
            }
            generator.compileFunctionAsHandler(name, func);
        }
    }

    // Run Hindley-Milner type inference pre-pass.
    // Inference errors are non-fatal: unresolved types simply remain unannotated
    // and fall back to AST inlining during codegen.
    {
        auto inferencer = TypeInferencer(createStandardTypeEnv());
        generator._inferenceResult = inferencer.inferProgram(rootNode);
    }

    generator.codegen(&rootNode);

    // Persist newly defined functions back to persistent state
    if (persistentState)
    {
        for (auto const& [name, func]: generator._fsharpFunctions)
        {
            // Skip auto-generated lambda names (partial application intermediates)
            if (name.starts_with("__lambda_"))
                continue;

            FSharpPersistentState::PersistedFunction persisted;
            persisted.parameters = func.parameters;
            persisted.parameterTypes = func.parameterTypes;
            persisted.returnType = func.returnType;
            persisted.body = func.body;
            persisted.returnKind = func.returnKind;
            persisted.isRecursive = func.isRecursive;
            persistentState->functions[name] = std::move(persisted);
        }

        // Persist newly created value bindings
        for (auto const& vb: generator._newValueBindings)
        {
            auto it = std::ranges::find_if(persistentState->valueBindings,
                                           [&](auto const& b) { return b.name == vb.name; });
            if (it != persistentState->valueBindings.end())
                *it = vb;
            else
                persistentState->valueBindings.push_back(vb);
        }
    }

    // Clean up F# scope
    generator.popFSharpScope();

    if (generator._hasErrors)
        return nullptr;

    generator._builder.createRet(generator._builder.get(CoreVM::CoreNumber(0)));

    return generator._builder.takeProgram();
}

IRGenerator::IRGenerator(CoreVM::diagnostics::Report& report, CoreVM::Runtime& runtime):
    _report { report }, _runtime { runtime }
{
    _processCallSignature.setReturnType(CoreVM::LiteralType::Number);
    _processCallSignature.setName("ProcessCall");
}

// F# scope management implementation
void IRGenerator::pushFSharpScope()
{
    auto newScope = std::make_unique<FSharpScope>();
    newScope->parent = _currentFSharpScope;
    if (!_rootFSharpScope)
    {
        _rootFSharpScope = std::move(newScope);
        _currentFSharpScope = _rootFSharpScope.get();
    }
    else
    {
        _currentFSharpScope = newScope.release();
    }
}

void IRGenerator::popFSharpScope()
{
    if (_currentFSharpScope)
    {
        // Release all object variables in this scope before exiting.
        // We pass the storage (alloca) directly to ObjReleaseInstr, which allows
        // the TargetCodeGenerator to emit LOAD from the alloca's fixed index.
        // This avoids cross-block value tracking issues.
        for (CoreVM::AllocaInstr* storage: _currentFSharpScope->objectVariables)
        {
            _builder.createObjRelease(storage, "scope.exit.release");
        }

        FSharpScope* parent = _currentFSharpScope->parent;
        if (_currentFSharpScope != _rootFSharpScope.get())
        {
            delete _currentFSharpScope;
        }
        _currentFSharpScope = parent;
    }
}

void IRGenerator::bindFSharpVariable(std::string const& name, CoreVM::Value* value, bool isMutable)
{
    if (_currentFSharpScope)
        _currentFSharpScope->bindings[name] = BindingInfo { value, isMutable };
}

void IRGenerator::bindFSharpObjectVariable(std::string const& name,
                                           CoreVM::AllocaInstr* storage,
                                           bool isMutable)
{
    if (_currentFSharpScope)
    {
        // Track the storage for ORELEASE at scope exit
        _currentFSharpScope->objectVariables.push_back(storage);
        // Also bind as a regular variable
        _currentFSharpScope->bindings[name] = BindingInfo { storage, isMutable };
    }
}

CoreVM::Value* IRGenerator::lookupFSharpVariable(std::string const& name) const
{
    for (FSharpScope const* scope = _currentFSharpScope; scope != nullptr; scope = scope->parent)
    {
        auto it = scope->bindings.find(name);
        if (it != scope->bindings.end())
            return it->second.value;
    }
    return nullptr;
}

IRGenerator::BindingInfo const* IRGenerator::lookupFSharpBinding(std::string const& name) const
{
    for (FSharpScope const* scope = _currentFSharpScope; scope != nullptr; scope = scope->parent)
    {
        auto it = scope->bindings.find(name);
        if (it != scope->bindings.end())
            return &it->second;
    }
    return nullptr;
}

// F# function management implementation
void IRGenerator::registerFSharpFunction(std::string const& name, FSharpFunction func)
{
    _fsharpFunctions[name] = std::move(func);
}

IRGenerator::FSharpFunction const* IRGenerator::lookupFSharpFunction(std::string const& name) const
{
    auto it = _fsharpFunctions.find(name);
    if (it != _fsharpFunctions.end())
        return &it->second;
    return nullptr;
}

std::optional<CoreVM::LiteralType> IRGenerator::mapTypeToLiteralType(TypePtr const& type)
{
    if (auto const* prim = type->asPrimitive())
    {
        switch (prim->kind)
        {
            case PrimitiveType::Int: return CoreVM::LiteralType::Number;
            case PrimitiveType::Float: return CoreVM::LiteralType::Float;
            case PrimitiveType::Str: return CoreVM::LiteralType::String;
            case PrimitiveType::Bool: return CoreVM::LiteralType::Boolean;
            case PrimitiveType::Unit: return CoreVM::LiteralType::Void;
        }
    }
    if (type->isOption() || type->isResult() || type->isTuple())
        return CoreVM::LiteralType::Object;
    if (type->isFunction())
        return CoreVM::LiteralType::String; // Function references stored as string names
    if (type->isList())
        return CoreVM::LiteralType::Object;
    return std::nullopt;
}

bool IRGenerator::validateTypeAnnotation(TypePtr const& annotated,
                                         CoreVM::Value* value,
                                         std::string_view context)
{
    auto const expected = mapTypeToLiteralType(annotated);
    if (!expected)
        return true; // Unknown type annotation — skip validation

    auto const actual = value->type();

    // Accept Object/Void as compatible with any expected type (dynamic values from ObjGetSlot, etc.)
    if (actual == CoreVM::LiteralType::Object || actual == CoreVM::LiteralType::Void)
        return true;

    if (actual != *expected)
    {
        reportTypeError(
            "Type mismatch for {}: expected '{}', got '{}'", context, toString(annotated), typeName(value));
        return false;
    }
    return true;
}

void IRGenerator::extractTypedParameters(std::vector<ast::TypedParameter> const& typedParams,
                                         FSharpFunction& func)
{
    func.parameters.clear();
    func.parameterTypes.clear();
    func.parameters.reserve(typedParams.size());
    func.parameterTypes.reserve(typedParams.size());
    for (auto const& tp: typedParams)
    {
        func.parameters.push_back(tp.name);
        func.parameterTypes.push_back(tp.typeAnnotation);
    }
}

void IRGenerator::applyInferredTypes(std::string const& name, FSharpFunction& func)
{
    auto it = _inferenceResult.functions.find(name);
    if (it == _inferenceResult.functions.end())
        return;

    auto const& inferred = it->second;

    // Fill in missing parameter type annotations from inference results.
    // Only apply types that resolve to concrete primitive types (int, float, bool, str, unit).
    // Complex types (list, option, result, function, tuple) and unresolved type variables
    // are not applied — functions using them continue to use AST inlining, which handles
    // their runtime semantics correctly (recursion patterns, ? operator, object lifecycle).
    for (size_t i = 0; i < func.parameterTypes.size() && i < inferred.paramTypes.size(); ++i)
    {
        if (!func.parameterTypes[i].has_value() && inferred.paramTypes[i]->isPrimitive())
            func.parameterTypes[i] = inferred.paramTypes[i];
    }

    // Fill in missing return type annotation (only if concrete primitive)
    if (!func.returnType.has_value() && inferred.returnType && (*inferred.returnType)->isPrimitive())
        func.returnType = inferred.returnType;
}

ReturnKind IRGenerator::determineReturnKind(ast::Expr const* body) const
{
    if (!body)
        return ReturnKind::Plain;

    // Direct Result/Option constructors
    if (dynamic_cast<ast::ResultExpr const*>(body))
        return ReturnKind::Result;
    if (dynamic_cast<ast::OptionExpr const*>(body))
        return ReturnKind::Option;

    // Try expressions: the ? operator unwraps Result/Option, but the function
    // itself needs to return a Result/Option for the error path. Default to Result.
    if (auto* tryExpr = dynamic_cast<ast::TryExpr const*>(body))
    {
        // Check the operand to determine if it's Option or Result
        auto operandKind = determineReturnKind(tryExpr->operand.get());
        if (operandKind != ReturnKind::Plain)
            return operandKind;
        return ReturnKind::Result; // default for ? operator
    }

    // Try-finally: result type is the body's result type
    if (auto* tryFinally = dynamic_cast<ast::TryFinallyExpr const*>(body))
        return determineReturnKind(tryFinally->body.get());

    // Check through parentheses
    if (auto* paren = dynamic_cast<ast::ParenExpr const*>(body))
        return determineReturnKind(paren->inner.get());

    // Let-in expression: check both value and body recursively
    if (auto* letIn = dynamic_cast<ast::LetInExpr const*>(body))
    {
        auto valueKind = determineReturnKind(letIn->value.get());
        if (valueKind != ReturnKind::Plain)
            return valueKind;
        return determineReturnKind(letIn->body.get());
    }

    // If expression: check both branches
    if (auto* ifExpr = dynamic_cast<ast::IfExpr const*>(body))
    {
        auto thenKind = determineReturnKind(ifExpr->thenExpr.get());
        if (thenKind != ReturnKind::Plain)
            return thenKind;
        return determineReturnKind(ifExpr->elseExpr.get());
    }

    // Check match expression arms
    if (auto* match = dynamic_cast<ast::MatchExpr const*>(body))
    {
        for (auto const& arm: match->arms)
        {
            auto kind = determineReturnKind(arm.body.get());
            if (kind != ReturnKind::Plain)
                return kind;
        }
        return ReturnKind::Plain;
    }

    // Check pipeline - the result type is the last function's return type
    if (auto* pipe = dynamic_cast<ast::PipelineExpr const*>(body))
    {
        auto valueKind = determineReturnKind(pipe->value.get());
        if (valueKind != ReturnKind::Plain)
            return valueKind;
        return determineReturnKind(pipe->function.get());
    }

    // Function application - look up the called function's return kind
    if (auto* app = dynamic_cast<ast::ApplicationExpr const*>(body))
    {
        // Drill down to find the base identifier
        ast::Expr const* base = app->function.get();
        while (auto* inner = dynamic_cast<ast::ApplicationExpr const*>(base))
            base = inner->function.get();
        if (auto* ident = dynamic_cast<ast::IdentifierExpr const*>(base))
        {
            auto const* func = lookupFSharpFunction(ident->name);
            if (func)
                return func->returnKind;
        }
    }

    // Fallback: deep walk looking for any nested TryExpr
    if (containsTryExpr(body))
        return ReturnKind::Result; // default to Result when ? is found somewhere

    return ReturnKind::Plain;
}

bool IRGenerator::containsTryExpr(ast::Expr const* body) const
{
    if (!body)
        return false;

    if (dynamic_cast<ast::TryExpr const*>(body))
        return true;

    // Do NOT recurse into lambda bodies (they are separate functions)
    if (dynamic_cast<ast::LambdaExpr const*>(body))
        return false;

    if (auto* paren = dynamic_cast<ast::ParenExpr const*>(body))
        return containsTryExpr(paren->inner.get());

    if (auto* letIn = dynamic_cast<ast::LetInExpr const*>(body))
        return containsTryExpr(letIn->value.get()) || containsTryExpr(letIn->body.get());

    if (auto* ifExpr = dynamic_cast<ast::IfExpr const*>(body))
        return containsTryExpr(ifExpr->thenExpr.get()) || containsTryExpr(ifExpr->elseExpr.get());

    if (auto* match = dynamic_cast<ast::MatchExpr const*>(body))
    {
        if (containsTryExpr(match->scrutinee.get()))
            return true;
        for (auto const& arm: match->arms)
            if (containsTryExpr(arm.body.get()))
                return true;
        return false;
    }

    if (auto* pipe = dynamic_cast<ast::PipelineExpr const*>(body))
        return containsTryExpr(pipe->value.get()) || containsTryExpr(pipe->function.get());

    if (auto* binary = dynamic_cast<ast::BinaryExpr const*>(body))
        return containsTryExpr(binary->left.get()) || containsTryExpr(binary->right.get());

    if (auto* unary = dynamic_cast<ast::UnaryExpr const*>(body))
        return containsTryExpr(unary->operand.get());

    if (auto* app = dynamic_cast<ast::ApplicationExpr const*>(body))
        return containsTryExpr(app->function.get()) || containsTryExpr(app->argument.get());

    if (auto* tryFinally = dynamic_cast<ast::TryFinallyExpr const*>(body))
        return containsTryExpr(tryFinally->body.get());

    if (auto* result = dynamic_cast<ast::ResultExpr const*>(body))
        return containsTryExpr(result->payload.get());

    if (auto* option = dynamic_cast<ast::OptionExpr const*>(body))
        return option->isSome && containsTryExpr(option->value.get());

    return false;
}

bool IRGenerator::needsAutoWrap(ast::Expr const* body) const
{
    if (!body)
        return true;

    // Already produces a Result/Option object — no wrapping needed
    if (dynamic_cast<ast::ResultExpr const*>(body))
        return false;
    if (dynamic_cast<ast::OptionExpr const*>(body))
        return false;

    // TryExpr (?) returns a raw unwrapped value — needs wrapping
    if (dynamic_cast<ast::TryExpr const*>(body))
        return true;

    // Let-in: wrapping depends on the body (the final expression)
    if (auto* letIn = dynamic_cast<ast::LetInExpr const*>(body))
        return needsAutoWrap(letIn->body.get());

    // If expression: needs wrapping if either branch needs it
    if (auto* ifExpr = dynamic_cast<ast::IfExpr const*>(body))
        return needsAutoWrap(ifExpr->thenExpr.get()) || needsAutoWrap(ifExpr->elseExpr.get());

    // Match expression: needs wrapping if any arm needs it
    if (auto* match = dynamic_cast<ast::MatchExpr const*>(body))
    {
        for (auto const& arm: match->arms)
            if (needsAutoWrap(arm.body.get()))
                return true;
        return false;
    }

    // Parenthesized expression: check inner
    if (auto* paren = dynamic_cast<ast::ParenExpr const*>(body))
        return needsAutoWrap(paren->inner.get());

    // Try-finally: check the body
    if (auto* tryFinally = dynamic_cast<ast::TryFinallyExpr const*>(body))
        return needsAutoWrap(tryFinally->body.get());

    // Application of a known function that returns Result/Option — already wrapped
    if (auto* app = dynamic_cast<ast::ApplicationExpr const*>(body))
    {
        ast::Expr const* base = app->function.get();
        while (auto* inner = dynamic_cast<ast::ApplicationExpr const*>(base))
            base = inner->function.get();
        if (auto* ident = dynamic_cast<ast::IdentifierExpr const*>(base))
        {
            auto const* func = lookupFSharpFunction(ident->name);
            if (func && func->returnKind != ReturnKind::Plain)
                return false;
        }
    }

    // Everything else (BinaryExpr, literals, IdentifierExpr, etc.) — needs wrapping
    return true;
}

CoreVM::Value* IRGenerator::wrapInResultOrOption(CoreVM::Value* value, ReturnKind kind)
{
    if (kind == ReturnKind::Plain)
        return value; // Should not happen, but safety fallback

    auto typeId =
        (kind == ReturnKind::Result) ? CoreVM::BuiltinTypeId::Result : CoreVM::BuiltinTypeId::Option;
    auto const* typeName = (kind == ReturnKind::Result) ? "result" : "option";

    CoreVM::Value* obj = _builder.createObjAlloc(_builder.get(CoreVM::CoreNumber(typeId)),
                                                 std::format("autowrap.{}", typeName));
    obj = _builder.createObjSetTag(
        obj, _builder.get(CoreVM::CoreNumber(1)), std::format("autowrap.{}.tag", typeName)); // Ok=1 or Some=1
    obj = _builder.createObjSetSlot(
        obj, _builder.get(CoreVM::CoreNumber(0)), value, std::format("autowrap.{}.value", typeName));
    return obj;
}

std::string IRGenerator::generateLambdaName()
{
    return std::format("__lambda_{}", _lambdaCounter++);
}

void IRGenerator::annotateInnerType(CoreVM::Value* val, CoreVM::LiteralType type)
{
    _innerTypeAnnotations[val] = type;
}

std::optional<CoreVM::LiteralType> IRGenerator::getInnerType(CoreVM::Value* val) const
{
    auto it = _innerTypeAnnotations.find(val);
    if (it != _innerTypeAnnotations.end())
        return it->second;
    return std::nullopt;
}

void IRGenerator::annotateObjectTypeId(CoreVM::Value* val, uint16_t typeId)
{
    _objectTypeIdAnnotations[val] = typeId;
}

std::optional<uint16_t> IRGenerator::getObjectTypeId(CoreVM::Value* val) const
{
    auto it = _objectTypeIdAnnotations.find(val);
    if (it != _objectTypeIdAnnotations.end())
        return it->second;
    return std::nullopt;
}

void IRGenerator::annotateInnerObjectTypeId(CoreVM::Value* val, uint16_t typeId)
{
    _innerObjectTypeIdAnnotations[val] = typeId;
}

std::optional<uint16_t> IRGenerator::getInnerObjectTypeId(CoreVM::Value* val) const
{
    auto it = _innerObjectTypeIdAnnotations.find(val);
    if (it != _innerObjectTypeIdAnnotations.end())
        return it->second;
    return std::nullopt;
}

std::unordered_map<std::string, CoreVM::Value*> IRGenerator::collectFreeVariables(
    ast::Expr const* body, std::vector<std::string> const& boundNames) const
{
    std::unordered_map<std::string, CoreVM::Value*> freeVars;

    // Recursive walker as a lambda (avoids needing a full Visitor subclass)
    std::function<void(ast::Expr const*, std::vector<std::string> const&)> walk =
        [&](ast::Expr const* expr, std::vector<std::string> const& bound) {
            if (!expr)
                return;

            if (auto const* ident = dynamic_cast<ast::IdentifierExpr const*>(expr))
            {
                // Check if the identifier is already bound (parameter or locally-scoped)
                if (std::ranges::find(bound, ident->name) != bound.end())
                    return;
                // Check if it's a registered function name
                if (lookupFSharpFunction(ident->name) != nullptr)
                    return;
                // Check if it's accessible in the current variable scope
                if (auto* storage = lookupFSharpVariable(ident->name))
                    freeVars[ident->name] = storage;
                return;
            }

            if (auto const* bin = dynamic_cast<ast::BinaryExpr const*>(expr))
            {
                walk(bin->left.get(), bound);
                walk(bin->right.get(), bound);
                return;
            }

            if (auto const* unary = dynamic_cast<ast::UnaryExpr const*>(expr))
            {
                walk(unary->operand.get(), bound);
                return;
            }

            if (auto const* paren = dynamic_cast<ast::ParenExpr const*>(expr))
            {
                walk(paren->inner.get(), bound);
                return;
            }

            if (auto const* app = dynamic_cast<ast::ApplicationExpr const*>(expr))
            {
                walk(app->function.get(), bound);
                walk(app->argument.get(), bound);
                return;
            }

            if (auto const* pipe = dynamic_cast<ast::PipelineExpr const*>(expr))
            {
                walk(pipe->value.get(), bound);
                walk(pipe->function.get(), bound);
                return;
            }

            if (auto const* lambda = dynamic_cast<ast::LambdaExpr const*>(expr))
            {
                // Lambda parameters shadow outer bindings within the lambda body
                auto innerBound = bound;
                auto const names = ast::extractParameterNames(lambda->parameters);
                innerBound.insert(innerBound.end(), names.begin(), names.end());
                walk(lambda->body.get(), innerBound);
                return;
            }

            if (auto const* match = dynamic_cast<ast::MatchExpr const*>(expr))
            {
                walk(match->scrutinee.get(), bound);
                for (auto const& arm: match->arms)
                {
                    // Pattern bindings shadow outer names within the arm body and guard
                    auto armBound = bound;
                    auto bindings = pattern::collectBindings(*arm.pattern);
                    armBound.insert(armBound.end(), bindings.begin(), bindings.end());
                    if (arm.guard)
                        walk(arm.guard.get(), armBound);
                    walk(arm.body.get(), armBound);
                }
                return;
            }

            if (auto const* opt = dynamic_cast<ast::OptionExpr const*>(expr))
            {
                if (opt->value)
                    walk(opt->value.get(), bound);
                return;
            }

            if (auto const* res = dynamic_cast<ast::ResultExpr const*>(expr))
            {
                if (res->payload)
                    walk(res->payload.get(), bound);
                return;
            }

            if (auto const* tryExpr = dynamic_cast<ast::TryExpr const*>(expr))
            {
                walk(tryExpr->operand.get(), bound);
                return;
            }

            if (auto const* tryWith = dynamic_cast<ast::TryWithExpr const*>(expr))
            {
                walk(tryWith->body.get(), bound);
                for (auto const& handler: tryWith->handlers)
                {
                    auto handlerBound = bound;
                    auto bindings = pattern::collectBindings(*handler.pattern);
                    handlerBound.insert(handlerBound.end(), bindings.begin(), bindings.end());
                    if (handler.guard)
                        walk(handler.guard.get(), handlerBound);
                    walk(handler.body.get(), handlerBound);
                }
                return;
            }

            if (auto const* list = dynamic_cast<ast::ListExpr const*>(expr))
            {
                for (auto const& elem: list->elements)
                    walk(elem.get(), bound);
                return;
            }

            if (auto const* range = dynamic_cast<ast::ListRangeExpr const*>(expr))
            {
                walk(range->start.get(), bound);
                if (range->step)
                    walk(range->step.get(), bound);
                walk(range->end.get(), bound);
                return;
            }

            if (auto const* comp = dynamic_cast<ast::ListComprehensionExpr const*>(expr))
            {
                walk(comp->source.get(), bound);
                // The iteration variable is bound within filter and body
                auto innerBound = bound;
                innerBound.push_back(comp->variable);
                if (comp->filter)
                    walk(comp->filter.get(), innerBound);
                walk(comp->body.get(), innerBound);
                return;
            }

            if (auto const* ifExpr = dynamic_cast<ast::IfExpr const*>(expr))
            {
                walk(ifExpr->condition.get(), bound);
                walk(ifExpr->thenExpr.get(), bound);
                walk(ifExpr->elseExpr.get(), bound);
                return;
            }

            if (auto const* tupleExpr = dynamic_cast<ast::TupleExpr const*>(expr))
            {
                for (auto const& elem: tupleExpr->elements)
                    walk(elem.get(), bound);
                return;
            }

            // Literal types (IntLiteralExpr, FloatLiteralExpr, BoolLiteralExpr) have no free variables.
            // ShellCommandExpr has no F# free variables.
        };

    walk(body, boundNames);
    return freeVars;
}

CoreVM::AllocaInstr* IRGenerator::createAllocaInEntryBlock(CoreVM::LiteralType type, std::string const& name)
{
    // Get the entry block of the current handler
    CoreVM::BasicBlock* entryBlock = _builder.handler()->getEntryBlock();

    // Create the alloca instruction
    auto allocaInstr = std::make_unique<CoreVM::AllocaInstr>(
        type, _builder.get(CoreVM::CoreNumber(1)), _builder.makeName(name));

    // Insert after existing allocas to maintain the alloca-prefix invariant.
    // Using insertBeforeTerminator would interleave allocas with non-alloca instructions,
    // breaking TargetCodeGenerator's assumption that allocas form a contiguous stack prefix.
    CoreVM::Instr* inserted = entryBlock->insertAfterAllocas(std::move(allocaInstr));

    return static_cast<CoreVM::AllocaInstr*>(inserted);
}

CoreVM::NativeCallback* IRGenerator::findCallback(std::string const& signature) const
{
    return _runtime.find(signature);
}

CoreVM::Value* IRGenerator::codegen(ast::Node const* node)
{
    TRACE_SCOPE(std::format("codegen({})", node ? typeid(*node).name() : "nullptr"));
    _result = nullptr;
    if (node)
    {
        // Track current location for error reporting and IR instruction annotation
        if (node->location.has_value())
        {
            _builder.setSourceLocation(toCoreLoc(node->location.value()));
        }
        node->accept(*this);
    }
    return _result;
}

template <typename... Args>
void IRGenerator::reportTypeError(std::format_string<Args...> f, Args&&... args)
{
    auto const msg = std::format(f, std::forward<Args>(args)...);
    _report.typeError(_builder.sourceLocation(), "{}", std::string_view(msg));
    _hasErrors = true;
}

void IRGenerator::visit(ast::BuiltinExitStmt const& node)
{
    CoreVM::Value* exitCode = nullptr;
    if (!node.code)
        exitCode = _builder.get(CoreVM::CoreNumber(0));
    else
    {
        // Special handling: if the exit code is a LiteralExpr that looks like an identifier,
        // check if it's an F# variable first (e.g., "exit r" where r is a let-bound variable)
        if (auto const* literal = dynamic_cast<ast::LiteralExpr const*>(node.code.get()))
        {
            // Check if this literal is an F# variable name
            if (CoreVM::Value* fsharpVar = lookupFSharpVariable(literal->value))
            {
                // It's an F# variable - load its value
                exitCode = _builder.createLoad(fsharpVar, literal->value);
            }
        }

        if (!exitCode)
        {
            exitCode = codegen(node.code.get());
            if (!exitCode)
                return; // Error already reported
        }

        if (exitCode->type() == CoreVM::LiteralType::String)
            exitCode = _builder.createS2N(exitCode);
        else if (exitCode->type() != CoreVM::LiteralType::Number)
        {
            reportTypeError("exit code must be a number, got {}", exitCode->type());
            return;
        }
    }
    _result =
        _builder.createCallFunction(_builder.getBuiltinFunction(node.callback.get()), { exitCode }, "exit");
}

void IRGenerator::visit(ast::BuiltinExportStmt const& node)
{
    auto callArguments = std::vector<CoreVM::Value*> {};
    callArguments.push_back(_builder.get(node.name));
    _result = _builder.createCallFunction(
        _builder.getBuiltinFunction(node.callback.get()), callArguments, "export");
}

void IRGenerator::visit(ast::BuiltinChDirStmt const& node)
{
    auto callArguments = std::vector<CoreVM::Value*> {};
    if (node.path)
        callArguments.push_back(codegen(node.path.get()));

    _result =
        _builder.createCallFunction(_builder.getBuiltinFunction(node.callback.get()), callArguments, "chdir");
}

void IRGenerator::visit(ast::BuiltinSetStmt const& node)
{
    auto callArguments = std::vector<CoreVM::Value*> {};
    if (node.name && node.value)
    {

        callArguments.push_back(codegen(node.name.get()));
        callArguments.push_back(codegen(node.value.get()));
    }

    _result =
        _builder.createCallFunction(_builder.getBuiltinFunction(node.callback.get()), callArguments, "set");
}

void IRGenerator::visit(ast::BuiltinFalseStmt const& node)
{
    _result = _builder.createCallFunction(_builder.getBuiltinFunction(node.callback.get()), {}, "false");
}

void IRGenerator::visit(ast::BuiltinReadStmt const& node)
{
    auto callArguments = std::vector<CoreVM::Value*> {};
    if (!node.parameters.empty())
        callArguments.emplace_back(_builder.get(createCallArgs(node.parameters)));

    _result =
        _builder.createCallFunction(_builder.getBuiltinFunction(node.callback.get()), callArguments, "read");
}

void IRGenerator::visit(ast::BuiltinTrueStmt const& node)
{
    _result = _builder.createCallFunction(_builder.getBuiltinFunction(node.callback.get()), {}, "true");
}

void IRGenerator::visit(ast::CallPipeline const& node)
{
    // A | B | C | D
    //
    // process      | stdin             |   stdout
    // -------------------------------------------------------
    // A            | STDIN             |   pipe 1 (write end)
    // B            | pipe 1 (read end) |   pipe 2 (write end)
    // C            | pipe 2 (read end) |   pipe 3 (write end)
    // D            | pipe 3 (read end) |   STDOUT

    for (size_t i = 0; i < node.calls.size(); ++i)
    {
        std::unique_ptr<ast::ProgramCall> const& call = node.calls[i];
        bool const lastInChain = i == node.calls.size() - 1;

        bool const hasRedirects = !call->inputRedirects.empty() || !call->outputRedirects.empty()
                                  || !call->hereDocuments.empty() || !call->hereStrings.empty();

        // Start redirect context if we have any redirects
        if (hasRedirects)
        {
            auto* startCallback = findCallback("internal.redirect_start()V");
            if (startCallback)
                _builder.createCallFunction(
                    _builder.getBuiltinFunction(*startCallback), {}, "redirect_start");
        }

        // Generate code for all redirects
        for (auto const& redirect: call->inputRedirects)
            codegen(redirect.get());

        for (auto const& redirect: call->outputRedirects)
            codegen(redirect.get());

        for (auto const& heredoc: call->hereDocuments)
            codegen(heredoc.get());

        for (auto const& herestring: call->hereStrings)
            codegen(herestring.get());

        if (containsRuntimeExpr(call->parameters))
        {
            // Use dynamic argument building and execution
            buildCommandArgs(call->program, call->parameters);

            // Check if this is the last command in a background pipeline
            if (lastInChain && node.background)
            {
                _result = execBuiltCommandPipedBackground(call->program, call->parameters);
            }
            else
            {
                _result = execBuiltCommandPiped(lastInChain);
            }
        }
        else
        {
            // Check if this is the last command in a background pipeline
            if (lastInChain && node.background)
            {
                // Build command args first for background execution
                buildCommandArgs(call->program, call->parameters);
                _result = execBuiltCommandPipedBackground(call->program, call->parameters);
            }
            else
            {
                // Use constant array (fast path)
                std::vector<CoreVM::Value*> callArguments {};
                callArguments.push_back(_builder.get(lastInChain));
                callArguments.push_back(_builder.get(createCallArgs(call->program, call->parameters)));
                _result = _builder.createCallFunction(
                    _builder.getBuiltinFunction(call->callback.get()), callArguments, "callProcess");
            }
        }

        // End redirect context
        if (hasRedirects)
        {
            auto* endCallback = findCallback("internal.redirect_end()V");
            if (endCallback)
                _builder.createCallFunction(_builder.getBuiltinFunction(*endCallback), {}, "redirect_end");
        }
    }
}

void IRGenerator::visit(ast::CommandFileSubst const& node)
{
    // Process substitution: <(command) or >(command)
    // This requires forking: child runs the command, parent gets the fd path
    bool const isWrite = (node.mode == ast::ProcessSubstMode::Write);

    // Fork for process substitution
    // Returns 0 in child process, fd number (> 0) in parent process
    auto* forkCb = findCallback("internal.procsubst_fork(B)I");
    if (!forkCb)
    {
        reportTypeError("Internal error: internal.procsubst_fork builtin not found");
        return;
    }

    auto* forkResult = _builder.createCallFunction(
        _builder.getBuiltinFunction(*forkCb), { _builder.get(isWrite) }, "procsubst_fork");

    // Check if we're the child (result == 0)
    auto* isChild = _builder.createNCmpEQ(forkResult, _builder.get(CoreVM::CoreNumber(0)));

    CoreVM::BasicBlock* childBlock = _builder.createBlock("procsubst.child");
    CoreVM::BasicBlock* contBlock = _builder.createBlock("procsubst.cont");

    _builder.createCondBr(isChild, childBlock, contBlock);

    // Child block: execute command, then exit
    _builder.setInsertPoint(childBlock);
    codegen(node.command.get());

    // Child exits after running command
    auto* exitCb = findCallback("internal.procsubst_exit()V");
    if (exitCb)
        _builder.createCallFunction(_builder.getBuiltinFunction(*exitCb), {}, "procsubst_exit");
    _builder.createBr(contBlock); // Unreachable due to exit, but needed for valid IR

    // Continue block: parent gets the fd path
    _builder.setInsertPoint(contBlock);
    auto* pathCb = findCallback("internal.procsubst_get_path()S");
    if (!pathCb)
    {
        reportTypeError("Internal error: internal.procsubst_get_path builtin not found");
        return;
    }
    _result = _builder.createCallFunction(_builder.getBuiltinFunction(*pathCb), {}, "procsubst_get_path");
}

void IRGenerator::visit(ast::CompoundStmt const& node)
{
    for (auto const& stmt: node.statements)
    {
        codegen(stmt.get());
        // Stop generating code after a terminator (break, continue, return, etc.)
        if (_builder.getInsertPoint() && _builder.getInsertPoint()->getTerminator() != nullptr)
            break;
    }

    _result = nullptr;
}

void IRGenerator::visit(ast::FileDescriptor const& node)
{
    _result = _builder.get(CoreVM::CoreNumber { node.value });
}

void IRGenerator::visit(ast::IfStmt const& node)
{
    CoreVM::BasicBlock* cond = _builder.createBlock("if.cond");
    CoreVM::BasicBlock* trueBlock = _builder.createBlock("if.trueBlock");
    CoreVM::BasicBlock* falseBlock = _builder.createBlock("if.falseBlock");
    CoreVM::BasicBlock* end = _builder.createBlock("if.end");

    _builder.createBr(cond);
    _builder.setInsertPoint(cond);
    _builder.createCondBr(toBool(codegen(node.condition.get())), trueBlock, falseBlock);

    _builder.setInsertPoint(trueBlock);
    codegen(node.thenBlock.get());
    _builder.createBr(end);

    _builder.setInsertPoint(falseBlock);
    codegen(node.elseBlock.get());
    _builder.createBr(end);

    _builder.setInsertPoint(end);
}

void IRGenerator::visit(ast::LogicalAndStmt const& node)
{
    // Short-circuit AND: execute right only if left succeeds (exit code 0)
    // A && B:
    //   eval A
    //   if A succeeded (exit code == 0): eval B, result = B's exit code
    //   else: result = A's exit code
    CoreVM::BasicBlock* evalRight = _builder.createBlock("and.evalRight");
    CoreVM::BasicBlock* end = _builder.createBlock("and.end");

    // Evaluate left side
    auto* leftResult = codegen(node.left.get());

    // If left succeeded (exit code == 0), evaluate right side
    _builder.createCondBr(toBool(leftResult), evalRight, end);

    // Evaluate right side
    _builder.setInsertPoint(evalRight);
    codegen(node.right.get());
    _builder.createBr(end);

    _builder.setInsertPoint(end);
    // Note: The exit code is automatically set by the last executed command
}

void IRGenerator::visit(ast::LogicalOrStmt const& node)
{
    // Short-circuit OR: execute right only if left fails (exit code != 0)
    // A || B:
    //   eval A
    //   if A failed (exit code != 0): eval B, result = B's exit code
    //   else: result = A's exit code (which is 0, success)
    CoreVM::BasicBlock* evalRight = _builder.createBlock("or.evalRight");
    CoreVM::BasicBlock* end = _builder.createBlock("or.end");

    // Evaluate left side
    auto* leftResult = codegen(node.left.get());

    // If left failed (exit code != 0), evaluate right side
    // toBool returns true for exit code 0 (success), so we flip the branches
    _builder.createCondBr(toBool(leftResult), end, evalRight);

    // Evaluate right side
    _builder.setInsertPoint(evalRight);
    codegen(node.right.get());
    _builder.createBr(end);

    _builder.setInsertPoint(end);
    // Note: The exit code is automatically set by the last executed command
}

void IRGenerator::visit(ast::InputRedirect const& node)
{
    auto* callback = findCallback("internal.redirect_input(IS)V");
    if (!callback)
    {
        reportTypeError("Internal error: internal.redirect_input builtin not found");
        return;
    }
    auto* targetFd = _builder.get(CoreVM::CoreNumber(node.targetFd->value));
    auto* source = codegen(node.source.get());
    if (!source)
        return;
    _result = _builder.createCallFunction(
        _builder.getBuiltinFunction(*callback), { targetFd, source }, "redirect_input");
}

void IRGenerator::visit(ast::HereDocument const& node)
{
    auto* callback = findCallback("internal.redirect_heredoc(IS)V");
    if (!callback)
    {
        reportTypeError("Internal error: internal.redirect_heredoc builtin not found");
        return;
    }
    auto* targetFd = _builder.get(CoreVM::CoreNumber(node.targetFd->value));
    auto* content = _builder.get(node.content);
    _result = _builder.createCallFunction(
        _builder.getBuiltinFunction(*callback), { targetFd, content }, "redirect_heredoc");
}

void IRGenerator::visit(ast::HereString const& node)
{
    auto* callback = findCallback("internal.redirect_herestring(IS)V");
    if (!callback)
    {
        reportTypeError("Internal error: internal.redirect_herestring builtin not found");
        return;
    }
    auto* targetFd = _builder.get(CoreVM::CoreNumber(node.targetFd->value));
    auto* content = codegen(node.content.get());
    if (!content)
        return;
    _result = _builder.createCallFunction(
        _builder.getBuiltinFunction(*callback), { targetFd, content }, "redirect_herestring");
}

void IRGenerator::visit(ast::LiteralExpr const& node)
{
    _result = _builder.get(node.value);
}

void IRGenerator::visit(ast::TildeExpr const& node)
{
    if (node.user.empty())
    {
        // Standalone ~ or ~/path - expand to home directory
        auto* callback = findCallback("expand.tilde(S)S");
        if (!callback)
        {
            reportTypeError("Internal error: expand.tilde builtin not found");
            return;
        }
        _result = _builder.createCallFunction(
            _builder.getBuiltinFunction(*callback), { _builder.get(node.suffix) }, "expand.tilde");
    }
    else
    {
        // ~user or ~user/path - expand to user's home directory
        auto* callback = findCallback("expand.tilde_user(SS)S");
        if (!callback)
        {
            reportTypeError("Internal error: expand.tilde_user builtin not found");
            return;
        }
        _result = _builder.createCallFunction(_builder.getBuiltinFunction(*callback),
                                              { _builder.get(node.user), _builder.get(node.suffix) },
                                              "expand.tilde_user");
    }
}

void IRGenerator::visit(ast::GlobExpr const& node)
{
    auto* callback = findCallback("expand.glob(S)V");
    if (!callback)
    {
        reportTypeError("Internal error: expand.glob builtin not found");
        return;
    }
    // Glob expansion is handled specially - it adds multiple arguments to the command builder
    _builder.createCallFunction(
        _builder.getBuiltinFunction(*callback), { _builder.get(node.pattern) }, "expand.glob");
    _result = nullptr; // Result is captured via cmdBuilderArgs
}

void IRGenerator::visit(ast::ConcatExpr const& node)
{
    // Generate code for each part and concatenate them
    if (node.parts.empty())
    {
        _result = _builder.get("");
        return;
    }

    // Generate code for the first part
    auto* result = codegen(node.parts[0].get());
    if (!result)
        return;

    // Ensure result is a string
    if (result->type() != CoreVM::LiteralType::String)
    {
        // Convert to string if needed
        auto* callback = findCallback("expand.to_string(I)S");
        if (callback)
            result =
                _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { result }, "to_string");
    }

    // Concatenate remaining parts
    for (size_t i = 1; i < node.parts.size(); ++i)
    {
        auto* part = codegen(node.parts[i].get());
        if (!part)
            return;

        // Ensure part is a string
        if (part->type() != CoreVM::LiteralType::String)
        {
            auto* callback = findCallback("expand.to_string(I)S");
            if (callback)
                part = _builder.createCallFunction(
                    _builder.getBuiltinFunction(*callback), { part }, "to_string");
        }

        result = _builder.createSAdd(result, part, "concat");
    }

    _result = result;
}

void IRGenerator::visit(ast::FStringExpr const& node)
{
    // F#-style interpolated string: $"text {expr} text"
    // Similar to ConcatExpr but uses convertToString() for F# type support
    if (node.parts.empty())
    {
        _result = _builder.get("");
        return;
    }

    // Generate first part
    auto* result = codegen(node.parts[0].get());
    if (!result)
        return;

    result = convertToString(result, "fstr");
    if (!result)
        return;

    // Concatenate remaining parts
    for (size_t i = 1; i < node.parts.size(); ++i)
    {
        auto* part = codegen(node.parts[i].get());
        if (!part)
            return;

        part = convertToString(part, "fstr");
        if (!part)
            return;

        result = _builder.createSAdd(result, part, "fstr.concat");
    }

    _result = result;
}

void IRGenerator::visit(ast::ArithExpansionExpr const& node)
{
    // Evaluate the arithmetic expression and return the result as a string
    auto* result = codegenArith(node.expression.get());
    if (!result)
        return;

    // Convert the integer result to a string
    auto* callback = findCallback("expand.arith_to_string(I)S");
    if (!callback)
    {
        reportTypeError("Internal error: expand.arith_to_string builtin not found");
        return;
    }
    _result = _builder.createCallFunction(
        _builder.getBuiltinFunction(*callback), { result }, "expand.arith_to_string");
}

CoreVM::Value* IRGenerator::codegenArith(ast::ArithExpr const* expr)
{
    if (auto const* lit = dynamic_cast<ast::ArithLiteralExpr const*>(expr))
    {
        return _builder.get(CoreVM::CoreNumber(lit->value));
    }
    else if (auto const* var = dynamic_cast<ast::ArithVarExpr const*>(expr))
    {
        // Get variable value and convert to integer
        auto* callback = findCallback("expand.arith_getvar(S)I");
        if (!callback)
        {
            reportTypeError("Internal error: expand.arith_getvar builtin not found");
            return nullptr;
        }
        return _builder.createCallFunction(
            _builder.getBuiltinFunction(*callback), { _builder.get(var->name) }, "expand.arith_getvar");
    }
    else if (auto const* binary = dynamic_cast<ast::ArithBinaryExpr const*>(expr))
    {
        auto* left = codegenArith(binary->left.get());
        auto* right = codegenArith(binary->right.get());
        if (!left || !right)
            return nullptr;

        switch (binary->op)
        {
            case ast::ArithOp::Add: return _builder.createAdd(left, right);
            case ast::ArithOp::Sub: return _builder.createSub(left, right);
            case ast::ArithOp::Mul: return _builder.createMul(left, right);
            case ast::ArithOp::Div: return _builder.createDiv(left, right);
            case ast::ArithOp::Mod: return _builder.createRem(left, right);
            case ast::ArithOp::Pow: {
                // Power operation via builtin
                auto* callback = findCallback("expand.arith_pow(II)I");
                if (!callback)
                {
                    reportTypeError("Internal error: expand.arith_pow builtin not found");
                    return nullptr;
                }
                return _builder.createCallFunction(
                    _builder.getBuiltinFunction(*callback), { left, right }, "expand.arith_pow");
            }
            case ast::ArithOp::Lt: return _builder.createNCmpLT(left, right);
            case ast::ArithOp::Gt: return _builder.createNCmpGT(left, right);
            case ast::ArithOp::Le: return _builder.createNCmpLE(left, right);
            case ast::ArithOp::Ge: return _builder.createNCmpGE(left, right);
            case ast::ArithOp::Eq: return _builder.createNCmpEQ(left, right);
            case ast::ArithOp::Ne: return _builder.createNCmpNE(left, right);
            case ast::ArithOp::And: return _builder.createAnd(left, right);
            case ast::ArithOp::Or: return _builder.createOr(left, right);
            case ast::ArithOp::BitAnd: return _builder.createAnd(left, right);
            case ast::ArithOp::BitOr: return _builder.createOr(left, right);
            case ast::ArithOp::BitXor: return _builder.createXor(left, right);
            case ast::ArithOp::Shl: return _builder.createShl(left, right);
            case ast::ArithOp::Shr: return _builder.createShr(left, right);
            default: reportTypeError("Unsupported arithmetic operator"); return nullptr;
        }
    }
    else if (auto const* unary = dynamic_cast<ast::ArithUnaryExpr const*>(expr))
    {
        auto* operand = codegenArith(unary->operand.get());
        if (!operand)
            return nullptr;

        switch (unary->op)
        {
            case ast::ArithOp::Neg:
                // Implement negation as 0 - operand for proper signed behavior
                return _builder.createSub(_builder.get(CoreVM::CoreNumber(0)), operand);
            case ast::ArithOp::Not: return _builder.createNot(operand);
            case ast::ArithOp::BitNot: return _builder.createNot(operand); // Bitwise NOT
            default: reportTypeError("Unsupported unary arithmetic operator"); return nullptr;
        }
    }

    reportTypeError("Unknown arithmetic expression type");
    return nullptr;
}

void IRGenerator::visit(ast::ParamExpansionExpr const& node)
{
    std::string callbackName;
    std::vector<CoreVM::Value*> args;

    switch (node.op)
    {
        case ast::ParamExpansionOp::Length:
            callbackName = "expand.param_length(S)S";
            args.push_back(_builder.get(node.variable));
            break;
        case ast::ParamExpansionOp::DefaultValue:
            callbackName = "expand.param_default(SS)S";
            args.push_back(_builder.get(node.variable));
            args.push_back(_builder.get(node.operand1));
            break;
        case ast::ParamExpansionOp::AlternateValue:
            callbackName = "expand.param_alternate(SS)S";
            args.push_back(_builder.get(node.variable));
            args.push_back(_builder.get(node.operand1));
            break;
        case ast::ParamExpansionOp::AssignDefault:
            callbackName = "expand.param_assign(SS)S";
            args.push_back(_builder.get(node.variable));
            args.push_back(_builder.get(node.operand1));
            break;
        case ast::ParamExpansionOp::ErrorIfUnset:
            callbackName = "expand.param_error(SS)S";
            args.push_back(_builder.get(node.variable));
            args.push_back(_builder.get(node.operand1));
            break;
        case ast::ParamExpansionOp::RemovePrefixShort:
            callbackName = "expand.param_remove_prefix(SSB)S";
            args.push_back(_builder.get(node.variable));
            args.push_back(_builder.get(node.operand1));
            args.push_back(_builder.get(false)); // shortest
            break;
        case ast::ParamExpansionOp::RemovePrefixLong:
            callbackName = "expand.param_remove_prefix(SSB)S";
            args.push_back(_builder.get(node.variable));
            args.push_back(_builder.get(node.operand1));
            args.push_back(_builder.get(true)); // longest
            break;
        case ast::ParamExpansionOp::RemoveSuffixShort:
            callbackName = "expand.param_remove_suffix(SSB)S";
            args.push_back(_builder.get(node.variable));
            args.push_back(_builder.get(node.operand1));
            args.push_back(_builder.get(false)); // shortest
            break;
        case ast::ParamExpansionOp::RemoveSuffixLong:
            callbackName = "expand.param_remove_suffix(SSB)S";
            args.push_back(_builder.get(node.variable));
            args.push_back(_builder.get(node.operand1));
            args.push_back(_builder.get(true)); // longest
            break;
        case ast::ParamExpansionOp::ReplaceFirst:
            callbackName = "expand.param_replace(SSSB)S";
            args.push_back(_builder.get(node.variable));
            args.push_back(_builder.get(node.operand1));
            args.push_back(_builder.get(node.operand2));
            args.push_back(_builder.get(false)); // first only
            break;
        case ast::ParamExpansionOp::ReplaceAll:
            callbackName = "expand.param_replace(SSSB)S";
            args.push_back(_builder.get(node.variable));
            args.push_back(_builder.get(node.operand1));
            args.push_back(_builder.get(node.operand2));
            args.push_back(_builder.get(true)); // all
            break;
    }

    auto* callback = findCallback(callbackName);
    if (!callback)
    {
        reportTypeError("Internal error: parameter expansion builtin not found");
        return;
    }
    _result = _builder.createCallFunction(_builder.getBuiltinFunction(*callback), args, "expand.param");
}

void IRGenerator::visit(ast::VariableExpr const& node)
{
    switch (node.type)
    {
        case ast::VariableType::Named: {
            // Call getvar(name) to retrieve the variable value at runtime
            auto* callback = findCallback("getvar(S)S");
            if (!callback)
            {
                reportTypeError("Internal error: getvar builtin not found");
                return;
            }
            _result = _builder.createCallFunction(
                _builder.getBuiltinFunction(*callback), { _builder.get(node.name) }, "getvar");
            break;
        }
        case ast::VariableType::ExitStatus: {
            auto* callback = findCallback("getvar.exitstatus()S");
            if (!callback)
            {
                reportTypeError("Internal error: getvar.exitstatus builtin not found");
                return;
            }
            _result =
                _builder.createCallFunction(_builder.getBuiltinFunction(*callback), {}, "getvar.exitstatus");
            break;
        }
        case ast::VariableType::ProcessId: {
            auto* callback = findCallback("getvar.processid()S");
            if (!callback)
            {
                reportTypeError("Internal error: getvar.processid builtin not found");
                return;
            }
            _result =
                _builder.createCallFunction(_builder.getBuiltinFunction(*callback), {}, "getvar.processid");
            break;
        }
        case ast::VariableType::BackgroundId: {
            auto* callback = findCallback("getvar.backgroundid()S");
            if (!callback)
            {
                reportTypeError("Internal error: getvar.backgroundid builtin not found");
                return;
            }
            _result = _builder.createCallFunction(
                _builder.getBuiltinFunction(*callback), {}, "getvar.backgroundid");
            break;
        }
        case ast::VariableType::Positional: {
            // Convert name to integer index
            int index = 0;
            if (!node.name.empty())
                index = std::stoi(node.name);

            auto* callback = findCallback("getvar.positional(I)S");
            if (!callback)
            {
                reportTypeError("Internal error: getvar.positional builtin not found");
                return;
            }
            _result = _builder.createCallFunction(_builder.getBuiltinFunction(*callback),
                                                  { _builder.get(CoreVM::CoreNumber(index)) },
                                                  "getvar.positional");
            break;
        }
    }
}

void IRGenerator::visit(ast::BuiltinUnsetStmt const& node)
{
    auto callArguments = std::vector<CoreVM::Value*> {};
    callArguments.push_back(_builder.get(node.name));
    _result =
        _builder.createCallFunction(_builder.getBuiltinFunction(node.callback.get()), callArguments, "unset");
}

void IRGenerator::visit(ast::BuiltinJobsStmt const& node)
{
    _result = _builder.createCallFunction(_builder.getBuiltinFunction(node.callback.get()), {}, "jobs");
}

void IRGenerator::visit(ast::BuiltinFgStmt const& node)
{
    if (!node.jobId)
    {
        _result = _builder.createCallFunction(_builder.getBuiltinFunction(node.callback.get()), {}, "fg");
    }
    else
    {
        auto* jobIdValue = codegen(node.jobId.get());
        if (!jobIdValue)
            return;
        if (jobIdValue->type() == CoreVM::LiteralType::String)
            jobIdValue = _builder.createS2N(jobIdValue);
        else if (jobIdValue->type() != CoreVM::LiteralType::Number)
        {
            reportTypeError("job ID must be a number, got {}", jobIdValue->type());
            return;
        }
        _result = _builder.createCallFunction(
            _builder.getBuiltinFunction(node.callback.get()), { jobIdValue }, "fg");
    }
}

void IRGenerator::visit(ast::BuiltinBgStmt const& node)
{
    if (!node.jobId)
    {
        _result = _builder.createCallFunction(_builder.getBuiltinFunction(node.callback.get()), {}, "bg");
    }
    else
    {
        auto* jobIdValue = codegen(node.jobId.get());
        if (!jobIdValue)
            return;
        if (jobIdValue->type() == CoreVM::LiteralType::String)
            jobIdValue = _builder.createS2N(jobIdValue);
        else if (jobIdValue->type() != CoreVM::LiteralType::Number)
        {
            reportTypeError("job ID must be a number, got {}", jobIdValue->type());
            return;
        }
        _result = _builder.createCallFunction(
            _builder.getBuiltinFunction(node.callback.get()), { jobIdValue }, "bg");
    }
}

void IRGenerator::visit(ast::BuiltinWaitStmt const& node)
{
    if (!node.jobId)
    {
        _result = _builder.createCallFunction(_builder.getBuiltinFunction(node.callback.get()), {}, "wait");
    }
    else
    {
        auto* jobIdValue = codegen(node.jobId.get());
        if (!jobIdValue)
            return;
        if (jobIdValue->type() == CoreVM::LiteralType::String)
            jobIdValue = _builder.createS2N(jobIdValue);
        else if (jobIdValue->type() != CoreVM::LiteralType::Number)
        {
            reportTypeError("job ID must be a number, got {}", jobIdValue->type());
            return;
        }
        _result = _builder.createCallFunction(
            _builder.getBuiltinFunction(node.callback.get()), { jobIdValue }, "wait");
    }
}

void IRGenerator::visit(ast::BuiltinBindStmt const& node)
{
    if (node.args.empty())
    {
        _result = _builder.createCallFunction(_builder.getBuiltinFunction(node.callback.get()), {}, "bind");
    }
    else
    {
        auto callArguments = std::vector<CoreVM::Value*> {};
        callArguments.emplace_back(_builder.get(createCallArgs(node.args)));
        _result = _builder.createCallFunction(
            _builder.getBuiltinFunction(node.callback.get()), callArguments, "bind");
    }
}

void IRGenerator::visit(ast::BuiltinWhichStmt const& node)
{
    if (node.args.empty())
    {
        _result = _builder.createCallFunction(_builder.getBuiltinFunction(node.callback.get()), {}, "which");
    }
    else
    {
        auto callArguments = std::vector<CoreVM::Value*> {};
        callArguments.emplace_back(_builder.get(createCallArgs(node.args)));
        _result = _builder.createCallFunction(
            _builder.getBuiltinFunction(node.callback.get()), callArguments, "which");
    }
}

void IRGenerator::visit(ast::OutputRedirect const& node)
{
    if (std::holds_alternative<std::unique_ptr<ast::FileDescriptor>>(node.target))
    {
        // fd duplication: 2>&1
        auto* callback = findCallback("internal.redirect_fd_dup(II)V");
        if (!callback)
        {
            reportTypeError("Internal error: internal.redirect_fd_dup builtin not found");
            return;
        }
        auto* sourceFd = _builder.get(CoreVM::CoreNumber(node.source->value));
        auto* targetFd = _builder.get(
            CoreVM::CoreNumber(std::get<std::unique_ptr<ast::FileDescriptor>>(node.target)->value));
        _result = _builder.createCallFunction(
            _builder.getBuiltinFunction(*callback), { sourceFd, targetFd }, "redirect_fd_dup");
    }
    else
    {
        // file redirect: > file or >> file
        auto* callback = findCallback("internal.redirect_output(ISB)V");
        if (!callback)
        {
            reportTypeError("Internal error: internal.redirect_output builtin not found");
            return;
        }
        auto* sourceFd = _builder.get(CoreVM::CoreNumber(node.source->value));
        auto* target = codegen(std::get<std::unique_ptr<ast::Expr>>(node.target).get());
        if (!target)
            return;
        auto* append = _builder.get(node.append);
        _result = _builder.createCallFunction(
            _builder.getBuiltinFunction(*callback), { sourceFd, target, append }, "redirect_output");
    }
}

void IRGenerator::visit(ast::ProgramCall const& node)
{
    TRACE_SCOPE("ProgramCall");

    bool const hasRedirects = !node.inputRedirects.empty() || !node.outputRedirects.empty()
                              || !node.hereDocuments.empty() || !node.hereStrings.empty();

    // Start redirect context if we have any redirects
    if (hasRedirects)
    {
        auto* startCallback = findCallback("internal.redirect_start()V");
        if (startCallback)
            _builder.createCallFunction(_builder.getBuiltinFunction(*startCallback), {}, "redirect_start");
    }

    // Generate code for all redirects
    for (auto const& redirect: node.inputRedirects)
        codegen(redirect.get());

    for (auto const& redirect: node.outputRedirects)
        codegen(redirect.get());

    for (auto const& heredoc: node.hereDocuments)
        codegen(heredoc.get());

    for (auto const& herestring: node.hereStrings)
        codegen(herestring.get());

    if (containsRuntimeExpr(node.parameters))
    {
        // Use dynamic argument building and execution
        buildCommandArgs(node.program, node.parameters);
        _result = execBuiltCommand();
    }
    else
    {
        // Use constant array (fast path)
        auto callArguments = std::vector<CoreVM::Value*> {};
        callArguments.push_back(_builder.get(createCallArgs(node.program, node.parameters)));
        _result = _builder.createCallFunction(
            _builder.getBuiltinFunction(node.callback.get()), callArguments, "callProcess");
    }

    // End redirect context
    if (hasRedirects)
    {
        auto* endCallback = findCallback("internal.redirect_end()V");
        if (endCallback)
            _builder.createCallFunction(_builder.getBuiltinFunction(*endCallback), {}, "redirect_end");
    }
}

void IRGenerator::visit(ast::SubstitutionExpr const& node)
{
    // Command substitution: $(command) or `command`
    // 1. Start capture - redirects stdout to a pipe
    auto* startCb = findCallback("internal.subst_start()V");
    if (!startCb)
    {
        reportTypeError("Internal error: internal.subst_start builtin not found");
        return;
    }
    _builder.createCallFunction(_builder.getBuiltinFunction(*startCb), {}, "subst_start");

    // 2. Execute the command pipeline
    codegen(node.pipeline.get());

    // 3. End capture - reads captured output and returns as string
    auto* endCb = findCallback("internal.subst_end()S");
    if (!endCb)
    {
        reportTypeError("Internal error: internal.subst_end builtin not found");
        return;
    }
    _result = _builder.createCallFunction(_builder.getBuiltinFunction(*endCb), {}, "subst_end");
}

void IRGenerator::visit(ast::WhileStmt const& node)
{
    CoreVM::BasicBlock* cond = _builder.createBlock("while.cond");
    CoreVM::BasicBlock* body = _builder.createBlock("while.body");
    CoreVM::BasicBlock* end = _builder.createBlock("while.end");

    _builder.createBr(cond);

    _builder.setInsertPoint(cond);
    _builder.createCondBr(toBool(codegen(node.condition.get())), body, end);

    _builder.setInsertPoint(body);
    pushLoopContext(cond, end);
    codegen(node.body.get());
    popLoopContext();
    // Only add loop-back branch if body wasn't terminated (by break/continue/return)
    if (_builder.getInsertPoint() && !_builder.getInsertPoint()->getTerminator())
        _builder.createBr(cond);

    _builder.setInsertPoint(end);
}

void IRGenerator::visit(ast::ForListStmt const& node)
{
    // for var in item1 item2 ...; do body; done
    //
    // IR pattern:
    //   for.init:  index = 0; items = [item1, item2, ...]
    //   for.cond:  if index >= count goto for.end
    //   for.body:  var = items[index]; BODY
    //   for.step:  index++; goto for.cond
    //   for.end:

    // Initialize the iterator
    auto* initIterCb = findCallback("internal.for_init(S)V");
    if (!initIterCb)
    {
        reportTypeError("Internal error: internal.for_init builtin not found");
        return;
    }
    _builder.createCallFunction(
        _builder.getBuiltinFunction(*initIterCb), { _builder.get(node.variable) }, "for_init");

    // Add all items to the iterator
    auto* addItemCb = findCallback("internal.for_add_item(S)V");
    if (!addItemCb)
    {
        reportTypeError("Internal error: internal.for_add_item builtin not found");
        return;
    }
    for (auto const& item: node.items)
    {
        auto* itemValue = codegen(item.get());
        if (itemValue)
            _builder.createCallFunction(
                _builder.getBuiltinFunction(*addItemCb), { itemValue }, "for_add_item");
    }

    CoreVM::BasicBlock* cond = _builder.createBlock("for.cond");
    CoreVM::BasicBlock* body = _builder.createBlock("for.body");
    CoreVM::BasicBlock* step = _builder.createBlock("for.step");
    CoreVM::BasicBlock* end = _builder.createBlock("for.end");

    _builder.createBr(cond);

    // Condition: check if there are more items
    _builder.setInsertPoint(cond);
    auto* hasMoreCb = findCallback("internal.for_has_more()B");
    if (!hasMoreCb)
    {
        reportTypeError("Internal error: internal.for_has_more builtin not found");
        return;
    }
    auto* hasMore = _builder.createCallFunction(_builder.getBuiltinFunction(*hasMoreCb), {}, "for_has_more");
    _builder.createCondBr(hasMore, body, end);

    // Body: set variable to next item and execute body
    _builder.setInsertPoint(body);
    auto* nextCb = findCallback("internal.for_next(S)V");
    if (!nextCb)
    {
        reportTypeError("Internal error: internal.for_next builtin not found");
        return;
    }
    _builder.createCallFunction(
        _builder.getBuiltinFunction(*nextCb), { _builder.get(node.variable) }, "for_next");

    pushLoopContext(step, end);
    codegen(node.body.get());
    popLoopContext();
    // Only add step branch if body wasn't terminated (by break/continue/return)
    if (_builder.getInsertPoint() && !_builder.getInsertPoint()->getTerminator())
        _builder.createBr(step);

    // Step: just loop back to condition (next was already called)
    _builder.setInsertPoint(step);
    _builder.createBr(cond);

    // End: clean up the for-loop state
    _builder.setInsertPoint(end);
    auto* cleanupCb = findCallback("internal.for_cleanup()V");
    if (cleanupCb)
        _builder.createCallFunction(_builder.getBuiltinFunction(*cleanupCb), {}, "for_cleanup");
}

void IRGenerator::visit(ast::ForCStyleStmt const& node)
{
    // for ((init; cond; step)); do body; done
    //
    // IR pattern:
    //   forc.init: eval(init)
    //   forc.cond: if !eval(cond) goto forc.end
    //   forc.body: BODY
    //   forc.step: eval(step); goto forc.cond
    //   forc.end:

    CoreVM::BasicBlock* initBlock = _builder.createBlock("forc.init");
    CoreVM::BasicBlock* cond = _builder.createBlock("forc.cond");
    CoreVM::BasicBlock* body = _builder.createBlock("forc.body");
    CoreVM::BasicBlock* step = _builder.createBlock("forc.step");
    CoreVM::BasicBlock* end = _builder.createBlock("forc.end");

    _builder.createBr(initBlock);

    // Init: evaluate init expression
    _builder.setInsertPoint(initBlock);
    if (node.init)
        codegenArith(node.init.get());
    _builder.createBr(cond);

    // Condition: check condition
    _builder.setInsertPoint(cond);
    if (node.condition)
    {
        auto* condValue = codegenArith(node.condition.get());
        // If condValue is already Boolean (from comparison), use it directly
        // Otherwise, convert to Boolean by comparing with 0
        CoreVM::Value* condBool = condValue;
        if (condValue->type() != CoreVM::LiteralType::Boolean)
            condBool = _builder.createNCmpNE(condValue, _builder.get(CoreVM::CoreNumber(0)));
        _builder.createCondBr(condBool, body, end);
    }
    else
    {
        // No condition = infinite loop (always enter body)
        _builder.createBr(body);
    }

    // Body
    _builder.setInsertPoint(body);
    pushLoopContext(step, end);
    codegen(node.body.get());
    popLoopContext();
    _builder.createBr(step);

    // Step: evaluate step expression and loop
    _builder.setInsertPoint(step);
    if (node.step)
        codegenArith(node.step.get());
    _builder.createBr(cond);

    _builder.setInsertPoint(end);
}

void IRGenerator::visit(ast::CaseStmt const& node)
{
    // case word in pattern1) cmd1;; pattern2) cmd2;; esac
    //
    // IR pattern:
    //   case.word:  word_value = eval(word)
    //   case.check0: if matches(word, patterns[0]) goto case.body0
    //   case.check1: if matches(word, patterns[1]) goto case.body1
    //   ...         goto case.end
    //   case.body0: commands; goto case.end
    //   case.body1: commands; goto case.end
    //   case.end:

    // Evaluate the word first
    auto* wordValue = codegen(node.word.get());
    if (!wordValue)
        return;

    CoreVM::BasicBlock* endBlock = _builder.createBlock("case.end");

    // Create blocks for each clause
    std::vector<CoreVM::BasicBlock*> bodyBlocks;
    std::vector<CoreVM::BasicBlock*> checkBlocks;

    for (size_t i = 0; i < node.clauses.size(); ++i)
    {
        checkBlocks.push_back(_builder.createBlock(std::format("case.check{}", i)));
        bodyBlocks.push_back(_builder.createBlock(std::format("case.body{}", i)));
    }

    // Start checking patterns
    _builder.createBr(checkBlocks.empty() ? endBlock : checkBlocks[0]);

    // Generate pattern matching checks
    auto* matchCb = findCallback("internal.case_match(SS)B");
    if (!matchCb)
    {
        reportTypeError("Internal error: internal.case_match builtin not found");
        return;
    }

    for (size_t i = 0; i < node.clauses.size(); ++i)
    {
        auto const& clause = node.clauses[i];
        _builder.setInsertPoint(checkBlocks[i]);

        // Check each pattern (pipe-separated)
        // For multiple patterns, we chain the checks: if any pattern matches, go to body
        CoreVM::BasicBlock* nextClause = (i + 1 < checkBlocks.size()) ? checkBlocks[i + 1] : endBlock;

        for (size_t p = 0; p < clause.patterns.size(); ++p)
        {
            auto const& pattern = clause.patterns[p];
            auto* match = _builder.createCallFunction(
                _builder.getBuiltinFunction(*matchCb), { wordValue, _builder.get(pattern) }, "case_match");

            // Create intermediate check block for next pattern (if any)
            CoreVM::BasicBlock* nextPatternCheck =
                (p + 1 < clause.patterns.size())
                    ? _builder.createBlock(std::format("case.check{}.pat{}", i, p + 1))
                    : nextClause;

            _builder.createCondBr(match, bodyBlocks[i], nextPatternCheck);

            if (p + 1 < clause.patterns.size())
                _builder.setInsertPoint(nextPatternCheck);
        }

        // Handle empty patterns (shouldn't happen, but defensive)
        if (clause.patterns.empty())
            _builder.createBr(nextClause);
    }

    // Generate body blocks
    for (size_t i = 0; i < node.clauses.size(); ++i)
    {
        auto const& clause = node.clauses[i];
        _builder.setInsertPoint(bodyBlocks[i]);
        if (clause.body)
            codegen(clause.body.get());
        _builder.createBr(endBlock);
    }

    _builder.setInsertPoint(endBlock);
}

void IRGenerator::visit(ast::FunctionDefStmt const& node)
{
    // Register the function for later invocation
    // Functions are compiled as separate handlers and called at runtime
    auto* registerCb = findCallback("internal.function_register(S)V");
    if (!registerCb)
    {
        reportTypeError("Internal error: internal.function_register builtin not found");
        return;
    }

    // Save current handler and insertion point
    auto* savedHandler = _builder.handler();
    auto* savedBlock = _builder.getInsertPoint();

    // Create a new handler for the function and switch to it
    auto* funcHandler = _builder.getHandler(node.name);
    _builder.setHandler(funcHandler);
    auto* entryBlock = _builder.createBlock(node.name + ".entry");
    _builder.setInsertPoint(entryBlock);

    pushFunctionContext();
    codegen(node.body.get());
    popFunctionContext();

    // Always add return at the end - the VM will handle duplicate terminators
    _builder.createRet(_builder.get(CoreVM::CoreNumber(0)));

    // Restore to main handler
    _builder.setHandler(savedHandler);
    _builder.setInsertPoint(savedBlock);

    // Register the function name
    _builder.createCallFunction(
        _builder.getBuiltinFunction(*registerCb), { _builder.get(node.name) }, "function_register");
}

void IRGenerator::visit(ast::BreakStmt const& node)
{
    auto* ctx = getLoopContext(node.levels);
    if (!ctx)
    {
        reportTypeError("break: not in a loop");
        return;
    }
    _builder.createBr(ctx->breakTarget);
}

void IRGenerator::visit(ast::ContinueStmt const& node)
{
    auto* ctx = getLoopContext(node.levels);
    if (!ctx)
    {
        reportTypeError("continue: not in a loop");
        return;
    }
    _builder.createBr(ctx->continueTarget);
}

void IRGenerator::visit(ast::ReturnStmt const& node)
{
    if (!inFunction())
    {
        reportTypeError("return: not in a function");
        return;
    }

    CoreVM::Value* returnValue = nullptr;
    if (node.value)
    {
        returnValue = codegen(node.value.get());
        if (!returnValue)
            return;
        if (returnValue->type() == CoreVM::LiteralType::String)
            returnValue = _builder.createS2N(returnValue);
    }
    else
    {
        // Default to last exit code ($?)
        auto* exitStatusCb = findCallback("getvar.exitstatus()S");
        if (exitStatusCb)
        {
            auto* exitStr = _builder.createCallFunction(
                _builder.getBuiltinFunction(*exitStatusCb), {}, "getvar.exitstatus");
            returnValue = _builder.createS2N(exitStr);
        }
        else
        {
            returnValue = _builder.get(CoreVM::CoreNumber(0));
        }
    }

    // Set $? to the return value before exiting
    auto* setExitCb = findCallback("setvar.exitstatus(I)V");
    if (setExitCb)
        _builder.createCallFunction(
            _builder.getBuiltinFunction(*setExitCb), { returnValue }, "setvar.exitstatus");

    _builder.createRet(returnValue);
}

CoreVM::Value* IRGenerator::toBool(CoreVM::Value* value)
{
    if (value->type() == CoreVM::LiteralType::Boolean)
        return value;
    return _builder.createNCmpEQ(value, _builder.get(CoreVM::CoreNumber(0)));
}

bool IRGenerator::containsRuntimeExpr(std::vector<std::unique_ptr<ast::Expr>> const& expressions) const
{
    for (auto const& expr: expressions)
    {
        if (dynamic_cast<ast::VariableExpr const*>(expr.get()) != nullptr)
            return true;
        if (dynamic_cast<ast::SubstitutionExpr const*>(expr.get()) != nullptr)
            return true;
        if (dynamic_cast<ast::CommandFileSubst const*>(expr.get()) != nullptr)
            return true;
        if (dynamic_cast<ast::TildeExpr const*>(expr.get()) != nullptr)
            return true;
        if (dynamic_cast<ast::ParamExpansionExpr const*>(expr.get()) != nullptr)
            return true;
        if (dynamic_cast<ast::GlobExpr const*>(expr.get()) != nullptr)
            return true;
        if (dynamic_cast<ast::ArithExpansionExpr const*>(expr.get()) != nullptr)
            return true;
        if (dynamic_cast<ast::ConcatExpr const*>(expr.get()) != nullptr)
            return true;
    }
    return false;
}

std::vector<CoreVM::Constant*> IRGenerator::createConstantArray(
    std::vector<std::unique_ptr<ast::Expr>> const& expressions)
{
    auto irArray = std::vector<CoreVM::Constant*> {};
    for (auto const& expr: expressions)
    {
        TRACE_SCOPE(std::format("Parameter: ", ast::ASTPrinter::print(*expr)));
        auto* value = codegen(expr.get());
        if (!value)
        {
            // Error already reported
            continue;
        }
        if (auto* constant = dynamic_cast<CoreVM::Constant*>(value); constant != nullptr)
            irArray.push_back(constant);
        else
        {
            reportTypeError("Non-constant expression in array context");
        }
    }
    return irArray;
}

void IRGenerator::buildCommandArgs(std::string const& programName,
                                   std::vector<std::unique_ptr<ast::Expr>> const& args)
{
    TRACE_SCOPE("buildCommandArgs");

    // Start building the command with the program name
    auto* cmdStartCallback = findCallback("internal.cmd_start(S)V");
    if (!cmdStartCallback)
    {
        reportTypeError("Internal error: internal.cmd_start builtin not found");
        return;
    }
    _builder.createCallFunction(
        _builder.getBuiltinFunction(*cmdStartCallback), { _builder.get(programName) }, "cmd_start");

    // Add each argument
    auto* cmdArgCallback = findCallback("internal.cmd_arg(S)V");
    if (!cmdArgCallback)
    {
        reportTypeError("Internal error: internal.cmd_arg builtin not found");
        return;
    }

    for (auto const& arg: args)
    {
        auto* value = codegen(arg.get());
        if (!value)
            continue; // Error already reported

        _builder.createCallFunction(_builder.getBuiltinFunction(*cmdArgCallback), { value }, "cmd_arg");
    }
}

CoreVM::Value* IRGenerator::execBuiltCommand()
{
    auto* cmdExecCallback = findCallback("internal.cmd_exec()I");
    if (!cmdExecCallback)
    {
        reportTypeError("Internal error: internal.cmd_exec builtin not found");
        return nullptr;
    }
    return _builder.createCallFunction(_builder.getBuiltinFunction(*cmdExecCallback), {}, "cmd_exec");
}

CoreVM::Value* IRGenerator::execBuiltCommandPiped(bool lastInChain)
{
    auto* cmdExecCallback = findCallback("internal.cmd_exec_piped(B)I");
    if (!cmdExecCallback)
    {
        reportTypeError("Internal error: internal.cmd_exec_piped builtin not found");
        return nullptr;
    }
    return _builder.createCallFunction(
        _builder.getBuiltinFunction(*cmdExecCallback), { _builder.get(lastInChain) }, "cmd_exec_piped");
}

CoreVM::Value* IRGenerator::execBuiltCommandPipedBackground(
    std::string const& programName, std::vector<std::unique_ptr<ast::Expr>> const& args)
{
    auto* cmdExecCallback = findCallback("internal.cmd_exec_piped_background(S)I");
    if (!cmdExecCallback)
    {
        reportTypeError("Internal error: internal.cmd_exec_piped_background builtin not found");
        return nullptr;
    }

    // Build the command string for the job table
    std::string command = programName;
    for (auto const& arg: args)
    {
        if (auto const* lit = dynamic_cast<ast::LiteralExpr const*>(arg.get()))
            command += " " + lit->value;
    }
    command += " &";

    return _builder.createCallFunction(_builder.getBuiltinFunction(*cmdExecCallback),
                                       { _builder.get(command) },
                                       "cmd_exec_piped_background");
}

CoreVM::Value* IRGenerator::convertToString(CoreVM::Value* value, std::string_view label)
{
    if (value->type() == CoreVM::LiteralType::Number)
    {
        // Check if this Number is actually a list object pointer (e.g., from list_concat native)
        if (auto objTypeId = getObjectTypeId(value); objTypeId && *objTypeId == CoreVM::BuiltinTypeId::List)
        {
            auto* callback = findCallback("list_to_string(I)S");
            if (callback)
            {
                return _builder.createCallFunction(
                    _builder.getBuiltinFunction(*callback), { value }, std::string(label) + ".list2s");
            }
        }
        return _builder.createN2S(value, std::string(label) + ".n2s");
    }
    if (value->type() == CoreVM::LiteralType::Float)
    {
        return _builder.createF2S(value, std::string(label) + ".f2s");
    }
    if (value->type() == CoreVM::LiteralType::Boolean)
    {
        // Convert boolean to "true"/"false" string via conditional branch
        auto* trueBlock = _builder.createBlock(std::string(label) + ".b2s.true");
        auto* falseBlock = _builder.createBlock(std::string(label) + ".b2s.false");
        auto* mergeBlock = _builder.createBlock(std::string(label) + ".b2s.merge");
        auto* storage =
            createAllocaInEntryBlock(CoreVM::LiteralType::String, std::string(label) + ".b2s.tmp");
        _builder.createCondBr(value, trueBlock, falseBlock);
        _builder.setInsertPoint(trueBlock);
        _builder.createStore(storage, _builder.get("true"));
        _builder.createBr(mergeBlock);
        _builder.setInsertPoint(falseBlock);
        _builder.createStore(storage, _builder.get("false"));
        _builder.createBr(mergeBlock);
        _builder.setInsertPoint(mergeBlock);
        return _builder.createLoad(storage, std::string(label) + ".b2s");
    }
    if (value->type() == CoreVM::LiteralType::Object)
    {
        // Check if value is a known typed object via annotation or IR chain analysis
        bool isList = false;
        if (auto objTypeId = getObjectTypeId(value))
            isList = (*objTypeId == CoreVM::BuiltinTypeId::List);
        else if (auto info = tryGetObjectInfo(value))
            isList = (info->typeId == CoreVM::BuiltinTypeId::List);

        if (isList)
        {
            auto* callback = findCallback("list_to_string(I)S");
            if (callback)
            {
                return _builder.createCallFunction(
                    _builder.getBuiltinFunction(*callback), { value }, std::string(label) + ".list2s");
            }
        }
        // Fallback: assume numeric for unknown object types
        return _builder.createN2S(value, std::string(label) + ".n2s");
    }
    if (value->type() == CoreVM::LiteralType::Void)
    {
        // Dynamically-typed value (e.g., from pattern matching or OGETSLOT).
        // Check if we have a type ID annotation to dispatch correctly.
        if (auto objTypeId = getObjectTypeId(value); objTypeId && *objTypeId == CoreVM::BuiltinTypeId::List)
        {
            auto* callback = findCallback("list_to_string(I)S");
            if (callback)
            {
                return _builder.createCallFunction(
                    _builder.getBuiltinFunction(*callback), { value }, std::string(label) + ".list2s");
            }
        }
        // Fallback: assume numeric for unknown types
        return _builder.createN2S(value, std::string(label) + ".n2s");
    }
    if (value->type() == CoreVM::LiteralType::String)
    {
        return value; // Already a string
    }
    return nullptr; // Unsupported type
}

void IRGenerator::generatePrintCall(ast::Expr const* argument, bool appendNewline)
{
    TRACE_SCOPE("generatePrintCall");

    // Evaluate the argument
    CoreVM::Value* argValue = codegen(argument);
    if (!argValue)
    {
        reportTypeError("Failed to evaluate print argument");
        return;
    }

    // Convert to string if needed
    argValue = convertToString(argValue, "print");
    if (!argValue)
    {
        reportTypeError("print/println requires a string or number argument");
        return;
    }

    // Find the appropriate native callback (using short signature format)
    std::string signature = appendNewline ? "println(S)V" : "print(S)V";

    auto* callback = findCallback(signature);
    if (!callback)
    {
        if (appendNewline)
            reportTypeError("println builtin not available");
        else
            reportTypeError("print builtin not available");
        return;
    }

    // Generate native call
    std::string funcName = appendNewline ? "println" : "print";
    _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { argValue }, funcName);
    _result = _builder.get(CoreVM::CoreNumber(0)); // print/println returns unit
}

bool IRGenerator::tryGenerateBuiltinCall(std::string const& name,
                                         std::vector<ast::Expr const*> const& argExprs)
{
    if (name == "string_length")
    {
        if (argExprs.size() != 1)
        {
            reportTypeError("string_length requires exactly 1 argument, got {}", argExprs.size());
            return true;
        }
        auto* argVal = codegen(argExprs[0]);
        if (!argVal)
        {
            reportTypeError("Failed to evaluate string_length argument");
            return true;
        }
        if (argVal->type() != CoreVM::LiteralType::String)
        {
            reportTypeError("string_length requires a string argument");
            return true;
        }
        _result = _builder.createSLen(argVal, "slen");
        return true;
    }

    if (name == "int_of_string")
    {
        if (argExprs.size() != 1)
        {
            reportTypeError("int_of_string requires exactly 1 argument, got {}", argExprs.size());
            return true;
        }
        auto* argVal = codegen(argExprs[0]);
        if (!argVal)
        {
            reportTypeError("Failed to evaluate int_of_string argument");
            return true;
        }
        _result = _builder.createS2N(argVal, "s2n");
        return true;
    }

    if (name == "string_of_int")
    {
        if (argExprs.size() != 1)
        {
            reportTypeError("string_of_int requires exactly 1 argument, got {}", argExprs.size());
            return true;
        }
        auto* argVal = codegen(argExprs[0]);
        if (!argVal)
        {
            reportTypeError("Failed to evaluate string_of_int argument");
            return true;
        }
        _result = _builder.createN2S(argVal, "n2s");
        return true;
    }

    if (name == "not")
    {
        if (argExprs.size() != 1)
        {
            reportTypeError("not requires exactly 1 argument, got {}", argExprs.size());
            return true;
        }
        auto* argVal = codegen(argExprs[0]);
        if (!argVal)
        {
            reportTypeError("Failed to evaluate not argument");
            return true;
        }
        _result = _builder.createBNot(toBool(argVal), "not");
        return true;
    }

    if (name == "env")
    {
        if (argExprs.size() != 1)
        {
            reportTypeError("env requires exactly 1 argument, got {}", argExprs.size());
            return true;
        }

        // Evaluate key argument
        auto* keyVal = codegen(argExprs[0]);
        if (!keyVal)
        {
            reportTypeError("Failed to evaluate env argument");
            return true;
        }
        if (keyVal->type() != CoreVM::LiteralType::String)
        {
            reportTypeError("env requires a string argument");
            return true;
        }

        // Store key in alloca (needed twice: for env.has and env.get)
        auto* keyStorage = createAllocaInEntryBlock(CoreVM::LiteralType::String, "env.key");
        _builder.createStore(keyStorage, keyVal);

        // Call env.has(key) -> boolean
        auto* envHasCallback = findCallback("env.has(S)B");
        if (!envHasCallback)
        {
            reportTypeError("env.has builtin not available");
            return true;
        }
        auto* keyReload1 = _builder.createLoad(keyStorage, "env.key.reload1");
        auto* hasResult = _builder.createCallFunction(
            _builder.getBuiltinFunction(*envHasCallback), { keyReload1 }, "env.has");

        // Create blocks for branching
        auto* someBlock = _builder.createBlock("env.some");
        auto* noneBlock = _builder.createBlock("env.none");
        auto* mergeBlock = _builder.createBlock("env.merge");

        // Create result alloca for the Option object
        auto* resultStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "env.result");

        // Branch on env.has result
        _builder.createCondBr(hasResult, someBlock, noneBlock);

        // someBlock: call env.get, construct Some
        _builder.setInsertPoint(someBlock);
        auto* keyReload2 = _builder.createLoad(keyStorage, "env.key.reload2");
        auto* envGetCallback = findCallback("env.get(S)S");
        if (!envGetCallback)
        {
            reportTypeError("env.get builtin not available");
            return true;
        }
        auto* getValue = _builder.createCallFunction(
            _builder.getBuiltinFunction(*envGetCallback), { keyReload2 }, "env.get");

        // Construct Some(value): ObjAlloc Option -> ObjSetTag 1 -> ObjSetSlot 0 value
        auto* typeIdSome = _builder.get(CoreVM::CoreNumber(CoreVM::BuiltinTypeId::Option));
        CoreVM::Value* someObj = _builder.createObjAlloc(typeIdSome, "env.some.option");
        someObj = _builder.createObjSetTag(someObj, _builder.get(CoreVM::CoreNumber(1)), "env.some.tag");
        someObj = _builder.createObjSetSlot(
            someObj, _builder.get(CoreVM::CoreNumber(0)), getValue, "env.some.value");
        _builder.createStore(resultStorage, someObj);
        _builder.createBr(mergeBlock);

        // noneBlock: construct None
        _builder.setInsertPoint(noneBlock);
        auto* typeIdNone = _builder.get(CoreVM::CoreNumber(CoreVM::BuiltinTypeId::Option));
        CoreVM::Value* noneObj = _builder.createObjAlloc(typeIdNone, "env.none.option");
        noneObj = _builder.createObjSetTag(noneObj, _builder.get(CoreVM::CoreNumber(0)), "env.none.tag");
        _builder.createStore(resultStorage, noneObj);
        _builder.createBr(mergeBlock);

        // mergeBlock: load result
        _builder.setInsertPoint(mergeBlock);
        _result = _builder.createLoad(resultStorage, "env.result");
        annotateInnerType(_result, CoreVM::LiteralType::String);
        return true;
    }

    return false;
}

std::vector<CoreVM::Constant*> IRGenerator::createCallArgs(
    std::vector<std::unique_ptr<ast::Expr>> const& args)
{
    TRACE_SCOPE("createCallArgs");
    return createConstantArray(args);
}

std::vector<CoreVM::Constant*> IRGenerator::createCallArgs(
    std::string const& programName, std::vector<std::unique_ptr<ast::Expr>> const& args)
{
    TRACE_SCOPE("createCallArgs");
    auto callArguments = createConstantArray(args);
    callArguments.insert(callArguments.begin(), _builder.get(programName));
    return callArguments;
}

void IRGenerator::pushLoopContext(CoreVM::BasicBlock* continueTarget, CoreVM::BasicBlock* breakTarget)
{
    _loopStack.push_back({ continueTarget, breakTarget });
}

void IRGenerator::popLoopContext()
{
    if (!_loopStack.empty())
        _loopStack.pop_back();
}

IRGenerator::LoopContext* IRGenerator::getLoopContext(int levels)
{
    if (_loopStack.empty())
        return nullptr;

    // levels is 1-indexed: break 1 = current loop, break 2 = parent loop
    int const index = static_cast<int>(_loopStack.size()) - levels;
    if (index < 0)
        return nullptr;

    return &_loopStack[static_cast<size_t>(index)];
}

void IRGenerator::pushFunctionContext()
{
    ++_functionDepth;
}

void IRGenerator::popFunctionContext()
{
    if (_functionDepth > 0)
        --_functionDepth;
}

bool IRGenerator::inFunction() const
{
    return _functionDepth > 0;
}

bool IRGenerator::needsDynamicCompare(CoreVM::Value* lhs, CoreVM::Value* rhs) const
{
    // Check if either operand has a dynamic type (from OGETSLOT or similar)
    return lhs->type() == CoreVM::LiteralType::Void || lhs->type() == CoreVM::LiteralType::Object
           || rhs->type() == CoreVM::LiteralType::Void || rhs->type() == CoreVM::LiteralType::Object;
}

// ============================================================================
// F# Style Expressions and Statements (Stubs)
// ============================================================================
// F# Phase 2 expressions: if-then-else, tuples, mutable assignment

void IRGenerator::visit(ast::IfExpr const& node)
{
    TRACE_SCOPE("visit(IfExpr)");

    // Result storage is created lazily after we know the actual type from the branches.
    // createAllocaInEntryBlock() always inserts into the entry block, so calling it later is safe.
    CoreVM::AllocaInstr* resultStorage = nullptr;

    // Codegen condition (not in tail position)
    auto savedTailPos = _inTailPosition;
    _inTailPosition = false;
    auto* condValue = codegen(node.condition.get());
    _inTailPosition = savedTailPos; // Restore for branches
    if (!condValue)
    {
        reportTypeError("Failed to generate code for if condition");
        return;
    }
    auto* condBool = toBool(condValue);

    // Create basic blocks
    auto* thenBlock = _builder.createBlock("if.then");
    auto* elseBlock = _builder.createBlock("if.else");
    auto* mergeBlock = _builder.createBlock("if.merge");

    _builder.createCondBr(condBool, thenBlock, elseBlock);

    // Then branch (inherits tail position from parent)
    _builder.setInsertPoint(thenBlock);
    auto* thenResult = codegen(node.thenExpr.get());
    if (thenResult)
    {
        resultStorage = createAllocaInEntryBlock(thenResult->type(), "if.result");
        _builder.createStore(resultStorage, thenResult, "if.then.store");
        _builder.createBr(mergeBlock);
    }
    else if (_activeRecursion || _activeMutualRecursion || _compilingHandler)
    {
        // Tail call in then branch — no merge needed from this path
    }
    else
    {
        reportTypeError("Failed to generate code for if-then branch");
        return;
    }

    // Else branch
    _builder.setInsertPoint(elseBlock);
    auto* elseResult = codegen(node.elseExpr.get());
    if (elseResult)
    {
        if (resultStorage && thenResult && !typesCompatible(thenResult, elseResult))
        {
            reportTypeError("Type mismatch in if-then-else: 'then' branch is '{}' but 'else' branch is '{}'",
                            typeName(thenResult),
                            typeName(elseResult));
            return;
        }
        if (!resultStorage)
            resultStorage = createAllocaInEntryBlock(elseResult->type(), "if.result");
        _builder.createStore(resultStorage, elseResult, "if.else.store");
        _builder.createBr(mergeBlock);
    }
    else if (_activeRecursion || _activeMutualRecursion || _compilingHandler)
    {
        // Tail call in else branch — no merge needed from this path
    }
    else
    {
        reportTypeError("Failed to generate code for if-else branch");
        return;
    }

    // Merge block: load result
    _builder.setInsertPoint(mergeBlock);
    if (resultStorage)
        _result = _builder.createLoad(resultStorage, "if.result");
    else
        _result = nullptr; // Both branches are tail calls — merge is unreachable
}

void IRGenerator::visit(ast::TupleExpr const& node)
{
    TRACE_SCOPE("visit(TupleExpr)");

    if (node.elements.size() < 2 || node.elements.size() > 3)
    {
        reportTypeError("Tuples must have 2 or 3 elements, got {}", node.elements.size());
        return;
    }

    // Determine the type ID
    auto typeId = node.elements.size() == 2 ? CoreVM::BuiltinTypeId::Tuple2 : CoreVM::BuiltinTypeId::Tuple3;

    // Codegen all elements
    std::vector<CoreVM::Value*> elemValues;
    for (auto const& elem: node.elements)
    {
        auto* val = codegen(elem.get());
        if (!val)
        {
            reportTypeError("Failed to generate code for tuple element");
            return;
        }
        elemValues.push_back(val);
    }

    // Allocate the tuple object
    CoreVM::Value* obj = _builder.createObjAlloc(_builder.get(CoreVM::CoreNumber(typeId)), "tuple");

    // Set each slot
    for (size_t i = 0; i < elemValues.size(); ++i)
    {
        obj =
            _builder.createObjSetSlot(obj, _builder.get(CoreVM::CoreNumber(i)), elemValues[i], "tuple.slot");
    }

    _result = obj;
}

void IRGenerator::visit(ast::MutAssignStmt const& node)
{
    TRACE_SCOPE("visit(MutAssignStmt)");

    // Look up the binding
    auto const* binding = lookupFSharpBinding(node.name);
    if (!binding)
    {
        reportTypeError("Undefined variable: {}", std::string_view(node.name));
        return;
    }

    if (!binding->isMutable)
    {
        reportTypeError(
            "Cannot assign to immutable variable '{}'. Use 'let mut' to declare mutable variables.",
            std::string_view(node.name));
        return;
    }

    // Codegen the new value
    auto* newValue = codegen(node.value.get());
    if (!newValue)
    {
        reportTypeError("Failed to generate code for assignment value");
        return;
    }

    // Store the new value
    _builder.createStore(binding->value, newValue, node.name + ".assign");
    _result = nullptr;
}

// ============================================================================
// These are placeholder implementations. Full implementation will be added
// in a future iteration once the type system and evaluation strategy are finalized.

void IRGenerator::visit(ast::LetBindingStmt const& node)
{
    TRACE_SCOPE("visit(LetBindingStmt)");

    // Destructuring let binding: let (x, y) = expr
    if (node.isDestructuring())
    {
        auto* value = codegen(node.value.get());
        if (!value)
        {
            reportTypeError("Failed to generate code for destructuring let binding value");
            return;
        }

        // Store scrutinee in alloca
        auto* scrutineeStorage = createAllocaInEntryBlock(value->type(), "destructure.scrutinee");
        _builder.createStore(scrutineeStorage, value, "destructure.store");

        // Collect binding names from the pattern
        auto bindingNames = pattern::collectBindings(*node.destructurePattern);

        // Pre-allocate storage for each binding
        std::unordered_map<std::string, CoreVM::AllocaInstr*> bindingStorage;
        for (auto const& name: bindingNames)
        {
            auto* alloca = createAllocaInEntryBlock(CoreVM::LiteralType::Void, name);
            bindingStorage[name] = alloca;
        }

        // Use PatternIRGenerator to match and bind
        PatternIRGenerator patternGen(_builder);
        patternGen.setBindingStorage(bindingStorage);

        auto* successBlock = _builder.createBlock("destructure.ok");
        auto* failBlock = _builder.createBlock("destructure.fail");

        patternGen.compile(*node.destructurePattern, value, scrutineeStorage, successBlock, failBlock);

        // Fail block: runtime error (tuple destructure failed)
        _builder.setInsertPoint(failBlock);
        _builder.createRet(_builder.get(CoreVM::CoreNumber(1)));

        // Success block: register all bindings in F# scope
        _builder.setInsertPoint(successBlock);
        for (auto const& name: bindingNames)
            bindFSharpVariable(name, bindingStorage[name], node.isMutable);

        _result = nullptr;
        return;
    }

    if (node.isFunction())
    {
        // Function definition: let add x y = x + y
        // Store the function for later inlining during application
        // We don't compile it now - we'll inline the body when called

        // For mutual recursion (let rec f ... and g ...), register all names first
        // so that captured-variable analysis can see sibling functions
        auto allRecNames = std::vector<std::string> {};
        if (node.isRecursive)
        {
            allRecNames.push_back(node.name);
            for (auto const& ab: node.andBindings)
                allRecNames.push_back(ab.name);
        }

        auto const isMutual = allRecNames.size() > 1;

        // Register the primary function
        {
            FSharpFunction func;
            extractTypedParameters(node.parameters, func);
            applyInferredTypes(node.name, func);
            func.returnType = node.returnType;
            func.body = node.value.get();
            func.returnKind = determineReturnKind(func.body);
            func.isRecursive = node.isRecursive;
            if (isMutual)
                func.mutualGroup = allRecNames;

            auto allBound = func.parameters;
            for (auto const& rn: allRecNames)
                allBound.push_back(rn);
            func.capturedBindings = collectFreeVariables(func.body, allBound);

            registerFSharpFunction(node.name, std::move(func));

            // Compile functions as separate IRHandlers (with captures as extra params).
            // For recursive functions, compiledHandler is set before body codegen so that
            // recursive references emit UCALL/UTCALL instead of infinite AST inlining.
            if (auto* registered = const_cast<FSharpFunction*>(lookupFSharpFunction(node.name)))
                compileFunctionAsHandler(node.name, *registered);
        }

        // Register 'and' bindings (mutual recursion partners)
        for (auto const& ab: node.andBindings)
        {
            FSharpFunction func;
            extractTypedParameters(ab.parameters, func);
            applyInferredTypes(ab.name, func);
            func.returnType = ab.returnType;
            func.body = ab.value.get();
            func.returnKind = determineReturnKind(func.body);
            func.isRecursive = true;
            if (isMutual)
                func.mutualGroup = allRecNames;

            auto allBound = func.parameters;
            for (auto const& rn: allRecNames)
                allBound.push_back(rn);
            func.capturedBindings = collectFreeVariables(func.body, allBound);

            registerFSharpFunction(ab.name, std::move(func));
        }

        // Compile mutual recursion 'and' bindings as handlers (after ALL are registered)
        if (isMutual)
        {
            for (auto const& ab: node.andBindings)
            {
                if (auto* registered = const_cast<FSharpFunction*>(lookupFSharpFunction(ab.name)))
                    compileFunctionAsHandler(ab.name, *registered);
            }
        }

        _result = nullptr;
        return;
    }

    // Simple binding: let x = expr
    // Special case: let f = fun x -> x * 2 (lambda assigned to variable)
    // We register the lambda as a function under the variable name
    if (auto const* lambda = dynamic_cast<ast::LambdaExpr const*>(node.value.get()))
    {
        FSharpFunction func;
        extractTypedParameters(lambda->parameters, func);
        applyInferredTypes(node.name, func);
        func.body = lambda->body.get();
        func.returnKind = determineReturnKind(func.body);
        func.capturedBindings = collectFreeVariables(func.body, func.parameters);
        registerFSharpFunction(node.name, std::move(func));

        // Compile lambda-as-variable as separate IRHandler (with captures as extra params)
        if (auto* registered = const_cast<FSharpFunction*>(lookupFSharpFunction(node.name)))
            compileFunctionAsHandler(node.name, *registered);

        _result = nullptr;
        return;
    }

    // Check if the expression produces an object (Option/Result/Tuple)
    // These need special tracking for reference counting.
    // NOTE: TryExpr (?) is NOT included — it unwraps the inner value, which is a primitive,
    // not an object that needs ORELEASE.
    bool isObjectExpr = dynamic_cast<ast::OptionExpr const*>(node.value.get()) != nullptr
                        || dynamic_cast<ast::ResultExpr const*>(node.value.get()) != nullptr
                        || dynamic_cast<ast::TupleExpr const*>(node.value.get()) != nullptr
                        || dynamic_cast<ast::ListExpr const*>(node.value.get()) != nullptr
                        || dynamic_cast<ast::ListRangeExpr const*>(node.value.get()) != nullptr
                        || dynamic_cast<ast::ConsExpr const*>(node.value.get()) != nullptr
                        || dynamic_cast<ast::ConcatListExpr const*>(node.value.get()) != nullptr;

    // Codegen the value expression
    CoreVM::Value* value = codegen(node.value.get());
    if (!value)
    {
        reportTypeError("Failed to generate code for let binding value");
        return;
    }

    // Validate type annotation on simple binding (returnType serves as binding type)
    if (node.returnType)
        validateTypeAnnotation(*node.returnType, value, std::format("binding '{}'", node.name));

    // Check if the value is a function reference (string constant naming a function).
    // This handles: let add5 = add 5  (partial application returns "__lambda_0")
    //               let g = f          (function-as-value returns "f")
    if (value->type() == CoreVM::LiteralType::String)
    {
        if (auto* strConst = dynamic_cast<CoreVM::ConstantString*>(value))
        {
            if (auto const* srcFunc = lookupFSharpFunction(strConst->get()))
            {
                registerFSharpFunction(node.name, *srcFunc);
                _result = nullptr;
                return;
            }
        }
    }

    // Determine the type for storage
    CoreVM::LiteralType storageType = value->type();

    // Create storage (alloca) for the variable in the entry block
    // This ensures proper stack tracking in TargetCodeGenerator even if the value
    // expression created new blocks (like match expressions)
    CoreVM::AllocaInstr* storage = createAllocaInEntryBlock(storageType, node.name);

    // Store the value
    _builder.createStore(storage, value, node.name);

    // Propagate inner type annotation through the binding
    if (auto innerType = getInnerType(value))
        annotateInnerType(storage, *innerType);

    // Propagate object type ID annotation through the binding
    if (auto objTypeId = getObjectTypeId(value))
        annotateObjectTypeId(storage, *objTypeId);

    // Propagate inner object type ID annotation through the binding
    if (auto innerObjTypeId = getInnerObjectTypeId(value))
        annotateInnerObjectTypeId(storage, *innerObjTypeId);

    // Register in F# scope - track objects for ORELEASE at scope exit
    if (isObjectExpr)
    {
        bindFSharpObjectVariable(node.name, storage, node.isMutable);
    }
    else
    {
        bindFSharpVariable(node.name, storage, node.isMutable);
    }

    // Record for REPL persistence (re-evaluated at each subsequent prompt)
    _newValueBindings.push_back({ node.name, node.value.get(), node.isMutable, isObjectExpr, storageType });

    // Let bindings as statements don't produce a result value
    _result = nullptr;
}

void IRGenerator::visit(ast::LetInExpr const& node)
{
    TRACE_SCOPE("visit(LetInExpr)");

    pushFSharpScope();

    // Destructuring let-in: let (x, y) = expr in body
    if (node.isDestructuring())
    {
        auto savedTailPos = _inTailPosition;
        _inTailPosition = false; // Binding value is not in tail position
        auto* value = codegen(node.value.get());
        _inTailPosition = savedTailPos;
        if (!value)
        {
            popFSharpScope();
            reportTypeError("Failed to evaluate destructuring let-in binding value");
            return;
        }

        auto* scrutineeStorage = createAllocaInEntryBlock(value->type(), "destructure.scrutinee");
        _builder.createStore(scrutineeStorage, value, "destructure.store");

        auto bindingNames = pattern::collectBindings(*node.destructurePattern);

        std::unordered_map<std::string, CoreVM::AllocaInstr*> bindingStorage;
        for (auto const& name: bindingNames)
        {
            auto* alloca = createAllocaInEntryBlock(CoreVM::LiteralType::Void, name);
            bindingStorage[name] = alloca;
        }

        PatternIRGenerator patternGen(_builder);
        patternGen.setBindingStorage(bindingStorage);

        auto* successBlock = _builder.createBlock("destructure.ok");
        auto* failBlock = _builder.createBlock("destructure.fail");

        patternGen.compile(*node.destructurePattern, value, scrutineeStorage, successBlock, failBlock);

        _builder.setInsertPoint(failBlock);
        _builder.createRet(_builder.get(CoreVM::CoreNumber(1)));

        _builder.setInsertPoint(successBlock);
        for (auto const& name: bindingNames)
            bindFSharpVariable(name, bindingStorage[name]);

        _result = codegen(node.body.get());
        popFSharpScope();
        return;
    }

    if (node.isFunction())
    {
        // Function binding: let f x = body in expr
        FSharpFunction func;
        extractTypedParameters(node.parameters, func);
        applyInferredTypes(node.name, func);
        func.returnType = node.returnType;
        func.body = node.value.get();
        func.returnKind = determineReturnKind(func.body);
        func.isRecursive = node.isRecursive;
        func.capturedBindings = collectFreeVariables(func.body, func.parameters);

        registerFSharpFunction(node.name, std::move(func));
    }
    else
    {
        // Simple binding: let x = expr in body (value is not in tail position)
        auto savedTailPos = _inTailPosition;
        _inTailPosition = false;
        auto* value = codegen(node.value.get());
        _inTailPosition = savedTailPos;
        if (!value)
        {
            popFSharpScope();
            reportTypeError("Failed to evaluate let-in binding value");
            return;
        }

        // Validate type annotation on simple let-in binding
        if (node.returnType)
            validateTypeAnnotation(*node.returnType, value, std::format("binding '{}'", node.name));

        auto* storage = createAllocaInEntryBlock(value->type(), node.name);
        _builder.createStore(storage, value, node.name + ".store");

        // Propagate inner type annotation through the binding
        if (auto innerType = getInnerType(value))
            annotateInnerType(storage, *innerType);

        // Propagate object type ID annotation through the binding
        if (auto objTypeId = getObjectTypeId(value))
            annotateObjectTypeId(storage, *objTypeId);

        // Propagate inner object type ID annotation through the binding
        if (auto innerObjTypeId = getInnerObjectTypeId(value))
            annotateInnerObjectTypeId(storage, *innerObjTypeId);

        bindFSharpVariable(node.name, storage);
    }

    // Evaluate the body expression with the binding in scope
    _result = codegen(node.body.get());

    popFSharpScope();
}

void IRGenerator::visit(ast::ExprStmt const& node)
{
    TRACE_SCOPE("visit(ExprStmt)");
    // Expression statement: evaluate the expression for its side effects
    // The result is discarded
    codegen(node.expr.get());
    _result = nullptr;
}

void IRGenerator::visit(ast::BinaryExpr const& node)
{
    TRACE_SCOPE("visit(BinaryExpr)");

    // Binary operands are not in tail position
    auto savedTailPos = _inTailPosition;
    _inTailPosition = false;

    // Codegen both operands
    CoreVM::Value* left = codegen(node.left.get());
    if (!left)
    {
        if (_activeRecursion)
        {
            reportTypeError("Non-tail recursive call detected. Recursive calls must be in tail position. "
                            "Use an accumulator parameter to restructure the recursion.");
        }
        return;
    }

    CoreVM::Value* right = codegen(node.right.get());
    if (!right)
    {
        if (_activeRecursion)
        {
            reportTypeError("Non-tail recursive call detected. Recursive calls must be in tail position. "
                            "Use an accumulator parameter to restructure the recursion.");
        }
        return;
    }

    // String concatenation: if + operator and either operand is a string, concat
    if (node.op == ast::BinaryOp::Add
        && (left->type() == CoreVM::LiteralType::String || right->type() == CoreVM::LiteralType::String))
    {
        if (left->type() == CoreVM::LiteralType::Float)
            left = _builder.createF2S(left);
        else if (left->type() != CoreVM::LiteralType::String)
            left = _builder.createN2S(left);
        if (right->type() == CoreVM::LiteralType::Float)
            right = _builder.createF2S(right);
        else if (right->type() != CoreVM::LiteralType::String)
            right = _builder.createN2S(right);
        _result = _builder.createSAdd(left, right, "concat");
        return;
    }

    // String repetition: "ha" * 3 or 3 * "ha"
    if (node.op == ast::BinaryOp::Mul
        && (left->type() == CoreVM::LiteralType::String || right->type() == CoreVM::LiteralType::String))
    {
        auto* strVal = left->type() == CoreVM::LiteralType::String ? left : right;
        auto* countVal = left->type() == CoreVM::LiteralType::String ? right : left;
        if (countVal->type() == CoreVM::LiteralType::String)
            countVal = _builder.createS2N(countVal);
        auto* cb = findCallback("string_repeat(SI)S");
        if (!cb)
        {
            reportTypeError("Internal error: string_repeat builtin not found");
            return;
        }
        _result =
            _builder.createCallFunction(_builder.getBuiltinFunction(*cb), { strVal, countVal }, "srepeat");
        return;
    }

    // Float promotion: if either operand is Float, promote the other to Float
    auto const isFloat = [](CoreVM::Value* v) {
        return v->type() == CoreVM::LiteralType::Float;
    };
    if (isFloat(left) || isFloat(right))
    {
        if (!isFloat(left))
        {
            if (left->type() == CoreVM::LiteralType::String)
                left = _builder.createS2F(left);
            else
                left = _builder.createN2F(left);
        }
        if (!isFloat(right))
        {
            if (right->type() == CoreVM::LiteralType::String)
                right = _builder.createS2F(right);
            else
                right = _builder.createN2F(right);
        }

        switch (node.op)
        {
            case ast::BinaryOp::Add: _result = _builder.createFAdd(left, right, "fadd"); break;
            case ast::BinaryOp::Sub: _result = _builder.createFSub(left, right, "fsub"); break;
            case ast::BinaryOp::Mul: _result = _builder.createFMul(left, right, "fmul"); break;
            case ast::BinaryOp::Div: _result = _builder.createFDiv(left, right, "fdiv"); break;
            case ast::BinaryOp::Mod: _result = _builder.createFRem(left, right, "fmod"); break;
            case ast::BinaryOp::Pow: _result = _builder.createFPow(left, right, "fpow"); break;
            case ast::BinaryOp::Eq: _result = _builder.createFCmpEQ(left, right, "feq"); break;
            case ast::BinaryOp::Ne: _result = _builder.createFCmpNE(left, right, "fne"); break;
            case ast::BinaryOp::Lt: _result = _builder.createFCmpLT(left, right, "flt"); break;
            case ast::BinaryOp::Le: _result = _builder.createFCmpLE(left, right, "fle"); break;
            case ast::BinaryOp::Gt: _result = _builder.createFCmpGT(left, right, "fgt"); break;
            case ast::BinaryOp::Ge: _result = _builder.createFCmpGE(left, right, "fge"); break;
            case ast::BinaryOp::And: _result = _builder.createBAnd(toBool(left), toBool(right), "and"); break;
            case ast::BinaryOp::Or: _result = _builder.createBOr(toBool(left), toBool(right), "or"); break;
        }
        return;
    }

    // For arithmetic and comparison, ensure operands are numbers
    if (left->type() == CoreVM::LiteralType::String)
        left = _builder.createS2N(left);
    if (right->type() == CoreVM::LiteralType::String)
        right = _builder.createS2N(right);

    switch (node.op)
    {
        // Arithmetic operators
        case ast::BinaryOp::Add: _result = _builder.createAdd(left, right, "add"); break;
        case ast::BinaryOp::Sub: _result = _builder.createSub(left, right, "sub"); break;
        case ast::BinaryOp::Mul: _result = _builder.createMul(left, right, "mul"); break;
        case ast::BinaryOp::Div: _result = _builder.createDiv(left, right, "div"); break;
        case ast::BinaryOp::Mod: _result = _builder.createRem(left, right, "mod"); break;
        case ast::BinaryOp::Pow: _result = _builder.createPow(left, right, "pow"); break;

        // Comparison operators (return boolean)
        // Use dynamic comparison (VCmpXX) when operands have unknown compile-time types
        case ast::BinaryOp::Eq:
            _result = needsDynamicCompare(left, right) ? _builder.createVCmpEQ(left, right, "eq")
                                                       : _builder.createNCmpEQ(left, right, "eq");
            break;
        case ast::BinaryOp::Ne:
            _result = needsDynamicCompare(left, right) ? _builder.createVCmpNE(left, right, "ne")
                                                       : _builder.createNCmpNE(left, right, "ne");
            break;
        case ast::BinaryOp::Lt:
            _result = needsDynamicCompare(left, right) ? _builder.createVCmpLT(left, right, "lt")
                                                       : _builder.createNCmpLT(left, right, "lt");
            break;
        case ast::BinaryOp::Le:
            _result = needsDynamicCompare(left, right) ? _builder.createVCmpLE(left, right, "le")
                                                       : _builder.createNCmpLE(left, right, "le");
            break;
        case ast::BinaryOp::Gt:
            _result = needsDynamicCompare(left, right) ? _builder.createVCmpGT(left, right, "gt")
                                                       : _builder.createNCmpGT(left, right, "gt");
            break;
        case ast::BinaryOp::Ge:
            _result = needsDynamicCompare(left, right) ? _builder.createVCmpGE(left, right, "ge")
                                                       : _builder.createNCmpGE(left, right, "ge");
            break;

        // Logical operators
        case ast::BinaryOp::And: _result = _builder.createBAnd(toBool(left), toBool(right), "and"); break;
        case ast::BinaryOp::Or: _result = _builder.createBOr(toBool(left), toBool(right), "or"); break;
    }

    _inTailPosition = savedTailPos;
}

void IRGenerator::visit(ast::UnaryExpr const& node)
{
    TRACE_SCOPE("visit(UnaryExpr)");

    CoreVM::Value* operand = codegen(node.operand.get());
    if (!operand)
        return;

    switch (node.op)
    {
        case ast::UnaryOp::Neg:
            if (operand->type() == CoreVM::LiteralType::Float)
                _result = _builder.createFNeg(operand, "fneg");
            else
            {
                // Ensure operand is a number for negation
                if (operand->type() == CoreVM::LiteralType::String)
                    operand = _builder.createS2N(operand);
                _result = _builder.createNeg(operand, "neg");
            }
            break;

        case ast::UnaryOp::Not: _result = _builder.createBNot(toBool(operand), "not"); break;
    }
}

void IRGenerator::visit(ast::PipelineExpr const& node)
{
    TRACE_SCOPE("visit(PipelineExpr)");

    // Pipeline: value |> function
    // This is syntactic sugar for function application: f(value)
    // value |> f is equivalent to f value

    // Evaluate the value (left-hand side)
    CoreVM::Value* value = codegen(node.value.get());
    if (!value)
    {
        reportTypeError("Failed to evaluate pipeline value");
        return;
    }

    // The function (right-hand side) can be:
    // 1. An identifier (named function): 5 |> double
    // 2. A lambda expression: 5 |> (fun x -> x * 2)
    // 3. A parenthesized lambda: 5 |> (fun x -> x * 2)
    FSharpFunction const* func = nullptr;
    std::string funcName;

    // Unwrap ParenExpr if present
    ast::Expr const* funcExpr = node.function.get();
    while (auto const* paren = dynamic_cast<ast::ParenExpr const*>(funcExpr))
        funcExpr = paren->inner.get();

    if (auto const* funcIdent = dynamic_cast<ast::IdentifierExpr const*>(funcExpr))
    {
        // Check for builtin functions first (string_length, etc.)
        // For pipelines, we need to pass the piped value as the single argument
        if (funcIdent->name == "print" || funcIdent->name == "println")
        {
            // Special case: pipe to print/println
            CoreVM::Value* argValue = convertToString(value, "pipe");
            if (!argValue)
            {
                reportTypeError("print/println requires a string or number argument");
                return;
            }

            auto* callback = findCallback(funcIdent->name == "println" ? "println(S)V" : "print(S)V");
            if (callback)
            {
                _builder.createCallFunction(
                    _builder.getBuiltinFunction(*callback), { argValue }, funcIdent->name);
            }
            _result = nullptr;
            return;
        }

        // Check other builtins
        // Build a temporary argument expression list pointing to a synthetic node.
        // Since builtins codegen their args, and we already have the value, we use
        // a different approach: codegen the value manually for builtins.
        if (funcIdent->name == "string_length")
        {
            if (value->type() != CoreVM::LiteralType::String)
            {
                reportTypeError("string_length requires a string argument");
                return;
            }
            _result = _builder.createSLen(value, "slen");
            return;
        }
        if (funcIdent->name == "int_of_string")
        {
            _result = _builder.createS2N(value, "s2n");
            return;
        }
        if (funcIdent->name == "string_of_int")
        {
            _result = _builder.createN2S(value, "n2s");
            return;
        }
        if (funcIdent->name == "not")
        {
            _result = _builder.createBNot(toBool(value), "not");
            return;
        }

        // Named function or stored lambda
        funcName = funcIdent->name;
        func = lookupFSharpFunction(funcName);
        if (!func)
        {
            reportTypeError("Undefined function in pipeline: {}", std::string_view(funcName));
            return;
        }
    }
    else if (auto const* lambda = dynamic_cast<ast::LambdaExpr const*>(funcExpr))
    {
        // Lambda expression - register it as an anonymous function
        funcName = generateLambdaName();
        FSharpFunction lambdaFunc;
        extractTypedParameters(lambda->parameters, lambdaFunc);
        lambdaFunc.body = lambda->body.get();
        lambdaFunc.returnKind = determineReturnKind(lambdaFunc.body);
        lambdaFunc.capturedBindings = collectFreeVariables(lambdaFunc.body, lambdaFunc.parameters);
        registerFSharpFunction(funcName, std::move(lambdaFunc));
        func = lookupFSharpFunction(funcName);
    }
    else if (auto const* app = dynamic_cast<ast::ApplicationExpr const*>(funcExpr))
    {
        // Partial application in pipeline: value |> func arg
        // Flatten the application to get base function + explicit args
        std::vector<ast::Expr const*> explicitArgExprs;
        ast::Expr const* base = funcExpr;
        while (auto const* innerApp = dynamic_cast<ast::ApplicationExpr const*>(base))
        {
            explicitArgExprs.push_back(innerApp->argument.get());
            base = innerApp->function.get();
        }
        std::reverse(explicitArgExprs.begin(), explicitArgExprs.end());

        // Unwrap parens
        while (auto const* paren = dynamic_cast<ast::ParenExpr const*>(base))
            base = paren->inner.get();

        auto const* baseIdent = dynamic_cast<ast::IdentifierExpr const*>(base);
        if (!baseIdent)
        {
            reportTypeError("Pipeline partial application requires a named function");
            return;
        }

        auto const* baseFunc = lookupFSharpFunction(baseIdent->name);
        if (!baseFunc)
        {
            reportTypeError("Undefined function in pipeline: {}", std::string_view(baseIdent->name));
            return;
        }

        // Total args = explicit args + piped value (last parameter)
        if (explicitArgExprs.size() + 1 != baseFunc->arity())
        {
            reportTypeError("Pipeline function '{}' expects {} {}, got {} (including piped value)",
                            std::string_view(baseIdent->name),
                            baseFunc->arity(),
                            baseFunc->arity() == 1 ? "argument" : "arguments",
                            explicitArgExprs.size() + 1);
            return;
        }

        // Evaluate explicit args
        std::vector<CoreVM::Value*> allArgs;
        for (auto const* argExpr: explicitArgExprs)
        {
            auto* argVal = codegen(argExpr);
            if (!argVal)
            {
                reportTypeError("Failed to evaluate pipeline argument");
                return;
            }
            allArgs.push_back(argVal);
        }
        // Piped value is the last argument
        allArgs.push_back(value);

        // Inline the function body with all arguments
        funcName = baseIdent->name;
        func = baseFunc;

        pushFSharpScope();

        // Re-bind captured variables
        for (auto const& [name, storage]: func->capturedBindings)
            bindFSharpVariable(name, storage);

        CoreVM::BasicBlock* returnBlock = nullptr;
        CoreVM::AllocaInstr* returnStorage = nullptr;
        if (func->returnKind != ReturnKind::Plain)
        {
            returnBlock = _builder.createBlock("pipe.return");
            returnStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "pipe.result");
            pushFSharpFunctionContext(returnBlock, returnStorage, func->returnKind);
        }

        for (size_t i = 0; i < func->parameters.size(); ++i)
        {
            auto storageType = allArgs[i]->type();
            auto* paramStorage = createAllocaInEntryBlock(storageType, func->parameters[i]);
            _builder.createStore(paramStorage, allArgs[i], func->parameters[i]);
            bindFSharpVariable(func->parameters[i], paramStorage);
        }

        auto* bodyResult = codegen(func->body);

        if (func->returnKind != ReturnKind::Plain)
        {
            if (bodyResult)
            {
                auto* storeValue = needsAutoWrap(func->body)
                                       ? wrapInResultOrOption(bodyResult, func->returnKind)
                                       : bodyResult;
                _builder.createStore(returnStorage, storeValue, "store.result");
                _builder.createBr(returnBlock);
            }
            _builder.setInsertPoint(returnBlock);
            _result = _builder.createLoad(returnStorage, "load.result");
            popFSharpFunctionContext();
        }
        else
        {
            _result = bodyResult;
        }

        popFSharpScope();
        return;
    }
    else
    {
        reportTypeError("Pipeline function must be an identifier, lambda, or partial application");
        return;
    }

    // For pipeline, the value becomes the first (and for now, only) argument
    if (func->arity() != 1)
    {
        reportTypeError("Pipeline function '{}' must take exactly 1 argument, got {}",
                        std::string_view(funcName),
                        func->arity());
        return;
    }

    // Handle recursive function via pipeline (e.g., 10 |> countdown)
    if (func->isRecursive)
    {
        // Reuse the same loop-based compilation as ApplicationExpr Case A
        auto* entryBlock = _builder.createBlock("rec.entry");
        auto* exitBlock = _builder.createBlock("rec.exit");

        auto* paramAlloca = createAllocaInEntryBlock(value->type(), "rec.param." + func->parameters[0]);
        _builder.createStore(paramAlloca, value, "rec.param.init");

        auto* resultStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Number, "rec.result");

        _activeRecursion = RecursiveCallContext {
            .functionName = funcName,
            .entryBlock = entryBlock,
            .paramAllocas = { paramAlloca },
            .resultStorage = resultStorage,
            .exitBlock = exitBlock,
        };

        _builder.createBr(entryBlock);
        _builder.setInsertPoint(entryBlock);

        pushFSharpScope();
        for (auto const& [capName, capStorage]: func->capturedBindings)
            bindFSharpVariable(capName, capStorage);
        bindFSharpVariable(func->parameters[0], paramAlloca);

        auto* bodyResult = codegen(func->body);
        if (bodyResult)
        {
            _builder.createStore(resultStorage, bodyResult, "rec.store.result");
            _builder.createBr(exitBlock);
        }

        popFSharpScope();

        _builder.setInsertPoint(exitBlock);
        _result = _builder.createLoad(resultStorage, "rec.load.result");

        _activeRecursion.reset();
        return;
    }

    // Non-recursive: inline the function body with the piped value as argument
    pushFSharpScope();

    // Re-bind captured variables from the closure
    for (auto const& [capName, capStorage]: func->capturedBindings)
        bindFSharpVariable(capName, capStorage);

    // Only set up return infrastructure for functions that return Result/Option
    CoreVM::BasicBlock* returnBlock = nullptr;
    CoreVM::AllocaInstr* returnStorage = nullptr;

    if (func->returnKind != ReturnKind::Plain)
    {
        returnBlock = _builder.createBlock("pipe.return");
        returnStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "pipe.result");
        pushFSharpFunctionContext(returnBlock, returnStorage, func->returnKind);
    }

    // Bind the piped value to the parameter
    CoreVM::LiteralType storageType = value->type();
    CoreVM::AllocaInstr* storage = createAllocaInEntryBlock(storageType, func->parameters[0]);
    _builder.createStore(storage, value, func->parameters[0]);
    bindFSharpVariable(func->parameters[0], storage);

    // Inline the function body
    CoreVM::Value* bodyResult = codegen(func->body);

    if (func->returnKind != ReturnKind::Plain)
    {
        // Store result and jump to return block (normal path)
        if (bodyResult)
        {
            auto* storeValue =
                needsAutoWrap(func->body) ? wrapInResultOrOption(bodyResult, func->returnKind) : bodyResult;
            _builder.createStore(returnStorage, storeValue, "store.result");
            _builder.createBr(returnBlock);
        }

        // Continue from return block (merges normal and early return paths)
        _builder.setInsertPoint(returnBlock);
        _result = _builder.createLoad(returnStorage, "load.result");

        popFSharpFunctionContext();
    }
    else
    {
        // Simple case: no error propagation
        _result = bodyResult;
    }

    popFSharpScope();
}

void IRGenerator::visit(ast::ApplicationExpr const& node)
{
    TRACE_SCOPE("visit(ApplicationExpr)");

    // Function application: f x y is parsed as ApplicationExpr(ApplicationExpr(f, x), y)
    // We need to flatten this to get the function name and all arguments

    // Collect arguments in reverse order (innermost first)
    std::vector<ast::Expr const*> argExprs;
    ast::Expr const* current = &node;

    while (auto const* app = dynamic_cast<ast::ApplicationExpr const*>(current))
    {
        argExprs.push_back(app->argument.get());
        current = app->function.get();
    }

    // Reverse to get arguments in correct order (first arg first)
    std::reverse(argExprs.begin(), argExprs.end());

    // Unwrap ParenExpr if present
    while (auto const* paren = dynamic_cast<ast::ParenExpr const*>(current))
        current = paren->inner.get();

    // Check for builtin print/println functions
    if (auto const* funcIdent = dynamic_cast<ast::IdentifierExpr const*>(current))
    {
        if (funcIdent->name == "print" || funcIdent->name == "println")
        {
            if (argExprs.size() != 1)
            {
                reportTypeError("{} requires exactly one string argument", std::string_view(funcIdent->name));
                return;
            }
            generatePrintCall(argExprs[0], funcIdent->name == "println");
            return;
        }

        // Check for standard library builtins (string_length, etc.)
        if (tryGenerateBuiltinCall(funcIdent->name, argExprs))
            return;
    }

    // Evaluate all arguments (arguments are NOT in tail position)
    auto savedTailPos = _inTailPosition;
    _inTailPosition = false;
    std::vector<CoreVM::Value*> args;
    for (ast::Expr const* argExpr: argExprs)
    {
        CoreVM::Value* argValue = codegen(argExpr);
        if (!argValue)
        {
            reportTypeError("Failed to evaluate function argument");
            return;
        }
        args.push_back(argValue);
    }
    _inTailPosition = savedTailPos; // Restore: the call itself inherits parent's tail position

    // The base can be:
    // 1. An identifier (named function): double 5
    // 2. A lambda expression: (fun x -> x * 2) 5
    FSharpFunction const* func = nullptr;
    std::string funcName;

    if (auto const* funcIdent = dynamic_cast<ast::IdentifierExpr const*>(current))
    {
        // Named function or stored lambda
        funcName = funcIdent->name;
        func = lookupFSharpFunction(funcName);
        if (!func)
        {
            reportTypeError("Undefined function: {}", std::string_view(funcName));
            return;
        }
    }
    else if (auto const* lambda = dynamic_cast<ast::LambdaExpr const*>(current))
    {
        // Lambda expression - register it as an anonymous function
        funcName = generateLambdaName();
        FSharpFunction lambdaFunc;
        extractTypedParameters(lambda->parameters, lambdaFunc);
        lambdaFunc.body = lambda->body.get();
        lambdaFunc.returnKind = determineReturnKind(lambdaFunc.body);
        lambdaFunc.capturedBindings = collectFreeVariables(lambdaFunc.body, lambdaFunc.parameters);
        registerFSharpFunction(funcName, std::move(lambdaFunc));
        func = lookupFSharpFunction(funcName);
    }
    else
    {
        reportTypeError("Function application requires a function name or lambda");
        return;
    }

    // Check arity: over-application is an error, under-application creates partial application
    if (args.size() > func->arity())
    {
        reportTypeError("Function '{}' expects {} {}, got {}",
                        std::string_view(funcName),
                        func->arity(),
                        func->arity() == 1 ? "argument" : "arguments",
                        args.size());
        return;
    }

    if (args.size() < func->arity())
    {
        generatePartialApplication(func, funcName, args);
        return;
    }

    // If the function was compiled as a handler (UCALL/UTCALL), use that path
    // regardless of whether it's recursive or not.
    if (func->compiledHandler)
    {
        generateFSharpCall(func, funcName, args);
        return;
    }

    // Fallback: loop-based recursion for untyped recursive functions
    if (func->isRecursive)
    {
        if (!func->mutualGroup.empty())
            generateMutualRecursiveCall(func, funcName, args);
        else
            generateRecursiveCall(func, funcName, args);
        return;
    }

    // Non-recursive function: inline the function body
    generateFSharpCall(func, funcName, args);
}

void IRGenerator::generatePartialApplication(FSharpFunction const* func,
                                             std::string const& funcName,
                                             std::vector<CoreVM::Value*> const& args)
{
    // Partial application: validate supplied argument types
    for (size_t i = 0; i < args.size(); ++i)
    {
        if (i < func->parameterTypes.size() && func->parameterTypes[i])
        {
            if (!validateTypeAnnotation(*func->parameterTypes[i],
                                        args[i],
                                        std::format("parameter '{}' of '{}'", func->parameters[i], funcName)))
                return;
        }
    }

    // Create a new function with remaining parameters
    std::unordered_map<std::string, CoreVM::Value*> newCaptures = func->capturedBindings;
    for (size_t i = 0; i < args.size(); ++i)
    {
        auto const& paramName = func->parameters[i];
        auto* alloca = createAllocaInEntryBlock(args[i]->type(), "partial." + paramName);
        _builder.createStore(alloca, args[i], "partial.store." + paramName);
        newCaptures[paramName] = alloca;
    }

    auto partialName = generateLambdaName();
    FSharpFunction partialFunc;
    partialFunc.parameters = { func->parameters.begin() + static_cast<ptrdiff_t>(args.size()),
                               func->parameters.end() };
    if (func->parameterTypes.size() > args.size())
        partialFunc.parameterTypes = { func->parameterTypes.begin() + static_cast<ptrdiff_t>(args.size()),
                                       func->parameterTypes.end() };
    partialFunc.returnType = func->returnType;
    partialFunc.body = func->body;
    partialFunc.returnKind = func->returnKind;
    partialFunc.isRecursive = false;
    partialFunc.capturedBindings = std::move(newCaptures);

    registerFSharpFunction(partialName, std::move(partialFunc));
    _result = _builder.get(partialName);
}

void IRGenerator::generateMutualRecursiveCall(FSharpFunction const* func,
                                              std::string const& funcName,
                                              std::vector<CoreVM::Value*> const& args)
{
    // Case B (mutual): Tail-call from within a mutual recursion body
    if (_activeMutualRecursion)
    {
        if (auto const* slot = _activeMutualRecursion->findFunction(funcName))
        {
            // Store new argument values into the target function's param allocas
            for (size_t i = 0; i < args.size(); ++i)
                _builder.createStore(slot->paramAllocas[i], args[i], "mutual.arg.update");

            // Set dispatch tag to route to the target function
            _builder.createStore(_activeMutualRecursion->dispatchTag,
                                 _builder.get(CoreVM::CoreNumber(slot->dispatchIndex)),
                                 "mutual.dispatch.update");

            // Jump back to dispatch loop entry
            _builder.createBr(_activeMutualRecursion->dispatchBlock);

            // Create unreachable continuation block (code after tail call is dead)
            auto* unreachable = _builder.createBlock("mutual.unreachable");
            _builder.setInsertPoint(unreachable);

            _result = nullptr;
            return;
        }
    }

    // Case A (mutual): First external call — set up dispatch loop
    auto* dispatchBlock = _builder.createBlock("mutual.dispatch");
    auto* exitBlock = _builder.createBlock("mutual.exit");
    auto* resultStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Number, "mutual.result");
    auto* dispatchTag = createAllocaInEntryBlock(CoreVM::LiteralType::Number, "mutual.tag");

    // Build the mutual recursion context with param allocas for every function
    MutualRecursionContext ctx;
    ctx.dispatchBlock = dispatchBlock;
    ctx.exitBlock = exitBlock;
    ctx.resultStorage = resultStorage;
    ctx.dispatchTag = dispatchTag;

    int calledIndex = -1;
    for (size_t i = 0; i < func->mutualGroup.size(); ++i)
    {
        auto const& fnName = func->mutualGroup[i];
        auto const* fn = lookupFSharpFunction(fnName);

        MutualRecursionContext::FunctionSlot slot;
        slot.name = fnName;
        slot.dispatchIndex = static_cast<int>(i);
        for (auto const& param: fn->parameters)
        {
            auto* alloca =
                createAllocaInEntryBlock(CoreVM::LiteralType::Number, "mutual." + fnName + "." + param);
            slot.paramAllocas.push_back(alloca);
        }
        ctx.functions.push_back(std::move(slot));

        if (fnName == funcName)
            calledIndex = static_cast<int>(i);
    }

    // Store initial dispatch tag and arguments for the called function
    _builder.createStore(dispatchTag, _builder.get(CoreVM::CoreNumber(calledIndex)), "mutual.tag.init");
    for (size_t i = 0; i < args.size(); ++i)
        _builder.createStore(ctx.functions[calledIndex].paramAllocas[i], args[i], "mutual.arg.init");

    _activeMutualRecursion = std::move(ctx);

    // Jump to the dispatch loop
    _builder.createBr(dispatchBlock);
    _builder.setInsertPoint(dispatchBlock);

    // Create body blocks for each function
    std::vector<CoreVM::BasicBlock*> bodyBlocks;
    for (auto const& fn: func->mutualGroup)
        bodyBlocks.push_back(_builder.createBlock("mutual.body." + fn));

    // Generate dispatch chain: tag == 0 → body[0], tag == 1 → body[1], ...
    auto* tagValue = _builder.createLoad(dispatchTag, "mutual.tag.load");
    for (size_t i = 0; i + 1 < func->mutualGroup.size(); ++i)
    {
        auto* nextCheck = _builder.createBlock("mutual.check." + std::to_string(i + 1));
        auto* cmp = _builder.createNCmpEQ(tagValue, _builder.get(CoreVM::CoreNumber(static_cast<int>(i))));
        _builder.createCondBr(cmp, bodyBlocks[i], nextCheck);
        _builder.setInsertPoint(nextCheck);
    }
    // Last function: unconditional branch
    _builder.createBr(bodyBlocks.back());

    // Generate each function body
    for (size_t i = 0; i < func->mutualGroup.size(); ++i)
    {
        _builder.setInsertPoint(bodyBlocks[i]);
        auto const& fnName = func->mutualGroup[i];
        auto const* fn = lookupFSharpFunction(fnName);
        auto const& slot = _activeMutualRecursion->functions[i];

        pushFSharpScope();
        for (auto const& [capName, capStorage]: fn->capturedBindings)
            bindFSharpVariable(capName, capStorage);
        for (size_t j = 0; j < fn->parameters.size(); ++j)
            bindFSharpVariable(fn->parameters[j], slot.paramAllocas[j]);

        // Codegen body (recursive calls within will hit Case B above)
        auto* bodyResult = codegen(fn->body);

        if (bodyResult)
        {
            _builder.createStore(resultStorage, bodyResult, "mutual.store.result");
            _builder.createBr(exitBlock);
        }

        popFSharpScope();
    }

    // Continue from exit block
    _builder.setInsertPoint(exitBlock);
    _result = _builder.createLoad(resultStorage, "mutual.load.result");

    _activeMutualRecursion.reset();
}

void IRGenerator::generateRecursiveCall(FSharpFunction const* func,
                                        std::string const& funcName,
                                        std::vector<CoreVM::Value*> const& args)
{
    // Case B: Recursive tail-call from within body — jump back to entry block
    if (_activeRecursion && _activeRecursion->functionName == funcName)
    {
        // Store new argument values into parameter allocas
        for (size_t i = 0; i < args.size(); ++i)
            _builder.createStore(_activeRecursion->paramAllocas[i], args[i], "rec.arg.update");

        // Jump back to the entry block (tail-call as loop iteration)
        _builder.createBr(_activeRecursion->entryBlock);

        // Create unreachable continuation block (code after tail call is dead)
        auto* unreachable = _builder.createBlock("rec.unreachable");
        _builder.setInsertPoint(unreachable);

        // Signal tail call: result is nullptr (no value produced inline)
        _result = nullptr;
        return;
    }

    // Case A: First (external) call to recursive function — set up loop

    // Validate parameter type annotations at entry point
    for (size_t i = 0; i < func->parameters.size(); ++i)
    {
        if (i < func->parameterTypes.size() && func->parameterTypes[i])
        {
            if (!validateTypeAnnotation(*func->parameterTypes[i],
                                        args[i],
                                        std::format("parameter '{}' of '{}'", func->parameters[i], funcName)))
                return;
        }
    }

    auto* entryBlock = _builder.createBlock("rec.entry");
    auto* exitBlock = _builder.createBlock("rec.exit");

    // Create parameter allocas and result storage in the handler entry block
    std::vector<CoreVM::AllocaInstr*> paramAllocas;
    for (size_t i = 0; i < func->parameters.size(); ++i)
    {
        auto* alloca = createAllocaInEntryBlock(args[i]->type(), "rec.param." + func->parameters[i]);
        _builder.createStore(alloca, args[i], "rec.param.init");
        paramAllocas.push_back(alloca);
    }

    auto* resultStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Number, "rec.result");

    // Set up the recursion context
    _activeRecursion = RecursiveCallContext {
        .functionName = funcName,
        .entryBlock = entryBlock,
        .paramAllocas = paramAllocas,
        .resultStorage = resultStorage,
        .exitBlock = exitBlock,
    };

    // Jump to entry block and begin the loop
    _builder.createBr(entryBlock);
    _builder.setInsertPoint(entryBlock);

    // Push scope and bind captures and parameters from allocas
    pushFSharpScope();
    for (auto const& [capName, capStorage]: func->capturedBindings)
        bindFSharpVariable(capName, capStorage);
    for (size_t i = 0; i < func->parameters.size(); ++i)
        bindFSharpVariable(func->parameters[i], paramAllocas[i]);

    // Codegen the function body (recursive calls will hit Case B above)
    auto* bodyResult = codegen(func->body);

    // If body produced a result (non-tail path), store it and branch to exit
    if (bodyResult)
    {
        _builder.createStore(resultStorage, bodyResult, "rec.store.result");
        _builder.createBr(exitBlock);
    }

    popFSharpScope();

    // Continue from the exit block, load the result
    _builder.setInsertPoint(exitBlock);
    _result = _builder.createLoad(resultStorage, "rec.load.result");

    // Clear the recursion context
    _activeRecursion.reset();
}

void IRGenerator::generateFSharpCall(FSharpFunction const* func,
                                     std::string const& funcName,
                                     std::vector<CoreVM::Value*> const& args)
{
    // If this function was compiled as a separate IRHandler, emit a function call instruction
    if (func->compiledHandler)
    {
        // Validate parameter type annotations before the call
        for (size_t i = 0; i < func->parameters.size(); ++i)
        {
            if (i < func->parameterTypes.size() && func->parameterTypes[i])
            {
                if (!validateTypeAnnotation(
                        *func->parameterTypes[i],
                        args[i],
                        std::format("parameter '{}' of '{}'", func->parameters[i], funcName)))
                    return;
            }
        }

        // Build full argument list: captured variables first, then explicit arguments.
        // When inside a handler compilation (recursive call), load captures from the
        // handler's own scope variables (not from the outer scope's capturedBindings).
        std::vector<CoreVM::Value*> fullArgs;
        fullArgs.reserve(func->captureOrder.size() + args.size());
        for (auto const& capName: func->captureOrder)
        {
            CoreVM::Value* capStorage = nullptr;
            if (_compilingHandler)
            {
                // Inside a handler: look up capture in the current scope
                capStorage = lookupFSharpVariable(capName);
            }
            if (!capStorage)
            {
                // Outside handler or not found in scope: use outer capturedBindings
                capStorage = func->capturedBindings.at(capName);
            }
            fullArgs.push_back(_builder.createLoad(capStorage, "cap." + capName));
        }
        fullArgs.insert(fullArgs.end(), args.begin(), args.end());

        // Emit tail call (UTCALL) when in tail position inside a handler compilation,
        // otherwise emit regular function call (UCALL).
        if (_inTailPosition && _compilingHandler)
        {
            _builder.createTailCall(func->compiledHandler, fullArgs, funcName + ".tailcall");

            // Create unreachable continuation block (code after tail call is dead)
            auto* unreachable = _builder.createBlock("tailcall.unreachable");
            _builder.setInsertPoint(unreachable);

            _result = nullptr; // Tail call doesn't produce a value in the current handler
        }
        else
        {
            _result = _builder.createFunctionCall(
                func->compiledHandler, fullArgs, funcName + ".call", func->compiledReturnType);
        }
        return;
    }

    // Non-recursive function: inline the function body
    // 1. Push a new scope for the function call
    // 2. Re-bind captured variables from the closure
    // 3. Set up return infrastructure for ? operator (only if function returns Result/Option)
    // 4. Bind arguments to parameters
    // 5. Evaluate the function body
    // 6. Handle normal return path and merge with early returns (if applicable)
    // 7. Pop scope and return result

    pushFSharpScope();

    // Re-bind captured variables from the closure
    for (auto const& [capName, capStorage]: func->capturedBindings)
        bindFSharpVariable(capName, capStorage);

    // Only set up return infrastructure for functions that return Result/Option
    // This is needed for the ? operator to propagate errors
    CoreVM::BasicBlock* returnBlock = nullptr;
    CoreVM::AllocaInstr* returnStorage = nullptr;

    if (func->returnKind != ReturnKind::Plain)
    {
        returnBlock = _builder.createBlock("func.return");
        returnStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "func.result");
        pushFSharpFunctionContext(returnBlock, returnStorage, func->returnKind);
    }

    // Bind arguments to parameter names (with type annotation validation)
    for (size_t i = 0; i < func->parameters.size(); ++i)
    {
        // Validate parameter type annotation if present
        if (i < func->parameterTypes.size() && func->parameterTypes[i])
        {
            if (!validateTypeAnnotation(*func->parameterTypes[i],
                                        args[i],
                                        std::format("parameter '{}' of '{}'", func->parameters[i], funcName)))
                return;
        }

        CoreVM::LiteralType storageType = args[i]->type();
        CoreVM::AllocaInstr* storage = createAllocaInEntryBlock(storageType, func->parameters[i]);
        _builder.createStore(storage, args[i], func->parameters[i]);
        bindFSharpVariable(func->parameters[i], storage);
    }

    // Inline the function body
    CoreVM::Value* bodyResult = codegen(func->body);

    // Validate return type annotation if present
    if (bodyResult && func->returnType)
        validateTypeAnnotation(*func->returnType, bodyResult, std::format("return type of '{}'", funcName));

    if (func->returnKind != ReturnKind::Plain)
    {
        // Store result and jump to return block (normal path)
        if (bodyResult)
        {
            auto* storeValue =
                needsAutoWrap(func->body) ? wrapInResultOrOption(bodyResult, func->returnKind) : bodyResult;
            _builder.createStore(returnStorage, storeValue, "store.result");
            _builder.createBr(returnBlock);
        }

        // Continue from return block (merges normal and early return paths)
        _builder.setInsertPoint(returnBlock);
        _result = _builder.createLoad(returnStorage, "load.result");

        popFSharpFunctionContext();
    }
    else
    {
        // Simple case: no error propagation, just use the body result directly
        _result = bodyResult;
    }

    popFSharpScope();
}

void IRGenerator::compileFunctionAsHandler(std::string const& name, FSharpFunction& func)
{
    // Check if all parameters have types (either annotated or inferred).
    // Without types, parameters would get Void type, causing wrong runtime behavior.
    // Type inference should have filled in missing annotations; fall back to AST inlining if not.
    auto const allParamsTyped =
        !func.parameters.empty() && func.parameterTypes.size() == func.parameters.size()
        && std::ranges::all_of(func.parameterTypes, [](auto const& t) { return t.has_value(); });
    if (!func.parameters.empty() && !allParamsTyped)
        return;

    // Compute deterministic capture ordering (sorted alphabetically)
    func.captureOrder.clear();
    for (auto const& [capName, _]: func.capturedBindings)
        func.captureOrder.push_back(capName);
    std::ranges::sort(func.captureOrder);

    // Save current state so we can revert if compilation fails
    auto* savedHandler = _builder.handler();
    auto* savedInsertPoint = _builder.getInsertPoint();
    auto savedHasErrors = _hasErrors;
    auto* bufferedReport = dynamic_cast<CoreVM::diagnostics::BufferedReport*>(&_report);
    auto const savedReportSize = bufferedReport ? bufferedReport->size() : size_t { 0 };

    // Create a new IRHandler for this function
    // Parameter count includes captured variables (prepended) + explicit parameters
    auto* handler = _builder.program()->createHandler("fsharp." + name);
    handler->setParameterCount(func.captureOrder.size() + func.parameters.size());

    // Switch builder to the new handler
    _builder.setHandler(handler);
    auto* entryBlock = _builder.createBlock("entry");
    _builder.setInsertPoint(entryBlock);

    // Push a new scope for the function body
    pushFSharpScope();

    // Create capture parameter allocas first (they occupy the first slots from the caller)
    for (auto const& capName: func.captureOrder)
    {
        auto* sourceStorage = func.capturedBindings.at(capName);
        auto* storage = createAllocaInEntryBlock(sourceStorage->type(), "cap." + capName);
        bindFSharpVariable(capName, storage);
    }

    // Create explicit parameter allocas, using type annotations
    for (size_t i = 0; i < func.parameters.size(); ++i)
    {
        auto paramType = CoreVM::LiteralType::Void;
        if (i < func.parameterTypes.size() && func.parameterTypes[i].has_value())
        {
            if (auto mapped = mapTypeToLiteralType(*func.parameterTypes[i]))
                paramType = *mapped;
        }
        auto* storage = createAllocaInEntryBlock(paramType, func.parameters[i]);
        bindFSharpVariable(func.parameters[i], storage);
    }

    // Set up return infrastructure for ? operator (only if function returns Result/Option)
    CoreVM::BasicBlock* returnBlock = nullptr;
    CoreVM::AllocaInstr* returnStorage = nullptr;

    if (func.returnKind != ReturnKind::Plain)
    {
        returnBlock = _builder.createBlock("func.return");
        returnStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "func.result");
        pushFSharpFunctionContext(returnBlock, returnStorage, func.returnKind);
    }

    // Pre-set compiledHandler so that recursive references during body codegen
    // emit FunctionCallInstr/TailCallInstr instead of trying to inline (which would infinite-loop).
    func.compiledHandler = handler;

    // Track tail position and compiling handler for UCALL/UTCALL decisions
    auto savedTailPosition = _inTailPosition;
    auto* savedCompilingHandler = _compilingHandler;
    _inTailPosition = true;
    _compilingHandler = handler;

    // Codegen the function body
    auto* bodyResult = codegen(func.body);

    // Restore tail position and compiling handler
    _inTailPosition = savedTailPosition;
    _compilingHandler = savedCompilingHandler;

    // Validate return type annotation if present
    if (bodyResult && func.returnType)
        validateTypeAnnotation(*func.returnType, bodyResult, std::format("return type of '{}'", name));

    if (func.returnKind != ReturnKind::Plain)
    {
        if (bodyResult)
        {
            auto* storeValue =
                needsAutoWrap(func.body) ? wrapInResultOrOption(bodyResult, func.returnKind) : bodyResult;
            _builder.createStore(returnStorage, storeValue, "store.result");
            _builder.createBr(returnBlock);
        }
        _builder.setInsertPoint(returnBlock);
        auto* retVal = _builder.createLoad(returnStorage, "load.result");
        _builder.createFunctionRet(retVal, "ret");
        popFSharpFunctionContext();
    }
    else
    {
        if (bodyResult)
            _builder.createFunctionRet(bodyResult, "ret");
    }

    popFSharpScope();

    // Helper to revert compilation state when falling back to AST inlining
    auto const revertToInlining = [&] {
        func.compiledHandler = nullptr; // Reset so fallback path (AST inlining) is used
        _hasErrors = savedHasErrors;
        if (bufferedReport)
            bufferedReport->truncate(savedReportSize);
        _builder.program()->removeHandler(handler);
        _builder.setHandler(savedHandler);
        _builder.setInsertPoint(savedInsertPoint);
    };

    // If compilation produced errors, fall back to AST inlining
    if (_hasErrors && !savedHasErrors)
    {
        revertToInlining();
        return;
    }

    // If bodyResult is null and no errors, all code paths end with tail calls (valid).
    // If bodyResult is null and we're not in a recursive/handler context, it's unexpected.
    if (!bodyResult && !func.isRecursive)
    {
        revertToInlining();
        return;
    }

    // Store the return type (if known from a non-tail-call path)
    if (bodyResult)
        func.compiledReturnType = bodyResult->type();

    // Restore builder state
    _builder.setHandler(savedHandler);
    _builder.setInsertPoint(savedInsertPoint);
}

void IRGenerator::visit(ast::IdentifierExpr const& node)
{
    TRACE_SCOPE("visit(IdentifierExpr)");

    // Look up in F# scope
    CoreVM::Value* storage = lookupFSharpVariable(node.name);
    if (!storage)
    {
        // Fall back to function name lookup (function-as-value)
        if (lookupFSharpFunction(node.name))
        {
            _result = _builder.get(node.name);
            return;
        }
        reportTypeError("Undefined F# identifier: {}", std::string_view(node.name));
        return;
    }

    // Load the value from storage
    _result = _builder.createLoad(storage, node.name);

    // Propagate inner type annotation through variable loads
    if (auto innerType = getInnerType(storage))
        annotateInnerType(_result, *innerType);

    // Propagate object type ID annotation through variable loads
    if (auto objTypeId = getObjectTypeId(storage))
        annotateObjectTypeId(_result, *objTypeId);

    // Propagate inner object type ID annotation through variable loads
    if (auto innerObjTypeId = getInnerObjectTypeId(storage))
        annotateInnerObjectTypeId(_result, *innerObjTypeId);
}

void IRGenerator::visit(ast::IntLiteralExpr const& node)
{
    // Integer literals can be directly converted to CoreVM numbers
    _result = _builder.get(CoreVM::CoreNumber(node.value));
}

void IRGenerator::visit(ast::FloatLiteralExpr const& node)
{
    _result = _builder.getFloat(node.value);
}

void IRGenerator::visit(ast::BoolLiteralExpr const& node)
{
    _result = _builder.getBoolean(node.value);
}

void IRGenerator::visit(ast::ParenExpr const& node)
{
    // Parentheses just evaluate the inner expression
    if (node.inner)
        codegen(node.inner.get());
}

void IRGenerator::visit(ast::LambdaExpr const& node)
{
    TRACE_SCOPE("visit(LambdaExpr)");

    // Lambda expressions are registered as anonymous functions.
    // When used directly in application/pipeline contexts, they are handled there.
    // When used standalone (e.g., let f = fun x -> x * 2), we register and return
    // a "function reference" that can be looked up later.

    std::string lambdaName = generateLambdaName();

    FSharpFunction func;
    extractTypedParameters(node.parameters, func);
    func.body = node.body.get();
    func.returnKind = determineReturnKind(func.body);
    func.capturedBindings = collectFreeVariables(func.body, func.parameters);
    registerFSharpFunction(lambdaName, std::move(func));

    // Store the lambda name in a way that can be retrieved by the calling context.
    // We use a string constant to represent the function reference.
    // This allows let bindings to store the function name for later lookup.
    _result = _builder.get(lambdaName);
}

void IRGenerator::visit(ast::MatchExpr const& node)
{
    TRACE_SCOPE("visit(MatchExpr)");

    // Evaluate the scrutinee (not in tail position)
    auto savedTailPos = _inTailPosition;
    _inTailPosition = false;
    node.scrutinee->accept(*this);
    _inTailPosition = savedTailPos; // Restore for arm bodies
    CoreVM::Value* scrutinee = _result;
    if (!scrutinee)
    {
        reportTypeError("Failed to evaluate match scrutinee");
        return;
    }

    // Store scrutinee in a local variable so it's available across all arms
    // Use createAllocaInEntryBlock to ensure proper stack tracking
    CoreVM::AllocaInstr* scrutineeStorage = createAllocaInEntryBlock(scrutinee->type(), "scrutinee");
    _builder.createStore(scrutineeStorage, scrutinee, "scrutinee.store");

    // Propagate inner object type ID so pattern extraction can annotate bound values
    if (auto innerObjTypeId = getInnerObjectTypeId(scrutinee))
        annotateInnerObjectTypeId(scrutineeStorage, *innerObjTypeId);

    // Result storage is created lazily after we know the actual type from the first arm body.
    // createAllocaInEntryBlock() always inserts into the entry block, so calling it later is safe.
    CoreVM::AllocaInstr* resultStorage = nullptr;

    // Pre-allocate storage for all bindings from all arms in the entry block
    // This is critical: all allocas must be created before any branching to ensure
    // the TargetCodeGenerator's stack tracking remains consistent across all paths.
    PatternIRGenerator patternIRGenerator(_builder);
    std::vector<std::vector<std::pair<std::string, CoreVM::AllocaInstr*>>> armBindingStorage;

    for (size_t i = 0; i < node.arms.size(); ++i)
    {
        auto const& arm = node.arms[i];
        patternIRGenerator.clearBindings();

        // Compile pattern just to collect bindings (we don't emit branches yet)
        // We use a dummy compilation to extract binding names
        patternIRGenerator.collectBindings(*arm.pattern);

        std::vector<std::pair<std::string, CoreVM::AllocaInstr*>> bindings;
        for (auto const& binding: patternIRGenerator.bindings())
        {
            // Use createAllocaInEntryBlock to ensure proper stack tracking
            auto* storage =
                createAllocaInEntryBlock(scrutinee->type(), binding.name + ".arm" + std::to_string(i));
            bindings.emplace_back(binding.name, storage);
        }
        armBindingStorage.push_back(std::move(bindings));
    }

    // Create the merge block where all arms will eventually converge
    auto* mergeBlock = _builder.createBlock("match.merge");

    // Create blocks for each arm body and pattern check
    std::vector<CoreVM::BasicBlock*> armBodyBlocks;
    std::vector<CoreVM::BasicBlock*> patternCheckBlocks;

    for (size_t i = 0; i < node.arms.size(); ++i)
    {
        patternCheckBlocks.push_back(_builder.createBlock("match.check." + std::to_string(i)));
        armBodyBlocks.push_back(_builder.createBlock("match.arm." + std::to_string(i)));
    }

    // Branch to first pattern check block
    _builder.createBr(patternCheckBlocks[0]);

    // Process each arm
    for (size_t i = 0; i < node.arms.size(); ++i)
    {
        auto const& arm = node.arms[i];

        // Set insert point to this arm's pattern check block
        _builder.setInsertPoint(patternCheckBlocks[i]);

        // Determine where to jump on pattern failure
        CoreVM::BasicBlock* onFailure = (i + 1 < node.arms.size()) ? patternCheckBlocks[i + 1] : mergeBlock;

        // Load the scrutinee for this pattern check
        CoreVM::Value* scrutineeValue = _builder.createLoad(scrutineeStorage, "scrutinee.load");

        // Compile the pattern (this emits the pattern matching IR)
        // Pass scrutineeStorage so the pattern can reload when crossing block boundaries
        patternIRGenerator.clearBindings();

        // Provide pre-allocated binding storage so the pattern compiler stores values
        // in the same basic block where they're extracted (avoiding cross-block references)
        {
            std::unordered_map<std::string, CoreVM::AllocaInstr*> storageMap;
            for (auto const& [name, storage]: armBindingStorage[i])
                storageMap[name] = storage;
            patternIRGenerator.setBindingStorage(std::move(storageMap));
        }

        patternIRGenerator.compile(
            *arm.pattern, scrutineeValue, scrutineeStorage, armBodyBlocks[i], onFailure);

        // Emit the arm body
        _builder.setInsertPoint(armBodyBlocks[i]);

        // Install variable bindings from pattern matching in a new scope
        // Use pre-allocated storage from entry block
        pushFSharpScope();
        auto const& preAllocatedBindings = armBindingStorage[i];

        bool const isTuplePattern = dynamic_cast<pattern::TuplePattern const*>(arm.pattern.get()) != nullptr;
        bool const isConsPattern = dynamic_cast<pattern::ConsPattern const*>(arm.pattern.get()) != nullptr;
        bool const isListPattern = dynamic_cast<pattern::ListPattern const*>(arm.pattern.get()) != nullptr;

        if (isTuplePattern || isConsPattern || isListPattern)
        {
            // Tuple/Cons/List patterns: values were already stored into allocas by PatternIRGenerator
            // Just register the allocas as variable bindings
            for (auto const& [name, storage]: preAllocatedBindings)
            {
                bindFSharpVariable(name, storage);
            }
        }
        // For constructor patterns (Error e, Some x), we need to extract the payload
        // For simple variable patterns, we bind the whole scrutinee
        // Only load the scrutinee if there are actual bindings to store.
        // Dead loads leave values on the stack that accumulate in loops (e.g., let rec)
        // and corrupt stack state across basic blocks.
        else if (auto* ctorPatCheck = dynamic_cast<pattern::ConstructorPattern const*>(arm.pattern.get());
                 !preAllocatedBindings.empty() || (ctorPatCheck && ctorPatCheck->payload.has_value()))
        {
            CoreVM::Value* bindingSource = _builder.createLoad(scrutineeStorage, "scrutinee.reload");

            if (auto* ctorPat = dynamic_cast<pattern::ConstructorPattern const*>(arm.pattern.get()))
            {
                // Extract payload from slot 0 for constructor patterns
                if (ctorPat->payload.has_value())
                {
                    bindingSource = _builder.createObjGetSlot(
                        bindingSource, _builder.get(CoreVM::CoreNumber(0)), "ctor.payload");
                    // Recover the inner object's type ID (e.g., List inside Some/Ok)
                    if (auto innerObjTypeId = getInnerObjectTypeId(scrutineeStorage))
                        annotateObjectTypeId(bindingSource, *innerObjTypeId);
                }
            }

            for (auto const& [name, storage]: preAllocatedBindings)
            {
                _builder.createStore(storage, bindingSource, name + ".store");
                // Propagate object type ID to binding storage for convertToString
                if (auto objTypeId = getObjectTypeId(bindingSource))
                    annotateObjectTypeId(storage, *objTypeId);
                bindFSharpVariable(name, storage);
            }
        }

        // If there's a guard, evaluate it and branch accordingly
        if (arm.guard)
        {
            auto* guardPassBlock = _builder.createBlock("match.guard." + std::to_string(i) + ".pass");

            arm.guard->accept(*this);
            CoreVM::Value* guardResult = _result;
            if (!guardResult)
            {
                popFSharpScope();
                reportTypeError("Failed to evaluate match guard");
                return;
            }

            // Convert to bool if needed
            CoreVM::Value* guardBool = toBool(guardResult);
            _builder.createCondBr(guardBool, guardPassBlock, onFailure);

            _builder.setInsertPoint(guardPassBlock);
        }

        // Evaluate the arm body
        arm.body->accept(*this);
        CoreVM::Value* bodyResult = _result;

        popFSharpScope();

        if (!bodyResult)
        {
            // A null result inside an active recursion means a tail call was made.
            // The branch back to the entry block has already been emitted, so skip
            // the store-and-branch-to-merge for this arm.
            if (_activeRecursion || _activeMutualRecursion || _compilingHandler)
                continue;

            reportTypeError("Failed to evaluate match arm body");
            return;
        }

        // Create result storage lazily from the first arm's actual result type
        if (!resultStorage)
            resultStorage = createAllocaInEntryBlock(bodyResult->type(), "match.result");

        // Store the result
        _builder.createStore(resultStorage, bodyResult, "match.result.store");

        // Branch to merge block
        _builder.createBr(mergeBlock);
    }

    // Set insert point to merge block and load the result
    _builder.setInsertPoint(mergeBlock);
    if (resultStorage)
        _result = _builder.createLoad(resultStorage, "match.result.load");
    else
        _result = nullptr; // All arms are tail calls — merge is unreachable
}

void IRGenerator::visit(ast::ListExpr const& node)
{
    TRACE_SCOPE("visit(ListExpr)");

    auto* typeId = _builder.get(CoreVM::CoreNumber(CoreVM::BuiltinTypeId::List));

    // Empty list: just allocate a Nil (tag=0)
    if (node.elements.empty())
    {
        CoreVM::Value* obj = _builder.createObjAlloc(typeId, "list.nil");
        obj = _builder.createObjSetTag(obj, _builder.get(CoreVM::CoreNumber(0)), "list.nil.tag");
        _result = obj;
        annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
        return;
    }

    // Codegen all elements left-to-right (correct evaluation order),
    // storing results in a vector.
    std::vector<CoreVM::Value*> elemValues;
    elemValues.reserve(node.elements.size());
    for (auto const& elem: node.elements)
    {
        auto* val = codegen(elem.get());
        if (!val)
        {
            reportTypeError("Failed to evaluate list element");
            return;
        }
        elemValues.push_back(val);
    }

    // Store evaluated elements in allocas so they survive across basic blocks
    // created by ObjAlloc/ObjSetSlot below.
    std::vector<CoreVM::AllocaInstr*> elemAllocas;
    elemAllocas.reserve(elemValues.size());
    for (size_t i = 0; i < elemValues.size(); ++i)
    {
        auto* alloca = createAllocaInEntryBlock(elemValues[i]->type(), "list.elem." + std::to_string(i));
        _builder.createStore(alloca, elemValues[i]);
        elemAllocas.push_back(alloca);
    }

    // Build the list right-to-left: start with Nil, then prepend elements
    CoreVM::Value* acc = _builder.createObjAlloc(typeId, "list.nil");
    acc = _builder.createObjSetTag(acc, _builder.get(CoreVM::CoreNumber(0)), "list.nil.tag");

    for (int i = static_cast<int>(elemValues.size()) - 1; i >= 0; --i)
    {
        // Store accumulator so it's available after ObjAlloc
        auto* accStorage =
            createAllocaInEntryBlock(CoreVM::LiteralType::Object, "list.acc." + std::to_string(i));
        _builder.createStore(accStorage, acc);

        // Create a Cons cell: tag=1, slot[0]=head, slot[1]=tail
        CoreVM::Value* cons = _builder.createObjAlloc(typeId, "list.cons." + std::to_string(i));
        cons = _builder.createObjSetTag(cons, _builder.get(CoreVM::CoreNumber(1)), "list.cons.tag");

        auto* head = _builder.createLoad(elemAllocas[i], "list.head." + std::to_string(i));
        cons = _builder.createObjSetSlot(cons, _builder.get(CoreVM::CoreNumber(0)), head, "list.cons.head");

        auto* tail = _builder.createLoad(accStorage, "list.tail." + std::to_string(i));
        cons = _builder.createObjSetSlot(cons, _builder.get(CoreVM::CoreNumber(1)), tail, "list.cons.tail");

        acc = cons;
    }

    _result = acc;
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
}

void IRGenerator::visit(ast::ConsExpr const& node)
{
    TRACE_SCOPE("visit(ConsExpr)");

    // Evaluate head and tail
    auto* headVal = codegen(node.head.get());
    if (!headVal)
    {
        reportTypeError("Failed to evaluate cons head expression");
        return;
    }

    // Store head so it survives across tail codegen and object allocation
    auto* headStorage = createAllocaInEntryBlock(headVal->type(), "cons.head.tmp");
    _builder.createStore(headStorage, headVal);

    auto* tailVal = codegen(node.tail.get());
    if (!tailVal)
    {
        reportTypeError("Failed to evaluate cons tail expression");
        return;
    }

    // Store tail so it survives across object allocation
    auto* tailStorage = createAllocaInEntryBlock(tailVal->type(), "cons.tail.tmp");
    _builder.createStore(tailStorage, tailVal);

    // Create a Cons cell: OALLOC List, OSETTAG 1, OSETSLOT 0 head, OSETSLOT 1 tail
    auto* typeId = _builder.get(CoreVM::CoreNumber(CoreVM::BuiltinTypeId::List));
    CoreVM::Value* obj = _builder.createObjAlloc(typeId, "cons");
    obj = _builder.createObjSetTag(obj, _builder.get(CoreVM::CoreNumber(1)), "cons.tag");

    auto* head = _builder.createLoad(headStorage, "cons.head");
    obj = _builder.createObjSetSlot(obj, _builder.get(CoreVM::CoreNumber(0)), head, "cons.head.set");

    auto* tail = _builder.createLoad(tailStorage, "cons.tail");
    obj = _builder.createObjSetSlot(obj, _builder.get(CoreVM::CoreNumber(1)), tail, "cons.tail.set");

    _result = obj;
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
}

void IRGenerator::visit(ast::ConcatListExpr const& node)
{
    TRACE_SCOPE("visit(ConcatListExpr)");

    auto* leftVal = codegen(node.left.get());
    if (!leftVal)
    {
        reportTypeError("Failed to evaluate left operand of list concatenation");
        return;
    }

    // Store left so it survives across right codegen
    auto* leftStorage = createAllocaInEntryBlock(leftVal->type(), "concat.left.tmp");
    _builder.createStore(leftStorage, leftVal);

    auto* rightVal = codegen(node.right.get());
    if (!rightVal)
    {
        reportTypeError("Failed to evaluate right operand of list concatenation");
        return;
    }

    auto* leftReload = _builder.createLoad(leftStorage, "concat.left");

    auto* callback = findCallback("list_concat(II)I");
    if (!callback)
    {
        reportTypeError("list_concat builtin not found");
        return;
    }

    _result = _builder.createCallFunction(
        _builder.getBuiltinFunction(*callback), { leftReload, rightVal }, "concat.result");
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
}

void IRGenerator::visit(ast::ListRangeExpr const& node)
{
    TRACE_SCOPE("visit(ListRangeExpr)");

    // Evaluate start, step, and end expressions
    auto* startVal = codegen(node.start.get());
    if (!startVal)
    {
        reportTypeError("Failed to evaluate list range start expression");
        return;
    }

    auto* endVal = codegen(node.end.get());
    if (!endVal)
    {
        reportTypeError("Failed to evaluate list range end expression");
        return;
    }

    CoreVM::Value* stepVal = nullptr;
    if (node.step)
    {
        stepVal = codegen(node.step.get());
        if (!stepVal)
        {
            reportTypeError("Failed to evaluate list range step expression");
            return;
        }
    }
    else
    {
        stepVal = _builder.get(CoreVM::CoreNumber(1));
    }

    // Store start, step, end in allocas so they survive across loop blocks
    auto* startStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Number, "range.start");
    _builder.createStore(startStorage, startVal);

    auto* stepStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Number, "range.step");
    _builder.createStore(stepStorage, stepVal);

    auto* endStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Number, "range.end");
    _builder.createStore(endStorage, endVal);

    // Compute the last valid element: adjusted_end = start + ((end - start) / step) * step
    // This ensures the loop variable aligns to valid range elements (e.g., [1..10..5] → adjusted=1).
    auto* iStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Number, "range.i");
    auto* loadStart = _builder.createLoad(startStorage, "range.start.adj");
    auto* loadEnd = _builder.createLoad(endStorage, "range.end.adj");
    auto* loadStep = _builder.createLoad(stepStorage, "range.step.adj");
    auto* span = _builder.createSub(loadEnd, loadStart, "range.span");
    auto* count = _builder.createDiv(span, loadStep, "range.count");
    auto* alignedSpan = _builder.createMul(count, loadStep, "range.aligned.span");
    auto* adjustedEnd = _builder.createAdd(loadStart, alignedSpan, "range.adjusted.end");
    _builder.createStore(iStorage, adjustedEnd);

    // Accumulator: starts as Nil
    auto* accStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "range.acc");
    auto* typeId = _builder.get(CoreVM::CoreNumber(CoreVM::BuiltinTypeId::List));
    CoreVM::Value* nil = _builder.createObjAlloc(typeId, "range.nil");
    nil = _builder.createObjSetTag(nil, _builder.get(CoreVM::CoreNumber(0)), "range.nil.tag");
    _builder.createStore(accStorage, nil);

    // Create loop blocks
    auto* condBlock = _builder.createBlock("range.cond");
    auto* bodyBlock = _builder.createBlock("range.body");
    auto* endBlock = _builder.createBlock("range.end");

    _builder.createBr(condBlock);

    // Condition block: check (i - start) * step >= 0
    _builder.setInsertPoint(condBlock);
    auto* iLoad = _builder.createLoad(iStorage, "range.i.cond");
    auto* startLoad = _builder.createLoad(startStorage, "range.start.cond");
    auto* stepLoad = _builder.createLoad(stepStorage, "range.step.cond");
    auto* diff = _builder.createSub(iLoad, startLoad, "range.diff");
    auto* product = _builder.createMul(diff, stepLoad, "range.product");
    auto* zero = _builder.get(CoreVM::CoreNumber(0));
    auto* cond = _builder.createNCmpGE(product, zero, "range.cond.check");
    _builder.createCondBr(cond, bodyBlock, endBlock);

    // Body block: acc = Cons(i, acc), i = i - step
    _builder.setInsertPoint(bodyBlock);
    auto* iBody = _builder.createLoad(iStorage, "range.i.body");
    auto* accBody = _builder.createLoad(accStorage, "range.acc.body");

    // Store i and acc so they survive across ObjAlloc
    auto* iTemp = createAllocaInEntryBlock(CoreVM::LiteralType::Number, "range.i.tmp");
    _builder.createStore(iTemp, iBody);
    auto* accTemp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "range.acc.tmp");
    _builder.createStore(accTemp, accBody);

    // Create Cons cell: tag=1, slot[0]=head(i), slot[1]=tail(acc)
    CoreVM::Value* cons = _builder.createObjAlloc(typeId, "range.cons");
    cons = _builder.createObjSetTag(cons, _builder.get(CoreVM::CoreNumber(1)), "range.cons.tag");

    auto* headReload = _builder.createLoad(iTemp, "range.head.reload");
    cons =
        _builder.createObjSetSlot(cons, _builder.get(CoreVM::CoreNumber(0)), headReload, "range.cons.head");

    auto* tailReload = _builder.createLoad(accTemp, "range.tail.reload");
    cons =
        _builder.createObjSetSlot(cons, _builder.get(CoreVM::CoreNumber(1)), tailReload, "range.cons.tail");

    // Store new acc
    _builder.createStore(accStorage, cons);

    // i = i - step
    auto* iForSub = _builder.createLoad(iStorage, "range.i.sub");
    auto* stepForSub = _builder.createLoad(stepStorage, "range.step.sub");
    auto* newI = _builder.createSub(iForSub, stepForSub, "range.i.next");
    _builder.createStore(iStorage, newI);

    _builder.createBr(condBlock);

    // End block: result is the accumulated list
    _builder.setInsertPoint(endBlock);
    _result = _builder.createLoad(accStorage, "range.result");
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
}

void IRGenerator::visit(ast::ListComprehensionExpr const& node)
{
    // TODO: Implement list comprehensions - requires list type and iteration in CoreVM
    reportTypeError("F# list comprehensions are not yet implemented in IR generator");
    (void) node;
}

void IRGenerator::visit(ast::ShellCommandExpr const& node)
{
    // Shell command expression: & git status
    // Captures command output as a string (like command substitution).
    //
    // IR pattern (same as SubstitutionExpr):
    //   1. Start capture - redirects stdout to a pipe
    //   2. Execute the command pipeline
    //   3. End capture - reads captured output and returns as string

    if (!node.command)
    {
        // Empty command - result is empty string
        _result = _builder.get("");
        return;
    }

    // 1. Start capture - redirects stdout to a pipe
    auto* startCb = findCallback("internal.subst_start()V");
    if (!startCb)
    {
        reportTypeError("Internal error: internal.subst_start builtin not found");
        return;
    }
    _builder.createCallFunction(_builder.getBuiltinFunction(*startCb), {}, "subst_start");

    // 2. Execute the command pipeline
    codegen(node.command.get());

    // 3. End capture - reads captured output and returns as string
    auto* endCb = findCallback("internal.subst_end()S");
    if (!endCb)
    {
        reportTypeError("Internal error: internal.subst_end builtin not found");
        return;
    }
    _result = _builder.createCallFunction(_builder.getBuiltinFunction(*endCb), {}, "subst_end");
}

// ============================================================================
// F# Error Handling Expressions
// ============================================================================

void IRGenerator::pushFSharpFunctionContext(CoreVM::BasicBlock* returnBlock,
                                            CoreVM::AllocaInstr* returnStorage,
                                            ReturnKind returnKind)
{
    _fsharpFunctionContextStack.push_back({ returnBlock, returnStorage, returnKind });
}

void IRGenerator::popFSharpFunctionContext()
{
    if (!_fsharpFunctionContextStack.empty())
        _fsharpFunctionContextStack.pop_back();
}

IRGenerator::FSharpFunctionContext* IRGenerator::currentFSharpFunctionContext()
{
    if (_fsharpFunctionContextStack.empty())
        return nullptr;
    return &_fsharpFunctionContextStack.back();
}

void IRGenerator::visit(ast::OptionExpr const& node)
{
    TRACE_SCOPE("visit(OptionExpr)");

    // Option values are represented as TypedObjects:
    // - Tag 0 = None (no payload)
    // - Tag 1 = Some (1 slot payload)

    // Allocate Option object
    auto* typeId = _builder.get(CoreVM::CoreNumber(CoreVM::BuiltinTypeId::Option));
    CoreVM::Value* obj = _builder.createObjAlloc(typeId, "option");

    if (node.isSome)
    {
        // Some value - evaluate the inner expression
        if (!node.value)
        {
            reportTypeError("Some constructor requires a value");
            return;
        }
        CoreVM::Value* innerValue = codegen(node.value.get());
        if (!innerValue)
            return;

        // Set tag to 1 (Some) and store the value in slot 0
        obj = _builder.createObjSetTag(obj, _builder.get(CoreVM::CoreNumber(1)), "option.tag");
        obj = _builder.createObjSetSlot(obj, _builder.get(CoreVM::CoreNumber(0)), innerValue, "option.value");
        _result = obj;
        annotateInnerType(_result, innerValue->type());
        if (auto objTypeId = getObjectTypeId(innerValue))
            annotateInnerObjectTypeId(_result, *objTypeId);
    }
    else
    {
        // None - just set tag to 0, no payload needed
        obj = _builder.createObjSetTag(obj, _builder.get(CoreVM::CoreNumber(0)), "option.tag");
        _result = obj;
    }
}

void IRGenerator::visit(ast::ResultExpr const& node)
{
    TRACE_SCOPE("visit(ResultExpr)");

    // Result values are represented as TypedObjects:
    // - Tag 0 = Error (1 slot payload)
    // - Tag 1 = Ok (1 slot payload)

    if (!node.payload)
    {
        reportTypeError("Result constructor requires a value");
        return;
    }

    CoreVM::Value* payloadValue = codegen(node.payload.get());
    if (!payloadValue)
        return;

    // Allocate Result object
    auto* typeId = _builder.get(CoreVM::CoreNumber(CoreVM::BuiltinTypeId::Result));
    CoreVM::Value* obj = _builder.createObjAlloc(typeId, "result");

    // Set tag (0=Error, 1=Ok) and store the payload in slot 0
    CoreVM::Value* tag = _builder.get(CoreVM::CoreNumber(node.isOk ? 1 : 0));
    obj = _builder.createObjSetTag(obj, tag, "result.tag");
    obj = _builder.createObjSetSlot(obj, _builder.get(CoreVM::CoreNumber(0)), payloadValue, "result.value");
    _result = obj;
    if (node.isOk)
    {
        annotateInnerType(_result, payloadValue->type());
        if (auto objTypeId = getObjectTypeId(payloadValue))
            annotateInnerObjectTypeId(_result, *objTypeId);
    }
}

void IRGenerator::visit(ast::TryExpr const& node)
{
    TRACE_SCOPE("visit(TryExpr)");

    // The ? operator unwraps a Result or Option value:
    // - If the value is Ok/Some (tag=1), extract and return the inner value
    // - If the value is Error/None (tag=0), propagate the error (early return)
    //
    // This requires a function context to know where to jump on error.

    FSharpFunctionContext* funcCtx = currentFSharpFunctionContext();

    // IMPORTANT: Copy the context values BEFORE calling codegen() below.
    // The operand might be a function application (e.g., `(inc x)?`) which pushes
    // a new FSharpFunctionContext onto _fsharpFunctionContextStack. This can cause
    // the vector to reallocate, invalidating the funcCtx pointer.
    CoreVM::BasicBlock* returnBlock = funcCtx ? funcCtx->returnBlock : nullptr;
    CoreVM::AllocaInstr* returnStorage = funcCtx ? funcCtx->returnStorage : nullptr;

    // Evaluate the operand (should be an Option or Result object)
    // NOTE: This may invalidate funcCtx pointer due to vector reallocation!
    CoreVM::Value* obj = codegen(node.operand.get());
    if (!obj)
        return;

    // Store the object in an alloca so we can reload it in successor blocks.
    // This is necessary because the stack tracking resets at block boundaries.
    CoreVM::AllocaInstr* objStorage = createAllocaInEntryBlock(obj->type(), "try.obj");
    _builder.createStore(objStorage, obj, "try.obj.store");

    // Extract tag using OGETTAG
    CoreVM::Value* tag = _builder.createObjGetTag(obj, "try.tag");

    // Check if success (tag == 1 means Some/Ok)
    CoreVM::Value* isSuccess =
        _builder.createNCmpEQ(tag, _builder.get(CoreVM::CoreNumber(1)), "try.is_success");

    // Pre-allocate result storage in entry block for consistent stack tracking.
    // Use inner type annotation if available (e.g., String for env "USER"),
    // otherwise fall back to Object for unknown/nested cases.
    auto const innerTypeHint = getInnerType(obj);
    auto const resultType = innerTypeHint.value_or(CoreVM::LiteralType::Object);
    CoreVM::AllocaInstr* resultStorage = createAllocaInEntryBlock(resultType, "try.result");

    // Create blocks
    auto* successBlock = _builder.createBlock("try.success");
    auto* errorBlock = _builder.createBlock("try.error");
    auto* continueBlock = _builder.createBlock("try.continue");

    _builder.createCondBr(isSuccess, successBlock, errorBlock);

    // Success path: reload object and extract inner value using OGETSLOT
    _builder.setInsertPoint(successBlock);
    CoreVM::Value* objReload1 = _builder.createLoad(objStorage, "try.obj.reload");
    CoreVM::Value* innerValue =
        _builder.createObjGetSlot(objReload1, _builder.get(CoreVM::CoreNumber(0)), "try.inner");

    // Store result and branch to continue
    _builder.createStore(resultStorage, innerValue, "try.result.store");
    _builder.createBr(continueBlock);

    // Error path: propagate (early return from function or exit handler)
    _builder.setInsertPoint(errorBlock);
    if (funcCtx)
    {
        // Function-level: store error object and jump to return block
        CoreVM::Value* objReload2 = _builder.createLoad(objStorage, "try.obj.reload");
        // NOTE: Using local copies of returnStorage/returnBlock since funcCtx pointer
        // may have been invalidated by codegen() above.
        _builder.createStore(returnStorage, objReload2, "try.error.store");
        _builder.createBr(returnBlock);
    }
    else
    {
        // Top-level: exit handler with non-zero exit code
        _builder.createRet(_builder.get(CoreVM::CoreNumber(1)));
    }

    // Continue with extracted value
    _builder.setInsertPoint(continueBlock);
    _result = _builder.createLoad(resultStorage, "try.result.load");
}

void IRGenerator::visit(ast::TryWithExpr const& node)
{
    TRACE_SCOPE("visit(TryWithExpr)");

    // try expr with | pattern -> handler | ...
    //
    // 1. Evaluate the body expression (should be an Option or Result object)
    // 2. Check if it's an error (tag == 0)
    // 3. If success, return the inner value
    // 4. If error, match against handlers (similar to match expression)

    if (!node.body)
    {
        reportTypeError("try-with expression requires a body");
        return;
    }

    // Create result storage - use Object type since we may return either unwrapped value or error
    CoreVM::AllocaInstr* resultStorage =
        createAllocaInEntryBlock(CoreVM::LiteralType::Object, "trywith.result");

    // Create blocks
    auto* successBlock = _builder.createBlock("trywith.success");
    auto* errorBlock = _builder.createBlock("trywith.error");
    auto* mergeBlock = _builder.createBlock("trywith.merge");

    // Evaluate body (should be an Option or Result object)
    CoreVM::Value* bodyObj = codegen(node.body.get());
    if (!bodyObj)
        return;

    // Store bodyObj in an alloca for cross-block access
    // This is required because values don't persist across basic block boundaries
    CoreVM::AllocaInstr* bodyStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "trywith.body");
    _builder.createStore(bodyStorage, bodyObj, "trywith.body.store");

    // Extract tag using OGETTAG
    CoreVM::Value* tag = _builder.createObjGetTag(bodyObj, "trywith.tag");
    CoreVM::Value* isSuccess =
        _builder.createNCmpEQ(tag, _builder.get(CoreVM::CoreNumber(1)), "trywith.is_success");

    _builder.createCondBr(isSuccess, successBlock, errorBlock);

    // Success path: reload object and extract inner value using OGETSLOT
    _builder.setInsertPoint(successBlock);
    CoreVM::Value* bodyReload1 = _builder.createLoad(bodyStorage, "trywith.body.reload");
    CoreVM::Value* successValue =
        _builder.createObjGetSlot(bodyReload1, _builder.get(CoreVM::CoreNumber(0)), "trywith.success_value");
    _builder.createStore(resultStorage, successValue, "trywith.success.store");
    _builder.createBr(mergeBlock);

    // Error path: match against handlers
    _builder.setInsertPoint(errorBlock);

    if (node.handlers.empty())
    {
        // No handlers - just return the error object as-is
        CoreVM::Value* bodyReload2 = _builder.createLoad(bodyStorage, "trywith.body.reload");
        _builder.createStore(resultStorage, bodyReload2, "trywith.error.store");
        _builder.createBr(mergeBlock);
    }
    else
    {
        // Reload the body object for error handling
        CoreVM::Value* bodyReload2 = _builder.createLoad(bodyStorage, "trywith.body.reload");

        // Extract error value from slot 0 (the payload of Error/None)
        CoreVM::Value* errorValue = _builder.createObjGetSlot(
            bodyReload2, _builder.get(CoreVM::CoreNumber(0)), "trywith.error_value");

        // Store error value for pattern matching
        CoreVM::AllocaInstr* errorStorage =
            createAllocaInEntryBlock(CoreVM::LiteralType::Object, "trywith.error");
        _builder.createStore(errorStorage, errorValue, "trywith.error.bind");

        // Pre-create blocks for all handlers
        std::vector<CoreVM::BasicBlock*> handlerCheckBlocks;
        std::vector<CoreVM::BasicBlock*> handlerBodyBlocks;
        for (size_t i = 0; i < node.handlers.size(); ++i)
        {
            handlerCheckBlocks.push_back(_builder.createBlock("trywith.check." + std::to_string(i)));
            handlerBodyBlocks.push_back(_builder.createBlock("trywith.body." + std::to_string(i)));
        }

        // Default block - when no handler matches, propagate error as-is
        auto* defaultBlock = _builder.createBlock("trywith.default");

        // Branch to first handler check
        _builder.createBr(handlerCheckBlocks[0]);

        // Process each handler
        for (size_t i = 0; i < node.handlers.size(); ++i)
        {
            auto const& arm = node.handlers[i];

            // Set insert point to this handler's check block
            _builder.setInsertPoint(handlerCheckBlocks[i]);

            // Determine where to jump on pattern/guard failure
            CoreVM::BasicBlock* onFailure =
                (i + 1 < node.handlers.size()) ? handlerCheckBlocks[i + 1] : defaultBlock;

            // Reload error value for pattern matching
            CoreVM::Value* errorReload = _builder.createLoad(errorStorage, "trywith.error.reload");

            // Check if pattern matches
            bool patternAlwaysMatches = false;

            if (auto* varPat = dynamic_cast<pattern::VariablePattern const*>(arm.pattern.get()))
            {
                // Variable pattern always matches
                patternAlwaysMatches = true;
            }
            else if (auto* wildPat = dynamic_cast<pattern::WildcardPattern const*>(arm.pattern.get()))
            {
                // Wildcard always matches
                patternAlwaysMatches = true;
            }
            else if (auto* ctorPat = dynamic_cast<pattern::ConstructorPattern const*>(arm.pattern.get()))
            {
                // Constructor pattern - check payload if it's a literal
                if (ctorPat->payload.has_value())
                {
                    if (auto* litPat = dynamic_cast<pattern::LiteralPattern const*>(ctorPat->payload->get()))
                    {
                        // Compare error value with literal
                        if (auto* intVal = std::get_if<int64_t>(&litPat->value))
                        {
                            CoreVM::Value* litValue = _builder.get(CoreVM::CoreNumber(*intVal));
                            // Use VCmpEQ for dynamic value comparison (errorReload is from OGETSLOT)
                            CoreVM::Value* matches =
                                _builder.createVCmpEQ(errorReload, litValue, "trywith.lit.cmp");
                            _builder.createCondBr(matches, handlerBodyBlocks[i], onFailure);
                        }
                        else
                        {
                            // Non-integer literal patterns not yet supported
                            patternAlwaysMatches = true;
                        }
                    }
                    else if (dynamic_cast<pattern::WildcardPattern const*>(ctorPat->payload->get()))
                    {
                        // Wildcard payload - always matches
                        patternAlwaysMatches = true;
                    }
                    else if (dynamic_cast<pattern::VariablePattern const*>(ctorPat->payload->get()))
                    {
                        // Variable payload - always matches
                        patternAlwaysMatches = true;
                    }
                }
                else
                {
                    // No payload (e.g., just "None") - always matches for that constructor type
                    patternAlwaysMatches = true;
                }
            }
            else if (auto* litPat = dynamic_cast<pattern::LiteralPattern const*>(arm.pattern.get()))
            {
                // Direct literal pattern - compare error value
                if (auto* intVal = std::get_if<int64_t>(&litPat->value))
                {
                    CoreVM::Value* litValue = _builder.get(CoreVM::CoreNumber(*intVal));
                    // Use VCmpEQ for dynamic value comparison (errorReload is from OGETSLOT)
                    CoreVM::Value* matches = _builder.createVCmpEQ(errorReload, litValue, "trywith.lit.cmp");
                    _builder.createCondBr(matches, handlerBodyBlocks[i], onFailure);
                }
                else
                {
                    // Non-integer literal patterns - fall through
                    patternAlwaysMatches = true;
                }
            }

            if (patternAlwaysMatches)
            {
                _builder.createBr(handlerBodyBlocks[i]);
            }

            // Now emit the handler body
            _builder.setInsertPoint(handlerBodyBlocks[i]);

            pushFSharpScope();

            // Bind pattern variables
            if (auto* varPat = dynamic_cast<pattern::VariablePattern const*>(arm.pattern.get()))
            {
                bindFSharpVariable(varPat->name, errorStorage);
            }
            else if (auto* ctorPat = dynamic_cast<pattern::ConstructorPattern const*>(arm.pattern.get()))
            {
                if (ctorPat->payload.has_value())
                {
                    if (auto* innerVar =
                            dynamic_cast<pattern::VariablePattern const*>(ctorPat->payload->get()))
                    {
                        bindFSharpVariable(innerVar->name, errorStorage);
                    }
                }
            }

            // Check guard if present
            if (arm.guard)
            {
                auto* guardPassBlock = _builder.createBlock("trywith.guard." + std::to_string(i) + ".pass");

                CoreVM::Value* guardResult = codegen(arm.guard.get());
                if (!guardResult)
                {
                    popFSharpScope();
                    return;
                }
                CoreVM::Value* guardBool = toBool(guardResult);
                _builder.createCondBr(guardBool, guardPassBlock, onFailure);

                _builder.setInsertPoint(guardPassBlock);
            }

            // Evaluate handler body
            CoreVM::Value* handlerResult = codegen(arm.body.get());
            popFSharpScope();

            if (!handlerResult)
                return;

            _builder.createStore(
                resultStorage, handlerResult, "trywith.handler." + std::to_string(i) + ".store");
            _builder.createBr(mergeBlock);
        }

        // Default block: no handler matched - propagate error as-is
        _builder.setInsertPoint(defaultBlock);
        CoreVM::Value* bodyReload3 = _builder.createLoad(bodyStorage, "trywith.body.reload");
        _builder.createStore(resultStorage, bodyReload3, "trywith.default.store");
        _builder.createBr(mergeBlock);
    }

    // Merge block: load result
    _builder.setInsertPoint(mergeBlock);
    _result = _builder.createLoad(resultStorage, "trywith.result.load");
}

void IRGenerator::visit(ast::TryFinallyExpr const& node)
{
    TRACE_SCOPE("visit(TryFinallyExpr)");

    // try body finally cleanup
    //
    // Evaluates body, then ALWAYS evaluates cleanup (result discarded),
    // then returns the body's result.
    //
    // When `?` inside body triggers early return, the finally block runs
    // before error propagation continues.

    if (!node.body)
    {
        reportTypeError("try-finally expression requires a body");
        return;
    }

    if (!node.finallyExpr)
    {
        reportTypeError("try-finally expression requires a finally clause");
        return;
    }

    auto* funcCtx = currentFSharpFunctionContext();

    if (!funcCtx)
    {
        // Top-level: simple linear codegen (no ? interception needed)
        auto* bodyVal = codegen(node.body.get());
        if (!bodyVal)
            return;

        // Store body result so cleanup can't clobber it
        auto* bodyResultStorage = createAllocaInEntryBlock(bodyVal->type(), "tryfinally.body.result");
        _builder.createStore(bodyResultStorage, bodyVal, "tryfinally.body.store");

        // Run cleanup (result discarded)
        codegen(node.finallyExpr.get());

        // Return the body's result
        _result = _builder.createLoad(bodyResultStorage, "tryfinally.result.load");
        return;
    }

    // Inside a function context: intercept ? operator's early return

    // Pre-allocate error flag in entry block
    auto* errorFlag = createAllocaInEntryBlock(CoreVM::LiteralType::Number, "tryfinally.error_flag");

    // Save the original return block that ? would jump to
    auto* originalReturnBlock = funcCtx->returnBlock;

    // Create ONLY the error intercept block before body codegen.
    // The ? operator needs this pointer for its returnBlock redirect.
    // Other blocks are created AFTER body codegen so that body blocks
    // (try.success, try.error, try.continue, func.return, etc.)
    // appear before finally blocks in the handler's block list.
    // The TargetCodeGenerator processes blocks in list order, so this
    // ordering ensures the forward pass stack tracking is correct.
    auto* finallyFromError = _builder.createBlock("tryfinally.from_error");

    // Redirect ? operator's return target to our intercept block
    funcCtx->returnBlock = finallyFromError;

    // Suppress tail call optimization during body codegen to prevent
    // skipping the finally block
    auto savedRecursion = std::move(_activeRecursion);
    auto savedMutualRecursion = std::move(_activeMutualRecursion);
    _activeRecursion.reset();
    _activeMutualRecursion.reset();

    // Evaluate body
    auto* bodyVal = codegen(node.body.get());

    // Restore tail call contexts
    _activeRecursion = std::move(savedRecursion);
    _activeMutualRecursion = std::move(savedMutualRecursion);

    // Restore original return block
    // Re-fetch funcCtx since codegen may have caused reallocation
    funcCtx = currentFSharpFunctionContext();
    funcCtx->returnBlock = originalReturnBlock;

    if (!bodyVal)
    {
        // Body codegen failed (could be a compilation error)
        return;
    }

    // Create body result storage with actual type (deferred until after body codegen).
    // Using the actual body type preserves type fidelity through the alloca.
    auto* bodyResultStorage = createAllocaInEntryBlock(bodyVal->type(), "tryfinally.body.result");

    // Create remaining blocks AFTER body codegen (correct forward order)
    auto* finallyBlock = _builder.createBlock("tryfinally.block");
    auto* finallyErrorExit = _builder.createBlock("tryfinally.error_exit");
    auto* finallyNormal = _builder.createBlock("tryfinally.normal");

    // Normal path: store body result, set error flag to 0, branch to finally
    _builder.createStore(bodyResultStorage, bodyVal, "tryfinally.body.store");
    _builder.createStore(errorFlag, _builder.get(CoreVM::CoreNumber(0)), "tryfinally.flag.normal");
    _builder.createBr(finallyBlock);

    // Error path: ? operator jumped here. Error is already in funcCtx->returnStorage.
    // Set error flag to 1, branch to finally
    _builder.setInsertPoint(finallyFromError);
    _builder.createStore(errorFlag, _builder.get(CoreVM::CoreNumber(1)), "tryfinally.flag.error");
    _builder.createBr(finallyBlock);

    // Finally block: run cleanup, then check error flag
    _builder.setInsertPoint(finallyBlock);
    codegen(node.finallyExpr.get()); // result discarded

    auto* flag = _builder.createLoad(errorFlag, "tryfinally.flag.load");
    auto* isErr = _builder.createNCmpEQ(flag, _builder.get(CoreVM::CoreNumber(1)), "tryfinally.is_err");
    _builder.createCondBr(isErr, finallyErrorExit, finallyNormal);

    // Error exit: propagate to original return block (error already in returnStorage)
    _builder.setInsertPoint(finallyErrorExit);
    _builder.createBr(originalReturnBlock);

    // Normal exit: load body result
    _builder.setInsertPoint(finallyNormal);
    _result = _builder.createLoad(bodyResultStorage, "tryfinally.result.load");
}

void IRGenerator::visit(ast::UnitExpr const& /*node*/)
{
    // Unit produces integer 0 (void/unit semantics)
    _result = _builder.get(CoreVM::CoreNumber(0));
}

void IRGenerator::visit(ast::BlockExpr const& node)
{
    TRACE_SCOPE("visit(BlockExpr)");

    pushFSharpScope();

    // Codegen all statements (let bindings, etc.)
    for (auto const& stmt: node.statements)
        codegen(stmt.get());

    // Codegen the result expression (the block's value)
    _result = codegen(node.result.get());

    popFSharpScope();
}

} // namespace endo
