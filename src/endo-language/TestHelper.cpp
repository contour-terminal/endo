// SPDX-License-Identifier: Apache-2.0
#include "TestHelper.hpp"

#include <endo-language/ast/AST.hpp>
#include <endo-language/ast/ASTPrinter.hpp>
#include <endo-language/builtins/BuiltinImpls.hpp>
#include <endo-language/builtins/BuiltinSignatures.hpp>
#include <endo-language/codegen/IRGenerator.hpp>
#include <endo-language/lexer/Lexer.hpp>
#include <endo-language/parser/Parser.hpp>

#include <CoreVM/types/TypeDescriptor.hpp>
#include <CoreVM/types/TypedObject.hpp>

#include <bit>

namespace endo::test
{

// =============================================================================
// Mock structured data builders (stateless, test-specific)
// =============================================================================

namespace
{

    void mockStructuredPs(CoreVM::Params& args)
    {
        auto* runner = args.caller();

        struct MockProc
        {
            int64_t pid;
            int64_t ppid;
            char const* user;
            double cpu;
            int64_t mem;
            char const* command;
        };

        constexpr MockProc procs[] = {
            { 1, 0, "root", 0.1, 1024, "/sbin/init" },
            { 42, 1, "alice", 15.5, 4096, "firefox" },
            { 100, 1, "bob", 2.3, 2048, "vim" },
        };
        auto* list = runner->makeNilList(CoreVM::LiteralType::Object);
        for (int i = 2; i >= 0; --i)
        {
            auto const& p = procs[i];
            auto* record = runner->allocObject(CoreVM::BuiltinTypeId::ProcessInfo);
            record->setSlot(0, static_cast<uint64_t>(p.pid));
            record->setSlot(1, static_cast<uint64_t>(p.ppid));
            record->setSlot(2, reinterpret_cast<uintptr_t>(runner->newString(p.user)));
            record->setSlot(3, std::bit_cast<uint64_t>(p.cpu));
            record->setSlot(4, static_cast<uint64_t>(p.mem));
            record->setSlot(5, reinterpret_cast<uintptr_t>(runner->newString(p.command)));
            list =
                runner->makeConsCell(reinterpret_cast<uintptr_t>(record), list, CoreVM::LiteralType::Object);
        }
        args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(list)));
    }

    void mockStructuredLs(CoreVM::Params& args)
    {
        auto* runner = args.caller();

        struct MockFile
        {
            char const* name;
            int64_t size;
            int64_t mode;
            int64_t mtime;
            bool isDir;
        };

        constexpr MockFile files[] = {
            { "docs", 4096, 0755, 1700000000, true },
            { "hello.txt", 42, 0644, 1700001000, false },
            { "script.sh", 256, 0755, 1700002000, false },
        };
        auto* list = runner->makeNilList(CoreVM::LiteralType::Object);
        for (int i = 2; i >= 0; --i)
        {
            auto const& f = files[i];
            auto* record = runner->allocObject(CoreVM::BuiltinTypeId::FileInfo);
            record->setSlot(0, reinterpret_cast<uintptr_t>(runner->newString(f.name)));
            record->setSlot(1, static_cast<uint64_t>(f.size));
            record->setSlot(2, static_cast<uint64_t>(f.mode));
            record->setSlot(3, static_cast<uint64_t>(f.mtime));
            record->setSlot(4, static_cast<uint64_t>(f.isDir ? 1 : 0));
            list =
                runner->makeConsCell(reinterpret_cast<uintptr_t>(record), list, CoreVM::LiteralType::Object);
        }
        args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(list)));
    }

    void mockStructuredJobs(CoreVM::Params& args)
    {
        auto* runner = args.caller();

        struct MockJob
        {
            int64_t id;
            char const* state;
            char const* command;
            int64_t pid;
        };

        constexpr MockJob jobs[] = {
            { 1, "Running", "sleep 100", 1234 },
            { 2, "Stopped", "vim", 5678 },
            { 3, "Done", "make build", 9012 },
        };
        auto* list = runner->makeNilList(CoreVM::LiteralType::Object);
        for (int i = 2; i >= 0; --i)
        {
            auto const& j = jobs[i];
            auto* record = runner->allocObject(CoreVM::BuiltinTypeId::JobInfo);
            record->setSlot(0, static_cast<uint64_t>(j.id));
            record->setSlot(1, reinterpret_cast<uintptr_t>(runner->newString(j.state)));
            record->setSlot(2, reinterpret_cast<uintptr_t>(runner->newString(j.command)));
            record->setSlot(3, static_cast<uint64_t>(j.pid));
            list =
                runner->makeConsCell(reinterpret_cast<uintptr_t>(record), list, CoreVM::LiteralType::Object);
        }
        args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(list)));
    }

    void mockStructuredDockerPs(CoreVM::Params& args)
    {
        auto* runner = args.caller();

        struct MockContainer
        {
            char const* id;
            char const* image;
            char const* command;
            char const* created;
            char const* status;
            char const* ports;
            char const* names;
        };

        constexpr MockContainer containers[] = {
            { "abc123def",
              "nginx:latest",
              "/docker-entrypoint…",
              "2024-01-15 10:00:00",
              "Up 3 hours",
              "80/tcp",
              "web-server" },
            { "def456ghi",
              "postgres:16",
              "docker-entrypoint.s…",
              "2024-01-14 08:00:00",
              "Up 2 days",
              "5432/tcp",
              "db-main" },
            { "ghi789jkl",
              "redis:7",
              "docker-entrypoint.s…",
              "2024-01-13 12:00:00",
              "Exited (0) 1 hour ago",
              "",
              "cache" },
        };
        auto* list = runner->makeNilList(CoreVM::LiteralType::Object);
        for (int i = 2; i >= 0; --i)
        {
            auto const& c = containers[i];
            auto* record = runner->allocObject(CoreVM::BuiltinTypeId::OutputDefBase);
            record->setSlot(0, reinterpret_cast<uintptr_t>(runner->newString(c.id)));
            record->setSlot(1, reinterpret_cast<uintptr_t>(runner->newString(c.image)));
            record->setSlot(2, reinterpret_cast<uintptr_t>(runner->newString(c.command)));
            record->setSlot(3, reinterpret_cast<uintptr_t>(runner->newString(c.created)));
            record->setSlot(4, reinterpret_cast<uintptr_t>(runner->newString(c.status)));
            record->setSlot(5, reinterpret_cast<uintptr_t>(runner->newString(c.ports)));
            record->setSlot(6, reinterpret_cast<uintptr_t>(runner->newString(c.names)));
            list =
                runner->makeConsCell(reinterpret_cast<uintptr_t>(record), list, CoreVM::LiteralType::Object);
        }
        args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(list)));
    }

    void mockStructuredDockerImages(CoreVM::Params& args)
    {
        auto* runner = args.caller();

        struct MockImage
        {
            char const* id;
            char const* repository;
            char const* tag;
            char const* created;
            char const* size;
        };

        constexpr MockImage images[] = {
            { "sha256:abc", "nginx", "latest", "2024-01-10", "187MB" },
            { "sha256:def", "postgres", "16", "2024-01-08", "412MB" },
            { "sha256:ghi", "redis", "7", "2024-01-05", "130MB" },
        };
        auto* list = runner->makeNilList(CoreVM::LiteralType::Object);
        for (int i = 2; i >= 0; --i)
        {
            auto const& img = images[i];
            constexpr uint16_t typeId = CoreVM::BuiltinTypeId::OutputDefBase + 1;
            auto* record = runner->allocObject(typeId);
            record->setSlot(0, reinterpret_cast<uintptr_t>(runner->newString(img.id)));
            record->setSlot(1, reinterpret_cast<uintptr_t>(runner->newString(img.repository)));
            record->setSlot(2, reinterpret_cast<uintptr_t>(runner->newString(img.tag)));
            record->setSlot(3, reinterpret_cast<uintptr_t>(runner->newString(img.created)));
            record->setSlot(4, reinterpret_cast<uintptr_t>(runner->newString(img.size)));
            list =
                runner->makeConsCell(reinterpret_cast<uintptr_t>(record), list, CoreVM::LiteralType::Object);
        }
        args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(list)));
    }

    void mockStructuredGitLog(CoreVM::Params& args)
    {
        auto* runner = args.caller();

        struct MockCommit
        {
            char const* sha;
            char const* author;
            char const* email;
            char const* date;
            char const* message;
        };

        constexpr MockCommit commits[] = {
            { "abc123", "Alice", "alice@example.com", "2024-01-15", "feat: add login" },
            { "def456", "Bob", "bob@example.com", "2024-01-14", "fix: null check" },
            { "ghi789", "Alice", "alice@example.com", "2024-01-13", "docs: update README" },
        };
        auto* list = runner->makeNilList(CoreVM::LiteralType::Object);
        for (int i = 2; i >= 0; --i)
        {
            auto const& c = commits[i];
            constexpr uint16_t typeId = CoreVM::BuiltinTypeId::OutputDefBase + 2;
            auto* record = runner->allocObject(typeId);
            record->setSlot(0, reinterpret_cast<uintptr_t>(runner->newString(c.sha)));
            record->setSlot(1, reinterpret_cast<uintptr_t>(runner->newString(c.author)));
            record->setSlot(2, reinterpret_cast<uintptr_t>(runner->newString(c.email)));
            record->setSlot(3, reinterpret_cast<uintptr_t>(runner->newString(c.date)));
            record->setSlot(4, reinterpret_cast<uintptr_t>(runner->newString(c.message)));
            list =
                runner->makeConsCell(reinterpret_cast<uintptr_t>(record), list, CoreVM::LiteralType::Object);
        }
        args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(list)));
    }

    void mockStructuredGitStatus(CoreVM::Params& args)
    {
        auto* runner = args.caller();

        struct MockStatusEntry
        {
            char const* status;
            char const* path;
        };

        constexpr MockStatusEntry entries[] = {
            { "M", "src/main.cpp" },
            { "??", "README.md" },
            { "A", ".gitignore" },
        };
        auto* list = runner->makeNilList(CoreVM::LiteralType::Object);
        for (int i = 2; i >= 0; --i)
        {
            auto const& e = entries[i];
            constexpr uint16_t typeId = CoreVM::BuiltinTypeId::OutputDefBase + 3;
            auto* record = runner->allocObject(typeId);
            record->setSlot(0, reinterpret_cast<uintptr_t>(runner->newString(e.status)));
            record->setSlot(1, reinterpret_cast<uintptr_t>(runner->newString(e.path)));
            list =
                runner->makeConsCell(reinterpret_cast<uintptr_t>(record), list, CoreVM::LiteralType::Object);
        }
        args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(list)));
    }

} // namespace

