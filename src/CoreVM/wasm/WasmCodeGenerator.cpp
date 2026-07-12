// SPDX-License-Identifier: Apache-2.0
#include <CoreVM/ir/IRProgram.hpp>
#include <CoreVM/types/TypeRegistry.hpp>
#include <CoreVM/wasm/WasmCodeGenerator.hpp>
#include <CoreVM/wasm/WasmFunctionLowerer.hpp>
#include <CoreVM/wasm/WasmRuntimeABI.hpp>
#include <CoreVM/wasm/WasmStringTable.hpp>

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include <binaryen-c.h>

namespace CoreVM::wasm
{

namespace
{
    /// RAII ownership of a binaryen module.
    struct ModuleGuard
    {
        BinaryenModuleRef module;

        ModuleGuard(): module { BinaryenModuleCreate() } {}

        ModuleGuard(ModuleGuard const&) = delete;
        ModuleGuard& operator=(ModuleGuard const&) = delete;
        ModuleGuard(ModuleGuard&&) = delete;
        ModuleGuard& operator=(ModuleGuard&&) = delete;

        ~ModuleGuard() { BinaryenModuleDispose(module); }
    };

    /// Synthesizes the WASI command entry point: `_start` runs global
    /// initialization (if present), then the program's main function, and
    /// finally reports a non-zero exit status via proc_exit.
    void synthesizeStart(BinaryenModuleRef module, IRProgram& program)
    {
        auto statements = std::vector<BinaryenExpressionRef> {};
        for (auto const* name: { "@__global_init__", "@main" })
        {
            auto* function = program.findFunction(name);
            if (function == nullptr || function->empty())
                continue;
            auto* call = BinaryenCall(
                module, WasmFunctionLowerer::mangledName(name).c_str(), nullptr, 0, BinaryenTypeInt64());
            statements.push_back(BinaryenDrop(module, call));
        }

        // if (exit_status != 0) proc_exit(exit_status)
        auto const exitStatusName = std::string(layout::ExitStatusGlobal);
        auto exitArgs = std::array { BinaryenGlobalGet(module, exitStatusName.c_str(), BinaryenTypeInt32()) };
        statements.push_back(
            BinaryenIf(module,
                       BinaryenGlobalGet(module, exitStatusName.c_str(), BinaryenTypeInt32()),
                       BinaryenCall(module,
                                    "proc_exit",
                                    exitArgs.data(),
                                    static_cast<BinaryenIndex>(exitArgs.size()),
                                    BinaryenTypeNone()),
                       nullptr));

        auto* body = BinaryenBlock(module,
                                   nullptr,
                                   statements.data(),
                                   static_cast<BinaryenIndex>(statements.size()),
                                   BinaryenTypeNone());
        BinaryenAddFunction(module, "_start", BinaryenTypeNone(), BinaryenTypeNone(), nullptr, 0, body);
        BinaryenAddFunctionExport(module, "_start", "_start");
    }

    /// Builds the object typeId -> slot count map from the builtin type
    /// registry plus the program's custom product/sum types, mirroring
    /// TargetCodeGenerator's type registration.
    std::unordered_map<int64_t, int64_t> collectSlotCounts(IRProgram& program)
    {
        auto registry = TypeRegistry {};
        for (auto const& customType: program.customProductTypes())
        {
            auto type = std::make_unique<TypeDescriptor>();
            type->kind = TypeKind::Product;
            type->id = customType.assignedId;
            type->name = customType.name;
            type->fields = customType.fields;
            type->slotCount = customType.slotCount > 0 ? customType.slotCount
                                                       : static_cast<uint16_t>(customType.fields.size());
            registry.registerProductType(std::move(type));
        }
        for (auto const& customType: program.customSumTypes())
        {
            auto type = std::make_unique<TypeDescriptor>();
            type->kind = TypeKind::Sum;
            type->id = customType.assignedId;
            type->name = customType.name;
            type->variants = customType.variants;
            registry.registerSumType(std::move(type));
        }

        auto slotCounts = std::unordered_map<int64_t, int64_t> {};
        for (auto const& type: registry.allTypes())
            slotCounts[type->id] = type->slotCount;
        return slotCounts;
    }

