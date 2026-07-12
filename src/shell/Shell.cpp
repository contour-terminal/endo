// SPDX-License-Identifier: Apache-2.0
#include "Shell.hpp"
#include <shell/builtins/InlineCommandDescriptor.hpp>
#include <shell/completion/ScriptedCompleter.hpp>
#include <shell/history/RequiredPaths.hpp>
#include <shell/ui/Prompt.hpp>
#include <shell/ui/PromptPresets.hpp>
#include <shell/ui/RichConsoleReport.hpp>
#include <shell/ui/SyntaxHighlighter.hpp>
#include <shell/ui/modules/GitModule.hpp>

#include <endo-language/LogCategories.hpp>
#include <endo-language/LogConfig.hpp>
#include <endo-language/ast/ASTPrinter.hpp>
#include <endo-language/builtins/TypeFormatters.hpp>
#include <endo-language/codegen/IRGenerator.hpp>
#include <endo-language/ide/TypeRegistryCompletionAdapter.hpp>
#include <endo-language/lexer/Lexer.hpp>
#include <endo-language/module/ModuleLoader.hpp>
#include <endo-language/parser/Parser.hpp>

#include <tui/Canvas.hpp>
#include <tui/CommandRegistry.hpp>
#include <tui/GenericSyntaxHighlighter.hpp>
#include <tui/ImageLoader.hpp>
#include <tui/MarkdownRenderer.hpp>
#include <tui/QuestionComponent.hpp>
#include <tui/Screen.hpp>
#include <tui/Theme.hpp>
#include <tui/runtime/TerminalEventSource.hpp>
#include <tui/runtime/TuiRuntime.hpp>

#include <CoreVM/CoreVM.hpp>
#include <CoreVM/types/TypeDescriptor.hpp>

#include <crispy/assert.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <future>
#include <iostream>
#include <print>
#include <set>
#include <unordered_set>

#include "Error.hpp"
#include "TTY.hpp"
#if defined(ENDO_ENABLE_AGENT) && ENDO_ENABLE_AGENT
    #include <shell/AgentContext.hpp>
    #include <shell/AgentModeSession.hpp>

    #include <agent/AgentConfig.hpp>
    #include <agent/HeadlessRunner.hpp>
    #include <agent/PermissionManager.hpp>
    #include <agent/RunCommand.hpp>
    #include <agent/commands/AgentHistoryProvider.hpp>
    #include <agent/commands/FilePathCompleter.hpp>
    #include <agent/commands/SlashCommandCompleter.hpp>
    #include <agent/commands/SlashCommandRegistry.hpp>
    #include <agent/commands/SlashCommands.hpp>
    #include <agent/context/FileReferenceExpander.hpp>
    #include <agent/context/ProjectContextLoader.hpp>
    #include <agent/context/SystemPromptBuilder.hpp>
    #include <agent/conversation/ConversationHistoryStore.hpp>
    #include <agent/conversation/SessionManager.hpp>
    #include <agent/mcp/McpToolAdapter.hpp>
    #include <agent/providers/ProviderFactory.hpp>
    #include <agent/providers/ProviderModels.hpp>
    #include <agent/session/AgentMessages.hpp>
    #include <agent/session/AgentSession.hpp>
    #include <agent/session/AgentWorker.hpp>
    #include <agent/session/PlanExecutor.hpp>
    #include <agent/tools/AskUserTool.hpp>
    #include <agent/tools/DiffRenderer.hpp>
    #include <agent/tools/EditFileTool.hpp>
    #include <agent/tools/EndoExecuteTool.hpp>
    #include <agent/tools/ExploreTool.hpp>
    #include <agent/tools/GitTool.hpp>
    #include <agent/tools/GlobTool.hpp>
    #include <agent/tools/GrepTool.hpp>
    #include <agent/tools/ListDirectoryTool.hpp>
    #include <agent/tools/ReadFileTool.hpp>
    #include <agent/tools/SaveMemoryTool.hpp>
    #include <agent/tools/SearchTool.hpp>
    #include <agent/tools/ShellExecuteTool.hpp>
    #include <agent/tools/SubmitPlanTool.hpp>
    #include <agent/tools/ToolRegistry.hpp>
    #include <agent/tools/WebFetchTool.hpp>
    #include <agent/tools/WebSearchTool.hpp>
    #include <agent/tools/WriteFileTool.hpp>
    #include <agent/tracing/AgentTracer.hpp>
    #include <agent/tracing/TraceTerminalRenderer.hpp>
    #include <agent/ui/AgentInputComponent.hpp>
    #include <agent/ui/AgentResponseRenderer.hpp>
    #include <agent/ui/ToolStatusComponent.hpp>
#endif
#include <nlohmann/json.hpp>
#include <platform/InstallPaths.hpp>
#include <platform/NativeFileSystem.hpp>
#include <platform/PathUtils.hpp>
#include <platform/Pipe.hpp>
#include <platform/Process.hpp>
#include <platform/SignalHandler.hpp>
#include <platform/SystemInfo.hpp>
#include <platform/Types.hpp>
#include <platform/UserPaths.hpp>
#if defined(_WIN32)
    #include <platform/windows/WindowsEnvironmentProvider.hpp>
#else
    #include <cerrno>
    #include <csignal>

    #include <sys/wait.h>

    #include <fcntl.h>
    #include <poll.h>
    #include <unistd.h>

    #include <platform/posix/PosixEnvironmentProvider.hpp>
#endif

namespace
{

auto& debugLog()
{
    return endo::log::shellDebug();
}

auto& traceLog()
{
    return endo::log::vmTrace();
}

auto& irLog()
{
    return endo::log::vmIR();
}

/// Simple RAII scope guard that invokes a callable on destruction.
template <typename F>
struct ScopeGuard
{
    F cleanup;
    bool active = true;

    explicit ScopeGuard(F f): cleanup(std::move(f)) {}

    ~ScopeGuard()
    {
        if (active)
            cleanup();
    }

    ScopeGuard(ScopeGuard const&) = delete;
    ScopeGuard& operator=(ScopeGuard const&) = delete;
};

#if defined(ENDO_ENABLE_AGENT) && ENDO_ENABLE_AGENT
    #if !defined(_WIN32)
auto shellExecImpl(std::string const& shellPath,
                   std::string const& command,
                   std::chrono::milliseconds timeout) -> endo::agent::ShellExecResult
{
    auto pipeFds = std::array<int, 2> {};
    if (pipe(pipeFds.data()) != 0)
        return endo::agent::ShellExecResult { .output = "Failed to create pipe", .exitCode = -1 };

    auto const pid = fork();
    if (pid < 0)
    {
        close(pipeFds[0]);
        close(pipeFds[1]);
        return endo::agent::ShellExecResult { .output = "Failed to fork process", .exitCode = -1 };
    }

    if (pid == 0)
    {
        close(pipeFds[0]);
        dup2(pipeFds[1], STDOUT_FILENO);
        dup2(pipeFds[1], STDERR_FILENO);
        close(pipeFds[1]);

        sigset_t mask {};
        sigemptyset(&mask);
        sigaddset(&mask, SIGCHLD);
        sigaddset(&mask, SIGTSTP);
        sigaddset(&mask, SIGCONT);
        sigaddset(&mask, SIGINT);
        sigprocmask(SIG_UNBLOCK, &mask, nullptr);

        signal(SIGINT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGPIPE, SIG_DFL);

        execl(shellPath.c_str(), shellPath.c_str(), "-c", command.c_str(), nullptr);
        _exit(127);
    }

    close(pipeFds[1]);

    auto output = std::string {};
    auto buffer = std::array<char, 4096> {};
    auto const deadline = std::chrono::steady_clock::now() + timeout;
    auto timedOut = false;

    auto pfd = pollfd { .fd = pipeFds[0], .events = POLLIN, .revents = 0 };
    for (;;)
    {
        auto const remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0)
        {
            timedOut = true;
            break;
        }
        auto const pollResult = poll(&pfd, 1, static_cast<int>(remaining.count()));
        if (pollResult < 0)
        {
            if (errno == EINTR)
                continue;
            break;
        }
        if (pollResult == 0)
        {
            timedOut = true;
            break;
        }
        auto const bytesRead = read(pipeFds[0], buffer.data(), buffer.size());
        if (bytesRead < 0)
        {
            if (errno == EINTR)
                continue;
            break;
        }
        if (bytesRead == 0)
            break;
        output.append(buffer.data(), static_cast<size_t>(bytesRead));
    }
    close(pipeFds[0]);

    if (timedOut)
    {
        kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
        return endo::agent::ShellExecResult { .output = std::move(output), .exitCode = -1, .timedOut = true };
    }

    auto status = 0;
    waitpid(pid, &status, 0);
    auto const exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    return endo::agent::ShellExecResult { .output = std::move(output), .exitCode = exitCode };
}
    #else
auto shellExecImpl(std::string const& command, std::chrono::milliseconds timeout)
    -> endo::agent::ShellExecResult
{
    SECURITY_ATTRIBUTES sa {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0))
        return endo::agent::ShellExecResult { .output = "Failed to create pipe", .exitCode = -1 };

    if (!SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0))
    {
        CloseHandle(readPipe);
        CloseHandle(writePipe);
        return endo::agent::ShellExecResult { .output = "Failed to configure pipe", .exitCode = -1 };
    }

    STARTUPINFOW si {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi {};

    auto const narrowCmdLine = std::string("cmd.exe /c ") + command;
    auto const wideLen = MultiByteToWideChar(CP_UTF8, 0, narrowCmdLine.c_str(), -1, nullptr, 0);
    auto cmdLine = std::wstring(static_cast<size_t>(wideLen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, narrowCmdLine.c_str(), -1, cmdLine.data(), wideLen);

    if (!CreateProcessW(
            nullptr, cmdLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    {
        CloseHandle(readPipe);
        CloseHandle(writePipe);
        return endo::agent::ShellExecResult { .output = "Failed to create process", .exitCode = -1 };
    }

    CloseHandle(writePipe);

    auto output = std::string {};
    auto buffer = std::array<char, 4096> {};
    auto const deadline = std::chrono::steady_clock::now() + timeout;
    auto timedOut = false;

    for (;;)
    {
        auto const remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0)
        {
            timedOut = true;
            break;
        }

        DWORD bytesAvailable = 0;
        if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &bytesAvailable, nullptr))
            break;

        if (bytesAvailable == 0)
        {
            if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0)
            {
                DWORD bytesRead = 0;
                while (
                    ReadFile(readPipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr)
                    && bytesRead > 0)
                    output.append(buffer.data(), bytesRead);
                break;
            }
            Sleep(10);
            continue;
        }

        DWORD bytesRead = 0;
        if (!ReadFile(readPipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr)
            || bytesRead == 0)
            break;
        output.append(buffer.data(), bytesRead);
    }
    CloseHandle(readPipe);

    if (timedOut)
    {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return endo::agent::ShellExecResult { .output = std::move(output), .exitCode = -1, .timedOut = true };
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return endo::agent::ShellExecResult { .output = std::move(output),
                                          .exitCode = static_cast<int>(exitCode) };
}
    #endif
#endif // ENDO_ENABLE_AGENT (shellExecImpl)

} // namespace

namespace endo
{

using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;

std::string readLine(TTY& tty, std::string_view prompt)
{
    // Most super-native implementation, yet to be replaced by a proper line editor.
    tty.writeToStdout(std::format("{}", prompt));
    std::string line;
    while (true)
    {
        char ch {};
        auto const n = platformRead(tty.inputFd(), &ch, 1);
        if (n == 0)
        {
            break;
        }
        else if (n == -1)
        {
            if (errno == EINTR)
                continue;
            else
                break;
        }
        else if (ch == '\n')
        {
            break;
        }
        else
        {
            line += ch;
        }
    }
    return line;
}

// ========================================================================
// Shell::PipelineBuilder implementation
// ========================================================================

auto Shell::PipelineBuilder::requestShellPipe(bool lastInChain) -> IODescriptors
{
    NativeHandle const stdinFd = !currentPipe ? defaultStdinFd : currentPipe->releaseReader();
    lastReleasedReaderFd = (stdinFd != defaultStdinFd) ? stdinFd : InvalidHandle;
    if (lastInChain)
        currentPipe = nullptr;
    else if (auto pipeResult = createPipe(); pipeResult.has_value())
        currentPipe = std::move(pipeResult.value());
    else
        currentPipe = nullptr; // Error case - will result in using default stdout
    NativeHandle const stdoutFd = lastInChain || !currentPipe ? defaultStdoutFd : currentPipe->writer();
    return IODescriptors { .reader = stdinFd, .writer = stdoutFd };
}

void Shell::PipelineBuilder::closeCurrentPipeWriter() const
{
    if (currentPipe)
        currentPipe->closeWriter();
}

void Shell::PipelineBuilder::closePipeFdsInParent()
{
    if (lastReleasedReaderFd != InvalidHandle)
    {
        platformClose(lastReleasedReaderFd);
        lastReleasedReaderFd = InvalidHandle;
    }
    closeCurrentPipeWriter();
}

// ========================================================================
// Shell::RedirectState implementation
// ========================================================================

void Shell::RedirectState::clear()
{
    entries.clear();
}

void Shell::RedirectState::addInputFile(int targetFd, std::string path)
{
    entries.push_back({ .type = Type::InputFile, .targetFd = targetFd, .path = std::move(path) });
}

void Shell::RedirectState::addOutputFile(int sourceFd, std::string path, bool append)
{
    entries.push_back(
        { .type = Type::OutputFile, .sourceFd = sourceFd, .path = std::move(path), .append = append });
}

void Shell::RedirectState::addFdDup(int sourceFd, int targetFd)
{
    entries.push_back({ .type = Type::FdDup, .sourceFd = sourceFd, .targetFd = targetFd });
}

void Shell::RedirectState::addHereDoc(int targetFd, std::string content)
{
    entries.push_back({ .type = Type::HereDoc, .targetFd = targetFd, .content = std::move(content) });
}

void Shell::RedirectState::addHereString(int targetFd, std::string content)
{
    entries.push_back({ .type = Type::HereString, .targetFd = targetFd, .content = std::move(content) });
}

NativeHandle Shell::RedirectState::getEffectiveStdoutFd(NativeHandle defaultFd, ProcessManager& pm)
{
    for (auto& entry: entries)
    {
        if (entry.type == Type::OutputFile && entry.sourceFd == STDOUT_FILENO)
        {
            // Open the file if not already open
            if (entry.openedFd == InvalidHandle)
            {
                int const oflags =
                    entry.append ? (O_WRONLY | O_CREAT | O_APPEND) : (O_WRONLY | O_CREAT | O_TRUNC);
                auto const result = pm.openFile(entry.path, oflags);
                if (result.has_value())
                    entry.openedFd = result.value();
            }
            if (entry.openedFd != InvalidHandle)
                return entry.openedFd;
        }
    }
    return defaultFd;
}

NativeHandle Shell::RedirectState::getEffectiveStdinFd(NativeHandle defaultFd, ProcessManager& pm)
{
    for (auto& entry: entries)
    {
        if (entry.type == Type::InputFile && entry.targetFd == STDIN_FILENO)
        {
            // Open the file if not already open
            if (entry.openedFd == InvalidHandle)
            {
                auto const result = pm.openFile(entry.path, O_RDONLY);
                if (result.has_value())
                    entry.openedFd = result.value();
            }
            if (entry.openedFd != InvalidHandle)
                return entry.openedFd;
        }
        else if ((entry.type == Type::HereDoc || entry.type == Type::HereString)
                 && entry.targetFd == STDIN_FILENO)
        {
            // Lazily create the pipe if not already created
            if (entry.openedFd == InvalidHandle)
            {
                auto pipeResult = createPipe();
                if (pipeResult.has_value())
                {
                    auto pipe = std::move(pipeResult.value());
                    // Write content to pipe
                    platformWrite(pipe->writer(), entry.content.data(), entry.content.size());
                    // Add trailing newline for herestrings if needed
                    if (entry.type == Type::HereString && !entry.content.empty()
                        && entry.content.back() != '\n')
                    {
                        platformWrite(pipe->writer(), "\n", 1);
                    }
                    pipe->closeWriter();
                    entry.openedFd = pipe->releaseReader();
                }
            }
            if (entry.openedFd != InvalidHandle)
                return entry.openedFd;
        }
    }
    return defaultFd;
}

// ========================================================================
// Shell::SubstitutionCapture implementation
// ========================================================================

void Shell::SubstitutionCapture::clear()
{
    pipe.reset();
    if (savedStdout != InvalidHandle)
    {
        savedStdout = InvalidHandle;
    }
    output.clear();
}

// ========================================================================
// TypeRegistry cache (computed once, reused for all Shell instances)
// ========================================================================

namespace
{