// =============================================================================
// TestRuntime constructor — resolver-based builtin registration
// =============================================================================

TestRuntime::TestRuntime()
{
    // Resolver chains test-specific overrides with shared stateless implementations.
    // Returns std::nullopt for builtins that should get a no-op default.
    auto resolver = [this](std::string_view name,
                           size_t arity) -> std::optional<CoreVM::NativeCallback::Functor> {
        using Functor = CoreVM::NativeCallback::Functor;

        // --- Output capture (stateful) ---
        if (name == "print" && arity == 1)
            return Functor([this](CoreVM::Params& p) { builtinPrint(p); });
        if (name == "println" && arity == 1)
            return Functor([this](CoreVM::Params& p) { builtinPrintln(p); });
        if (name == "display_result" && arity == 1)
            return Functor([this](CoreVM::Params& args) {
                auto rawVal = static_cast<uint64_t>(args.getInt(1));
                capturedOutput += builtins::valueToString(rawVal, args.caller());
                capturedOutput += '\n';
            });

        // --- Shell command execution (stateful) ---
        if (name == "callproc" && arity == 1)
            return Functor([this](CoreVM::Params& p) { dummyCallProc(p); });
        if (name == "callproc" && arity == 2)
            return Functor([this](CoreVM::Params& p) { dummyCallProcPiped(p); });

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
        if (name == "which_find" && arity == 1)
            return Functor([this](CoreVM::Params& args) {
                auto const& program = args.getString(1);
                if (auto const it = mockWhichPaths.find(std::string(program)); it != mockWhichPaths.end())
                {
                    auto* pathStr = args.caller()->newString(it->second);
                    auto* some = args.caller()->makeSomeOption(reinterpret_cast<uintptr_t>(pathStr),
                                                               CoreVM::LiteralType::String);
                    args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(some)));
                }
                else
                {
                    auto* none = args.caller()->makeNoneOption();
                    args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(none)));
                }
            });
        if (name == "getvar" && arity == 1)
            return Functor([this](CoreVM::Params& args) {
                auto const& key = args.getString(1);
                if (auto const it = mockEnv.find(std::string(key)); it != mockEnv.end())
                    args.setResult(args.caller()->newString(it->second));
                else
                    args.setResult(args.caller()->newString(""));
            });
        if (name == "getvar.exitstatus" && arity == 0)
            return Functor([](CoreVM::Params& args) { args.setResult(CoreVM::CoreNumber(0)); });

        // --- Structured mock data (stateless) ---
        if (name == "structured_ps" && arity == 0)
            return Functor(mockStructuredPs);
        if (name == "structured_ls" && arity == 1)
            return Functor(mockStructuredLs);
        if (name == "structured_jobs" && arity == 0)
            return Functor(mockStructuredJobs);
        if (name == "structured_docker_ps" && arity == 0)
            return Functor(mockStructuredDockerPs);
        if (name == "structured_docker_images" && arity == 0)
            return Functor(mockStructuredDockerImages);
        if (name == "structured_git_log" && arity == 0)
            return Functor(mockStructuredGitLog);
        if (name == "structured_git_status" && arity == 0)
            return Functor(mockStructuredGitStatus);

        // Fall back to shared stateless implementations (list_*, string_*, format_*, rand, etc.)
        return builtins::resolveSharedImpl(name, arity);
    };

    registerFSharpBuiltins(runtime, resolver);
    registerShellBuiltins(runtime, resolver);
    registerInternalBuiltins(runtime, resolver);
    registerStructuredBuiltins(runtime, resolver);
    registerPromptPropertyBuiltins(runtime, resolver);
    registerAgentConfigPropertyBuiltins(runtime, resolver);
}

