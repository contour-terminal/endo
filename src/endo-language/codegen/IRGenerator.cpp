// SPDX-License-Identifier: Apache-2.0
#include <endo-language/ScopedLogger.hpp>
#include <endo-language/ast/AST.hpp>
#include <endo-language/ast/ASTPrinter.hpp>
#include <endo-language/ast/Pattern.hpp>
#include <endo-language/codegen/IRGenerator.hpp>
#include <endo-language/codegen/PatternIRGenerator.hpp>
#include <endo-language/parser/DiagnosticsAdapter.hpp>
#include <endo-language/types/TypeInferencer.hpp>
#include <endo-language/types/Unification.hpp>

#include <CoreVM/CoreVM.hpp>

#include <bit>
#include <functional>
#include <map>
#include <typeinfo>

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
        // This occurs for function parameters whose concrete types aren't
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
    generator._persistentState = persistentState;

    generator._builder.setProgram(std::make_unique<CoreVM::IRProgram>());
    generator._builder.setFunction(generator._builder.getFunction(GLOBAL_SCOPE_INIT_NAME));
    generator._builder.setInsertPoint(generator._builder.createBlock("EntryPoint"));

    // Initialize F# root scope
    generator.pushFSharpScope();

    // Register builtin higher-order list functions (map, filter, fold, reduce, reverse).
    // These are registered as FSharpFunction entries with body=nullptr and builtinHOF set,
    // which leverages existing partial application and pipeline infrastructure.
    {
        auto registerHOF = [&](std::string name,
                               std::vector<std::string> params,
                               std::string hofName,
                               ResultKind resultKind) {
            FSharpFunction func;
            func.parameters = std::move(params);
            func.parameterTypes.resize(func.parameters.size()); // all nullopt (untyped)
            func.body = nullptr;
            func.builtinHOF = std::move(hofName);
            func.resultKind = resultKind;
            generator.registerFSharpFunction(std::move(name), std::move(func));
        };
        registerHOF("map", { "__f", "__xs" }, "map", ResultKind::Value);
        registerHOF("filter", { "__pred", "__xs" }, "filter", ResultKind::Value);
        registerHOF("fold", { "__init", "__f", "__xs" }, "fold", ResultKind::Value);
        registerHOF("reduce", { "__f", "__xs" }, "reduce", ResultKind::Value);
        registerHOF("reverse", { "__xs" }, "reverse", ResultKind::Value);
        registerHOF("find", { "__pred", "__xs" }, "find", ResultKind::Value);
        registerHOF("exists", { "__pred", "__xs" }, "exists", ResultKind::Value);
        registerHOF("forall", { "__pred", "__xs" }, "forall", ResultKind::Value);
        registerHOF("each", { "__f", "__xs" }, "each", ResultKind::Unit);
        registerHOF("take", { "__n", "__xs" }, "take", ResultKind::Value);
        registerHOF("drop", { "__n", "__xs" }, "drop", ResultKind::Value);
        registerHOF("zip", { "__xs", "__ys" }, "zip", ResultKind::Value);
        registerHOF("flatten", { "__xss" }, "flatten", ResultKind::Value);
        registerHOF("sortBy", { "__f", "__xs" }, "sortBy", ResultKind::Value);
        registerHOF("groupBy", { "__f", "__xs" }, "groupBy", ResultKind::Value);
        registerHOF("sort", { "__xs" }, "sort", ResultKind::Value);
        registerHOF("distinct", { "__xs" }, "distinct", ResultKind::Value);
    }

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
            func.hasVariadicParam = persisted.hasVariadicParam;
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
                generator.bindFSharpVariable(binding.name, storage, binding.isMutable, binding.isExported);

            // Re-export persisted exported bindings
            if (binding.isExported)
                generator.emitExportVariable(storage, binding.name);
        }

        // Re-compute captured bindings and compile persisted functions.
        // Value bindings are now in scope, so closures can resolve their captures.
        for (auto& [name, func]: generator._fsharpFunctions)
        {
            if (func.body)
            {
                auto boundNames = func.parameters;
                func.capturedBindings = generator.collectFreeVariables(func.body, boundNames);
            }
            generator.compileFunctionBody(name, func);
        }

        // Restore persisted properties
        for (auto const& [name, prop]: persistentState->properties)
        {
            FSharpProperty fsProp;
            fsProp.getter = prop.getter;
            fsProp.setter = prop.setter;
            generator._fsharpProperties[name] = fsProp;
        }
    }

    // Pre-register well-known structured command record types so that
    // field access (.pid, .command) and pattern matching work without user declarations.
    {
        RecordTypeInfo processInfoType;
        processInfoType.typeId = CoreVM::BuiltinTypeId::ProcessInfo;
        processInfoType.name = "ProcessInfo";
        processInfoType.fields = {
            { "pid", 0, CoreVM::LiteralType::Number },  { "ppid", 1, CoreVM::LiteralType::Number },
            { "user", 2, CoreVM::LiteralType::String }, { "cpu", 3, CoreVM::LiteralType::Number },
            { "mem", 4, CoreVM::LiteralType::Number },  { "command", 5, CoreVM::LiteralType::String },
        };
        for (auto const& f: processInfoType.fields)
            processInfoType.fieldTypes[f.name] = f.type;
        generator._recordTypes["ProcessInfo"] = std::move(processInfoType);
    }

    // Pre-register DateTime record type.
    {
        RecordTypeInfo dateTimeType;
        dateTimeType.typeId = CoreVM::BuiltinTypeId::DateTime;
        dateTimeType.name = "DateTime";
        dateTimeType.fields = {
            { "year", 0, CoreVM::LiteralType::Number },   { "month", 1, CoreVM::LiteralType::Number },
            { "day", 2, CoreVM::LiteralType::Number },    { "hour", 3, CoreVM::LiteralType::Number },
            { "minute", 4, CoreVM::LiteralType::Number }, { "second", 5, CoreVM::LiteralType::Number },
            { "epoch", 6, CoreVM::LiteralType::Number },
        };
        for (auto const& f: dateTimeType.fields)
            dateTimeType.fieldTypes[f.name] = f.type;
        generator._recordTypes["DateTime"] = std::move(dateTimeType);
    }

    // Pre-register Size record type.
    {
        RecordTypeInfo sizeType;
        sizeType.typeId = CoreVM::BuiltinTypeId::Size;
        sizeType.name = "Size";
        sizeType.fields = {
            { "bytes", 0, CoreVM::LiteralType::Number },
        };
        for (auto const& f: sizeType.fields)
            sizeType.fieldTypes[f.name] = f.type;
        generator._recordTypes["Size"] = std::move(sizeType);
    }

    // Pre-register FileInfo record type for the ls builtin.
    {
        RecordTypeInfo fileInfoType;
        fileInfoType.typeId = CoreVM::BuiltinTypeId::FileInfo;
        fileInfoType.name = "FileInfo";
        fileInfoType.fields = {
            { "name", 0, CoreVM::LiteralType::String },   { "size", 1, CoreVM::LiteralType::Object },
            { "mode", 2, CoreVM::LiteralType::Number },   { "mtime", 3, CoreVM::LiteralType::Object },
            { "isDir", 4, CoreVM::LiteralType::Boolean },
        };
        for (auto const& f: fileInfoType.fields)
            fileInfoType.fieldTypes[f.name] = f.type;
        fileInfoType.fieldObjectTypeIds["mtime"] = CoreVM::BuiltinTypeId::DateTime;
        fileInfoType.fieldObjectTypeIds["size"] = CoreVM::BuiltinTypeId::Size;
        generator._recordTypes["FileInfo"] = std::move(fileInfoType);
    }

    // Pre-register JobInfo record type for the jobs builtin.
    {
        RecordTypeInfo jobInfoType;
        jobInfoType.typeId = CoreVM::BuiltinTypeId::JobInfo;
        jobInfoType.name = "JobInfo";
        jobInfoType.fields = {
            { "id", 0, CoreVM::LiteralType::Number },
            { "state", 1, CoreVM::LiteralType::String },
            { "command", 2, CoreVM::LiteralType::String },
            { "pid", 3, CoreVM::LiteralType::Number },
        };
        for (auto const& f: jobInfoType.fields)
            jobInfoType.fieldTypes[f.name] = f.type;
        generator._recordTypes["JobInfo"] = std::move(jobInfoType);
    }

    // Pre-register output definition record types from persistent state.
    if (persistentState)
    {
        for (auto const& [typeName, defType]: persistentState->outputDefinitionTypes)
        {
            RecordTypeInfo info;
            info.typeId = defType.typeId;
            info.name = typeName;
            info.fields = defType.fields;
            for (auto const& f: info.fields)
                info.fieldTypes[f.name] = f.type;
            generator._recordTypes[typeName] = std::move(info);

            CoreVM::IRProgram::CustomProductType customType;
            customType.name = typeName;
            customType.fields = defType.fields;
            customType.assignedId = defType.typeId;
            generator._builder.program()->addCustomProductType(std::move(customType));
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

    // Persist newly defined functions and value bindings back to persistent state.
    // Only persist when there are no errors — on error the AST won't be retained
    // (see Shell::execute()), so persisted raw pointers (body, value) would dangle.
    if (persistentState && !generator._hasErrors)
    {
        for (auto const& [name, func]: generator._fsharpFunctions)
        {
            // Skip auto-generated lambda names (partial application intermediates)
            if (name.starts_with("__lambda_"))
                continue;

            // Skip builtin HOFs — they are re-registered on each codegen
            if (!func.builtinHOF.empty())
                continue;

            FSharpPersistentState::PersistedFunction persisted;
            persisted.parameters = func.parameters;
            persisted.parameterTypes = func.parameterTypes;
            persisted.returnType = func.returnType;
            persisted.body = func.body;
            persisted.returnKind = func.returnKind;
            persisted.isRecursive = func.isRecursive;
            persisted.hasVariadicParam = func.hasVariadicParam;
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

void IRGenerator::bindFSharpVariable(std::string const& name,
                                     CoreVM::Value* value,
                                     bool isMutable,
                                     bool isExported)
{
    if (_currentFSharpScope)
        _currentFSharpScope->bindings[name] = BindingInfo { value, isMutable, isExported };
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

void IRGenerator::emitExportVariable(CoreVM::Value* storage, std::string const& name)
{
    auto* loadedVal = _builder.createLoad(storage, name + ".export.load");
    auto* strVal = convertToString(loadedVal, name + ".export");
    if (auto* exportCb = findCallback("export(SS)V"))
        _builder.createCallFunction(
            _builder.getBuiltinFunction(*exportCb), { _builder.get(name), strVal }, "export");
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

std::optional<std::string> IRGenerator::lookupFSharpFunctionRef(std::string const& name) const
{
    for (FSharpScope const* scope = _currentFSharpScope; scope != nullptr; scope = scope->parent)
    {
        if (auto it = scope->functionRefs.find(name); it != scope->functionRefs.end())
            return it->second;
    }
    return std::nullopt;
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
    if (type->isRecord() || type->isUnion())
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

    // Unit (Void) expected accepts any value — unit means "no meaningful value"
    if (*expected == CoreVM::LiteralType::Void)
        return true;

    // Accept Number as compatible with Object expected type — native callbacks (e.g., list_concat)
    // return object pointers stored as Numbers at the IR level.
    if (*expected == CoreVM::LiteralType::Object && actual == CoreVM::LiteralType::Number)
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
    func.hasVariadicParam = false;
    func.parameters.reserve(typedParams.size());
    func.parameterTypes.reserve(typedParams.size());
    for (auto const& tp: typedParams)
    {
        func.parameters.push_back(tp.name);
        func.parameterTypes.push_back(tp.typeAnnotation);
        if (tp.isVariadic)
            func.hasVariadicParam = true;
    }
}

void IRGenerator::applyInferredTypes(std::string const& name, FSharpFunction& func)
{
    auto it = _inferenceResult.functions.find(name);
    if (it == _inferenceResult.functions.end())
        return;

    auto const& inferred = it->second;

    // Fill in missing parameter type annotations from inference results.
    // Apply any concrete, fully-resolved, non-function type (primitives, list, option, result, tuple,
    // record, union). This enables UCALL compilation for functions with complex-typed parameters,
    // supporting non-tail recursion via the call stack. Unresolved type variables and function types
    // are excluded — polymorphic and higher-order functions continue to use AST inlining.
    for (size_t i = 0; i < func.parameterTypes.size() && i < inferred.paramTypes.size(); ++i)
    {
        if (!func.parameterTypes[i].has_value() && !inferred.paramTypes[i]->isTypeVar()
            && !inferred.paramTypes[i]->isFunction() && collectTypeVars(inferred.paramTypes[i]).empty()
            && mapTypeToLiteralType(inferred.paramTypes[i]).has_value())
        {
            func.parameterTypes[i] = inferred.paramTypes[i];
        }
    }

    // NOTE: We intentionally do NOT apply inferred return types here.
    // The return type is determined from the actual body result during function compilation
    // (compiledReturnType), and applying it here causes false validation errors in the
    // AST inlining path (e.g., print returns Number(0) but unit maps to Void).
}

bool IRGenerator::isUnitProducingExpr(ast::Expr const* expr) const
{
    std::unordered_set<std::string> visited;
    return isUnitProducingExprImpl(expr, visited);
}

bool IRGenerator::isUnitProducingExprImpl(ast::Expr const* expr,
                                          std::unordered_set<std::string>& visited) const
{
    if (!expr)
        return false;

    // Unwrap parentheses
    if (auto const* paren = dynamic_cast<ast::ParenExpr const*>(expr))
        return isUnitProducingExprImpl(paren->inner.get(), visited);

    // Unit literal ()
    if (dynamic_cast<ast::UnitExpr const*>(expr))
        return true;

    // Function application: check if the called function returns void
    if (auto const* app = dynamic_cast<ast::ApplicationExpr const*>(expr))
    {
        // Walk to the leftmost function in curried application chain
        auto const* fn = app->function.get();
        while (auto const* innerApp = dynamic_cast<ast::ApplicationExpr const*>(fn))
            fn = innerApp->function.get();

        if (auto const* ident = dynamic_cast<ast::IdentifierExpr const*>(fn))
        {
            auto const& name = ident->name;

            // Cycle detection for recursive functions
            if (visited.contains(name))
                return false;

            // Check builtins: any overload with this name that returns Void
            for (auto const* cb: _runtime.builtins())
                if (cb->signature().name() == name
                    && cb->signature().returnType() == CoreVM::LiteralType::Void)
                    return true;

            // Check user-defined F# functions
            if (auto const* func = lookupFSharpFunction(name))
            {
                // Builtin HOFs with no AST body: check the resultKind flag
                if (!func->builtinHOF.empty())
                    return func->resultKind == ResultKind::Unit;

                visited.insert(name);
                auto result = isUnitProducingExprImpl(func->body, visited);
                visited.erase(name);
                return result;
            }
        }
    }

    // Pipeline: result type is determined by the rightmost function
    if (auto const* pipeline = dynamic_cast<ast::PipelineExpr const*>(expr))
        return isUnitProducingExprImpl(pipeline->function.get(), visited);

    // Match where ALL arms produce unit
    if (auto const* match = dynamic_cast<ast::MatchExpr const*>(expr))
        return !match->arms.empty() && std::ranges::all_of(match->arms, [this, &visited](auto const& arm) {
            return isUnitProducingExprImpl(arm.body.get(), visited);
        });

    // If-then-else where both branches produce unit
    if (auto const* ifE = dynamic_cast<ast::IfExpr const*>(expr))
    {
        if (!isUnitProducingExprImpl(ifE->thenExpr.get(), visited))
            return false;
        return ifE->elseExpr ? isUnitProducingExprImpl(ifE->elseExpr.get(), visited) : true;
    }

    return false;
}

ReturnKind IRGenerator::determineReturnKind(ast::Expr const* body) const
{
    if (!body)
        return ReturnKind::Plain;

    // Block expression: return kind is determined by the result expression
    if (auto const* block = dynamic_cast<ast::BlockExpr const*>(body))
        return determineReturnKind(block->result.get());

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
        return ifExpr->elseExpr ? determineReturnKind(ifExpr->elseExpr.get()) : ReturnKind::Plain;
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
        return containsTryExpr(ifExpr->thenExpr.get())
               || (ifExpr->elseExpr && containsTryExpr(ifExpr->elseExpr.get()));

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

    if (auto* optDefault = dynamic_cast<ast::OptionDefaultExpr const*>(body))
        return containsTryExpr(optDefault->option.get()) || containsTryExpr(optDefault->defaultValue.get());

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
        return needsAutoWrap(ifExpr->thenExpr.get())
               || (ifExpr->elseExpr && needsAutoWrap(ifExpr->elseExpr.get()));

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

    if (kind == ReturnKind::Result)
        return emitOkResult(value, value->type(), "autowrap.result");
    else
        return emitSomeOption(value, value->type(), "autowrap.option");
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

void IRGenerator::annotateListElementTypeId(CoreVM::Value* val, uint16_t typeId)
{
    _listElementTypeAnnotations[val] = typeId;
}

std::optional<uint16_t> IRGenerator::getListElementTypeId(CoreVM::Value* val) const
{
    auto it = _listElementTypeAnnotations.find(val);
    if (it != _listElementTypeAnnotations.end())
        return it->second;
    return std::nullopt;
}

void IRGenerator::annotateListElementLiteralType(CoreVM::Value* val, CoreVM::LiteralType type)
{
    _listElementLiteralTypes[val] = type;
}

std::optional<CoreVM::LiteralType> IRGenerator::getListElementLiteralType(CoreVM::Value* val) const
{
    auto it = _listElementLiteralTypes.find(val);
    if (it != _listElementLiteralTypes.end())
        return it->second;
    return std::nullopt;
}

std::optional<CoreVM::LiteralType> IRGenerator::determineCommonLiteralType(
    std::span<CoreVM::Value* const> values)
{
    if (values.empty())
        return std::nullopt;
    auto const commonType = values.front()->type();
    if (commonType == CoreVM::LiteralType::Void)
        return std::nullopt;
    for (size_t i = 1; i < values.size(); ++i)
        if (values[i]->type() != commonType)
            return std::nullopt;
    return commonType;
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

            if (auto const* optDefault = dynamic_cast<ast::OptionDefaultExpr const*>(expr))
            {
                walk(optDefault->option.get(), bound);
                walk(optDefault->defaultValue.get(), bound);
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
                if (ifExpr->elseExpr)
                    walk(ifExpr->elseExpr.get(), bound);
                return;
            }

            if (auto const* tupleExpr = dynamic_cast<ast::TupleExpr const*>(expr))
            {
                for (auto const& elem: tupleExpr->elements)
                    walk(elem.get(), bound);
                return;
            }

            if (auto const* mutExpr = dynamic_cast<ast::MutAssignExpr const*>(expr))
            {
                // The assigned variable itself is a free variable reference
                if (std::ranges::find(bound, mutExpr->name) == bound.end())
                    if (auto* storage = lookupFSharpVariable(mutExpr->name))
                        freeVars[mutExpr->name] = storage;
                walk(mutExpr->value.get(), bound);
                return;
            }

            if (auto const* block = dynamic_cast<ast::BlockExpr const*>(expr))
            {
                auto innerBound = bound;
                for (auto const& stmt: block->statements)
                {
                    if (auto const* letStmt = dynamic_cast<ast::LetBindingStmt const*>(stmt.get()))
                    {
                        walk(letStmt->value.get(), innerBound);
                        innerBound.push_back(letStmt->name);
                    }
                    else if (auto const* exprStmt = dynamic_cast<ast::ExprStmt const*>(stmt.get()))
                    {
                        walk(exprStmt->expr.get(), innerBound);
                    }
                }
                walk(block->result.get(), innerBound);
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
    // Get the entry block of the current function
    CoreVM::BasicBlock* entryBlock = _builder.function()->getEntryBlock();

    // Create the alloca instruction
    auto allocaInstr = std::make_unique<CoreVM::AllocaInstr>(
        type, _builder.get(CoreVM::CoreNumber(1)), _builder.makeName(name));

    // Insert after existing allocas to maintain the alloca-prefix invariant.
    // Using insertBeforeTerminator would interleave allocas with non-alloca instructions,
    // breaking TargetCodeGenerator's assumption that allocas form a contiguous stack prefix.
    CoreVM::Instr* inserted = entryBlock->insertAfterAllocas(std::move(allocaInstr));

    return static_cast<CoreVM::AllocaInstr*>(inserted);
}

// ---------------------------------------------------------------------------
// Container emit helpers (type tag slots)
// ---------------------------------------------------------------------------

CoreVM::Value* IRGenerator::emitNilList(CoreVM::LiteralType elemType, std::string_view label)
{
    auto* typeId = _builder.get(CoreVM::CoreNumber(CoreVM::BuiltinTypeId::List));
    auto* tag0 = _builder.get(CoreVM::CoreNumber(0));
    auto* slot2 = _builder.get(CoreVM::CoreNumber(2));

    CoreVM::Value* obj = _builder.createObjAlloc(typeId, std::string(label));
    obj = _builder.createObjSetTag(obj, tag0, std::string(label) + ".tag");
    obj = _builder.createObjSetSlot(obj,
                                    slot2,
                                    _builder.get(CoreVM::CoreNumber(static_cast<int>(elemType))),
                                    std::string(label) + ".etype");
    return obj;
}

CoreVM::Value* IRGenerator::emitListCons(CoreVM::Value* head,
                                         CoreVM::Value* tail,
                                         CoreVM::LiteralType elemType,
                                         std::string_view label)
{
    auto* typeId = _builder.get(CoreVM::CoreNumber(CoreVM::BuiltinTypeId::List));
    auto* tag1 = _builder.get(CoreVM::CoreNumber(1));
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0));
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1));
    auto* slot2 = _builder.get(CoreVM::CoreNumber(2));

    CoreVM::Value* obj = _builder.createObjAlloc(typeId, std::string(label));
    obj = _builder.createObjSetTag(obj, tag1, std::string(label) + ".tag");
    obj = _builder.createObjSetSlot(obj, slot0, head, std::string(label) + ".head");
    obj = _builder.createObjSetSlot(obj, slot1, tail, std::string(label) + ".tail");
    obj = _builder.createObjSetSlot(obj,
                                    slot2,
                                    _builder.get(CoreVM::CoreNumber(static_cast<int>(elemType))),
                                    std::string(label) + ".etype");
    return obj;
}

CoreVM::Value* IRGenerator::emitSomeOption(CoreVM::Value* value,
                                           CoreVM::LiteralType innerType,
                                           std::string_view label)
{
    auto* typeId = _builder.get(CoreVM::CoreNumber(CoreVM::BuiltinTypeId::Option));
    auto* tag1 = _builder.get(CoreVM::CoreNumber(1));
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0));
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1));

    CoreVM::Value* obj = _builder.createObjAlloc(typeId, std::string(label));
    obj = _builder.createObjSetTag(obj, tag1, std::string(label) + ".tag");
    obj = _builder.createObjSetSlot(obj, slot0, value, std::string(label) + ".value");
    obj = _builder.createObjSetSlot(obj,
                                    slot1,
                                    _builder.get(CoreVM::CoreNumber(static_cast<int>(innerType))),
                                    std::string(label) + ".itype");
    return obj;
}

CoreVM::Value* IRGenerator::emitNoneOption(std::string_view label)
{
    auto* typeId = _builder.get(CoreVM::CoreNumber(CoreVM::BuiltinTypeId::Option));
    auto* tag0 = _builder.get(CoreVM::CoreNumber(0));

    CoreVM::Value* obj = _builder.createObjAlloc(typeId, std::string(label));
    obj = _builder.createObjSetTag(obj, tag0, std::string(label) + ".tag");
    // slot 1 (type tag) defaults to 0 = Void = unknown (from zero-initialization)
    return obj;
}

CoreVM::Value* IRGenerator::emitOkResult(CoreVM::Value* value,
                                         CoreVM::LiteralType innerType,
                                         std::string_view label)
{
    auto* typeId = _builder.get(CoreVM::CoreNumber(CoreVM::BuiltinTypeId::Result));
    auto* tag1 = _builder.get(CoreVM::CoreNumber(1));
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0));
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1));

    CoreVM::Value* obj = _builder.createObjAlloc(typeId, std::string(label));
    obj = _builder.createObjSetTag(obj, tag1, std::string(label) + ".tag");
    obj = _builder.createObjSetSlot(obj, slot0, value, std::string(label) + ".value");
    obj = _builder.createObjSetSlot(obj,
                                    slot1,
                                    _builder.get(CoreVM::CoreNumber(static_cast<int>(innerType))),
                                    std::string(label) + ".itype");
    return obj;
}

CoreVM::Value* IRGenerator::emitErrorResult(CoreVM::Value* value,
                                            CoreVM::LiteralType innerType,
                                            std::string_view label)
{
    auto* typeId = _builder.get(CoreVM::CoreNumber(CoreVM::BuiltinTypeId::Result));
    auto* tag0 = _builder.get(CoreVM::CoreNumber(0));
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0));
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1));

    CoreVM::Value* obj = _builder.createObjAlloc(typeId, std::string(label));
    obj = _builder.createObjSetTag(obj, tag0, std::string(label) + ".tag");
    obj = _builder.createObjSetSlot(obj, slot0, value, std::string(label) + ".value");
    obj = _builder.createObjSetSlot(obj,
                                    slot1,
                                    _builder.get(CoreVM::CoreNumber(static_cast<int>(innerType))),
                                    std::string(label) + ".itype");
    return obj;
}

CoreVM::Value* IRGenerator::emitTuple2(CoreVM::Value* fst,
                                       CoreVM::Value* snd,
                                       CoreVM::LiteralType fstType,
                                       CoreVM::LiteralType sndType,
                                       std::string_view label)
{
    auto* typeId = _builder.get(CoreVM::CoreNumber(CoreVM::BuiltinTypeId::Tuple2));
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0));
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1));
    auto* slot2 = _builder.get(CoreVM::CoreNumber(2));

    CoreVM::Value* obj = _builder.createObjAlloc(typeId, std::string(label));
    obj = _builder.createObjSetSlot(obj, slot0, fst, std::string(label) + ".fst");
    obj = _builder.createObjSetSlot(obj, slot1, snd, std::string(label) + ".snd");
    obj = _builder.createObjSetSlot(
        obj,
        slot2,
        _builder.get(CoreVM::CoreNumber(static_cast<int64_t>(CoreVM::packTypeTag(fstType, sndType)))),
        std::string(label) + ".ttag");
    return obj;
}

CoreVM::Value* IRGenerator::emitTuple3(CoreVM::Value* e0,
                                       CoreVM::Value* e1,
                                       CoreVM::Value* e2,
                                       CoreVM::LiteralType t0,
                                       CoreVM::LiteralType t1,
                                       CoreVM::LiteralType t2,
                                       std::string_view label)
{
    auto* typeId = _builder.get(CoreVM::CoreNumber(CoreVM::BuiltinTypeId::Tuple3));
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0));
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1));
    auto* slot2 = _builder.get(CoreVM::CoreNumber(2));
    auto* slot3 = _builder.get(CoreVM::CoreNumber(3));

    CoreVM::Value* obj = _builder.createObjAlloc(typeId, std::string(label));
    obj = _builder.createObjSetSlot(obj, slot0, e0, std::string(label) + ".e0");
    obj = _builder.createObjSetSlot(obj, slot1, e1, std::string(label) + ".e1");
    obj = _builder.createObjSetSlot(obj, slot2, e2, std::string(label) + ".e2");
    obj = _builder.createObjSetSlot(
        obj,
        slot3,
        _builder.get(CoreVM::CoreNumber(static_cast<int64_t>(CoreVM::packTypeTag(t0, t1, t2)))),
        std::string(label) + ".ttag");
    return obj;
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