    /// Cached data derived from CoreVM::TypeRegistry (immutable after construction).
    struct TypeRegistryCachedData
    {
        std::unordered_map<std::string, std::vector<endo::RecordFieldInfo>> recordTypeFields;
        endo::ModuleFunctionMap moduleFunctions;
        std::unordered_map<std::string, std::string> commandOutputTypes;
        std::unordered_map<std::string, endo::FSharpPersistentState::StructuredCommandInfo>
            structuredCommands;
    };

    /// Returns cached TypeRegistry-derived data (computed once, reused for all Shell instances).
    TypeRegistryCachedData const& cachedTypeRegistryData()
    {
        static auto const instance = [] {
            CoreVM::TypeRegistry registry;
            TypeRegistryCachedData data;
            data.recordTypeFields = endo::builtinRecordFields(registry);
            data.moduleFunctions = endo::builtinModuleFunctions(registry);
            data.commandOutputTypes = endo::builtinCommandOutputTypes(registry);

            for (auto const& type: registry.allTypes())
            {
                if (!type->producingCommand.empty())
                {
                    data.structuredCommands[type->producingCommand] = {
                        .builtinCallbackName = "structured_" + type->producingCommand,
                        .recordTypeId = type->id,
                        .recordTypeName = type->name,
                    };
                }
            }
            if (auto it = data.structuredCommands.find("ls"); it != data.structuredCommands.end())
                it->second.defaultStringArg = ".";

            return data;
        }();
        return instance;
    }

} // namespace

// ========================================================================
// Shell implementation
// ========================================================================

#if defined(_WIN32)
Shell::Shell(): Shell(WindowsTTY::instance(), WindowsEnvironmentProvider::instance())
{
}
#else
Shell::Shell(): Shell(RealTTY::instance(), PosixEnvironmentProvider::instance())
{
}
#endif

Shell::Shell(TTY& tty, EnvironmentProvider& env): Shell(tty, env, NativeFileSystem::instance())
{
}

void Shell::setSixelCapability(std::unique_ptr<SixelCapabilityProvider> provider)
{
    _sixelCapability = std::move(provider);
}

Shell::Shell(TTY& tty, EnvironmentProvider& env, FileSystem& fs):
    _fs { fs },
    _env { env },
    _tty { tty },
    _sixelCapability { std::make_unique<TerminalSixelCapability>(tty, env) },
    _processManager {
#if defined(_WIN32)
        WindowsProcessManager::instance()
#else
        PosixProcessManager::instance()
#endif
    }
{
    _currentPipelineBuilder.defaultStdinFd = _tty.inputFd();
    _currentPipelineBuilder.defaultStdoutFd = _tty.outputFd();

    // SHELL must be a fully-qualified path (not a bare name): programs such as
    // sudo-rs' `sudo -s` read SHELL and refuse to spawn it unless it resolves to
    // an absolute path. Fall back to "endo" only if the path cannot be determined.
    if (auto const exePath = endo::platform::executablePath())
        _env.setAndExport("SHELL", endo::platform::normalizePath(*exePath));
    else
        _env.setAndExport("SHELL", "endo");
    _env.set("PWD", _env.currentDirectory());

    // Track shell nesting level (0 = outermost)
    if (auto const shlvl = _env.get("ENDO_SHLVL"); shlvl.has_value())
    {
        try
        {
            _shellLevel = std::stoi(std::string(*shlvl)) + 1;
        }
        catch (...)
        {
            _shellLevel = 0;
        }
    }
    _env.setAndExport("ENDO_SHLVL", std::to_string(_shellLevel));

    updateTerminalSizeEnv();

    // Capture the shell's process ID at startup
#if !defined(_WIN32)
    _shellPid = static_cast<ProcessId>(getpid());
    _shellPgid = static_cast<ProcessId>(getpgrp());
#else
    _shellPid = static_cast<ProcessId>(GetCurrentProcessId());
    _shellPgid = 0;
#endif

    // Initialize signal handling (returns signalfd on Linux, -1 otherwise)
    _signalFd = SignalHandler::initialize(this);

    // Let a blocked event-source wait wake promptly on Ctrl+C. On Windows the
    // console control handler runs on another thread and WaitForMultipleObjects has
    // no EINTR, so without this an agent/auth wait would not wake until the next
    // event or timeout. The wakeup outlives the registration (member of this Shell).
    SignalHandler::setInterruptWakeup(&_interruptWakeup);

    // Seed built-in record type fields, module functions, and command output types from TypeRegistry (cached)
    {
        auto const& cached = cachedTypeRegistryData();
        _fsharpState.recordTypeFields = cached.recordTypeFields;
        _fsharpState.moduleFunctions = cached.moduleFunctions;
        _fsharpState.commandOutputTypes = cached.commandOutputTypes;
        _fsharpState.structuredCommands = cached.structuredCommands;
    }

    // Initialize module loader for import/open support
    {
        _fsharpState.moduleLoader = std::make_shared<endo::ModuleLoader>(_runtime, _moduleReport, _fs);

        // Add search paths: user modules, then system stdlib
        if (auto const home = _env.get("HOME"); home && !home->empty())
        {
            auto const userModulesDir = std::filesystem::path(*home) / ".config" / "endo" / "modules";
            if (std::filesystem::exists(userModulesDir))
                _fsharpState.moduleLoader->addSearchPath(userModulesDir);
        }

        // System stdlib path (relative to executable)
        if (auto const stdlibDir = endo::platform::resolveDataDir("stdlib"); !stdlibDir.empty())
            _fsharpState.moduleLoader->addSearchPath(stdlibDir);
    }

    // Load output definition files for structured pipelines

    // 1. Installed location (relative to executable)
    if (auto const dir = endo::platform::resolveDataDir("definitions"); !dir.empty())
        _outputDefinitions.loadFromDirectory(dir, _fs);

    // 2. Development fallback (source tree)
#if defined(ENDO_DEFINITIONS_DIR)
    _outputDefinitions.loadFromDirectory(ENDO_DEFINITIONS_DIR, _fs);
#endif

    // 3. User overrides
#if defined(_WIN32)
    if (auto const* appData = std::getenv("LOCALAPPDATA"))
        _outputDefinitions.loadFromDirectory(std::filesystem::path(appData) / "endo" / "definitions", _fs);
    else if (auto const* userProfile = std::getenv("USERPROFILE"))
        _outputDefinitions.loadFromDirectory(
            std::filesystem::path(userProfile) / ".config" / "endo" / "definitions", _fs);
#else
    if (auto const* home = std::getenv("HOME"))
        _outputDefinitions.loadFromDirectory(std::filesystem::path(home) / ".config" / "endo" / "definitions",
                                             _fs);
#endif

    // Register output definition types and structured commands in persistent state
    {
        uint16_t nextTypeId = CoreVM::BuiltinTypeId::OutputDefBase;
        for (auto& def: const_cast<std::vector<OutputDefinition>&>(_outputDefinitions.definitions()))
        {
            for (auto& variant: def.variants)
            {
                variant.assignedTypeId = nextTypeId;

                // Register record type in persistent state
                FSharpPersistentState::OutputDefRecordType defType;
                defType.typeId = nextTypeId;
                for (size_t i = 0; i < variant.schema.size(); ++i)
                {
                    defType.fields.push_back(CoreVM::FieldInfo {
                        .name = variant.schema[i].name,
                        .offset = static_cast<uint8_t>(i),
                        .type = variant.schema[i].type,
                    });
                }
                _fsharpState.outputDefinitionTypes[variant.recordTypeName] = std::move(defType);

                // Register structured command lookup and output type mapping
                for (auto const& matchPattern: variant.matches)
                {
                    std::string key = def.command;
                    for (auto const& arg: matchPattern)
                    {
                        key += '\0';
                        key += arg;
                    }
                    _fsharpState.structuredCommands[key] = {
                        .builtinCallbackName = "structured_" + variant.fsharpName,
                        .recordTypeId = nextTypeId,
                        .recordTypeName = variant.recordTypeName,
                    };
                    // Register for pipeline completion (NUL-key for multi-word, bare for simple)
                    _fsharpState.commandOutputTypes[key] = variant.recordTypeName;
                }

                // Register record type fields for completion
                std::vector<RecordFieldInfo> fieldInfos;
                fieldInfos.reserve(variant.schema.size());
                for (auto const& field: variant.schema)
                {
                    auto const* typeName = [&]() -> char const* {
                        if (field.type == CoreVM::LiteralType::Number)
                            return "int";
                        if (field.type == CoreVM::LiteralType::Boolean)
                            return "bool";
                        return "string";
                    }();
                    fieldInfos.push_back({ .name = field.name, .typeName = typeName });
                }
                _fsharpState.recordTypeFields[variant.recordTypeName] = std::move(fieldInfos);

                ++nextTypeId;
            }
        }
    }

    // Register inline builtins for diagnostics, completions, and LSP hover.
    // This ensures that every entry in InlineCommandDescriptors is automatically
    // recognized — no separate list to maintain.
    registerInlineBuiltins(inlineBuiltinInfos(inlineCommandDescriptors()));

    // Register Endo syntax highlighter for agent-mode code blocks and diffs
    tui::registerEndoHighlighter(
        [](std::string_view line,
           tui::HighlightState /*state*/) -> std::pair<tui::HighlightMap, tui::HighlightState> {
            auto const endoMap = endo::computeHighlightMap(line);
            auto map = tui::HighlightMap(line.size(), tui::HighlightCategory::Default);
            for (std::size_t i = 0; i < endoMap.size() && i < map.size(); ++i)
                map[i] = static_cast<tui::HighlightCategory>(endoMap[i]);
            return { std::move(map), tui::HighlightState::Normal };
        });

    // NB: These lines could go away once we have a proper command line parser and
    //     the ability to set these options from the command line.
    registerBuiltinFunctions();

    // Wire the prompt's dynamic-field resolver so that user-assigned function
    // values on `shell_prompt_indicator` (and future dynamic fields) are invoked
    // at each prompt render.
    prompt.setDynamicFieldResolver(
        [this](std::string const& fnName) { return invokePromptCallback(fnName); });

    _dirConfigManager =
        std::make_unique<DirectoryConfigManager>(*this, _fs, _env, stderrDiagnosticSink(_tty));

    // Register dark/light mode auto-switching via terminal color scheme detection
    prompt.terminal().onColorSchemeChanged([this](tui::ColorScheme scheme) {
        auto& mgr = tui::ThemeManager::instance();
        mgr.setCurrent(scheme == tui::ColorScheme::Light ? tui::lightTheme() : tui::darkTheme());
        prompt.setTheme(mgr.current());
        // Re-apply prompt preset with appropriate colors for new color scheme,
        // but preserve user color overrides (e.g., from init.endo or interactive config).
        auto const& currentName = prompt.promptConfig().name;
        if (!currentName.empty())
        {
            auto savedOverrides = prompt.promptConfig().colorOverrides;
            auto config = promptPreset(currentName, scheme);
            config.colorOverrides = std::move(savedOverrides);
            prompt.setPromptConfig(std::move(config));
        }
    });
}

Shell::~Shell()
{
    // Clear the interrupt-wakeup registration before _interruptWakeup is destroyed,
    // so a late signal cannot dereference a dangling pointer.
    SignalHandler::setInterruptWakeup(nullptr);
    SignalHandler::restore();
}

EnvironmentProvider& Shell::environment() noexcept
{
    return _env;
}

EnvironmentProvider const& Shell::environment() const noexcept
{
    return _env;
}

void Shell::setOptimize(bool optimize)
{
    _optimize = optimize;
}

#if defined(ENDO_ENABLE_AGENT) && ENDO_ENABLE_AGENT
void Shell::setAgentTracePath(std::string path)
{
    _agentTracePath = std::move(path);
}
#endif

// NOLINTNEXTLINE(readability-make-member-function-const)
void Shell::addModuleSearchPath(std::filesystem::path path)
{
    if (_fsharpState.moduleLoader)
        _fsharpState.moduleLoader->addSearchPath(std::move(path));
}

void Shell::setSourceFile(std::filesystem::path path)
{
    _fsharpState.sourceFilePath = std::move(path);
}

void Shell::setInteractive(bool interactive)
{
    _interactive = interactive;
}

void Shell::setPositionalParameters(std::vector<std::string> params)
{
    _positionalParameters = std::move(params);
}

void Shell::updateTerminalSizeEnv()
{
    if (auto const size = _tty.getSize(); size.has_value())
    {
        _env.set("LINES", std::to_string(size->rows));
        _env.set("COLUMNS", std::to_string(size->cols));
    }
}

// ========================================================================
// Shell integration (OSC 133) and CWD propagation (OSC 7)
// ========================================================================

void Shell::emitPromptStart()
{
    if (_interactive && _tty.isTerminal())
        _tty.writeToStdout("\033]133;A\033\\");
}

void Shell::emitPromptEnd()
{
    if (_interactive && _tty.isTerminal())
        _tty.writeToStdout("\033]133;B\033\\");
}

void Shell::emitCommandStart()
{
    if (_interactive && _tty.isTerminal())
        _tty.writeToStdout("\033]133;C\033\\");
}

void Shell::emitCommandFinished(int exitCode)
{
    if (_interactive && _tty.isTerminal())
        _tty.writeToStdout(std::format("\033]133;D;{}\033\\", exitCode));
}

void Shell::emitCurrentWorkingDirectory()
{
    if (!_interactive || !_tty.isTerminal())
        return;

    auto const cwd = _env.get("PWD").value_or(_env.currentDirectory());

    // Percent-encode the path for the file:// URI
    auto encoded = std::string();
    for (auto const ch: cwd)
    {
        if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '/' || ch == '-' || ch == '_' || ch == '.'
            || ch == '~')
            encoded += ch;
        else
            encoded += std::format("%{:02X}", static_cast<unsigned char>(ch));
    }