void TestRuntime::dummyCallProc(CoreVM::Params& params)
{
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
}

void TestRuntime::dummyCallProcPiped(CoreVM::Params& params)
{
    auto const lastInChain = params.getBool(1);
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
    (void) lastInChain;
    params.setResult(CoreVM::CoreNumber(0));
}

void TestRuntime::builtinPrint(CoreVM::Params& params)
{
    capturedOutput += params.getString(1);
}

void TestRuntime::builtinPrintln(CoreVM::Params& params)
{
    capturedOutput += params.getString(1);
    capturedOutput += '\n';
}

void TestRuntime::setMockEnvVar(std::string const& key, std::string const& value)
{
    mockEnv[key] = value;
}

void TestRuntime::clearMockEnvVars()
{
    mockEnv.clear();
}

void TestRuntime::setMockWhichPath(std::string const& program, std::string const& path)
{
    mockWhichPaths[program] = path;
}

void TestRuntime::clearMockWhichPaths()
{
    mockWhichPaths.clear();
}

void TestRuntime::clearErrors()
{
    report.clear();
}

void TestRuntime::clearOutput()
{
    capturedOutput.clear();
}

bool TestRuntime::hasErrors() const
{
    return report.containsFailures();
}

std::string const& TestRuntime::output() const
{
    return capturedOutput;
}

