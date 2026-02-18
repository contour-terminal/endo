// SPDX-License-Identifier: Apache-2.0
#include "Shell.hpp"

#include <endo-language/ASTPrinter.hpp>
#include <endo-language/IRGenerator.hpp>
#include <endo-language/Lexer.hpp>
#include <endo-language/LogCategories.hpp>
#include <endo-language/LogConfig.hpp>
#include <endo-language/Parser.hpp>

#include <tui/Screen.hpp>
#include <tui/Theme.hpp>

#include <CoreVM/CoreVM.hpp>
#include <CoreVM/types/TypeDescriptor.hpp>

#include <crispy/assert.h>

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
#include "Pipe.hpp"
#include "Platform.hpp"
#include "Process.hpp"
#include "Prompt.hpp"
#include "SignalHandler.hpp"
#include "TTY.hpp"
#include <agent/AgentConfig.hpp>
#include <agent/AgentInputComponent.hpp>
#include <agent/AgentResponseRenderer.hpp>
#include <agent/AgentSession.hpp>
#include <agent/PlanExecutor.hpp>
#include <agent/ProjectContextLoader.hpp>
#include <agent/SlashCommandCompleter.hpp>
#include <agent/SlashCommandRegistry.hpp>
#include <agent/SlashCommands.hpp>
#include <agent/SystemPromptBuilder.hpp>
#include <agent/providers/ProviderFactory.hpp>
#include <agent/tools/EditFileTool.hpp>
#include <agent/tools/GitTool.hpp>
#include <agent/tools/GlobTool.hpp>
#include <agent/tools/GrepTool.hpp>
#include <agent/tools/ReadFileTool.hpp>
#include <agent/tools/SaveMemoryTool.hpp>
#include <agent/tools/ShellExecuteTool.hpp>
#include <agent/tools/SubmitPlanTool.hpp>
#include <agent/tools/ToolRegistry.hpp>
#include <agent/tools/WriteFileTool.hpp>
#include <nlohmann/json.hpp>
#if defined(_WIN32)
    #include "platform/WindowsEnvironmentProvider.hpp"
#else
    #include <sys/wait.h>

    #include <fcntl.h>
    #include <poll.h>
    #include <unistd.h>

    #include "platform/PosixEnvironmentProvider.hpp"
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

void Shell::setInteractive(bool interactive)
{
    _interactive = interactive;
}

void Shell::setPositionalParameters(std::vector<std::string> params)
{
    _positionalParameters = std::move(params);
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
                execute(content);
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
                prompt.resume();
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
            prompt.resume();
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

        return AgentContextResult {
            .systemPrompt = promptBuilder.build(),
            .gitBranch = gitBranch,
            .projectPath = std::move(projectPath),
            .projectContext = std::move(projectContext),
        };
    }

} // namespace