    // Get hostname for the file:// URI
    auto const hostname = platform::hostName();

    _tty.writeToStdout(std::format("\033]7;file://{}{}\033\\", hostname, encoded));
}

void Shell::emitWindowTitle(std::string_view title)
{
    if (!_interactive || !_tty.isTerminal())
        return;

    // Sanitize: strip C0, DEL, and C1 control characters
    // to prevent escape injection in the OSC payload.
    auto sanitized = std::string {};
    sanitized.reserve(title.size());
    for (auto const ch: title)
    {
        auto const uch = static_cast<unsigned char>(ch);
        if (uch < 0x20 || uch == 0x7F || (uch >= 0x80 && uch <= 0x9F))
            continue;
        sanitized += ch;
    }

    _tty.writeToStdout(std::format("\033]2;{}\033\\", sanitized));
}

void Shell::loadInitScript()
{
#if defined(ENDO_ENABLE_AGENT) && ENDO_ENABLE_AGENT
    // Load API keys from agent.yml (init.endo overrides all other settings).
    agentConfig = agent::loadAgentConfig();
#endif

    // Auto-execute init.endo if it exists.
    if (auto const configDir = platform::configHome())
    {
        auto const initPath = *configDir / "endo" / "init.endo";
        if (_fs.exists(initPath))
        {
            if (auto content = _fs.readFile(initPath))
            {
                if (auto const initResult = executeConfigScript(*content, platform::normalizePath(initPath));
                    initResult != 0)
                    _tty.writeToStderr(
                        std::format("endo: warning: init.endo exited with code {}\n", initResult));
            }
            else
            {
                _tty.writeToStderr(std::format("endo: warning: error loading {}: {}\n",
                                               platform::normalizePath(initPath),
                                               content.error()));
            }
        }
    }
}

int Shell::executeConfigScript(std::string const& content, std::string_view sourceName)
{
    // Save state that execute() overwrites — needed for re-entrant safety
    // when called from a running VM (e.g., dirconfig allow triggers config loading)
    auto savedProgram = std::move(_currentProgram);
    auto* const savedRunner = _runner;

    auto const savedUnusedDetection = _unusedValueDetection;
    ++_configScriptDepth;
    _unusedValueDetection = false;
    int result = 0;
    try
    {
        result = execute(content, sourceName);
    }
    catch (std::exception const& e)
    {
        _tty.writeToStderr(std::format("endo: warning: error executing {}: {}\n", sourceName, e.what()));
        result = 1;
    }
    --_configScriptDepth;
    _unusedValueDetection = savedUnusedDetection;

    // Restore outer program and runner so the calling VM can resume safely
    _currentProgram = std::move(savedProgram);
    _runner = savedRunner;

    return result;
}

void Shell::onDirectoryChanged()
{
    if (_dirConfigManager)
        _dirConfigManager->onDirectoryChanged(_env.currentDirectory());
}

void Shell::loadCompleters()
{
    std::set<std::string> seenBasenames;

    auto const loadDir = [&](std::filesystem::path const& dir) {
        if (!_fs.exists(dir) || !_fs.isDirectory(dir))
            return;

        // Collect .endo files and sort alphabetically for deterministic load order
        auto const entries = _fs.listDirectory(dir);
        if (!entries)
            return;

        std::vector<std::filesystem::path> files;
        for (auto const& entry: *entries)
        {
            if (entry.isRegularFile && entry.path.extension() == ".endo")
                files.push_back(entry.path);
        }
        std::ranges::sort(files);

        for (auto const& path: files)
        {
            auto const basename = path.filename().string(); // no separators in filename
            if (seenBasenames.contains(basename))
                continue;
            seenBasenames.insert(basename);

            if (auto content = _fs.readFile(path))
            {
                (void) executeConfigScript(*content, platform::normalizePath(path));
            }
            else
            {
                _tty.writeToStderr(std::format("endo: warning: error loading completer {}: {}\n",
                                               platform::normalizePath(path),
                                               content.error()));
            }
        }
    };

    // User overrides first
    if (auto const configDir = platform::configHome())
        loadDir(*configDir / "endo" / "completers");

    // Installed location (relative to executable)
    if (auto const dir = endo::platform::resolveDataDir("completers"); !dir.empty())
        loadDir(dir);

    // Development fallback (source tree)
#ifdef ENDO_COMPLETERS_DIR
    loadDir(ENDO_COMPLETERS_DIR);
#endif

    // Register the ScriptedCompleter provider if any completers were loaded
    if (!_completerFunctions.commands().empty())
    {
        completer->addProvider(std::make_unique<ScriptedCompleter>(
            _completerFunctions,
            [this](std::string_view funcName, std::vector<std::string> const& args, std::string_view prefix) {
                return executeCompleterFunction(funcName, args, prefix);
            }));
    }
}

CompleterExecutionResult Shell::executeCompleterFunction(std::string_view funcName,
                                                         std::vector<std::string> const& args,
                                                         std::string_view prefix)
{
    // Build the expression: __collect_completions (funcName [args] "prefix")
    // Uses direct function application instead of pipeline because the |> handler
    // only resolves F# user functions, not native Runtime functions.
    std::string expr = "__collect_completions (";
    expr += funcName;
    expr += " [";
    for (size_t i = 0; i < args.size(); ++i)
    {
        if (i > 0)
            expr += "; ";
        expr += '"';
        for (auto c: args[i])
        {
            if (c == '"')
                expr += "\\\"";
            else if (c == '\\')
                expr += "\\\\";
            else
                expr += c;
        }
        expr += '"';
    }
    expr += "] \"";
    for (auto c: prefix)
    {
        if (c == '"')
            expr += "\\\"";
        else if (c == '\\')
            expr += "\\\\";
        else
            expr += c;
    }
    expr += "\")";

    // Clear collection buffer
    _collectedCompletions.clear();

    // Use buffering report to capture errors instead of writing to stderr
    BufferingConsoleReport bufferingReport;
    bufferingReport.setSourceText(expr);

    auto const savedUnusedDetection = _unusedValueDetection;
    ++_configScriptDepth;
    _unusedValueDetection = false;
    (void) execute(expr, bufferingReport);
    --_configScriptDepth;
    _unusedValueDetection = savedUnusedDetection;

    // Collect results from the bridge function
    CompleterExecutionResult result;
    result.completions = std::move(_collectedCompletions);

    // Capture any compilation/link errors
    if (bufferingReport.hasMessages())
    {
        result.errors.reserve(bufferingReport.formattedMessages().size());
        for (auto const& msg: bufferingReport.formattedMessages())
            result.errors.push_back(msg);
    }

    return result;
}

void Shell::ensureInteractiveReady()
{
    if (_interactiveReady)
        return;
    _interactiveReady = true;

    // Load persistent history and auto-import from other shells on first run
    history.load();
    history.autoImportIfEmpty();

    // Initialize completion system
    completer = std::make_unique<Completer>(_env, history, _fsharpState, &_fs);
    prompt.setCompleter(completer.get());
    prompt.setHistory(&history);
    prompt.setEnvironmentProvider(&_env);
    prompt.setFileSystem(&_fs);
}

std::optional<std::string> Shell::invokePromptCallback(std::string const& functionName)
{
    // Only named persisted F# functions are invocable as prompt callbacks.
    if (!_fsharpState.functions.contains(functionName))
        return std::nullopt;

    // Build a one-line source that calls the user function and captures its
    // return value via the `__prompt_capture_string` builtin. Reuses the full
    // parse/sema/irgen/target pipeline via Shell::execute so that user-defined
    // functions, modules, and builtins are all visible.
    auto const source = "__prompt_capture_string (" + functionName + " ())";

    _promptCallbackResult.clear();

    // RAII scope guard: runs a callable at scope exit regardless of normal/exception path.
    class ScopeExit
    {
      public:
        explicit ScopeExit(std::function<void()> fn): _fn { std::move(fn) } {}

        ~ScopeExit()
        {
            if (_fn)
                _fn();
        }

        ScopeExit(ScopeExit const&) = delete;
        ScopeExit& operator=(ScopeExit const&) = delete;

      private:
        std::function<void()> _fn;
    };

    // Re-entrant safety: save outer VM state that execute() replaces, mirroring
    // Shell::executeConfigScript(). Without this, a prompt callback fired during
    // render would clobber _currentProgram/_runner used elsewhere in the shell.
    auto savedProgram = std::move(_currentProgram);
    auto* const savedRunner = _runner;
    auto const savedExitCode = _exitCode;
    auto const savedDuration = _lastCommandDuration;
    auto const savedUnusedDetection = _unusedValueDetection;
    // Snapshot retainedASTs size so we can drop anything this invocation pushes —
    // prompt callbacks fire on every context change, and retaining their trivial
    // invocation AST would grow memory unboundedly.
    auto const savedRetainedASTsSize = _fsharpState.retainedASTs.size();

    ++_configScriptDepth;          // Suppress auto-display and unused-value detection.
    _unusedValueDetection = false; // Avoid spurious diagnostics from unused bindings in callbacks.

    ScopeExit const restore { [this,
                               savedRunner,
                               savedExitCode,
                               savedDuration,
                               savedUnusedDetection,
                               savedRetainedASTsSize,
                               &savedProgram] {
        --_configScriptDepth;
        _unusedValueDetection = savedUnusedDetection;
        _exitCode = savedExitCode;
        _lastCommandDuration = savedDuration;
        _currentProgram = std::move(savedProgram);
        _runner = savedRunner;
        if (_fsharpState.retainedASTs.size() > savedRetainedASTsSize)
            _fsharpState.retainedASTs.resize(savedRetainedASTsSize);
    } };

    try
    {
        CoreVM::diagnostics::BufferedReport report;
        execute(source, report, "<prompt-callback>");
    }
    catch (...)
    {
        // Swallow any exception — the prompt must stay alive. Guards restore state.
        return std::nullopt;
    }

    if (_promptCallbackResult.empty())
        return std::nullopt;

    return std::exchange(_promptCallbackResult, std::string {});
}

namespace
{
    /// Resets any interrupt left pending from a prior command so the new command starts with a
    /// clean SIGINT state. Without this, a Ctrl+C delivered during the previous command (on
    /// Windows the console control handler sets the flag for the shell itself) would leak into the
    /// next in-process builtin, which checks hasPendingSigint() before its first read and would
    /// spuriously return 130 with no output.
    void clearStalePendingInterrupt() noexcept
    {
        SignalHandler::clearPendingSigint();
    }
} // namespace

void Shell::updatePromptContext()
{
    auto ctx = PromptContext {};
    // Canonicalize on-disk capitalization so the prompt shows the real path case
    // (e.g. "D:/Lastrada" after `cd d:/lastrada`) rather than the typed case.
    ctx.cwd = platform::canonicalCasePath(_fs.currentPath());
    emitWindowTitle(ctx.cwd);
    // Resolve home via the environment abstraction (HOME, then USERPROFILE on Windows)
    // and canonicalize it the same way as cwd, so the tilde-contraction prefix match in
    // PathModule compares matching separators and case.
    if (auto const home = _env.homeDirectory())
        ctx.homePath = platform::canonicalCasePath(*home);
    ctx.lastExitCode = _exitCode;
    ctx.lastDuration = _lastCommandDuration;
    ctx.terminalWidth = prompt.terminal().columns();
    ctx.isSSH = _env.get("SSH_CONNECTION").has_value();
    // Populate identity unconditionally so the prompt can show user@host in every session.
    ctx.hostname = platform::hostName();
    ctx.username = _env.userName().value_or("");
    ctx.theme = &tui::currentTheme();
    ctx.fsharpState = &_fsharpState;
    ctx.outputDefs = &_outputDefinitions;
    ctx.shellLevel = _shellLevel;
    ctx.cellPixelWidth = prompt.terminal().cellPixelWidth();
    ctx.cellPixelHeight = prompt.terminal().cellPixelHeight();
    prompt.setPromptContext(std::move(ctx));
}

int Shell::run()
{
    if (_interactive && !_tty.isTerminal())
    {
        _tty.writeToStderr("endo: interactive mode requires a terminal.\n");
        return EXIT_FAILURE;
    }

    ensureInteractiveReady();

    // Set up command palette registry for shell mode
    auto shellCommandRegistry = tui::CommandRegistry {};
#if defined(ENDO_ENABLE_AGENT) && ENDO_ENABLE_AGENT
    shellCommandRegistry.add({
        .id = "shell.enter_agent_mode",
        .label = "Enter Agent Mode",
        .description = "Switch to the AI agent chat interface",
        .category = "Mode",
        .keybinding = "#",
        .context = tui::CommandContext::Shell,
        .action = [] {}, // Handled via Action::AgentMode
    });
#endif
    shellCommandRegistry.add({
        .id = "shell.clear_screen",
        .label = "Clear Screen",
        .description = "Clear the terminal screen",
        .category = "View",
        .keybinding = "Ctrl+L",
        .context = tui::CommandContext::Both,
        .action = [] {}, // Handled via Action::ClearScreen
    });
    prompt.setCommandRegistry(&shellCommandRegistry);

    // Ensure terminal is initialized (raw mode, ECHO off) before sending
    // any terminal queries that produce response bytes.
    // This must happen before loadInitScript() so that color scheme detection
    // (which fires onColorSchemeChanged and re-applies the preset) completes
    // before user overrides from init.endo are applied on top.
    prompt.ensureInitialized();

    // Enable semantic block query extension (DEC mode 2034) for error recovery.
    // This silently fails on terminals that don't support it.
    // Must run after terminal initialization so response bytes aren't echoed.
    if (_interactive && _tty.isTerminal() && prompt.ready())
    {
        _semanticBlockClient =
            std::make_unique<tui::SemanticBlockClient>(prompt.terminal().output(), prompt.terminal().input());
        (void) _semanticBlockClient->enable(); // Silently ignore failure (unsupported terminal).
    }

    if (!_noProfile)
        loadInitScript();
    loadCompleters();
    onDirectoryChanged();

    // Drive the interactive prompt through the coroutine runtime. The event source
    // multiplexes terminal input with the POSIX signal fd (job control); Ctrl+C at
    // the prompt is a key in raw mode, so SIGINT is ignored here (a no-op interrupt
    // handler keeps it from tripping the runtime's root cancellation across reads).
#if defined(_WIN32)
    auto promptEventSource = tui::runtime::TerminalEventSource(prompt.terminal(), nullptr, &_interruptWakeup);
#else
    auto promptEventSource =
        tui::runtime::TerminalEventSource(prompt.terminal(), nullptr, &_interruptWakeup, _signalFd);
#endif
    auto promptRuntime = tui::runtime::TuiRuntime(promptEventSource);
    promptRuntime.setInterruptHandler([] {});

    // While read() is blocked waiting for input, the event source reaps job-control
    // signals (SIGCHLD) during the wait and resumes the prompt with a non-input
    // activity wake. Report finished jobs on that idle wake so a background job's
    // completion is announced promptly rather than at the next keystroke.
    prompt.setOnIdle([this] {
        SignalHandler::processPendingSignals();
        reportJobStatus();
    });

#if !defined(_WIN32)
    while (!_quit && prompt.ready())
    {
        // Check for pending signals on non-signalfd platforms
        SignalHandler::processPendingSignals();
        updateTerminalSizeEnv();

        // Report completed jobs before prompting
        reportJobStatus();

        // Shell integration: notify terminal of CWD and prompt lifecycle
        emitCurrentWorkingDirectory();
        emitPromptStart();

        // Populate prompt context for module evaluation
        updatePromptContext();

        // Display the prompt before waiting for input
        prompt.display();

        emitPromptEnd();

        // Wait for input. The runtime's event source multiplexes the signal fd,
        // so job-control signals are handled during the wait; report them after.
        auto const lineBuffer = promptRuntime.blockOn(prompt.read(&promptRuntime));

        SignalHandler::processPendingSignals();
        reportJobStatus();

        {
            debugLog()()("input buffer: {}", lineBuffer);

    #if defined(ENDO_ENABLE_AGENT) && ENDO_ENABLE_AGENT
            // Check if the user wants to enter agent mode
            if (prompt.lastAction() == PromptComponent::Action::AgentMode)
            {
                runAgentMode();
                // Only update terminal dimensions after agent mode — do NOT call prompt.resume()
                // which would query cursor position and risk emitting a partial line indicator
                // that shifts the prompt down. The screen cursor tracking was already released
                // inside Prompt::read()'s AgentMode handler.
                prompt.terminal().output().updateDimensions();
                continue;
            }
    #endif

            // Add non-empty commands to history
            if (!lineBuffer.empty())
            {
                prompt.addHistory(lineBuffer);
                auto const homeEnv = normalizedHomeDirectory(_env);
                auto const cwdAbs = _env.currentDirectory();
                history.add(
                    lineBuffer,
                    HistoryAddContext {
                        .cwd = canonicalizeForHistory(cwdAbs, homeEnv),
                        .requiredPaths = collectRequiredPathsFromCommandLine(lineBuffer, cwdAbs, homeEnv),
                    });
            }

            {
                clearStalePendingInterrupt();

                auto const _ = Prompt::ScopedSuspend(prompt);
                emitWindowTitle(lineBuffer);
                emitCommandStart();
                auto const cmdStart = std::chrono::steady_clock::now();
                _exitCode = execute(lineBuffer);
                _lastCommandDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - cmdStart);
                emitCommandFinished(_exitCode);
            }

            if (!lineBuffer.empty())
                history.markLastResult(_exitCode);

            // Update diagnostics with known F# names from persisted state
            auto names = std::set<std::string>();
            for (auto const& [name, func]: _fsharpState.functions)
                names.insert(name);
            for (auto const& binding: _fsharpState.valueBindings)
                names.insert(binding.name);
            prompt.setKnownFSharpNames(std::move(names));

    #if defined(ENDO_ENABLE_AGENT) && ENDO_ENABLE_AGENT
            // Offer error recovery if command failed.
            if (_exitCode != 0 && !lineBuffer.empty())
            {
                auto const effectiveAction =
                    _hasSessionOverride ? _sessionErrorRecoveryOverride : agentConfig.errorRecovery.action;

                if (effectiveAction != agent::ErrorRecoveryAction::Ignore)
                    offerErrorRecovery(_exitCode, lineBuffer);
            }
    #endif
        }
    }

    // Disable semantic block extension on exit.
    if (_semanticBlockClient)
        _semanticBlockClient->disable();