TestRuntime& TestRuntime::instance()
{
    static TestRuntime instance;
    return instance;
}

std::unique_ptr<ast::Statement> parse(std::string const& source)
{
    auto& testRuntime = TestRuntime::instance();
    testRuntime.clearErrors();

    Parser parser(testRuntime.runtime, testRuntime.report, std::make_unique<StringSource>(source));
    auto result = parser.parse();

    if (testRuntime.hasErrors())
    {
        return nullptr;
    }

    return result;
}

std::unique_ptr<CoreVM::IRProgram> generateIR(std::string const& source)
{
    auto& testRuntime = TestRuntime::instance();
    testRuntime.clearErrors();

    Parser parser(testRuntime.runtime, testRuntime.report, std::make_unique<StringSource>(source));
    auto ast = parser.parse();

    if (!ast || testRuntime.hasErrors())
    {
        return nullptr;
    }

    auto ir = IRGenerator::generate(*ast, testRuntime.report, testRuntime.runtime);

    if (!ir || testRuntime.hasErrors())
    {
        return nullptr;
    }

    return ir;
}

bool generatesIRSuccessfully(std::string const& source)
{
    auto ir = generateIR(source);
    return ir != nullptr;
}

bool generatesIRWithError(std::string const& source, std::string_view expectedErrorSubstring)
{
    auto ir = generateIR(source);
    if (ir)
        return false; // Expected failure but IR generation succeeded

    auto const& messages = TestRuntime::instance().report.messages();
    for (auto const& msg: messages)
        if (msg.text.find(expectedErrorSubstring) != std::string::npos)
            return true;

    return false;
}

