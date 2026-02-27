// SPDX-License-Identifier: Apache-2.0
#include <endo-language/ast/AST.hpp>
#include <endo-language/ast/ASTPrinter.hpp>
#include <endo-language/builtins/BuiltinImpls.hpp>
#include <endo-language/builtins/BuiltinSignatures.hpp>
#include <endo-language/builtins/TypeFormatters.hpp>
#include <endo-language/codegen/IRGenerator.hpp>
#include <endo-language/lexer/Lexer.hpp>
#include <endo-language/parser/Parser.hpp>

#include <CoreVM/CoreVM.hpp>
#include <CoreVM/types/TypeDescriptor.hpp>
#include <CoreVM/types/TypedObject.hpp>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <emscripten/emscripten.h>

namespace
{

/// Escapes a string for safe inclusion in a JSON string value.
std::string jsonEscape(std::string const& s)
{
    std::string result;
    result.reserve(s.size() + 16);
    for (auto c: s)
    {
        switch (c)
        {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                    result += std::format("\\u{:04x}", static_cast<unsigned>(c));
                else
                    result += c;
                break;
        }
    }
    return result;
}

/// Playground runtime that wraps the Endo interpreter for browser use.
/// Modeled after TestRuntime but with REPL persistence and no POSIX dependencies.
class PlaygroundRuntime
{
  public:
    CoreVM::Runtime runtime;
    CoreVM::diagnostics::BufferedReport report;
    std::string capturedOutput;
    endo::FSharpPersistentState fsharpState;

    // Mock state for shell command simulation
    std::string mockCmdName;
    std::vector<std::string> mockCmdArgs;
    bool mockSubstActive = false;
    std::string mockSubstBuffer;
    std::unordered_map<std::string, std::string> mockEnv;