template <typename... Args>
void IRGenerator::reportTypeErrorWithSuggestions(std::vector<std::string> suggestions,
                                                 std::format_string<Args...> f,
                                                 Args&&... args)
{
    auto const msg = std::format(f, std::forward<Args>(args)...);
    _report.typeErrorWithSuggestions(
        _builder.sourceLocation(), std::move(suggestions), std::nullopt, "{}", std::string_view(msg));
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
            reportTypeError("exit code must be a number, got {}", CoreVM::tos(exitCode->type()));
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

void IRGenerator::visit(ast::BuiltinReadStmt const& node)
{
    auto callArguments = std::vector<CoreVM::Value*> {};
    if (!node.parameters.empty())
        callArguments.emplace_back(_builder.get(createCallArgs(node.parameters)));

    _result =
        _builder.createCallFunction(_builder.getBuiltinFunction(node.callback.get()), callArguments, "read");
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

        if (call->programExpr)
        {
            // Runtime-evaluated program name (tilde expansion)
            auto* expandedProgram = codegen(call->programExpr.get());
            buildCommandArgs(expandedProgram, call->parameters);

            if (lastInChain && node.background)
                _result = execBuiltCommandPipedBackground(call->program, call->parameters);
            else
                _result = execBuiltCommandPiped(lastInChain);
        }
        else if (containsRuntimeExpr(call->parameters))
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
            reportTypeError("job ID must be a number, got {}", CoreVM::tos(jobIdValue->type()));
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
            reportTypeError("job ID must be a number, got {}", CoreVM::tos(jobIdValue->type()));
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
            reportTypeError("job ID must be a number, got {}", CoreVM::tos(jobIdValue->type()));
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

    if (node.programExpr)
    {
        // Runtime-evaluated program name (tilde expansion)
        auto* expandedProgram = codegen(node.programExpr.get());
        buildCommandArgs(expandedProgram, node.parameters);
        _result = execBuiltCommand();
    }
    else if (containsRuntimeExpr(node.parameters))
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

namespace
{
    /// Builds a structured command lookup key from command name and arguments.
    /// Format: "docker\0ps" (command + NUL + args joined by NUL).
    std::string makeStructuredCommandKey(std::string const& command, std::vector<std::string> const& args)
    {
        std::string key = command;
        for (auto const& arg: args)
        {
            key += '\0';
            key += arg;
        }
        return key;
    }
} // namespace

void IRGenerator::visit(ast::StructuredPipelineSourceExpr const& node)
{
    // Handle builtin structured commands (find, etc.) before output definitions
    if (node.command)
    {
        if (auto const* call = dynamic_cast<ast::ProgramCall const*>(node.command.get()))
        {
            if (call->program == "find")
            {
                // Serialize find args as null-separated string
                std::string serializedArgs;
                for (auto const& param: call->parameters)
                {
                    if (auto const* lit = dynamic_cast<ast::LiteralExpr const*>(param.get()))
                    {
                        if (!serializedArgs.empty())
                            serializedArgs += '\0';
                        serializedArgs += lit->value;
                    }
                }
                auto* callback = findCallback("structured_find(S)I");
                if (callback)
                {
                    auto* argsVal = _builder.get(serializedArgs);
                    _result = _builder.createCallFunction(
                        _builder.getBuiltinFunction(*callback), { argsVal }, "find");
                    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
                    annotateListElementTypeId(_result, CoreVM::BuiltinTypeId::FileInfo);
                    annotateListElementLiteralType(_result, CoreVM::LiteralType::Object);
                    return;
                }
            }
        }
    }

    // Try to match against output definitions from persistent state
    if (_persistentState && node.command)
    {
        if (auto const* call = dynamic_cast<ast::ProgramCall const*>(node.command.get()))
        {
            // Extract literal string args
            std::vector<std::string> args;
            for (auto const& param: call->parameters)
                if (auto const* lit = dynamic_cast<ast::LiteralExpr const*>(param.get()))
                    args.push_back(lit->value);

            auto const key = makeStructuredCommandKey(call->program, args);
            if (auto it = _persistentState->structuredCommands.find(key);
                it != _persistentState->structuredCommands.end())
            {
                auto const& info = it->second;
                auto const sig = info.builtinCallbackName + "()I";
                if (auto* cb = findCallback(sig))
                {
                    _result = _builder.createCallFunction(_builder.getBuiltinFunction(*cb), {}, key);
                    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
                    annotateListElementTypeId(_result, info.recordTypeId);
                    annotateListElementLiteralType(_result, CoreVM::LiteralType::Object);
                    return;
                }
            }
        }
        else if (auto const* pipeline = dynamic_cast<ast::CallPipeline const*>(node.command.get()))
        {
            // Handle CallPipeline: extract the first call's program and args
            if (!pipeline->calls.empty())
            {
                auto const& firstCall = *pipeline->calls[0];
                std::vector<std::string> args;
                for (auto const& param: firstCall.parameters)
                    if (auto const* lit = dynamic_cast<ast::LiteralExpr const*>(param.get()))
                        args.push_back(lit->value);

                auto const key = makeStructuredCommandKey(firstCall.program, args);
                if (auto it = _persistentState->structuredCommands.find(key);
                    it != _persistentState->structuredCommands.end())
                {
                    auto const& info = it->second;
                    auto const sig = info.builtinCallbackName + "()I";
                    if (auto* cb = findCallback(sig))
                    {
                        _result = _builder.createCallFunction(_builder.getBuiltinFunction(*cb), {}, key);
                        annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
                        annotateListElementTypeId(_result, info.recordTypeId);
                        annotateListElementLiteralType(_result, CoreVM::LiteralType::Object);
                        return;
                    }
                }
            }
        }
    }

    // Fallback: command substitution (capture stdout as string)
    auto* startCb = findCallback("internal.subst_start()V");
    if (startCb)
    {
        _builder.createCallFunction(_builder.getBuiltinFunction(*startCb), {}, "subst_start");
        codegen(node.command.get());
        auto* endCb = findCallback("internal.subst_end()S");
        if (endCb)
        {
            _result = _builder.createCallFunction(_builder.getBuiltinFunction(*endCb), {}, "subst_end");
            return;
        }
    }

    // If no subst builtins available, execute the command and result is void
    codegen(node.command.get());
}

void IRGenerator::visit(ast::DataSourceExpr const& node)
{
    TRACE_SCOPE("visit(DataSourceExpr)");

    // 1. Resolve or register the record type
    uint16_t typeId = 0;
    std::string schemaDesc;

    if (!node.typeName.empty())
    {
        // Named type reference — look up in _recordTypes
        auto const* recInfo = lookupRecordType(node.typeName);
        if (!recInfo)
        {
            reportTypeError("Unknown type '{}' in data source 'as' annotation", node.typeName);
            return;
        }
        typeId = recInfo->typeId;

        // Build schema descriptor from registered fields
        for (size_t i = 0; i < recInfo->fields.size(); ++i)
        {
            if (i > 0)
                schemaDesc += ',';
            auto const& f = recInfo->fields[i];
            schemaDesc += f.name;
            schemaDesc += ':';
            switch (f.type)
            {
                case CoreVM::LiteralType::String: schemaDesc += "string"; break;
                case CoreVM::LiteralType::Number: schemaDesc += "int"; break;
                case CoreVM::LiteralType::Float: schemaDesc += "float"; break;
                case CoreVM::LiteralType::Boolean: schemaDesc += "bool"; break;
                default: schemaDesc += "string"; break;
            }
        }
    }
    else
    {
        // Inline field definition — register a new type
        typeId = _builder.program()->allocateCustomTypeId();

        std::vector<CoreVM::FieldInfo> fields;
        std::unordered_map<std::string, CoreVM::LiteralType> fieldTypes;

        for (uint8_t i = 0; i < node.inlineFields.size(); ++i)
        {
            auto const& field = node.inlineFields[i];
            auto vmType = CoreVM::LiteralType::String; // default
            if (auto const* prim = std::get_if<PrimitiveTypeNode>(&field.type->node))
            {
                switch (prim->kind)
                {
                    case PrimitiveType::Int: vmType = CoreVM::LiteralType::Number; break;
                    case PrimitiveType::Float: vmType = CoreVM::LiteralType::Float; break;
                    case PrimitiveType::Str: vmType = CoreVM::LiteralType::String; break;
                    case PrimitiveType::Bool: vmType = CoreVM::LiteralType::Boolean; break;
                    case PrimitiveType::Unit: vmType = CoreVM::LiteralType::Void; break;
                }
            }
            fields.push_back({ field.name, i, vmType });
            fieldTypes[field.name] = vmType;

            // Build schema descriptor
            if (i > 0)
                schemaDesc += ',';
            schemaDesc += field.name;
            schemaDesc += ':';
            switch (vmType)
            {
                case CoreVM::LiteralType::String: schemaDesc += "string"; break;
                case CoreVM::LiteralType::Number: schemaDesc += "int"; break;
                case CoreVM::LiteralType::Float: schemaDesc += "float"; break;
                case CoreVM::LiteralType::Boolean: schemaDesc += "bool"; break;
                default: schemaDesc += "string"; break;
            }
        }

        // Generate an anonymous type name
        auto const anonymousTypeName = std::format("__datasource_{}", typeId);

        // Store in the IR generator's record type table
        RecordTypeInfo info;
        info.typeId = typeId;
        info.name = anonymousTypeName;
        info.fields = fields;
        info.fieldTypes = std::move(fieldTypes);
        _recordTypes[anonymousTypeName] = std::move(info);

        // Register as a custom product type
        CoreVM::IRProgram::CustomProductType customType;
        customType.name = anonymousTypeName;
        customType.fields = fields;
        customType.assignedId = typeId;
        _builder.program()->addCustomProductType(std::move(customType));
    }

    // 2. Determine callback name based on kind
    std::string callbackName;
    switch (node.kind)
    {
        case ast::DataSourceExpr::Kind::OpenJson: callbackName = "open_json"; break;
        case ast::DataSourceExpr::Kind::OpenCsv: callbackName = "open_csv"; break;
        case ast::DataSourceExpr::Kind::FromJson: callbackName = "from_json"; break;
        case ast::DataSourceExpr::Kind::FromCsv: callbackName = "from_csv"; break;
    }

    // 3. Build callback arguments
    auto const sig = callbackName + "(SSI)I";
    auto* cb = findCallback(sig);
    if (!cb)
    {
        reportTypeError("Data source builtin '{}' not found", callbackName);
        return;
    }

    // First argument: file path (for open-*) or reconstructed source command (for from-*)
    CoreVM::Value* firstArg = nullptr;
    if (node.kind == ast::DataSourceExpr::Kind::OpenJson || node.kind == ast::DataSourceExpr::Kind::OpenCsv)
    {
        if (node.filePath)
        {
            codegen(node.filePath.get());
            firstArg = _result;
        }
        else
        {
            firstArg = _builder.get("");
        }
    }
    else
    {
        // For from-*: reconstruct the source command string from the pipe source AST
        std::string sourceCmdStr;
        if (node.pipeSource)
        {
            if (auto const* pipeline = dynamic_cast<ast::CallPipeline const*>(node.pipeSource.get()))
            {
                for (size_t i = 0; i < pipeline->calls.size(); ++i)
                {
                    if (i > 0)
                        sourceCmdStr += " | ";
                    sourceCmdStr += pipeline->calls[i]->program;
                    for (auto const& param: pipeline->calls[i]->parameters)
                        if (auto const* lit = dynamic_cast<ast::LiteralExpr const*>(param.get()))
                        {
                            sourceCmdStr += ' ';
                            sourceCmdStr += lit->value;
                        }
                }
            }
            else if (auto const* call = dynamic_cast<ast::ProgramCall const*>(node.pipeSource.get()))
            {
                sourceCmdStr = call->program;
                for (auto const& param: call->parameters)
                    if (auto const* lit = dynamic_cast<ast::LiteralExpr const*>(param.get()))
                    {
                        sourceCmdStr += ' ';
                        sourceCmdStr += lit->value;
                    }
            }
        }
        firstArg = _builder.get(sourceCmdStr);
    }

    auto* schemaArg = _builder.get(schemaDesc);
    auto* typeIdArg = _builder.get(CoreVM::CoreNumber(typeId));

    // 4. Emit callback call
    _result = _builder.createCallFunction(
        _builder.getBuiltinFunction(*cb), { firstArg, schemaArg, typeIdArg }, callbackName);
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
    annotateListElementTypeId(_result, typeId);
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

void IRGenerator::visit(ast::ForInStmt const& node)
{
    TRACE_SCOPE("visit(ForInStmt)");

    // for pattern in source_list do body done
    //
    // IR pattern:
    //   forin.init: srcStorage = codegen(source)
    //   forin.cond: if OGETTAG(srcStorage) == 1 (Cons) goto forin.body else goto forin.end
    //   forin.body: head = OGETSLOT(srcStorage, 0)
    //              tail = OGETSLOT(srcStorage, 1)
    //              srcStorage = tail
    //              destructure head via PatternIRGenerator
    //              execute body
    //              goto forin.cond
    //   forin.end:  (continue after loop)

    auto* tag1 = _builder.get(CoreVM::CoreNumber(1));  // Cons
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0)); // head
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1)); // tail

    // Evaluate source expression
    auto* sourceVal = codegen(node.source.get());
    if (!sourceVal)
    {
        reportTypeError("Failed to evaluate for-in source expression");
        return;
    }

    // Store source in alloca for iteration
    auto* srcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "forin.src");
    _builder.createStore(srcStorage, sourceVal);

    // Pre-allocate binding allocas for all pattern variables
    auto bindingNames = pattern::collectBindings(*node.pattern);
    std::unordered_map<std::string, CoreVM::AllocaInstr*> bindingStorage;
    for (auto const& name: bindingNames)
    {
        auto* alloca = createAllocaInEntryBlock(CoreVM::LiteralType::Void, name);
        bindingStorage[name] = alloca;
    }

    // Alloca for head element storage (used by PatternIRGenerator)
    auto* headStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "forin.head");

    // Create blocks
    auto* condBlock = _builder.createBlock("forin.cond");
    auto* bodyBlock = _builder.createBlock("forin.body");
    auto* destructureOk = _builder.createBlock("forin.destructure.ok");
    auto* destructureFail = _builder.createBlock("forin.destructure.fail");
    auto* endBlock = _builder.createBlock("forin.end");

    _builder.createBr(condBlock);

    // Condition: check if source list is Cons (tag == 1)
    _builder.setInsertPoint(condBlock);
    auto* srcLoad = _builder.createLoad(srcStorage, "forin.src.load");
    auto* srcTag = _builder.createObjGetTag(srcLoad, "forin.src.tag");
    auto* isCons = _builder.createNCmpEQ(srcTag, tag1, "forin.is_cons");
    _builder.createCondBr(isCons, bodyBlock, endBlock);

    // Body: extract head and tail, destructure head, execute body
    _builder.setInsertPoint(bodyBlock);

    // Extract head (separate load to avoid multi-use)
    auto* srcForHead = _builder.createLoad(srcStorage, "forin.src.for_head");
    auto* head = _builder.createObjGetSlot(srcForHead, slot0, "forin.head");
    _builder.createStore(headStorage, head);

    // Advance cursor: extract tail and store back (separate load)
    auto* srcForTail = _builder.createLoad(srcStorage, "forin.src.for_tail");
    auto* tail = _builder.createObjGetSlot(srcForTail, slot1, "forin.tail");
    _builder.createStore(srcStorage, tail);

    // Reload head from storage for PatternIRGenerator (avoid multi-use of raw ObjGetSlot result)
    auto* headReloaded = _builder.createLoad(headStorage, "forin.head.reload");

    // Destructure head using PatternIRGenerator
    PatternIRGenerator patternGen(_builder);
    patternGen.setBindingStorage(bindingStorage);

    // Set record field offsets if the head is a known record type
    if (auto objTypeId = getObjectTypeId(head))
    {
        for (auto const& [typeName, recInfo]: _recordTypes)
        {
            if (recInfo.typeId == *objTypeId)
            {
                std::unordered_map<std::string, uint8_t> fieldOffsets;
                for (auto const& field: recInfo.fields)
                    fieldOffsets[field.name] = field.offset;
                patternGen.setRecordFieldOffsets(std::move(fieldOffsets));
                break;
            }
        }
    }

    patternGen.compile(*node.pattern, headReloaded, headStorage, destructureOk, destructureFail);

    // Fail block: runtime pattern match failure
    _builder.setInsertPoint(destructureFail);
    _builder.createRet(_builder.get(CoreVM::CoreNumber(1)));

    // Success block: bind variables, execute body
    _builder.setInsertPoint(destructureOk);

    pushFSharpScope();
    for (auto const& name: bindingNames)
        bindFSharpVariable(name, bindingStorage[name]);

    pushLoopContext(condBlock, endBlock);
    codegen(node.body.get());
    popLoopContext();
    popFSharpScope();

    // Only branch back to condition if body wasn't terminated (by break/continue/return)
    if (_builder.getInsertPoint() && !_builder.getInsertPoint()->getTerminator())
        _builder.createBr(condBlock);

    // End block: continue after loop
    _builder.setInsertPoint(endBlock);
    _result = nullptr;
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

void IRGenerator::visit(ast::BreakExpr const& /*node*/)
{
    auto* ctx = getLoopContext(1);
    if (!ctx)
    {
        reportTypeError("break: not in a loop");
        return;
    }
    _builder.createBr(ctx->breakTarget);
    _result = nullptr;
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

void IRGenerator::visit(ast::ContinueExpr const& /*node*/)
{
    auto* ctx = getLoopContext(1);
    if (!ctx)
    {
        reportTypeError("continue: not in a loop");
        return;
    }
    _builder.createBr(ctx->continueTarget);
    _result = nullptr;
}

CoreVM::Value* IRGenerator::toBool(CoreVM::Value* value)
{
    if (value->type() == CoreVM::LiteralType::Boolean)
        return value;
    if (value->type() == CoreVM::LiteralType::Float)
        return _builder.createFCmpEQ(value, _builder.getFloat(0.0));
    if (value->type() == CoreVM::LiteralType::String)
        return _builder.createSCmpEQ(value, _builder.get(std::string("")));
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
        if (dynamic_cast<ast::SplatExpr const*>(expr.get()) != nullptr)
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
        // SplatExpr emits cmd_arg calls internally (in a loop), so skip the explicit cmd_arg here
        if (dynamic_cast<ast::SplatExpr const*>(arg.get()))
        {
            codegen(arg.get());
            continue;
        }

        auto* value = codegen(arg.get());
        if (!value)
            continue; // Error already reported

        _builder.createCallFunction(_builder.getBuiltinFunction(*cmdArgCallback), { value }, "cmd_arg");
    }
}

void IRGenerator::buildCommandArgs(CoreVM::Value* programNameValue,
                                   std::vector<std::unique_ptr<ast::Expr>> const& args)
{
    TRACE_SCOPE("buildCommandArgs(Value*)");

    // Start building the command with the runtime-evaluated program name
    auto* cmdStartCallback = findCallback("internal.cmd_start(S)V");
    if (!cmdStartCallback)
    {
        reportTypeError("Internal error: internal.cmd_start builtin not found");
        return;
    }
    _builder.createCallFunction(
        _builder.getBuiltinFunction(*cmdStartCallback), { programNameValue }, "cmd_start");

    // Add each argument
    auto* cmdArgCallback = findCallback("internal.cmd_arg(S)V");
    if (!cmdArgCallback)
    {
        reportTypeError("Internal error: internal.cmd_arg builtin not found");
        return;
    }

    for (auto const& arg: args)
    {
        if (dynamic_cast<ast::SplatExpr const*>(arg.get()))
        {
            codegen(arg.get());
            continue;
        }

        auto* value = codegen(arg.get());
        if (!value)
            continue;

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
        if (auto objTypeId = getObjectTypeId(value))
        {
            if (*objTypeId == CoreVM::BuiltinTypeId::List)
            {
                auto* callback = findCallback("list_to_string(I)S");
                if (callback)
                    return _builder.createCallFunction(
                        _builder.getBuiltinFunction(*callback), { value }, std::string(label) + ".list2s");
            }
            else
            {
                // Typed objects stored as Number (records, tuples, options, results from native callbacks).
                // Dispatch to object_to_string which handles all typed objects at runtime via
                // valueToString().
                auto* callback = findCallback("object_to_string(I)S");
                if (callback)
                    return _builder.createCallFunction(
                        _builder.getBuiltinFunction(*callback), { value }, std::string(label) + ".obj2s");
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
        // Check if value is a known typed object via annotation or IR chain analysis.
        // This must come BEFORE getInnerType() because container types (Option, Result, Tuple)
        // use annotateInnerType() to describe their payload type, not themselves.
        bool isList = false;
        bool isTypedObject = false;
        if (auto objTypeId = getObjectTypeId(value))
        {
            isList = (*objTypeId == CoreVM::BuiltinTypeId::List);
            if (!isList)
            {
                // Tuples, Options, Results, and records are all typed objects
                isTypedObject = true;
            }
        }
        else if (auto info = tryGetObjectInfo(value))
        {
            isList = (info->typeId == CoreVM::BuiltinTypeId::List);
            if (!isList)
                isTypedObject = true;
        }

        if (isList)
        {
            auto* callback = findCallback("list_to_string(I)S");
            if (callback)
            {
                return _builder.createCallFunction(
                    _builder.getBuiltinFunction(*callback), { value }, std::string(label) + ".list2s");
            }
        }
        if (isTypedObject)
        {
            auto* callback = findCallback("object_to_string(I)S");
            if (callback)
            {
                return _builder.createCallFunction(
                    _builder.getBuiltinFunction(*callback), { value }, std::string(label) + ".obj2s");
            }
        }

        // For non-container Object values, check inner type annotation
        // (e.g., a string field extracted from a record via pattern matching).
        if (auto innerType = getInnerType(value))
        {
            switch (*innerType)
            {
                case CoreVM::LiteralType::String: {
                    auto* storage = createAllocaInEntryBlock(CoreVM::LiteralType::String,
                                                             std::string(label) + ".o2s.tmp");
                    _builder.createStore(storage, value);
                    return _builder.createLoad(storage, std::string(label) + ".o2s");
                }
                case CoreVM::LiteralType::Number:
                    return _builder.createN2S(value, std::string(label) + ".n2s");
                case CoreVM::LiteralType::Float:
                    return _builder.createF2S(value, std::string(label) + ".f2s");
                default: break;
            }
        }
        // Fallback: assume numeric for unknown object types
        return _builder.createN2S(value, std::string(label) + ".n2s");
    }
    if (value->type() == CoreVM::LiteralType::Void)
    {
        // Dynamically-typed value (e.g., from pattern matching or OGETSLOT).
        // Check inner type annotation first (e.g., record field with known primitive type).
        if (auto innerType = getInnerType(value))
        {
            switch (*innerType)
            {
                case CoreVM::LiteralType::String: {
                    // Value is a string but IR-typed as Void — cast via typed alloca
                    auto* storage = createAllocaInEntryBlock(CoreVM::LiteralType::String,
                                                             std::string(label) + ".v2s.tmp");
                    _builder.createStore(storage, value);
                    return _builder.createLoad(storage, std::string(label) + ".v2s");
                }
                case CoreVM::LiteralType::Number:
                    return _builder.createN2S(value, std::string(label) + ".n2s");
                case CoreVM::LiteralType::Float:
                    return _builder.createF2S(value, std::string(label) + ".f2s");
                case CoreVM::LiteralType::Boolean: {
                    auto* trueBlock = _builder.createBlock(std::string(label) + ".b2s.true");
                    auto* falseBlock = _builder.createBlock(std::string(label) + ".b2s.false");
                    auto* mergeBlock = _builder.createBlock(std::string(label) + ".b2s.merge");
                    auto* storage = createAllocaInEntryBlock(CoreVM::LiteralType::String,
                                                             std::string(label) + ".b2s.tmp");
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
                default: break; // Fall through to object type checks
            }
        }

        // Check if we have a type ID annotation to dispatch correctly.
        auto objTypeId = getObjectTypeId(value);
        if (objTypeId && *objTypeId == CoreVM::BuiltinTypeId::List)
        {
            auto* callback = findCallback("list_to_string(I)S");
            if (callback)
            {
                return _builder.createCallFunction(
                    _builder.getBuiltinFunction(*callback), { value }, std::string(label) + ".list2s");
            }
        }
        // Check for record types
        if (objTypeId)
        {
            for (auto const& [name, info]: _recordTypes)
            {
                if (info.typeId == *objTypeId)
                {
                    auto* callback = findCallback("object_to_string(I)S");
                    if (callback)
                    {
                        return _builder.createCallFunction(
                            _builder.getBuiltinFunction(*callback), { value }, std::string(label) + ".rec2s");
                    }
                    break;
                }
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

CoreVM::Value* IRGenerator::ensureString(CoreVM::Value* value, std::string_view label)
{
    if (value->type() == CoreVM::LiteralType::String)
        return value;

    // For Number-typed values, use convertToString which handles N2S correctly
    if (value->type() == CoreVM::LiteralType::Number)
        return convertToString(value, label);

    // For Object/Void-typed values (e.g., from pattern matching on tuples),
    // the runtime value is a string but the IR type is wrong.
    // Reinterpret via typed alloca store/load to get a String-typed IR value.
    auto* storage = createAllocaInEntryBlock(CoreVM::LiteralType::String, std::string(label) + ".cast");
    _builder.createStore(storage, value);
    return _builder.createLoad(storage, std::string(label) + ".str");
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

    if (name == "string")
    {
        if (argExprs.size() != 1)
        {
            reportTypeError("string requires exactly 1 argument, got {}", argExprs.size());
            return true;
        }
        auto* argVal = codegen(argExprs[0]);
        if (!argVal)
        {
            reportTypeError("Failed to evaluate string argument");
            return true;
        }
        _result = convertToString(argVal, "string");
        if (!_result)
        {
            reportTypeError("Cannot convert value to string");
            return true;
        }
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

        // Construct Some(value)
        auto* someObj = emitSomeOption(getValue, CoreVM::LiteralType::String, "env.some.option");
        _builder.createStore(resultStorage, someObj);
        _builder.createBr(mergeBlock);

        // noneBlock: construct None
        _builder.setInsertPoint(noneBlock);
        auto* noneObj = emitNoneOption("env.none.option");
        _builder.createStore(resultStorage, noneObj);
        _builder.createBr(mergeBlock);

        // mergeBlock: load result
        _builder.setInsertPoint(mergeBlock);
        _result = _builder.createLoad(resultStorage, "env.result");
        annotateInnerType(_result, CoreVM::LiteralType::String);
        annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::Option);
        return true;
    }

    if (name == "which")
    {
        if (argExprs.size() != 1)
        {
            reportTypeError("which requires exactly 1 argument, got {}", argExprs.size());
            return true;
        }
        auto* argVal = codegen(argExprs[0]);
        if (!argVal)
        {
            reportTypeError("Failed to evaluate which argument");
            return true;
        }
        auto* callback = findCallback("which_find(S)I");
        if (!callback)
        {
            reportTypeError("which_find builtin not found");
            return true;
        }
        _result =
            _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { argVal }, "which_find");
        annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::Option);
        annotateInnerType(_result, CoreVM::LiteralType::String);
        return true;
    }

    if (name == "head")
    {
        if (argExprs.size() != 1)
        {
            reportTypeError("head requires exactly 1 argument, got {}", argExprs.size());
            return true;
        }
        auto* argVal = codegen(argExprs[0]);
        if (!argVal)
        {
            reportTypeError("Failed to evaluate head argument");
            return true;
        }
        auto* callback = findCallback("list_head(I)I");
        if (!callback)
        {
            reportTypeError("list_head builtin not found");
            return true;
        }
        _result =
            _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { argVal }, "list_head");
        annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::Option);
        // Propagate list element type as inner object type of the Option (Some wraps the element)
        if (auto elemTypeId = getListElementTypeId(argVal))
            annotateInnerObjectTypeId(_result, *elemTypeId);
        return true;
    }

    if (name == "tail")
    {
        if (argExprs.size() != 1)
        {
            reportTypeError("tail requires exactly 1 argument, got {}", argExprs.size());
            return true;
        }
        auto* argVal = codegen(argExprs[0]);
        if (!argVal)
        {
            reportTypeError("Failed to evaluate tail argument");
            return true;
        }
        auto* callback = findCallback("list_tail(I)I");
        if (!callback)
        {
            reportTypeError("list_tail builtin not found");
            return true;
        }
        _result =
            _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { argVal }, "list_tail");
        annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
        // Propagate list element type through tail (same element type as input list)
        if (auto elemTypeId = getListElementTypeId(argVal))
            annotateListElementTypeId(_result, *elemTypeId);
        if (auto elt = getListElementLiteralType(argVal))
            annotateListElementLiteralType(_result, *elt);
        return true;
    }

    if (name == "length")
    {
        if (argExprs.size() != 1)
        {
            reportTypeError("length requires exactly 1 argument, got {}", argExprs.size());
            return true;
        }
        auto* argVal = codegen(argExprs[0]);
        if (!argVal)
        {
            reportTypeError("Failed to evaluate length argument");
            return true;
        }
        auto* callback = findCallback("list_length(I)I");
        if (!callback)
        {
            reportTypeError("list_length builtin not found");
            return true;
        }
        _result =
            _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { argVal }, "list_length");
        return true;
    }

    if (name == "isEmpty")
    {
        if (argExprs.size() != 1)
        {
            reportTypeError("isEmpty requires exactly 1 argument, got {}", argExprs.size());
            return true;
        }
        auto* argVal = codegen(argExprs[0]);
        if (!argVal)
        {
            reportTypeError("Failed to evaluate isEmpty argument");
            return true;
        }
        auto* callback = findCallback("list_isEmpty(I)B");
        if (!callback)
        {
            reportTypeError("list_isEmpty builtin not found");
            return true;
        }
        _result =
            _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { argVal }, "list_isEmpty");
        return true;
    }

    if (name == "nth" && !lookupFSharpFunction(name))
    {
        if (argExprs.size() != 2)
        {
            reportTypeError("nth requires exactly 2 arguments (index, list), got {}", argExprs.size());
            return true;
        }
        auto* indexVal = codegen(argExprs[0]);
        if (!indexVal)
        {
            reportTypeError("Failed to evaluate nth index argument");
            return true;
        }
        auto* listVal = codegen(argExprs[1]);
        if (!listVal)
        {
            reportTypeError("Failed to evaluate nth list argument");
            return true;
        }
        auto* callback = findCallback("list_nth(II)I");
        if (!callback)
        {
            reportTypeError("list_nth builtin not found");
            return true;
        }
        _result = _builder.createCallFunction(
            _builder.getBuiltinFunction(*callback), { indexVal, listVal }, "list_nth");
        annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::Option);
        if (auto elemTypeId = getListElementTypeId(listVal))
            annotateInnerObjectTypeId(_result, *elemTypeId);
        return true;
    }

    if (name == "last" && !lookupFSharpFunction(name))
    {
        if (argExprs.size() != 1)
        {
            reportTypeError("last requires exactly 1 argument, got {}", argExprs.size());
            return true;
        }
        auto* argVal = codegen(argExprs[0]);
        if (!argVal)
        {
            reportTypeError("Failed to evaluate last argument");
            return true;
        }
        auto* callback = findCallback("list_last(I)I");
        if (!callback)
        {
            reportTypeError("list_last builtin not found");
            return true;
        }
        _result =
            _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { argVal }, "list_last");
        annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::Option);
        if (auto elemTypeId = getListElementTypeId(argVal))
            annotateInnerObjectTypeId(_result, *elemTypeId);
        return true;
    }

    if (name == "replicate" && !lookupFSharpFunction(name))
    {
        if (argExprs.size() != 2)
        {
            reportTypeError("replicate requires exactly 2 arguments (count, value), got {}", argExprs.size());
            return true;
        }
        auto* countVal = codegen(argExprs[0]);
        if (!countVal)
        {
            reportTypeError("Failed to evaluate replicate count argument");
            return true;
        }
        auto* valueVal = codegen(argExprs[1]);
        if (!valueVal)
        {
            reportTypeError("Failed to evaluate replicate value argument");
            return true;
        }
        auto* callback = findCallback("list_replicate(II)I");
        if (!callback)
        {
            reportTypeError("list_replicate builtin not found");
            return true;
        }
        _result = _builder.createCallFunction(
            _builder.getBuiltinFunction(*callback), { countVal, valueVal }, "list_replicate");
        annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
        if (valueVal->type() != CoreVM::LiteralType::Void)
            annotateListElementLiteralType(_result, valueVal->type());
        return true;
    }

    // --- String builtins ---

    // Unary string functions: trim, toLower, toUpper
    if (name == "trim" || name == "toLower" || name == "toUpper")
    {
        if (argExprs.size() != 1)
        {
            reportTypeError("{} requires exactly 1 argument, got {}", name, argExprs.size());
            return true;
        }
        auto* argVal = codegen(argExprs[0]);
        if (!argVal)
        {
            reportTypeError("Failed to evaluate {} argument", name);
            return true;
        }
        auto const sigName = "string_" + std::string(name);
        auto* callback = findCallback(sigName + "(S)S");
        if (!callback)
        {
            reportTypeError("{} builtin not found", sigName);
            return true;
        }
        _result = _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { argVal }, sigName);
        return true;
    }

    // Binary predicate string functions: contains, startsWith, endsWith
    if (name == "contains" || name == "startsWith" || name == "endsWith")
    {
        if (argExprs.size() != 2)
        {
            reportTypeError("{} requires exactly 2 arguments, got {}", name, argExprs.size());
            return true;
        }
        auto* arg1 = codegen(argExprs[0]);
        if (!arg1)
        {
            reportTypeError("Failed to evaluate {} first argument", name);
            return true;
        }
        auto* arg2 = codegen(argExprs[1]);
        if (!arg2)
        {
            reportTypeError("Failed to evaluate {} second argument", name);
            return true;
        }
        auto const sigName = "string_" + std::string(name);
        auto* callback = findCallback(sigName + "(SS)B");
        if (!callback)
        {
            reportTypeError("{} builtin not found", sigName);
            return true;
        }
        _result =
            _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { arg1, arg2 }, sigName);
        return true;
    }

    // replace: 3 args (old, new, text) → string
    if (name == "replace")
    {
        if (argExprs.size() != 3)
        {
            reportTypeError("replace requires exactly 3 arguments, got {}", argExprs.size());
            return true;
        }
        auto* arg1 = codegen(argExprs[0]);
        if (!arg1)
        {
            reportTypeError("Failed to evaluate replace first argument");
            return true;
        }
        auto* arg2 = codegen(argExprs[1]);
        if (!arg2)
        {
            reportTypeError("Failed to evaluate replace second argument");
            return true;
        }
        auto* arg3 = codegen(argExprs[2]);
        if (!arg3)
        {
            reportTypeError("Failed to evaluate replace third argument");
            return true;
        }
        auto* callback = findCallback("string_replace(SSS)S");
        if (!callback)
        {
            reportTypeError("string_replace builtin not found");
            return true;
        }
        _result = _builder.createCallFunction(
            _builder.getBuiltinFunction(*callback), { arg1, arg2, arg3 }, "string_replace");
        return true;
    }

    // split: 2 args (delimiter, text) → list<str>
    if (name == "split")
    {
        if (argExprs.size() != 2)
        {
            reportTypeError("split requires exactly 2 arguments, got {}", argExprs.size());
            return true;
        }
        auto* arg1 = codegen(argExprs[0]);
        if (!arg1)
        {
            reportTypeError("Failed to evaluate split first argument");
            return true;
        }
        auto* arg2 = codegen(argExprs[1]);
        if (!arg2)
        {
            reportTypeError("Failed to evaluate split second argument");
            return true;
        }
        auto* callback = findCallback("string_split(SS)I");
        if (!callback)
        {
            reportTypeError("string_split builtin not found");
            return true;
        }
        _result = _builder.createCallFunction(
            _builder.getBuiltinFunction(*callback), { arg1, arg2 }, "string_split");
        annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
        annotateListElementLiteralType(_result, CoreVM::LiteralType::String);
        return true;
    }

    // join: 2 args (separator, list) → str
    if (name == "join")
    {
        if (argExprs.size() != 2)
        {
            reportTypeError("join requires exactly 2 arguments, got {}", argExprs.size());
            return true;
        }
        auto* arg1 = codegen(argExprs[0]);
        if (!arg1)
        {
            reportTypeError("Failed to evaluate join first argument");
            return true;
        }
        auto* arg2 = codegen(argExprs[1]);
        if (!arg2)
        {
            reportTypeError("Failed to evaluate join second argument");
            return true;
        }
        auto* callback = findCallback("string_join(SI)S");
        if (!callback)
        {
            reportTypeError("string_join builtin not found");
            return true;
        }
        _result = _builder.createCallFunction(
            _builder.getBuiltinFunction(*callback), { arg1, arg2 }, "string_join");
        return true;
    }

    // toText: convert object value to its string representation
    if (name == "toText")
    {
        if (argExprs.size() != 1)
        {
            reportTypeError("toText requires exactly 1 argument, got {}", argExprs.size());
            return true;
        }
        auto* argVal = codegen(argExprs[0]);
        if (!argVal)
        {
            reportTypeError("Failed to evaluate toText argument");
            return true;
        }
        auto* callback = findCallback("object_to_string(I)S");
        if (!callback)
        {
            reportTypeError("object_to_string builtin not found");
            return true;
        }
        _result = _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { argVal }, "toText");
        return true;
    }

    // ps: zero-arg builtin returning list<ProcessInfo>
    if (name == "ps")
    {
        if (!argExprs.empty())
        {
            reportTypeError("ps takes no arguments, got {}", argExprs.size());
            return true;
        }
        auto* callback = findCallback("structured_ps()I");
        if (!callback)
        {
            reportTypeError("structured_ps builtin not registered");
            return true;
        }
        _result = _builder.createCallFunction(_builder.getBuiltinFunction(*callback), {}, "ps");
        annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
        annotateListElementTypeId(_result, CoreVM::BuiltinTypeId::ProcessInfo);
        return true;
    }

    // jobs: zero-arg builtin returning list<JobInfo>
    if (name == "jobs")
    {
        if (!argExprs.empty())
        {
            reportTypeError("jobs takes no arguments, got {}", argExprs.size());
            return true;
        }
        auto* callback = findCallback("structured_jobs()I");
        if (!callback)
        {
            reportTypeError("structured_jobs builtin not registered");
            return true;
        }
        _result = _builder.createCallFunction(_builder.getBuiltinFunction(*callback), {}, "jobs");
        annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
        annotateListElementTypeId(_result, CoreVM::BuiltinTypeId::JobInfo);
        return true;
    }

    // ls: zero-or-one-arg builtin returning list<FileInfo>
    if (name == "ls")
    {
        if (argExprs.size() > 1)
        {
            reportTypeError("ls takes 0 or 1 arguments, got {}", argExprs.size());
            return true;
        }
        auto* callback = findCallback("structured_ls(S)I");
        if (!callback)
        {
            reportTypeError("structured_ls builtin not registered");
            return true;
        }
        CoreVM::Value* pathArg = nullptr;
        if (argExprs.empty())
        {
            pathArg = _builder.get(".");
        }
        else
        {
            pathArg = codegen(argExprs[0]);
            if (!pathArg)
            {
                reportTypeError("Failed to evaluate ls path argument");
                return true;
            }
        }
        _result = _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { pathArg }, "ls");
        annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
        annotateListElementTypeId(_result, CoreVM::BuiltinTypeId::FileInfo);
        return true;
    }

    // formatDateTime: int -> string (format epoch seconds as UTC datetime string)
    if (name == "formatDateTime")
    {
        if (argExprs.size() != 1)
        {
            reportTypeError("formatDateTime takes 1 argument, got {}", argExprs.size());
            return true;
        }
        auto* arg = codegen(argExprs[0]);
        if (!arg)
        {
            reportTypeError("Failed to evaluate formatDateTime argument");
            return true;
        }
        auto* callback = findCallback("format_datetime(I)S");
        if (!callback)
        {
            reportTypeError("format_datetime builtin not registered");
            return true;
        }
        _result =
            _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { arg }, "formatDateTime");
        return true;
    }

    // formatMode: int -> string (format permission bits as rwx string)
    if (name == "formatMode")
    {
        if (argExprs.size() != 1)
        {
            reportTypeError("formatMode takes 1 argument, got {}", argExprs.size());
            return true;
        }
        auto* arg = codegen(argExprs[0]);
        if (!arg)
        {
            reportTypeError("Failed to evaluate formatMode argument");
            return true;
        }
        auto* callback = findCallback("format_mode(I)S");
        if (!callback)
        {
            reportTypeError("format_mode builtin not registered");
            return true;
        }
        _result = _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { arg }, "formatMode");
        return true;
    }

    // formatNumber: string -> int -> string  OR  int -> string (locale-aware)
    if (name == "formatNumber")
    {
        if (argExprs.size() == 1)
        {
            // 1-arg: use user's locale thousand separator
            auto* num = codegen(argExprs[0]);
            if (!num)
            {
                reportTypeError("Failed to evaluate formatNumber number argument");
                return true;
            }
            auto* callback = findCallback("format_number(I)S");
            if (!callback)
            {
                reportTypeError("format_number builtin not registered");
                return true;
            }
            _result =
                _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { num }, "formatNumber");
            return true;
        }
        if (argExprs.size() == 2)
        {
            // 2-arg: explicit separator
            auto* sep = codegen(argExprs[0]);
            if (!sep)
            {
                reportTypeError("Failed to evaluate formatNumber separator argument");
                return true;
            }
            auto* num = codegen(argExprs[1]);
            if (!num)
            {
                reportTypeError("Failed to evaluate formatNumber number argument");
                return true;
            }
            auto* callback = findCallback("format_number(SI)S");
            if (!callback)
            {
                reportTypeError("format_number builtin not registered");
                return true;
            }
            _result = _builder.createCallFunction(
                _builder.getBuiltinFunction(*callback), { sep, num }, "formatNumber");
            return true;
        }
        reportTypeError("formatNumber requires 1 or 2 arguments, got {}", argExprs.size());
        return true;
    }

    // isReadable/isWritable/isExecutable: int -> bool (test permission bits)
    if (name == "isReadable" || name == "isWritable" || name == "isExecutable")
    {
        if (argExprs.size() != 1)
        {
            reportTypeError("{} takes 1 argument, got {}", name, argExprs.size());
            return true;
        }
        auto* arg = codegen(argExprs[0]);
        if (!arg)
        {
            reportTypeError("Failed to evaluate {} argument", name);
            return true;
        }
        auto const callbackSig = std::format("mode_{}(I)B", name);
        auto* callback = findCallback(callbackSig);
        if (!callback)
        {
            reportTypeError("{} builtin not registered", name);
            return true;
        }
        _result = _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { arg }, name);
        return true;
    }

    // fetch: 1 or 2 args, returns Result<string, string>
    if (name == "fetch")
    {
        if (argExprs.empty() || argExprs.size() > 2)
        {
            reportTypeError("fetch requires 1 or 2 arguments (url [, headers]), got {}", argExprs.size());
            return true;
        }

        auto* urlVal = codegen(argExprs[0]);
        if (!urlVal)
        {
            reportTypeError("Failed to evaluate fetch url argument");
            return true;
        }
        if (urlVal->type() != CoreVM::LiteralType::String)
        {
            reportTypeError("fetch url argument must be a string");
            return true;
        }

        if (argExprs.size() == 1)
        {
            auto* callback = findCallback("fetch(S)I");
            if (!callback)
            {
                reportTypeError("fetch builtin not registered");
                return true;
            }
            _result =
                _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { urlVal }, "fetch");
        }
        else
        {
            auto* headersVal = codegen(argExprs[1]);
            if (!headersVal)
            {
                reportTypeError("Failed to evaluate fetch headers argument");
                return true;
            }
            auto* callback = findCallback("fetch(SI)I");
            if (!callback)
            {
                reportTypeError("fetch builtin not registered");
                return true;
            }
            _result = _builder.createCallFunction(
                _builder.getBuiltinFunction(*callback), { urlVal, headersVal }, "fetch");
        }

        annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::Result);
        annotateInnerType(_result, CoreVM::LiteralType::String);
        return true;
    }

    if (name == "rand")
    {
        if (argExprs.size() == 0)
        {
            // rand — no arguments, returns random positive integer
            auto* callback = findCallback("rand()I");
            if (!callback)
            {
                reportTypeError("rand builtin not available");
                return true;
            }
            _result = _builder.createCallFunction(_builder.getBuiltinFunction(*callback), {}, "rand");
            return true;
        }
        if (argExprs.size() == 2)
        {
            // rand A B — returns random integer in [A, B]
            auto* minVal = codegen(argExprs[0]);
            if (!minVal)
            {
                reportTypeError("Failed to evaluate rand min argument");
                return true;
            }
            auto* maxVal = codegen(argExprs[1]);
            if (!maxVal)
            {
                reportTypeError("Failed to evaluate rand max argument");
                return true;
            }
            auto* callback = findCallback("rand(II)I");
            if (!callback)
            {
                reportTypeError("rand builtin not available");
                return true;
            }
            _result = _builder.createCallFunction(
                _builder.getBuiltinFunction(*callback), { minVal, maxVal }, "rand");
            return true;
        }
        reportTypeError("rand requires 0 or 2 arguments, got {}", argExprs.size());
        return true;
    }

    return false;
}