ast::Statement* getFirstStatement(ast::Statement* stmt)
{
    if (auto* compound = dynamic_cast<ast::CompoundStmt*>(stmt))
    {
        if (!compound->statements.empty())
        {
            return dynamic_cast<ast::Statement*>(compound->statements[0].get());
        }
    }
    return nullptr;
}

std::string parseAndPrintAST(std::string const& source)
{
    auto ast = parse(source);
    if (!ast)
    {
        throw ParseError("Parse failed for: \"" + source + "\"");
    }
    return ast::ASTPrinter::print(*ast);
}

ExecutionResult executeSource(std::string const& source)
{
    auto& testRuntime = TestRuntime::instance();
    testRuntime.clearErrors();
    testRuntime.clearOutput();

    // Generate IR
    auto ir = generateIR(source);
    if (!ir)
        return std::unexpected(TestError::IRGenerationFailed);

    // Generate target code
    CoreVM::TargetCodeGenerator codegen;
    auto targetProgram = codegen.generate(ir.get());
    if (!targetProgram)
        return std::unexpected(TestError::CodeGenerationFailed);

    // Link the program to the runtime (required for native function calls like print/println)
    if (!targetProgram->link(&testRuntime.runtime, &testRuntime.report))
        return std::unexpected(TestError::LinkFailed);

    // Find the main function
    CoreVM::Function const* fn = targetProgram->findFunction("@main");
    if (!fn)
        return std::unexpected(TestError::FunctionNotFound);

    // Execute
    CoreVM::Runner::Globals globals;
    CoreVM::Runner runner(fn, nullptr, &globals, CoreVM::RuntimeConfig::defaultConfig(), nullptr);

    // Runner::run() returns true if exit code was non-zero, false if it was 0
    bool exitNonZero = runner.run();
    int64_t exitCode = exitNonZero ? 1 : 0;

    return TestExecutionSuccess { .exitCode = exitCode, .output = testRuntime.output() };
}

std::string executeSourceAndGetOutput(std::string const& source)
{
    auto result = executeSource(source);
    if (!result.has_value())
        throw ExecutionError(result.error());
    return std::move(result->output);
}

bool executesSuccessfully(std::string const& source)
{
    auto result = executeSource(source);
    return result.has_value() && result->exitCode == 0;
}

bool executesWithExitCode(std::string const& source, int64_t expectedExitCode)
{
    auto result = executeSource(source);
    return result.has_value() && result->exitCode == expectedExitCode;
}

bool executesWithResult(std::string const& source, int64_t expectedExitCode, std::string_view expectedOutput)
{
    auto result = executeSource(source);
    return result.has_value() && result->exitCode == expectedExitCode && result->output == expectedOutput;
}

// =============================================================================
// Multi-prompt (REPL session) test helpers
// =============================================================================

