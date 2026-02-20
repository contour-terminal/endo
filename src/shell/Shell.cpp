// SPDX-License-Identifier: Apache-2.0
#include "Shell.hpp"

#include <endo-language/ASTPrinter.hpp>
#include <endo-language/IRGenerator.hpp>
#include <endo-language/Lexer.hpp>
#include <endo-language/LogCategories.hpp>
#include <endo-language/LogConfig.hpp>
#include <endo-language/Parser.hpp>

#include <tui/MarkdownRenderer.hpp>
#include <tui/Screen.hpp>
#include <tui/Theme.hpp>

#include <CoreVM/CoreVM.hpp>
#include <CoreVM/types/TypeDescriptor.hpp>

#include <crispy/assert.h>

#include <algorithm>
#include <array>
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
#include "Prompt.hpp"
#include "TTY.hpp"
#include <agent/AgentConfig.hpp>
#include <agent/AgentHistoryProvider.hpp>
#include <agent/AgentInputComponent.hpp>
#include <agent/AgentMessages.hpp>
#include <agent/AgentResponseRenderer.hpp>
#include <agent/AgentSession.hpp>
#include <agent/AgentTracer.hpp>
#include <agent/AgentWorker.hpp>
#include <agent/ConversationHistoryStore.hpp>
#include <agent/FilePathCompleter.hpp>
#include <agent/PlanExecutor.hpp>
#include <agent/ProjectContextLoader.hpp>
#include <agent/SlashCommandCompleter.hpp>
#include <agent/SlashCommandRegistry.hpp>
#include <agent/SlashCommands.hpp>
#include <agent/SystemPromptBuilder.hpp>
#include <agent/mcp/McpToolAdapter.hpp>
#include <agent/providers/ProviderFactory.hpp>
#include <agent/tools/AskUserTool.hpp>
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
#include <nlohmann/json.hpp>
#include <platform/Pipe.hpp>
#include <platform/Process.hpp>
#include <platform/SignalHandler.hpp>
#include <platform/Types.hpp>
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
            break;
        else if (n == -1)
        {
            if (errno == EINTR)
                continue;
            else
                break;
        }
        else if (ch == '\n')
            break;
        else
            line += ch;
    }
    return line;
}

// ========================================================================
// Shell::PipelineBuilder implementation
// ========================================================================

auto Shell::PipelineBuilder::requestShellPipe(bool lastInChain) -> IODescriptors
{
    NativeHandle const stdinFd = !currentPipe ? defaultStdinFd : currentPipe->releaseReader();
    if (lastInChain)
        currentPipe = nullptr;
    else if (auto pipeResult = createPipe(); pipeResult.has_value())
        currentPipe = std::move(pipeResult.value());
    else
        currentPipe = nullptr; // Error case - will result in using default stdout
    NativeHandle const stdoutFd = lastInChain || !currentPipe ? defaultStdoutFd : currentPipe->writer();
    return IODescriptors { .reader = stdinFd, .writer = stdoutFd };
}

void Shell::PipelineBuilder::closeCurrentPipeWriter()
{
    if (currentPipe)
        currentPipe->closeWriter();
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

Shell::Shell(TTY& tty, EnvironmentProvider& env):
    _env { env },
    _tty { tty },
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

    _env.setAndExport("SHELL", "endo");
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

    // Seed built-in record type fields for completion support
    _fsharpState.recordTypeFields["ProcessInfo"] = {
        { "pid", "int" },   { "ppid", "int" },  { "user", "str" },
        { "cpu", "float" }, { "mem", "float" }, { "command", "str" },
    };
    _fsharpState.recordTypeFields["FileInfo"] = {
        { "name", "str" }, { "size", "int" }, { "mode", "int" }, { "mtime", "int" }, { "isDir", "bool" },
    };
    _fsharpState.recordTypeFields["JobInfo"] = {
        { "id", "int" },
        { "state", "str" },
        { "command", "str" },
        { "pid", "int" },
    };

    // Load output definition files for structured pipelines
#if defined(ENDO_DEFINITIONS_DIR)
    _outputDefinitions.loadFromDirectory(ENDO_DEFINITIONS_DIR);
#endif
#if defined(_WIN32)
    if (auto const* appData = std::getenv("LOCALAPPDATA"))
        _outputDefinitions.loadFromDirectory(std::filesystem::path(appData) / "endo" / "definitions");
    else if (auto const* userProfile = std::getenv("USERPROFILE"))
        _outputDefinitions.loadFromDirectory(std::filesystem::path(userProfile) / ".config" / "endo"
                                             / "definitions");
#else
    if (auto const* home = std::getenv("HOME"))
        _outputDefinitions.loadFromDirectory(std::filesystem::path(home) / ".config" / "endo"
                                             / "definitions");
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
                        variant.schema[i].name,
                        static_cast<uint8_t>(i),
                        variant.schema[i].type,
                    });
                }
                _fsharpState.outputDefinitionTypes[variant.recordTypeName] = std::move(defType);

                // Register structured command lookup
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
                }

                // Register record type fields for completion
                std::vector<RecordFieldInfo> fieldInfos;
                for (auto const& field: variant.schema)
                    fieldInfos.push_back({ field.name,
                                           field.type == CoreVM::LiteralType::Number    ? "int"
                                           : field.type == CoreVM::LiteralType::Boolean ? "bool"
                                                                                        : "string" });
                _fsharpState.recordTypeFields[variant.recordTypeName] = std::move(fieldInfos);

                ++nextTypeId;
            }
        }
    }

    // Load persistent history and auto-import from other shells on first run
    history.load();
    history.autoImportIfEmpty();

    // Initialize completion system
    completer = std::make_unique<Completer>(_env, history, _fsharpState);
    prompt.setCompleter(completer.get());
    prompt.setHistory(&history);

    // NB: These lines could go away once we have a proper command line parser and
    //     the ability to set these options from the command line.
    registerBuiltinFunctions();

    // Register dark/light mode auto-switching via terminal color scheme detection
    prompt.terminal().onColorSchemeChanged([](tui::ColorScheme scheme) {
        auto& mgr = tui::ThemeManager::instance();
        mgr.setCurrent(scheme == tui::ColorScheme::Light ? tui::lightTheme() : tui::darkTheme());
    });
}