bool IRGenerator::tryGenerateBuiltinPropertyAccess(CoreVM::Value* obj, std::string const& fieldName)
{
    // Determine the object's type ID
    auto const objTypeId = getObjectTypeId(obj);
    auto const objLiteralType = obj->type();

    // --- String dot properties ---
    if (objLiteralType == CoreVM::LiteralType::String)
    {
        if (fieldName == "length")
        {
            _result = _builder.createSLen(obj, "string.length");
            return true;
        }
        return false;
    }

    if (!objTypeId)
        return false;

    // --- List dot properties ---
    if (*objTypeId == CoreVM::BuiltinTypeId::List)
    {
        if (fieldName == "length")
        {
            auto* callback = findCallback("list_length(I)I");
            if (!callback)
            {
                reportTypeError("list_length builtin not found");
                return true;
            }
            _result =
                _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { obj }, "list.length");
            return true;
        }
        if (fieldName == "isEmpty")
        {
            auto* callback = findCallback("list_isEmpty(I)B");
            if (!callback)
            {
                reportTypeError("list_isEmpty builtin not found");
                return true;
            }
            _result =
                _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { obj }, "list.isEmpty");
            return true;
        }
        if (fieldName == "head")
        {
            auto* callback = findCallback("list_head(I)I");
            if (!callback)
            {
                reportTypeError("list_head builtin not found");
                return true;
            }
            _result =
                _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { obj }, "list.head");
            annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::Option);
            if (auto elemTypeId = getListElementTypeId(obj))
                annotateInnerObjectTypeId(_result, *elemTypeId);
            return true;
        }
        if (fieldName == "tail")
        {
            auto* callback = findCallback("list_tail(I)I");
            if (!callback)
            {
                reportTypeError("list_tail builtin not found");
                return true;
            }
            _result =
                _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { obj }, "list.tail");
            annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
            if (auto elemTypeId = getListElementTypeId(obj))
                annotateListElementTypeId(_result, *elemTypeId);
            if (auto elt = getListElementLiteralType(obj))
                annotateListElementLiteralType(_result, *elt);
            return true;
        }
        if (fieldName == "last")
        {
            auto* callback = findCallback("list_last(I)I");
            if (!callback)
            {
                reportTypeError("list_last builtin not found");
                return true;
            }
            _result =
                _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { obj }, "list.last");
            annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::Option);
            if (auto elemTypeId = getListElementTypeId(obj))
                annotateInnerObjectTypeId(_result, *elemTypeId);
            return true;
        }
        return false;
    }

    // --- Option dot properties ---
    if (*objTypeId == CoreVM::BuiltinTypeId::Option)
    {
        if (fieldName == "isSome")
        {
            auto* tag = _builder.createObjGetTag(obj, "option.tag");
            _result = _builder.createNCmpEQ(tag, _builder.get(CoreVM::CoreNumber(1)), "option.isSome");
            return true;
        }
        if (fieldName == "isNone")
        {
            auto* tag = _builder.createObjGetTag(obj, "option.tag");
            _result = _builder.createNCmpEQ(tag, _builder.get(CoreVM::CoreNumber(0)), "option.isNone");
            return true;
        }
        return false;
    }

    // --- Result dot properties ---
    // Result convention: Ok = tag 1, Error = tag 0
    if (*objTypeId == CoreVM::BuiltinTypeId::Result)
    {
        if (fieldName == "isOk")
        {
            auto* tag = _builder.createObjGetTag(obj, "result.tag");
            _result = _builder.createNCmpEQ(tag, _builder.get(CoreVM::CoreNumber(1)), "result.isOk");
            return true;
        }
        if (fieldName == "isError")
        {
            auto* tag = _builder.createObjGetTag(obj, "result.tag");
            _result = _builder.createNCmpEQ(tag, _builder.get(CoreVM::CoreNumber(0)), "result.isError");
            return true;
        }
        return false;
    }

    // --- Tuple2 dot properties ---
    if (*objTypeId == CoreVM::BuiltinTypeId::Tuple2)
    {
        if (fieldName == "fst" || fieldName == "0")
        {
            _result = _builder.createObjGetSlot(obj, _builder.get(CoreVM::CoreNumber(0)), "tuple.fst");
            return true;
        }
        if (fieldName == "snd" || fieldName == "1")
        {
            _result = _builder.createObjGetSlot(obj, _builder.get(CoreVM::CoreNumber(1)), "tuple.snd");
            return true;
        }
        return false;
    }

    // --- Tuple3 dot properties ---
    if (*objTypeId == CoreVM::BuiltinTypeId::Tuple3)
    {
        if (fieldName == "fst" || fieldName == "0")
        {
            _result = _builder.createObjGetSlot(obj, _builder.get(CoreVM::CoreNumber(0)), "tuple.fst");
            return true;
        }
        if (fieldName == "snd" || fieldName == "1")
        {
            _result = _builder.createObjGetSlot(obj, _builder.get(CoreVM::CoreNumber(1)), "tuple.snd");
            return true;
        }
        if (fieldName == "trd" || fieldName == "2")
        {
            _result = _builder.createObjGetSlot(obj, _builder.get(CoreVM::CoreNumber(2)), "tuple.trd");
            return true;
        }
        return false;
    }

    return false;
}

bool IRGenerator::tryGenerateNativeCall(std::string const& name, std::vector<CoreVM::Value*> const& args)
{
    // Search runtime builtins for a function matching by name and argument count.
    for (auto const* builtin: _runtime.builtins())
    {
        if (builtin->signature().name() != name)
            continue;

        auto const& sig = builtin->signature();
        // Function signatures list only user-visible parameters (no hidden function context).
        if (sig.args().size() != args.size())
            continue;

        // Convert arguments to match expected types
        auto convertedArgs = std::vector<CoreVM::Value*> {};
        convertedArgs.reserve(args.size());
        for (std::size_t i = 0; i < args.size(); ++i)
        {
            auto* arg = args[i];
            auto const expectedType = sig.args()[i];
            if (arg->type() != expectedType)
            {
                // Try basic type conversions
                if (expectedType == CoreVM::LiteralType::String && arg->type() == CoreVM::LiteralType::Number)
                    arg = _builder.createN2S(arg, "n2s");
                else if (expectedType == CoreVM::LiteralType::Number
                         && arg->type() == CoreVM::LiteralType::String)
                    arg = _builder.createS2N(arg, "s2n");
            }
            convertedArgs.push_back(arg);
        }

        _result = _builder.createCallFunction(_builder.getBuiltinFunction(*builtin), convertedArgs, name);
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
    else if (_activeRecursion || _activeMutualRecursion || _compilingFunction
             || (_builder.getInsertPoint() && _builder.getInsertPoint()->getTerminator()))
    {
        // Tail call or break/continue in then branch — no merge needed from this path
    }
    else
    {
        reportTypeError("Failed to generate code for if-then branch");
        return;
    }

    // Else branch (unit value when no else clause)
    _builder.setInsertPoint(elseBlock);
    auto* elseResult = node.elseExpr ? codegen(node.elseExpr.get()) : _builder.get(CoreVM::CoreNumber(0));
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
    else if (_activeRecursion || _activeMutualRecursion || _compilingFunction
             || (_builder.getInsertPoint() && _builder.getInsertPoint()->getTerminator()))
    {
        // Tail call or break/continue in else branch — no merge needed from this path
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

    // Codegen all elements, storing each to an alloca immediately.
    // Elements like `env "X"` create control-flow blocks; storing before the next
    // element's codegen prevents block-boundary cleanup from discarding values.
    std::vector<CoreVM::AllocaInstr*> elemAllocas;
    elemAllocas.reserve(node.elements.size());
    for (size_t i = 0; i < node.elements.size(); ++i)
    {
        auto* val = codegen(node.elements[i].get());
        if (!val)
        {
            reportTypeError("Failed to generate code for tuple element");
            return;
        }
        auto* alloca = createAllocaInEntryBlock(val->type(), "tuple.elem." + std::to_string(i));
        _builder.createStore(alloca, val);
        elemAllocas.push_back(alloca);
    }

    // Reload each element from its alloca and emit the tuple
    if (node.elements.size() == 2)
    {
        auto* e0 = _builder.createLoad(elemAllocas[0], "tuple.elem.reload.0");
        auto* e1 = _builder.createLoad(elemAllocas[1], "tuple.elem.reload.1");
        _result = emitTuple2(e0, e1, elemAllocas[0]->type(), elemAllocas[1]->type(), "tuple");
        annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::Tuple2);
    }
    else
    {
        auto* e0 = _builder.createLoad(elemAllocas[0], "tuple.elem.reload.0");
        auto* e1 = _builder.createLoad(elemAllocas[1], "tuple.elem.reload.1");
        auto* e2 = _builder.createLoad(elemAllocas[2], "tuple.elem.reload.2");
        _result = emitTuple3(
            e0, e1, e2, elemAllocas[0]->type(), elemAllocas[1]->type(), elemAllocas[2]->type(), "tuple");
        annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::Tuple3);
    }
}

void IRGenerator::visit(ast::MutAssignStmt const& node)
{
    TRACE_SCOPE("visit(MutAssignStmt)");

    // Check if this is a user-defined property setter invocation
    if (auto it = _fsharpProperties.find(node.name); it != _fsharpProperties.end())
    {
        if (!it->second.setter)
        {
            reportTypeError("Cannot assign to read-only property '{}'", std::string_view(node.name));
            return;
        }
        auto* newValue = codegen(node.value.get());
        if (!newValue)
        {
            reportTypeError("Failed to generate code for assignment value");
            return;
        }
        pushFSharpScope();
        auto* paramStorage = createAllocaInEntryBlock(newValue->type(), it->second.setter->paramName);
        _builder.createStore(paramStorage, newValue, it->second.setter->paramName + ".store");
        bindFSharpVariable(it->second.setter->paramName, paramStorage, false);
        codegen(it->second.setter->body.get());
        popFSharpScope();
        _result = nullptr;
        return;
    }

    // Check if this is a builtin property setter invocation
    if (auto const* prop = _runtime.findProperty(node.name))
    {
        if (!prop->hasSetter())
        {
            reportTypeError("Cannot assign to read-only property '{}'", std::string_view(node.name));
            return;
        }
        auto* newValue = codegen(node.value.get());
        if (!newValue)
        {
            reportTypeError("Failed to generate code for assignment value");
            return;
        }
        // Emit call to setter callback: name(T)V
        auto const setterSig = node.name + "(" + CoreVM::signatureType(prop->type()) + ")V";
        if (auto* cb = findCallback(setterSig))
        {
            _builder.createCallFunction(_builder.getBuiltinFunction(*cb), { newValue }, node.name + ".set");
        }
        _result = nullptr;
        return;
    }

    // Look up the binding
    auto const* binding = lookupFSharpBinding(node.name);
    if (!binding)
    {
        reportTypeErrorWithSuggestions({ std::format("Declare with 'let mut {} = <value>'", node.name) },
                                       "Undefined variable: {}",
                                       std::string_view(node.name));
        return;
    }

    if (!binding->isMutable)
    {
        reportTypeErrorWithSuggestions(
            { std::format("Use 'let mut {}' to declare a mutable variable", node.name) },
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

    // Re-export if the variable was declared with `let export mut`
    if (binding->isExported)
        emitExportVariable(binding->value, node.name);

    _result = nullptr;
}

void IRGenerator::visit(ast::MutAssignExpr const& node)
{
    TRACE_SCOPE("visit(MutAssignExpr)");

    // Check if this is a user-defined property setter invocation
    if (auto it = _fsharpProperties.find(node.name); it != _fsharpProperties.end())
    {
        if (!it->second.setter)
        {
            reportTypeError("Cannot assign to read-only property '{}'", std::string_view(node.name));
            return;
        }
        auto* newValue = codegen(node.value.get());
        if (!newValue)
        {
            reportTypeError("Failed to generate code for assignment value");
            return;
        }
        pushFSharpScope();
        auto* paramStorage = createAllocaInEntryBlock(newValue->type(), it->second.setter->paramName);
        _builder.createStore(paramStorage, newValue, it->second.setter->paramName + ".store");
        bindFSharpVariable(it->second.setter->paramName, paramStorage, false);
        codegen(it->second.setter->body.get());
        popFSharpScope();
        _result = _builder.get(CoreVM::CoreNumber(0)); // returns unit
        return;
    }

    // Check if this is a builtin property setter invocation
    if (auto const* prop = _runtime.findProperty(node.name))
    {
        if (!prop->hasSetter())
        {
            reportTypeError("Cannot assign to read-only property '{}'", std::string_view(node.name));
            return;
        }
        auto* newValue = codegen(node.value.get());
        if (!newValue)
        {
            reportTypeError("Failed to generate code for assignment value");
            return;
        }
        // Emit call to setter callback: name(T)V
        auto const setterSig = node.name + "(" + CoreVM::signatureType(prop->type()) + ")V";
        if (auto* cb = findCallback(setterSig))
        {
            _builder.createCallFunction(_builder.getBuiltinFunction(*cb), { newValue }, node.name + ".set");
        }
        _result = _builder.get(CoreVM::CoreNumber(0)); // returns unit
        return;
    }

    // Look up the binding
    auto const* binding = lookupFSharpBinding(node.name);
    if (!binding)
    {
        reportTypeErrorWithSuggestions({ std::format("Declare with 'let mut {} = <value>'", node.name) },
                                       "Undefined variable: {}",
                                       std::string_view(node.name));
        return;
    }

    if (!binding->isMutable)
    {
        reportTypeErrorWithSuggestions(
            { std::format("Use 'let mut {}' to declare a mutable variable", node.name) },
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

    // Re-export if the variable was declared with `let export mut`
    if (binding->isExported)
        emitExportVariable(binding->value, node.name);

    // As an expression, mutation returns unit
    _result = _builder.get(CoreVM::CoreNumber(0));
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

        // Set record field offsets if the value is a known record type
        if (auto objTypeId = getObjectTypeId(value))
        {
            for (auto const& [typeName, recInfo]: _recordTypes)
            {
                if (recInfo.typeId == *objTypeId)
                {
                    std::unordered_map<std::string, uint8_t> fieldOffsets;
                    for (auto const& field: recInfo.fields)
                        fieldOffsets[field.name] = field.offset;
                    patternGen.setRecordFieldOffsets(std::move(fieldOffsets));
                    break;
                }
            }
        }

        auto* successBlock = _builder.createBlock("destructure.ok");
        auto* failBlock = _builder.createBlock("destructure.fail");

        patternGen.compile(*node.destructurePattern, value, scrutineeStorage, successBlock, failBlock);

        // Fail block: runtime error (tuple destructure failed)
        _builder.setInsertPoint(failBlock);
        _builder.createRet(_builder.get(CoreVM::CoreNumber(1)));

        // Success block: register all bindings in F# scope
        _builder.setInsertPoint(successBlock);

        // Look up record type info for field type annotations
        RecordTypeInfo const* recTypeInfo = nullptr;
        if (auto objTypeId = getObjectTypeId(value))
        {
            for (auto const& [typeName, recInfo]: _recordTypes)
            {
                if (recInfo.typeId == *objTypeId)
                {
                    recTypeInfo = &recInfo;
                    break;
                }
            }
        }

        for (auto const& name: bindingNames)
        {
            bindFSharpVariable(name, bindingStorage[name], node.isMutable);

            // Annotate record field bindings with their field types
            if (recTypeInfo)
            {
                if (auto it = recTypeInfo->fieldTypes.find(name); it != recTypeInfo->fieldTypes.end())
                    annotateInnerType(bindingStorage[name], it->second);
            }
        }

        _result = nullptr;
        return;
    }

    // Property definition: let Name with get () = ... and set (v) = ...
    if (node.isProperty())
    {
        FSharpProperty prop;
        if (node.getter)
            prop.getter = node.getter.get();
        if (node.setter)
            prop.setter = node.setter.get();
        _fsharpProperties[node.name] = prop;

        // Persist property for REPL sessions
        // (AST is retained by the caller — Shell/TestHelper push the full statement into retainedASTs)
        if (_persistentState)
        {
            _persistentState->properties[node.name] = FSharpPersistentState::PersistedProperty {
                node.getter.get(),
                node.setter.get(),
            };
        }

        _result = nullptr;
        return;
    }

    if (node.isExported && node.isFunction())
    {
        reportTypeError("'let export' cannot be used with function definitions");
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

            // Compile functions as separate IRFunctions (with captures as extra params).
            // For recursive functions, compiledFunction is set before body codegen so that
            // recursive references emit UCALL/UTCALL instead of infinite AST inlining.
            if (auto* registered = const_cast<FSharpFunction*>(lookupFSharpFunction(node.name)))
                compileFunctionBody(node.name, *registered);
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

        // Compile mutual recursion 'and' bindings as functions (after ALL are registered)
        if (isMutual)
        {
            for (auto const& ab: node.andBindings)
            {
                if (auto* registered = const_cast<FSharpFunction*>(lookupFSharpFunction(ab.name)))
                    compileFunctionBody(ab.name, *registered);
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
        if (node.isExported)
        {
            reportTypeError("'let export' cannot be used with lambda expressions");
            return;
        }

        FSharpFunction func;
        extractTypedParameters(lambda->parameters, func);
        applyInferredTypes(node.name, func);
        func.body = lambda->body.get();
        func.returnKind = determineReturnKind(func.body);
        func.capturedBindings = collectFreeVariables(func.body, func.parameters);
        registerFSharpFunction(node.name, std::move(func));

        // Compile lambda-as-variable as separate IRFunction (with captures as extra params)
        if (auto* registered = const_cast<FSharpFunction*>(lookupFSharpFunction(node.name)))
            compileFunctionBody(node.name, *registered);

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
                        || dynamic_cast<ast::ConcatListExpr const*>(node.value.get()) != nullptr
                        || dynamic_cast<ast::RecordExpr const*>(node.value.get()) != nullptr
                        || dynamic_cast<ast::RecordUpdateExpr const*>(node.value.get()) != nullptr
                        || dynamic_cast<ast::ListComprehensionExpr const*>(node.value.get()) != nullptr
                        || dynamic_cast<ast::UnionConstructorExpr const*>(node.value.get()) != nullptr
                        || dynamic_cast<ast::SizeLiteralExpr const*>(node.value.get()) != nullptr;

    // Reject compound types for export — only scalars (string, number, float, bool) are allowed.
    // Users should compose with |> join ":" to convert lists before exporting.
    if (node.isExported && isObjectExpr)
    {
        reportTypeError("'let export' requires a scalar type (string, number, float, bool), "
                        "not a compound type. Use '|> join \":\"' to convert lists to strings.");
        return;
    }

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

    // Propagate list element type annotation through the binding
    if (auto elemTypeId = getListElementTypeId(value))
        annotateListElementTypeId(storage, *elemTypeId);
    if (auto elt = getListElementLiteralType(value))
        annotateListElementLiteralType(storage, *elt);

    // Register in F# scope - track objects for ORELEASE at scope exit
    if (isObjectExpr)
    {
        bindFSharpObjectVariable(node.name, storage, node.isMutable);
    }
    else
    {
        bindFSharpVariable(node.name, storage, node.isMutable, node.isExported);
    }

    // Export the binding as an environment variable if requested
    if (node.isExported)
        emitExportVariable(storage, node.name);

    // Record for REPL persistence (re-evaluated at each subsequent prompt)
    _newValueBindings.push_back(
        { node.name, node.value.get(), node.isMutable, isObjectExpr, storageType, node.isExported });

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

        // Set record field offsets if the value is a known record type
        RecordTypeInfo const* recTypeInfo = nullptr;
        if (auto objTypeId = getObjectTypeId(value))
        {
            for (auto const& [typeName, recInfo]: _recordTypes)
            {
                if (recInfo.typeId == *objTypeId)
                {
                    std::unordered_map<std::string, uint8_t> fieldOffsets;
                    for (auto const& field: recInfo.fields)
                        fieldOffsets[field.name] = field.offset;
                    patternGen.setRecordFieldOffsets(std::move(fieldOffsets));
                    recTypeInfo = &recInfo;
                    break;
                }
            }
        }

        auto* successBlock = _builder.createBlock("destructure.ok");
        auto* failBlock = _builder.createBlock("destructure.fail");

        patternGen.compile(*node.destructurePattern, value, scrutineeStorage, successBlock, failBlock);

        _builder.setInsertPoint(failBlock);
        _builder.createRet(_builder.get(CoreVM::CoreNumber(1)));

        _builder.setInsertPoint(successBlock);
        for (auto const& name: bindingNames)
        {
            bindFSharpVariable(name, bindingStorage[name]);

            // Annotate record field bindings with their field types
            if (recTypeInfo)
            {
                if (auto it = recTypeInfo->fieldTypes.find(name); it != recTypeInfo->fieldTypes.end())
                    annotateInnerType(bindingStorage[name], it->second);
            }
        }

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

        // Include function name as bound for recursive functions (prevents self-capture)
        auto allBound = func.parameters;
        if (node.isRecursive)
            allBound.push_back(node.name);
        func.capturedBindings = collectFreeVariables(func.body, allBound);

        registerFSharpFunction(node.name, std::move(func));

        // Compile as function (same pattern as LetBindingStmt)
        if (auto* registered = const_cast<FSharpFunction*>(lookupFSharpFunction(node.name)))
            compileFunctionBody(node.name, *registered);
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

        // Propagate list element type annotation through the binding
        if (auto elemTypeId = getListElementTypeId(value))
            annotateListElementTypeId(storage, *elemTypeId);
        if (auto elt = getListElementLiteralType(value))
            annotateListElementLiteralType(storage, *elt);

        bindFSharpVariable(node.name, storage);
    }

    // Evaluate the body expression with the binding in scope
    _result = codegen(node.body.get());

    popFSharpScope();
}

void IRGenerator::visit(ast::ExprStmt const& node)
{
    TRACE_SCOPE("visit(ExprStmt)");

    // Bare variadic function at statement level → invoke with zero args
    if (auto const* ident = dynamic_cast<ast::IdentifierExpr const*>(node.expr.get()))
    {
        if (auto const* func = lookupFSharpFunction(ident->name))
        {
            if (func->hasVariadicParam)
            {
                auto const savedCaptureMode = _shellCommandCaptureMode;
                _shellCommandCaptureMode = false; // Statement level → normal I/O

                // Build empty list for the variadic parameter
                auto* list = emitNilList(CoreVM::LiteralType::Void, "varargs.nil");
                std::vector<CoreVM::Value*> args = { list };

                generateFSharpCall(func, ident->name, args);
                _shellCommandCaptureMode = savedCaptureMode;
                return;
            }
        }
    }

    // At statement level, shell commands should run with normal I/O (not capture mode)
    auto const savedCaptureMode = _shellCommandCaptureMode;
    _shellCommandCaptureMode = false;
    auto* value = codegen(node.expr.get());
    _shellCommandCaptureMode = savedCaptureMode;

    // When a boolean literal is used as a statement (e.g., bare `true` or `false`),
    // set the shell exit code accordingly (true→0, false→1) to match shell semantics.
    if (auto const* boolLit = dynamic_cast<ast::BoolLiteralExpr const*>(node.expr.get()))
    {
        if (auto* callback = findCallback("setvar.exitstatus(I)V"))
        {
            auto const exitCode = boolLit->value ? 0 : 1;
            _builder.createCallFunction(_builder.getBuiltinFunction(*callback),
                                        { _builder.get(CoreVM::CoreNumber(exitCode)) },
                                        "setExitStatus");
        }
    }

    if (node.displayResult && value && !isUnitProducingExpr(node.expr.get()))
    {
        // Bare expression evaluation: auto-display the result
        auto const type = value->type();
        if (type == CoreVM::LiteralType::String)
        {
            // String: display directly via println
            auto* callback = findCallback("println(S)V");
            if (callback)
                _builder.createCallFunction(
                    _builder.getBuiltinFunction(*callback), { value }, "display.println");
        }
        else if (type == CoreVM::LiteralType::Boolean || type == CoreVM::LiteralType::Float)
        {
            // Convert to string, then println
            auto* strVal = convertToString(value, "display");
            if (strVal)
            {
                auto* callback = findCallback("println(S)V");
                if (callback)
                    _builder.createCallFunction(
                        _builder.getBuiltinFunction(*callback), { strVal }, "display.println");
            }
        }
        else
        {
            // Number/Object/Void: use display_result for runtime dispatch (table rendering etc.)
            auto* callback = findCallback("display_result(I)V");
            if (callback)
                _builder.createCallFunction(
                    _builder.getBuiltinFunction(*callback), { value }, "display.result");
        }
    }

    _result = value;
}

void IRGenerator::visit(ast::BinaryExpr const& node)
{
    TRACE_SCOPE("visit(BinaryExpr)");

    // Save this node's source location before codegenning children,
    // which will overwrite _builder.sourceLocation() with their own locations.
    auto const binaryExprLocation = _builder.sourceLocation();

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

    // Restore the BinaryExpr's own source location for error reporting
    _builder.setSourceLocation(binaryExprLocation);

    // Check for Option/Result operands that need unwrapping
    auto const checkWrappedType = [&](CoreVM::Value* operand, std::string_view side) -> bool {
        if (auto const typeId = getObjectTypeId(operand))
        {
            if (*typeId == CoreVM::BuiltinTypeId::Option || *typeId == CoreVM::BuiltinTypeId::Result)
            {
                // Build a readable type name — typeName() may return "object" for loaded values
                // since SSA chain walking doesn't traverse through loads, so fall back to
                // annotation-based naming when possible.
                auto tn = typeName(operand);
                if (tn == "object")
                {
                    auto const baseName = *typeId == CoreVM::BuiltinTypeId::Option ? "option" : "result";
                    if (auto const innerType = getInnerType(operand);
                        innerType && *innerType != CoreVM::LiteralType::Void)
                    {
                        auto const innerName = [&]() -> std::string_view {
                            switch (*innerType)
                            {
                                case CoreVM::LiteralType::Number: return "int";
                                case CoreVM::LiteralType::Float: return "float";
                                case CoreVM::LiteralType::String: return "string";
                                case CoreVM::LiteralType::Boolean: return "bool";
                                default: return "unknown";
                            }
                        }();
                        tn = std::format("{}<{}>", baseName, innerName);
                    }
                    else
                        tn = baseName;
                }
                auto suggestions = std::vector<std::string> { std::format(
                    "Use '?' to unwrap the {} operand, e.g.: expr?", side) };
                reportTypeErrorWithSuggestions(
                    std::move(suggestions),
                    "Cannot use '{}' value directly in binary operation; it must be unwrapped first",
                    tn);
                return true;
            }
        }
        return false;
    };
    if (checkWrappedType(left, "left") || checkWrappedType(right, "right"))
        return;

    // Size auto-unwrapping: extract .bytes for comparison/arithmetic
    auto const unwrapSize = [&](CoreVM::Value*& operand) {
        if (auto const typeId = getObjectTypeId(operand))
        {
            if (*typeId == CoreVM::BuiltinTypeId::Size)
                operand =
                    _builder.createObjGetSlot(operand, _builder.get(CoreVM::CoreNumber(0)), "size.bytes");
        }
    };
    unwrapSize(left);
    unwrapSize(right);

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

    // Check if this is a comparison operation
    bool const isComparison = node.op == ast::BinaryOp::Eq || node.op == ast::BinaryOp::Ne
                              || node.op == ast::BinaryOp::Lt || node.op == ast::BinaryOp::Le
                              || node.op == ast::BinaryOp::Gt || node.op == ast::BinaryOp::Ge;

    // When either operand is a String and we're doing comparison, use string comparison
    // instead of converting to numbers (S2N would crash on non-numeric strings like "db-main")
    bool const hasStringOperand =
        left->type() == CoreVM::LiteralType::String || right->type() == CoreVM::LiteralType::String;

    if (isComparison && hasStringOperand)
    {
        // Convert non-string operands to string for string comparison
        auto const ensureStringCompatible = [&](CoreVM::Value* val,
                                                std::string_view label) -> CoreVM::Value* {
            switch (val->type())
            {
                case CoreVM::LiteralType::Number: return _builder.createN2S(val, std::string(label) + ".n2s");
                case CoreVM::LiteralType::Float: return _builder.createF2S(val, std::string(label) + ".f2s");
                case CoreVM::LiteralType::Boolean: return convertToString(val, label);
                default: return val; // String, Void, Object are already string-compatible
            }
        };
        left = ensureStringCompatible(left, "lhs");
        right = ensureStringCompatible(right, "rhs");

        // For Void/Object types (e.g., from ObjGetSlot), the runtime value is already a string pointer,
        // so we can compare directly with SCmpXX which handles Void/Object operands.
        switch (node.op)
        {
            case ast::BinaryOp::Eq: _result = _builder.createSCmpEQ(left, right, "seq"); break;
            case ast::BinaryOp::Ne: _result = _builder.createSCmpNE(left, right, "sne"); break;
            case ast::BinaryOp::Lt: _result = _builder.createSCmpLT(left, right, "slt"); break;
            case ast::BinaryOp::Le: _result = _builder.createSCmpLE(left, right, "sle"); break;
            case ast::BinaryOp::Gt: _result = _builder.createSCmpGT(left, right, "sgt"); break;
            case ast::BinaryOp::Ge: _result = _builder.createSCmpGE(left, right, "sge"); break;
            default: break; // Logical ops not reached here
        }
    }
    else
    {
        // For arithmetic and numeric comparison, ensure operands are numbers
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
        if (funcIdent->name == "string")
        {
            _result = convertToString(value, "string");
            if (!_result)
            {
                reportTypeError("Cannot convert value to string in pipeline");
                return;
            }
            return;
        }
        if (funcIdent->name == "not")
        {
            _result = _builder.createBNot(toBool(value), "not");
            return;
        }
        if (funcIdent->name == "head")
        {
            auto* callback = findCallback("list_head(I)I");
            if (!callback)
            {
                reportTypeError("list_head builtin not found");
                return;
            }
            _result =
                _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { value }, "list_head");
            annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::Option);
            // Propagate list element type as inner object type of the Option
            if (auto elemTypeId = getListElementTypeId(value))
                annotateInnerObjectTypeId(_result, *elemTypeId);
            return;
        }
        if (funcIdent->name == "tail")
        {
            auto* callback = findCallback("list_tail(I)I");
            if (!callback)
            {
                reportTypeError("list_tail builtin not found");
                return;
            }
            _result =
                _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { value }, "list_tail");
            annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
            // Propagate list element type through tail
            if (auto elemTypeId = getListElementTypeId(value))
                annotateListElementTypeId(_result, *elemTypeId);
            if (auto elt = getListElementLiteralType(value))
                annotateListElementLiteralType(_result, *elt);
            return;
        }
        if (funcIdent->name == "which")
        {
            auto* callback = findCallback("which_find(S)I");
            if (!callback)
            {
                reportTypeError("which_find builtin not found");
                return;
            }
            _result =
                _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { value }, "which_find");
            annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::Option);
            annotateInnerType(_result, CoreVM::LiteralType::String);
            return;
        }
        if (funcIdent->name == "length")
        {
            auto* callback = findCallback("list_length(I)I");
            if (!callback)
            {
                reportTypeError("list_length builtin not found");
                return;
            }
            _result =
                _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { value }, "list_length");
            return;
        }
        if (funcIdent->name == "isEmpty")
        {
            auto* callback = findCallback("list_isEmpty(I)B");
            if (!callback)
            {
                reportTypeError("list_isEmpty builtin not found");
                return;
            }
            _result = _builder.createCallFunction(
                _builder.getBuiltinFunction(*callback), { value }, "list_isEmpty");
            return;
        }
        if (funcIdent->name == "last")
        {
            auto* callback = findCallback("list_last(I)I");
            if (!callback)
            {
                reportTypeError("list_last builtin not found");
                return;
            }
            _result =
                _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { value }, "list_last");
            annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::Option);
            if (auto elemTypeId = getListElementTypeId(value))
                annotateInnerObjectTypeId(_result, *elemTypeId);
            return;
        }
        // toText: convert object to its string representation (bypass table rendering)
        if (funcIdent->name == "toText")
        {
            auto* callback = findCallback("object_to_string(I)S");
            if (!callback)
            {
                reportTypeError("object_to_string builtin not found");
                return;
            }
            _result =
                _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { value }, "toText");
            return;
        }
        // Unary string builtins in pipeline
        if (funcIdent->name == "trim" || funcIdent->name == "toLower" || funcIdent->name == "toUpper")
        {
            auto const sigName = "string_" + std::string(funcIdent->name);
            auto* callback = findCallback(sigName + "(S)S");
            if (!callback)
            {
                reportTypeError("{} builtin not found", sigName);
                return;
            }
            _result = _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { value }, sigName);
            return;
        }
        // Locale-aware formatNumber in pipeline: number |> formatNumber
        if (funcIdent->name == "formatNumber")
        {
            auto* callback = findCallback("format_number(I)S");
            if (!callback)
            {
                reportTypeError("format_number builtin not found");
                return;
            }
            _result = _builder.createCallFunction(
                _builder.getBuiltinFunction(*callback), { value }, "formatNumber");
            return;
        }

        // Named function or stored lambda
        funcName = funcIdent->name;
        func = lookupFSharpFunction(funcName);
        if (!func)
        {
            // Fallback: check if the identifier is a function reference (HOF support)
            if (auto ref = lookupFSharpFunctionRef(funcName))
            {
                funcName = *ref;
                func = lookupFSharpFunction(funcName);
            }
            if (!func)
            {
                reportTypeError("Undefined function in pipeline: {}", std::string_view(funcIdent->name));
                return;
            }
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

        // Check for Option.{map,bind,defaultValue} in pipeline: value |> Option.map f
        if (!baseIdent)
        {
            if (auto const* fieldAccess = dynamic_cast<ast::FieldAccessExpr const*>(base))
            {
                auto const& method = fieldAccess->fieldName;
                if (auto const* modIdent =
                        dynamic_cast<ast::IdentifierExpr const*>(fieldAccess->object.get()))
                {
                    if (modIdent->name == "Option"
                        && (method == "map" || method == "bind" || method == "defaultValue"))
                    {
                        if (method == "map")
                        {
                            if (explicitArgExprs.size() != 1)
                            {
                                reportTypeError(
                                    "Option.map in pipeline requires exactly 1 function argument");
                                return;
                            }
                            generateOptionMapWithValue(explicitArgExprs[0], value);
                        }
                        else if (method == "bind")
                        {
                            if (explicitArgExprs.size() != 1)
                            {
                                reportTypeError(
                                    "Option.bind in pipeline requires exactly 1 function argument");
                                return;
                            }
                            generateOptionBindWithValue(explicitArgExprs[0], value);
                        }
                        else // defaultValue
                        {
                            if (explicitArgExprs.size() != 1)
                            {
                                reportTypeError(
                                    "Option.defaultValue in pipeline requires exactly 1 default argument");
                                return;
                            }
                            generateOptionDefaultValueWithValue(explicitArgExprs[0], value);
                        }
                        return;
                    }
                }
            }
            reportTypeError("Pipeline partial application requires a named function");
            return;
        }

        // Handle list builtins as partial applications in pipelines:
        // list |> nth 1 → list_nth(1, list)
        if (baseIdent->name == "nth")
        {
            if (explicitArgExprs.size() != 1)
            {
                reportTypeError("nth in pipeline requires exactly 1 index argument");
                return;
            }
            auto* indexArg = codegen(explicitArgExprs[0]);
            if (!indexArg)
            {
                reportTypeError("Failed to evaluate nth index argument");
                return;
            }
            auto* callback = findCallback("list_nth(II)I");
            if (!callback)
            {
                reportTypeError("list_nth builtin not found");
                return;
            }
            _result = _builder.createCallFunction(
                _builder.getBuiltinFunction(*callback), { indexArg, value }, "list_nth");
            annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::Option);
            if (auto elemTypeId = getListElementTypeId(value))
                annotateInnerObjectTypeId(_result, *elemTypeId);
            return;
        }

        // count |> replicate value → list_replicate(count, value)
        // Actually: list |> replicate N is not meaningful. replicate is: replicate count value
        // In pipeline: value |> replicate 3 → list_replicate(3, value)
        if (baseIdent->name == "replicate")
        {
            if (explicitArgExprs.size() != 1)
            {
                reportTypeError("replicate in pipeline requires exactly 1 count argument");
                return;
            }
            auto* countArg = codegen(explicitArgExprs[0]);
            if (!countArg)
            {
                reportTypeError("Failed to evaluate replicate count argument");
                return;
            }
            auto* callback = findCallback("list_replicate(II)I");
            if (!callback)
            {
                reportTypeError("list_replicate builtin not found");
                return;
            }
            _result = _builder.createCallFunction(
                _builder.getBuiltinFunction(*callback), { countArg, value }, "list_replicate");
            annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
            if (value->type() != CoreVM::LiteralType::Void)
                annotateListElementLiteralType(_result, value->type());
            return;
        }

        // Handle builtin string functions as partial applications in pipelines:
        // value |> contains "pattern"  →  string_contains(value, pattern)
        // value |> startsWith "prefix" →  string_startsWith(value, prefix)
        // value |> endsWith "suffix"   →  string_endsWith(value, suffix)
        if (baseIdent->name == "contains" || baseIdent->name == "startsWith" || baseIdent->name == "endsWith")
        {
            if (explicitArgExprs.size() != 1)
            {
                reportTypeError("{} in pipeline requires exactly 1 argument", baseIdent->name);
                return;
            }
            auto* patternArg = codegen(explicitArgExprs[0]);
            if (!patternArg)
            {
                reportTypeError("Failed to evaluate {} argument", baseIdent->name);
                return;
            }
            auto const sigName = "string_" + std::string(baseIdent->name);
            auto* callback = findCallback(sigName + "(SS)B");
            if (!callback)
            {
                reportTypeError("{} builtin not found", sigName);
                return;
            }
            _result = _builder.createCallFunction(
                _builder.getBuiltinFunction(*callback), { value, patternArg }, sigName);
            return;
        }

        // Handle formatNumber as partial application in pipeline:
        // number |> formatNumber ","  →  format_number(",", number)
        if (baseIdent->name == "formatNumber")
        {
            if (explicitArgExprs.size() != 1)
            {
                reportTypeError("formatNumber in pipeline requires exactly 1 argument (separator)");
                return;
            }
            auto* separatorArg = codegen(explicitArgExprs[0]);
            if (!separatorArg)
            {
                reportTypeError("Failed to evaluate formatNumber separator argument");
                return;
            }
            auto* callback = findCallback("format_number(SI)S");
            if (!callback)
            {
                reportTypeError("format_number builtin not found");
                return;
            }
            _result = _builder.createCallFunction(
                _builder.getBuiltinFunction(*callback), { separatorArg, value }, "format_number");
            return;
        }

        // Handle join as partial application in pipeline:
        // list |> join ":"  →  string_join(":", list)
        if (baseIdent->name == "join")
        {
            if (explicitArgExprs.size() != 1)
            {
                reportTypeError("join in pipeline requires exactly 1 argument");
                return;
            }
            auto* separatorArg = codegen(explicitArgExprs[0]);
            if (!separatorArg)
            {
                reportTypeError("Failed to evaluate join separator argument");
                return;
            }
            auto* callback = findCallback("string_join(SI)S");
            if (!callback)
            {
                reportTypeError("string_join builtin not found");
                return;
            }
            _result = _builder.createCallFunction(
                _builder.getBuiltinFunction(*callback), { separatorArg, value }, "string_join");
            return;
        }

        auto baseFuncName = baseIdent->name;
        auto const* baseFunc = lookupFSharpFunction(baseFuncName);
        if (!baseFunc)
        {
            // Fallback: check if the identifier is a function reference (HOF support)
            if (auto ref = lookupFSharpFunctionRef(baseFuncName))
            {
                baseFuncName = *ref;
                baseFunc = lookupFSharpFunction(baseFuncName);
            }
            if (!baseFunc)
            {
                reportTypeError("Undefined function in pipeline: {}", std::string_view(baseIdent->name));
                return;
            }
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
        funcName = baseFuncName;
        func = baseFunc;

        // Builtin HOFs: dispatch to IR generators
        if (!func->builtinHOF.empty())
        {
            generateBuiltinHOFCall(func, funcName, allArgs);
            return;
        }

        pushFSharpScope();

        // Re-bind captured variables
        for (auto const& [name, storage]: func->capturedBindings)
            bindFSharpVariable(name, storage);

        // Re-establish function references from captured function refs (HOF support)
        for (auto const& [varName, targetFunc]: func->capturedFunctionRefs)
            _currentFSharpScope->functionRefs[varName] = targetFunc;

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

            // Propagate type annotations through pipeline partial application parameters
            if (auto objTypeId = getObjectTypeId(allArgs[i]))
                annotateObjectTypeId(paramStorage, *objTypeId);
            if (auto innerObjTypeId = getInnerObjectTypeId(allArgs[i]))
                annotateInnerObjectTypeId(paramStorage, *innerObjTypeId);
            if (auto innerType = getInnerType(allArgs[i]))
                annotateInnerType(paramStorage, *innerType);
            if (auto elemTypeId = getListElementTypeId(allArgs[i]))
                annotateListElementTypeId(paramStorage, *elemTypeId);
            if (auto elt = getListElementLiteralType(allArgs[i]))
                annotateListElementLiteralType(paramStorage, *elt);

            // Track function references passed as arguments (HOF support)
            if (auto const* constStr = dynamic_cast<CoreVM::ConstantString*>(allArgs[i]))
            {
                if (lookupFSharpFunction(constStr->get()))
                    _currentFSharpScope->functionRefs[func->parameters[i]] = constStr->get();
            }
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

        CoreVM::AllocaInstr* resultStorage = nullptr;

        _activeRecursion = RecursiveCallContext {
            .functionName = funcName,
            .entryBlock = entryBlock,
            .paramAllocas = { paramAlloca },
            .resultStorage = nullptr,
            .exitBlock = exitBlock,
        };

        _builder.createBr(entryBlock);
        _builder.setInsertPoint(entryBlock);

        pushFSharpScope();
        for (auto const& [capName, capStorage]: func->capturedBindings)
            bindFSharpVariable(capName, capStorage);
        bindFSharpVariable(func->parameters[0], paramAlloca);

        // Propagate type annotations through recursive piped parameter
        if (auto objTypeId = getObjectTypeId(value))
            annotateObjectTypeId(paramAlloca, *objTypeId);
        if (auto innerObjTypeId = getInnerObjectTypeId(value))
            annotateInnerObjectTypeId(paramAlloca, *innerObjTypeId);
        if (auto innerType = getInnerType(value))
            annotateInnerType(paramAlloca, *innerType);
        if (auto elemTypeId = getListElementTypeId(value))
            annotateListElementTypeId(paramAlloca, *elemTypeId);
        if (auto elt = getListElementLiteralType(value))
            annotateListElementLiteralType(paramAlloca, *elt);

        auto* bodyResult = codegen(func->body);
        if (bodyResult)
        {
            resultStorage = createAllocaInEntryBlock(bodyResult->type(), "rec.result");
            _activeRecursion->resultStorage = resultStorage;
            _builder.createStore(resultStorage, bodyResult, "rec.store.result");
            _builder.createBr(exitBlock);
        }

        popFSharpScope();

        _builder.setInsertPoint(exitBlock);
        _result = _builder.createLoad(resultStorage, "rec.load.result");

        // Propagate objectTypeId annotation from body result to final result
        if (bodyResult)
        {
            if (auto objTypeId = getObjectTypeId(bodyResult))
                annotateObjectTypeId(_result, *objTypeId);
        }

        _activeRecursion.reset();
        return;
    }

    // Builtin HOFs with arity 1 (e.g., reverse): dispatch to IR generators
    if (!func->builtinHOF.empty())
    {
        generateBuiltinHOFCall(func, funcName, { value });
        return;
    }

    // Non-recursive: inline the function body with the piped value as argument
    pushFSharpScope();

    // Re-bind captured variables from the closure
    for (auto const& [capName, capStorage]: func->capturedBindings)
        bindFSharpVariable(capName, capStorage);

    // Re-establish function references from captured function refs (HOF support)
    for (auto const& [varName, targetFunc]: func->capturedFunctionRefs)
        _currentFSharpScope->functionRefs[varName] = targetFunc;

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

    // Propagate type annotations through piped parameter binding
    if (auto objTypeId = getObjectTypeId(value))
        annotateObjectTypeId(storage, *objTypeId);
    if (auto innerObjTypeId = getInnerObjectTypeId(value))
        annotateInnerObjectTypeId(storage, *innerObjTypeId);
    if (auto innerType = getInnerType(value))
        annotateInnerType(storage, *innerType);
    if (auto elemTypeId = getListElementTypeId(value))
        annotateListElementTypeId(storage, *elemTypeId);
    if (auto elt = getListElementLiteralType(value))
        annotateListElementLiteralType(storage, *elt);

    // Track function references passed as piped value (HOF support)
    if (auto const* constStr = dynamic_cast<CoreVM::ConstantString*>(value))
    {
        if (lookupFSharpFunction(constStr->get()))
            _currentFSharpScope->functionRefs[func->parameters[0]] = constStr->get();
    }

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
            // Print argument is an expression context → restore capture mode
            auto const savedCapture = _shellCommandCaptureMode;
            _shellCommandCaptureMode = true;
            generatePrintCall(argExprs[0], funcIdent->name == "println");
            _shellCommandCaptureMode = savedCapture;
            return;
        }

        // Check for standard library builtins (string_length, etc.)
        if (tryGenerateBuiltinCall(funcIdent->name, argExprs))
            return;
    }

    // Check for FieldAccessExpr patterns: Option.map f opt or opt.map f
    if (auto const* fieldAccess = dynamic_cast<ast::FieldAccessExpr const*>(current))
    {
        auto const& method = fieldAccess->fieldName;

        // Case 1: Module-qualified — Option.map f opt
        if (auto const* modIdent = dynamic_cast<ast::IdentifierExpr const*>(fieldAccess->object.get()))
        {
            if (modIdent->name == "Option" && tryGenerateOptionCall(method, argExprs))
                return;

            // DateTime.fromEpoch epoch → datetime_from_epoch(epoch)
            if (modIdent->name == "DateTime" && method == "fromEpoch" && argExprs.size() == 1)
            {
                auto* epochArg = codegen(argExprs[0]);
                if (!epochArg)
                {
                    reportTypeError("Failed to evaluate epoch argument for DateTime.fromEpoch");
                    return;
                }
                if (tryGenerateNativeCall("datetime_from_epoch", { epochArg }))
                {
                    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::DateTime);
                    return;
                }
            }

            // DateTime.now → datetime_now()
            if (modIdent->name == "DateTime" && method == "now" && argExprs.empty())
            {
                if (tryGenerateNativeCall("datetime_now", {}))
                {
                    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::DateTime);
                    return;
                }
            }

            // Size.fromBytes n → size_from_bytes(n)
            if (modIdent->name == "Size" && argExprs.size() == 1)
            {
                static std::unordered_map<std::string_view, std::string_view> const sizeMethods = {
                    { "fromBytes", "size_from_bytes" }, { "fromKB", "size_from_kb" },
                    { "fromMB", "size_from_mb" },       { "fromGB", "size_from_gb" },
                    { "fromTB", "size_from_tb" },
                };
                if (auto const it = sizeMethods.find(method); it != sizeMethods.end())
                {
                    auto* arg = codegen(argExprs[0]);
                    if (!arg)
                    {
                        reportTypeError("Failed to evaluate argument for Size.{}", method);
                        return;
                    }
                    if (tryGenerateNativeCall(std::string(it->second), { arg }))
                    {
                        annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::Size);
                        return;
                    }
                }
            }
        }

        // Case 2: Method-style — opt.map f
        if (method == "map" || method == "bind" || method == "defaultValue")
        {
            if (tryGenerateOptionMethodCall(method, fieldAccess->object.get(), argExprs))
                return;
        }
    }

    // Evaluate all arguments (arguments are NOT in tail position).
    // Arguments are expression contexts, so restore capture mode for shell commands.
    auto savedTailPos = _inTailPosition;
    _inTailPosition = false;
    auto const savedCaptureMode = _shellCommandCaptureMode;
    _shellCommandCaptureMode = true; // Arguments are expression contexts → capture mode
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
    _shellCommandCaptureMode = savedCaptureMode; // Restore for function body inlining
    _inTailPosition = savedTailPos;              // Restore: the call itself inherits parent's tail position

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
            // Fallback: check if the identifier is a function reference (HOF support)
            if (auto ref = lookupFSharpFunctionRef(funcName))
            {
                funcName = *ref;
                func = lookupFSharpFunction(funcName);
            }
            if (!func)
            {
                // Fallback: try native runtime function (e.g., add_mcp_server)
                if (tryGenerateNativeCall(funcIdent->name, args))
                    return;
                reportTypeError("Undefined function: {}", std::string_view(funcIdent->name));
                return;
            }
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

    // For variadic functions, collect extra arguments into a list for the variadic parameter
    if (func->hasVariadicParam && func->arity() > 0)
    {
        auto const fixedCount = func->arity() - 1; // Non-variadic params
        if (args.size() < fixedCount)
        {
            generatePartialApplication(func, funcName, args);
            return;
        }

        // Build a list from the variadic arguments (args[fixedCount..])
        // Start with Nil (empty list)
        CoreVM::Value* list = emitNilList(CoreVM::LiteralType::Void, "varargs.nil");

        // Build Cons cells in reverse order so first extra arg is at the head
        for (auto i = static_cast<int>(args.size()) - 1; i >= static_cast<int>(fixedCount); --i)
        {
            list = emitListCons(args[i], list, args[i]->type(), "varargs.cons");
        }

        // Annotate list element literal type if all variadic args share the same type
        auto const variadicArgs = std::span<CoreVM::Value* const>(args).subspan(fixedCount);
        if (auto commonType = determineCommonLiteralType(variadicArgs))
            annotateListElementLiteralType(list, *commonType);

        // Replace variadic args with the built list
        std::vector<CoreVM::Value*> finalArgs(args.begin(), args.begin() + fixedCount);
        finalArgs.push_back(list);
        args = std::move(finalArgs);
    }
    else
    {
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
    }

    // If the function was compiled as a separate IRFunction (UCALL/UTCALL), use that path
    // regardless of whether it's recursive or not.
    if (func->compiledFunction)
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

    // Builtin higher-order functions: dispatch to IR generators
    if (!func->builtinHOF.empty())
    {
        generateBuiltinHOFCall(func, funcName, args);
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
    partialFunc.builtinHOF = func->builtinHOF;
    partialFunc.resultKind = func->resultKind;
    partialFunc.capturedBindings = std::move(newCaptures);

    // Carry over existing captured function refs from the parent function
    partialFunc.capturedFunctionRefs = func->capturedFunctionRefs;

    // Track function references in supplied arguments (HOF support)
    for (size_t i = 0; i < args.size(); ++i)
    {
        if (auto const* constStr = dynamic_cast<CoreVM::ConstantString*>(args[i]))
        {
            if (lookupFSharpFunction(constStr->get()))
                partialFunc.capturedFunctionRefs[func->parameters[i]] = constStr->get();
        }
    }

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

    // Create parameter allocas and result storage in the function entry block
    std::vector<CoreVM::AllocaInstr*> paramAllocas;
    for (size_t i = 0; i < func->parameters.size(); ++i)
    {
        auto* alloca = createAllocaInEntryBlock(args[i]->type(), "rec.param." + func->parameters[i]);
        _builder.createStore(alloca, args[i], "rec.param.init");
        paramAllocas.push_back(alloca);

        // Propagate type annotations from initial args to param allocas
        if (auto objTypeId = getObjectTypeId(args[i]))
            annotateObjectTypeId(alloca, *objTypeId);
        if (auto innerObjTypeId = getInnerObjectTypeId(args[i]))
            annotateInnerObjectTypeId(alloca, *innerObjTypeId);
        if (auto innerType = getInnerType(args[i]))
            annotateInnerType(alloca, *innerType);
        if (auto elemTypeId = getListElementTypeId(args[i]))
            annotateListElementTypeId(alloca, *elemTypeId);
        if (auto elt = getListElementLiteralType(args[i]))
            annotateListElementLiteralType(alloca, *elt);
    }

    CoreVM::AllocaInstr* resultStorage = nullptr;

    // Set up the recursion context
    _activeRecursion = RecursiveCallContext {
        .functionName = funcName,
        .entryBlock = entryBlock,
        .paramAllocas = paramAllocas,
        .resultStorage = nullptr,
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
        resultStorage = createAllocaInEntryBlock(bodyResult->type(), "rec.result");
        _activeRecursion->resultStorage = resultStorage;
        _builder.createStore(resultStorage, bodyResult, "rec.store.result");
        _builder.createBr(exitBlock);
    }

    popFSharpScope();

    // Continue from the exit block, load the result
    _builder.setInsertPoint(exitBlock);
    _result = _builder.createLoad(resultStorage, "rec.load.result");

    // Propagate objectTypeId annotation from body result to final result
    if (bodyResult)
    {
        if (auto objTypeId = getObjectTypeId(bodyResult))
            annotateObjectTypeId(_result, *objTypeId);
    }

    // Clear the recursion context
    _activeRecursion.reset();
}