ExecutionResult executeSession(std::vector<std::string> const& prompts)
{
    auto& testRuntime = TestRuntime::instance();
    FSharpPersistentState fsharpState;

    ExecutionResult lastResult = std::unexpected(TestError::ExecutionFailed);

    for (auto const& source: prompts)
    {
        testRuntime.clearErrors();
        testRuntime.clearOutput();

        // Parse
        Parser parser(testRuntime.runtime, testRuntime.report, std::make_unique<StringSource>(source));
        if (!fsharpState.functions.empty() || !fsharpState.valueBindings.empty())
        {
            std::unordered_set<std::string> names;
            for (auto const& [name, _]: fsharpState.functions)
                names.insert(name);
            for (auto const& binding: fsharpState.valueBindings)
                names.insert(binding.name);
            parser.setKnownFSharpFunctions(std::move(names));
        }
        auto ast = parser.parse();
        if (!ast || testRuntime.hasErrors())
            return std::unexpected(TestError::ParseFailed);

        // Generate IR with persistent state
        auto ir = IRGenerator::generate(*ast, testRuntime.report, testRuntime.runtime, &fsharpState);
        if (!ir || testRuntime.hasErrors())
            return std::unexpected(TestError::IRGenerationFailed);

        // Retain the AST so persisted function body pointers remain valid
        fsharpState.retainedASTs.push_back(std::move(ast));

        // Generate target code
        CoreVM::TargetCodeGenerator codegen;
        auto targetProgram = codegen.generate(ir.get());
        if (!targetProgram)
            return std::unexpected(TestError::CodeGenerationFailed);

        // Link
        if (!targetProgram->link(&testRuntime.runtime, &testRuntime.report))
            return std::unexpected(TestError::LinkFailed);

        // Find main function
        CoreVM::Function const* fn = targetProgram->findFunction("@main");
        if (!fn)
            return std::unexpected(TestError::FunctionNotFound);

        // Execute
        CoreVM::Runner::Globals globals;
        CoreVM::Runner runner(fn, nullptr, &globals, CoreVM::RuntimeConfig::defaultConfig(), nullptr);
        bool exitNonZero = runner.run();
        int64_t exitCode = exitNonZero ? 1 : 0;

        // Save runtime values of mutable bindings for cross-prompt persistence.
        // Allocas are at the bottom of the stack (positions 0, 1, 2, ...),
        // matching the order of persisted value bindings.
        auto const& stack = runner.stack();
        for (size_t i = 0; i < fsharpState.valueBindings.size() && i < stack.size(); ++i)
            if (fsharpState.valueBindings[i].isMutable)
                fsharpState.mutableSnapshots[fsharpState.valueBindings[i].name] = stack[i];

        lastResult = TestExecutionSuccess { .exitCode = exitCode, .output = testRuntime.output() };
    }

    return lastResult;
}

std::string executeSessionAndGetOutput(std::vector<std::string> const& prompts)
{
    auto result = executeSession(prompts);
    if (!result.has_value())
        throw ExecutionError(result.error());
    return std::move(result->output);
}

bool sessionProducesOutput(std::vector<std::string> const& prompts, std::string_view expectedOutput)
{
    auto result = executeSession(prompts);
    return result.has_value() && result->output == expectedOutput;
}

// =============================================================================
// Structured pipeline test helpers
// =============================================================================

