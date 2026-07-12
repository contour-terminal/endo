// SPDX-License-Identifier: Apache-2.0
#include "CompileCommand.hpp"

#include <cstdlib>
#include <print>

#if defined(ENDO_HAS_WASM)

    #include <endo-language/CompileToIR.hpp>
    #include <endo-language/builtins/StubRuntime.hpp>

    #include <CoreVM/CoreVM.hpp>
    #include <CoreVM/transform/Passes.hpp>
    #include <CoreVM/wasm/WasmCodeGenerator.hpp>
    #include <CoreVM/wasm/WasmRuntime.hpp>

    #include <expected>
    #include <filesystem>
    #include <fstream>
    #include <sstream>
    #include <string>

namespace endo::compile
{

namespace
{

    /// Reads a script file and strips a leading shebang line.
    std::expected<std::string, std::string> readScriptSource(std::string_view scriptPath)
    {
        auto const path = std::string(scriptPath);
        std::ifstream file(path);
        if (!file)
            return std::unexpected(std::format("{}: {}", scriptPath, strerror(errno)));

        std::ostringstream ss;
        ss << file.rdbuf();
        std::string content = ss.str();

        if (content.starts_with("#!"))
        {
            auto const pos = content.find('\n');
            content = pos != std::string::npos ? content.substr(pos + 1) : std::string {};
        }
        return content;
    }

    /// Writes the compiled module to the output file (binary or text).
    bool writeOutput(std::string_view outputFile, CoreVM::wasm::WasmOutput const& output, bool asText)
    {
        auto stream = std::ofstream(std::string(outputFile), std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            std::print(stderr, "endo: cannot write {}: {}\n", outputFile, strerror(errno));
            return false;
        }
        if (asText)
            stream.write(output.wat.data(), static_cast<std::streamsize>(output.wat.size()));
        else
            stream.write(reinterpret_cast<char const*>(output.binary.data()),
                         static_cast<std::streamsize>(output.binary.size()));
        return stream.good();
    }

} // namespace

int runCompileCommand(CompileOptions const& options)
{
    auto const extension = std::filesystem::path(options.outputFile).extension().string();
    bool const emitWat = extension == ".wat";
    if (!emitWat && extension != ".wasm")
    {
        std::print(stderr,
                   "endo: unsupported output format '{}' (supported: .wasm, .wat)\n",
                   extension.empty() ? std::string(options.outputFile) : extension);
        return EXIT_FAILURE;
    }

    auto source = readScriptSource(options.scriptFile);
    if (!source)
    {
        std::print(stderr, "endo: {}\n", source.error());
        return EXIT_FAILURE;
    }

    // IR generation only needs builtin signatures, not executable callbacks.
    auto runtime = CoreVM::Runtime {};
    registerStubRuntime(runtime);

    auto report = CoreVM::diagnostics::ConsoleReport {};
    // NOTE: The CoreVM IR-level optimization passes (PassManager) are not run
    // here: they verify() the IR after each change, and the frontend routinely
    // emits blocks that fail verification (e.g. unterminated match merge
    // blocks after fully-returning arms) yet execute fine. -O optimization
    // happens at the WASM module level via binaryen instead.
    auto irProgram = compileToIR(std::move(*source), runtime, report, options.scriptFile);
    if (!irProgram)
        return EXIT_FAILURE;

    auto provider = CoreVM::wasm::WasmRuntime {};
    auto generator = CoreVM::wasm::WasmCodeGenerator(provider,
                                                     CoreVM::wasm::WasmOptions {
                                                         .optimize = options.optimize,
                                                         .tailCalls = options.tailCalls,
                                                         .emitWat = emitWat,
                                                     });
    auto const output = generator.generate(irProgram.get(), report);
    if (!output)
        return EXIT_FAILURE;

    return writeOutput(options.outputFile, *output, emitWat) ? EXIT_SUCCESS : EXIT_FAILURE;
}

} // namespace endo::compile

#else

namespace endo::compile
{

int runCompileCommand(CompileOptions const& options)
{
    (void) options;
    std::print(stderr,
               "endo: this build does not include the WebAssembly backend "
               "(binaryen was not found at configure time)\n");
    return EXIT_FAILURE;
}

} // namespace endo::compile

#endif