void IRGenerator::generateFSharpCall(FSharpFunction const* func,
                                     std::string const& funcName,
                                     std::vector<CoreVM::Value*> const& args)
{
    // If this function was compiled as a separate IRFunction, emit a function call instruction
    if (func->compiledFunction)
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
        // When inside a function compilation (recursive call), load captures from the
        // function's own scope variables (not from the outer scope's capturedBindings).
        std::vector<CoreVM::Value*> fullArgs;
        fullArgs.reserve(func->captureOrder.size() + args.size());
        for (auto const& capName: func->captureOrder)
        {
            CoreVM::Value* capStorage = nullptr;
            if (_compilingFunction)
            {
                // Inside a function: look up capture in the current scope
                capStorage = lookupFSharpVariable(capName);
            }
            if (!capStorage)
            {
                // Outside function or not found in scope: use outer capturedBindings
                capStorage = func->capturedBindings.at(capName);
            }
            fullArgs.push_back(_builder.createLoad(capStorage, "cap." + capName));
        }
        fullArgs.insert(fullArgs.end(), args.begin(), args.end());

        // Emit tail call (UTCALL) when in tail position inside a function compilation,
        // otherwise emit regular function call (UCALL).
        if (_inTailPosition && _compilingFunction)
        {
            _builder.createTailCall(func->compiledFunction, fullArgs, funcName + ".tailcall");

            // Create unreachable continuation block (code after tail call is dead)
            auto* unreachable = _builder.createBlock("tailcall.unreachable");
            _builder.setInsertPoint(unreachable);

            _result = nullptr; // Tail call doesn't produce a value in the current function
        }
        else
        {
            _result = _builder.createFunctionCall(
                func->compiledFunction, fullArgs, funcName + ".call", func->compiledReturnType);
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

    // Re-establish function references from captured function refs (HOF support)
    for (auto const& [varName, targetFunc]: func->capturedFunctionRefs)
        _currentFSharpScope->functionRefs[varName] = targetFunc;

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

        // Propagate type annotations through parameter bindings
        if (auto objTypeId = getObjectTypeId(args[i]))
            annotateObjectTypeId(storage, *objTypeId);
        if (auto innerObjTypeId = getInnerObjectTypeId(args[i]))
            annotateInnerObjectTypeId(storage, *innerObjTypeId);
        if (auto innerType = getInnerType(args[i]))
            annotateInnerType(storage, *innerType);
        if (auto elemTypeId = getListElementTypeId(args[i]))
            annotateListElementTypeId(storage, *elemTypeId);
        if (auto elt = getListElementLiteralType(args[i]))
            annotateListElementLiteralType(storage, *elt);

        // Track function references passed as arguments (HOF support)
        if (auto const* constStr = dynamic_cast<CoreVM::ConstantString*>(args[i]))
        {
            if (lookupFSharpFunction(constStr->get()))
                _currentFSharpScope->functionRefs[func->parameters[i]] = constStr->get();
        }
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

void IRGenerator::compileFunctionBody(std::string const& name, FSharpFunction& func)
{
    // Builtin HOFs have no AST body — they use custom IR generators, not function compilation.
    if (!func.builtinHOF.empty())
        return;

    // Check if all parameters have types (either annotated or inferred).
    // Without types, parameters would get Void type, causing wrong runtime behavior.
    // Type inference should have filled in missing annotations; fall back to AST inlining if not.
    auto const allParamsTyped =
        !func.parameters.empty() && func.parameterTypes.size() == func.parameters.size()
        && std::ranges::all_of(func.parameterTypes, [](auto const& t) { return t.has_value(); });
    if (!func.parameters.empty() && !allParamsTyped)
        return;

    // Functions with function-typed parameters must use AST inlining so that
    // functionRefs tracking can resolve higher-order function calls.
    if (std::ranges::any_of(func.parameterTypes, [](auto const& t) { return t && (*t)->isFunction(); }))
        return;

    // Functions whose body is a lambda cannot be compiled as separate IRFunctions because the inner
    // lambda's captures reference function-local allocas that are destroyed after URET.
    // Unwrap ParenExpr wrappers to detect parenthesized lambdas like `fun x -> (fun y -> ...)`.
    {
        auto const* actualBody = func.body;
        while (auto const* paren = dynamic_cast<ast::ParenExpr const*>(actualBody))
            actualBody = paren->inner.get();
        if (dynamic_cast<ast::LambdaExpr const*>(actualBody) != nullptr)
            return;
    }

    // Compute deterministic capture ordering (sorted alphabetically)
    func.captureOrder.clear();
    for (auto const& [capName, _]: func.capturedBindings)
        func.captureOrder.push_back(capName);
    std::ranges::sort(func.captureOrder);

    // Save current state so we can revert if compilation fails
    auto* savedFunction = _builder.function();
    auto* savedInsertPoint = _builder.getInsertPoint();
    auto savedHasErrors = _hasErrors;
    auto* bufferedReport = dynamic_cast<CoreVM::diagnostics::BufferedReport*>(&_report);
    auto const savedReportSize = bufferedReport ? bufferedReport->size() : size_t { 0 };

    // Create a new IRFunction for this function
    // Parameter count includes captured variables (prepended) + explicit parameters
    auto* irFunction = _builder.program()->createFunction("fsharp." + name);
    irFunction->setParameterCount(func.captureOrder.size() + func.parameters.size());

    // Switch builder to the new function
    _builder.setFunction(irFunction);
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

    // Pre-set compiledFunction so that recursive references during body codegen
    // emit FunctionCallInstr/TailCallInstr instead of trying to inline (which would infinite-loop).
    func.compiledFunction = irFunction;

    // Track tail position and compiling function for UCALL/UTCALL decisions
    auto savedTailPosition = _inTailPosition;
    auto* savedCompilingFunction = _compilingFunction;
    _inTailPosition = true;
    _compilingFunction = irFunction;

    // Codegen the function body
    auto* bodyResult = codegen(func.body);

    // Restore tail position and compiling function
    _inTailPosition = savedTailPosition;
    _compilingFunction = savedCompilingFunction;

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
        {
            // Void-returning builtin calls (e.g., add_mcp_server, export) don't push a value
            // onto the TargetCodeGenerator's stack. If the function body ends with such a call,
            // the FunctionRetInstr would reference an unloadable value, crashing emitLoad().
            // Replace with unit (0) since the function semantically returns unit.
            if (auto* callInstr = dynamic_cast<CoreVM::CallInstr*>(bodyResult);
                callInstr && callInstr->callee()->signature().returnType() == CoreVM::LiteralType::Void)
            {
                bodyResult = _builder.get(CoreVM::CoreNumber(0));
            }
            _builder.createFunctionRet(bodyResult, "ret");
        }
    }

    popFSharpScope();

    // Helper to revert compilation state when falling back to AST inlining
    auto const revertToInlining = [&] {
        func.compiledFunction = nullptr; // Reset so fallback path (AST inlining) is used
        _hasErrors = savedHasErrors;
        if (bufferedReport)
            bufferedReport->truncate(savedReportSize);
        _builder.program()->removeFunction(irFunction);
        _builder.setFunction(savedFunction);
        _builder.setInsertPoint(savedInsertPoint);
    };

    // If compilation produced errors, fall back to AST inlining
    if (_hasErrors && !savedHasErrors)
    {
        revertToInlining();
        return;
    }

    // If bodyResult is null and no errors, all code paths end with tail calls (valid).
    // If bodyResult is null and we're not in a recursive/function context, it's unexpected.
    if (!bodyResult && !func.isRecursive)
    {
        revertToInlining();
        return;
    }

    // Store the return type (if known from a non-tail-call path)
    if (bodyResult)
        func.compiledReturnType = bodyResult->type();

    // Restore builder state
    _builder.setFunction(savedFunction);
    _builder.setInsertPoint(savedInsertPoint);
}

void IRGenerator::visit(ast::IdentifierExpr const& node)
{
    TRACE_SCOPE("visit(IdentifierExpr)");

    // Check if identifier is a user-defined property (getter invocation)
    if (auto it = _fsharpProperties.find(node.name); it != _fsharpProperties.end())
    {
        if (!it->second.getter)
        {
            reportTypeError("Property '{}' is write-only (has no getter)", std::string_view(node.name));
            return;
        }
        pushFSharpScope();
        codegen(it->second.getter->body.get());
        popFSharpScope();
        return;
    }

    // Check if identifier is a builtin property (getter callback)
    if (auto const* prop = _runtime.findProperty(node.name))
    {
        if (!prop->hasGetter())
        {
            reportTypeError("Property '{}' is write-only (has no getter)", std::string_view(node.name));
            return;
        }
        // Emit call to the zero-arg getter callback: name()T
        auto const getterSig = node.name + "()" + CoreVM::signatureType(prop->type());
        if (auto* cb = findCallback(getterSig))
        {
            _result = _builder.createCallFunction(_builder.getBuiltinFunction(*cb), {}, node.name + ".get");
            return;
        }
    }

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
        // HOF support: check if identifier maps to a function reference
        if (auto ref = lookupFSharpFunctionRef(node.name))
        {
            _result = _builder.get(*ref);
            return;
        }
        // Builtin functions used as function references (e.g., each print)
        if (node.name == "print" || node.name == "println")
        {
            _result = _builder.get(node.name);
            return;
        }
        // Zero-argument builtins (e.g., ps, ls, rand) invoked as bare identifiers in F# context
        if ((node.name == "ps" || node.name == "ls" || node.name == "jobs" || node.name == "rand")
            && tryGenerateBuiltinCall(node.name, {}))
            return;
        reportTypeError("Undefined F# identifier: {}", std::string_view(node.name));
        return;
    }

    // HOF support: if this variable holds a function reference, return the constant
    // function name so that downstream let bindings and argument passing can detect it.
    if (auto ref = lookupFSharpFunctionRef(node.name))
    {
        _result = _builder.get(*ref);
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

    // Propagate list element type annotation through variable loads
    if (auto elemTypeId = getListElementTypeId(storage))
        annotateListElementTypeId(_result, *elemTypeId);
    if (auto elt = getListElementLiteralType(storage))
        annotateListElementLiteralType(_result, *elt);
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

void IRGenerator::visit(ast::SizeLiteralExpr const& node)
{
    if (tryGenerateNativeCall("size_from_bytes", { _builder.get(CoreVM::CoreNumber(node.bytes)) }))
        annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::Size);
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

    // Preserve function reference info through lambda captures (HOF support)
    for (auto const& [capName, _]: func.capturedBindings)
    {
        if (auto ref = lookupFSharpFunctionRef(capName))
            func.capturedFunctionRefs[capName] = *ref;
    }

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
            // For constructor patterns with payload on Option/Result, use the inner literal
            // type for the binding alloca so that extracted values retain their correct type
            // (e.g., String from Option<string> instead of Number from callback return type).
            auto allocaType = scrutinee->type();
            if (auto innerLiteralType = getInnerType(scrutinee))
            {
                if (auto* ctorPat = dynamic_cast<pattern::ConstructorPattern const*>(arm.pattern.get()))
                {
                    if (ctorPat->payload.has_value())
                        allocaType = *innerLiteralType;
                }
            }
            // Use createAllocaInEntryBlock to ensure proper stack tracking
            auto* storage = createAllocaInEntryBlock(allocaType, binding.name + ".arm" + std::to_string(i));
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

        // Set record field offsets if the scrutinee is a known record type
        if (auto objTypeId = getObjectTypeId(scrutinee))
        {
            for (auto const& [typeName, recInfo]: _recordTypes)
            {
                if (recInfo.typeId == *objTypeId)
                {
                    std::unordered_map<std::string, uint8_t> fieldOffsets;
                    for (auto const& field: recInfo.fields)
                        fieldOffsets[field.name] = field.offset;
                    patternIRGenerator.setRecordFieldOffsets(std::move(fieldOffsets));
                    break;
                }
            }
        }

        // Set constructor lookup for user-defined discriminated union patterns
        if (!_constructorRegistry.empty())
        {
            std::unordered_map<std::string, PatternIRGenerator::ConstructorMeta> ctorLookup;
            for (auto const& [ctorName, ctorInfo]: _constructorRegistry)
                ctorLookup[ctorName] = { ctorInfo.typeId, ctorInfo.tag, ctorInfo.payloadSlots };
            patternIRGenerator.setConstructorLookup(std::move(ctorLookup));
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
        bool const isRecordPattern =
            dynamic_cast<pattern::RecordPattern const*>(arm.pattern.get()) != nullptr;

        // User-defined constructor patterns with multi-slot payloads have their bindings
        // stored into pre-allocated allocas by PatternIRGenerator (same as tuple patterns).
        bool isUserDefinedCtorWithMultiSlot = false;
        if (auto const* ctorPat = dynamic_cast<pattern::ConstructorPattern const*>(arm.pattern.get()))
        {
            if (auto const* ctorInfo = lookupConstructor(ctorPat->name))
                isUserDefinedCtorWithMultiSlot = ctorInfo->payloadSlots > 1;
        }

        if (isTuplePattern || isConsPattern || isListPattern || isRecordPattern
            || isUserDefinedCtorWithMultiSlot)
        {
            // Tuple/Cons/List/Record patterns: values were already stored into allocas by
            // PatternIRGenerator. Just register the allocas as variable bindings.
            RecordTypeInfo const* recTypeInfo = nullptr;
            if (isRecordPattern)
            {
                if (auto objTypeId = getObjectTypeId(scrutinee))
                {
                    for (auto const& [typeName, recInfo]: _recordTypes)
                    {
                        if (recInfo.typeId == *objTypeId)
                        {
                            recTypeInfo = &recInfo;
                            break;
                        }
                    }
                }
            }

            for (auto const& [name, storage]: preAllocatedBindings)
            {
                bindFSharpVariable(name, storage);

                // For record patterns, annotate bindings with field types
                if (recTypeInfo)
                {
                    if (auto it = recTypeInfo->fieldTypes.find(name); it != recTypeInfo->fieldTypes.end())
                        annotateInnerType(storage, it->second);
                }
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
            if (_activeRecursion || _activeMutualRecursion || _compilingFunction)
                continue;

            reportTypeError("Failed to evaluate match arm body");
            return;
        }

        // Create result storage lazily from the first arm's actual result type
        if (!resultStorage)
            resultStorage = createAllocaInEntryBlock(bodyResult->type(), "match.result");

        // Store the result
        _builder.createStore(resultStorage, bodyResult, "match.result.store");

        // Propagate type annotations through match result storage
        if (auto innerType = getInnerType(bodyResult))
            annotateInnerType(resultStorage, *innerType);
        if (auto objTypeId = getObjectTypeId(bodyResult))
            annotateObjectTypeId(resultStorage, *objTypeId);

        // Branch to merge block
        _builder.createBr(mergeBlock);
    }

    // Set insert point to merge block and load the result
    _builder.setInsertPoint(mergeBlock);
    if (resultStorage)
    {
        _result = _builder.createLoad(resultStorage, "match.result.load");

        // Propagate type annotations from storage to result
        if (auto innerType = getInnerType(resultStorage))
            annotateInnerType(_result, *innerType);
        if (auto objTypeId = getObjectTypeId(resultStorage))
            annotateObjectTypeId(_result, *objTypeId);
    }
    else
        _result = nullptr; // All arms are tail calls — merge is unreachable
}

void IRGenerator::visit(ast::ListExpr const& node)
{
    TRACE_SCOPE("visit(ListExpr)");

    // Save this node's source location before codegenning children,
    // which will overwrite _builder.sourceLocation() with their own locations.
    auto const listExprLocation = _builder.sourceLocation();

    // Empty list: just allocate a Nil (tag=0)
    if (node.elements.empty())
    {
        CoreVM::Value* obj = emitNilList(CoreVM::LiteralType::Void, "list.nil");
        _result = obj;
        annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
        return;
    }

    // Codegen all elements left-to-right, storing each to an alloca immediately.
    // Elements like `env "X"` create control-flow blocks; storing before the next
    // element's codegen prevents block-boundary cleanup from discarding values.
    std::vector<CoreVM::Value*> elemValues;
    std::vector<CoreVM::AllocaInstr*> elemAllocas;
    elemValues.reserve(node.elements.size());
    elemAllocas.reserve(node.elements.size());
    for (size_t i = 0; i < node.elements.size(); ++i)
    {
        auto* val = codegen(node.elements[i].get());
        if (!val)
        {
            reportTypeError("Failed to evaluate list element");
            return;
        }
        elemValues.push_back(val);
        auto* alloca = createAllocaInEntryBlock(val->type(), "list.elem." + std::to_string(i));
        _builder.createStore(alloca, val);
        elemAllocas.push_back(alloca);
    }

    // Check element type homogeneity
    _builder.setSourceLocation(listExprLocation);
    for (size_t i = 1; i < elemValues.size(); ++i)
    {
        if (!typesCompatible(elemValues[0], elemValues[i]))
        {
            auto suggestions = std::vector<std::string> {};
            if (auto const tid = getObjectTypeId(elemValues[i]);
                tid && (*tid == CoreVM::BuiltinTypeId::Option || *tid == CoreVM::BuiltinTypeId::Result))
            {
                suggestions.push_back(std::format("Use '?' to unwrap element {}, e.g.: (expr)?", i));
            }
            else
            {
                suggestions.push_back("Ensure all list elements have the same type");
            }
            reportTypeErrorWithSuggestions(
                std::move(suggestions),
                "List elements must have the same type: element 0 is '{}' but element {} is '{}'",
                typeName(elemValues[0]),
                i,
                typeName(elemValues[i]));
            return;
        }
    }

    // Build the list right-to-left: start with Nil, then prepend elements
    auto commonElemType = determineCommonLiteralType(elemValues);
    CoreVM::Value* acc = emitNilList(commonElemType.value_or(CoreVM::LiteralType::Void), "list.nil");

    for (int i = static_cast<int>(elemValues.size()) - 1; i >= 0; --i)
    {
        // Store accumulator so it's available after ObjAlloc
        auto* accStorage =
            createAllocaInEntryBlock(CoreVM::LiteralType::Object, "list.acc." + std::to_string(i));
        _builder.createStore(accStorage, acc);

        // Create a Cons cell: tag=1, slot[0]=head, slot[1]=tail
        auto* head = _builder.createLoad(elemAllocas[i], "list.head." + std::to_string(i));
        auto* tail = _builder.createLoad(accStorage, "list.tail." + std::to_string(i));
        CoreVM::Value* cons = emitListCons(
            head, tail, commonElemType.value_or(CoreVM::LiteralType::Void), "list.cons." + std::to_string(i));

        acc = cons;
    }

    _result = acc;
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);

    // Annotate list element literal type if all elements share the same type
    if (auto commonType = determineCommonLiteralType(elemValues))
        annotateListElementLiteralType(_result, *commonType);
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
    auto* head = _builder.createLoad(headStorage, "cons.head");
    auto* tail = _builder.createLoad(tailStorage, "cons.tail");
    CoreVM::Value* obj = emitListCons(head, tail, headVal->type(), "cons");

    _result = obj;
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);

    // Annotate list element literal type from the head value
    if (headVal->type() != CoreVM::LiteralType::Void)
        annotateListElementLiteralType(_result, headVal->type());
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
    if (auto elt = getListElementLiteralType(leftVal))
        annotateListElementLiteralType(_result, *elt);
}