    PlaygroundRuntime()
    {
        // Resolver chains playground-specific overrides with shared stateless implementations.
        auto resolver = [this](std::string_view name,
                               size_t arity) -> std::optional<CoreVM::NativeCallback::Functor> {
            using Functor = CoreVM::NativeCallback::Functor;

            // --- Output capture (stateful) ---
            if (name == "print" && arity == 1)
                return Functor([this](CoreVM::Params& p) { capturedOutput += p.getString(1); });
            if (name == "println" && arity == 1)
                return Functor([this](CoreVM::Params& p) {
                    capturedOutput += p.getString(1);
                    capturedOutput += '\n';
                });
            if (name == "display_result" && arity == 1)
                return Functor([this](CoreVM::Params& args) {
                    auto rawVal = static_cast<uint64_t>(args.getInt(1));
                    capturedOutput += endo::builtins::valueToString(rawVal, args.caller());
                    capturedOutput += '\n';
                });

            // --- Shell command execution (stateful) ---
            if (name == "callproc" && arity == 1)
                return Functor([this](CoreVM::Params& params) {
                    auto const& args = params.getStringArray(1);
                    if (!args.empty() && args[0] == "echo")
                    {
                        std::string output;
                        for (size_t i = 1; i < args.size(); ++i)
                        {
                            if (i > 1)
                                output += ' ';
                            output += args[i];
                        }
                        output += '\n';
                        if (mockSubstActive)
                            mockSubstBuffer += output;
                        else
                            capturedOutput += output;
                    }
                    params.setResult(CoreVM::CoreNumber(0));
                });
            if (name == "callproc" && arity == 2)
                return Functor([this](CoreVM::Params& params) {
                    auto const& args = params.getStringArray(2);
                    if (!args.empty() && args[0] == "echo")
                    {
                        std::string output;
                        for (size_t i = 1; i < args.size(); ++i)
                        {
                            if (i > 1)
                                output += ' ';
                            output += args[i];
                        }
                        output += '\n';
                        if (mockSubstActive)
                            mockSubstBuffer += output;
                        else
                            capturedOutput += output;
                    }
                    params.setResult(CoreVM::CoreNumber(0));
                });

            // --- Command substitution (stateful) ---
            if (name == "internal.subst_start" && arity == 0)
                return Functor([this](CoreVM::Params&) {
                    mockSubstActive = true;
                    mockSubstBuffer.clear();
                });
            if (name == "internal.subst_end" && arity == 0)
                return Functor([this](CoreVM::Params& args) {
                    mockSubstActive = false;
                    auto result = std::move(mockSubstBuffer);
                    while (!result.empty() && result.back() == '\n')
                        result.pop_back();
                    args.setResult(std::move(result));
                });

            // --- Shell command building (stateful) ---
            if (name == "internal.cmd_start" && arity == 1)
                return Functor([this](CoreVM::Params& args) {
                    mockCmdName = args.getString(1);
                    mockCmdArgs.clear();
                });
            if (name == "internal.cmd_arg" && arity == 1)
                return Functor([this](CoreVM::Params& args) { mockCmdArgs.emplace_back(args.getString(1)); });
            if (name == "internal.cmd_exec" && arity == 0)
                return Functor([this](CoreVM::Params& args) {
                    if (mockCmdName == "echo")
                    {
                        std::string output;
                        for (size_t i = 0; i < mockCmdArgs.size(); ++i)
                        {
                            if (i > 0)
                                output += ' ';
                            output += mockCmdArgs[i];
                        }
                        output += '\n';
                        if (mockSubstActive)
                            mockSubstBuffer += output;
                        else
                            capturedOutput += output;
                    }
                    args.setResult(CoreVM::CoreNumber(0));
                });
            if (name == "internal.cmd_exec_piped" && arity == 1)
                return Functor([this](CoreVM::Params& args) {
                    if (mockCmdName == "echo" || mockCmdName == "/bin/echo")
                    {
                        std::string output;
                        for (size_t i = 0; i < mockCmdArgs.size(); ++i)
                        {
                            if (i > 0)
                                output += ' ';
                            output += mockCmdArgs[i];
                        }
                        output += '\n';
                        if (mockSubstActive)
                            mockSubstBuffer += output;
                        else
                            capturedOutput += output;
                    }
                    args.setResult(CoreVM::CoreNumber(0));
                });

            // --- Environment (stateful) ---
            if (name == "env.has" && arity == 1)
                return Functor([this](CoreVM::Params& args) {
                    args.setResult(mockEnv.contains(std::string(args.getString(1))));
                });
            if (name == "env.get" && arity == 1)
                return Functor([this](CoreVM::Params& args) {
                    auto const& key = args.getString(1);
                    if (auto const it = mockEnv.find(std::string(key)); it != mockEnv.end())
                        args.setResult(args.caller()->newString(it->second));
                    else
                        args.setResult(args.caller()->newString(""));
                });
            if (name == "export" && arity == 2)
                return Functor([this](CoreVM::Params& args) {
                    mockEnv[std::string(args.getString(1))] = std::string(args.getString(2));
                });

            // --- Playground-specific: no programs available ---
            if (name == "which_find" && arity == 1)
                return Functor([](CoreVM::Params& args) {
                    auto* none = args.caller()->makeNoneOption();
                    args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(none)));
                });

            if (name == "getvar.exitstatus" && arity == 0)
                return Functor([](CoreVM::Params& args) { args.setResult(CoreVM::CoreNumber(0)); });

            // Markdown render (WASM — prints raw text)
            if (name == "markdown_render" && arity == 1)
                return Functor([this](CoreVM::Params& args) {
                    auto* obj =
                        reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
                    auto const* content =
                        reinterpret_cast<CoreVM::CoreString const*>(static_cast<uintptr_t>(obj->getSlot(0)));
                    if (content)
                    {
                        capturedOutput += *content;
                        capturedOutput += '\n';
                    }
                });

            // Fall back to shared stateless implementations (list_*, string_*, format_*, rand, etc.)
            return endo::builtins::resolveSharedImpl(name, arity);
        };

        endo::registerFSharpBuiltins(runtime, resolver);
        endo::registerShellBuiltins(runtime, resolver);
        endo::registerInternalBuiltins(runtime, resolver);
        endo::registerStructuredBuiltins(runtime, resolver);
        endo::registerPromptPropertyBuiltins(runtime, resolver);
        endo::registerAgentConfigPropertyBuiltins(runtime, resolver);
    }

    /// Returns the singleton instance of PlaygroundRuntime.
    static PlaygroundRuntime& instance()
    {
        static PlaygroundRuntime inst;
        return inst;
    }
};

} // anonymous namespace