#else
    // Windows: simple loop without poll/signalfd
    while (!_quit && prompt.ready())
    {
        updateTerminalSizeEnv();

        // Report completed jobs before prompting
        reportJobStatus();

        // Shell integration: notify terminal of CWD and prompt lifecycle
        emitCurrentWorkingDirectory();
        emitPromptStart();

        // Populate prompt context for module evaluation
        updatePromptContext();

        // Display the prompt before waiting for input
        prompt.display();

        emitPromptEnd();

        auto const lineBuffer = promptRuntime.blockOn(prompt.read(&promptRuntime));
        debugLog()()("input buffer: {}", lineBuffer);

    #if defined(ENDO_ENABLE_AGENT) && ENDO_ENABLE_AGENT
        // Check if the user wants to enter agent mode
        if (prompt.lastAction() == PromptComponent::Action::AgentMode)
        {
            runAgentMode();
            // Only update terminal dimensions after agent mode — do NOT call prompt.resume()
            // which would query cursor position and risk emitting a partial line indicator
            // that shifts the prompt down. The screen cursor tracking was already released
            // inside Prompt::read()'s AgentMode handler.
            prompt.terminal().output().updateDimensions();
            continue;
        }
    #endif

        // Add non-empty commands to history
        if (!lineBuffer.empty())
        {
            prompt.addHistory(lineBuffer);
            auto const homeEnv = normalizedHomeDirectory(_env);
            auto const cwdAbs = _env.currentDirectory();
            history.add(lineBuffer,
                        HistoryAddContext {
                            .cwd = canonicalizeForHistory(cwdAbs, homeEnv),
                            .requiredPaths = collectRequiredPathsFromCommandLine(lineBuffer, cwdAbs, homeEnv),
                        });
        }

        {
            clearStalePendingInterrupt();

            auto const _ = Prompt::ScopedSuspend(prompt);
            emitWindowTitle(lineBuffer);
            emitCommandStart();
            auto const cmdStart = std::chrono::steady_clock::now();
            _exitCode = execute(lineBuffer);
            _lastCommandDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - cmdStart);
            emitCommandFinished(_exitCode);
        }

        if (!lineBuffer.empty())
            history.markLastResult(_exitCode);

        // Update diagnostics with known F# names from persisted state
        auto names = std::set<std::string>();
        for (auto const& [name, func]: _fsharpState.functions)
            names.insert(name);
        for (auto const& binding: _fsharpState.valueBindings)
            names.insert(binding.name);
        prompt.setKnownFSharpNames(std::move(names));

    #if defined(ENDO_ENABLE_AGENT) && ENDO_ENABLE_AGENT
        // Offer error recovery if command failed.
        if (_exitCode != 0 && !lineBuffer.empty())
        {
            auto const effectiveAction =
                _hasSessionOverride ? _sessionErrorRecoveryOverride : agentConfig.errorRecovery.action;

            if (effectiveAction != agent::ErrorRecoveryAction::Ignore)
                offerErrorRecovery(_exitCode, lineBuffer);
        }
    #endif
    }
#endif

    // Disable semantic block extension on exit.
    if (_semanticBlockClient)
        _semanticBlockClient->disable();

    return _quit ? _exitCode : EXIT_SUCCESS;
}

int Shell::execute(std::string const& lineBuffer, std::string_view sourceName)
{
    RichConsoleReport report(_tty);
    report.setSourceText(lineBuffer);
    return execute(lineBuffer, report, sourceName);
}

