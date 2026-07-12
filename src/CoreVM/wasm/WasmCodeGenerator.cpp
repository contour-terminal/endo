// SPDX-License-Identifier: Apache-2.0
#include <CoreVM/ir/IRProgram.hpp>
#include <CoreVM/wasm/WasmCodeGenerator.hpp>
#include <CoreVM/wasm/WasmFunctionLowerer.hpp>
#include <CoreVM/wasm/WasmRuntimeABI.hpp>

#include <cstdlib>
#include <memory>
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
    /// initialization (if present) and then the program's main function.
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
        auto* body = BinaryenBlock(module,
                                   nullptr,
                                   statements.data(),
                                   static_cast<BinaryenIndex>(statements.size()),
                                   BinaryenTypeNone());
        BinaryenAddFunction(module, "_start", BinaryenTypeNone(), BinaryenTypeNone(), nullptr, 0, body);
        BinaryenAddFunctionExport(module, "_start", "_start");
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

    auto features = BinaryenFeatureBulkMemory() | BinaryenFeatureNontrappingFPToInt();
    if (_options.tailCalls)
        features |= BinaryenFeatureTailCall();
    BinaryenModuleSetFeatures(module, features);

    // One linear memory, exported as "memory" (required by WASI).
    // Data segments for string constants are added in a later milestone.
    BinaryenSetMemory(module,
                      /*initial=*/1,
                      /*maximum=*/65536,
                      std::string(layout::MemoryExportName).c_str(),
                      /*segmentNames=*/nullptr,
                      /*segmentDatas=*/nullptr,
                      /*segmentPassives=*/nullptr,
                      /*segmentOffsets=*/nullptr,
                      /*segmentSizes=*/nullptr,
                      /*numSegments=*/0,
                      /*shared=*/false,
                      /*memory64=*/false,
                      /*name=*/"0");

    // WASI Preview 1 imports used directly by generated code.
    BinaryenAddFunctionImport(
        module, "proc_exit", "wasi_snapshot_preview1", "proc_exit", BinaryenTypeInt32(), BinaryenTypeNone());

    auto usedHelpers = std::set<RuntimeHelperDef const*> {};
    for (IRFunction* function: program->functions())
    {
        if (function->empty())
            continue;
        WasmFunctionLowerer lowerer(module, _options, report, usedHelpers);
        lowerer.lower(function);
    }

    synthesizeStart(module, *program);
    _runtimeProvider.provide(module, usedHelpers);

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
