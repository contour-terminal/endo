// SPDX-License-Identifier: Apache-2.0
#include "Shell.hpp"

#include <endo-language/ASTPrinter.hpp>
#include <endo-language/IRGenerator.hpp>
#include <endo-language/Lexer.hpp>
#include <endo-language/LogCategories.hpp>
#include <endo-language/LogConfig.hpp>
#include <endo-language/Parser.hpp>

#include <tui/Theme.hpp>

#include <CoreVM/CoreVM.hpp>
#include <CoreVM/types/TypeDescriptor.hpp>

#include <crispy/assert.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
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

} // namespace endo