void IRGenerator::visit(ast::ListRangeExpr const& node)
{
    TRACE_SCOPE("visit(ListRangeExpr)");

    // Detect character range: ['a'..'z'] where start and end are single-character string literals
    if (auto const* startLit = dynamic_cast<ast::LiteralExpr const*>(node.start.get()))
    {
        if (auto const* endLit = dynamic_cast<ast::LiteralExpr const*>(node.end.get()))
        {
            if (startLit->value.size() == 1 && endLit->value.size() == 1 && !node.step)
            {
                auto startOrd =
                    _builder.get(CoreVM::CoreNumber(static_cast<unsigned char>(startLit->value[0])));
                auto endOrd = _builder.get(CoreVM::CoreNumber(static_cast<unsigned char>(endLit->value[0])));
                auto* callback = findCallback("list_char_range(II)I");
                if (!callback)
                {
                    reportTypeError("list_char_range builtin not found");
                    return;
                }
                _result = _builder.createCallFunction(
                    _builder.getBuiltinFunction(*callback), { startOrd, endOrd }, "list_char_range");
                annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
                annotateListElementLiteralType(_result, CoreVM::LiteralType::String);
                return;
            }
        }
    }

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
    auto* nil = emitNilList(CoreVM::LiteralType::Number, "range.nil");
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
    auto* headReload = _builder.createLoad(iTemp, "range.head.reload");
    auto* tailReload = _builder.createLoad(accTemp, "range.tail.reload");
    auto* cons = emitListCons(headReload, tailReload, CoreVM::LiteralType::Number, "range.cons");

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
    annotateListElementLiteralType(_result, CoreVM::LiteralType::Number);
}

void IRGenerator::visit(ast::ListComprehensionExpr const& node)
{
    TRACE_SCOPE("visit(ListComprehensionExpr)");

    auto* tag1 = _builder.get(CoreVM::CoreNumber(1)); // Cons

    // Shared accumulator for all nesting levels (produces flat list)
    auto* accStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "comp.acc");
    auto* nil = emitNilList(CoreVM::LiteralType::Void, "comp.nil");
    _builder.createStore(accStorage, nil);

    // Create revInitBlock early (needed as doneBlock pointer for outermost condBr).
    // After emitComprehensionLevel creates all forward blocks, we move revInitBlock
    // to the end of the block list to preserve execution order for TargetCodeGenerator.
    auto* revInitBlock = _builder.createBlock("comp.rev.init");

    // Phase 1: Forward iteration building reversed accumulator (recursive for nesting).
    // Source codegen runs before block creation at each level, ensuring source-generated
    // blocks (e.g., ListRangeExpr) appear before the comprehension blocks they feed.
    emitComprehensionLevel(node, accStorage, revInitBlock, 0);

    // Move revInitBlock to end of block list (after all forward-phase blocks).
    // emitComprehensionLevel created blocks after revInitBlock; move it past them.
    auto* currentFn = _builder.function();
    CoreVM::BasicBlock* lastBlock = nullptr;
    for (auto* bb: currentFn->basicBlocks())
        lastBlock = bb;
    if (lastBlock && lastBlock != revInitBlock)
        currentFn->moveAfter(revInitBlock, lastBlock);

    // Create reverse-phase blocks (they now appear after all forward blocks)
    auto* revCondBlock = _builder.createBlock("comp.rev.cond");
    auto* revBodyBlock = _builder.createBlock("comp.rev.body");
    auto* endBlock = _builder.createBlock("comp.end");

    // Phase 2: Reverse — initialize reverse cursor and output accumulator
    auto* revSrcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "comp.rev.src");
    auto* revAccStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "comp.rev.acc");

    _builder.setInsertPoint(revInitBlock);
    auto* revSrcInit = _builder.createLoad(accStorage, "comp.rev.src.init");
    _builder.createStore(revSrcStorage, revSrcInit);
    auto* revNil = emitNilList(CoreVM::LiteralType::Void, "comp.rev.nil");
    _builder.createStore(revAccStorage, revNil);
    _builder.createBr(revCondBlock);

    // Phase 2: Condition — check if reversed source is Cons
    _builder.setInsertPoint(revCondBlock);
    auto* revSrcLoad = _builder.createLoad(revSrcStorage, "comp.rev.src.load");
    auto* revSrcTag = _builder.createObjGetTag(revSrcLoad, "comp.rev.src.tag");
    auto* revIsCons = _builder.createNCmpEQ(revSrcTag, tag1, "comp.rev.is_cons");
    _builder.createCondBr(revIsCons, revBodyBlock, endBlock);

    // Phase 2: Body — extract head, advance cursor, cons onto output
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0)); // head
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1)); // tail

    _builder.setInsertPoint(revBodyBlock);
    auto* revSrcForHead = _builder.createLoad(revSrcStorage, "comp.rev.src.for_head");
    auto* revHead = _builder.createObjGetSlot(revSrcForHead, slot0, "comp.rev.head");
    auto* revSrcForTail = _builder.createLoad(revSrcStorage, "comp.rev.src.for_tail");
    auto* revTail = _builder.createObjGetSlot(revSrcForTail, slot1, "comp.rev.tail");
    _builder.createStore(revSrcStorage, revTail);

    // Store head and accumulator to survive ObjAlloc
    auto* revElemTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Void, "comp.rev.elem.tmp");
    _builder.createStore(revElemTmp, revHead);
    auto* revAccTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "comp.rev.acc.tmp");
    auto* revAccForCons = _builder.createLoad(revAccStorage, "comp.rev.acc.for_cons");
    _builder.createStore(revAccTmp, revAccForCons);

    auto* revElemReload = _builder.createLoad(revElemTmp, "comp.rev.elem.reload");
    auto* revAccReload = _builder.createLoad(revAccTmp, "comp.rev.acc.reload");
    auto* revCons = emitListCons(revElemReload, revAccReload, CoreVM::LiteralType::Void, "comp.rev.cons");

    _builder.createStore(revAccStorage, revCons);
    _builder.createBr(revCondBlock);

    // End block: result is the correctly ordered list
    _builder.setInsertPoint(endBlock);
    _result = _builder.createLoad(revAccStorage, "comp.result");
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
}

void IRGenerator::emitComprehensionLevel(ast::ListComprehensionExpr const& node,
                                         CoreVM::AllocaInstr* accStorage,
                                         CoreVM::BasicBlock* doneBlock,
                                         int level)
{
    auto const prefix = std::format("comp{}", level);
    auto* tag1 = _builder.get(CoreVM::CoreNumber(1)); // Cons
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0));
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1));

    // Evaluate source collection FIRST — source codegen may create blocks (e.g., ListRangeExpr)
    // that must appear before this level's blocks in the block list.
    auto* sourceVal = codegen(node.source.get());
    if (!sourceVal)
    {
        reportTypeError("Failed to evaluate list comprehension source");
        return;
    }

    auto* srcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, std::format("{}.src", prefix));
    _builder.createStore(srcStorage, sourceVal);

    auto const compElemType = getListElementLiteralType(sourceVal).value_or(CoreVM::LiteralType::Number);
    auto* elemAlloca = createAllocaInEntryBlock(compElemType, std::format("{}.elem", prefix));

    // Determine whether body is a nested comprehension
    auto* innerComp = dynamic_cast<ast::ListComprehensionExpr const*>(node.body.get());

    // Create blocks AFTER source codegen to ensure correct block ordering
    auto* condBlock = _builder.createBlock(std::format("{}.cond", prefix));
    auto* bodyEntryBlock = _builder.createBlock(std::format("{}.body.entry", prefix));
    auto* filterBlock = node.filter ? _builder.createBlock(std::format("{}.filter", prefix)) : nullptr;
    auto* actionBlock = _builder.createBlock(std::format("{}.{}", prefix, innerComp ? "action" : "cons"));

    _builder.createBr(condBlock);

    // Condition block — check if source list is Cons (tag == 1)
    _builder.setInsertPoint(condBlock);
    auto* srcLoad = _builder.createLoad(srcStorage, std::format("{}.src.load", prefix));
    auto* srcTag = _builder.createObjGetTag(srcLoad, std::format("{}.src.tag", prefix));
    auto* isCons = _builder.createNCmpEQ(srcTag, tag1, std::format("{}.is_cons", prefix));
    _builder.createCondBr(isCons, bodyEntryBlock, doneBlock);

    // Body entry — extract head and advance tail (separate loads per MEMORY.md)
    _builder.setInsertPoint(bodyEntryBlock);
    auto* srcForHead = _builder.createLoad(srcStorage, std::format("{}.src.for_head", prefix));
    auto* head = _builder.createObjGetSlot(srcForHead, slot0, std::format("{}.head", prefix));
    _builder.createStore(elemAlloca, head);

    auto* srcForTail = _builder.createLoad(srcStorage, std::format("{}.src.for_tail", prefix));
    auto* tail = _builder.createObjGetSlot(srcForTail, slot1, std::format("{}.tail", prefix));
    _builder.createStore(srcStorage, tail);

    _builder.createBr(filterBlock ? filterBlock : actionBlock);

    // Optional filter block
    if (filterBlock)
    {
        _builder.setInsertPoint(filterBlock);
        pushFSharpScope();
        bindFSharpVariable(node.variable, elemAlloca);
        auto* filterVal = codegen(node.filter.get());
        popFSharpScope();
        if (!filterVal)
        {
            reportTypeError("Failed to evaluate list comprehension filter");
            return;
        }
        _builder.createCondBr(filterVal, actionBlock, condBlock);
    }

    // Action block — either recurse into nested comprehension or evaluate leaf body
    _builder.setInsertPoint(actionBlock);

    if (innerComp)
    {
        // Nested: bind outer variable, recurse with shared accumulator.
        // When inner source exhausts, inner doneBlock → this level's condBlock.
        pushFSharpScope();
        bindFSharpVariable(node.variable, elemAlloca);
        emitComprehensionLevel(*innerComp, accStorage, condBlock, level + 1);
        popFSharpScope();
    }
    else
    {
        // Leaf: evaluate body, cons onto shared accumulator
        pushFSharpScope();
        bindFSharpVariable(node.variable, elemAlloca);
        auto* bodyVal = codegen(node.body.get());
        popFSharpScope();
        if (!bodyVal)
        {
            reportTypeError("Failed to evaluate list comprehension body");
            return;
        }

        // Store body result and accumulator in temp allocas to survive ObjAlloc
        auto* bodyTmp = createAllocaInEntryBlock(bodyVal->type(), std::format("{}.body.tmp", prefix));
        _builder.createStore(bodyTmp, bodyVal);
        auto* accTmp =
            createAllocaInEntryBlock(CoreVM::LiteralType::Object, std::format("{}.acc.tmp", prefix));
        auto* accForCons = _builder.createLoad(accStorage, std::format("{}.acc.for_cons", prefix));
        _builder.createStore(accTmp, accForCons);

        auto* bodyReload = _builder.createLoad(bodyTmp, std::format("{}.body.reload", prefix));
        auto* accReload = _builder.createLoad(accTmp, std::format("{}.acc.reload", prefix));
        auto* cons = emitListCons(bodyReload, accReload, bodyReload->type(), std::format("{}.cons", prefix));

        _builder.createStore(accStorage, cons);
        _builder.createBr(condBlock);
    }
}

void IRGenerator::visit(ast::ShellCommandExpr const& node)
{
    if (!node.command)
    {
        // Empty command - result is empty string
        _result = _builder.get("");
        return;
    }

    if (!_shellCommandCaptureMode)
    {
        // Statement-level: run command with normal I/O (no capture)
        codegen(node.command.get());

        // Set result to exit code
        auto* exitCb = findCallback("getvar.exitstatus()I");
        if (exitCb)
            _result = _builder.createCallFunction(_builder.getBuiltinFunction(*exitCb), {}, "exit_status");
        else
            _result = _builder.get(CoreVM::CoreNumber(0));
        return;
    }

    // Expression-level: capture command output as a string (like command substitution).
    //
    // IR pattern (same as SubstitutionExpr):
    //   1. Start capture - redirects stdout to a pipe
    //   2. Execute the command pipeline
    //   3. End capture - reads captured output and returns as string

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

void IRGenerator::visit(ast::SplatExpr const& node)
{
    // Splat expression: ...args inside a shell command
    // Iterates a list variable and emits each element as a cmd_arg

    auto* listValue = lookupFSharpVariable(node.name);
    if (!listValue)
    {
        reportTypeError("Undefined variable '{}' in splat expression", std::string_view(node.name));
        return;
    }

    // Load the list value (it's stored in an alloca)
    if (auto* alloca = dynamic_cast<CoreVM::AllocaInstr*>(listValue))
        listValue = _builder.createLoad(alloca, "splat.list");

    auto* cmdArgCb = findCallback("internal.cmd_arg(S)V");
    if (!cmdArgCb)
    {
        reportTypeError("Internal error: internal.cmd_arg builtin not found");
        return;
    }

    // Build a while loop: while list tag == 1 (Cons), extract head, call cmd_arg, advance to tail
    auto* currentFn = _builder.function();
    auto* condBlock = currentFn->createBlock("splat.cond");
    auto* bodyBlock = currentFn->createBlock("splat.body");
    auto* endBlock = currentFn->createBlock("splat.end");

    // Store list pointer in an alloca for the loop
    auto* cursorStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "splat.cursor");
    _builder.createStore(cursorStorage, listValue);
    _builder.createBr(condBlock);

    // Condition: check tag == 1 (Cons)
    _builder.setInsertPoint(condBlock);
    auto* cursorLoad = _builder.createLoad(cursorStorage, "splat.cursor.load");
    auto* tag = _builder.createObjGetTag(cursorLoad, "splat.tag");
    auto* tag1 = _builder.get(CoreVM::CoreNumber(1));
    auto* isCons = _builder.createNCmpEQ(tag, tag1, "splat.is_cons");
    _builder.createCondBr(isCons, bodyBlock, endBlock);

    // Body: extract head, pass directly to cmd_arg (list elements are already string pointers), advance to
    // tail
    _builder.setInsertPoint(bodyBlock);
    auto* cursorForHead = _builder.createLoad(cursorStorage, "splat.for_head");
    auto* head = _builder.createObjGetSlot(cursorForHead, _builder.get(CoreVM::CoreNumber(0)), "splat.head");
    _builder.createCallFunction(_builder.getBuiltinFunction(*cmdArgCb), { head }, "splat.cmd_arg");

    auto* cursorForTail = _builder.createLoad(cursorStorage, "splat.for_tail");
    auto* tail = _builder.createObjGetSlot(cursorForTail, _builder.get(CoreVM::CoreNumber(1)), "splat.tail");
    _builder.createStore(cursorStorage, tail);
    _builder.createBr(condBlock);

    // End block
    _builder.setInsertPoint(endBlock);
    _result = _builder.get(CoreVM::CoreNumber(0)); // unit value
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

        // Create Some: tag=1, slot[0]=value
        _result = emitSomeOption(innerValue, innerValue->type(), "option");
        annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::Option);
        annotateInnerType(_result, innerValue->type());
        if (auto objTypeId = getObjectTypeId(innerValue))
            annotateInnerObjectTypeId(_result, *objTypeId);
    }
    else
    {
        // None - tag=0, no payload
        _result = emitNoneOption("option");
        annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::Option);
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

    // Create Result object: Ok (tag=1) or Error (tag=0), payload in slot 0
    if (node.isOk)
        _result = emitOkResult(payloadValue, payloadValue->type(), "result");
    else
        _result = emitErrorResult(payloadValue, payloadValue->type(), "result");

    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::Result);
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

    // IMPORTANT: Copy the context values BEFORE calling codegen() below.
    // The operand might be a function application (e.g., `(inc x)?`) which pushes
    // a new FSharpFunctionContext onto _fsharpFunctionContextStack. This can cause
    // the vector to reallocate, invalidating any raw pointer to the context.
    auto const* funcCtx = currentFSharpFunctionContext();
    CoreVM::BasicBlock* returnBlock = funcCtx ? funcCtx->returnBlock : nullptr;
    CoreVM::AllocaInstr* returnStorage = funcCtx ? funcCtx->returnStorage : nullptr;

    // Evaluate the operand (should be an Option or Result object)
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

    // Error path: propagate (early return from function or exit program)
    _builder.setInsertPoint(errorBlock);
    if (returnBlock)
    {
        // Function-level: store error object and jump to return block
        CoreVM::Value* objReload2 = _builder.createLoad(objStorage, "try.obj.reload");
        _builder.createStore(returnStorage, objReload2, "try.error.store");
        _builder.createBr(returnBlock);
    }
    else
    {
        // Top-level: exit program with non-zero exit code
        _builder.createRet(_builder.get(CoreVM::CoreNumber(1)));
    }

    // Continue with extracted value
    _builder.setInsertPoint(continueBlock);
    _result = _builder.createLoad(resultStorage, "try.result.load");
}

void IRGenerator::visit(ast::OptionDefaultExpr const& node)
{
    TRACE_SCOPE("visit(OptionDefaultExpr)");

    // The ?| operator unwraps an Option value with a fallback:
    // - If the value is Some (tag=1), extract and return the inner value
    // - If the value is None (tag=0), evaluate and return the default expression

    // Evaluate the option operand
    CoreVM::Value* obj = codegen(node.option.get());
    if (!obj)
        return;

    // Store the object in an alloca so we can reload it in successor blocks.
    auto* objStorage = createAllocaInEntryBlock(obj->type(), "optdefault.obj");
    _builder.createStore(objStorage, obj, "optdefault.obj.store");

    // Extract tag using OGETTAG
    auto* tag = _builder.createObjGetTag(obj, "optdefault.tag");

    // Check if Some (tag == 1)
    auto* isSome = _builder.createNCmpEQ(tag, _builder.get(CoreVM::CoreNumber(1)), "optdefault.is_some");

    // Create blocks
    auto* someBlock = _builder.createBlock("optdefault.some");
    auto* noneBlock = _builder.createBlock("optdefault.none");
    auto* continueBlock = _builder.createBlock("optdefault.continue");

    _builder.createCondBr(isSome, someBlock, noneBlock);

    // None path first: evaluate default expression to get concrete type for deferred alloca
    _builder.setInsertPoint(noneBlock);
    auto* defaultVal = codegen(node.defaultValue.get());
    if (!defaultVal)
        return;

    // Create result storage with default expression's concrete type (deferred alloca pattern)
    auto* resultStorage = createAllocaInEntryBlock(defaultVal->type(), "optdefault.result");
    _builder.createStore(resultStorage, defaultVal, "optdefault.none.store");
    _builder.createBr(continueBlock);

    // Some path: reload object and extract inner value using OGETSLOT
    _builder.setInsertPoint(someBlock);
    auto* objReload = _builder.createLoad(objStorage, "optdefault.obj.reload");
    auto* innerValue =
        _builder.createObjGetSlot(objReload, _builder.get(CoreVM::CoreNumber(0)), "optdefault.inner");
    _builder.createStore(resultStorage, innerValue, "optdefault.some.store");
    _builder.createBr(continueBlock);

    // Continue with result
    _builder.setInsertPoint(continueBlock);
    _result = _builder.createLoad(resultStorage, "optdefault.result.load");
}

// ============================================================================
// Option Combinators: Option.map, Option.bind, Option.defaultValue
// ============================================================================

std::optional<IRGenerator::ResolvedFunction> IRGenerator::resolveFunctionArgument(ast::Expr const* expr)
{
    // Unwrap ParenExpr wrappers
    while (auto const* paren = dynamic_cast<ast::ParenExpr const*>(expr))
        expr = paren->inner.get();

    if (auto const* ident = dynamic_cast<ast::IdentifierExpr const*>(expr))
    {
        // Named function
        auto funcName = ident->name;
        if (auto const* func = lookupFSharpFunction(funcName))
            return ResolvedFunction { func, funcName };

        // Fallback: check function reference
        if (auto ref = lookupFSharpFunctionRef(funcName))
        {
            funcName = *ref;
            if (auto const* func = lookupFSharpFunction(funcName))
                return ResolvedFunction { func, funcName };
        }
        return std::nullopt;
    }

    if (auto const* lambda = dynamic_cast<ast::LambdaExpr const*>(expr))
    {
        auto funcName = generateLambdaName();
        FSharpFunction lambdaFunc;
        extractTypedParameters(lambda->parameters, lambdaFunc);
        lambdaFunc.body = lambda->body.get();
        lambdaFunc.returnKind = determineReturnKind(lambdaFunc.body);
        lambdaFunc.capturedBindings = collectFreeVariables(lambdaFunc.body, lambdaFunc.parameters);
        registerFSharpFunction(funcName, std::move(lambdaFunc));
        return ResolvedFunction { lookupFSharpFunction(funcName), funcName };
    }

    return std::nullopt;
}

bool IRGenerator::tryGenerateOptionCall(std::string const& methodName,
                                        std::vector<ast::Expr const*> const& argExprs)
{
    if (methodName == "map")
    {
        generateOptionMap(argExprs);
        return true;
    }
    if (methodName == "bind")
    {
        generateOptionBind(argExprs);
        return true;
    }
    if (methodName == "defaultValue")
    {
        generateOptionDefaultValue(argExprs);
        return true;
    }
    return false;
}

bool IRGenerator::tryGenerateOptionMethodCall(std::string const& methodName,
                                              ast::Expr const* objectExpr,
                                              std::vector<ast::Expr const*> const& argExprs)
{
    // Evaluate the object (the option value)
    auto* optionValue = codegen(objectExpr);
    if (!optionValue)
    {
        reportTypeError("Failed to evaluate option object for .{}", std::string_view(methodName));
        return true; // consumed, even on error
    }

    if (methodName == "map")
    {
        if (argExprs.size() != 1)
        {
            reportTypeError("Option.map requires exactly 1 function argument");
            return true;
        }
        generateOptionMapWithValue(argExprs[0], optionValue);
        return true;
    }
    if (methodName == "bind")
    {
        if (argExprs.size() != 1)
        {
            reportTypeError("Option.bind requires exactly 1 function argument");
            return true;
        }
        generateOptionBindWithValue(argExprs[0], optionValue);
        return true;
    }
    if (methodName == "defaultValue")
    {
        if (argExprs.size() != 1)
        {
            reportTypeError("Option.defaultValue requires exactly 1 default argument");
            return true;
        }
        generateOptionDefaultValueWithValue(argExprs[0], optionValue);
        return true;
    }
    return false;
}

void IRGenerator::generateOptionMap(std::vector<ast::Expr const*> const& argExprs)
{
    if (argExprs.size() != 2)
    {
        reportTypeError("Option.map requires 2 arguments: function and option value");
        return;
    }
    // Evaluate the option value first (argExprs[1])
    auto* optionValue = codegen(argExprs[1]);
    if (!optionValue)
    {
        reportTypeError("Failed to evaluate option argument for Option.map");
        return;
    }
    generateOptionMapWithValue(argExprs[0], optionValue);
}

void IRGenerator::generateOptionMapWithValue(ast::Expr const* funcExpr, CoreVM::Value* optionValue)
{
    // Resolve the function argument
    auto resolved = resolveFunctionArgument(funcExpr);
    if (!resolved)
    {
        reportTypeError("Option.map requires a function as first argument");
        return;
    }

    // Store option value in alloca for cross-block access
    auto* objStorage = createAllocaInEntryBlock(optionValue->type(), "optmap.obj");
    _builder.createStore(objStorage, optionValue, "optmap.obj.store");

    // Extract tag
    auto* tag = _builder.createObjGetTag(optionValue, "optmap.tag");
    auto* isSome = _builder.createNCmpEQ(tag, _builder.get(CoreVM::CoreNumber(1)), "optmap.is_some");

    // Create blocks
    auto* someBlock = _builder.createBlock("optmap.some");
    auto* noneBlock = _builder.createBlock("optmap.none");
    auto* continueBlock = _builder.createBlock("optmap.continue");

    // Result storage — always holds an Option (Object type)
    auto* resultStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "optmap.result");

    _builder.createCondBr(isSome, someBlock, noneBlock);

    // None path: create None option
    _builder.setInsertPoint(noneBlock);
    auto* noneObj = emitNoneOption("optmap.none");
    _builder.createStore(resultStorage, noneObj, "optmap.none.store");
    _builder.createBr(continueBlock);

    // Some path: extract inner value, apply function, wrap in Some
    _builder.setInsertPoint(someBlock);
    auto* objReload = _builder.createLoad(objStorage, "optmap.obj.reload");
    auto* innerValue =
        _builder.createObjGetSlot(objReload, _builder.get(CoreVM::CoreNumber(0)), "optmap.inner");

    // Use the option's inner type annotation (if available) for correct element typing.
    // ObjGetSlot returns Void; the annotation carries the actual type (e.g., String).
    auto elemType = getInnerType(optionValue).value_or(innerValue->type());
    auto* elemStorage = createAllocaInEntryBlock(elemType, "optmap.elem");
    _builder.createStore(elemStorage, innerValue, "optmap.elem.store");
    auto* elemReload = _builder.createLoad(elemStorage, "optmap.elem.reload");

    // Call the function with inner value
    generateFSharpCall(resolved->func, resolved->name, { elemReload });
    auto* mapped = _result;
    if (!mapped)
    {
        reportTypeError("Failed to evaluate function in Option.map");
        return;
    }

    // Remember the mapped value's type for inner type annotation
    auto const mappedType = mapped->type();

    // Store mapped value in tmp alloca (survives across OALLOC)
    auto* mappedTmp = createAllocaInEntryBlock(mappedType, "optmap.mapped");
    _builder.createStore(mappedTmp, mapped, "optmap.mapped.store");

    // Wrap result in Some
    auto* mappedReload = _builder.createLoad(mappedTmp, "optmap.mapped.reload");
    auto* someObj = emitSomeOption(mappedReload, mappedType, "optmap.some");
    _builder.createStore(resultStorage, someObj, "optmap.some.store");
    _builder.createBr(continueBlock);

    // Continue
    _builder.setInsertPoint(continueBlock);
    _result = _builder.createLoad(resultStorage, "optmap.result.load");

    // Annotate the result with inner type info for downstream ?| and convertToString
    annotateInnerType(_result, mappedType);
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::Option);
}

void IRGenerator::generateOptionBind(std::vector<ast::Expr const*> const& argExprs)
{
    if (argExprs.size() != 2)
    {
        reportTypeError("Option.bind requires 2 arguments: function and option value");
        return;
    }
    // Evaluate the option value first (argExprs[1])
    auto* optionValue = codegen(argExprs[1]);
    if (!optionValue)
    {
        reportTypeError("Failed to evaluate option argument for Option.bind");
        return;
    }
    generateOptionBindWithValue(argExprs[0], optionValue);
}

void IRGenerator::generateOptionBindWithValue(ast::Expr const* funcExpr, CoreVM::Value* optionValue)
{
    // Resolve the function argument
    auto resolved = resolveFunctionArgument(funcExpr);
    if (!resolved)
    {
        reportTypeError("Option.bind requires a function as first argument");
        return;
    }

    // Store option value in alloca for cross-block access
    auto* objStorage = createAllocaInEntryBlock(optionValue->type(), "optbind.obj");
    _builder.createStore(objStorage, optionValue, "optbind.obj.store");

    // Extract tag
    auto* tag = _builder.createObjGetTag(optionValue, "optbind.tag");
    auto* isSome = _builder.createNCmpEQ(tag, _builder.get(CoreVM::CoreNumber(1)), "optbind.is_some");

    // Create blocks
    auto* someBlock = _builder.createBlock("optbind.some");
    auto* noneBlock = _builder.createBlock("optbind.none");
    auto* continueBlock = _builder.createBlock("optbind.continue");

    // Result storage — holds Option from either path
    auto* resultStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "optbind.result");

    _builder.createCondBr(isSome, someBlock, noneBlock);

    // None path: create None option
    _builder.setInsertPoint(noneBlock);
    auto* noneObj = emitNoneOption("optbind.none");
    _builder.createStore(resultStorage, noneObj, "optbind.none.store");
    _builder.createBr(continueBlock);

    // Some path: extract inner value, call function (f returns Option directly)
    _builder.setInsertPoint(someBlock);
    auto* objReload = _builder.createLoad(objStorage, "optbind.obj.reload");
    auto* innerValue =
        _builder.createObjGetSlot(objReload, _builder.get(CoreVM::CoreNumber(0)), "optbind.inner");

    // Use the option's inner type annotation for correct element typing
    auto elemType = getInnerType(optionValue).value_or(innerValue->type());
    auto* elemStorage = createAllocaInEntryBlock(elemType, "optbind.elem");
    _builder.createStore(elemStorage, innerValue, "optbind.elem.store");
    auto* elemReload = _builder.createLoad(elemStorage, "optbind.elem.reload");

    // Call the function — f already returns an Option, so no wrapping needed
    generateFSharpCall(resolved->func, resolved->name, { elemReload });
    auto* boundResult = _result;
    if (!boundResult)
    {
        reportTypeError("Failed to evaluate function in Option.bind");
        return;
    }

    _builder.createStore(resultStorage, boundResult, "optbind.some.store");
    _builder.createBr(continueBlock);

    // Continue
    _builder.setInsertPoint(continueBlock);
    _result = _builder.createLoad(resultStorage, "optbind.result.load");
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::Option);
}

void IRGenerator::generateOptionDefaultValue(std::vector<ast::Expr const*> const& argExprs)
{
    if (argExprs.size() != 2)
    {
        reportTypeError("Option.defaultValue requires 2 arguments: default value and option value");
        return;
    }
    // Evaluate the option value first (argExprs[1])
    auto* optionValue = codegen(argExprs[1]);
    if (!optionValue)
    {
        reportTypeError("Failed to evaluate option argument for Option.defaultValue");
        return;
    }
    generateOptionDefaultValueWithValue(argExprs[0], optionValue);
}