int Shell::execute(std::string const& lineBuffer,
                   CoreVM::diagnostics::Report& report,
                   std::string_view sourceName)
{
    // Clear any leftover redirect state from previous commands
    _redirectState.clear();

    try
    {
        // Use positional parameters for script mode, otherwise use the provided source name
        auto const effectiveSourceName = !_interactive && !_positionalParameters.empty()
                                             ? std::string_view(_positionalParameters[0])
                                             : sourceName;
        auto parser = endo::Parser(
            _runtime, report, std::make_unique<endo::StringSource>(lineBuffer, effectiveSourceName));
        parser.setSourceText(lineBuffer);
        {
            auto names = std::unordered_set<std::string> {};
            auto variadicNames = std::unordered_set<std::string> {};
            for (auto const& [name, func]: _fsharpState.functions)
            {
                if (func.hasVariadicParam)
                    variadicNames.insert(name);
                else
                    names.insert(name);
            }
            for (auto const& binding: _fsharpState.valueBindings)
                names.insert(binding.name);
            // Include runtime builtin functions whose names contain underscores,
            // so the parser routes them as F# calls (e.g., set_prompt_preset).
            // Names without underscores (exit, cd, fg, export, ...) are shell commands.
            for (auto const* builtin: _runtime.builtins())
                if (builtin->name().find('_') != std::string::npos)
                    names.insert(builtin->name());
            parser.setKnownFSharpFunctions(std::move(names));
            parser.setKnownVariadicFunctions(std::move(variadicNames));
            parser.setKnownUnitFunctions(_fsharpState.unitFunctions);
        }
        parser.setAutoDisplay(_interactive && _configScriptDepth == 0);
        auto rootNode = parser.parse();

        // Check for parser errors
        if (report.containsFailures())
            return EXIT_FAILURE;

        if (!rootNode)
            return EXIT_FAILURE;

        debugLog()()("Parsed & printed: {}", endo::ast::ASTPrinter::print(*rootNode));

        auto irProgram =
            IRGenerator::generate(*rootNode, report, _runtime, &_fsharpState, _unusedValueDetection);

        // Forward any module-load diagnostics into the execution report
        for (auto const& msg: _moduleReport)
            report.push_back(msg);
        _moduleReport.clear();

        // Check for IR generation errors
        if (report.containsFailures())
            return EXIT_FAILURE;

        if (!irProgram)
            return EXIT_FAILURE;

        // Retain the AST so that persisted F# function body pointers remain valid
        _fsharpState.retainedASTs.push_back(std::move(rootNode));

        if (_optimize)
        {
            CoreVM::PassManager pm;

            // clang-format off
            pm.registerPass("eliminate-empty-blocks", &CoreVM::transform::emptyBlockElimination);
            pm.registerPass("eliminate-linear-br", &CoreVM::transform::eliminateLinearBr);
            pm.registerPass("eliminate-unused-blocks", &CoreVM::transform::eliminateUnusedBlocks);
            pm.registerPass("eliminate-unused-instr", &CoreVM::transform::eliminateUnusedInstr);
            pm.registerPass("fold-constant-condbr", &CoreVM::transform::foldConstantCondBr);
            pm.registerPass("rewrite-br-to-exit", &CoreVM::transform::rewriteBrToExit);
            pm.registerPass("rewrite-cond-br-to-same-branches", &CoreVM::transform::rewriteCondBrToSameBranches);
            // clang-format on

            pm.run(irProgram.get());
        }

        if (irLog().is_enabled())
        {
            irLog()()("================================================\n");
            irLog()()("{} IR program (SSA form):\n", _optimize ? "Optimized" : "Unoptimized");
            irLog()()("{}", irProgram->dumpToString());
        }

        _currentProgram = CoreVM::TargetCodeGenerator {}.generate(irProgram.get());
        if (!_currentProgram)
        {
            error("Failed to generate target code");
            return EXIT_FAILURE;
        }
        builtins::registerBuiltinFormatters(_currentProgram->constants().typeRegistry());
        // Set generic product formatter for output definition types
        for (auto const& [name, defType]: _fsharpState.outputDefinitionTypes)
        {
            if (auto* td = _currentProgram->constants().typeRegistry().getMutable(defType.typeId))
                td->formatFn = builtins::formatProduct;
        }
        _currentProgram->link(&_runtime, &report);
        if (report.containsFailures())
            return EXIT_FAILURE;

        if (_checkOnly)
            return EXIT_SUCCESS;

        if (irLog().is_enabled())
        {
            irLog()()("================================================\n");
            irLog()()("Linked target code (bytecode):\n");
            irLog()()("{}", _currentProgram->dumpToString());
        }

        CoreVM::Function* main = _currentProgram->findFunction("@main");
        assert(main != nullptr);
        auto runner = CoreVM::Runner(main,
                                     nullptr,
                                     &_globals,
                                     CoreVM::RuntimeConfig::defaultConfig(),
                                     std::bind(&Shell::trace, this, _1, _2, _3));
        _runner = &runner;

        // Save current exit code before running - $? expansion will see this value
        int const savedExitCode = _exitCode;

        // Run the handler - run() returns true if exit code was non-zero
        bool const runnerExitNonZero = runner.run();

        // If _exitCode wasn't changed during execution (no command ran),
        // set it based on the runner's result
        if (_exitCode == savedExitCode)
        {
            _exitCode = runnerExitNonZero ? 1 : 0;
        }

        // Save runtime values of mutable bindings for cross-prompt persistence.
        // Allocas are at the bottom of the stack (positions 0, 1, 2, ...),
        // matching the order of persisted value bindings.
        auto const& stack = runner.stack();
        for (size_t i = 0; i < _fsharpState.valueBindings.size() && i < stack.size(); ++i)
            if (_fsharpState.valueBindings[i].isMutable)
                _fsharpState.mutableSnapshots[_fsharpState.valueBindings[i].name] = stack[i];

        return _exitCode;
    }
    catch (std::exception const& e)
    {
        error("Exception caught: {}", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

// ========================================================================
// Signal handling
// ========================================================================

void Shell::onSigchld()
{
#if !defined(_WIN32)
    // Reap all terminated/stopped children
    while (true)
    {
        int status = 0;
        pid_t const pid = waitpid(-1, &status, WNOHANG | WUNTRACED);
        if (pid <= 0)
            break;

        WaitResult result;
        if (WIFEXITED(status))
        {
            result.exitCode = WEXITSTATUS(status);
        }
        else if (WIFSIGNALED(status))
        {
            result.signaled = true;
            result.signal = WTERMSIG(status);
            result.exitCode = 128 + result.signal;
        }
        else if (WIFSTOPPED(status))
        {
            result.stopped = true;
            result.signal = WSTOPSIG(status);
        }

        // Update job table with this result
        jobTable.updateJobState(static_cast<ProcessId>(pid), result);
    }
#else
    // Windows: non-blocking check for terminated background processes
    auto jobs = jobTable.listJobs();
    for (auto const* constJob: jobs)
    {
        if (constJob->state != JobState::Running)
            continue;

        for (ProcessId const pid: constJob->pids)
        {
            auto const waitResult = _processManager.wait(pid, WaitFlag::NoHang);
            if (waitResult.has_value() && (waitResult->exitCode != 0 || !waitResult->stopped))
                jobTable.updateJobState(pid, *waitResult);
        }
    }
#endif
}

void Shell::onSigtstp()
{
#if !defined(_WIN32)
    // Step 1: Restore terminal to cooked mode so the parent shell can use it
    prompt.suspend();

    // Step 2: Actually stop the shell process by re-raising SIGTSTP with default handling
    SignalHandler::suspendSelf();

    // Step 3: When we reach here, we've been resumed (SIGCONT was received)
    // Restore terminal to raw mode and redraw
    prompt.resume();
    prompt.display();
#else
    // Windows: no SIGTSTP — shell suspension is not supported.
#endif
}

void Shell::onSigcont()
{
#if !defined(_WIN32)
    // This is called when SIGCONT is received after being stopped.
    // The main work is done in onSigtstp() after suspendSelf() returns,
    // but this handler is useful for cases where SIGCONT arrives without
    // a preceding SIGTSTP (e.g., if we were stopped by SIGSTOP instead).
    //
    // Note: On Linux with signalfd, we may receive SIGCONT here even after
    // the onSigtstp() handling has already resumed the terminal. In that case,
    // calling resume() again is harmless (it checks for suspended state).
    prompt.resume();
#else
    // Windows: no SIGCONT — shell continuation is not supported.
#endif
}

void Shell::reportJobStatus()
{
#if defined(_WIN32)
    // Windows: no SIGCHLD, so poll for completed background processes
    onSigchld();
#endif
    auto unnotified = jobTable.getUnnotifiedJobs();
    for (Job* job: unnotified)
    {
        char const marker = (job->id == jobTable.getCurrentJob()->id) ? '+' : '-';
        std::string stateStr;

        switch (job->state)
        {
            case JobState::Done: stateStr = "Done"; break;
            case JobState::Terminated: stateStr = std::format("Terminated (signal {})", job->signal); break;
            default: continue; // Only report completed jobs
        }

        _tty.writeToStdout(std::format("[{}]{} {}\t{}\n", job->id, marker, stateStr, job->command));
        job->notified = true;
    }

    // Clean up notified completed jobs
    jobTable.cleanupCompletedJobs();
}

// ========================================================================
// VM trace
// ========================================================================

void Shell::trace(CoreVM::Instruction instr, size_t ip, size_t sp)
{
    traceLog()()("{}\n", CoreVM::disassemble(instr, ip, sp, &_currentProgram->constants()));
}

#if defined(ENDO_ENABLE_AGENT) && ENDO_ENABLE_AGENT
// ========================================================================
// Agent mode
// ========================================================================

namespace
{

    /// @brief RAII guard that sets a pointer for the duration of a scope and clears it on destruction.
    template <typename T>
    class ScopedAssign
    {
      public:
        ScopedAssign(T*& target, T& value): _target(target) { _target = &value; }

        ~ScopedAssign() { _target = nullptr; }

        ScopedAssign(ScopedAssign const&) = delete;
        ScopedAssign& operator=(ScopedAssign const&) = delete;

      private:
        T*& _target;
    };

    /// @brief RAII guard that resets the terminal scroll region on destruction.
    class ScrollRegionGuard
    {
      public:
        tui::TerminalOutput& output;
        bool active = false;

        explicit ScrollRegionGuard(tui::TerminalOutput& out): output(out) {}

        ~ScrollRegionGuard()
        {
            if (active)
                output.resetScrollRegion();
        }

        ScrollRegionGuard(ScrollRegionGuard const&) = delete;
        ScrollRegionGuard& operator=(ScrollRegionGuard const&) = delete;
        ScrollRegionGuard(ScrollRegionGuard&&) = delete;
        ScrollRegionGuard& operator=(ScrollRegionGuard&&) = delete;
    };

    /// @brief Manages the agent prompt display during LLM streaming.
    ///
    /// Keeps the prompt visible while the response streams above it.
    /// Two phases:
    /// - **Drifting**: Response + prompt fit on screen; prompt is re-rendered below the response.
    /// - **Stuck**: Response exceeds available space; a scroll region is set up and the prompt
    ///   is rendered in a fixed area below the scroll region.
    class StreamingPromptManager
    {
      public:
        tui::Terminal& terminal;
        agent::AgentInputComponent& inputComponent;
        tui::TerminalOutput& output;

        int responseLineCount = 0;
        bool scrollRegionActive = false;
        bool cancelled = false;
        int promptHeight = 0;

        StreamingPromptManager(tui::Terminal& term,
                               agent::AgentInputComponent& input,
                               tui::TerminalOutput& out):
            terminal(term), inputComponent(input), output(out), _scrollGuard(out)
        {
        }

        /// @brief Called when the response emits new lines.
        /// @param lineCount Total lines emitted so far.
        void onNewResponseLine(int lineCount)
        {
            responseLineCount = lineCount;
            promptHeight = inputComponent.preferredSize().height;

            if (promptHeight >= terminal.rows())
                return; // Degenerate case: prompt taller than terminal.

            auto const totalNeeded = responseLineCount + promptHeight;
            if (!scrollRegionActive && totalNeeded >= terminal.rows())
                enterScrollRegion();
            else if (!scrollRegionActive)
                renderPromptBelow();
            // In scroll region mode, the prompt stays fixed — no re-render needed per line.
        }

        /// @brief Non-blocking poll for user input during streaming.
        void pollInput()
        {
            auto events = terminal.poll(0);
            for (auto const& event: events)
            {
                if (std::holds_alternative<tui::ResizeEvent>(event))
                {
                    handleResize();
                    continue;
                }

                auto const action = inputComponent.processInput(event);
                switch (action)
                {
                    case agent::AgentInputComponent::Action::Abort: cancelled = true; return;
                    case agent::AgentInputComponent::Action::ClearScreen: handleClearScreen(); break;
                    case agent::AgentInputComponent::Action::Changed: renderPromptAtCurrentPosition(); break;
                    default: break;
                }
            }
        }

        /// @brief Cleans up scroll region state. Must be called when streaming ends.
        void teardown()
        {
            if (scrollRegionActive)
            {
                output.resetScrollRegion();
                _scrollGuard.active = false;
                scrollRegionActive = false;

                // Move cursor past the prompt area so subsequent output starts clean.
                output.moveTo(terminal.rows(), 1);
                output.linefeed();
            }
            // Clear the prompt we rendered during streaming (the Screen system will re-render).
            if (promptHeight > 0 && !scrollRegionActive)
            {
                // The prompt was rendered directly; clear it so the Screen can take over.
                output.saveCursor();
                for (auto i = 0; i < promptHeight; ++i)
                {
                    output.moveDown(1);
                    output.carriageReturn();
                    output.clearToEndOfLine();
                }
                output.restoreCursor();
            }
            output.flush();
        }

      private:
        void enterScrollRegion()
        {
            promptHeight = inputComponent.preferredSize().height;
            auto const scrollBottom = terminal.rows() - promptHeight;
            if (scrollBottom < 1)
                return;

            output.setScrollRegion(1, scrollBottom);
            _scrollGuard.active = true;
            scrollRegionActive = true;

            renderPromptFixed();
        }

        /// @brief Renders the prompt below the response (drifting phase).
        void renderPromptBelow()
        {
            // In drifting mode we don't re-render the prompt on every line —
            // only when the user types (Changed action). This avoids flicker.
        }

        /// @brief Renders the prompt in the fixed area below the scroll region.
        void renderPromptFixed()
        {
            auto const scrollBottom = terminal.rows() - promptHeight;
            auto guard = output.syncGuard();
            output.saveCursor();

            // Clear the fixed prompt area and render the prompt header + input.
            for (auto row = scrollBottom + 1; row <= terminal.rows(); ++row)
            {
                output.moveTo(row, 1);
                output.clearToEndOfLine();
            }

            renderPromptDirect(scrollBottom + 1);
            output.restoreCursor();
            output.flush();
        }

        /// @brief Renders the prompt at the current position (for Changed events).
        void renderPromptAtCurrentPosition()
        {
            if (scrollRegionActive)
                renderPromptFixed();
        }

        /// @brief Renders a minimal version of the prompt directly via TerminalOutput.
        /// @param startRow The 1-based row to start rendering at.
        void renderPromptDirect(int startRow)
        {
            auto const& theme = tui::currentTheme();
            auto const barStyle = tui::Style { .fg = theme.agentColors.leftBar };
            auto const labelStyle = tui::Style { .fg = theme.agentColors.leftBar };
            auto const statusStyle = tui::Style { .fg = theme.agentColors.statusText };

            // Header line: ╭─ agent
            output.moveTo(startRow, 1);
            output.writeText("\xe2\x95\xad\xe2\x94\x80 ", barStyle);
            output.writeText("agent", labelStyle);

            // Input line: ╰─ ❯ <input text>
            output.moveTo(startRow + 1, 1);
            output.writeText("\xe2\x95\xb0\xe2\x94\x80 ", barStyle);
            output.writeText("\xe2\x9d\xaf ", statusStyle);

            auto const text = inputComponent.text();
            if (!text.empty())
                output.writeText(text);
        }

        void handleClearScreen()
        {
            if (scrollRegionActive)
            {
                output.resetScrollRegion();
                _scrollGuard.active = false;
                scrollRegionActive = false;
            }
            output.clearScreen();
            output.flush();
            responseLineCount = 0;
        }

        void handleResize()
        {
            terminal.output().updateDimensions();
            if (scrollRegionActive)
            {
                output.resetScrollRegion();
                _scrollGuard.active = false;
                scrollRegionActive = false;

                // Re-evaluate whether we need a scroll region with new dimensions.
                promptHeight = inputComponent.preferredSize().height;
                auto const totalNeeded = responseLineCount + promptHeight;
                if (totalNeeded >= terminal.rows())
                    enterScrollRegion();
            }
        }

        ScrollRegionGuard _scrollGuard;
    };

    /// @brief Formats tool call arguments as a compact string for display.
    /// @param arguments The JSON arguments from a tool call.
    /// @return A compact string representation with large content fields replaced by size placeholders.
    [[nodiscard]] auto formatToolCallArgs(nlohmann::json const& arguments) -> std::string
    {
        if (arguments.is_null() || (arguments.is_object() && arguments.empty()))
            return {};

        auto truncated = arguments;
        for (const auto& [key, value]: truncated.items())
        {
            if (value.is_string())
            {
                // Replace large content fields (write_file/edit_file payloads) with size placeholder
                if (key == "content" || key == "new_string" || key == "old_string")
                {
                    auto const len = value.get<std::string>().size();
                    value = std::format("<{} chars>", len);
                }
            }
        }

        return truncated.dump(-1);
    }

    /// @brief Formats a tool status line for terminal display.
    ///
    /// For shell_execute / endo_execute, returns a shell-prompt style line with the full command.
    /// For all other tools, returns the gear icon with truncated JSON arguments.
    ///
    /// @param call The tool call to format.
    /// @return A pair of (prefix, text) strings for styled rendering. The prefix is "$ " for shell
    ///         commands or "\xe2\x9a\x99 tool_name" for other tools.
    [[nodiscard]] auto formatToolStatusLine(agent::ToolCall const& call)
        -> std::pair<std::string, std::string>
    {
        if (call.name == "shell_execute" || call.name == "endo_execute")
        {
            auto command = std::string {};
            if (call.arguments.contains("command") && call.arguments["command"].is_string())
                command = call.arguments["command"].get<std::string>();
            else if (call.arguments.contains("source") && call.arguments["source"].is_string())
                command = call.arguments["source"].get<std::string>();

            if (call.arguments.contains("timeout_ms") && call.arguments["timeout_ms"].is_number())
            {
                auto const timeoutSeconds = call.arguments["timeout_ms"].get<int64_t>() / 1000;
                return { "$ ", std::format("timeout {} {}", timeoutSeconds, command) };
            }

            return { "$ ", command };
        }

        auto args = formatToolCallArgs(call.arguments);
        auto prefix = "\xe2\x9a\x99 " + std::string(call.name);
        return { std::move(prefix), args.empty() ? std::string {} : " " + args };
    }

    /// @brief Runs a command and captures stdout (for git info queries).
    [[nodiscard]] auto runCommandCapture(std::string const& cmd) -> std::string
    {
        auto result = std::string {};
    #if defined(_WIN32)
        auto* fp = _popen(cmd.c_str(), "r"); // NOLINT(cert-env33-c)
    #else
        auto* fp = popen(cmd.c_str(), "r"); // NOLINT(cert-env33-c)
    #endif
        if (!fp)
            return result;

        auto buf = std::array<char, 256> {};
        while (fgets(buf.data(), static_cast<int>(buf.size()), fp) != nullptr)
            result += buf.data();
    #if defined(_WIN32)
        _pclose(fp); // NOLINT(cert-env33-c)
    #else
        pclose(fp); // NOLINT(cert-env33-c)
    #endif

        while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
            result.pop_back();
        return result;
    }

    /// @brief Builds agent context (project files, git info, system prompt) — runs in background thread.
    /// @param agentConfig The agent configuration.
    /// @param cwd The current working directory.
    /// @param cachedContext Optional cached project context to reuse (skips file scanning).
    /// @param cachedGitInfo Optional cached git info from the prompt's GitModule.
    /// @return The assembled context result.
    [[nodiscard]] auto buildAgentContext(agent::AgentConfig const& agentConfig,
                                         std::filesystem::path const& cwd,
                                         std::optional<agent::ProjectContext> cachedContext,
                                         std::optional<GitInfo> cachedGitInfo) -> AgentContextResult
    {
        auto projectContext =
            cachedContext ? std::move(*cachedContext) : agent::ProjectContextLoader::load(cwd);

        auto promptBuilder = agent::SystemPromptBuilder {};
        promptBuilder.setWorkingDirectory(platform::normalizePath(cwd));
        promptBuilder.setShellInfo("endo");
        promptBuilder.setProjectRules(projectContext.rulesFiles);
        promptBuilder.setGlobalRules(projectContext.globalRules);
        promptBuilder.setMemoryFiles(projectContext.memoryFiles);
        promptBuilder.setFileTree(projectContext.fileTree);

        // Use cached git info from the prompt module when available to avoid
        // spawning additional subprocess calls.
        auto gitBranch = std::string {};
        if (cachedGitInfo && cachedGitInfo->valid)
        {
            gitBranch = cachedGitInfo->branch;
            promptBuilder.setGitBranch(gitBranch);
            promptBuilder.setGitStatus((cachedGitInfo->dirty > 0 || cachedGitInfo->staged > 0) ? "has changes"
                                                                                               : "clean");
        }
        else
        {
            // Fallback: query git directly (e.g., first agent entry before prompt displayed)
            auto const cwdStr = platform::normalizePath(cwd);
    #if defined(_WIN32)
            gitBranch = runCommandCapture("git -C " + cwdStr + " rev-parse --abbrev-ref HEAD 2>NUL");
    #else
            gitBranch = runCommandCapture("git -C " + cwdStr + " rev-parse --abbrev-ref HEAD 2>/dev/null");
    #endif
            if (!gitBranch.empty())
            {
                promptBuilder.setGitBranch(gitBranch);
    #if defined(_WIN32)
                auto const gitStatus =
                    runCommandCapture("git -C " + cwdStr + " status --porcelain=v2 --branch 2>NUL");
    #else
                auto const gitStatus =
                    runCommandCapture("git -C " + cwdStr + " status --porcelain=v2 --branch 2>/dev/null");
    #endif
                promptBuilder.setGitStatus(gitStatus.empty() ? "clean" : "has changes");
            }
        }

        // Tilde-contract the project path for display
        auto projectPath = platform::normalizePath(cwd);
        if (auto const home = platform::homeDirectory())
        {
            auto const homeStr = platform::normalizePath(*home);
            if (projectPath.starts_with(homeStr))
            {
                auto contracted = "~" + projectPath.substr(homeStr.size());
                if (contracted.size() == 1 || contracted[1] == '/')
                    projectPath = std::move(contracted);
            }
        }

        // Build the explore sub-agent system prompt (shares project context, uses exploration-focused
        // instructions)
        auto explorePromptBuilder = agent::SystemPromptBuilder {};
        explorePromptBuilder.setBaseInstructions(
            "You are a codebase exploration agent. Your task is to answer questions about the codebase "
            "by reading files, searching with grep, and listing files with glob. "
            "Be thorough but concise. Reference file paths with line numbers. "
            "Do not ask follow-up questions — produce a complete answer. "
            "Do not suggest code changes — only report findings.");
        explorePromptBuilder.setWorkingDirectory(platform::normalizePath(cwd));
        explorePromptBuilder.setShellInfo("endo");
        explorePromptBuilder.setProjectRules(projectContext.rulesFiles);
        explorePromptBuilder.setGlobalRules(projectContext.globalRules);
        explorePromptBuilder.setMemoryFiles(projectContext.memoryFiles);
        explorePromptBuilder.setFileTree(projectContext.fileTree);
        if (!gitBranch.empty())
            explorePromptBuilder.setGitBranch(gitBranch);

        return AgentContextResult {
            .systemPrompt = promptBuilder.build(),
            .exploreSystemPrompt = explorePromptBuilder.build(),
            .gitBranch = gitBranch,
            .projectPath = std::move(projectPath),
            .projectContext = std::move(projectContext),
        };
    }

} // namespace

int Shell::runAgentHeadless(agent::AgentRunOptions const& options)
{
    // --- Provider setup (same lazy init as runAgentMode) ---
    if (!_agentProviderFactory)
    {
        _agentHttpClient = std::make_unique<http::HttpClient>();
        _agentProviderFactory = std::make_unique<agent::ProviderFactory>(*_agentHttpClient, agentConfig);
    }

    // Apply provider override if requested.
    if (options.provider.has_value())
    {
        if (!_agentProviderFactory->switchProvider(*options.provider))
        {
            std::print(
                stderr, "endo agent run: unknown or unauthenticated provider '{}'\n", *options.provider);
            return EXIT_FAILURE;
        }
    }

    auto* provider = _agentProviderFactory->activeProvider();
    if (!provider)
    {
        if (!agentConfig.activeProvider.empty())
            std::print(stderr,
                       "endo agent run: provider '{}' is not available.\n"
                       "Check your configuration or run `endo agent status` for details.\n",
                       agentConfig.activeProvider);
        else
            std::print(stderr,
                       "endo agent run: no AI provider configured or authenticated.\n"
                       "Run `endo agent login` or configure a provider in ~/.config/endo/init.endo.\n");
        return EXIT_FAILURE;
    }

    // Apply model override if requested.
    // NOTE: Model override is provider-specific; currently not directly supported
    // via the LlmProvider interface. The model is configured via AgentConfig at
    // provider construction time. For now, we log a warning if the user tries
    // to override.
    if (options.model.has_value())
    {
        // Model override would require re-creating the provider with a different model.
        // For now, this is a best-effort: the user can set the model in init.endo.
        std::print(stderr,
                   "endo agent run: --model override is not yet implemented; using configured model.\n");
    }

    // --- Session creation ---
    _agentSession = std::make_unique<agent::AgentSession>(*provider);

    // --- Tool registration (same tools as interactive mode) ---
    auto toolRegistry = agent::ToolRegistry {};

    #if !defined(_WIN32)
    auto const shellPath = [&]() -> std::string {
        if (access("/bin/bash", X_OK) == 0)
            return "/bin/bash";
        if (access("/usr/bin/bash", X_OK) == 0)
            return "/usr/bin/bash";
        return "/bin/sh";
    }();

    auto shellExecCb = [shellPath](std::string const& command,
                                   std::chrono::milliseconds timeout) -> agent::ShellExecResult {
        return shellExecImpl(shellPath, command, timeout);
    };

    auto endoExecCb = [this](std::string const& source,
                             std::chrono::milliseconds /*timeout*/) -> agent::EndoExecResult {
        auto* tmpFile = tmpfile();
        if (!tmpFile)
            return agent::EndoExecResult { .output = "Failed to create temporary file", .exitCode = -1 };

        auto const tmpFd = fileno(tmpFile);
        auto const savedStdout = dup(STDOUT_FILENO);
        auto const savedStderr = dup(STDERR_FILENO);

        fflush(stdout);
        fflush(stderr);
        dup2(tmpFd, STDOUT_FILENO);
        dup2(tmpFd, STDERR_FILENO);

        auto const exitCode = this->execute(source);

        fflush(stdout);
        fflush(stderr);
        if (savedStdout >= 0)
        {
            dup2(savedStdout, STDOUT_FILENO);
            close(savedStdout);
        }
        if (savedStderr >= 0)
        {
            dup2(savedStderr, STDERR_FILENO);
            close(savedStderr);
        }

        fflush(tmpFile);
        auto const outputSize = lseek(tmpFd, 0, SEEK_END);
        lseek(tmpFd, 0, SEEK_SET);

        auto output = std::string {};
        if (outputSize > 0)
        {
            output.resize(static_cast<size_t>(outputSize));
            auto const bytesRead = ::read(tmpFd, output.data(), output.size());
            if (bytesRead >= 0)
                output.resize(static_cast<size_t>(bytesRead));
            else
                output.clear();
        }
        fclose(tmpFile);

        return agent::EndoExecResult { .output = std::move(output), .exitCode = exitCode };
    };
    #else
    auto shellExecCb = [](std::string const& command,
                          std::chrono::milliseconds timeout) -> agent::ShellExecResult {
        return shellExecImpl(command, timeout);
    };

    auto endoExecCb = [this](std::string const& source,
                             std::chrono::milliseconds /*timeout*/) -> agent::EndoExecResult {
        auto const exitCode = this->execute(source);
        return agent::EndoExecResult { .output = {}, .exitCode = exitCode };
    };
    #endif

    toolRegistry.registerTool(std::make_unique<agent::ReadFileTool>());
    toolRegistry.registerTool(std::make_unique<agent::WriteFileTool>());
    toolRegistry.registerTool(std::make_unique<agent::EditFileTool>());
    toolRegistry.registerTool(std::make_unique<agent::GlobTool>());
    toolRegistry.registerTool(std::make_unique<agent::GrepTool>());
    toolRegistry.registerTool(std::make_unique<agent::SearchTool>());
    toolRegistry.registerTool(std::make_unique<agent::ListDirectoryTool>());
    toolRegistry.registerTool(std::make_unique<agent::ShellExecuteTool>(shellExecCb));
    toolRegistry.registerTool(std::make_unique<agent::EndoExecuteTool>(endoExecCb));
    toolRegistry.registerTool(std::make_unique<agent::GitTool>(shellExecCb));
    toolRegistry.registerTool(
        std::make_unique<agent::SaveMemoryTool>([this]() { _cachedProjectContext.reset(); }));
    toolRegistry.registerTool(std::make_unique<agent::SubmitPlanTool>());
    toolRegistry.registerTool(
        std::make_unique<agent::ExploreTool>(*provider, shellExecCb, agentConfig.explore));
    toolRegistry.registerTool(std::make_unique<agent::WebSearchTool>(*_agentHttpClient, webSearchConfig));

    auto const webFetchConfig = agent::WebFetchConfig {};
    toolRegistry.registerTool(std::make_unique<agent::WebFetchTool>(*_agentHttpClient, webFetchConfig));

    // AskUserTool returns cancelled in headless mode (no user to interact with).
    toolRegistry.registerTool(
        std::make_unique<agent::AskUserTool>([](agent::UserQuestion const&) -> agent::UserAnswer {
            return { .answer = "User unavailable in headless mode.", .cancelled = true };
        }));

    // Start MCP servers and register their tools.
    auto mcpServerManager = agent::mcp::ServerManager {};
    for (auto const& config: mcpServerConfigs)
    {
        auto result = mcpServerManager.addServer(config);
        if (!result)
            std::println(stderr, "MCP: Failed to connect server '{}': {}", config.name, result.error());
    }
    for (auto const& toolDef: mcpServerManager.allTools())
        toolRegistry.registerTool(std::make_unique<agent::mcp::McpToolAdapter>(mcpServerManager, toolDef));

    mcpServerManager.setToolsChangedCallback([&toolRegistry,
                                              &mcpServerManager](std::string_view /*serverName*/,
                                                                 std::span<agent::ToolDefinition const> added,
                                                                 std::vector<std::string> const& removed) {
        for (auto const& name: removed)
            toolRegistry.unregisterTool(name);
        for (auto const& def: added)
            toolRegistry.registerTool(std::make_unique<agent::mcp::McpToolAdapter>(mcpServerManager, def));
    });

    _agentSession->setToolRegistry(&toolRegistry);
    _agentSession->setMaxToolResultSize(agentConfig.maxToolResultSize);
    _agentSession->setMaxToolIterations(options.maxTurns);
    _agentSession->setTracer(nullptr);
    _agentSession->setToolStatusCallback(nullptr);

    // Set up terminal trace callback for headless mode (writes to stderr).
    if (agentConfig.trace.terminal)
        _agentSession->setTraceEventCallback(
            [](agent::TraceEvent const& e) { agent::renderTraceEventToStderr(e); });
    else
        _agentSession->setTraceEventCallback(nullptr);

    // --- Permission manager ---
    auto permConfig = agentConfig.permissions;
    if (options.autoApprove)
        permConfig.policy = agent::PermissionPolicy::TrustAll;
    auto permissionManager = agent::PermissionManager(permConfig);
    // In non-auto-approve mode, deny by default (no TTY for prompting).
    if (!options.autoApprove)
        permissionManager.setPromptCallback(
            [](agent::PermissionPrompt const&) { return agent::PermissionDecision::Denied; });
    _agentSession->setPermissionManager(&permissionManager);

    // --- System prompt ---
    auto const cwd = std::filesystem::current_path();
    auto agentContext = buildAgentContext(agentConfig, cwd, std::nullopt, std::nullopt);
    _agentSession->setSystemPrompt(std::move(agentContext.systemPrompt));
    if (auto* explore = dynamic_cast<agent::ExploreTool*>(toolRegistry.findTool("explore")))
        explore->setSystemPrompt(std::move(agentContext.exploreSystemPrompt));

    // --- Collect tool call records ---
    auto headlessResult = agent::HeadlessRunResult {};
    auto const modelInfo = provider->modelInfo();
    headlessResult.providerName = modelInfo.providerName;
    headlessResult.modelName = modelInfo.modelName;

    _agentSession->setToolStatusCallback(nullptr);
    _agentSession->setToolResultCallback([&headlessResult](std::string const& name,
                                                           std::string const& content,
                                                           bool isError,
                                                           std::chrono::milliseconds duration) {
        headlessResult.toolCalls.push_back(agent::ToolCallRecord {
            .name = name,
            .result = content,
            .isError = isError,
            .duration = duration,
        });
    });

    // --- Execute synchronously ---
    auto streamCb = agent::StreamCallback {};
    if (!options.jsonOutput)
    {
        // In text mode, stream tokens directly to stdout.
        streamCb = [](std::string_view token) -> bool {
            std::print("{}", token);
            return true;
        };
    }

    auto result = _agentSession->processMessage(options.prompt, streamCb);

    if (result.has_value())
    {
        headlessResult.success = true;
        headlessResult.response = std::move(*result);
    }
    else
    {
        headlessResult.success = false;
        headlessResult.errorMessage = result.error().message;
    }

    headlessResult.tokenUsage = _agentSession->sessionUsage();
    headlessResult.turnCount = _agentSession->turnCount();

    // --- Output ---
    if (options.jsonOutput)
    {
        std::println("{}", agent::toJson(headlessResult).dump(2));
    }
    else
    {
        // Ensure final newline after streamed text.
        if (headlessResult.success && !headlessResult.response.empty()
            && headlessResult.response.back() != '\n')
            std::println("");

        if (!headlessResult.success)
            std::print(stderr, "Error: {}\n", headlessResult.errorMessage);

        // Print token usage summary to stderr.
        std::print(stderr,
                   "\n[{}/{} | {} turns | in:{} out:{} cache_r:{} cache_w:{}]\n",
                   headlessResult.providerName,
                   headlessResult.modelName,
                   headlessResult.turnCount,
                   agent::formatTokenCount(headlessResult.tokenUsage.inputTokens),
                   agent::formatTokenCount(headlessResult.tokenUsage.outputTokens),
                   agent::formatTokenCount(headlessResult.tokenUsage.cacheReadTokens),
                   agent::formatTokenCount(headlessResult.tokenUsage.cacheCreationTokens));
    }

    return headlessResult.success ? EXIT_SUCCESS : EXIT_FAILURE;
}

void Shell::offerErrorRecovery(int exitCode, std::string const& command)
{
    // Try to capture command output via semantic block query (Contour VT extension).
    auto commandOutput = std::string {};
    if (_semanticBlockClient && _semanticBlockClient->isEnabled())
    {
        auto const result = _semanticBlockClient->queryLastCommand();
        if (result.status == tui::SemanticBlockStatus::Success && result.block.has_value())
            commandOutput = result.block->output;
    }

    // Determine effective action (session override takes precedence).
    auto const effectiveAction =
        _hasSessionOverride ? _sessionErrorRecoveryOverride : agentConfig.errorRecovery.action;

    if (effectiveAction == agent::ErrorRecoveryAction::Analyze)
    {
        // Auto-analyze without asking.
    }
    else
    {
        // Ask the user via QuestionComponent.
        auto& terminal = prompt.terminal();
        auto& out = terminal.output();
        auto const& theme = tui::currentTheme();

        auto questionConfig = tui::QuestionConfig {
            .questionText = std::format("Command failed (exit {}). Analyze this error?", exitCode),
            .options = { "Analyze", "Analyze (always)", "Ignore", "Ignore (always)" },
            .allowOther = false,
        };
        auto question = tui::QuestionComponent(questionConfig);

        // Render the question inline.
        auto const width = terminal.columns();
        auto const prefSize = question.preferredSize();
        auto const height = prefSize.height;

        // Reserve room for the question.
        for (auto i = 0; i < height; ++i)
            out.linefeed();
        out.moveUp(height);
        out.saveCursor();

        auto renderQuestion = [&] {
            out.restoreCursor();
            auto buffer = tui::Buffer(height, width);
            auto canvas =
                tui::Canvas(buffer, tui::Rect { .x = 0, .y = 0, .width = width, .height = height }, theme);
            question.setArea(tui::Rect { .x = 0, .y = 0, .width = width, .height = height });
            question.setScreenBounds(tui::Rect { .x = 0, .y = 0, .width = width, .height = height });
            question.render(canvas);
            buffer.writeTo(out);
            out.showCursor();
            out.flush();
        };

        renderQuestion();

        // Input loop for the question.
        auto answered = false;
        while (!answered)
        {
            auto events = terminal.input().poll(-1);
            for (auto const& event: events)
            {
                auto const action = question.processInput(event);
                switch (action)
                {
                    case tui::QuestionAction::Confirmed: answered = true; break;
                    case tui::QuestionAction::Cancelled: {
                        // Clean up the question display.
                        out.restoreCursor();
                        out.clearToEndOfDisplay();
                        out.flush();
                        return; // User cancelled — do nothing.
                    }
                    case tui::QuestionAction::Changed: renderQuestion(); break;
                    case tui::QuestionAction::None: break;
                }
                if (answered)
                    break;
            }
        }

        // Clean up the question display.
        out.restoreCursor();
        out.clearToEndOfDisplay();
        out.flush();

        auto const answer = question.answer();
        if (answer == "Ignore")
            return;
        if (answer == "Ignore (always)")
        {
            _hasSessionOverride = true;
            _sessionErrorRecoveryOverride = agent::ErrorRecoveryAction::Ignore;
            return;
        }
        if (answer == "Analyze (always)")
        {
            _hasSessionOverride = true;
            _sessionErrorRecoveryOverride = agent::ErrorRecoveryAction::Analyze;
        }
        // "Analyze" or "Analyze (always)" — fall through to analysis.
    }

    // Build the initial message with error context.
    if (commandOutput.size() > agentConfig.maxToolResultSize)
        commandOutput.resize(agentConfig.maxToolResultSize);

    auto message = std::string {};
    if (!commandOutput.empty())
    {
        message = std::format("The following shell command failed with exit code {}:\n\n```\n{}\n```\n\n"
                              "Command output:\n```\n{}\n```\n\n"
                              "Please analyze why this command failed and suggest how to fix it.",
                              exitCode,
                              command,
                              commandOutput);
    }
    else
    {
        message = std::format("The following shell command failed with exit code {}:\n\n```\n{}\n```\n\n"
                              "Please analyze why this command failed and suggest how to fix it.",
                              exitCode,
                              command);
    }

    // If agent_error_recovery_model is configured, temporarily override the active provider/model
    // so that error recovery uses the specified model instead of the current agent model.
    auto const& errorModel = agentConfig.errorRecovery.model;
    if (!errorModel.empty())
    {
        auto const preferredProvider =
            _agentProviderFactory ? _agentProviderFactory->activeProviderName() : std::string {};
        auto const match = agent::findModelByName(errorModel, preferredProvider);
        if (!match)
        {
            std::println(
                stderr, "Warning: Unknown error recovery model '{}', using active agent model.", errorModel);
        }
        else
        {
            // Resolve the model config pointer for the matched provider.
            std::string* modelPtr = nullptr;
            if (match->providerName == "claude")
                modelPtr = &agentConfig.claude.model;
            else if (match->providerName == "openai")
                modelPtr = &agentConfig.openai.model;
            else if (match->providerName == "openai_compat")
                modelPtr = &agentConfig.openaiCompat.model;
            else if (match->providerName == "gemini")
                modelPtr = &agentConfig.gemini.model;

            if (modelPtr)
            {
                // Save original config values.
                auto const savedActiveProvider =
                    std::exchange(agentConfig.activeProvider, std::string(match->providerName));
                auto const savedModel = std::exchange(*modelPtr, std::string(match->modelName));
                auto savedSession = std::move(_agentSession);
                _agentProviderFactory.reset();

                // Scope guard restores original config regardless of how runAgentMode exits.
                auto guard = ScopeGuard([&] {
                    agentConfig.activeProvider = savedActiveProvider;
                    *modelPtr = savedModel;
                    _agentSession = std::move(savedSession);
                    _agentProviderFactory.reset();

                    // Eagerly recreate the factory and rebind the existing session's provider
                    // to avoid a dangling pointer from the destroyed temporary factory.
                    if (_agentSession)
                    {
                        _agentProviderFactory =
                            std::make_unique<agent::ProviderFactory>(*_agentHttpClient, agentConfig);
                        if (auto* restoredProvider = _agentProviderFactory->activeProvider())
                            _agentSession->setProvider(*restoredProvider);
                    }
                });

                runAgentMode(std::move(message));

                // Restore terminal dimensions after agent mode.
                prompt.terminal().output().updateDimensions();
                return;
            }
        }
    }

    // Enter agent mode with the pre-filled error context (default: active agent model).
    // Use an isolated session so that prior agent conversation context does not
    // leak into error recovery (the LLM should only see the failing command).
    {
        auto savedSession = std::move(_agentSession);
        runAgentMode(std::move(message));
        _agentSession = std::move(savedSession);
    }

    // Restore terminal dimensions after agent mode.
    prompt.terminal().output().updateDimensions();
}

void Shell::runAgentMode(std::optional<std::string> initialMessage)
{
    // Own a runtime + event source for the duration of agent mode. The event source
    // multiplexes terminal input with the agent-message wakeup (fixing the Windows
    // gap where the input poll never woke on agent messages). SIGINT is ignored here:
    // Ctrl+C in agent mode is a key the input component turns into an Abort.
    auto& agentTerminal = prompt.terminal();
    auto agentEventSource =
        tui::runtime::TerminalEventSource(agentTerminal, &_agentWakeup, &_interruptWakeup);
    auto runtime = tui::runtime::TuiRuntime(agentEventSource);
    runtime.setInterruptHandler([] {});
    runtime.blockOn(runAgentModeFlow(&runtime, std::move(initialMessage)));
}

coro::Task<void> Shell::runAgentModeFlow(tui::runtime::TuiRuntime* runtime,
                                         std::optional<std::string> initialMessage)
{
    // Lazy initialization of agent infrastructure
    if (!_agentProviderFactory)
    {
        _agentHttpClient = std::make_unique<http::HttpClient>();
        _agentProviderFactory = std::make_unique<agent::ProviderFactory>(*_agentHttpClient, agentConfig);
    }

    auto* provider = _agentProviderFactory->activeProvider();
    if (!provider)
    {
        auto const& theme = tui::currentTheme();
        auto& out = prompt.terminal().output();
        auto const errorStyle = tui::Style { .fg = theme.agentColors.errorText };
        auto const mutedStyle = tui::Style { .fg = theme.agentColors.statusText };
        if (!agentConfig.activeProvider.empty())
        {
            out.writeText(std::format("Provider '{}' is not available.\n", agentConfig.activeProvider),
                          errorStyle);
            out.writeText("Check your configuration or run `endo agent status` for details.\n", mutedStyle);
        }
        else
        {
            out.writeText("No AI provider configured or authenticated.\n", errorStyle);
            out.writeText("Run `endo agent login` or configure a provider in ~/.config/endo/init.endo.\n",
                          mutedStyle);
        }
        out.flush();
        out.setInlineRoomReserved(0); // Prevent prompt from overwriting error text
        co_return;
    }

    // Create or reuse agent session (preserves conversation history).
    // Only load persisted history from disk for a brand-new session.
    // When reusing an existing session, the in-memory history is the source of truth;
    // re-loading from disk would re-inject old conversation turns that the LLM
    // already responded to, causing it to answer stale questions again.
    auto const freshSession = !_agentSession;
    if (freshSession)
        _agentSession = std::make_unique<agent::AgentSession>(*provider);

    auto const historyStore = agent::ConversationHistoryStore(".endo/agent-history.json");
    auto const sessionManager = agent::SessionManager(".endo");

    auto historyProvider = std::make_unique<agent::AgentHistoryProvider>();
    auto loadedFromNamedSession = false;

    if (freshSession)
    {
        // Try auto-resume of last named session first.
        if (agentConfig.session.autoResume)
        {
            auto const lastSession = sessionManager.lastActiveSession();
            if (!lastSession.empty() && sessionManager.sessionExists(lastSession))
            {
                if (auto loaded = sessionManager.loadSession(lastSession); loaded.has_value())
                {
                    auto& [meta, messages] = *loaded;
                    for (auto const& msg: messages)
                    {
                        if (msg.role == agent::Role::User)
                        {
                            auto const text =
                                agent::FileReferenceExpander::stripExpansions(msg.textContent());
                            if (!text.empty())
                                historyProvider->addEntry(text);
                        }
                    }
                    _agentSession->loadPersistedMessages(std::move(messages));
                    _activeSessionName = lastSession;
                    _sessionCreatedAt = meta.createdAt;
                    loadedFromNamedSession = true;
                }
            }
        }

        // Fall through to anonymous history load if no named session was resumed.
        if (!loadedFromNamedSession)
        {
            if (auto loaded = historyStore.load(); loaded.has_value() && !loaded->empty())
            {
                for (auto const& msg: *loaded)
                {
                    if (msg.role == agent::Role::User)
                    {
                        auto const text = agent::FileReferenceExpander::stripExpansions(msg.textContent());
                        if (!text.empty())
                            historyProvider->addEntry(text);
                    }
                }
                _agentSession->loadPersistedMessages(std::move(*loaded));
            }
        }
    }
    else
    {
        // Reusing existing session — populate history provider from in-memory history
        // so that ghost-text completion of past queries still works.
        for (auto const& msg: _agentSession->history().messages())
        {
            if (msg.role == agent::Role::User)
            {
                auto const text = agent::FileReferenceExpander::stripExpansions(msg.textContent());
                if (!text.empty())
                    historyProvider->addEntry(text);
            }
        }
    }
    auto* historyProviderPtr = historyProvider.get();

    // Set up tool registry with built-in tools
    auto toolRegistry = agent::ToolRegistry {};

    #if !defined(_WIN32)
    auto const shellPath = [&]() -> std::string {
        if (access("/bin/bash", X_OK) == 0)
            return "/bin/bash";
        if (access("/usr/bin/bash", X_OK) == 0)
            return "/usr/bin/bash";
        return "/bin/sh";
    }();

    auto shellExecCb = [shellPath](std::string const& command,
                                   std::chrono::milliseconds timeout) -> agent::ShellExecResult {
        return shellExecImpl(shellPath, command, timeout);
    };

    auto endoExecCb = [this](std::string const& source,
                             std::chrono::milliseconds /*timeout*/) -> agent::EndoExecResult {
        auto* tmpFile = tmpfile();
        if (!tmpFile)
            return agent::EndoExecResult { .output = "Failed to create temporary file", .exitCode = -1 };

        auto const tmpFd = fileno(tmpFile);
        auto const savedStdout = dup(STDOUT_FILENO);
        auto const savedStderr = dup(STDERR_FILENO);

        fflush(stdout);
        fflush(stderr);
        dup2(tmpFd, STDOUT_FILENO);
        dup2(tmpFd, STDERR_FILENO);

        auto const exitCode = this->execute(source);

        fflush(stdout);
        fflush(stderr);
        if (savedStdout >= 0)
        {
            dup2(savedStdout, STDOUT_FILENO);
            close(savedStdout);
        }
        if (savedStderr >= 0)
        {
            dup2(savedStderr, STDERR_FILENO);
            close(savedStderr);
        }

        fflush(tmpFile);
        auto const outputSize = lseek(tmpFd, 0, SEEK_END);
        lseek(tmpFd, 0, SEEK_SET);

        auto output = std::string {};
        if (outputSize > 0)
        {
            output.resize(static_cast<size_t>(outputSize));
            auto const bytesRead = ::read(tmpFd, output.data(), output.size());
            if (bytesRead >= 0)
                output.resize(static_cast<size_t>(bytesRead));
            else
                output.clear();
        }
        fclose(tmpFile);

        return agent::EndoExecResult { .output = std::move(output), .exitCode = exitCode };
    };
    #else
    auto shellExecCb = [](std::string const& command,
                          std::chrono::milliseconds timeout) -> agent::ShellExecResult {
        return shellExecImpl(command, timeout);
    };

    auto endoExecCb = [this](std::string const& source,
                             std::chrono::milliseconds /*timeout*/) -> agent::EndoExecResult {
        auto const exitCode = this->execute(source);
        return agent::EndoExecResult { .output = {}, .exitCode = exitCode };
    };
    #endif

    toolRegistry.registerTool(std::make_unique<agent::ReadFileTool>());
    toolRegistry.registerTool(std::make_unique<agent::WriteFileTool>());
    toolRegistry.registerTool(std::make_unique<agent::EditFileTool>());
    toolRegistry.registerTool(std::make_unique<agent::GlobTool>());
    toolRegistry.registerTool(std::make_unique<agent::GrepTool>());
    toolRegistry.registerTool(std::make_unique<agent::SearchTool>());
    toolRegistry.registerTool(std::make_unique<agent::ListDirectoryTool>());
    toolRegistry.registerTool(std::make_unique<agent::ShellExecuteTool>(shellExecCb));
    toolRegistry.registerTool(std::make_unique<agent::EndoExecuteTool>(endoExecCb));
    toolRegistry.registerTool(std::make_unique<agent::GitTool>(shellExecCb));
    toolRegistry.registerTool(
        std::make_unique<agent::SaveMemoryTool>([this]() { _cachedProjectContext.reset(); }));
    toolRegistry.registerTool(std::make_unique<agent::SubmitPlanTool>());
    toolRegistry.registerTool(
        std::make_unique<agent::ExploreTool>(*provider, shellExecCb, agentConfig.explore));
    toolRegistry.registerTool(std::make_unique<agent::WebSearchTool>(*_agentHttpClient, webSearchConfig));

    auto const webFetchConfig = agent::WebFetchConfig {};
    toolRegistry.registerTool(std::make_unique<agent::WebFetchTool>(*_agentHttpClient, webFetchConfig));

    // Start MCP servers and register their tools
    auto mcpServerManager = agent::mcp::ServerManager {};
    for (auto const& config: mcpServerConfigs)
    {
        auto result = mcpServerManager.addServer(config);
        if (!result)
            std::println(stderr, "MCP: Failed to connect server '{}': {}", config.name, result.error());
    }
    for (auto const& toolDef: mcpServerManager.allTools())
        toolRegistry.registerTool(std::make_unique<agent::mcp::McpToolAdapter>(mcpServerManager, toolDef));

    mcpServerManager.setToolsChangedCallback([&toolRegistry,
                                              &mcpServerManager](std::string_view /*serverName*/,
                                                                 std::span<agent::ToolDefinition const> added,
                                                                 std::vector<std::string> const& removed) {
        for (auto const& name: removed)
            toolRegistry.unregisterTool(name);
        for (auto const& def: added)
            toolRegistry.registerTool(std::make_unique<agent::mcp::McpToolAdapter>(mcpServerManager, def));
    });

    _agentSession->setToolRegistry(&toolRegistry);
    _agentSession->setMaxToolResultSize(agentConfig.maxToolResultSize);
    _agentSession->setMaxExplorationIterations(agentConfig.planMode.maxExplorationTurns);
    _agentSession->setTracer(nullptr);
    _agentSession->setToolStatusCallback(nullptr);

    // Set up tool I/O tracing if enabled via CLI flag or config
    auto agentTracer = std::optional<agent::AgentTracer> {};
    if (_agentTracePath.has_value() || agentConfig.trace.enabled)
    {
        auto tracePath = std::string {};
        if (_agentTracePath.has_value() && !_agentTracePath->empty())
        {
            tracePath = *_agentTracePath;
        }
        else if (!agentConfig.trace.defaultPath.empty())
        {
            tracePath = agentConfig.trace.defaultPath;
        }
        else
        {
            auto const traceDir = agent::resolveTraceLogDirectory();
            auto const now = std::chrono::system_clock::now();
            auto const timestamp =
                std::format("{:%Y%m%d-%H%M%S}", std::chrono::floor<std::chrono::seconds>(now));
            tracePath = platform::normalizePath(traceDir / ("agent-trace-" + timestamp + ".jsonl"));
        }

        auto tracerResult = agent::AgentTracer::create(tracePath);
        if (tracerResult.has_value())
        {
            agentTracer.emplace(std::move(*tracerResult));
            auto const tracerModelInfo = provider->modelInfo();
            agentTracer->writeSessionHeader(tracerModelInfo.providerName, tracerModelInfo.modelName);
            _agentSession->setTracer(&*agentTracer);

            // Prune old trace files if auto-generated path was used.
            if (!_agentTracePath.has_value() && agentConfig.trace.defaultPath.empty())
                agent::pruneOldTraceFiles(agentTracer->path().parent_path(), agentConfig.trace.maxFiles);
        }
        else
        {
            std::println(std::cerr, "Warning: Failed to open trace file: {}", tracerResult.error());
        }
    }

    // --- Set up outbound queue and AgentWorker ---
    auto agentOutbound = platform::MessageQueue<agent::FromAgentMessage> {};
    agentOutbound.setWakeup(&_agentWakeup);

    auto worker = agent::AgentWorker(*_agentSession, agentOutbound);

    // Register AskUserTool with a callback that routes through the worker's message queues.
    toolRegistry.registerTool(std::make_unique<agent::AskUserTool>(worker.makeAskUserCallback()));

    // Set up permission manager with a callback that routes through the worker's message queues.
    auto permissionManager = agent::PermissionManager(agentConfig.permissions);
    permissionManager.setPromptCallback(worker.makePermissionCallback());
    _agentSession->setPermissionManager(&permissionManager);

    worker.start();

    // --- Agent input loop ---
    auto& terminal = prompt.terminal();
    auto& out = terminal.output();
    auto const& theme = tui::currentTheme();

    // Point the outbound-message wakeup at the terminal input. The agent loop now
    // waits via the coroutine runtime's event source (which also selects on this
    // wakeup directly), so this keeps any residual TerminalInput::poll() path in
    // sync; the event source is what actually wakes the agent loop on a message.
    terminal.input().setWakeup(&_agentWakeup);

    // Wake the runtime's wait on focus changes so the poll timeout (focused vs
    // unfocused) is re-evaluated immediately on the next loop iteration.
    terminal.onFocusChanged([this](bool) { _agentWakeup.signal(); });

    // Create inline Screen with AgentInputComponent
    auto screenConfig = tui::ScreenConfig {
        .viewport = tui::Viewport::Inline,
        .inhibitReflow = true,
    };
    auto screen = tui::Screen(terminal, screenConfig);
    auto inputComponent = agent::AgentInputComponent {};
    auto toolStatusComponent = agent::ToolStatusComponent {};

    inputComponent.setTopPadding(prompt.promptConfig().promptSpacing);
    inputComponent.setPromptIndicator(agentConfig.promptIndicator);
    auto const modelInfo = provider->modelInfo();
    inputComponent.setProviderName(modelInfo.providerName);
    inputComponent.setModelName(modelInfo.modelName);
    inputComponent.setCellPixelDimensions(terminal.cellPixelWidth(), terminal.cellPixelHeight());

    // Set initial thinking mode from config for the active provider.
    auto const& providerName = _agentProviderFactory->activeProviderName();
    auto initialThinkingMode = agent::ThinkingMode::Off;
    if (providerName == "claude")
        initialThinkingMode = agentConfig.claude.thinkingMode;
    else if (providerName == "openai")
        initialThinkingMode = agentConfig.openai.thinkingMode;
    else if (providerName == "openai_compat")
        initialThinkingMode = agentConfig.openaiCompat.thinkingMode;
    else if (providerName == "gemini")
        initialThinkingMode = agentConfig.gemini.thinkingMode;
    inputComponent.setThinkingMode(initialThinkingMode);

    // The @-file completion provider's raw pointer is needed by the session (its
    // file paths are populated once the background context build completes); the
    // unique_ptr is moved into the input component further below.
    auto filePathProvider = std::make_unique<agent::FilePathCompleter>();
    auto* filePathProviderPtr = filePathProvider.get();

    // Launch background context loading (system prompt, project files, git info).
    auto const cwd = std::filesystem::current_path();
    auto cachedCtx = (_cachedProjectContextCwd == cwd) ? std::move(_cachedProjectContext) : std::nullopt;
    _cachedProjectContext.reset();

    // Capture cached git info from the prompt's GitModule to avoid redundant subprocess calls.
    auto cachedGit = std::optional<GitInfo> {};
    if (auto const* gitMod = prompt.gitModule())
        cachedGit = gitMod->cachedInfo();

    auto contextFuture = std::async(
        std::launch::async,
        [&cfg = agentConfig, cwd, ctx = std::move(cachedCtx), git = std::move(cachedGit)]() mutable {
            return buildAgentContext(cfg, cwd, std::move(ctx), std::move(git));
        });

    // The agent-mode loop's mutable state and helper behavior live in this session
    // object; the locals/lambdas below alias or delegate to it during the migration.
    auto session = AgentModeSession(*this,
                                    out,
                                    terminal,
                                    screen,
                                    inputComponent,
                                    toolStatusComponent,
                                    worker,
                                    provider,
                                    sessionManager,
                                    toolRegistry,
                                    mcpServerManager,
                                    historyProviderPtr,
                                    filePathProviderPtr,
                                    contextFuture,
                                    cwd,
                                    permissionManager,
                                    historyStore);

    // Set up slash command registry
    auto slashRegistry = agent::SlashCommandRegistry {};
    agent::registerBuiltinSlashCommands(slashRegistry);
    session.registerSlashCommands(slashRegistry);

    auto slashCompleter = std::make_unique<agent::SlashCommandCompleter>(slashRegistry);
    slashCompleter->setSessionNameProvider([&sessionManager] { return sessionManager.sessionNames(); });
    inputComponent.addCompletionProvider(std::move(slashCompleter));
    // filePathProvider / filePathProviderPtr were declared earlier (the session
    // needs the raw pointer); move the owning unique_ptr into the input component.
    inputComponent.addCompletionProvider(std::move(filePathProvider));
    inputComponent.addCompletionProvider(std::move(historyProvider));

    // Set up command palette registry for agent mode
    auto agentCommandRegistry = tui::CommandRegistry {};
    agentCommandRegistry.add({
        .id = "agent.toggle_plan_mode",
        .label = "Toggle Plan Mode",
        .description = "Switch between plan and execute mode",
        .category = "Mode",
        .keybinding = "S-Tab",
        .context = tui::CommandContext::Agent,
        .action = [] {}, // Handled via Action::CycleMode
    });
    agentCommandRegistry.add({
        .id = "agent.cycle_thinking",
        .label = "Cycle Thinking Mode",
        .description = "Cycle through off/normal/extended thinking",
        .category = "Mode",
        .keybinding = "Ctrl+/",
        .context = tui::CommandContext::Agent,
        .action = [] {}, // Handled via Action::CycleThinkingMode
    });
    agentCommandRegistry.add({
        .id = "agent.cycle_model",
        .label = "Switch Model",
        .description = "Cycle through available AI models",
        .category = "Provider",
        .keybinding = "Ctrl+.",
        .context = tui::CommandContext::Agent,
        .action = [] {}, // Handled via Action::CycleModel
    });
    agentCommandRegistry.add({
        .id = "agent.exit",
        .label = "Exit Agent Mode",
        .description = "Return to shell prompt",
        .category = "Mode",
        .keybinding = "Esc",
        .context = tui::CommandContext::Agent,
        .action = [] {}, // Handled via Action::Abort
    });
    agentCommandRegistry.add({
        .id = "agent.clear_screen",
        .label = "Clear Screen",
        .description = "Clear the terminal screen",
        .category = "View",
        .keybinding = "Ctrl+L",
        .context = tui::CommandContext::Both,
        .action = [] {}, // Handled via Action::ClearScreen
    });
    inputComponent.setCommandRegistry(&agentCommandRegistry);

    for (auto const& entry: historyProviderPtr->entries())
        inputComponent.inputField().addHistory(entry);

    auto const prefSize = inputComponent.preferredSize();
    inputComponent.setArea(
        tui::Rect { .x = 0, .y = 0, .width = terminal.columns(), .height = prefSize.height });
    screen.root().addChild(inputComponent);
    screen.setFocus(&inputComponent);
    screen.invalidate();
    screen.draw();
    out.flush();

    // Show auto-resume context message if a named session was loaded.
    if (loadedFromNamedSession && agentConfig.session.showResumeContext)
    {
        auto const dimStyle = tui::Style { .fg = theme.agentColors.statusText };
        auto const total =
            _agentSession->sessionUsage().inputTokens + _agentSession->sessionUsage().outputTokens;
        out.writeText(std::format("Resumed session '{}' ({} turns, ~{}k tokens).\n",
                                  _activeSessionName,
                                  _agentSession->turnCount(),
                                  total / 1000),
                      dimStyle);
        out.flush();
    }

    // If an initial message was provided (e.g., from error recovery), submit it immediately.
    if (initialMessage.has_value() && !initialMessage->empty())
    {
        // Ensure system prompt is ready.
        session.ensureSystemPromptReady();

        mcpServerManager.processNotifications();

        auto const query = std::move(*initialMessage);
        initialMessage.reset();

        out.carriageReturn();
        out.linefeed();

        screen.releaseCursor();

        worker.inbound().push(agent::UserPromptMessage { .text = query });
        session.streaming = true;
        session.streamCancelled = false;
    }

    // --- Main event loop ---
    while (true)
    {
        // 1. Drain agent outbound messages (non-blocking).
        auto agentMessages = std::vector<agent::FromAgentMessage> {};
        agentOutbound.drainTo(agentMessages);

        // Process the drained batch (streaming, tool status, prompts, completion).
        session.drainAgentMessages(agentMessages, modelInfo);

        // 2. Determine poll timeout.
        auto const prePollFocused = terminal.isFocused();
        auto pollTimeout = prePollFocused ? 80 : 2000; // Longer timeout when unfocused.
        if (session.streaming)
        {
            pollTimeout = 5; // Fast polling during streaming for responsive cancellation.
        }
        else if (prePollFocused)
        {
            auto const ghostTimeout = inputComponent.ghostTextTimeoutMs();
            auto const escapeTimeout = inputComponent.escapeHintTimeoutMs();
            if (ghostTimeout >= 0)
                pollTimeout = std::min(ghostTimeout, 80);
            if (escapeTimeout >= 0)
                pollTimeout = std::min(escapeTimeout, pollTimeout);
        }
        // Include input component spinner timeout for info line animation.
        if (prePollFocused)
        {
            if (auto const spinnerTimeout = inputComponent.spinnerTimeoutMs(); spinnerTimeout >= 0)
                pollTimeout = std::min(spinnerTimeout, pollTimeout);
            if (auto const toolSpinnerTimeout = toolStatusComponent.spinnerTimeoutMs();
                toolSpinnerTimeout >= 0)
                pollTimeout = std::min(toolSpinnerTimeout, pollTimeout);
        }

        // 3. Wait for input, an agent message, or a timeout (whichever happens first).
        auto const activity = co_await runtime->nextActivity(std::chrono::milliseconds { pollTimeout });
        if (activity.kind == tui::runtime::ActivityKind::AgentReady)
            continue; // Loop back to drain the agent messages at the top.

        // Re-read focus state after the wait — a FocusEvent may have been consumed.
        auto const terminalFocused = terminal.isFocused();

        if (activity.kind == tui::runtime::ActivityKind::Timeout)
        {
            // Check background context loading. ensureSystemPromptReady() applies
            // the built context (system prompt, git/project info, file paths) and
            // is shared with the submit path; the guard ensures get() won't block.
            if (!session.systemPromptReady
                && contextFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            {
                session.ensureSystemPromptReady();
                auto const newPrefSize = inputComponent.preferredSize();
                inputComponent.setArea(
                    tui::Rect { .x = 0, .y = 0, .width = terminal.columns(), .height = newPrefSize.height });
                screen.draw();
            }

            // Skip spinner and deferred updates when terminal is unfocused
            if (terminalFocused)
            {
                // Tick all spinners first, then do a single combined render pass.
                auto* const activeRenderer = session.activeRenderer;
                auto const thinkingTicked = activeRenderer && activeRenderer->isThinking()
                                            && !session.anyPromptActive() && activeRenderer->tickSpinner();
                auto const toolTicked =
                    session.streaming && toolStatusComponent.tickSpinner() && !session.anyPromptActive();
                auto const inputTicked = inputComponent.tickSpinner();

                if (session.streaming && !session.anyPromptActive()
                    && (thinkingTicked || toolTicked || inputTicked))
                {
                    auto guard = out.syncGuard();
                    session.clearStreamingPrompt();
                    // Always re-render the spinner if thinking — clearStreamingPrompt erases it.
                    if (activeRenderer && activeRenderer->isThinking())
                        activeRenderer->renderSpinner();
                    if (toolStatusComponent.hasEntries())
                    {
                        session.renderToolStatusDirect();
                        out.flush();
                    }
                    session.renderStreamingPrompt();
                }
                else if (inputTicked && !session.anyPromptActive())
                {
                    auto const newPrefSize = inputComponent.preferredSize();
                    inputComponent.setArea(tui::Rect {
                        .x = 0, .y = 0, .width = terminal.columns(), .height = newPrefSize.height });
                    screen.draw();
                }
            }

            // Re-render inline prompts if they were cleared (e.g., by resize).
            if (session.askUserPrompt.active && !session.askUserPrompt.visible)
                session.askUserPrompt.render(out, terminal);
            if (session.permissionPrompt.active && !session.permissionPrompt.visible)
                session.permissionPrompt.render(out, terminal);
            if (session.planApprovalPrompt.active && !session.planApprovalPrompt.visible)
                session.planApprovalPrompt.render(out, terminal);
            if (session.sessionPickerPrompt.active && !session.sessionPickerPrompt.visible)
                session.sessionPickerPrompt.render(out, terminal);

            // Ghost text debounce and escape hint auto-clear.
            if (!session.streaming && terminalFocused)
            {
                // Capture pre-flush state: flushDeferredUpdates() may clear the escape hint,
                // and we still need to redraw to show the restored input text.
                auto const wasEscapeHintVisible = inputComponent.escapeHintTimeoutMs() >= 0;
                inputComponent.flushDeferredUpdates();
                if (wasEscapeHintVisible || inputComponent.inputField().hasGhostText()
                    || inputComponent.ghostTextTimeoutMs() >= 0 || inputComponent.escapeHintTimeoutMs() >= 0)
                {
                    auto const newPrefSize = inputComponent.preferredSize();
                    inputComponent.setArea(tui::Rect {
                        .x = 0, .y = 0, .width = terminal.columns(), .height = newPrefSize.height });
                    screen.draw();
                }
            }
            continue;
        }

        // 4. Process the terminal input event.
        auto needsRedraw = false;
        {
            auto const& event = *activity.event;
            // A resize only needs a redraw; skip all per-event dispatch and fall
            // through to the redraw blocks below, which re-layout the input
            // component and re-render any active inline prompt at the new width.
            auto const isResize = std::holds_alternative<tui::ResizeEvent>(event);
            if (isResize)
                needsRedraw = true;

            // A modifier-only keypress (bare Ctrl/Shift/Alt) produces no text or
            // action; ignore it with no redraw. (Never a resize, so order is moot.)
            if (auto const* key = std::get_if<tui::KeyEvent>(&event); key && tui::isModifierOnlyKey(key->key))
                continue;

            // Dispatch the event only when it is not a resize.
            if (!isResize)
                if (session.handleInputEvent(event, needsRedraw, slashRegistry) == LoopControl::Exit)
                    co_return;
        }

        if (needsRedraw && !session.streaming)
        {
            inputComponent.flushDeferredUpdates();
            auto const newPrefSize = inputComponent.preferredSize();
            inputComponent.setArea(
                tui::Rect { .x = 0, .y = 0, .width = terminal.columns(), .height = newPrefSize.height });
            screen.draw();
        }

        // Re-render active inline prompts on resize.
        if (needsRedraw && session.anyPromptActive())
        {
            auto guard = out.syncGuard();
            for (auto* p: { &session.askUserPrompt,
                            &session.permissionPrompt,
                            &session.planApprovalPrompt,
                            &session.sessionPickerPrompt })
            {
                if (p->active)
                {
                    p->clear(out);
                    p->render(out, terminal);
                }
            }
        }
    }
}

#endif // ENDO_ENABLE_AGENT

} // namespace endo
