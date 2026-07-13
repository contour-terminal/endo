// SPDX-License-Identifier: Apache-2.0
#include "CompileCommand.hpp"

#include <cstdlib>
#include <cstring>
#include <format>
#include <fstream>
#include <print>
#include <sstream>

namespace endo::compile
{

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

} // namespace endo::compile

#if defined(ENDO_HAS_WASM)

    #include <endo-language/CompileToWasm.hpp>

    #include <filesystem>

namespace endo::compile
{

namespace
{

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

    auto report = CoreVM::diagnostics::ConsoleReport {};
    auto const output = compileSourceToWasm(std::move(*source),
                                            options.scriptFile,
                                            CoreVM::wasm::WasmOptions {
                                                .optimize = options.optimize,
                                                .tailCalls = options.tailCalls,
                                                .emitWat = emitWat,
                                            },
                                            report);
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