FSharpPersistentState createMockStructuredState()
{
    using CoreVM::LiteralType;

    FSharpPersistentState state;

    // DockerPsRecord (typeId = OutputDefBase = 100)
    {
        constexpr uint16_t typeId = CoreVM::BuiltinTypeId::OutputDefBase;
        state.outputDefinitionTypes["DockerPsRecord"] = {
            .typeId = typeId,
            .fields = {
                { "id", 0, LiteralType::String },
                { "image", 1, LiteralType::String },
                { "command", 2, LiteralType::String },
                { "created", 3, LiteralType::String },
                { "status", 4, LiteralType::String },
                { "ports", 5, LiteralType::String },
                { "names", 6, LiteralType::String },
            },
        };
        state.structuredCommands[std::string("docker\0ps", 9)] = {
            .builtinCallbackName = "structured_docker_ps",
            .recordTypeId = typeId,
            .recordTypeName = "DockerPsRecord",
        };
        state.recordTypeFields["DockerPsRecord"] = {
            { "id", "string" },     { "image", "string" }, { "command", "string" }, { "created", "string" },
            { "status", "string" }, { "ports", "string" }, { "names", "string" },
        };
    }

    // DockerImagesRecord (typeId = OutputDefBase + 1 = 101)
    {
        constexpr uint16_t typeId = CoreVM::BuiltinTypeId::OutputDefBase + 1;
        state.outputDefinitionTypes["DockerImagesRecord"] = {
            .typeId = typeId,
            .fields = {
                { "id", 0, LiteralType::String },
                { "repository", 1, LiteralType::String },
                { "tag", 2, LiteralType::String },
                { "created", 3, LiteralType::String },
                { "size", 4, LiteralType::String },
            },
        };
        state.structuredCommands[std::string("docker\0images", 13)] = {
            .builtinCallbackName = "structured_docker_images",
            .recordTypeId = typeId,
            .recordTypeName = "DockerImagesRecord",
        };
        state.recordTypeFields["DockerImagesRecord"] = {
            { "id", "string" },      { "repository", "string" }, { "tag", "string" },
            { "created", "string" }, { "size", "string" },
        };
    }

    // GitLogRecord (typeId = OutputDefBase + 2 = 102)
    {
        constexpr uint16_t typeId = CoreVM::BuiltinTypeId::OutputDefBase + 2;
        state.outputDefinitionTypes["GitLogRecord"] = {
            .typeId = typeId,
            .fields = {
                { "sha", 0, LiteralType::String },
                { "author", 1, LiteralType::String },
                { "email", 2, LiteralType::String },
                { "date", 3, LiteralType::String },
                { "message", 4, LiteralType::String },
            },
        };
        state.structuredCommands[std::string("git\0log", 7)] = {
            .builtinCallbackName = "structured_git_log",
            .recordTypeId = typeId,
            .recordTypeName = "GitLogRecord",
        };
        state.recordTypeFields["GitLogRecord"] = {
            { "sha", "string" },  { "author", "string" },  { "email", "string" },
            { "date", "string" }, { "message", "string" },
        };
    }

    // GitStatusRecord (typeId = OutputDefBase + 3 = 103)
    {
        constexpr uint16_t typeId = CoreVM::BuiltinTypeId::OutputDefBase + 3;
        state.outputDefinitionTypes["GitStatusRecord"] = {
            .typeId = typeId,
            .fields = {
                { "status", 0, LiteralType::String },
                { "path", 1, LiteralType::String },
            },
        };
        state.structuredCommands[std::string("git\0status", 10)] = {
            .builtinCallbackName = "structured_git_status",
            .recordTypeId = typeId,
            .recordTypeName = "GitStatusRecord",
        };
        state.recordTypeFields["GitStatusRecord"] = {
            { "status", "string" },
            { "path", "string" },
        };
    }

    return state;
}

ExecutionResult executeSourceWithStructuredState(std::string const& source)
{
    auto& testRuntime = TestRuntime::instance();
    testRuntime.clearErrors();
    testRuntime.clearOutput();

    auto state = createMockStructuredState();

    Parser parser(testRuntime.runtime, testRuntime.report, std::make_unique<StringSource>(source));
    auto ast = parser.parse();
    if (!ast || testRuntime.hasErrors())
        return std::unexpected(TestError::ParseFailed);

    auto ir = IRGenerator::generate(*ast, testRuntime.report, testRuntime.runtime, &state);
    if (!ir || testRuntime.hasErrors())
        return std::unexpected(TestError::IRGenerationFailed);

    CoreVM::TargetCodeGenerator codegen;
    auto targetProgram = codegen.generate(ir.get());
    if (!targetProgram)
        return std::unexpected(TestError::CodeGenerationFailed);

    if (!targetProgram->link(&testRuntime.runtime, &testRuntime.report))
        return std::unexpected(TestError::LinkFailed);

    CoreVM::Function const* fn = targetProgram->findFunction("@main");
    if (!fn)
        return std::unexpected(TestError::FunctionNotFound);

    CoreVM::Runner::Globals globals;
    CoreVM::Runner runner(fn, nullptr, &globals, CoreVM::RuntimeConfig::defaultConfig(), nullptr);
    bool exitNonZero = runner.run();

    return TestExecutionSuccess { .exitCode = exitNonZero ? 1 : 0, .output = testRuntime.output() };
}

bool structuredExecutesWithOutput(std::string const& source, std::string_view expectedOutput)
{
    auto result = executeSourceWithStructuredState(source);
    return result.has_value() && result->output == expectedOutput;
}

} // namespace endo::test