void IRGenerator::generateOptionDefaultValueWithValue(ast::Expr const* defaultExpr,
                                                      CoreVM::Value* optionValue)
{
    // Same IR pattern as ?| operator

    // Store option in alloca for cross-block access
    auto* objStorage = createAllocaInEntryBlock(optionValue->type(), "optdefval.obj");
    _builder.createStore(objStorage, optionValue, "optdefval.obj.store");

    // Extract tag
    auto* tag = _builder.createObjGetTag(optionValue, "optdefval.tag");
    auto* isSome = _builder.createNCmpEQ(tag, _builder.get(CoreVM::CoreNumber(1)), "optdefval.is_some");

    // Create blocks
    auto* someBlock = _builder.createBlock("optdefval.some");
    auto* noneBlock = _builder.createBlock("optdefval.none");
    auto* continueBlock = _builder.createBlock("optdefval.continue");

    _builder.createCondBr(isSome, someBlock, noneBlock);

    // None path first: evaluate default expression to get concrete type for deferred alloca
    _builder.setInsertPoint(noneBlock);
    auto* defaultVal = codegen(defaultExpr);
    if (!defaultVal)
    {
        reportTypeError("Failed to evaluate default value for Option.defaultValue");
        return;
    }

    // Create result storage with default expression's concrete type (deferred alloca pattern)
    auto* resultStorage = createAllocaInEntryBlock(defaultVal->type(), "optdefval.result");
    _builder.createStore(resultStorage, defaultVal, "optdefval.none.store");
    _builder.createBr(continueBlock);

    // Some path: extract inner value
    _builder.setInsertPoint(someBlock);
    auto* objReload = _builder.createLoad(objStorage, "optdefval.obj.reload");
    auto* innerValue =
        _builder.createObjGetSlot(objReload, _builder.get(CoreVM::CoreNumber(0)), "optdefval.inner");
    _builder.createStore(resultStorage, innerValue, "optdefval.some.store");
    _builder.createBr(continueBlock);

    // Continue
    _builder.setInsertPoint(continueBlock);
    _result = _builder.createLoad(resultStorage, "optdefval.result.load");
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
    // appear before finally blocks in the function's block list.
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

// --- Record type support ---

IRGenerator::RecordTypeInfo const* IRGenerator::lookupRecordType(std::string const& name) const
{
    if (auto it = _recordTypes.find(name); it != _recordTypes.end())
        return &it->second;
    return nullptr;
}

IRGenerator::RecordTypeInfo const* IRGenerator::resolveRecordTypeByFields(
    std::vector<std::string> const& fieldNames) const
{
    for (auto const& [name, info]: _recordTypes)
    {
        if (info.fields.size() != fieldNames.size())
            continue;
        bool match = true;
        for (size_t i = 0; i < fieldNames.size(); ++i)
        {
            if (info.fields[i].name != fieldNames[i])
            {
                match = false;
                break;
            }
        }
        if (match)
            return &info;
    }
    return nullptr;
}

void IRGenerator::visit(ast::RecordTypeDefStmt const& node)
{
    TRACE_SCOPE("visit(RecordTypeDefStmt)");

    // Allocate a custom type ID from the IR program
    auto typeId = _builder.program()->allocateCustomTypeId();

    // Build field info list with type annotations from the AST
    std::vector<CoreVM::FieldInfo> fields;
    std::unordered_map<std::string, CoreVM::LiteralType> fieldTypes;
    for (uint8_t i = 0; i < node.fields.size(); ++i)
    {
        auto vmType = CoreVM::LiteralType::Number; // default for non-primitive or unknown types
        if (auto const* prim = std::get_if<PrimitiveTypeNode>(&node.fields[i].type->node))
        {
            switch (prim->kind)
            {
                case PrimitiveType::Int: vmType = CoreVM::LiteralType::Number; break;
                case PrimitiveType::Float: vmType = CoreVM::LiteralType::Float; break;
                case PrimitiveType::Str: vmType = CoreVM::LiteralType::String; break;
                case PrimitiveType::Bool: vmType = CoreVM::LiteralType::Boolean; break;
                case PrimitiveType::Unit: vmType = CoreVM::LiteralType::Void; break;
            }
        }
        fields.push_back({ node.fields[i].name, i, vmType });
        fieldTypes[node.fields[i].name] = vmType;
    }

    // Store in the IR generator's record type table
    RecordTypeInfo info;
    info.typeId = typeId;
    info.name = node.name;
    info.fields = fields;
    info.fieldTypes = std::move(fieldTypes);
    _recordTypes[node.name] = std::move(info);

    // Persist record field info for completion support in the REPL
    if (_persistentState)
    {
        std::vector<RecordFieldInfo> fieldInfos;
        for (auto const& field: node.fields)
            fieldInfos.push_back(RecordFieldInfo { .name = field.name, .typeName = toString(field.type) });
        _persistentState->recordTypeFields[node.name] = std::move(fieldInfos);
    }

    // Register as a custom product type on the IR program so TargetCodeGenerator
    // can register it in the ConstantPool's TypeRegistry before execution.
    CoreVM::IRProgram::CustomProductType customType;
    customType.name = node.name;
    customType.fields = fields;
    customType.assignedId = typeId;
    _builder.program()->addCustomProductType(std::move(customType));
}

void IRGenerator::visit(ast::RecordExpr const& node)
{
    TRACE_SCOPE("visit(RecordExpr)");

    // Resolve the record type — by explicit name or by matching field names
    RecordTypeInfo const* typeInfo = nullptr;
    if (!node.typeName.empty())
        typeInfo = lookupRecordType(node.typeName);

    if (!typeInfo)
    {
        // Try to resolve by field names
        std::vector<std::string> fieldNames;
        for (auto const& field: node.fields)
            fieldNames.push_back(field.name);
        typeInfo = resolveRecordTypeByFields(fieldNames);
    }

    if (!typeInfo)
    {
        reportTypeError("Unknown record type for literal with fields: {}", [&] {
            std::string s;
            for (size_t i = 0; i < node.fields.size(); ++i)
            {
                if (i > 0)
                    s += ", ";
                s += node.fields[i].name;
            }
            return s;
        }());
        return;
    }

    // Codegen each field value
    std::vector<CoreVM::Value*> fieldValues;
    for (auto const& field: node.fields)
    {
        auto* val = codegen(field.value.get());
        if (!val)
        {
            reportTypeError("Failed to generate code for record field '{}'", std::string_view(field.name));
            return;
        }
        fieldValues.push_back(val);
    }

    // Allocate the record object
    CoreVM::Value* obj =
        _builder.createObjAlloc(_builder.get(CoreVM::CoreNumber(typeInfo->typeId)), "record");

    // Set each field slot (fields are in definition order matching the type)
    for (size_t i = 0; i < node.fields.size(); ++i)
    {
        // Find the slot offset for this field name in the type definition
        uint8_t slotOffset = 0;
        for (auto const& fieldDef: typeInfo->fields)
        {
            if (fieldDef.name == node.fields[i].name)
            {
                slotOffset = fieldDef.offset;
                break;
            }
        }
        obj = _builder.createObjSetSlot(
            obj, _builder.get(CoreVM::CoreNumber(slotOffset)), fieldValues[i], "record.field");
    }

    _result = obj;
    annotateObjectTypeId(_result, typeInfo->typeId);
}

void IRGenerator::visit(ast::RecordUpdateExpr const& node)
{
    TRACE_SCOPE("visit(RecordUpdateExpr)");

    // Codegen the base record expression
    auto* baseObj = codegen(node.base.get());
    if (!baseObj)
    {
        reportTypeError("Failed to generate code for record update base");
        return;
    }

    // Determine the record type from the base object's annotation
    RecordTypeInfo const* typeInfo = nullptr;
    if (auto objTypeId = getObjectTypeId(baseObj))
    {
        for (auto const& [name, info]: _recordTypes)
        {
            if (info.typeId == *objTypeId)
            {
                typeInfo = &info;
                break;
            }
        }
    }

    if (!typeInfo)
    {
        reportTypeError("Record update requires a known record type");
        return;
    }

    // Allocate a new record object of the same type
    CoreVM::Value* newObj =
        _builder.createObjAlloc(_builder.get(CoreVM::CoreNumber(typeInfo->typeId)), "record.upd");

    // Copy all slots from the original record
    for (auto const& fieldDef: typeInfo->fields)
    {
        auto* slotVal = _builder.createObjGetSlot(
            baseObj, _builder.get(CoreVM::CoreNumber(fieldDef.offset)), "record.copy." + fieldDef.name);
        newObj = _builder.createObjSetSlot(
            newObj, _builder.get(CoreVM::CoreNumber(fieldDef.offset)), slotVal, "record.copy.set");
    }

    // Store newObj in an alloca so it survives across basic blocks that codegen() may create
    // (e.g., inlined untyped functions with match expressions create new blocks).
    auto* newObjStorage = createAllocaInEntryBlock(newObj->type(), "record.upd.obj");
    _builder.createStore(newObjStorage, newObj, "record.upd.init");

    // Overwrite updated fields
    for (auto const& update: node.updates)
    {
        auto* val = codegen(update.value.get());
        if (!val)
        {
            reportTypeError("Failed to generate code for record update field '{}'",
                            std::string_view(update.name));
            return;
        }

        auto* currentNewObj = _builder.createLoad(newObjStorage, "record.upd.reload");

        // Find the slot offset for this field name
        for (auto const& fieldDef: typeInfo->fields)
        {
            if (fieldDef.name == update.name)
            {
                auto* updatedObj =
                    _builder.createObjSetSlot(currentNewObj,
                                              _builder.get(CoreVM::CoreNumber(fieldDef.offset)),
                                              val,
                                              "record.upd.field");
                _builder.createStore(newObjStorage, updatedObj, "record.upd.store");
                break;
            }
        }
    }

    _result = _builder.createLoad(newObjStorage, "record.upd.result");
    annotateObjectTypeId(_result, typeInfo->typeId);
}

void IRGenerator::visit(ast::FieldAccessExpr const& node)
{
    TRACE_SCOPE("visit(FieldAccessExpr)");

    // Handle DateTime module-qualified access (DateTime.now, DateTime.fromEpoch)
    if (auto const* modIdent = dynamic_cast<ast::IdentifierExpr const*>(node.object.get()))
    {
        if (modIdent->name == "DateTime")
        {
            if (node.fieldName == "now")
            {
                if (tryGenerateNativeCall("datetime_now", {}))
                {
                    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::DateTime);
                    return;
                }
            }
            if (node.fieldName == "fromEpoch")
            {
                // DateTime.fromEpoch requires an argument — handled in ApplicationExpr
                reportTypeError("DateTime.fromEpoch requires an epoch argument");
                return;
            }
            reportTypeError("DateTime has no member '{}'", std::string_view(node.fieldName));
            return;
        }
        if (modIdent->name == "Size")
        {
            auto const isSizeMethod = node.fieldName == "fromBytes" || node.fieldName == "fromKB"
                                      || node.fieldName == "fromMB" || node.fieldName == "fromGB"
                                      || node.fieldName == "fromTB";
            auto const found = isSizeMethod;
            if (found)
            {
                // Size.fromXX requires an argument — handled in ApplicationExpr
                reportTypeError("Size.{} requires a numeric argument", std::string_view(node.fieldName));
                return;
            }
            reportTypeError("Size has no member '{}'", std::string_view(node.fieldName));
            return;
        }
    }

    // Codegen the object expression
    auto* obj = codegen(node.object.get());
    if (!obj)
    {
        reportTypeError("Failed to generate code for field access object");
        return;
    }

    // String dot properties (e.g., s.length) — check before record/union lookup
    if (obj->type() == CoreVM::LiteralType::String)
    {
        if (tryGenerateBuiltinPropertyAccess(obj, node.fieldName))
            return;
        reportTypeError("String has no property '{}'", std::string_view(node.fieldName));
        return;
    }

    // Look up the record type from the object's type ID annotation
    RecordTypeInfo const* typeInfo = nullptr;
    if (auto objTypeId = getObjectTypeId(obj))
    {
        for (auto const& [name, info]: _recordTypes)
        {
            if (info.typeId == *objTypeId)
            {
                typeInfo = &info;
                break;
            }
        }
    }

    if (!typeInfo)
    {
        // Try to resolve via IR chain analysis
        if (auto info = tryGetObjectInfo(obj))
        {
            for (auto const& [name, recInfo]: _recordTypes)
            {
                if (recInfo.typeId == info->typeId)
                {
                    typeInfo = &recInfo;
                    break;
                }
            }
        }
    }

    if (!typeInfo)
    {
        // Try union type field access: look up field name in union types
        UnionTypeInfo const* unionInfo = nullptr;
        if (auto objTypeId = getObjectTypeId(obj))
        {
            for (auto const& [name, uInfo]: _unionTypes)
            {
                if (uInfo.typeId == *objTypeId)
                {
                    unionInfo = &uInfo;
                    break;
                }
            }
        }
        if (!unionInfo)
        {
            // Try to resolve via IR chain analysis
            if (auto info = tryGetObjectInfo(obj))
            {
                for (auto const& [name, uInfo]: _unionTypes)
                {
                    if (uInfo.typeId == info->typeId)
                    {
                        unionInfo = &uInfo;
                        break;
                    }
                }
            }
        }

        if (unionInfo)
        {
            // Look up the field name in the union's field lookup map
            if (auto it = unionInfo->fieldLookup.find(node.fieldName); it != unionInfo->fieldLookup.end())
            {
                auto const& [variantTag, slotOffset] = it->second;
                _result = _builder.createObjGetSlot(
                    obj, _builder.get(CoreVM::CoreNumber(slotOffset)), "union." + node.fieldName);
                return;
            }
            // Try function-as-method on union type: obj.funcName → funcName(obj)
            if (auto const* func = lookupFSharpFunction(node.fieldName))
            {
                if (!func->parameters.empty() && !func->parameterTypes.empty() && func->parameterTypes[0])
                {
                    auto const* paramType = func->parameterTypes[0]->get();
                    bool typeMatches = false;
                    if (auto const* uType = std::get_if<UnionType>(&paramType->node))
                        typeMatches = (uType->name == unionInfo->name);
                    if (typeMatches)
                    {
                        generateFSharpCall(func, node.fieldName, { obj });
                        return;
                    }
                }
            }
            reportTypeError("Union type '{}' has no field '{}'",
                            std::string_view(unionInfo->name),
                            std::string_view(node.fieldName));
            return;
        }

        // Try built-in type property access (list.length, option.isSome, etc.)
        if (tryGenerateBuiltinPropertyAccess(obj, node.fieldName))
            return;

        reportTypeError("Field access requires a known record or union type, got unknown object for '.{}'",
                        std::string_view(node.fieldName));
        return;
    }

    // Find the field by name in the record type
    bool found = false;
    for (auto const& fieldDef: typeInfo->fields)
    {
        if (fieldDef.name == node.fieldName)
        {
            _result = _builder.createObjGetSlot(
                obj, _builder.get(CoreVM::CoreNumber(fieldDef.offset)), "record." + node.fieldName);

            // Annotate the result with the field's literal type for correct convertToString dispatch
            if (auto it = typeInfo->fieldTypes.find(node.fieldName); it != typeInfo->fieldTypes.end())
                annotateInnerType(_result, it->second);

            // For Object-typed fields with a known nested record type, propagate the type ID
            if (auto it = typeInfo->fieldObjectTypeIds.find(node.fieldName);
                it != typeInfo->fieldObjectTypeIds.end())
                annotateObjectTypeId(_result, it->second);

            found = true;
            break;
        }
    }

    if (!found)
    {
        // Try function-as-method dot access: obj.funcName → funcName(obj)
        if (auto const* func = lookupFSharpFunction(node.fieldName))
        {
            if (!func->parameters.empty() && !func->parameterTypes.empty() && func->parameterTypes[0])
            {
                // Check if the first parameter type matches the object's record type
                auto const* paramType = func->parameterTypes[0]->get();
                bool typeMatches = false;
                if (auto const* recType = std::get_if<RecordType>(&paramType->node))
                    typeMatches = (recType->name == typeInfo->name);
                if (typeMatches)
                {
                    generateFSharpCall(func, node.fieldName, { obj });
                    return;
                }
            }
        }
        reportTypeError("Record type '{}' has no field '{}'",
                        std::string_view(typeInfo->name),
                        std::string_view(node.fieldName));
    }
}

void IRGenerator::visit(ast::OptionalChainExpr const& node)
{
    TRACE_SCOPE("visit(OptionalChainExpr)");

    // The ?. operator accesses a field on an Option-wrapped record:
    // - If the value is Some (tag=1), extract inner value, access field, wrap in Some
    // - If the value is None (tag=0), return None
    //
    // Result is always option<T>, enabling chaining: a?.b?.c

    // Evaluate the option operand
    auto* obj = codegen(node.object.get());
    if (!obj)
        return;

    // Store the object in an alloca so we can reload it in successor blocks
    auto* objStorage = createAllocaInEntryBlock(obj->type(), "optchain.obj");
    _builder.createStore(objStorage, obj, "optchain.obj.store");

    // Extract tag using OGETTAG
    auto* tag = _builder.createObjGetTag(obj, "optchain.tag");

    // Check if Some (tag == 1)
    auto* isSome = _builder.createNCmpEQ(tag, _builder.get(CoreVM::CoreNumber(1)), "optchain.is_some");

    // Pre-allocate result storage (always Object type since result is an Option)
    auto* resultStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "optchain.result");

    // Create blocks
    auto* someBlock = _builder.createBlock("optchain.some");
    auto* noneBlock = _builder.createBlock("optchain.none");
    auto* continueBlock = _builder.createBlock("optchain.continue");

    _builder.createCondBr(isSome, someBlock, noneBlock);

    // None path: build a None object
    _builder.setInsertPoint(noneBlock);
    auto* noneObj = emitNoneOption("optchain.none.obj");
    _builder.createStore(resultStorage, noneObj, "optchain.none.store");
    _builder.createBr(continueBlock);

    // Some path: extract inner value, access field, wrap in Some
    _builder.setInsertPoint(someBlock);
    auto* objReload = _builder.createLoad(objStorage, "optchain.obj.reload");
    auto* innerVal =
        _builder.createObjGetSlot(objReload, _builder.get(CoreVM::CoreNumber(0)), "optchain.inner");

    // Resolve the record type from the inner value to find the field offset.
    // Try multiple strategies: inner object type ID annotation, direct type ID, IR chain,
    // and finally fall back to searching all record types by field name.
    RecordTypeInfo const* typeInfo = nullptr;

    // Strategy 1: Inner object type ID propagated from OptionExpr (e.g., Some { name = "Alice" })
    if (auto innerObjTypeId = getInnerObjectTypeId(obj))
    {
        for (auto const& [name, info]: _recordTypes)
        {
            if (info.typeId == *innerObjTypeId)
            {
                typeInfo = &info;
                break;
            }
        }
    }

    // Strategy 2: Inner value's own type ID annotation
    if (!typeInfo)
    {
        if (auto innerTypeId = getObjectTypeId(innerVal))
        {
            for (auto const& [name, info]: _recordTypes)
            {
                if (info.typeId == *innerTypeId)
                {
                    typeInfo = &info;
                    break;
                }
            }
        }
    }

    // Strategy 3: IR chain analysis
    if (!typeInfo)
    {
        if (auto info = tryGetObjectInfo(innerVal))
        {
            for (auto const& [name, recInfo]: _recordTypes)
            {
                if (recInfo.typeId == info->typeId)
                {
                    typeInfo = &recInfo;
                    break;
                }
            }
        }
    }

    // Strategy 4: Search all record types by field name (needed when option is None
    // and no type annotation propagates, e.g., `let x = None; x?.name`)
    if (!typeInfo)
    {
        for (auto const& [name, info]: _recordTypes)
        {
            for (auto const& field: info.fields)
            {
                if (field.name == node.fieldName)
                {
                    typeInfo = &info;
                    break;
                }
            }
            if (typeInfo)
                break;
        }
    }

    if (!typeInfo)
    {
        reportTypeError("Optional chaining requires a known record type inside the option for '.{}'",
                        std::string_view(node.fieldName));
        return;
    }

    // Find the field by name and extract its value
    bool found = false;
    for (auto const& fieldDef: typeInfo->fields)
    {
        if (fieldDef.name == node.fieldName)
        {
            auto* fieldValue = _builder.createObjGetSlot(
                innerVal, _builder.get(CoreVM::CoreNumber(fieldDef.offset)), "optchain." + node.fieldName);

            // Wrap the field value in Some
            auto* someObj = emitSomeOption(fieldValue, fieldValue->type(), "optchain.some.obj");

            // Annotate inner type for downstream use (e.g., ?| default value, or further ?. chaining)
            if (auto it = typeInfo->fieldTypes.find(node.fieldName); it != typeInfo->fieldTypes.end())
                annotateInnerType(someObj, it->second);

            // If the field is itself an option-wrapped record, propagate inner object type ID
            // to enable further ?. chaining
            if (auto fieldObjTypeId = getObjectTypeId(fieldValue))
                annotateInnerObjectTypeId(someObj, *fieldObjTypeId);

            _builder.createStore(resultStorage, someObj, "optchain.some.store");
            _builder.createBr(continueBlock);

            found = true;
            break;
        }
    }

    if (!found)
    {
        reportTypeError("Record type '{}' has no field '{}' (in optional chaining)",
                        std::string_view(typeInfo->name),
                        std::string_view(node.fieldName));
        return;
    }

    // Continue with result
    _builder.setInsertPoint(continueBlock);
    _result = _builder.createLoad(resultStorage, "optchain.result.load");
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::Option);
}

// ============================================================================
// Discriminated Unions (ADTs)
// ============================================================================

IRGenerator::ConstructorInfo const* IRGenerator::lookupConstructor(std::string const& name) const
{
    if (auto it = _constructorRegistry.find(name); it != _constructorRegistry.end())
        return &it->second;
    return nullptr;
}

void IRGenerator::visit(ast::UnionTypeDefStmt const& node)
{
    TRACE_SCOPE("visit(UnionTypeDefStmt)");

    // Allocate a custom type ID from the IR program
    auto typeId = _builder.program()->allocateCustomTypeId();

    // Build variant info list
    std::vector<CoreVM::VariantInfo> variants;
    // Maps field name → (variant_tag, slot_offset) for field access
    std::unordered_map<std::string, std::pair<int, uint8_t>> fieldLookup;

    for (size_t i = 0; i < node.variants.size(); ++i)
    {
        auto const& variant = node.variants[i];

        // Build FieldInfo entries from named fields
        std::vector<CoreVM::FieldInfo> fields;
        for (size_t j = 0; j < variant.fieldNames.size(); ++j)
        {
            if (!variant.fieldNames[j].empty())
            {
                fields.push_back({ variant.fieldNames[j],
                                   static_cast<uint8_t>(j),
                                   CoreVM::LiteralType::Number }); // TODO: derive from payloadTypes
                fieldLookup[variant.fieldNames[j]] = { static_cast<int>(i), static_cast<uint8_t>(j) };
            }
        }

        variants.push_back(
            { variant.name, static_cast<uint8_t>(variant.payloadTypes.size()), std::move(fields) });

        // Register each constructor in the constructor registry
        ConstructorInfo ctorInfo;
        ctorInfo.typeName = node.name;
        ctorInfo.typeId = typeId;
        ctorInfo.tag = static_cast<int>(i);
        ctorInfo.payloadSlots = static_cast<uint8_t>(variant.payloadTypes.size());
        ctorInfo.fieldNames = variant.fieldNames;
        _constructorRegistry[variant.name] = ctorInfo;
    }

    // Store in the IR generator's union type table
    UnionTypeInfo info;
    info.typeId = typeId;
    info.name = node.name;
    info.variants = variants;
    info.fieldLookup = std::move(fieldLookup);
    _unionTypes[node.name] = std::move(info);

    // Register as a custom sum type on the IR program so TargetCodeGenerator
    // can register it in the ConstantPool's TypeRegistry before execution.
    CoreVM::IRProgram::CustomSumType customType;
    customType.name = node.name;
    customType.variants = variants;
    customType.assignedId = typeId;
    _builder.program()->addCustomSumType(std::move(customType));
}

void IRGenerator::visit(ast::UnionConstructorExpr const& node)
{
    TRACE_SCOPE("visit(UnionConstructorExpr)");

    auto const* ctorInfo = lookupConstructor(node.constructorName);
    if (!ctorInfo)
    {
        reportTypeError("Unknown union constructor '{}'", std::string_view(node.constructorName));
        return;
    }

    // Allocate the object with the union's type ID
    auto* typeIdVal = _builder.get(CoreVM::CoreNumber(ctorInfo->typeId));
    CoreVM::Value* obj = _builder.createObjAlloc(typeIdVal, node.constructorName);

    // Set the tag for this constructor variant
    obj = _builder.createObjSetTag(
        obj, _builder.get(CoreVM::CoreNumber(ctorInfo->tag)), node.constructorName + ".tag");

    // Set each payload slot (chained to avoid multi-use ObjAlloc)
    for (size_t i = 0; i < node.arguments.size(); ++i)
    {
        CoreVM::Value* argVal = codegen(node.arguments[i].get());
        if (!argVal)
        {
            reportTypeError("Failed to generate code for constructor argument {}", i);
            return;
        }
        obj = _builder.createObjSetSlot(obj,
                                        _builder.get(CoreVM::CoreNumber(i)),
                                        argVal,
                                        node.constructorName + ".slot" + std::to_string(i));
    }

    _result = obj;
    annotateObjectTypeId(_result, ctorInfo->typeId);
}

void IRGenerator::visit(ast::ExecPipelineExpr const& node)
{
    TRACE_SCOPE("visit(ExecPipelineExpr)");

    auto* cmdStartCb = findCallback("internal.cmd_start(S)V");
    auto* cmdArgCb = findCallback("internal.cmd_arg(S)V");

    if (!cmdStartCb || !cmdArgCb)
    {
        reportTypeError("Internal error: command execution builtins not found");
        return;
    }

    for (size_t i = 0; i < node.commands.size(); ++i)
    {
        auto const& cmd = node.commands[i];
        bool const lastInChain = (i == node.commands.size() - 1);

        // Emit cmd_start with dynamic program value (F# expression)
        auto* progValue = codegen(cmd.program.get());
        if (!progValue)
        {
            reportTypeError("Failed to evaluate exec program expression");
            return;
        }
        // exec arguments are always strings at runtime. If the IR type is wrong
        // (Object/Void from pattern matching), reinterpret as String via typed alloca.
        // This avoids convertToString's N2S fallback which corrupts string pointers.
        auto* progStr = ensureString(progValue, "exec.prog");
        _builder.createCallFunction(_builder.getBuiltinFunction(*cmdStartCb), { progStr }, "cmd_start");

        // Emit cmd_arg for each argument (F# expressions)
        for (auto const& arg: cmd.arguments)
        {
            auto* argValue = codegen(arg.get());
            if (!argValue)
            {
                reportTypeError("Failed to evaluate exec argument expression");
                return;
            }
            auto* argStr = ensureString(argValue, "exec.arg");
            _builder.createCallFunction(_builder.getBuiltinFunction(*cmdArgCb), { argStr }, "cmd_arg");
        }

        // Emit piped execution
        _result = execBuiltCommandPiped(lastInChain);
    }
}

// {{{ Builtin Higher-Order Function IR Generators

void IRGenerator::generateBuiltinHOFCall(FSharpFunction const* func,
                                         std::string const& /*funcName*/,
                                         std::vector<CoreVM::Value*> const& args)
{
    // Set up scope: rebind captured variables (from partial application) and function refs
    pushFSharpScope();
    for (auto const& [capName, capStorage]: func->capturedBindings)
        bindFSharpVariable(capName, capStorage);
    for (auto const& [varName, targetFunc]: func->capturedFunctionRefs)
        _currentFSharpScope->functionRefs[varName] = targetFunc;

    // Bind explicit arguments to parameter names
    for (size_t i = 0; i < func->parameters.size(); ++i)
    {
        auto* storage = createAllocaInEntryBlock(args[i]->type(), func->parameters[i]);
        _builder.createStore(storage, args[i], func->parameters[i]);
        bindFSharpVariable(func->parameters[i], storage);

        // Propagate type annotations through HOF parameter bindings
        if (auto objTypeId = getObjectTypeId(args[i]))
            annotateObjectTypeId(storage, *objTypeId);
        if (auto elemTypeId = getListElementTypeId(args[i]))
            annotateListElementTypeId(storage, *elemTypeId);
        if (auto elt = getListElementLiteralType(args[i]))
            annotateListElementLiteralType(storage, *elt);

        // Track function references passed as arguments
        if (auto const* constStr = dynamic_cast<CoreVM::ConstantString*>(args[i]))
        {
            auto const& refName = constStr->get();
            if (lookupFSharpFunction(refName) || refName == "print" || refName == "println")
                _currentFSharpScope->functionRefs[func->parameters[i]] = constStr->get();
        }
    }

    // Resolve actual function and list arguments from scope
    auto const& hofName = func->builtinHOF;

    // Helper: load a list parameter and propagate list element type annotations
    auto loadListParam = [&](std::string_view paramName, std::string_view label) -> CoreVM::Value* {
        auto* storage = lookupFSharpVariable(std::string(paramName));
        auto* loaded = _builder.createLoad(storage, std::string(label));
        if (auto elemTypeId = getListElementTypeId(storage))
            annotateListElementTypeId(loaded, *elemTypeId);
        if (auto elt = getListElementLiteralType(storage))
            annotateListElementLiteralType(loaded, *elt);
        if (auto objTypeId = getObjectTypeId(storage))
            annotateObjectTypeId(loaded, *objTypeId);
        return loaded;
    };

    if (hofName == "map")
    {
        auto* listVal = loadListParam("__xs", "map.xs");
        generateMapIR("__f", listVal);
    }
    else if (hofName == "filter")
    {
        auto* listVal = loadListParam("__xs", "filter.xs");
        generateFilterIR("__pred", listVal);
    }
    else if (hofName == "fold")
    {
        auto* initVal = _builder.createLoad(lookupFSharpVariable("__init"), "fold.init");
        auto* listVal = loadListParam("__xs", "fold.xs");
        generateFoldIR(initVal, "__f", listVal);
    }
    else if (hofName == "reduce")
    {
        auto* listVal = loadListParam("__xs", "reduce.xs");
        generateReduceIR("__f", listVal);
    }
    else if (hofName == "reverse")
    {
        auto* listVal = loadListParam("__xs", "reverse.xs");
        generateReverseIR(listVal);
    }
    else if (hofName == "find")
    {
        auto* listVal = loadListParam("__xs", "find.xs");
        generateFindIR("__pred", listVal);
    }
    else if (hofName == "exists")
    {
        auto* listVal = loadListParam("__xs", "exists.xs");
        generateExistsIR("__pred", listVal);
    }
    else if (hofName == "forall")
    {
        auto* listVal = loadListParam("__xs", "forall.xs");
        generateForallIR("__pred", listVal);
    }
    else if (hofName == "each")
    {
        auto* listVal = loadListParam("__xs", "each.xs");
        generateEachIR("__f", listVal);
    }
    else if (hofName == "take")
    {
        auto* countVal = _builder.createLoad(lookupFSharpVariable("__n"), "take.n");
        auto* listVal = loadListParam("__xs", "take.xs");
        generateTakeIR(countVal, listVal);
    }
    else if (hofName == "drop")
    {
        auto* countVal = _builder.createLoad(lookupFSharpVariable("__n"), "drop.n");
        auto* listVal = loadListParam("__xs", "drop.xs");
        generateDropIR(countVal, listVal);
    }
    else if (hofName == "zip")
    {
        auto* listA = loadListParam("__xs", "zip.xs");
        auto* listB = loadListParam("__ys", "zip.ys");
        generateZipIR(listA, listB);
    }
    else if (hofName == "flatten")
    {
        auto* listVal = loadListParam("__xss", "flatten.xss");
        generateFlattenIR(listVal);
    }
    else if (hofName == "sortBy")
    {
        auto* listVal = loadListParam("__xs", "sortBy.xs");
        generateSortByIR("__f", listVal);
    }
    else if (hofName == "groupBy")
    {
        auto* listVal = loadListParam("__xs", "groupBy.xs");
        generateGroupByIR("__f", listVal);
    }
    else if (hofName == "sort")
    {
        auto* listVal = loadListParam("__xs", "sort.xs");
        generateSortIR(listVal);
    }
    else if (hofName == "distinct")
    {
        auto* listVal = loadListParam("__xs", "distinct.xs");
        generateDistinctIR(listVal);
    }
    else
    {
        reportTypeError("Unknown builtin HOF: {}", std::string_view(hofName));
    }

    popFSharpScope();
}

void IRGenerator::generateMapIR(std::string const& funcParamName, CoreVM::Value* listValue)
{
    auto* tag1 = _builder.get(CoreVM::CoreNumber(1));  // Cons
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0)); // head
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1)); // tail

    // Resolve the function to call
    auto funcName = funcParamName;
    if (auto ref = lookupFSharpFunctionRef(funcParamName))
        funcName = *ref;
    auto const* func = lookupFSharpFunction(funcName);
    if (!func)
    {
        reportTypeError("map: function argument '{}' not found", std::string_view(funcParamName));
        return;
    }

    // Allocas for phase 1 (forward iteration building reversed accumulator)
    auto* srcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "map.src");
    _builder.createStore(srcStorage, listValue);

    auto* accStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "map.acc");
    auto* nil = emitNilList(CoreVM::LiteralType::Void, "map.nil");
    _builder.createStore(accStorage, nil);

    auto const mapElemType = getListElementLiteralType(listValue).value_or(CoreVM::LiteralType::Number);
    auto* elemAlloca = createAllocaInEntryBlock(mapElemType, "map.elem");

    // Propagate list element type to extracted elements (e.g., ProcessInfo for ps)
    if (auto elemTypeId = getListElementTypeId(listValue))
        annotateObjectTypeId(elemAlloca, *elemTypeId);

    // Create blocks
    auto* condBlock = _builder.createBlock("map.cond");
    auto* bodyBlock = _builder.createBlock("map.body");
    auto* revInitBlock = _builder.createBlock("map.rev.init");
    auto* revCondBlock = _builder.createBlock("map.rev.cond");
    auto* revBodyBlock = _builder.createBlock("map.rev.body");
    auto* endBlock = _builder.createBlock("map.end");

    _builder.createBr(condBlock);

    // Phase 1: Check if source list is Cons
    _builder.setInsertPoint(condBlock);
    auto* srcLoad = _builder.createLoad(srcStorage, "map.src.load");
    auto* srcTag = _builder.createObjGetTag(srcLoad, "map.src.tag");
    auto* isCons = _builder.createNCmpEQ(srcTag, tag1, "map.is_cons");
    _builder.createCondBr(isCons, bodyBlock, revInitBlock);

    // Phase 1: Extract head, apply function, cons onto reversed accumulator
    _builder.setInsertPoint(bodyBlock);
    auto* srcForHead = _builder.createLoad(srcStorage, "map.src.for_head");
    auto* head = _builder.createObjGetSlot(srcForHead, slot0, "map.head");
    _builder.createStore(elemAlloca, head);

    // Advance source cursor to tail (separate load)
    auto* srcForTail = _builder.createLoad(srcStorage, "map.src.for_tail");
    auto* tail = _builder.createObjGetSlot(srcForTail, slot1, "map.tail");
    _builder.createStore(srcStorage, tail);

    // Apply function to element
    auto* elemLoad = _builder.createLoad(elemAlloca, "map.elem.load");
    // Propagate element type annotation through load (for field access in lambda body)
    if (auto elemTypeId = getObjectTypeId(elemAlloca))
        annotateObjectTypeId(elemLoad, *elemTypeId);
    generateFSharpCall(func, funcName, { elemLoad });
    auto* mapped = _result;
    if (!mapped)
    {
        reportTypeError("map: failed to apply function to element");
        return;
    }

    // Capture the mapped element's type info for annotating the output list.
    // getObjectTypeId checks annotations; tryGetObjectInfo traces the IR chain (ObjSetSlot → ObjAlloc)
    // as a fallback — needed because emitTuple2/emitTuple3 don't annotate their results.
    auto const mappedLiteralType = mapped->type();
    auto mappedObjTypeId = getObjectTypeId(mapped);
    if (!mappedObjTypeId)
    {
        if (auto info = tryGetObjectInfo(mapped))
            mappedObjTypeId = static_cast<uint16_t>(info->typeId);
    }

    // Store mapped value and accumulator in temp allocas to survive ObjAlloc
    auto* mappedTmp = createAllocaInEntryBlock(mapped->type(), "map.mapped.tmp");
    _builder.createStore(mappedTmp, mapped);
    auto* accTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "map.acc.tmp");
    auto* accForCons = _builder.createLoad(accStorage, "map.acc.for_cons");
    _builder.createStore(accTmp, accForCons);

    // Create Cons cell: tag=1, slot[0]=mapped, slot[1]=oldAcc
    auto* mappedReload = _builder.createLoad(mappedTmp, "map.mapped.reload");
    auto* accReload = _builder.createLoad(accTmp, "map.acc.reload");
    auto* cons = emitListCons(mappedReload, accReload, mappedLiteralType, "map.cons");

    _builder.createStore(accStorage, cons);
    _builder.createBr(condBlock);

    // Phase 2: Reverse the accumulated list
    auto* revSrcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "map.rev.src");
    auto* revAccStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "map.rev.acc");

    _builder.setInsertPoint(revInitBlock);
    auto* revSrcInit = _builder.createLoad(accStorage, "map.rev.src.init");
    _builder.createStore(revSrcStorage, revSrcInit);
    auto* revNil = emitNilList(CoreVM::LiteralType::Void, "map.rev.nil");
    _builder.createStore(revAccStorage, revNil);
    _builder.createBr(revCondBlock);

    _builder.setInsertPoint(revCondBlock);
    auto* revSrcLoad = _builder.createLoad(revSrcStorage, "map.rev.src.load");
    auto* revSrcTag = _builder.createObjGetTag(revSrcLoad, "map.rev.src.tag");
    auto* revIsCons = _builder.createNCmpEQ(revSrcTag, tag1, "map.rev.is_cons");
    _builder.createCondBr(revIsCons, revBodyBlock, endBlock);

    _builder.setInsertPoint(revBodyBlock);
    auto* revSrcForHead = _builder.createLoad(revSrcStorage, "map.rev.src.for_head");
    auto* revHead = _builder.createObjGetSlot(revSrcForHead, slot0, "map.rev.head");
    auto* revSrcForTail = _builder.createLoad(revSrcStorage, "map.rev.src.for_tail");
    auto* revTail = _builder.createObjGetSlot(revSrcForTail, slot1, "map.rev.tail");
    _builder.createStore(revSrcStorage, revTail);

    auto* revElemTmp = createAllocaInEntryBlock(mappedLiteralType, "map.rev.elem.tmp");
    _builder.createStore(revElemTmp, revHead);
    auto* revAccTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "map.rev.acc.tmp");
    auto* revAccForCons = _builder.createLoad(revAccStorage, "map.rev.acc.for_cons");
    _builder.createStore(revAccTmp, revAccForCons);

    auto* revElemReload = _builder.createLoad(revElemTmp, "map.rev.elem.reload");
    auto* revAccReload = _builder.createLoad(revAccTmp, "map.rev.acc.reload");
    auto* revCons = emitListCons(revElemReload, revAccReload, mappedLiteralType, "map.rev.cons");

    _builder.createStore(revAccStorage, revCons);
    _builder.createBr(revCondBlock);

    _builder.setInsertPoint(endBlock);
    _result = _builder.createLoad(revAccStorage, "map.result");
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
    // Annotate the output list with the mapped element type (may differ from input)
    if (mappedObjTypeId)
        annotateListElementTypeId(_result, *mappedObjTypeId);
    annotateListElementLiteralType(_result, mappedLiteralType);
}