void Shell::runAgentMode()
{
    auto const agentConfig = agent::loadAgentConfig();

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
        out.write("No AI provider configured or authenticated.\n", errorStyle);
        out.write("Create ~/.config/endo/agent.yml or set ANTHROPIC_API_KEY / OPENAI_API_KEY.\n", mutedStyle);
        out.flush();
        return;
    }

    // Create or reuse agent session (preserves conversation history)
    if (!_agentSession)
        _agentSession = std::make_unique<agent::AgentSession>(*provider);

    // Set up tool registry with built-in tools
    auto toolRegistry = agent::ToolRegistry {};

    auto shellExecCb = [](std::string const& command,
                          std::chrono::milliseconds timeout) -> agent::ShellExecResult {
        auto const timeoutSec = std::chrono::duration_cast<std::chrono::seconds>(timeout).count();
        auto const fullCommand = std::format("{} 2>&1", command);
        auto* pipe = popen(fullCommand.c_str(), "r");
        if (!pipe)
            return agent::ShellExecResult { .output = "Failed to execute command", .exitCode = -1 };

        auto output = std::string {};
        auto buffer = std::array<char, 4096> {};
        while (auto const bytesRead = fread(buffer.data(), 1, buffer.size(), pipe))
            output.append(buffer.data(), bytesRead);

        auto const status = pclose(pipe);
        auto const exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

        return agent::ShellExecResult { .output = std::move(output), .exitCode = exitCode };
    };

    toolRegistry.registerTool(std::make_unique<agent::ReadFileTool>());
    toolRegistry.registerTool(std::make_unique<agent::WriteFileTool>());
    toolRegistry.registerTool(std::make_unique<agent::EditFileTool>());
    toolRegistry.registerTool(std::make_unique<agent::GlobTool>());
    toolRegistry.registerTool(std::make_unique<agent::GrepTool>());
    toolRegistry.registerTool(std::make_unique<agent::ShellExecuteTool>(shellExecCb));
    toolRegistry.registerTool(std::make_unique<agent::GitTool>(shellExecCb));
    toolRegistry.registerTool(
        std::make_unique<agent::SaveMemoryTool>([this]() { _cachedProjectContext.reset(); }));
    toolRegistry.registerTool(std::make_unique<agent::SubmitPlanTool>());

    _agentSession->setToolRegistry(&toolRegistry);
    _agentSession->setMaxToolResultSize(agentConfig.maxToolResultSize);
    _agentSession->setMaxExplorationIterations(agentConfig.planMode.maxExplorationTurns);

    // Agent input loop
    auto& terminal = prompt.terminal();
    auto& out = terminal.output();
    auto const& theme = tui::currentTheme();

    // Track the active renderer so tool-use lines can re-render the spinner
    agent::AgentResponseRenderer* activeRenderer = nullptr;

    // Set up tool-use logging callback
    if (agentConfig.logToolUses)
    {
        auto const barStyle = tui::Style { .fg = theme.agentColors.leftBar };
        auto const toolNameStyle = tui::Style { .fg = theme.agentColors.leftBar, .bold = true };
        auto const argsStyle = tui::Style { .fg = theme.agentColors.statusText };

        _agentSession->setToolStatusCallback(
            [&out, &activeRenderer, barStyle, toolNameStyle, argsStyle](agent::ToolCall const& call) {
                // Clear spinner line
                out.writeRaw("\r");
                out.clearToEndOfLine();

                // Write styled tool use line: "│ ⚙ tool_name { args... }"
                out.write("\u2502 ", barStyle);
                out.write("\xe2\x9a\x99 " + std::string(call.name), toolNameStyle);
                if (auto const args = formatToolCallArgs(call.arguments); !args.empty())
                    out.write(" " + args, argsStyle);
                out.writeRaw("\n");

                // Re-render spinner if still in thinking phase
                if (activeRenderer && activeRenderer->isThinking())
                    activeRenderer->renderSpinner();
                out.flush();
            });
    }

    // Create inline Screen with AgentInputComponent — prompt visible instantly
    auto screenConfig = tui::ScreenConfig {
        .viewport = tui::Viewport::Inline,
        .inhibitReflow = true,
    };
    auto screen = tui::Screen(terminal, screenConfig);
    auto inputComponent = agent::AgentInputComponent {};
    inputComponent.setPromptIndicator(agentConfig.promptIndicator);
    auto const modelInfo = provider->modelInfo();
    inputComponent.setProviderName(modelInfo.providerName);
    inputComponent.setModelName(modelInfo.modelName);

    // Set up slash command registry with built-in commands and completion
    auto slashRegistry = agent::SlashCommandRegistry {};
    agent::registerBuiltinSlashCommands(slashRegistry);
    inputComponent.addCompletionProvider(std::make_unique<agent::SlashCommandCompleter>(slashRegistry));

    auto const prefSize = inputComponent.preferredSize();
    inputComponent.setArea(tui::Rect { 0, 0, terminal.columns(), prefSize.height });
    screen.root().addChild(inputComponent);
    screen.setFocus(&inputComponent);
    screen.invalidate();
    screen.draw();
    out.flush();

    // Launch background thread for heavy context loading (git queries, project file scanning).
    // Reuse cached project context if cwd hasn't changed (avoids re-scanning file tree/rules/memory).
    auto const cwd = std::filesystem::current_path();
    auto cachedCtx = (_cachedProjectContextCwd == cwd) ? std::move(_cachedProjectContext) : std::nullopt;
    _cachedProjectContext.reset();
    auto contextFuture =
        std::async(std::launch::async, buildAgentContext, agentConfig, cwd, std::move(cachedCtx));
    auto systemPromptReady = false;
    auto planModeActive = false;

    // Lambda to handle plan exploration and user decision after a plan is generated.
    // Used by both `/plan <task>` and persistent plan mode.
    auto executePlanFlow = [&](std::string const& planQuery) {
        auto renderer = agent::AgentResponseRenderer(out);
        auto const rendererGuard = ScopedAssign(activeRenderer, renderer);
        renderer.begin();

        auto planResult = _agentSession->processMessageForPlan(
            planQuery, [&](std::string_view token) { renderer.feedToken(token); });

        renderer.end();

        if (!planResult.has_value())
        {
            auto const errorStyle = tui::Style { .fg = theme.agentColors.errorText };
            out.write("Error: " + planResult.error().message + "\n", errorStyle);
            out.flush();
            return;
        }

        // Render plan for review
        renderer.renderPlan(*planResult);

        // Wait for user decision
        auto decided = false;
        while (!decided)
        {
            auto decisionEvents = terminal.poll(0);
            for (auto const& decisionEvent: decisionEvents)
            {
                auto const* keyEvent = std::get_if<tui::KeyEvent>(&decisionEvent);
                if (!keyEvent)
                    continue;

                if (keyEvent->codepoint == U'y' || keyEvent->codepoint == U'Y')
                {
                    auto executor = agent::PlanExecutor(*_agentSession, std::move(*planResult));
                    while (!executor.isComplete())
                    {
                        renderer.renderPlanProgress(executor.plan(), executor.currentStepIndex());

                        auto stepRenderer = agent::AgentResponseRenderer(out);
                        auto const stepGuard = ScopedAssign(activeRenderer, stepRenderer);
                        stepRenderer.begin();

                        auto stepResult = executor.executeNextStep(
                            [&](std::string_view token) { stepRenderer.feedToken(token); });

                        stepRenderer.end();

                        if (!stepResult.has_value())
                        {
                            auto const errorStyle = tui::Style { .fg = theme.agentColors.errorText };
                            out.write("Step failed: " + stepResult.error().message + "\n", errorStyle);
                            out.flush();
                            break;
                        }

                        if (agentConfig.planMode.pauseBetweenSteps && !executor.isComplete())
                        {
                            auto const labelStyle = tui::Style { .fg = theme.agentColors.statusText };
                            out.write("Press any key to continue...\n", labelStyle);
                            out.flush();
                            (void) terminal.poll(0);
                        }
                    }

                    renderer.renderPlanProgress(executor.plan(), executor.currentStepIndex());
                    decided = true;
                }
                else if (keyEvent->codepoint == U'n' || keyEvent->codepoint == U'N')
                {
                    auto const labelStyle = tui::Style { .fg = theme.agentColors.statusText };
                    out.write("Plan rejected.\n", labelStyle);
                    out.flush();
                    decided = true;
                }
                else if (keyEvent->codepoint == U'r' || keyEvent->codepoint == U'R')
                {
                    auto const labelStyle = tui::Style { .fg = theme.agentColors.statusText };
                    out.write("Enter revision feedback:\n", labelStyle);
                    out.flush();
                    decided = true;
                }
            }
        }
    };

    while (true)
    {
        auto const spinnerTimeout = 80; // ms for spinner animation if we add one later
        auto events = terminal.poll(spinnerTimeout);

        if (events.empty())
        {
            // Check if background context loading has completed
            if (!systemPromptReady
                && contextFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            {
                auto result = contextFuture.get();
                _agentSession->setSystemPrompt(std::move(result.systemPrompt));
                if (!result.gitBranch.empty())
                    inputComponent.setGitBranch(std::move(result.gitBranch));
                if (!result.projectPath.empty())
                    inputComponent.setProjectPath(std::move(result.projectPath));
                _cachedProjectContext = std::move(result.projectContext);
                _cachedProjectContextCwd = cwd;
                systemPromptReady = true;
                auto const newPrefSize = inputComponent.preferredSize();
                inputComponent.setArea(tui::Rect { 0, 0, terminal.columns(), newPrefSize.height });
                screen.draw();
            }
            continue;
        }

        auto needsRedraw = false;
        for (auto const& event: events)
        {
            if (std::holds_alternative<tui::ResizeEvent>(event))
            {
                needsRedraw = true;
                continue;
            }

            auto const action = inputComponent.processInput(event);
            switch (action)
            {
                case agent::AgentInputComponent::Action::Submit: {
                    // Ensure system prompt is ready before sending a message
                    if (!systemPromptReady)
                    {
                        auto result = contextFuture.get();
                        _agentSession->setSystemPrompt(std::move(result.systemPrompt));
                        if (!result.gitBranch.empty())
                            inputComponent.setGitBranch(std::move(result.gitBranch));
                        if (!result.projectPath.empty())
                            inputComponent.setProjectPath(std::move(result.projectPath));
                        _cachedProjectContext = std::move(result.projectContext);
                        _cachedProjectContextCwd = cwd;
                        systemPromptReady = true;
                    }

                    auto const query = std::string(inputComponent.text());

                    // Move cursor past the input component while text is still visible
                    auto const totalLines = inputComponent.inputField().lineCount();
                    auto const cursorLine = inputComponent.inputField().cursorLine();
                    inputComponent.clear();
                    auto const linesToMoveDown = totalLines - cursorLine;
                    if (linesToMoveDown > 0)
                        out.moveDown(linesToMoveDown);
                    out.writeRaw("\r\n");
                    out.flush();

                    // Release the screen before streaming the response
                    screen.releaseCursor();

                    // Dispatch slash commands via registry
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
                                out.write(d->text);
                                out.flush();
                            }
                            else if (auto const* p = std::get_if<agent::PlanModeRequest>(&commandResult))
                            {
                                if (!agentConfig.planMode.enabled)
                                {
                                    auto const errorStyle = tui::Style { .fg = theme.agentColors.errorText };
                                    out.write("Plan mode is disabled in configuration.\n", errorStyle);
                                    out.flush();
                                }
                                else if (p->query.empty())
                                {
                                    // Idempotently enter plan mode
                                    if (!planModeActive)
                                    {
                                        planModeActive = true;
                                        inputComponent.setPlanMode(true);
                                    }
                                    auto const infoStyle = tui::Style { .fg = theme.agentColors.statusText };
                                    out.write("Plan mode active. Type your task to generate a plan.\n",
                                              infoStyle);
                                    out.flush();
                                }
                                else
                                {
                                    executePlanFlow(p->query);
                                }
                            }
                            else if (auto const* r = std::get_if<agent::PromptRewrite>(&commandResult))
                            {
                                // PromptRewrite: send the rewritten prompt through normal message processing
                                auto renderer = agent::AgentResponseRenderer(out);
                                auto const rendererGuard = ScopedAssign(activeRenderer, renderer);
                                renderer.begin();

                                auto result = _agentSession->processMessage(
                                    r->prompt, [&](std::string_view token) { renderer.feedToken(token); });

                                renderer.end();

                                if (!result.has_value())
                                {
                                    auto const errorStyle = tui::Style { .fg = theme.agentColors.errorText };
                                    out.write("Error: " + result.error().message + "\n", errorStyle);
                                    out.flush();
                                }
                            }
                        }
                        else
                        {
                            auto const errorStyle = tui::Style { .fg = theme.agentColors.errorText };
                            out.write("Unknown command: /" + cmdName + "\n", errorStyle);
                            out.flush();
                        }
                    }
                    else if (planModeActive && agentConfig.planMode.enabled)
                    {
                        // Plan mode: route through plan exploration
                        executePlanFlow(query);
                    }
                    else
                    {
                        // Normal (non-slash) message processing
                        auto renderer = agent::AgentResponseRenderer(out);
                        auto const rendererGuard = ScopedAssign(activeRenderer, renderer);
                        renderer.begin();

                        auto result = _agentSession->processMessage(
                            query, [&](std::string_view token) { renderer.feedToken(token); });

                        renderer.end();

                        if (!result.has_value())
                        {
                            auto const errorStyle = tui::Style { .fg = theme.agentColors.errorText };
                            out.write("Error: " + result.error().message + "\n", errorStyle);
                            out.flush();
                        }
                    }

                    // Re-render the input component for the next query
                    auto const newPrefSize = inputComponent.preferredSize();
                    inputComponent.setArea(tui::Rect { 0, 0, terminal.columns(), newPrefSize.height });
                    screen.draw();
                    break;
                }
                case agent::AgentInputComponent::Action::Abort: {
                    // Move cursor up to the top of the agent prompt and clear from there,
                    // so the shell prompt can replace the agent prompt in-place.
                    auto const rowsUp = 1 // AgentInputComponent::HeaderHeight
                                        + inputComponent.inputField().cursorLine();
                    if (rowsUp > 0)
                        out.moveUp(rowsUp);
                    out.writeRaw("\r\033[J"); // CR + clear cursor to end of display
                    out.flush();
                    screen.releaseCursor();
                    return;
                }
                case agent::AgentInputComponent::Action::CycleMode: {
                    planModeActive = !planModeActive;
                    inputComponent.setPlanMode(planModeActive);
                    needsRedraw = true;
                    break;
                }
                case agent::AgentInputComponent::Action::Changed: needsRedraw = true; break;
                case agent::AgentInputComponent::Action::None: break;
            }
        }

        if (needsRedraw)
        {
            inputComponent.flushDeferredUpdates();
            auto const newPrefSize = inputComponent.preferredSize();
            inputComponent.setArea(tui::Rect { 0, 0, terminal.columns(), newPrefSize.height });
            screen.draw();
        }
    }
}

} // namespace endo