Shell::~Shell()
{
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

void Shell::setAgentTracePath(std::string path)
{
    _agentTracePath = std::move(path);
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
    auto hostname = std::array<char, 256> {};
#if defined(_WIN32)
    DWORD hostnameLen = static_cast<DWORD>(hostname.size());
    if (!GetComputerNameA(hostname.data(), &hostnameLen))
        hostname[0] = '\0';
#else
    if (gethostname(hostname.data(), hostname.size()) != 0)
        hostname[0] = '\0';
#endif

    _tty.writeToStdout(std::format("\033]7;file://{}{}\033\\", hostname.data(), encoded));
}

int Shell::run()
{
    if (_interactive && !_tty.isTerminal())
    {
        std::cerr << "endo: interactive mode requires a terminal.\n";
        return EXIT_FAILURE;
    }

    // Load API keys from agent.yml (init.endo overrides all other settings).
    agentConfig = agent::loadAgentConfig();

    // Auto-execute init.endo if it exists (only in interactive mode)
    if (auto const* home = std::getenv("HOME"))
    {
        auto const initPath = std::filesystem::path(home) / ".config" / "endo" / "init.endo";
        if (std::filesystem::exists(initPath))
        {
            try
            {
                auto ifs = std::ifstream(initPath);
                auto content = std::string(std::istreambuf_iterator<char>(ifs), {});
                auto const initResult = execute(content);
                if (initResult != 0)
                    std::println(std::cerr, "endo: warning: init.endo exited with code {}", initResult);
            }
            catch (std::exception const& e)
            {
                std::println(std::cerr, "endo: warning: error loading {}: {}", initPath.string(), e.what());
            }
        }
    }

#if !defined(_WIN32)
    pollfd fds[2];
    fds[0].fd = _tty.inputFd();
    fds[0].events = POLLIN;
    fds[1].fd = _signalFd; // -1 on non-Linux (ignored by poll when nfds=1)
    fds[1].events = POLLIN;

    int const nfds = (_signalFd >= 0) ? 2 : 1;

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
        {
            auto ctx = PromptContext {};
            ctx.cwd = std::filesystem::current_path().string();
            if (auto const* home = std::getenv("HOME"))
                ctx.homePath = home;
    #if defined(_WIN32)
            else if (auto const* userProfile = std::getenv("USERPROFILE"))
                ctx.homePath = userProfile;
    #endif
            ctx.lastExitCode = _exitCode;
            ctx.lastDuration = _lastCommandDuration;
            ctx.terminalWidth = prompt.terminal().columns();
            ctx.isSSH = std::getenv("SSH_CONNECTION") != nullptr;
            if (ctx.isSSH)
            {
                auto buf = std::array<char, 256> {};
    #if defined(_WIN32)
                DWORD bufLen = static_cast<DWORD>(buf.size());
                if (GetComputerNameA(buf.data(), &bufLen))
                    ctx.hostname = buf.data();
    #else
                if (gethostname(buf.data(), buf.size()) == 0)
                    ctx.hostname = buf.data();
    #endif
            }
            ctx.theme = &tui::currentTheme();
            ctx.fsharpState = &_fsharpState;
            ctx.outputDefs = &_outputDefinitions;
            ctx.cellPixelWidth = prompt.terminal().cellPixelWidth();
            ctx.cellPixelHeight = prompt.terminal().cellPixelHeight();
            prompt.setPromptContext(std::move(ctx));
        }

        // Display the prompt before waiting for input
        prompt.display();

        emitPromptEnd();

        // Wait for input or signals
        int const pollResult = poll(fds, static_cast<nfds_t>(nfds), -1);
        if (pollResult < 0)
        {
            if (errno == EINTR)
                continue; // Interrupted by signal, retry
            break;
        }

        // Process signals first (if signalfd is readable)
        if (_signalFd >= 0 && (fds[1].revents & POLLIN))
            SignalHandler::processSignalFd();

        // Check for pending signals again
        SignalHandler::processPendingSignals();

        // Report any newly completed jobs
        reportJobStatus();

        // Process input (if available)
        if (fds[0].revents & POLLIN)
        {
            auto const lineBuffer = prompt.read();
            debugLog()()("input buffer: {}", lineBuffer);

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

            // Add non-empty commands to history
            if (!lineBuffer.empty())
            {
                prompt.addHistory(lineBuffer);
                history.add(lineBuffer);
            }

            auto const _ = Prompt::ScopedSuspend(prompt);
            emitCommandStart();
            auto const cmdStart = std::chrono::steady_clock::now();
            _exitCode = execute(lineBuffer);
            _lastCommandDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - cmdStart);
            emitCommandFinished(_exitCode);

            if (!lineBuffer.empty())
                history.markLastResult(_exitCode);

            // Update diagnostics with known F# names from persisted state
            auto names = std::set<std::string>();
            for (auto const& [name, func]: _fsharpState.functions)
                names.insert(name);
            for (auto const& binding: _fsharpState.valueBindings)
                names.insert(binding.name);
            prompt.setKnownFSharpNames(std::move(names));
        }
    }
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
        {
            auto ctx = PromptContext {};
            ctx.cwd = std::filesystem::current_path().string();
            if (auto const* home = std::getenv("HOME"))
                ctx.homePath = home;
            else if (auto const* userProfile = std::getenv("USERPROFILE"))
                ctx.homePath = userProfile;
            ctx.lastExitCode = _exitCode;
            ctx.lastDuration = _lastCommandDuration;
            ctx.terminalWidth = prompt.terminal().columns();
            ctx.isSSH = std::getenv("SSH_CONNECTION") != nullptr;
            if (ctx.isSSH)
            {
                auto buf = std::array<char, 256> {};
                DWORD bufLen = static_cast<DWORD>(buf.size());
                if (GetComputerNameA(buf.data(), &bufLen))
                    ctx.hostname = buf.data();
            }
            ctx.theme = &tui::currentTheme();
            ctx.fsharpState = &_fsharpState;
            ctx.outputDefs = &_outputDefinitions;
            ctx.cellPixelWidth = prompt.terminal().cellPixelWidth();
            ctx.cellPixelHeight = prompt.terminal().cellPixelHeight();
            prompt.setPromptContext(std::move(ctx));
        }

        // Display the prompt before waiting for input
        prompt.display();

        emitPromptEnd();

        auto const lineBuffer = prompt.read();
        debugLog()()("input buffer: {}", lineBuffer);

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

        // Add non-empty commands to history
        if (!lineBuffer.empty())
        {
            prompt.addHistory(lineBuffer);
            history.add(lineBuffer);
        }

        auto const _ = Prompt::ScopedSuspend(prompt);
        emitCommandStart();
        auto const cmdStart = std::chrono::steady_clock::now();
        _exitCode = execute(lineBuffer);
        _lastCommandDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - cmdStart);
        emitCommandFinished(_exitCode);

        if (!lineBuffer.empty())
            history.markLastResult(_exitCode);

        // Update diagnostics with known F# names from persisted state
        auto names = std::set<std::string>();
        for (auto const& [name, func]: _fsharpState.functions)
            names.insert(name);
        for (auto const& binding: _fsharpState.valueBindings)
            names.insert(binding.name);
        prompt.setKnownFSharpNames(std::move(names));
    }
