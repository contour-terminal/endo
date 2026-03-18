// SPDX-License-Identifier: Apache-2.0
#include "TestHelper.hpp"

#include <endo-language/ast/AST.hpp>
#include <endo-language/ast/ASTPrinter.hpp>
#include <endo-language/builtins/BuiltinImpls.hpp>
#include <endo-language/builtins/BuiltinSignatures.hpp>
#include <endo-language/builtins/TypeFormatters.hpp>
#include <endo-language/codegen/IRGenerator.hpp>
#include <endo-language/lexer/Lexer.hpp>
#include <endo-language/module/ModuleLoader.hpp>
#include <endo-language/parser/Parser.hpp>

#include <CoreVM/types/TypeDescriptor.hpp>
#include <CoreVM/types/TypedObject.hpp>

#include <bit>
#include <filesystem>

#include <platform/GlobMatch.hpp>

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
            { .pid = 1, .ppid = 0, .user = "root", .cpu = 0.1, .mem = 1024, .command = "/sbin/init" },
            { .pid = 42, .ppid = 1, .user = "alice", .cpu = 15.5, .mem = 4096, .command = "firefox" },
            { .pid = 100, .ppid = 1, .user = "bob", .cpu = 2.3, .mem = 2048, .command = "vim" },
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
            auto* memSize = builtins::makeSizeFromBytes(runner, p.mem * 1024);
            record->setSlot(4, reinterpret_cast<uintptr_t>(memSize));
            record->setSlot(5, reinterpret_cast<uintptr_t>(runner->newString(p.command)));
            list =
                runner->makeConsCell(reinterpret_cast<uintptr_t>(record), list, CoreVM::LiteralType::Object);
        }
        args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(list)));
    }

    void mockStructuredLs(CoreVM::Params& args)
    {
        auto* runner = args.caller();
        auto const path = std::string(args.getString(1));

        struct MockFile
        {
            char const* name;
            int64_t size;
            int64_t mode;
            int64_t mtime;
            bool isDir;
        };

        constexpr MockFile allFiles[] = {
            { .name = "docs", .size = 4096, .mode = 0755, .mtime = 1700000000, .isDir = true },
            { .name = "hello.txt", .size = 42, .mode = 0644, .mtime = 1700001000, .isDir = false },
            { .name = "script.sh", .size = 256, .mode = 0755, .mtime = 1700002000, .isDir = false },
        };

        // Determine which entries to include based on the path argument.
        auto const hasGlob = endo::containsGlobChars(path);

        auto* list = runner->makeNilList(CoreVM::LiteralType::Object);
        for (int i = 2; i >= 0; --i)
        {
            auto const& f = allFiles[i];

            // Filter: directory path returns all, glob filters by pattern, else exact name match.
            if (!path.empty() && path != "." && path != "/tmp")
            {
                auto const nameOrPattern = std::filesystem::path(path).filename().string();
                if (hasGlob)
                {
                    if (!endo::globMatchFilename(f.name, nameOrPattern))
                        continue;
                }
                else
                {
                    if (nameOrPattern != f.name)
                        continue;
                }
            }

            auto* record = runner->allocObject(CoreVM::BuiltinTypeId::FileInfo);
            record->setSlot(0, reinterpret_cast<uintptr_t>(runner->newString(f.name)));
            auto* sizeObj = builtins::makeSizeFromBytes(runner, f.size);
            record->setSlot(1, reinterpret_cast<uintptr_t>(sizeObj));
            auto* modeObj = builtins::makeFileModeFromBits(runner, f.mode);
            record->setSlot(2, reinterpret_cast<uintptr_t>(modeObj));
            auto* mtimeObj = builtins::makeDateTimeFromEpoch(runner, f.mtime);
            record->setSlot(3, reinterpret_cast<uintptr_t>(mtimeObj));
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
            { .id = 1, .state = "Running", .command = "sleep 100", .pid = 1234 },
            { .id = 2, .state = "Stopped", .command = "vim", .pid = 5678 },
            { .id = 3, .state = "Done", .command = "make build", .pid = 9012 },
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

    void mockStructuredBind(CoreVM::Params& args)
    {
        auto* runner = args.caller();

        struct MockBinding
        {
            char const* key;
            char const* action;
        };

        constexpr MockBinding bindings[] = {
            { .key = "ctrl+z", .action = "undo" },
            { .key = "ctrl+y", .action = "redo" },
            { .key = "ctrl+a", .action = "select-all" },
        };
        auto* list = runner->makeNilList(CoreVM::LiteralType::Object);
        for (int i = 2; i >= 0; --i)
        {
            auto const& b = bindings[i];
            auto* record = runner->allocObject(CoreVM::BuiltinTypeId::KeyBindingInfo);
            record->setSlot(0, reinterpret_cast<uintptr_t>(runner->newString(b.key)));
            record->setSlot(1, reinterpret_cast<uintptr_t>(runner->newString(b.action)));
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
            { .id = "abc123def",
              .image = "nginx:latest",
              .command = "/docker-entrypoint…",
              .created = "2024-01-15 10:00:00",
              .status = "Up 3 hours",
              .ports = "80/tcp",
              .names = "web-server" },
            { .id = "def456ghi",
              .image = "postgres:16",
              .command = "docker-entrypoint.s…",
              .created = "2024-01-14 08:00:00",
              .status = "Up 2 days",
              .ports = "5432/tcp",
              .names = "db-main" },
            { .id = "ghi789jkl",
              .image = "redis:7",
              .command = "docker-entrypoint.s…",
              .created = "2024-01-13 12:00:00",
              .status = "Exited (0) 1 hour ago",
              .ports = "",
              .names = "cache" },
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
            { .id = "sha256:abc",
              .repository = "nginx",
              .tag = "latest",
              .created = "2024-01-10",
              .size = "187MB" },
            { .id = "sha256:def",
              .repository = "postgres",
              .tag = "16",
              .created = "2024-01-08",
              .size = "412MB" },
            { .id = "sha256:ghi",
              .repository = "redis",
              .tag = "7",
              .created = "2024-01-05",
              .size = "130MB" },
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
            { .sha = "abc123",
              .author = "Alice",
              .email = "alice@example.com",
              .date = "2024-01-15",
              .message = "feat: add login" },
            { .sha = "def456",
              .author = "Bob",
              .email = "bob@example.com",
              .date = "2024-01-14",
              .message = "fix: null check" },
            { .sha = "ghi789",
              .author = "Alice",
              .email = "alice@example.com",
              .date = "2024-01-13",
              .message = "docs: update README" },
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
            { .status = "M", .path = "src/main.cpp" },
            { .status = "??", .path = "README.md" },
            { .status = "A", .path = ".gitignore" },
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
                auto* runner = args.caller();
                if (runner->isKnownString(rawVal))
                {
                    auto const* str =
                        reinterpret_cast<CoreVM::CoreString const*>(static_cast<uintptr_t>(rawVal));
                    capturedOutput += '"';
                    capturedOutput += std::string_view(*str);
                    capturedOutput += '"';
                }
                else
                {
                    capturedOutput += builtins::valueToString(rawVal, runner);
                }
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
        if (name == "structured_bind" && arity == 0)
            return Functor(mockStructuredBind);
        if (name == "structured_docker_ps" && arity == 0)
            return Functor(mockStructuredDockerPs);
        if (name == "structured_docker_images" && arity == 0)
            return Functor(mockStructuredDockerImages);
        if (name == "structured_git_log" && arity == 0)
            return Functor(mockStructuredGitLog);
        if (name == "structured_git_status" && arity == 0)
            return Functor(mockStructuredGitStatus);

        // DateTime operations
        if (name == "datetime_now" && arity == 0)
            return Functor(builtins::dateTimeNow);
        if (name == "datetime_from_epoch" && arity == 1)
            return Functor(builtins::dateTimeFromEpoch);

        // Markdown render (test mock — writes raw content to captured output)
        if (name == "markdown_render" && arity == 1)
            return Functor([this](CoreVM::Params& args) {
                auto* obj = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
                auto const* content =
                    reinterpret_cast<CoreVM::CoreString const*>(static_cast<uintptr_t>(obj->getSlot(0)));
                if (content)
                {
                    capturedOutput += *content;
                    capturedOutput += '\n';
                }
            });

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

/// Creates a minimal FSharpPersistentState with builtin structured commands registered.
FSharpPersistentState createDefaultPersistentState()
{
    FSharpPersistentState state;
    CoreVM::TypeRegistry registry;
    for (auto const& type: registry.allTypes())
    {
        if (!type->producingCommand.empty())
        {
            state.structuredCommands[type->producingCommand] = {
                .builtinCallbackName = "structured_" + type->producingCommand,
                .recordTypeId = type->id,
                .recordTypeName = type->name,
            };
        }
    }
    if (auto it = state.structuredCommands.find("ls"); it != state.structuredCommands.end())
        it->second.defaultStringArg = ".";
    return state;
}

std::unique_ptr<CoreVM::IRProgram> generateIR(std::string const& source, bool unusedValueDetection)
{
    auto& testRuntime = TestRuntime::instance();
    testRuntime.clearErrors();

    Parser parser(testRuntime.runtime, testRuntime.report, std::make_unique<StringSource>(source));
    auto ast = parser.parse();

    if (!ast || testRuntime.hasErrors())
    {
        return nullptr;
    }

    auto defaultState = createDefaultPersistentState();
    auto ir = IRGenerator::generate(
        *ast, testRuntime.report, testRuntime.runtime, &defaultState, unusedValueDetection);

    if (!ir || testRuntime.hasErrors())
    {
        return nullptr;
    }

    return ir;
}

bool generatesIRSuccessfully(std::string const& source, bool unusedValueDetection)
{
    auto ir = generateIR(source, unusedValueDetection);
    return ir != nullptr;
}

bool generatesIRWithError(std::string const& source,
                          std::string_view expectedErrorSubstring,
                          bool unusedValueDetection)
{
    auto ir = generateIR(source, unusedValueDetection);
    if (ir)
        return false; // Expected failure but IR generation succeeded

    auto const& messages = TestRuntime::instance().report.messages();
    for (auto const& msg: messages)
        if (msg.text.find(expectedErrorSubstring) != std::string::npos)
            return true;

    return false;
}

bool generatesIRWithError(std::string const& source,
                          std::string_view expectedErrorSubstring,
                          std::vector<std::string> const& modulePaths,
                          bool unusedValueDetection)
{
    auto& testRuntime = TestRuntime::instance();
    testRuntime.clearErrors();
    testRuntime.clearOutput();

    // Create persistent state with module loader
    FSharpPersistentState fsharpState;
    fsharpState.moduleLoader = std::make_shared<ModuleLoader>(testRuntime.runtime, testRuntime.report);
    for (auto const& path: modulePaths)
        fsharpState.moduleLoader->addSearchPath(path);

    // Parse
    Parser parser(testRuntime.runtime, testRuntime.report, std::make_unique<StringSource>(source));
    auto ast = parser.parse();
    if (!ast)
    {
        // Parse failed — check if error matches
        for (auto const& msg: testRuntime.report.messages())
            if (msg.text.find(expectedErrorSubstring) != std::string::npos)
                return true;
        return false;
    }

    // Generate IR
    auto ir = IRGenerator::generate(
        *ast, testRuntime.report, testRuntime.runtime, &fsharpState, unusedValueDetection);
    if (ir && !testRuntime.hasErrors())
        return false; // Expected failure but IR generation succeeded

    for (auto const& msg: testRuntime.report.messages())
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

ExecutionResult executeSource(std::string const& source, bool unusedValueDetection)
{
    auto& testRuntime = TestRuntime::instance();
    testRuntime.clearErrors();
    testRuntime.clearOutput();

    // Generate IR
    auto ir = generateIR(source, unusedValueDetection);
    if (!ir)
        return std::unexpected(TestError::IRGenerationFailed);

    // Generate target code
    CoreVM::TargetCodeGenerator codegen;
    auto targetProgram = codegen.generate(ir.get());
    if (!targetProgram)
        return std::unexpected(TestError::CodeGenerationFailed);

    // Register type formatters for human-readable display
    builtins::registerBuiltinFormatters(targetProgram->constants().typeRegistry());

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

ExecutionResult executeSource(std::string const& source,
                              std::vector<std::string> const& modulePaths,
                              bool unusedValueDetection)
{
    auto& testRuntime = TestRuntime::instance();
    testRuntime.clearErrors();
    testRuntime.clearOutput();

    // Create persistent state with module loader
    FSharpPersistentState fsharpState;
    fsharpState.moduleLoader = std::make_shared<ModuleLoader>(testRuntime.runtime, testRuntime.report);
    for (auto const& path: modulePaths)
        fsharpState.moduleLoader->addSearchPath(path);

    // Parse
    Parser parser(testRuntime.runtime, testRuntime.report, std::make_unique<StringSource>(source));
    auto ast = parser.parse();
    if (!ast || testRuntime.hasErrors())
        return std::unexpected(TestError::ParseFailed);

    // Generate IR with persistent state (for module support)
    auto ir = IRGenerator::generate(
        *ast, testRuntime.report, testRuntime.runtime, &fsharpState, unusedValueDetection);
    if (!ir || testRuntime.hasErrors())
        return std::unexpected(TestError::IRGenerationFailed);

    // Generate target code
    CoreVM::TargetCodeGenerator codegen;
    auto targetProgram = codegen.generate(ir.get());
    if (!targetProgram)
        return std::unexpected(TestError::CodeGenerationFailed);

    // Register type formatters for human-readable display
    builtins::registerBuiltinFormatters(targetProgram->constants().typeRegistry());

    // Link the program to the runtime
    if (!targetProgram->link(&testRuntime.runtime, &testRuntime.report))
        return std::unexpected(TestError::LinkFailed);

    // Find the main function
    CoreVM::Function const* fn = targetProgram->findFunction("@main");
    if (!fn)
        return std::unexpected(TestError::FunctionNotFound);

    // Execute
    CoreVM::Runner::Globals globals;
    CoreVM::Runner runner(fn, nullptr, &globals, CoreVM::RuntimeConfig::defaultConfig(), nullptr);
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

ExecutionResult executeInteractive(std::string const& source)
{
    auto& testRuntime = TestRuntime::instance();
    testRuntime.clearErrors();
    testRuntime.clearOutput();

    // Parse with auto-display enabled (simulates interactive shell)
    Parser parser(testRuntime.runtime, testRuntime.report, std::make_unique<StringSource>(source));
    parser.setAutoDisplay(true);
    auto ast = parser.parse();
    if (!ast || testRuntime.hasErrors())
        return std::unexpected(TestError::ParseFailed);

    // Generate IR
    auto ir = IRGenerator::generate(*ast, testRuntime.report, testRuntime.runtime);
    if (!ir || testRuntime.hasErrors())
        return std::unexpected(TestError::IRGenerationFailed);

    // Generate target code
    CoreVM::TargetCodeGenerator codegen;
    auto targetProgram = codegen.generate(ir.get());
    if (!targetProgram)
        return std::unexpected(TestError::CodeGenerationFailed);

    builtins::registerBuiltinFormatters(targetProgram->constants().typeRegistry());

    if (!targetProgram->link(&testRuntime.runtime, &testRuntime.report))
        return std::unexpected(TestError::LinkFailed);

    auto const* fn = targetProgram->findFunction("@main");
    if (!fn)
        return std::unexpected(TestError::FunctionNotFound);

    CoreVM::Runner::Globals globals;
    CoreVM::Runner runner(fn, nullptr, &globals, CoreVM::RuntimeConfig::defaultConfig(), nullptr);
    auto const exitNonZero = runner.run();
    auto const exitCode = exitNonZero ? 1 : 0;

    return TestExecutionSuccess { .exitCode = exitCode, .output = testRuntime.output() };
}

// =============================================================================
// Multi-prompt (REPL session) test helpers
// =============================================================================

ExecutionResult executeSession(std::vector<std::string> const& prompts)
{
    auto& testRuntime = TestRuntime::instance();
    FSharpPersistentState fsharpState;
    fsharpState.moduleLoader = std::make_shared<ModuleLoader>(testRuntime.runtime, testRuntime.report);

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
            parser.setKnownUnitFunctions(fsharpState.unitFunctions);
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

ExecutionResult executeSession(std::vector<std::string> const& prompts,
                               std::vector<std::string> const& modulePaths)
{
    auto& testRuntime = TestRuntime::instance();
    FSharpPersistentState fsharpState;
    fsharpState.moduleLoader = std::make_shared<ModuleLoader>(testRuntime.runtime, testRuntime.report);
    for (auto const& path: modulePaths)
        fsharpState.moduleLoader->addSearchPath(path);

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
            parser.setKnownUnitFunctions(fsharpState.unitFunctions);
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

    // Register builtin structured commands from TypeRegistry (data-driven, same as Shell.cpp)
    {
        CoreVM::TypeRegistry registry;
        for (auto const& type: registry.allTypes())
        {
            if (!type->producingCommand.empty())
            {
                state.structuredCommands[type->producingCommand] = {
                    .builtinCallbackName = "structured_" + type->producingCommand,
                    .recordTypeId = type->id,
                    .recordTypeName = type->name,
                };
            }
        }
        // ls accepts optional directory argument with default "."
        if (auto it = state.structuredCommands.find("ls"); it != state.structuredCommands.end())
            it->second.defaultStringArg = ".";
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

    // Register type formatters for human-readable display
    builtins::registerBuiltinFormatters(targetProgram->constants().typeRegistry());
    // Set generic product formatter for output definition types
    for (auto const& [name, defType]: state.outputDefinitionTypes)
    {
        if (auto* td = targetProgram->constants().typeRegistry().getMutable(defType.typeId))
            td->formatFn = builtins::formatProduct;
    }

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