    /// Finalizes the linear memory: one active data segment holding the
    /// interned string constants, the bump-allocator base global, and the
    /// exported memory sized to cover data plus initial heap room.
    void finalizeMemory(BinaryenModuleRef module, WasmStringTable const& strings)
    {
        auto const heapBase = (strings.dataEnd() + 15U) & ~15U;
        BinaryenAddGlobal(module,
                          std::string(layout::HeapPointerGlobal).c_str(),
                          BinaryenTypeInt32(),
                          /*mutable=*/true,
                          BinaryenConst(module, BinaryenLiteralInt32(static_cast<int32_t>(heapBase))));

        auto const pageSize = 65536U;
        auto const initialPages = (heapBase + pageSize + (pageSize - 1)) / pageSize;

        auto const* segmentName = "strings";
        auto const* segmentData = reinterpret_cast<char const*>(strings.blob().data());
        auto segmentPassive = false;
        auto* segmentOffset =
            BinaryenConst(module, BinaryenLiteralInt32(static_cast<int32_t>(layout::DataBase)));
        auto segmentSize = static_cast<BinaryenIndex>(strings.blob().size());
        auto const numSegments = BinaryenIndex { strings.empty() ? 0U : 1U };

        BinaryenSetMemory(module,
                          initialPages,
                          /*maximum=*/65536,
                          std::string(layout::MemoryExportName).c_str(),
                          &segmentName,
                          &segmentData,
                          &segmentPassive,
                          &segmentOffset,
                          &segmentSize,
                          numSegments,
                          /*shared=*/false,
                          /*memory64=*/false,
                          /*name=*/"0");
    }
} // namespace

WasmCodeGenerator::WasmCodeGenerator(WasmRuntimeProvider& runtimeProvider, WasmOptions options):
    _runtimeProvider { runtimeProvider }, _options { options }
{
}

std::optional<WasmOutput> WasmCodeGenerator::generate(IRProgram* program, diagnostics::Report& report)
{
    auto guard = ModuleGuard {};
    auto* module = guard.module;

    auto features =
        BinaryenFeatureBulkMemory() | BinaryenFeatureBulkMemoryOpt() | BinaryenFeatureNontrappingFPToInt();
    if (_options.tailCalls)
        features |= BinaryenFeatureTailCall();
    BinaryenModuleSetFeatures(module, features);

    // WASI Preview 1 imports used directly by generated code.
    BinaryenAddFunctionImport(
        module, "proc_exit", "wasi_snapshot_preview1", "proc_exit", BinaryenTypeInt32(), BinaryenTypeNone());

    // Shell exit-status semantics: builtins update this global; a non-zero
    // value at the end of _start becomes the process exit code.
    BinaryenAddGlobal(module,
                      std::string(layout::ExitStatusGlobal).c_str(),
                      BinaryenTypeInt32(),
                      /*mutable=*/true,
                      BinaryenConst(module, BinaryenLiteralInt32(0)));

    auto strings = WasmStringTable {};
    auto usedHelpers = std::set<RuntimeHelperDef const*> {};
    auto const slotCounts = collectSlotCounts(*program);
    for (IRFunction* function: program->functions())
    {
        if (function->empty())
            continue;
        WasmFunctionLowerer lowerer(module, _options, report, usedHelpers, strings, slotCounts);
        lowerer.lower(function);
    }

    synthesizeStart(module, *program);
    _runtimeProvider.provide(module, usedHelpers, strings);
    finalizeMemory(module, strings);

    if (report.containsFailures())
        return std::nullopt;

    if (!BinaryenModuleValidate(module))
    {
        report.typeError(SourceLocation {},
                         "internal error (WASM backend): generated module failed validation; "
                         "compile with a .wat output file to inspect the module");
        return std::nullopt;
    }

    if (_options.optimize)
    {
        BinaryenSetOptimizeLevel(2);
        BinaryenSetShrinkLevel(0);
        BinaryenModuleOptimize(module);
    }

    // Binaryen allocates the serialization buffers with malloc() and hands
    // ownership to the caller.
    using MallocGuard = std::unique_ptr<void, decltype(&std::free)>;

    auto output = WasmOutput {};
    auto writeResult = BinaryenModuleAllocateAndWrite(module, nullptr);
    auto const binaryGuard = MallocGuard { writeResult.binary, &std::free };
    auto const sourceMapGuard = MallocGuard { writeResult.sourceMap, &std::free };
    auto const* bytes = static_cast<uint8_t const*>(writeResult.binary);
    output.binary.assign(bytes, bytes + writeResult.binaryBytes);

    if (_options.emitWat)
    {
        auto const watGuard = MallocGuard { BinaryenModuleAllocateAndWriteText(module), &std::free };
        output.wat = static_cast<char const*>(watGuard.get());
    }

    return output;
}

} // namespace CoreVM::wasm