#endif

    return _quit ? _exitCode : EXIT_SUCCESS;
}

int Shell::execute(std::string const& lineBuffer)
{
    // Clear any leftover redirect state from previous commands
    _redirectState.clear();

    try
    {
        CoreVM::diagnostics::ConsoleReport report;
        auto parser = endo::Parser(_runtime, report, std::make_unique<endo::StringSource>(lineBuffer));
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
        }
        auto rootNode = parser.parse();

        // Check for parser errors
        if (report.containsFailures())
            return EXIT_FAILURE;

        if (!rootNode)
            return EXIT_FAILURE;

        debugLog()()("Parsed & printed: {}", endo::ast::ASTPrinter::print(*rootNode));

        auto irProgram = IRGenerator::generate(*rootNode, report, _runtime, &_fsharpState);

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

        std::println("[{}]{} {}\t{}", job->id, marker, stateStr, job->command);
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

    /// @brief Formats tool call arguments as a compact, truncated string for display.
    /// @param arguments The JSON arguments from a tool call.
    /// @return A compact string representation, truncated at ~120 characters.
    [[nodiscard]] auto formatToolCallArgs(nlohmann::json const& arguments) -> std::string
    {
        if (arguments.is_null() || (arguments.is_object() && arguments.empty()))
            return {};

        // Build compact JSON with truncated string values
        auto truncated = arguments;
        for (auto& [key, value]: truncated.items())
        {
            if (value.is_string())
            {
                // Replace "content" fields (write_file/edit_file payloads) with size placeholder
                if (key == "content" || key == "new_string" || key == "old_string")
                {
                    auto const len = value.get<std::string>().size();
                    value = std::format("<{} chars>", len);
                }
                else if (auto const& s = value.get<std::string>(); s.size() > 60)
                {
                    value = s.substr(0, 57) + "...";
                }
            }
        }

        auto result = truncated.dump(-1);

        static constexpr auto maxLen = size_t { 120 };
        if (result.size() > maxLen)
        {
            result.resize(maxLen - 3);
            result += "...";
        }

        return result;
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
        auto* fp = popen(cmd.c_str(), "r"); // NOLINT(cert-env33-c)
        if (!fp)
            return result;

        auto buf = std::array<char, 256> {};
        while (fgets(buf.data(), static_cast<int>(buf.size()), fp) != nullptr)
            result += buf.data();
        pclose(fp); // NOLINT(cert-env33-c)

        while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
            result.pop_back();
        return result;
    }

    /// @brief Result of background agent context loading.
    struct AgentContextResult
    {
        std::string systemPrompt;             ///< Fully built system prompt.
        std::string exploreSystemPrompt;      ///< System prompt for the explore sub-agent.
        std::string gitBranch;                ///< Current git branch name (for header display).
        std::string projectPath;              ///< Tilde-contracted project path (for header display).
        agent::ProjectContext projectContext; ///< Project context (returned for caching).
    };

    /// @brief Builds agent context (project files, git info, system prompt) — runs in background thread.
    /// @param agentConfig The agent configuration.
    /// @param cwd The current working directory.
    /// @param cachedContext Optional cached project context to reuse (skips file scanning).
    /// @return The assembled context result.
    [[nodiscard]] auto buildAgentContext(agent::AgentConfig const& agentConfig,
                                         std::filesystem::path const& cwd,
                                         std::optional<agent::ProjectContext> cachedContext)
        -> AgentContextResult
    {
        auto projectContext =
            cachedContext ? std::move(*cachedContext) : agent::ProjectContextLoader {}.load(cwd);

        auto promptBuilder = agent::SystemPromptBuilder {};
        promptBuilder.setWorkingDirectory(cwd.string());
        promptBuilder.setShellInfo("endo");
        promptBuilder.setProjectRules(projectContext.rulesFiles);
        promptBuilder.setGlobalRules(projectContext.globalRules);
        promptBuilder.setMemoryFiles(projectContext.memoryFiles);
        promptBuilder.setFileTree(projectContext.fileTree);

        auto const cwdStr = cwd.string();
#if defined(_WIN32)
        auto const gitBranch = runCommandCapture("git -C " + cwdStr + " rev-parse --abbrev-ref HEAD 2>NUL");
#else
        auto const gitBranch =
            runCommandCapture("git -C " + cwdStr + " rev-parse --abbrev-ref HEAD 2>/dev/null");
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

        // Tilde-contract the project path for display
        auto projectPath = cwd.string();
        if (auto const* home = std::getenv("HOME"); home && projectPath.starts_with(home))
        {
            auto contracted = "~" + projectPath.substr(std::strlen(home));
            if (contracted.size() == 1 || contracted[1] == '/')
                projectPath = std::move(contracted);
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
        explorePromptBuilder.setWorkingDirectory(cwd.string());
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

void Shell::runAgentMode()
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
        out.writeText("No AI provider configured or authenticated.\n", errorStyle);
        out.writeText("Run `endo agent login` or configure a provider in ~/.config/endo/init.endo.\n",
                      mutedStyle);
        out.flush();
        return;
    }

    // Create or reuse agent session (preserves conversation history)
    if (!_agentSession)
        _agentSession = std::make_unique<agent::AgentSession>(*provider);

    // Load persisted conversation history from project-local store.
    auto const historyStore = agent::ConversationHistoryStore(".endo/agent-history.json");
    auto historyProvider = std::make_unique<agent::AgentHistoryProvider>();
    if (auto loaded = historyStore.load(); loaded.has_value() && !loaded->empty())
    {
        for (auto const& msg: *loaded)
        {
            if (msg.role == agent::Role::User)
            {
                auto const text = msg.textContent();
                if (!text.empty())
                    historyProvider->addEntry(text);
            }
        }
        _agentSession->loadPersistedMessages(std::move(*loaded));
    }
    auto* historyProviderPtr = historyProvider.get();

    auto const saveHistory = [&] {
        (void) historyStore.save(_agentSession->history().messages());
    };

    // Set up tool registry with built-in tools
    auto toolRegistry = agent::ToolRegistry {};

    auto const shellPath = [&]() -> std::string {
        if (access("/bin/bash", X_OK) == 0)
            return "/bin/bash";
        if (access("/usr/bin/bash", X_OK) == 0)
            return "/usr/bin/bash";
        return "/bin/sh";
    }();

    auto shellExecCb = [shellPath](std::string const& command,
                                   std::chrono::milliseconds timeout) -> agent::ShellExecResult {
        auto pipeFds = std::array<int, 2> {};
        if (pipe(pipeFds.data()) != 0)
            return agent::ShellExecResult { .output = "Failed to create pipe", .exitCode = -1 };

        auto const pid = fork();
        if (pid < 0)
        {
            close(pipeFds[0]);
            close(pipeFds[1]);
            return agent::ShellExecResult { .output = "Failed to fork process", .exitCode = -1 };
        }

        if (pid == 0)
        {
            close(pipeFds[0]);
            dup2(pipeFds[1], STDOUT_FILENO);
            dup2(pipeFds[1], STDERR_FILENO);
            close(pipeFds[1]);

            sigset_t mask;
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
            return agent::ShellExecResult { .output = std::move(output), .exitCode = -1, .timedOut = true };
        }

        auto status = 0;
        waitpid(pid, &status, 0);
        auto const exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

        return agent::ShellExecResult { .output = std::move(output), .exitCode = exitCode };
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
        dup2(savedStdout, STDOUT_FILENO);
        dup2(savedStderr, STDERR_FILENO);
        close(savedStdout);
        close(savedStderr);

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
            tracePath = *_agentTracePath;
        else if (!agentConfig.trace.defaultPath.empty())
            tracePath = agentConfig.trace.defaultPath;
        else
        {
            auto const now = std::chrono::system_clock::now();
            auto const timestamp =
                std::format("{:%Y%m%d-%H%M%S}", std::chrono::floor<std::chrono::seconds>(now));
            tracePath = ".endo/agent-trace-" + timestamp + ".jsonl";
        }

        auto tracerResult = agent::AgentTracer::create(tracePath);
        if (tracerResult.has_value())
        {
            agentTracer.emplace(std::move(*tracerResult));
            auto const tracerModelInfo = provider->modelInfo();
            agentTracer->writeSessionHeader(tracerModelInfo.providerName, tracerModelInfo.modelName);
            _agentSession->setTracer(&*agentTracer);
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

    worker.start();

    // --- Agent input loop ---
    auto& terminal = prompt.terminal();
    auto& out = terminal.output();
    auto const& theme = tui::currentTheme();

    // Connect wakeup to terminal input so poll() wakes on agent messages.
    terminal.input().setWakeup(&_agentWakeup);

    // Track the active renderer for tool-use line re-rendering of spinner.
    agent::AgentResponseRenderer* activeRenderer = nullptr;

    // Create inline Screen with AgentInputComponent
    auto screenConfig = tui::ScreenConfig {
        .viewport = tui::Viewport::Inline,
        .inhibitReflow = true,
    };
    auto screen = tui::Screen(terminal, screenConfig);
    auto inputComponent = agent::AgentInputComponent {};
    inputComponent.setTopPadding(prompt.promptConfig().promptSpacing);
    inputComponent.setPromptIndicator(agentConfig.promptIndicator);
    auto const modelInfo = provider->modelInfo();
    inputComponent.setProviderName(modelInfo.providerName);
    inputComponent.setModelName(modelInfo.modelName);

    // Set up slash command registry
    auto slashRegistry = agent::SlashCommandRegistry {};
    agent::registerBuiltinSlashCommands(slashRegistry);

    slashRegistry.registerCommand(std::make_unique<agent::CallbackSlashCommand>(
        "reset", "Clear conversation history", [&](std::string_view) -> agent::SlashCommandResult {
            _agentSession->reset();
            (void) historyStore.remove();
            historyProviderPtr->setEntries({});
            return agent::DirectOutput { .text = "Conversation history cleared.\n" };
        }));

    slashRegistry.registerCommand(std::make_unique<agent::CallbackSlashCommand>(
        "tools",
        "List all active agent tools",
        [&toolRegistry](std::string_view) -> agent::SlashCommandResult {
            auto defs = toolRegistry.definitions();
            std::ranges::sort(defs, {}, &agent::ToolDefinition::name);
            auto md = std::string { "| Tool | Description |\n|:-----|:------------|\n" };
            for (auto const& def: defs)
                md += std::format("| {} | {} |\n", def.name, def.description);
            md += std::format("\n{} tools registered.\n", toolRegistry.size());
            return agent::MarkdownOutput { .markdown = std::move(md) };
        }));

    inputComponent.addCompletionProvider(std::make_unique<agent::SlashCommandCompleter>(slashRegistry));
    auto filePathProvider = std::make_unique<agent::FilePathCompleter>();
    auto* filePathProviderPtr = filePathProvider.get();
    inputComponent.addCompletionProvider(std::move(filePathProvider));
    inputComponent.addCompletionProvider(std::move(historyProvider));

    for (auto const& entry: historyProviderPtr->entries())
        inputComponent.inputField().addHistory(entry);

    auto const prefSize = inputComponent.preferredSize();
    inputComponent.setArea(tui::Rect { 0, 0, terminal.columns(), prefSize.height });
    screen.root().addChild(inputComponent);
    screen.setFocus(&inputComponent);
    screen.invalidate();
    screen.draw();
    out.flush();

    // Launch background context loading.
    auto const cwd = std::filesystem::current_path();
    auto cachedCtx = (_cachedProjectContextCwd == cwd) ? std::move(_cachedProjectContext) : std::nullopt;
    _cachedProjectContext.reset();
    auto contextFuture =
        std::async(std::launch::async, buildAgentContext, agentConfig, cwd, std::move(cachedCtx));
    auto systemPromptReady = false;
    auto planModeActive = false;

    // --- Streaming state ---
    // When the worker is processing, we track the renderer and scroll region for the response.
    auto streaming = false;
    auto streamCancelled = false;
    std::optional<agent::AgentResponseRenderer> currentRenderer;
    auto scrollGuard = ScrollRegionGuard(out);
    auto streamResponseLineCount = 0;
    auto streamPromptHeight = 0;

    // Helper: manage scroll region for streaming response with prompt visible below.
    auto updateScrollRegion = [&](int lineCount) {
        streamResponseLineCount = lineCount;
        streamPromptHeight = inputComponent.preferredSize().height;

        if (streamPromptHeight >= terminal.rows())
            return;

        auto const totalNeeded = streamResponseLineCount + streamPromptHeight;
        if (!scrollGuard.active && totalNeeded >= terminal.rows())
        {
            auto const scrollBottom = terminal.rows() - streamPromptHeight;
            if (scrollBottom >= 1)
            {
                out.setScrollRegion(1, scrollBottom);
                scrollGuard.active = true;
            }
        }
    };

    // Helper: teardown streaming state after response completes.
    auto teardownStreaming = [&] {
        if (scrollGuard.active)
        {
            out.resetScrollRegion();
            scrollGuard.active = false;
            out.moveTo(terminal.rows(), 1);
            out.linefeed();
        }
        out.flush();
        streaming = false;
        streamCancelled = false;
        currentRenderer.reset();
        activeRenderer = nullptr;
        streamResponseLineCount = 0;
        streamPromptHeight = 0;
    };

    // --- Main event loop ---
    while (true)
    {
        // 1. Drain agent outbound messages (non-blocking).
        auto agentMessages = std::vector<agent::FromAgentMessage> {};
        agentOutbound.drainTo(agentMessages);

        for (auto& agentMsg: agentMessages)
        {
            std::visit(
                [&](auto& m) {
                    using T = std::decay_t<decltype(m)>;
                    if constexpr (std::is_same_v<T, agent::ThinkingStartMessage>)
                    {
                        streaming = true;
                        streamCancelled = false;
                        currentRenderer.emplace(out);
                        activeRenderer = &*currentRenderer;
                        currentRenderer->setLineCallback(
                            [&](int lineCount) { updateScrollRegion(lineCount); });
                        currentRenderer->begin();
                    }
                    else if constexpr (std::is_same_v<T, agent::TokenMessage>)
                    {
                        if (currentRenderer)
                            currentRenderer->feedToken(m.token);
                    }
                    else if constexpr (std::is_same_v<T, agent::ToolStatusMessage>)
                    {
                        if (agentConfig.logToolUses)
                        {
                            auto const barStyle = tui::Style { .fg = theme.agentColors.leftBar };
                            auto const toolNameStyle =
                                tui::Style { .fg = theme.agentColors.leftBar, .bold = true };
                            auto const argsStyle = tui::Style { .fg = theme.agentColors.statusText };
                            auto const shellPromptStyle =
                                tui::Style { .fg = theme.colors.accent, .bold = true };
                            auto const shellCommandStyle = tui::Style { .fg = theme.colors.text };

                            out.carriageReturn();
                            out.clearToEndOfLine();

                            auto const [prefix, text] = formatToolStatusLine(m.call);

                            out.writeText("\u2502 ", barStyle);
                            if (m.call.name == "shell_execute" || m.call.name == "endo_execute")
                            {
                                out.writeText(prefix, shellPromptStyle);
                                out.writeText(text, shellCommandStyle);
                            }
                            else
                            {
                                out.writeText(prefix, toolNameStyle);
                                if (!text.empty())
                                    out.writeText(text, argsStyle);
                            }
                            out.linefeed();

                            if (activeRenderer && activeRenderer->isThinking())
                                activeRenderer->renderSpinner();
                            out.flush();
                        }
                    }
                    else if constexpr (std::is_same_v<T, agent::CompletionMessage>)
                    {
                        if (currentRenderer)
                            currentRenderer->end();
                        teardownStreaming();
                        saveHistory();

                        if (!m.success)
                        {
                            if (!streamCancelled)
                            {
                                auto const errorStyle = tui::Style { .fg = theme.agentColors.errorText };
                                out.writeText("Error: " + m.errorMessage + "\n", errorStyle);
                                out.flush();
                            }
                            else
                            {
                                auto const infoStyle = tui::Style { .fg = theme.agentColors.statusText };
                                out.writeText("(cancelled)\n", infoStyle);
                                out.flush();
                            }
                        }

                        // Re-render input component for next query.
                        auto const newPrefSize = inputComponent.preferredSize();
                        inputComponent.setArea(tui::Rect { 0, 0, terminal.columns(), newPrefSize.height });
                        screen.draw();
                    }
                    else if constexpr (std::is_same_v<T, agent::AskUserRequest>)
                    {
                        // TODO: Render question in TUI and collect answer.
                        // For now, fall back to console I/O (preserving current behavior).
                        std::println(stderr, "\n{}", m.question.text);
                        if (!m.question.options.empty())
                        {
                            for (size_t i = 0; i < m.question.options.size(); ++i)
                                std::println(stderr, "  {}. {}", i + 1, m.question.options[i]);
                            std::print(stderr, "Choice (1-{}): ", m.question.options.size());
                        }
                        else
                        {
                            std::print(stderr, "> ");
                        }
                        auto answer = std::string {};
                        auto cancelled = false;
                        if (!std::getline(std::cin, answer))
                            cancelled = true;
                        if (!cancelled && !m.question.options.empty())
                        {
                            try
                            {
                                if (auto const idx = std::stoi(answer) - 1;
                                    idx >= 0 && static_cast<size_t>(idx) < m.question.options.size())
                                    answer = m.question.options[static_cast<size_t>(idx)];
                            }
                            catch (std::exception const&)
                            {
                            }
                        }
                        worker.inbound().push(agent::UserAnswerMessage {
                            .requestId = m.requestId,
                            .answer =
                                agent::UserAnswer { .answer = std::move(answer), .cancelled = cancelled } });
                    }
                    else if constexpr (std::is_same_v<T, agent::PlanGeneratedMessage>)
                    {
                        if (currentRenderer)
                            currentRenderer->renderPlan(m.plan);
                        // TODO: Wait for user plan approval (y/n/r) and execute.
                    }
                    else if constexpr (std::is_same_v<T, agent::AgentShutdownComplete>)
                    {
                        // Worker thread exited. Clean up if needed.
                    }
                },
                agentMsg);
        }

        // 2. Determine poll timeout.
        auto pollTimeout = 80; // Default: 80ms for ghost text and spinner.
        if (streaming)
            pollTimeout = 5; // Fast polling during streaming for responsive cancellation.
        else
        {
            auto const ghostTimeout = inputComponent.ghostTextTimeoutMs();
            if (ghostTimeout >= 0)
                pollTimeout = std::min(ghostTimeout, 80);
        }

        // 3. Poll terminal input.
        auto events = terminal.poll(pollTimeout);

        if (events.empty())
        {
            // Check background context loading.
            if (!systemPromptReady
                && contextFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            {
                auto result = contextFuture.get();
                _agentSession->setSystemPrompt(std::move(result.systemPrompt));
                if (auto* explore = dynamic_cast<agent::ExploreTool*>(toolRegistry.findTool("explore")))
                    explore->setSystemPrompt(std::move(result.exploreSystemPrompt));
                if (!result.gitBranch.empty())
                    inputComponent.setGitBranch(std::move(result.gitBranch));
                if (!result.projectPath.empty())
                    inputComponent.setProjectPath(std::move(result.projectPath));
                filePathProviderPtr->setFilePaths(result.projectContext.filePaths);
                _cachedProjectContext = std::move(result.projectContext);
                _cachedProjectContextCwd = cwd;
                systemPromptReady = true;
                auto const newPrefSize = inputComponent.preferredSize();
                inputComponent.setArea(tui::Rect { 0, 0, terminal.columns(), newPrefSize.height });
                screen.draw();
            }

            // Tick spinner during thinking phase.
            if (activeRenderer && activeRenderer->isThinking())
            {
                if (activeRenderer->tickSpinner())
                {
                    activeRenderer->renderSpinner();
                    out.flush();
                }
            }

            // Ghost text debounce.
            if (!streaming)
            {
                inputComponent.flushDeferredUpdates();
                if (inputComponent.inputField().hasGhostText() || inputComponent.ghostTextTimeoutMs() >= 0)
                {
                    auto const newPrefSize = inputComponent.preferredSize();
                    inputComponent.setArea(tui::Rect { 0, 0, terminal.columns(), newPrefSize.height });
                    screen.draw();
                }
            }
            continue;
        }

        // 4. Process terminal input events.
        auto needsRedraw = false;
        for (auto const& event: events)
        {
            if (std::holds_alternative<tui::ResizeEvent>(event))
            {
                if (scrollGuard.active)
                {
                    out.resetScrollRegion();
                    scrollGuard.active = false;
                    // Re-evaluate scroll region with new dimensions.
                    updateScrollRegion(streamResponseLineCount);
                }
                needsRedraw = true;
                continue;
            }

            if (auto const* key = std::get_if<tui::KeyEvent>(&event); key && tui::isModifierOnlyKey(key->key))
                continue;

            // During streaming, only handle Escape (cancel) and Ctrl+L (clear).
            if (streaming)
            {
                auto const action = inputComponent.processInput(event);
                switch (action)
                {
                    case agent::AgentInputComponent::Action::Abort:
                        streamCancelled = true;
                        worker.inbound().push(agent::CancelMessage {});
                        break;
                    case agent::AgentInputComponent::Action::ClearScreen:
                        if (scrollGuard.active)
                        {
                            out.resetScrollRegion();
                            scrollGuard.active = false;
                        }
                        out.clearScreen();
                        out.flush();
                        streamResponseLineCount = 0;
                        break;
                    case agent::AgentInputComponent::Action::Changed:
                        // User typed during streaming — input is captured for next query.
                        break;
                    default: break;
                }
                continue;
            }

            // Not streaming — handle full input.
            auto const action = inputComponent.processInput(event);
            switch (action)
            {
                case agent::AgentInputComponent::Action::Submit: {
                    // Ensure system prompt is ready.
                    if (!systemPromptReady)
                    {
                        auto result = contextFuture.get();
                        _agentSession->setSystemPrompt(std::move(result.systemPrompt));
                        if (auto* explore =
                                dynamic_cast<agent::ExploreTool*>(toolRegistry.findTool("explore")))
                            explore->setSystemPrompt(std::move(result.exploreSystemPrompt));
                        if (!result.gitBranch.empty())
                            inputComponent.setGitBranch(std::move(result.gitBranch));
                        if (!result.projectPath.empty())
                            inputComponent.setProjectPath(std::move(result.projectPath));
                        filePathProviderPtr->setFilePaths(result.projectContext.filePaths);
                        _cachedProjectContext = std::move(result.projectContext);
                        _cachedProjectContextCwd = cwd;
                        systemPromptReady = true;
                    }

                    auto const query = std::string(inputComponent.text());

                    if (!query.starts_with("/"))
                    {
                        inputComponent.inputField().addHistory(query);
                        historyProviderPtr->addEntry(query);
                    }

                    // Move cursor past the input component.
                    auto const totalLines = inputComponent.inputField().lineCount();
                    auto const cursorLine = inputComponent.inputField().cursorLine();
                    inputComponent.clear();
                    auto const linesToMoveDown = totalLines - cursorLine;
                    if (linesToMoveDown > 0)
                        out.moveDown(linesToMoveDown);
                    out.carriageReturn();
                    out.linefeed();
                    out.flush();

                    screen.releaseCursor();

                    // Dispatch slash commands.
                    if (query.starts_with("/"))
                    {
                        auto const spacePos = query.find(' ');
                        auto const cmdName =
                            query.substr(1, spacePos == std::string::npos ? std::string::npos : spacePos - 1);
                        auto const args = spacePos != std::string::npos ? query.substr(spacePos + 1) : "";

                        if (auto const* cmd = slashRegistry.findCommand(cmdName))
                        {
                            auto commandResult = cmd->execute(args);
                            if (auto const* d = std::get_if<agent::DirectOutput>(&commandResult))
                            {
                                out.writeText(d->text);
                                out.flush();
                            }
                            else if (auto const* m = std::get_if<agent::MarkdownOutput>(&commandResult))
                            {
                                auto mdRenderer = tui::MarkdownRenderer(out);
                                mdRenderer.setMaxWidth(terminal.columns());
                                mdRenderer.render(m->markdown);
                                out.flush();
                            }
                            else if (auto const* p = std::get_if<agent::PlanModeRequest>(&commandResult))
                            {
                                if (!agentConfig.planMode.enabled)
                                {
                                    auto const errorStyle = tui::Style { .fg = theme.agentColors.errorText };
                                    out.writeText("Plan mode is disabled in configuration.\n", errorStyle);
                                    out.flush();
                                }
                                else if (p->query.empty())
                                {
                                    if (!planModeActive)
                                    {
                                        planModeActive = true;
                                        inputComponent.setPlanMode(true);
                                    }
                                    auto const infoStyle = tui::Style { .fg = theme.agentColors.statusText };
                                    out.writeText("Plan mode active. Type your task to generate a plan.\n",
                                                  infoStyle);
                                    out.flush();
                                }
                                else
                                {
                                    // Send plan query to worker.
                                    worker.inbound().push(
                                        agent::UserPromptMessage { .text = p->query, .planMode = true });
                                }
                            }
                            else if (auto const* r = std::get_if<agent::PromptRewrite>(&commandResult))
                            {
                                // Send rewritten prompt to worker.
                                worker.inbound().push(agent::UserPromptMessage { .text = r->prompt });
                            }
                        }
                        else
                        {
                            auto const errorStyle = tui::Style { .fg = theme.agentColors.errorText };
                            out.writeText("Unknown command: /" + cmdName + "\n", errorStyle);
                            out.flush();
                        }
                    }
                    else if (planModeActive && agentConfig.planMode.enabled)
                    {
                        // Plan mode: send to worker with planMode flag.
                        worker.inbound().push(agent::UserPromptMessage { .text = query, .planMode = true });
                        saveHistory();
                    }
                    else
                    {
                        // Normal message: send to worker.
                        worker.inbound().push(agent::UserPromptMessage { .text = query });
                    }

                    // Re-render input component.
                    auto const newPrefSize = inputComponent.preferredSize();
                    inputComponent.setArea(tui::Rect { 0, 0, terminal.columns(), newPrefSize.height });
                    screen.draw();
                    break;
                }
                case agent::AgentInputComponent::Action::Abort: {
                    // Stop worker before exiting agent mode.
                    worker.stop();
                    _agentSession->setToolRegistry(nullptr);
                    _agentSession->setToolStatusCallback(nullptr);
                    _agentSession->setTracer(nullptr);
                    terminal.input().setWakeup(nullptr);
                    screen.clearAndRelease();
                    return;
                }
                case agent::AgentInputComponent::Action::CycleMode: {
                    planModeActive = !planModeActive;
                    inputComponent.setPlanMode(planModeActive);
                    needsRedraw = true;
                    break;
                }
                case agent::AgentInputComponent::Action::ClearScreen: {
                    out.clearScreen();
                    out.flush();
                    screen.releaseCursor();
                    needsRedraw = true;
                    break;
                }
                case agent::AgentInputComponent::Action::Changed: needsRedraw = true; break;
                case agent::AgentInputComponent::Action::None: break;
            }
        }

        if (needsRedraw && !streaming)
        {
            inputComponent.flushDeferredUpdates();
            auto const newPrefSize = inputComponent.preferredSize();
            inputComponent.setArea(tui::Rect { 0, 0, terminal.columns(), newPrefSize.height });
            screen.draw();
        }
    }
}

} // namespace endo