void IRGenerator::generateFilterIR(std::string const& predParamName, CoreVM::Value* listValue)
{
    auto* tag1 = _builder.get(CoreVM::CoreNumber(1));  // Cons
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0)); // head
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1)); // tail

    // Resolve the predicate function
    auto predName = predParamName;
    if (auto ref = lookupFSharpFunctionRef(predParamName))
        predName = *ref;
    auto const* pred = lookupFSharpFunction(predName);
    if (!pred)
    {
        reportTypeError("filter: predicate argument '{}' not found", std::string_view(predParamName));
        return;
    }

    // Allocas
    auto* srcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "filter.src");
    _builder.createStore(srcStorage, listValue);

    auto* accStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "filter.acc");
    auto* nil = emitNilList(CoreVM::LiteralType::Void, "filter.nil");
    _builder.createStore(accStorage, nil);

    auto const filterElemType = getListElementLiteralType(listValue).value_or(CoreVM::LiteralType::Number);
    auto* elemAlloca = createAllocaInEntryBlock(filterElemType, "filter.elem");

    // Propagate list element type to extracted elements (e.g., ProcessInfo for ps)
    if (auto elemTypeId = getListElementTypeId(listValue))
    {
        annotateObjectTypeId(elemAlloca, *elemTypeId);
        // Also annotate srcStorage so the result list inherits element type
        annotateListElementTypeId(srcStorage, *elemTypeId);
    }
    if (auto elt = getListElementLiteralType(listValue))
        annotateListElementLiteralType(srcStorage, *elt);

    // Create blocks
    auto* condBlock = _builder.createBlock("filter.cond");
    auto* bodyBlock = _builder.createBlock("filter.body");
    auto* consBlock = _builder.createBlock("filter.cons");
    auto* revInitBlock = _builder.createBlock("filter.rev.init");
    auto* revCondBlock = _builder.createBlock("filter.rev.cond");
    auto* revBodyBlock = _builder.createBlock("filter.rev.body");
    auto* endBlock = _builder.createBlock("filter.end");

    _builder.createBr(condBlock);

    // Phase 1: Check if source list is Cons
    _builder.setInsertPoint(condBlock);
    auto* srcLoad = _builder.createLoad(srcStorage, "filter.src.load");
    auto* srcTag = _builder.createObjGetTag(srcLoad, "filter.src.tag");
    auto* isCons = _builder.createNCmpEQ(srcTag, tag1, "filter.is_cons");
    _builder.createCondBr(isCons, bodyBlock, revInitBlock);

    // Phase 1: Extract head, apply predicate
    _builder.setInsertPoint(bodyBlock);
    auto* srcForHead = _builder.createLoad(srcStorage, "filter.src.for_head");
    auto* head = _builder.createObjGetSlot(srcForHead, slot0, "filter.head");
    _builder.createStore(elemAlloca, head);

    // Advance source cursor to tail
    auto* srcForTail = _builder.createLoad(srcStorage, "filter.src.for_tail");
    auto* tail = _builder.createObjGetSlot(srcForTail, slot1, "filter.tail");
    _builder.createStore(srcStorage, tail);

    // Apply predicate to element
    auto* elemLoad = _builder.createLoad(elemAlloca, "filter.elem.load");
    // Propagate element type annotation through load (for field access in lambda body)
    if (auto elemTypeId = getObjectTypeId(elemAlloca))
        annotateObjectTypeId(elemLoad, *elemTypeId);
    generateFSharpCall(pred, predName, { elemLoad });
    auto* predResult = _result;
    if (!predResult)
    {
        reportTypeError("filter: failed to apply predicate to element");
        return;
    }
    _builder.createCondBr(toBool(predResult), consBlock, condBlock);

    // Phase 1: Cons block — element passed filter, prepend to accumulator
    _builder.setInsertPoint(consBlock);
    auto* elemForCons = _builder.createLoad(elemAlloca, "filter.elem.for_cons");
    auto* accTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "filter.acc.tmp");
    auto* accForCons = _builder.createLoad(accStorage, "filter.acc.for_cons");
    _builder.createStore(accTmp, accForCons);
    auto* elemTmp = createAllocaInEntryBlock(filterElemType, "filter.elem.tmp");
    _builder.createStore(elemTmp, elemForCons);

    auto* elemReload = _builder.createLoad(elemTmp, "filter.elem.reload");
    auto* accReload = _builder.createLoad(accTmp, "filter.acc.reload");
    auto* cons = emitListCons(elemReload, accReload, CoreVM::LiteralType::Void, "filter.cons");

    _builder.createStore(accStorage, cons);
    _builder.createBr(condBlock);

    // Phase 2: Reverse the accumulated list
    auto* revSrcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "filter.rev.src");
    auto* revAccStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "filter.rev.acc");

    _builder.setInsertPoint(revInitBlock);
    auto* revSrcInit = _builder.createLoad(accStorage, "filter.rev.src.init");
    _builder.createStore(revSrcStorage, revSrcInit);
    auto* revNil = emitNilList(CoreVM::LiteralType::Void, "filter.rev.nil");
    _builder.createStore(revAccStorage, revNil);
    _builder.createBr(revCondBlock);

    _builder.setInsertPoint(revCondBlock);
    auto* revSrcLoad = _builder.createLoad(revSrcStorage, "filter.rev.src.load");
    auto* revSrcTag = _builder.createObjGetTag(revSrcLoad, "filter.rev.src.tag");
    auto* revIsCons = _builder.createNCmpEQ(revSrcTag, tag1, "filter.rev.is_cons");
    _builder.createCondBr(revIsCons, revBodyBlock, endBlock);

    _builder.setInsertPoint(revBodyBlock);
    auto* revSrcForHead = _builder.createLoad(revSrcStorage, "filter.rev.src.for_head");
    auto* revHead = _builder.createObjGetSlot(revSrcForHead, slot0, "filter.rev.head");
    auto* revSrcForTail = _builder.createLoad(revSrcStorage, "filter.rev.src.for_tail");
    auto* revTail = _builder.createObjGetSlot(revSrcForTail, slot1, "filter.rev.tail");
    _builder.createStore(revSrcStorage, revTail);

    auto* revElemTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Void, "filter.rev.elem.tmp");
    _builder.createStore(revElemTmp, revHead);
    auto* revAccTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "filter.rev.acc.tmp");
    auto* revAccForCons = _builder.createLoad(revAccStorage, "filter.rev.acc.for_cons");
    _builder.createStore(revAccTmp, revAccForCons);

    auto* revElemReload = _builder.createLoad(revElemTmp, "filter.rev.elem.reload");
    auto* revAccReload = _builder.createLoad(revAccTmp, "filter.rev.acc.reload");
    auto* revCons = emitListCons(revElemReload, revAccReload, CoreVM::LiteralType::Void, "filter.rev.cons");

    _builder.createStore(revAccStorage, revCons);
    _builder.createBr(revCondBlock);

    _builder.setInsertPoint(endBlock);
    _result = _builder.createLoad(revAccStorage, "filter.result");
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
    // Propagate list element type through filter (same element type as input)
    if (auto elemTypeId = getListElementTypeId(listValue))
        annotateListElementTypeId(_result, *elemTypeId);
    if (auto elt = getListElementLiteralType(listValue))
        annotateListElementLiteralType(_result, *elt);
}

void IRGenerator::generateFoldIR(CoreVM::Value* initValue,
                                 std::string const& funcParamName,
                                 CoreVM::Value* listValue)
{
    auto* tag1 = _builder.get(CoreVM::CoreNumber(1));  // Cons
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0)); // head
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1)); // tail

    // Resolve the function to call
    auto funcName = funcParamName;
    if (auto ref = lookupFSharpFunctionRef(funcParamName))
        funcName = *ref;
    auto const* func = lookupFSharpFunction(funcName);
    if (!func)
    {
        reportTypeError("fold: function argument '{}' not found", std::string_view(funcParamName));
        return;
    }

    // Allocas
    auto* srcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "fold.src");
    _builder.createStore(srcStorage, listValue);

    auto* accStorage = createAllocaInEntryBlock(initValue->type(), "fold.acc");
    _builder.createStore(accStorage, initValue);

    auto const foldElemType = getListElementLiteralType(listValue).value_or(CoreVM::LiteralType::Number);
    auto* elemAlloca = createAllocaInEntryBlock(foldElemType, "fold.elem");

    // Propagate list element type to extracted elements
    if (auto elemTypeId = getListElementTypeId(listValue))
        annotateObjectTypeId(elemAlloca, *elemTypeId);

    // Create blocks
    auto* condBlock = _builder.createBlock("fold.cond");
    auto* bodyBlock = _builder.createBlock("fold.body");
    auto* endBlock = _builder.createBlock("fold.end");

    _builder.createBr(condBlock);

    // Condition: check if source list is Cons
    _builder.setInsertPoint(condBlock);
    auto* srcLoad = _builder.createLoad(srcStorage, "fold.src.load");
    auto* srcTag = _builder.createObjGetTag(srcLoad, "fold.src.tag");
    auto* isCons = _builder.createNCmpEQ(srcTag, tag1, "fold.is_cons");
    _builder.createCondBr(isCons, bodyBlock, endBlock);

    // Body: extract head, apply function(acc, elem), update acc
    _builder.setInsertPoint(bodyBlock);
    auto* srcForHead = _builder.createLoad(srcStorage, "fold.src.for_head");
    auto* head = _builder.createObjGetSlot(srcForHead, slot0, "fold.head");
    _builder.createStore(elemAlloca, head);

    // Advance source cursor
    auto* srcForTail = _builder.createLoad(srcStorage, "fold.src.for_tail");
    auto* tail = _builder.createObjGetSlot(srcForTail, slot1, "fold.tail");
    _builder.createStore(srcStorage, tail);

    // Apply function to (acc, elem)
    auto* accLoad = _builder.createLoad(accStorage, "fold.acc.load");
    auto* elemLoad = _builder.createLoad(elemAlloca, "fold.elem.load");
    if (auto elemTypeId = getObjectTypeId(elemAlloca))
        annotateObjectTypeId(elemLoad, *elemTypeId);
    generateFSharpCall(func, funcName, { accLoad, elemLoad });
    auto* newAcc = _result;
    if (!newAcc)
    {
        reportTypeError("fold: failed to apply function");
        return;
    }
    _builder.createStore(accStorage, newAcc);
    _builder.createBr(condBlock);

    // End: return final accumulator
    _builder.setInsertPoint(endBlock);
    _result = _builder.createLoad(accStorage, "fold.result");
}

void IRGenerator::generateReduceIR(std::string const& funcParamName, CoreVM::Value* listValue)
{
    auto* tag1 = _builder.get(CoreVM::CoreNumber(1));  // Cons
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0)); // head
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1)); // tail

    // Resolve the function to call
    auto funcName = funcParamName;
    if (auto ref = lookupFSharpFunctionRef(funcParamName))
        funcName = *ref;
    auto const* func = lookupFSharpFunction(funcName);
    if (!func)
    {
        reportTypeError("reduce: function argument '{}' not found", std::string_view(funcParamName));
        return;
    }

    // Allocas
    auto* srcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "reduce.src");
    _builder.createStore(srcStorage, listValue);

    auto const reduceElemType = getListElementLiteralType(listValue).value_or(CoreVM::LiteralType::Number);
    auto* accStorage = createAllocaInEntryBlock(reduceElemType, "reduce.acc");
    auto* resultStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "reduce.result");
    auto* elemAlloca = createAllocaInEntryBlock(reduceElemType, "reduce.elem");

    // Propagate list element type to extracted elements
    if (auto elemTypeId = getListElementTypeId(listValue))
        annotateObjectTypeId(elemAlloca, *elemTypeId);

    // Create blocks
    auto* checkEmptyBlock = _builder.createBlock("reduce.check_empty");
    auto* initBlock = _builder.createBlock("reduce.init");
    auto* condBlock = _builder.createBlock("reduce.cond");
    auto* bodyBlock = _builder.createBlock("reduce.body");
    auto* noneBlock = _builder.createBlock("reduce.none");
    auto* someBlock = _builder.createBlock("reduce.some");
    auto* endBlock = _builder.createBlock("reduce.end");

    _builder.createBr(checkEmptyBlock);

    // Check if list is empty
    _builder.setInsertPoint(checkEmptyBlock);
    auto* srcLoad = _builder.createLoad(srcStorage, "reduce.src.load");
    auto* srcTag = _builder.createObjGetTag(srcLoad, "reduce.src.tag");
    auto* isCons = _builder.createNCmpEQ(srcTag, tag1, "reduce.is_cons");
    _builder.createCondBr(isCons, initBlock, noneBlock);

    // Empty list → return None
    _builder.setInsertPoint(noneBlock);
    auto* noneVal = emitNoneOption("reduce.none");
    _builder.createStore(resultStorage, noneVal);
    _builder.createBr(endBlock);

    // Non-empty: use first element as initial accumulator
    _builder.setInsertPoint(initBlock);
    auto* srcForHead = _builder.createLoad(srcStorage, "reduce.src.for_head");
    auto* firstElem = _builder.createObjGetSlot(srcForHead, slot0, "reduce.first");
    _builder.createStore(accStorage, firstElem);

    // Advance to tail
    auto* srcForTail = _builder.createLoad(srcStorage, "reduce.src.for_tail");
    auto* tailVal = _builder.createObjGetSlot(srcForTail, slot1, "reduce.tail");
    _builder.createStore(srcStorage, tailVal);
    _builder.createBr(condBlock);

    // Fold loop: condition
    _builder.setInsertPoint(condBlock);
    auto* srcLoad2 = _builder.createLoad(srcStorage, "reduce.src.load2");
    auto* srcTag2 = _builder.createObjGetTag(srcLoad2, "reduce.src.tag2");
    auto* isCons2 = _builder.createNCmpEQ(srcTag2, tag1, "reduce.is_cons2");
    _builder.createCondBr(isCons2, bodyBlock, someBlock);

    // Fold loop: body
    _builder.setInsertPoint(bodyBlock);
    auto* srcForHead2 = _builder.createLoad(srcStorage, "reduce.src.for_head2");
    auto* head = _builder.createObjGetSlot(srcForHead2, slot0, "reduce.head");
    _builder.createStore(elemAlloca, head);

    auto* srcForTail2 = _builder.createLoad(srcStorage, "reduce.src.for_tail2");
    auto* tail2 = _builder.createObjGetSlot(srcForTail2, slot1, "reduce.tail2");
    _builder.createStore(srcStorage, tail2);

    auto* accLoad = _builder.createLoad(accStorage, "reduce.acc.load");
    auto* elemLoad = _builder.createLoad(elemAlloca, "reduce.elem.load");
    if (auto elemTypeId = getObjectTypeId(elemAlloca))
        annotateObjectTypeId(elemLoad, *elemTypeId);
    generateFSharpCall(func, funcName, { accLoad, elemLoad });
    auto* newAcc = _result;
    if (!newAcc)
    {
        reportTypeError("reduce: failed to apply function");
        return;
    }
    _builder.createStore(accStorage, newAcc);
    _builder.createBr(condBlock);

    // Wrap result in Some
    _builder.setInsertPoint(someBlock);
    auto* finalAcc = _builder.createLoad(accStorage, "reduce.final_acc");
    auto* accTmp = createAllocaInEntryBlock(reduceElemType, "reduce.acc.tmp");
    _builder.createStore(accTmp, finalAcc);

    auto* accReload = _builder.createLoad(accTmp, "reduce.acc.reload");
    auto* someVal = emitSomeOption(accReload, CoreVM::LiteralType::Void, "reduce.some");

    _builder.createStore(resultStorage, someVal);
    _builder.createBr(endBlock);

    // End: return result
    _builder.setInsertPoint(endBlock);
    _result = _builder.createLoad(resultStorage, "reduce.result");
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::Option);
}

void IRGenerator::generateReverseIR(CoreVM::Value* listValue)
{
    auto* tag1 = _builder.get(CoreVM::CoreNumber(1));  // Cons
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0)); // head
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1)); // tail

    // Allocas
    auto* srcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "rev.src");
    _builder.createStore(srcStorage, listValue);

    auto* accStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "rev.acc");
    auto* nil = emitNilList(CoreVM::LiteralType::Void, "rev.nil");
    _builder.createStore(accStorage, nil);

    // Create blocks
    auto* condBlock = _builder.createBlock("rev.cond");
    auto* bodyBlock = _builder.createBlock("rev.body");
    auto* endBlock = _builder.createBlock("rev.end");

    _builder.createBr(condBlock);

    // Condition: check if source list is Cons
    _builder.setInsertPoint(condBlock);
    auto* srcLoad = _builder.createLoad(srcStorage, "rev.src.load");
    auto* srcTag = _builder.createObjGetTag(srcLoad, "rev.src.tag");
    auto* isCons = _builder.createNCmpEQ(srcTag, tag1, "rev.is_cons");
    _builder.createCondBr(isCons, bodyBlock, endBlock);

    // Body: extract head, cons onto accumulator (naturally reverses)
    _builder.setInsertPoint(bodyBlock);
    auto* srcForHead = _builder.createLoad(srcStorage, "rev.src.for_head");
    auto* head = _builder.createObjGetSlot(srcForHead, slot0, "rev.head");

    // Advance source cursor
    auto* srcForTail = _builder.createLoad(srcStorage, "rev.src.for_tail");
    auto* tail = _builder.createObjGetSlot(srcForTail, slot1, "rev.tail");
    _builder.createStore(srcStorage, tail);

    // Store head and acc in temp allocas to survive ObjAlloc
    auto* elemTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Void, "rev.elem.tmp");
    _builder.createStore(elemTmp, head);
    auto* accTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "rev.acc.tmp");
    auto* accForCons = _builder.createLoad(accStorage, "rev.acc.for_cons");
    _builder.createStore(accTmp, accForCons);

    // Create Cons cell: tag=1, slot[0]=head, slot[1]=oldAcc
    auto* elemReload = _builder.createLoad(elemTmp, "rev.elem.reload");
    auto* accReload = _builder.createLoad(accTmp, "rev.acc.reload");
    auto* cons = emitListCons(elemReload, accReload, CoreVM::LiteralType::Void, "rev.cons");

    _builder.createStore(accStorage, cons);
    _builder.createBr(condBlock);

    // End: return reversed list
    _builder.setInsertPoint(endBlock);
    _result = _builder.createLoad(accStorage, "rev.result");
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
    if (auto elemTypeId = getListElementTypeId(listValue))
        annotateListElementTypeId(_result, *elemTypeId);
    if (auto elt = getListElementLiteralType(listValue))
        annotateListElementLiteralType(_result, *elt);
}

void IRGenerator::generateFindIR(std::string const& predParamName, CoreVM::Value* listValue)
{
    auto* tag1 = _builder.get(CoreVM::CoreNumber(1));  // Cons
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0)); // head
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1)); // tail

    // Resolve predicate function
    auto predName = predParamName;
    if (auto ref = lookupFSharpFunctionRef(predParamName))
        predName = *ref;
    auto const* pred = lookupFSharpFunction(predName);
    if (!pred)
    {
        reportTypeError("find: predicate argument '{}' not found", std::string_view(predParamName));
        return;
    }

    // Allocas
    auto* srcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "find.src");
    _builder.createStore(srcStorage, listValue);
    auto const findElemType = getListElementLiteralType(listValue).value_or(CoreVM::LiteralType::Number);
    auto* elemAlloca = createAllocaInEntryBlock(findElemType, "find.elem");
    auto* resultStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "find.result");

    // Propagate list element type to extracted elements
    if (auto elemTypeId = getListElementTypeId(listValue))
        annotateObjectTypeId(elemAlloca, *elemTypeId);

    // Create blocks
    auto* condBlock = _builder.createBlock("find.cond");
    auto* bodyBlock = _builder.createBlock("find.body");
    auto* foundBlock = _builder.createBlock("find.found");
    auto* noneBlock = _builder.createBlock("find.none");
    auto* endBlock = _builder.createBlock("find.end");

    _builder.createBr(condBlock);

    // Condition: check if source list is Cons
    _builder.setInsertPoint(condBlock);
    auto* srcLoad = _builder.createLoad(srcStorage, "find.src.load");
    auto* srcTag = _builder.createObjGetTag(srcLoad, "find.src.tag");
    auto* isCons = _builder.createNCmpEQ(srcTag, tag1, "find.is_cons");
    _builder.createCondBr(isCons, bodyBlock, noneBlock);

    // Body: extract head, apply predicate
    _builder.setInsertPoint(bodyBlock);
    auto* srcForHead = _builder.createLoad(srcStorage, "find.src.for_head");
    auto* head = _builder.createObjGetSlot(srcForHead, slot0, "find.head");
    _builder.createStore(elemAlloca, head);

    // Advance source cursor to tail
    auto* srcForTail = _builder.createLoad(srcStorage, "find.src.for_tail");
    auto* tail = _builder.createObjGetSlot(srcForTail, slot1, "find.tail");
    _builder.createStore(srcStorage, tail);

    // Apply predicate to element
    auto* elemLoad = _builder.createLoad(elemAlloca, "find.elem.load");
    if (auto elemTypeId = getObjectTypeId(elemAlloca))
        annotateObjectTypeId(elemLoad, *elemTypeId);
    generateFSharpCall(pred, predName, { elemLoad });
    auto* predResult = _result;
    if (!predResult)
    {
        reportTypeError("find: failed to apply predicate to element");
        return;
    }
    _builder.createCondBr(toBool(predResult), foundBlock, condBlock);

    // Found: wrap element in Some
    _builder.setInsertPoint(foundBlock);
    auto* foundElem = _builder.createLoad(elemAlloca, "find.found.elem");
    auto* elemTmp = createAllocaInEntryBlock(findElemType, "find.elem.tmp");
    _builder.createStore(elemTmp, foundElem);

    auto* elemReload = _builder.createLoad(elemTmp, "find.elem.reload");
    auto* someVal = emitSomeOption(elemReload, CoreVM::LiteralType::Void, "find.some");
    _builder.createStore(resultStorage, someVal);
    _builder.createBr(endBlock);

    // None: list exhausted
    _builder.setInsertPoint(noneBlock);
    auto* noneVal = emitNoneOption("find.none");
    _builder.createStore(resultStorage, noneVal);
    _builder.createBr(endBlock);

    // End
    _builder.setInsertPoint(endBlock);
    _result = _builder.createLoad(resultStorage, "find.result");
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::Option);
    if (auto elemTypeId = getListElementTypeId(listValue))
        annotateInnerObjectTypeId(_result, *elemTypeId);
}

void IRGenerator::generateExistsIR(std::string const& predParamName, CoreVM::Value* listValue)
{
    auto* tag1 = _builder.get(CoreVM::CoreNumber(1));  // Cons
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0)); // head
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1)); // tail

    // Resolve predicate function
    auto predName = predParamName;
    if (auto ref = lookupFSharpFunctionRef(predParamName))
        predName = *ref;
    auto const* pred = lookupFSharpFunction(predName);
    if (!pred)
    {
        reportTypeError("exists: predicate argument '{}' not found", std::string_view(predParamName));
        return;
    }

    // Allocas
    auto* srcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "exists.src");
    _builder.createStore(srcStorage, listValue);
    auto const existsElemType = getListElementLiteralType(listValue).value_or(CoreVM::LiteralType::Number);
    auto* elemAlloca = createAllocaInEntryBlock(existsElemType, "exists.elem");
    auto* resultStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Boolean, "exists.result");

    // Propagate list element type to extracted elements
    if (auto elemTypeId = getListElementTypeId(listValue))
        annotateObjectTypeId(elemAlloca, *elemTypeId);

    // Create blocks
    auto* condBlock = _builder.createBlock("exists.cond");
    auto* bodyBlock = _builder.createBlock("exists.body");
    auto* trueBlock = _builder.createBlock("exists.true");
    auto* falseBlock = _builder.createBlock("exists.false");
    auto* endBlock = _builder.createBlock("exists.end");

    _builder.createBr(condBlock);

    // Condition: check if source list is Cons
    _builder.setInsertPoint(condBlock);
    auto* srcLoad = _builder.createLoad(srcStorage, "exists.src.load");
    auto* srcTag = _builder.createObjGetTag(srcLoad, "exists.src.tag");
    auto* isCons = _builder.createNCmpEQ(srcTag, tag1, "exists.is_cons");
    _builder.createCondBr(isCons, bodyBlock, falseBlock);

    // Body: extract head, apply predicate
    _builder.setInsertPoint(bodyBlock);
    auto* srcForHead = _builder.createLoad(srcStorage, "exists.src.for_head");
    auto* head = _builder.createObjGetSlot(srcForHead, slot0, "exists.head");
    _builder.createStore(elemAlloca, head);

    auto* srcForTail = _builder.createLoad(srcStorage, "exists.src.for_tail");
    auto* tail = _builder.createObjGetSlot(srcForTail, slot1, "exists.tail");
    _builder.createStore(srcStorage, tail);

    auto* elemLoad = _builder.createLoad(elemAlloca, "exists.elem.load");
    if (auto elemTypeId = getObjectTypeId(elemAlloca))
        annotateObjectTypeId(elemLoad, *elemTypeId);
    generateFSharpCall(pred, predName, { elemLoad });
    auto* predResult = _result;
    if (!predResult)
    {
        reportTypeError("exists: failed to apply predicate to element");
        return;
    }
    _builder.createCondBr(toBool(predResult), trueBlock, condBlock);

    // True: predicate matched
    _builder.setInsertPoint(trueBlock);
    _builder.createStore(resultStorage, _builder.getBoolean(true));
    _builder.createBr(endBlock);

    // False: list exhausted
    _builder.setInsertPoint(falseBlock);
    _builder.createStore(resultStorage, _builder.getBoolean(false));
    _builder.createBr(endBlock);

    // End
    _builder.setInsertPoint(endBlock);
    _result = _builder.createLoad(resultStorage, "exists.result");
}

void IRGenerator::generateForallIR(std::string const& predParamName, CoreVM::Value* listValue)
{
    auto* tag1 = _builder.get(CoreVM::CoreNumber(1));  // Cons
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0)); // head
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1)); // tail

    // Resolve predicate function
    auto predName = predParamName;
    if (auto ref = lookupFSharpFunctionRef(predParamName))
        predName = *ref;
    auto const* pred = lookupFSharpFunction(predName);
    if (!pred)
    {
        reportTypeError("forall: predicate argument '{}' not found", std::string_view(predParamName));
        return;
    }

    // Allocas
    auto* srcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "forall.src");
    _builder.createStore(srcStorage, listValue);
    auto const forallElemType = getListElementLiteralType(listValue).value_or(CoreVM::LiteralType::Number);
    auto* elemAlloca = createAllocaInEntryBlock(forallElemType, "forall.elem");
    auto* resultStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Boolean, "forall.result");

    // Propagate list element type to extracted elements
    if (auto elemTypeId = getListElementTypeId(listValue))
        annotateObjectTypeId(elemAlloca, *elemTypeId);

    // Create blocks
    auto* condBlock = _builder.createBlock("forall.cond");
    auto* bodyBlock = _builder.createBlock("forall.body");
    auto* trueBlock = _builder.createBlock("forall.true");
    auto* falseBlock = _builder.createBlock("forall.false");
    auto* endBlock = _builder.createBlock("forall.end");

    _builder.createBr(condBlock);

    // Condition: check if source list is Cons
    _builder.setInsertPoint(condBlock);
    auto* srcLoad = _builder.createLoad(srcStorage, "forall.src.load");
    auto* srcTag = _builder.createObjGetTag(srcLoad, "forall.src.tag");
    auto* isCons = _builder.createNCmpEQ(srcTag, tag1, "forall.is_cons");
    _builder.createCondBr(isCons, bodyBlock, trueBlock);

    // Body: extract head, apply predicate
    _builder.setInsertPoint(bodyBlock);
    auto* srcForHead = _builder.createLoad(srcStorage, "forall.src.for_head");
    auto* head = _builder.createObjGetSlot(srcForHead, slot0, "forall.head");
    _builder.createStore(elemAlloca, head);

    auto* srcForTail = _builder.createLoad(srcStorage, "forall.src.for_tail");
    auto* tail = _builder.createObjGetSlot(srcForTail, slot1, "forall.tail");
    _builder.createStore(srcStorage, tail);

    auto* elemLoad = _builder.createLoad(elemAlloca, "forall.elem.load");
    if (auto elemTypeId = getObjectTypeId(elemAlloca))
        annotateObjectTypeId(elemLoad, *elemTypeId);
    generateFSharpCall(pred, predName, { elemLoad });
    auto* predResult = _result;
    if (!predResult)
    {
        reportTypeError("forall: failed to apply predicate to element");
        return;
    }
    // If predicate is false, short-circuit to falseBlock
    _builder.createCondBr(toBool(predResult), condBlock, falseBlock);

    // True: all elements matched
    _builder.setInsertPoint(trueBlock);
    _builder.createStore(resultStorage, _builder.getBoolean(true));
    _builder.createBr(endBlock);

    // False: predicate failed for an element
    _builder.setInsertPoint(falseBlock);
    _builder.createStore(resultStorage, _builder.getBoolean(false));
    _builder.createBr(endBlock);

    // End
    _builder.setInsertPoint(endBlock);
    _result = _builder.createLoad(resultStorage, "forall.result");
}

void IRGenerator::generateEachIR(std::string const& funcParamName, CoreVM::Value* listValue)
{
    auto* tag1 = _builder.get(CoreVM::CoreNumber(1));  // Cons
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0)); // head
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1)); // tail

    // Resolve function
    auto funcName = funcParamName;
    if (auto ref = lookupFSharpFunctionRef(funcParamName))
        funcName = *ref;

    // Check for builtin print/println as function argument
    bool const isPrintBuiltin = (funcName == "print" || funcName == "println");
    FSharpFunction const* func = nullptr;
    if (!isPrintBuiltin)
    {
        func = lookupFSharpFunction(funcName);
        if (!func)
        {
            reportTypeError("each: function argument '{}' not found", std::string_view(funcParamName));
            return;
        }
    }

    // Allocas
    auto* srcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "each.src");
    _builder.createStore(srcStorage, listValue);
    auto const eachElemType = getListElementLiteralType(listValue).value_or(CoreVM::LiteralType::Number);
    auto* elemAlloca = createAllocaInEntryBlock(eachElemType, "each.elem");

    // Propagate list element type to extracted elements
    if (auto elemTypeId = getListElementTypeId(listValue))
        annotateObjectTypeId(elemAlloca, *elemTypeId);

    // Create blocks
    auto* condBlock = _builder.createBlock("each.cond");
    auto* bodyBlock = _builder.createBlock("each.body");
    auto* endBlock = _builder.createBlock("each.end");

    _builder.createBr(condBlock);

    // Condition: check if source list is Cons
    _builder.setInsertPoint(condBlock);
    auto* srcLoad = _builder.createLoad(srcStorage, "each.src.load");
    auto* srcTag = _builder.createObjGetTag(srcLoad, "each.src.tag");
    auto* isCons = _builder.createNCmpEQ(srcTag, tag1, "each.is_cons");
    _builder.createCondBr(isCons, bodyBlock, endBlock);

    // Body: extract head, apply function, advance tail, loop
    _builder.setInsertPoint(bodyBlock);
    auto* srcForHead = _builder.createLoad(srcStorage, "each.src.for_head");
    auto* head = _builder.createObjGetSlot(srcForHead, slot0, "each.head");
    _builder.createStore(elemAlloca, head);

    auto* srcForTail = _builder.createLoad(srcStorage, "each.src.for_tail");
    auto* tail = _builder.createObjGetSlot(srcForTail, slot1, "each.tail");
    _builder.createStore(srcStorage, tail);

    auto* elemLoad = _builder.createLoad(elemAlloca, "each.elem.load");
    if (auto elemTypeId = getObjectTypeId(elemAlloca))
        annotateObjectTypeId(elemLoad, *elemTypeId);

    if (isPrintBuiltin)
    {
        // Directly call print/println builtin
        auto* strVal = convertToString(elemLoad, "each.elem");
        auto const sig = funcName == "println" ? "println(S)V" : "print(S)V";
        auto* callback = findCallback(sig);
        if (callback)
            _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { strVal }, funcName);
    }
    else
    {
        generateFSharpCall(func, funcName, { elemLoad });
        if (!_result)
        {
            reportTypeError("each: failed to apply function to element");
            return;
        }
    }
    // Discard function result (each is for side effects only)
    _builder.createBr(condBlock);

    // End: return unit
    _builder.setInsertPoint(endBlock);
    _result = _builder.get(CoreVM::CoreNumber(0));
}