extern "C"
{

    /// Evaluates Endo source code and returns JSON result.
    /// Returns a pointer to a static buffer with JSON:
    ///   {"status":"ok","output":"..."} or {"status":"error","errors":["..."]}
    EMSCRIPTEN_KEEPALIVE
    char const* endo_eval(char const* source)
    {
        static std::string resultBuffer;

        auto& pg = PlaygroundRuntime::instance();
        pg.report.clear();
        pg.capturedOutput.clear();

        // Parse
        endo::Parser parser(pg.runtime, pg.report, std::make_unique<endo::StringSource>(std::string(source)));

        // Provide known function names for the parser
        if (!pg.fsharpState.functions.empty() || !pg.fsharpState.valueBindings.empty())
        {
            std::unordered_set<std::string> names;
            for (auto const& [name, _]: pg.fsharpState.functions)
                names.insert(name);
            for (auto const& binding: pg.fsharpState.valueBindings)
                names.insert(binding.name);
            parser.setKnownFSharpFunctions(std::move(names));
        }

        auto ast = parser.parse();
        if (!ast || pg.report.containsFailures())
        {
            std::string errors = "[";
            bool first = true;
            for (auto const& msg: pg.report.messages())
            {
                if (!first)
                    errors += ",";
                first = false;
                errors += "\"" + jsonEscape(msg.text) + "\"";
            }
            errors += "]";
            resultBuffer = R"({"status":"error","errors":)" + errors + "}";
            return resultBuffer.c_str();
        }

        // Generate IR with persistent state
        auto ir = endo::IRGenerator::generate(*ast, pg.report, pg.runtime, &pg.fsharpState);
        if (!ir || pg.report.containsFailures())
        {
            std::string errors = "[";
            bool first = true;
            for (auto const& msg: pg.report.messages())
            {
                if (!first)
                    errors += ",";
                first = false;
                errors += "\"" + jsonEscape(msg.text) + "\"";
            }
            errors += "]";
            resultBuffer = R"({"status":"error","errors":)" + errors + "}";
            return resultBuffer.c_str();
        }

        // Retain the AST so persisted function body pointers remain valid
        pg.fsharpState.retainedASTs.push_back(std::move(ast));

        // Generate target code
        CoreVM::TargetCodeGenerator codegen;
        auto targetProgram = codegen.generate(ir.get());
        if (!targetProgram)
        {
            resultBuffer = R"({"status":"error","errors":["Code generation failed"]})";
            return resultBuffer.c_str();
        }

        // Register type formatters for human-readable display
        endo::builtins::registerBuiltinFormatters(targetProgram->constants().typeRegistry());

        // Link
        if (!targetProgram->link(&pg.runtime, &pg.report))
        {
            resultBuffer = R"({"status":"error","errors":["Link failed"]})";
            return resultBuffer.c_str();
        }

        // Find main function
        CoreVM::Function const* fn = targetProgram->findFunction("@main");
        if (!fn)
        {
            resultBuffer = R"({"status":"error","errors":["No main function found"]})";
            return resultBuffer.c_str();
        }

        // Execute
        CoreVM::Runner::Globals globals;
        CoreVM::Runner runner(fn, nullptr, &globals, CoreVM::RuntimeConfig::defaultConfig(), nullptr);
        runner.run();

        // Save mutable binding values for persistence
        auto const& stack = runner.stack();
        for (size_t i = 0; i < pg.fsharpState.valueBindings.size() && i < stack.size(); ++i)
            if (pg.fsharpState.valueBindings[i].isMutable)
                pg.fsharpState.mutableSnapshots[pg.fsharpState.valueBindings[i].name] = stack[i];

        // Build success JSON
        resultBuffer = R"({"status":"ok","output":")" + jsonEscape(pg.capturedOutput) + "\"}";
        return resultBuffer.c_str();
    }

    /// Resets the REPL session state.
    EMSCRIPTEN_KEEPALIVE
    void endo_reset()
    {
        auto& pg = PlaygroundRuntime::instance();
        pg.fsharpState.functions.clear();
        pg.fsharpState.valueBindings.clear();
        pg.fsharpState.retainedASTs.clear();
        pg.fsharpState.mutableSnapshots.clear();
    }

    /// Returns the version string.
    EMSCRIPTEN_KEEPALIVE
    char const* endo_version()
    {
        return "0.1.0-playground";
    }

} // extern "C"