void IRGenerator::generateTakeIR(CoreVM::Value* countValue, CoreVM::Value* listValue)
{
    auto* tag1 = _builder.get(CoreVM::CoreNumber(1));  // Cons
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0)); // head
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1)); // tail
    auto* one = _builder.get(CoreVM::CoreNumber(1));
    auto* zero = _builder.get(CoreVM::CoreNumber(0));

    // Allocas
    auto* srcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "take.src");
    _builder.createStore(srcStorage, listValue);

    auto* counterStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Number, "take.counter");
    _builder.createStore(counterStorage, countValue);

    auto* accStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "take.acc");
    auto* nil = emitNilList(CoreVM::LiteralType::Void, "take.nil");
    _builder.createStore(accStorage, nil);

    // Create blocks
    auto* condBlock = _builder.createBlock("take.cond");
    auto* bodyBlock = _builder.createBlock("take.body");
    auto* revInitBlock = _builder.createBlock("take.rev.init");
    auto* revCondBlock = _builder.createBlock("take.rev.cond");
    auto* revBodyBlock = _builder.createBlock("take.rev.body");
    auto* endBlock = _builder.createBlock("take.end");

    _builder.createBr(condBlock);

    // Phase 1: Check if counter > 0 AND list is Cons
    _builder.setInsertPoint(condBlock);
    auto* counterLoad = _builder.createLoad(counterStorage, "take.counter.load");
    auto* counterGtZero = _builder.createNCmpGT(counterLoad, zero, "take.counter_gt_zero");
    auto* checkListBlock = _builder.createBlock("take.check_list");
    _builder.createCondBr(counterGtZero, checkListBlock, revInitBlock);

    _builder.setInsertPoint(checkListBlock);
    auto* srcLoad = _builder.createLoad(srcStorage, "take.src.load");
    auto* srcTag = _builder.createObjGetTag(srcLoad, "take.src.tag");
    auto* isCons = _builder.createNCmpEQ(srcTag, tag1, "take.is_cons");
    _builder.createCondBr(isCons, bodyBlock, revInitBlock);

    // Body: extract head, cons onto reversed accumulator, decrement counter
    _builder.setInsertPoint(bodyBlock);
    auto* srcForHead = _builder.createLoad(srcStorage, "take.src.for_head");
    auto* head = _builder.createObjGetSlot(srcForHead, slot0, "take.head");

    auto* srcForTail = _builder.createLoad(srcStorage, "take.src.for_tail");
    auto* tail = _builder.createObjGetSlot(srcForTail, slot1, "take.tail");
    _builder.createStore(srcStorage, tail);

    // Store head and acc in temp allocas to survive ObjAlloc
    auto* elemTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Void, "take.elem.tmp");
    _builder.createStore(elemTmp, head);
    auto* accTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "take.acc.tmp");
    auto* accForCons = _builder.createLoad(accStorage, "take.acc.for_cons");
    _builder.createStore(accTmp, accForCons);

    auto* elemReload = _builder.createLoad(elemTmp, "take.elem.reload");
    auto* accReload = _builder.createLoad(accTmp, "take.acc.reload");
    auto* cons = emitListCons(elemReload, accReload, CoreVM::LiteralType::Void, "take.cons");
    _builder.createStore(accStorage, cons);

    // Decrement counter
    auto* counterLoad2 = _builder.createLoad(counterStorage, "take.counter.load2");
    auto* counterDec = _builder.createSub(counterLoad2, one, "take.counter.dec");
    _builder.createStore(counterStorage, counterDec);

    _builder.createBr(condBlock);

    // Phase 2: Reverse the accumulated list
    auto* revSrcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "take.rev.src");
    auto* revAccStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "take.rev.acc");

    _builder.setInsertPoint(revInitBlock);
    auto* revSrcInit = _builder.createLoad(accStorage, "take.rev.src.init");
    _builder.createStore(revSrcStorage, revSrcInit);
    auto* revNil = emitNilList(CoreVM::LiteralType::Void, "take.rev.nil");
    _builder.createStore(revAccStorage, revNil);
    _builder.createBr(revCondBlock);

    _builder.setInsertPoint(revCondBlock);
    auto* revSrcLoad = _builder.createLoad(revSrcStorage, "take.rev.src.load");
    auto* revSrcTag = _builder.createObjGetTag(revSrcLoad, "take.rev.src.tag");
    auto* revIsCons = _builder.createNCmpEQ(revSrcTag, tag1, "take.rev.is_cons");
    _builder.createCondBr(revIsCons, revBodyBlock, endBlock);

    _builder.setInsertPoint(revBodyBlock);
    auto* revSrcForHead = _builder.createLoad(revSrcStorage, "take.rev.src.for_head");
    auto* revHead = _builder.createObjGetSlot(revSrcForHead, slot0, "take.rev.head");
    auto* revSrcForTail = _builder.createLoad(revSrcStorage, "take.rev.src.for_tail");
    auto* revTail = _builder.createObjGetSlot(revSrcForTail, slot1, "take.rev.tail");
    _builder.createStore(revSrcStorage, revTail);

    auto* revElemTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Void, "take.rev.elem.tmp");
    _builder.createStore(revElemTmp, revHead);
    auto* revAccTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "take.rev.acc.tmp");
    auto* revAccForCons = _builder.createLoad(revAccStorage, "take.rev.acc.for_cons");
    _builder.createStore(revAccTmp, revAccForCons);

    auto* revElemReload = _builder.createLoad(revElemTmp, "take.rev.elem.reload");
    auto* revAccReload = _builder.createLoad(revAccTmp, "take.rev.acc.reload");
    auto* revCons = emitListCons(revElemReload, revAccReload, CoreVM::LiteralType::Void, "take.rev.cons");

    _builder.createStore(revAccStorage, revCons);
    _builder.createBr(revCondBlock);

    _builder.setInsertPoint(endBlock);
    _result = _builder.createLoad(revAccStorage, "take.result");
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
    if (auto elemTypeId = getListElementTypeId(listValue))
        annotateListElementTypeId(_result, *elemTypeId);
    if (auto elt = getListElementLiteralType(listValue))
        annotateListElementLiteralType(_result, *elt);
}

void IRGenerator::generateDropIR(CoreVM::Value* countValue, CoreVM::Value* listValue)
{
    auto* tag1 = _builder.get(CoreVM::CoreNumber(1));  // Cons
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1)); // tail
    auto* one = _builder.get(CoreVM::CoreNumber(1));
    auto* zero = _builder.get(CoreVM::CoreNumber(0));

    // Allocas
    auto* srcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "drop.src");
    _builder.createStore(srcStorage, listValue);

    auto* counterStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Number, "drop.counter");
    _builder.createStore(counterStorage, countValue);

    // Create blocks
    auto* condBlock = _builder.createBlock("drop.cond");
    auto* bodyBlock = _builder.createBlock("drop.body");
    auto* endBlock = _builder.createBlock("drop.end");

    _builder.createBr(condBlock);

    // Condition: check counter > 0 AND list is Cons
    _builder.setInsertPoint(condBlock);
    auto* counterLoad = _builder.createLoad(counterStorage, "drop.counter.load");
    auto* counterGtZero = _builder.createNCmpGT(counterLoad, zero, "drop.counter_gt_zero");
    auto* checkListBlock = _builder.createBlock("drop.check_list");
    _builder.createCondBr(counterGtZero, checkListBlock, endBlock);

    _builder.setInsertPoint(checkListBlock);
    auto* srcLoad = _builder.createLoad(srcStorage, "drop.src.load");
    auto* srcTag = _builder.createObjGetTag(srcLoad, "drop.src.tag");
    auto* isCons = _builder.createNCmpEQ(srcTag, tag1, "drop.is_cons");
    _builder.createCondBr(isCons, bodyBlock, endBlock);

    // Body: advance to tail, decrement counter
    _builder.setInsertPoint(bodyBlock);
    auto* srcForTail = _builder.createLoad(srcStorage, "drop.src.for_tail");
    auto* tail = _builder.createObjGetSlot(srcForTail, slot1, "drop.tail");
    _builder.createStore(srcStorage, tail);

    auto* counterLoad2 = _builder.createLoad(counterStorage, "drop.counter.load2");
    auto* counterDec = _builder.createSub(counterLoad2, one, "drop.counter.dec");
    _builder.createStore(counterStorage, counterDec);

    _builder.createBr(condBlock);

    // End: return remaining list
    _builder.setInsertPoint(endBlock);
    _result = _builder.createLoad(srcStorage, "drop.result");
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
    if (auto elemTypeId = getListElementTypeId(listValue))
        annotateListElementTypeId(_result, *elemTypeId);
    if (auto elt = getListElementLiteralType(listValue))
        annotateListElementLiteralType(_result, *elt);
}

void IRGenerator::generateZipIR(CoreVM::Value* listA, CoreVM::Value* listB)
{
    auto* tag1 = _builder.get(CoreVM::CoreNumber(1));  // Cons
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0)); // head / fst
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1)); // tail / snd

    // Allocas for both source lists
    auto* srcAStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "zip.srcA");
    _builder.createStore(srcAStorage, listA);
    auto* srcBStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "zip.srcB");
    _builder.createStore(srcBStorage, listB);

    auto* accStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "zip.acc");
    auto* nil = emitNilList(CoreVM::LiteralType::Object, "zip.nil");
    _builder.createStore(accStorage, nil);

    // Temp allocas for head values (must survive ObjAlloc for tuple + cons)
    auto* headATmp = createAllocaInEntryBlock(CoreVM::LiteralType::Void, "zip.headA.tmp");
    auto* headBTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Void, "zip.headB.tmp");
    auto* tupleTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "zip.tuple.tmp");
    auto* accTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "zip.acc.tmp");

    // Create blocks
    auto* condBlock = _builder.createBlock("zip.cond");
    auto* bodyBlock = _builder.createBlock("zip.body");
    auto* revInitBlock = _builder.createBlock("zip.rev.init");
    auto* revCondBlock = _builder.createBlock("zip.rev.cond");
    auto* revBodyBlock = _builder.createBlock("zip.rev.body");
    auto* endBlock = _builder.createBlock("zip.end");

    _builder.createBr(condBlock);

    // Phase 1: Check if BOTH lists are Cons
    _builder.setInsertPoint(condBlock);
    auto* srcALoad = _builder.createLoad(srcAStorage, "zip.srcA.load");
    auto* srcATag = _builder.createObjGetTag(srcALoad, "zip.srcA.tag");
    auto* isConsA = _builder.createNCmpEQ(srcATag, tag1, "zip.is_consA");
    auto* checkBBlock = _builder.createBlock("zip.checkB");
    _builder.createCondBr(isConsA, checkBBlock, revInitBlock);

    _builder.setInsertPoint(checkBBlock);
    auto* srcBLoad = _builder.createLoad(srcBStorage, "zip.srcB.load");
    auto* srcBTag = _builder.createObjGetTag(srcBLoad, "zip.srcB.tag");
    auto* isConsB = _builder.createNCmpEQ(srcBTag, tag1, "zip.is_consB");
    _builder.createCondBr(isConsB, bodyBlock, revInitBlock);

    // Body: extract heads from both lists, build tuple, cons onto acc
    _builder.setInsertPoint(bodyBlock);
    // Extract head A
    auto* srcAForHead = _builder.createLoad(srcAStorage, "zip.srcA.for_head");
    auto* headA = _builder.createObjGetSlot(srcAForHead, slot0, "zip.headA");
    _builder.createStore(headATmp, headA);
    // Advance A to tail
    auto* srcAForTail = _builder.createLoad(srcAStorage, "zip.srcA.for_tail");
    auto* tailA = _builder.createObjGetSlot(srcAForTail, slot1, "zip.tailA");
    _builder.createStore(srcAStorage, tailA);

    // Extract head B
    auto* srcBForHead = _builder.createLoad(srcBStorage, "zip.srcB.for_head");
    auto* headB = _builder.createObjGetSlot(srcBForHead, slot0, "zip.headB");
    _builder.createStore(headBTmp, headB);
    // Advance B to tail
    auto* srcBForTail = _builder.createLoad(srcBStorage, "zip.srcB.for_tail");
    auto* tailB = _builder.createObjGetSlot(srcBForTail, slot1, "zip.tailB");
    _builder.createStore(srcBStorage, tailB);

    // Build Tuple2 object from headA, headB
    // Reload heads from temp allocas (they must survive ObjAlloc)
    auto* headAReload = _builder.createLoad(headATmp, "zip.headA.reload");
    auto* headATmp2 = createAllocaInEntryBlock(CoreVM::LiteralType::Void, "zip.headA.tmp2");
    _builder.createStore(headATmp2, headAReload);
    auto* headBReload = _builder.createLoad(headBTmp, "zip.headB.reload");
    auto* headBTmp2 = createAllocaInEntryBlock(CoreVM::LiteralType::Void, "zip.headB.tmp2");
    _builder.createStore(headBTmp2, headBReload);

    auto* headAFinal = _builder.createLoad(headATmp2, "zip.headA.final");
    auto* headBFinal = _builder.createLoad(headBTmp2, "zip.headB.final");
    auto* tuple =
        emitTuple2(headAFinal, headBFinal, CoreVM::LiteralType::Void, CoreVM::LiteralType::Void, "zip.tuple");
    _builder.createStore(tupleTmp, tuple);

    // Cons tuple onto reversed accumulator
    auto* accForCons = _builder.createLoad(accStorage, "zip.acc.for_cons");
    _builder.createStore(accTmp, accForCons);

    auto* tupleReload = _builder.createLoad(tupleTmp, "zip.tuple.reload");
    auto* tupleStoreTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "zip.tuple.store.tmp");
    _builder.createStore(tupleStoreTmp, tupleReload);
    auto* accReloadPre = _builder.createLoad(accTmp, "zip.acc.reload.pre");
    auto* accStoreTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "zip.acc.store.tmp");
    _builder.createStore(accStoreTmp, accReloadPre);

    auto* tupleForSlot = _builder.createLoad(tupleStoreTmp, "zip.tuple.for_slot");
    auto* accForSlot = _builder.createLoad(accStoreTmp, "zip.acc.for_slot");
    auto* cons = emitListCons(tupleForSlot, accForSlot, CoreVM::LiteralType::Object, "zip.cons");

    _builder.createStore(accStorage, cons);
    _builder.createBr(condBlock);

    // Phase 2: Reverse the accumulated list
    auto* revSrcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "zip.rev.src");
    auto* revAccStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "zip.rev.acc");

    _builder.setInsertPoint(revInitBlock);
    auto* revSrcInit = _builder.createLoad(accStorage, "zip.rev.src.init");
    _builder.createStore(revSrcStorage, revSrcInit);
    auto* revNil = emitNilList(CoreVM::LiteralType::Object, "zip.rev.nil");
    _builder.createStore(revAccStorage, revNil);
    _builder.createBr(revCondBlock);

    _builder.setInsertPoint(revCondBlock);
    auto* revSrcLoad = _builder.createLoad(revSrcStorage, "zip.rev.src.load");
    auto* revSrcTag = _builder.createObjGetTag(revSrcLoad, "zip.rev.src.tag");
    auto* revIsCons = _builder.createNCmpEQ(revSrcTag, tag1, "zip.rev.is_cons");
    _builder.createCondBr(revIsCons, revBodyBlock, endBlock);

    _builder.setInsertPoint(revBodyBlock);
    auto* revSrcForHead = _builder.createLoad(revSrcStorage, "zip.rev.src.for_head");
    auto* revHead = _builder.createObjGetSlot(revSrcForHead, slot0, "zip.rev.head");
    auto* revSrcForTail = _builder.createLoad(revSrcStorage, "zip.rev.src.for_tail");
    auto* revTail = _builder.createObjGetSlot(revSrcForTail, slot1, "zip.rev.tail");
    _builder.createStore(revSrcStorage, revTail);

    auto* revElemTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "zip.rev.elem.tmp");
    _builder.createStore(revElemTmp, revHead);
    auto* revAccTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "zip.rev.acc.tmp");
    auto* revAccForCons = _builder.createLoad(revAccStorage, "zip.rev.acc.for_cons");
    _builder.createStore(revAccTmp, revAccForCons);

    auto* revElemReload = _builder.createLoad(revElemTmp, "zip.rev.elem.reload");
    auto* revAccReload = _builder.createLoad(revAccTmp, "zip.rev.acc.reload");
    auto* revCons = emitListCons(revElemReload, revAccReload, CoreVM::LiteralType::Object, "zip.rev.cons");

    _builder.createStore(revAccStorage, revCons);
    _builder.createBr(revCondBlock);

    _builder.setInsertPoint(endBlock);
    _result = _builder.createLoad(revAccStorage, "zip.result");
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
}

void IRGenerator::generateFlattenIR(CoreVM::Value* listOfLists)
{
    auto* tag1 = _builder.get(CoreVM::CoreNumber(1));  // Cons
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0)); // head
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1)); // tail

    // Allocas
    auto* outerSrcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "flatten.outer.src");
    _builder.createStore(outerSrcStorage, listOfLists);

    auto* innerSrcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "flatten.inner.src");

    auto* accStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "flatten.acc");
    auto* nil = emitNilList(CoreVM::LiteralType::Void, "flatten.nil");
    _builder.createStore(accStorage, nil);

    // Create blocks
    auto* outerCondBlock = _builder.createBlock("flatten.outer.cond");
    auto* outerBodyBlock = _builder.createBlock("flatten.outer.body");
    auto* innerCondBlock = _builder.createBlock("flatten.inner.cond");
    auto* innerBodyBlock = _builder.createBlock("flatten.inner.body");
    auto* revInitBlock = _builder.createBlock("flatten.rev.init");
    auto* revCondBlock = _builder.createBlock("flatten.rev.cond");
    auto* revBodyBlock = _builder.createBlock("flatten.rev.body");
    auto* endBlock = _builder.createBlock("flatten.end");

    _builder.createBr(outerCondBlock);

    // Outer loop: iterate list of lists
    _builder.setInsertPoint(outerCondBlock);
    auto* outerSrcLoad = _builder.createLoad(outerSrcStorage, "flatten.outer.src.load");
    auto* outerSrcTag = _builder.createObjGetTag(outerSrcLoad, "flatten.outer.src.tag");
    auto* outerIsCons = _builder.createNCmpEQ(outerSrcTag, tag1, "flatten.outer.is_cons");
    _builder.createCondBr(outerIsCons, outerBodyBlock, revInitBlock);

    // Outer body: extract inner list, advance outer
    _builder.setInsertPoint(outerBodyBlock);
    auto* outerForHead = _builder.createLoad(outerSrcStorage, "flatten.outer.for_head");
    auto* innerList = _builder.createObjGetSlot(outerForHead, slot0, "flatten.inner.list");
    _builder.createStore(innerSrcStorage, innerList);

    auto* outerForTail = _builder.createLoad(outerSrcStorage, "flatten.outer.for_tail");
    auto* outerTail = _builder.createObjGetSlot(outerForTail, slot1, "flatten.outer.tail");
    _builder.createStore(outerSrcStorage, outerTail);

    _builder.createBr(innerCondBlock);

    // Inner loop: iterate elements of current inner list
    _builder.setInsertPoint(innerCondBlock);
    auto* innerSrcLoad = _builder.createLoad(innerSrcStorage, "flatten.inner.src.load");
    auto* innerSrcTag = _builder.createObjGetTag(innerSrcLoad, "flatten.inner.src.tag");
    auto* innerIsCons = _builder.createNCmpEQ(innerSrcTag, tag1, "flatten.inner.is_cons");
    _builder.createCondBr(innerIsCons, innerBodyBlock, outerCondBlock);

    // Inner body: extract element, cons onto accumulator
    _builder.setInsertPoint(innerBodyBlock);
    auto* innerForHead = _builder.createLoad(innerSrcStorage, "flatten.inner.for_head");
    auto* elem = _builder.createObjGetSlot(innerForHead, slot0, "flatten.elem");

    auto* innerForTail = _builder.createLoad(innerSrcStorage, "flatten.inner.for_tail");
    auto* innerTail = _builder.createObjGetSlot(innerForTail, slot1, "flatten.inner.tail");
    _builder.createStore(innerSrcStorage, innerTail);

    // Store elem and acc in temp allocas to survive ObjAlloc
    auto* elemTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Void, "flatten.elem.tmp");
    _builder.createStore(elemTmp, elem);
    auto* accTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "flatten.acc.tmp");
    auto* accForCons = _builder.createLoad(accStorage, "flatten.acc.for_cons");
    _builder.createStore(accTmp, accForCons);

    auto* elemReload = _builder.createLoad(elemTmp, "flatten.elem.reload");
    auto* accReload = _builder.createLoad(accTmp, "flatten.acc.reload");
    auto* cons = emitListCons(elemReload, accReload, CoreVM::LiteralType::Void, "flatten.cons");

    _builder.createStore(accStorage, cons);
    _builder.createBr(innerCondBlock);

    // Phase 2: Reverse the accumulated list
    auto* revSrcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "flatten.rev.src");
    auto* revAccStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "flatten.rev.acc");

    _builder.setInsertPoint(revInitBlock);
    auto* revSrcInit = _builder.createLoad(accStorage, "flatten.rev.src.init");
    _builder.createStore(revSrcStorage, revSrcInit);
    auto* revNil = emitNilList(CoreVM::LiteralType::Void, "flatten.rev.nil");
    _builder.createStore(revAccStorage, revNil);
    _builder.createBr(revCondBlock);

    _builder.setInsertPoint(revCondBlock);
    auto* revSrcLoad = _builder.createLoad(revSrcStorage, "flatten.rev.src.load");
    auto* revSrcTag = _builder.createObjGetTag(revSrcLoad, "flatten.rev.src.tag");
    auto* revIsCons = _builder.createNCmpEQ(revSrcTag, tag1, "flatten.rev.is_cons");
    _builder.createCondBr(revIsCons, revBodyBlock, endBlock);

    _builder.setInsertPoint(revBodyBlock);
    auto* revSrcForHead = _builder.createLoad(revSrcStorage, "flatten.rev.src.for_head");
    auto* revHead = _builder.createObjGetSlot(revSrcForHead, slot0, "flatten.rev.head");
    auto* revSrcForTail = _builder.createLoad(revSrcStorage, "flatten.rev.src.for_tail");
    auto* revTail = _builder.createObjGetSlot(revSrcForTail, slot1, "flatten.rev.tail");
    _builder.createStore(revSrcStorage, revTail);

    auto* revElemTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Void, "flatten.rev.elem.tmp");
    _builder.createStore(revElemTmp, revHead);
    auto* revAccTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "flatten.rev.acc.tmp");
    auto* revAccForCons = _builder.createLoad(revAccStorage, "flatten.rev.acc.for_cons");
    _builder.createStore(revAccTmp, revAccForCons);

    auto* revElemReload = _builder.createLoad(revElemTmp, "flatten.rev.elem.reload");
    auto* revAccReload = _builder.createLoad(revAccTmp, "flatten.rev.acc.reload");
    auto* revCons = emitListCons(revElemReload, revAccReload, CoreVM::LiteralType::Void, "flatten.rev.cons");

    _builder.createStore(revAccStorage, revCons);
    _builder.createBr(revCondBlock);

    _builder.setInsertPoint(endBlock);
    _result = _builder.createLoad(revAccStorage, "flatten.result");
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
}

void IRGenerator::generateSortIR(CoreVM::Value* listValue)
{
    auto* callback = findCallback("list_sort(I)I");
    if (!callback)
    {
        reportTypeError("sort: native callback 'list_sort' not found");
        return;
    }
    _result =
        _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { listValue }, "sort.result");
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
    if (auto elt = getListElementLiteralType(listValue))
        annotateListElementLiteralType(_result, *elt);
}

void IRGenerator::generateDistinctIR(CoreVM::Value* listValue)
{
    auto* callback = findCallback("list_distinct(I)I");
    if (!callback)
    {
        reportTypeError("distinct: native callback 'list_distinct' not found");
        return;
    }
    _result =
        _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { listValue }, "distinct.result");
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
    if (auto elt = getListElementLiteralType(listValue))
        annotateListElementLiteralType(_result, *elt);
}

void IRGenerator::generateSortByIR(std::string const& funcParamName, CoreVM::Value* listValue)
{
    auto* tag1 = _builder.get(CoreVM::CoreNumber(1));  // Cons
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0)); // head / fst
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1)); // tail / snd

    // Resolve the key function
    auto funcName = funcParamName;
    if (auto ref = lookupFSharpFunctionRef(funcParamName))
        funcName = *ref;
    auto const* func = lookupFSharpFunction(funcName);
    if (!func)
    {
        reportTypeError("sortBy: function argument '{}' not found", std::string_view(funcParamName));
        return;
    }

    // Phase 1: Iterate source list, call key function on each element, build reversed list of Tuple2(key,
    // elem)
    auto* srcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "sortBy.src");
    _builder.createStore(srcStorage, listValue);

    auto* accStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "sortBy.acc");
    auto* nil = emitNilList(CoreVM::LiteralType::Object, "sortBy.nil");
    _builder.createStore(accStorage, nil);

    auto const sortByElemType = getListElementLiteralType(listValue).value_or(CoreVM::LiteralType::Number);
    auto* elemAlloca = createAllocaInEntryBlock(sortByElemType, "sortBy.elem");

    // Propagate list element type to extracted elements
    if (auto elemTypeId = getListElementTypeId(listValue))
        annotateObjectTypeId(elemAlloca, *elemTypeId);

    // Temp allocas for values that must survive ObjAlloc
    auto* keyTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Number, "sortBy.key.tmp");
    auto* elemTmp2 = createAllocaInEntryBlock(sortByElemType, "sortBy.elem.tmp2");
    auto* tupleTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "sortBy.tuple.tmp");
    auto* accTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "sortBy.acc.tmp");

    // Create blocks
    auto* condBlock = _builder.createBlock("sortBy.cond");
    auto* bodyBlock = _builder.createBlock("sortBy.body");
    auto* callBlock = _builder.createBlock("sortBy.call");

    _builder.createBr(condBlock);

    // Condition: check if source list is Cons
    _builder.setInsertPoint(condBlock);
    auto* srcLoad = _builder.createLoad(srcStorage, "sortBy.src.load");
    auto* srcTag = _builder.createObjGetTag(srcLoad, "sortBy.src.tag");
    auto* isCons = _builder.createNCmpEQ(srcTag, tag1, "sortBy.is_cons");
    _builder.createCondBr(isCons, bodyBlock, callBlock);

    // Body: extract head, call key function, build Tuple2(key, elem), cons onto acc
    _builder.setInsertPoint(bodyBlock);

    // Extract head (elem)
    auto* srcForHead = _builder.createLoad(srcStorage, "sortBy.src.for_head");
    auto* head = _builder.createObjGetSlot(srcForHead, slot0, "sortBy.head");
    _builder.createStore(elemAlloca, head);

    // Advance source to tail
    auto* srcForTail = _builder.createLoad(srcStorage, "sortBy.src.for_tail");
    auto* tail = _builder.createObjGetSlot(srcForTail, slot1, "sortBy.tail");
    _builder.createStore(srcStorage, tail);

    // Call key function on element
    auto* elemLoad = _builder.createLoad(elemAlloca, "sortBy.elem.load");
    if (auto elemTypeId = getObjectTypeId(elemAlloca))
        annotateObjectTypeId(elemLoad, *elemTypeId);
    generateFSharpCall(func, funcName, { elemLoad });
    auto* keyValue = _result;
    if (!keyValue)
    {
        reportTypeError("sortBy: failed to apply key function to element");
        return;
    }

    // Unwrap Size objects to their raw byte count for numeric comparison
    if (auto const keyTypeId = getObjectTypeId(keyValue))
    {
        if (*keyTypeId == CoreVM::BuiltinTypeId::Size)
            keyValue = _builder.createObjGetSlot(
                keyValue, _builder.get(CoreVM::CoreNumber(0)), "sortBy.key.size.bytes");
    }

    // Store key and elem in temp allocas (must survive Tuple2 ObjAlloc)
    _builder.createStore(keyTmp, keyValue);
    auto* elemReload = _builder.createLoad(elemAlloca, "sortBy.elem.reload");
    _builder.createStore(elemTmp2, elemReload);

    // Build Tuple2(key, elem)
    auto* keyReload = _builder.createLoad(keyTmp, "sortBy.key.reload");
    auto* elemFinal = _builder.createLoad(elemTmp2, "sortBy.elem.final");
    auto* tuple = emitTuple2(
        keyReload, elemFinal, CoreVM::LiteralType::Void, CoreVM::LiteralType::Void, "sortBy.tuple");
    _builder.createStore(tupleTmp, tuple);

    // Cons tuple onto accumulator
    auto* accForCons = _builder.createLoad(accStorage, "sortBy.acc.for_cons");
    _builder.createStore(accTmp, accForCons);

    auto* tupleReload = _builder.createLoad(tupleTmp, "sortBy.tuple.reload");
    auto* tupleStoreTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "sortBy.tuple.store.tmp");
    _builder.createStore(tupleStoreTmp, tupleReload);
    auto* accReloadPre = _builder.createLoad(accTmp, "sortBy.acc.reload.pre");
    auto* accStoreTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "sortBy.acc.store.tmp");
    _builder.createStore(accStoreTmp, accReloadPre);

    auto* tupleForSlot = _builder.createLoad(tupleStoreTmp, "sortBy.tuple.for_slot");
    auto* accForSlot = _builder.createLoad(accStoreTmp, "sortBy.acc.for_slot");
    auto* cons = emitListCons(tupleForSlot, accForSlot, CoreVM::LiteralType::Object, "sortBy.cons");

    _builder.createStore(accStorage, cons);
    _builder.createBr(condBlock);

    // Phase 2: Call native list_sort_pairs to sort by key and extract elements
    _builder.setInsertPoint(callBlock);
    auto* callback = findCallback("list_sort_pairs(I)I");
    if (!callback)
    {
        reportTypeError("sortBy: native callback 'list_sort_pairs' not found");
        return;
    }
    auto* accFinal = _builder.createLoad(accStorage, "sortBy.acc.final");
    _result =
        _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { accFinal }, "sortBy.result");
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
    if (auto elemTypeId = getListElementTypeId(listValue))
        annotateListElementTypeId(_result, *elemTypeId);
    if (auto elt = getListElementLiteralType(listValue))
        annotateListElementLiteralType(_result, *elt);
}

void IRGenerator::generateGroupByIR(std::string const& funcParamName, CoreVM::Value* listValue)
{
    auto* tag1 = _builder.get(CoreVM::CoreNumber(1));  // Cons
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0)); // head / fst
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1)); // tail / snd

    // Resolve the key function
    auto funcName = funcParamName;
    if (auto ref = lookupFSharpFunctionRef(funcParamName))
        funcName = *ref;
    auto const* func = lookupFSharpFunction(funcName);
    if (!func)
    {
        reportTypeError("groupBy: function argument '{}' not found", std::string_view(funcParamName));
        return;
    }

    // Phase 1: Iterate source list, call key function on each element, build reversed list of Tuple2(key,
    // elem)
    auto* srcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "groupBy.src");
    _builder.createStore(srcStorage, listValue);

    auto* accStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "groupBy.acc");
    auto* nil = emitNilList(CoreVM::LiteralType::Object, "groupBy.nil");
    _builder.createStore(accStorage, nil);

    auto const groupByElemType = getListElementLiteralType(listValue).value_or(CoreVM::LiteralType::Number);
    auto* elemAlloca = createAllocaInEntryBlock(groupByElemType, "groupBy.elem");

    // Propagate list element type to extracted elements
    if (auto elemTypeId = getListElementTypeId(listValue))
        annotateObjectTypeId(elemAlloca, *elemTypeId);

    // Temp allocas for values that must survive ObjAlloc
    auto* keyTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Number, "groupBy.key.tmp");
    auto* elemTmp2 = createAllocaInEntryBlock(groupByElemType, "groupBy.elem.tmp2");
    auto* tupleTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "groupBy.tuple.tmp");
    auto* accTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "groupBy.acc.tmp");

    // Create blocks
    auto* condBlock = _builder.createBlock("groupBy.cond");
    auto* bodyBlock = _builder.createBlock("groupBy.body");
    auto* callBlock = _builder.createBlock("groupBy.call");

    _builder.createBr(condBlock);

    // Condition: check if source list is Cons
    _builder.setInsertPoint(condBlock);
    auto* srcLoad = _builder.createLoad(srcStorage, "groupBy.src.load");
    auto* srcTag = _builder.createObjGetTag(srcLoad, "groupBy.src.tag");
    auto* isCons = _builder.createNCmpEQ(srcTag, tag1, "groupBy.is_cons");
    _builder.createCondBr(isCons, bodyBlock, callBlock);

    // Body: extract head, call key function, build Tuple2(key, elem), cons onto acc
    _builder.setInsertPoint(bodyBlock);

    // Extract head (elem)
    auto* srcForHead = _builder.createLoad(srcStorage, "groupBy.src.for_head");
    auto* head = _builder.createObjGetSlot(srcForHead, slot0, "groupBy.head");
    _builder.createStore(elemAlloca, head);

    // Advance source to tail
    auto* srcForTail = _builder.createLoad(srcStorage, "groupBy.src.for_tail");
    auto* tail = _builder.createObjGetSlot(srcForTail, slot1, "groupBy.tail");
    _builder.createStore(srcStorage, tail);

    // Call key function on element
    auto* elemLoad = _builder.createLoad(elemAlloca, "groupBy.elem.load");
    if (auto elemTypeId = getObjectTypeId(elemAlloca))
        annotateObjectTypeId(elemLoad, *elemTypeId);
    generateFSharpCall(func, funcName, { elemLoad });
    auto* keyValue = _result;
    if (!keyValue)
    {
        reportTypeError("groupBy: failed to apply key function to element");
        return;
    }

    // Store key and elem in temp allocas (must survive Tuple2 ObjAlloc)
    _builder.createStore(keyTmp, keyValue);
    auto* elemReload = _builder.createLoad(elemAlloca, "groupBy.elem.reload");
    _builder.createStore(elemTmp2, elemReload);

    // Build Tuple2(key, elem)
    auto* keyReload = _builder.createLoad(keyTmp, "groupBy.key.reload");
    auto* elemFinal = _builder.createLoad(elemTmp2, "groupBy.elem.final");
    auto* tuple = emitTuple2(
        keyReload, elemFinal, CoreVM::LiteralType::Void, CoreVM::LiteralType::Void, "groupBy.tuple");
    _builder.createStore(tupleTmp, tuple);

    // Cons tuple onto accumulator
    auto* accForCons = _builder.createLoad(accStorage, "groupBy.acc.for_cons");
    _builder.createStore(accTmp, accForCons);

    auto* tupleReload = _builder.createLoad(tupleTmp, "groupBy.tuple.reload");
    auto* tupleStoreTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "groupBy.tuple.store.tmp");
    _builder.createStore(tupleStoreTmp, tupleReload);
    auto* accReloadPre = _builder.createLoad(accTmp, "groupBy.acc.reload.pre");
    auto* accStoreTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "groupBy.acc.store.tmp");
    _builder.createStore(accStoreTmp, accReloadPre);

    auto* tupleForSlot = _builder.createLoad(tupleStoreTmp, "groupBy.tuple.for_slot");
    auto* accForSlot = _builder.createLoad(accStoreTmp, "groupBy.acc.for_slot");
    auto* cons = emitListCons(tupleForSlot, accForSlot, CoreVM::LiteralType::Object, "groupBy.cons");

    _builder.createStore(accStorage, cons);
    _builder.createBr(condBlock);

    // Phase 2: Call native list_group_pairs to group by key
    _builder.setInsertPoint(callBlock);
    auto* callback = findCallback("list_group_pairs(I)I");
    if (!callback)
    {
        reportTypeError("groupBy: native callback 'list_group_pairs' not found");
        return;
    }
    auto* accFinal = _builder.createLoad(accStorage, "groupBy.acc.final");
    _result =
        _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { accFinal }, "groupBy.result");
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
}

// }}}

} // namespace endo
